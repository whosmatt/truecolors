#include "beatnn.h"

#include <math.h>
#include <string.h>

#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "audio.h"
#include "fe_ctx.h"
#include "frontend.h"
#include "model_params.h"
#include "selftest_window.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

static const char *TAG = "beatnn";

// model_params.h is generated from the model file, check consistency with
// audio frontend
static_assert(MODEL_FE_SPEC_VERSION == FE_SPEC_VERSION,
              "model was trained against a different front end version");
static_assert(MODEL_INPUTS == FE_CTX_INPUTS, "context window size mismatch");
static_assert(MODEL_FEATS == FE_CTX_FEATS, "feature count mismatch");
static_assert(MODEL_LOOKAHEAD == FE_CTX_LOOKAHEAD, "lookahead mismatch");
static_assert(MODEL_BLOCK_SAMPLES == FE_BLOCK_SAMPLES, "block size mismatch");
static_assert(MODEL_SAMPLE_RATE == FE_SAMPLE_RATE, "sample rate mismatch");

extern const uint8_t model_tflite_start[] asm("_binary_model_tflite_start");
extern const uint8_t model_tflite_end[] asm("_binary_model_tflite_end");

// weights MUST be 16-byte aligned and in RAM or there is a huge silent
// performance penalty
alignas(16) static uint8_t s_arena[CONFIG_BEATNN_ARENA_KB * 1024];
alignas(16) static uint8_t s_model[MODEL_TFLITE_BYTES];

static tflite::MicroInterpreter *s_interp;
static TfLiteTensor *s_in;
static bool s_ready;
static uint32_t s_cycles;
static uint32_t s_quant_cycles;
static uint32_t s_invoke_cycles;
static uint8_t s_in_align;
static beat_infer_t s_selftest;
static bool s_selftest_pass;

static uint64_t fnv1a64(const uint8_t *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 0x100000001b3ULL;
    }
    return h;
}

esp_err_t beatnn_init(void)
{
    if (fe_variant() != MODEL_FE_VARIANT) {
        ESP_LOGE(TAG, "front end variant %u, vs model %u",
                 (unsigned)fe_variant(), (unsigned)MODEL_FE_VARIANT);
        return ESP_ERR_INVALID_VERSION;
    }

    size_t n = (size_t)(model_tflite_end - model_tflite_start);
    if (n != MODEL_TFLITE_BYTES) {
        ESP_LOGE(TAG, "model is %u bytes, header says %u",
                 (unsigned)n, (unsigned)MODEL_TFLITE_BYTES);
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(s_model, model_tflite_start, n);
    uint64_t h = fnv1a64(s_model, n);
    if (h != MODEL_TFLITE_FNV1A) {
        ESP_LOGE(TAG, "model hash %016llx != %016llx: regenerate model_params.h",
                 (unsigned long long)h, (unsigned long long)MODEL_TFLITE_FNV1A);
        return ESP_ERR_INVALID_CRC;
    }

    const tflite::Model *model = tflite::GetModel(s_model);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "schema %u != %u", (unsigned)model->version(),
                 (unsigned)TFLITE_SCHEMA_VERSION);
        return ESP_ERR_INVALID_VERSION;
    }

    static tflite::MicroMutableOpResolver<2> resolver;
    resolver.AddFullyConnected();
    resolver.AddLogistic();

    static tflite::MicroInterpreter interp(model, resolver, s_arena, sizeof(s_arena));
    if (interp.AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors failed; arena is %u bytes",
                 (unsigned)sizeof(s_arena));
        return ESP_ERR_NO_MEM;
    }
    s_interp = &interp;

    s_in = interp.input(0);
    if (s_in->type != kTfLiteInt8 || s_in->bytes != MODEL_INPUTS) {
        ESP_LOGE(TAG, "input is type %d, %u bytes", s_in->type, (unsigned)s_in->bytes);
        return ESP_ERR_INVALID_ARG;
    }
    if (interp.outputs_size() != 4 ||
        interp.output(MODEL_OUT_BEAT)->bytes != 1 ||
        interp.output(MODEL_OUT_BEAT_OFFSET)->bytes != 1 ||
        interp.output(MODEL_OUT_HIT)->bytes != 4 ||
        interp.output(MODEL_OUT_MUSIC)->bytes != 1) {
        ESP_LOGE(TAG, "unexpected output layout");
        return ESP_ERR_INVALID_ARG;
    }

    if (!esp_ptr_internal(s_arena) || !esp_ptr_internal(s_model)) {
        ESP_LOGW(TAG, "arena or weights are not in internal RAM");
    }
    ESP_LOGI(TAG, "arena %p align%zu, weights %p align%zu, input %p align%zu",
             s_arena, (uintptr_t)s_arena & 15,
             s_model, (uintptr_t)s_model & 15,
             s_in->data.int8, (uintptr_t)s_in->data.int8 & 15);
    s_in_align = (uint8_t)((uintptr_t)s_in->data.int8 & 15);
    s_ready = true;

    // selftest against golden vector
    s_selftest_pass = true;
    for (int i = 0; i < SELFTEST_N; i++) {
        beat_infer_t r = {};
        beatnn_infer(kSelftestWindows[i], &r);
        // int8 output steps are 1/256; a real mismatch is far larger.
        const float tol = 1.0f / 512.0f;
        bool ok = fabsf(r.beat - kSelftestBeat[i]) < tol &&
                  fabsf(r.beat_offset - kSelftestOffset[i]) < tol &&
                  fabsf(r.music - kSelftestMusic[i]) < tol;
        if (i == 0) {
            s_selftest = r;
        }
        if (!ok) {
            s_selftest_pass = false;
            ESP_LOGE(TAG, "selftest %d: beat %.4f want %.4f, music %.4f want %.4f",
                     i, r.beat, kSelftestBeat[i], r.music, kSelftestMusic[i]);
        }
    }
    if (!s_selftest_pass) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    audio_set_infer_hook(beatnn_infer);
    ESP_LOGI(TAG, "model ok: %u bytes, arena %u/%u, fe v%d variant %u",
             (unsigned)n, (unsigned)s_interp->arena_used_bytes(),
             (unsigned)sizeof(s_arena), FE_SPEC_VERSION, (unsigned)fe_variant());
    return ESP_OK;
}

bool beatnn_ready(void) { return s_ready; }

uint32_t beatnn_arena_used(void)
{
    return s_interp ? (uint32_t)s_interp->arena_used_bytes() : 0;
}

uint32_t beatnn_last_cycles(void) { return s_cycles; }
uint32_t beatnn_quant_cycles(void) { return s_quant_cycles; }
uint32_t beatnn_invoke_cycles(void) { return s_invoke_cycles; }
uint8_t beatnn_input_align(void) { return s_in_align; }

void beatnn_selftest_result(beat_infer_t *out) { *out = s_selftest; }
bool beatnn_selftest_passed(void) { return s_selftest_pass; }

static inline float dequant(const TfLiteTensor *t, int i)
{
    return ((int)((const int8_t *)t->data.int8)[i] - t->params.zero_point) *
           t->params.scale;
}

bool beatnn_infer(const float *window, beat_infer_t *out)
{
    if (!s_ready) {
        return false;
    }
    uint32_t t0 = esp_cpu_get_cycle_count();

    // qmul folds the per-feature scale with the input quantisation
    int8_t *q = s_in->data.int8;
    for (int f = 0; f < MODEL_FRAMES; f++) {
        const float *src = window + f * MODEL_FEATS;
        int8_t *dst = q + f * MODEL_FEATS;
        for (int k = 0; k < MODEL_FEATS; k++) {
            int v = (int)lrintf((src[k] - kModelNormMean[k]) * kModelQuantMul[k]) +
                    MODEL_IN_ZERO;
            dst[k] = (int8_t)(v < -128 ? -128 : (v > 127 ? 127 : v));
        }
    }

    s_quant_cycles = esp_cpu_get_cycle_count() - t0;
    uint32_t t1 = esp_cpu_get_cycle_count();
    if (s_interp->Invoke() != kTfLiteOk) {
        s_cycles = esp_cpu_get_cycle_count() - t0;
        return false;
    }

    s_invoke_cycles = esp_cpu_get_cycle_count() - t1;

    out->beat = dequant(s_interp->output(MODEL_OUT_BEAT), 0);
    out->beat_offset = dequant(s_interp->output(MODEL_OUT_BEAT_OFFSET), 0);
    const TfLiteTensor *hit = s_interp->output(MODEL_OUT_HIT);
    for (int i = 0; i < 4; i++) {
        out->hit[i] = dequant(hit, i);
    }
    out->music = dequant(s_interp->output(MODEL_OUT_MUSIC), 0);

    s_cycles = esp_cpu_get_cycle_count() - t0;
    return true;
}
