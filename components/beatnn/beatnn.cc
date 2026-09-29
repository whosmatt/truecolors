#include "beatnn.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "audio.h"
#include "fe_ctx.h"
#include "frontend.h"
#include "melflux.h"
#include "model_params.h"
#include "selftest_window.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

static const char *TAG = "beatnn";

// model_params.h is generated from the model files, check consistency with
// audio frontend
static_assert(MODEL_FE_SPEC_VERSION == FE_SPEC_VERSION,
              "model was trained against a different front end version");
static_assert(MODEL_INPUTS == FE_CTX_INPUTS, "context window size mismatch");
static_assert(MODEL_FEATS == FE_CTX_FEATS, "feature count mismatch");
static_assert(MODEL_RING_BLOCKS == FE_CTX_BLOCKS, "ring depth mismatch");
static_assert(MODEL_LOOKAHEAD == FE_CTX_LOOKAHEAD, "lookahead mismatch");
static_assert(MODEL_BLOCK_SAMPLES == FE_BLOCK_SAMPLES, "block size mismatch");
static_assert(MODEL_SAMPLE_RATE == FE_SAMPLE_RATE, "sample rate mismatch");
static_assert(MODEL_X_FEATS == FE_CTX_FEATS + MEL_BANDS, "feature vector mismatch");

extern const uint8_t stage_a_start[] asm("_binary_stage_a_tflite_start");
extern const uint8_t stage_a_end[] asm("_binary_stage_a_tflite_end");
extern const uint8_t stage_b_start[] asm("_binary_stage_b_tflite_start");
extern const uint8_t stage_b_end[] asm("_binary_stage_b_tflite_end");

// weights MUST be 16-byte aligned and in RAM or there is a huge silent
// performance penalty
alignas(16) static uint8_t s_arena_a[CONFIG_BEATNN_ARENA_KB * 1024];
alignas(16) static uint8_t s_arena_b[CONFIG_BEATNN_ARENA_KB * 1024];
alignas(16) static uint8_t s_model_a[MODEL_A_BYTES];
alignas(16) static uint8_t s_model_b[MODEL_B_BYTES];

static tflite::MicroInterpreter *s_ia, *s_ib;
static TfLiteTensor *s_in_a, *s_in_b;
static fe_ctx_t s_ring_a, s_ring_b;
static float s_win[MODEL_INPUTS];
static float s_a_prev;   // stage A output of the previous block: feedback a[n-3]
static bool s_ready;
static uint32_t s_cycles;
static uint32_t s_quant_cycles;
static uint32_t s_invoke_cycles;
static uint8_t s_in_align;
static beat_infer_t s_selftest;
static bool s_selftest_pass;

static bool selftest(void);

static uint64_t fnv1a64(const uint8_t *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 0x100000001b3ULL;
    }
    return h;
}

static esp_err_t setup(const char *name, const uint8_t *src, const uint8_t *end,
                       uint8_t *dst, size_t want, uint64_t hash)
{
    size_t n = (size_t)(end - src);
    if (n != want) {
        ESP_LOGE(TAG, "%s is %u bytes, header says %u", name, (unsigned)n, (unsigned)want);
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(dst, src, n);
    uint64_t h = fnv1a64(dst, n);
    if (h != hash) {
        ESP_LOGE(TAG, "%s hash %016llx != %016llx: regenerate model_params.h",
                 name, (unsigned long long)h, (unsigned long long)hash);
        return ESP_ERR_INVALID_CRC;
    }
    if (tflite::GetModel(dst)->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "%s schema mismatch", name);
        return ESP_ERR_INVALID_VERSION;
    }
    return ESP_OK;
}

esp_err_t beatnn_init(void)
{
    if (fe_variant() != MODEL_FE_VARIANT) {
        ESP_LOGE(TAG, "front end variant %u, vs model %u",
                 (unsigned)fe_variant(), (unsigned)MODEL_FE_VARIANT);
        return ESP_ERR_INVALID_VERSION;
    }

    esp_err_t err = setup("stage_a", stage_a_start, stage_a_end, s_model_a,
                          MODEL_A_BYTES, MODEL_A_FNV1A);
    if (err != ESP_OK) {
        return err;
    }
    err = setup("stage_b", stage_b_start, stage_b_end, s_model_b,
                MODEL_B_BYTES, MODEL_B_FNV1A);
    if (err != ESP_OK) {
        return err;
    }

    static tflite::MicroMutableOpResolver<2> resolver;
    resolver.AddFullyConnected();
    resolver.AddLogistic();

    static tflite::MicroInterpreter ia(tflite::GetModel(s_model_a), resolver,
                                       s_arena_a, sizeof(s_arena_a));
    static tflite::MicroInterpreter ib(tflite::GetModel(s_model_b), resolver,
                                       s_arena_b, sizeof(s_arena_b));
    if (ia.AllocateTensors() != kTfLiteOk || ib.AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors failed; arenas are %u bytes each",
                 (unsigned)sizeof(s_arena_a));
        return ESP_ERR_NO_MEM;
    }
    s_ia = &ia;
    s_ib = &ib;

    s_in_a = ia.input(0);
    s_in_b = ib.input(0);
    if (s_in_a->type != kTfLiteInt8 || s_in_a->bytes != MODEL_INPUTS ||
        s_in_b->type != kTfLiteInt8 || s_in_b->bytes != MODEL_INPUTS) {
        ESP_LOGE(TAG, "trunk inputs are not %d int8", MODEL_INPUTS);
        return ESP_ERR_INVALID_ARG;
    }
    if (ia.outputs_size() != 1 || ia.output(MODEL_A_OUT_BEAT)->bytes != 1) {
        ESP_LOGE(TAG, "unexpected stage A output layout");
        return ESP_ERR_INVALID_ARG;
    }
    if (ib.outputs_size() != 4 ||
        ib.output(MODEL_OUT_BEAT)->bytes != 1 ||
        ib.output(MODEL_OUT_BEAT_OFFSET)->bytes != 1 ||
        ib.output(MODEL_OUT_HIT)->bytes != 4 ||
        ib.output(MODEL_OUT_MUSIC)->bytes != 1) {
        ESP_LOGE(TAG, "unexpected stage B output layout");
        return ESP_ERR_INVALID_ARG;
    }

    if (!esp_ptr_internal(s_arena_a) || !esp_ptr_internal(s_model_a) ||
        !esp_ptr_internal(s_arena_b) || !esp_ptr_internal(s_model_b)) {
        ESP_LOGW(TAG, "arena or weights are not in internal RAM");
    }
    s_in_align = (uint8_t)(((uintptr_t)s_in_a->data.int8 |
                            (uintptr_t)s_in_b->data.int8) & 15);
    ESP_LOGI(TAG, "A weights %p arena %u/%u, B weights %p arena %u/%u, in align%u",
             s_model_a, (unsigned)ia.arena_used_bytes(), (unsigned)sizeof(s_arena_a),
             s_model_b, (unsigned)ib.arena_used_bytes(), (unsigned)sizeof(s_arena_b),
             (unsigned)s_in_align);

    fe_ctx_init(&s_ring_a);
    fe_ctx_init(&s_ring_b);
    s_a_prev = 0.0f;
    s_ready = true;

    if (!selftest()) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    audio_set_infer_hook(beatnn_infer);
    ESP_LOGI(TAG, "two-stage model ok, fe v%d variant %u",
             FE_SPEC_VERSION, (unsigned)fe_variant());
    return ESP_OK;
}

bool beatnn_ready(void) { return s_ready; }

uint32_t beatnn_arena_used(void)
{
    return (s_ia && s_ib) ? (uint32_t)(s_ia->arena_used_bytes() +
                                       s_ib->arena_used_bytes()) : 0;
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

// p[j] = bias[j] + sum_i x[i] * W[i][j]; normalisation is folded into W.
static void project(const float *x, int n, const float (*w)[MODEL_FEATS],
                    const float *bias, float *p)
{
    for (int j = 0; j < MODEL_FEATS; j++) {
        p[j] = bias[j];
    }
    for (int i = 0; i < n; i++) {
        float xi = x[i];
        const float *wi = w[i];
        for (int j = 0; j < MODEL_FEATS; j++) {
            p[j] += xi * wi[j];
        }
    }
}

// s3 has no float divide, use inv scale
static void quantise(const float *src, TfLiteTensor *t, float inv_scale, int zero)
{
    int8_t *q = t->data.int8;
    for (int i = 0; i < MODEL_INPUTS; i++) {
        int v = (int)lrintf(src[i] * inv_scale) + zero;
        q[i] = (int8_t)(v < -128 ? -128 : (v > 127 ? 127 : v));
    }
}

// selftest using known golden block and vector
static bool selftest(void)
{
    const float proj_tol = 1e-3f;   // golden projections are rounded to 5 decimals
    float p[MODEL_FEATS];
    bool ok = true;

    project(kGoldenX28, MODEL_X_FEATS, kProjAW, kProjAB, p);
    for (int j = 0; j < MODEL_FEATS; j++) {
        if (fabsf(p[j] - kGoldenProjA[j]) > proj_tol) {
            ESP_LOGE(TAG, "projection A[%d] %.5f want %.5f", j, p[j], kGoldenProjA[j]);
            ok = false;
        }
    }

    float xb[MODEL_X_FEATS + 1];
    memcpy(xb, kGoldenX28, sizeof(kGoldenX28));
    xb[MODEL_X_FEATS] = kGoldenFb;
    project(xb, MODEL_X_FEATS + 1, kProjBW, kProjBB, p);
    for (int j = 0; j < MODEL_FEATS; j++) {
        if (fabsf(p[j] - kGoldenProjB[j]) > proj_tol) {
            ESP_LOGE(TAG, "projection B[%d] %.5f want %.5f", j, p[j], kGoldenProjB[j]);
            ok = false;
        }
    }

    memcpy(s_in_a->data.int8, kGoldenAInput, MODEL_INPUTS);
    if (s_ia->Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "stage A invoke failed");
        return false;
    }
    int8_t got = s_ia->output(MODEL_A_OUT_BEAT)->data.int8[0];
    if (abs(got - kGoldenABeat) > 1) {
        ESP_LOGE(TAG, "stage A beat %d want %d", got, kGoldenABeat);
        ok = false;
    }

    memcpy(s_in_b->data.int8, kGoldenBInput, MODEL_INPUTS);
    if (s_ib->Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "stage B invoke failed");
        return false;
    }
    const struct { int pos; int idx; int8_t want; const char *name; } checks[] = {
        {MODEL_OUT_BEAT, 0, kGoldenBBeat, "beat"},
        {MODEL_OUT_BEAT_OFFSET, 0, kGoldenBOffset, "beat_offset"},
        {MODEL_OUT_MUSIC, 0, kGoldenBMusic, "music"},
        {MODEL_OUT_HIT, 0, kGoldenBHit[0], "hit0"},
        {MODEL_OUT_HIT, 1, kGoldenBHit[1], "hit1"},
        {MODEL_OUT_HIT, 2, kGoldenBHit[2], "hit2"},
        {MODEL_OUT_HIT, 3, kGoldenBHit[3], "hit3"},
    };
    for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
        int8_t v = s_ib->output(checks[i].pos)->data.int8[checks[i].idx];
        if (abs(v - checks[i].want) > 1) {
            ESP_LOGE(TAG, "stage B %s %d want %d", checks[i].name, v, checks[i].want);
            ok = false;
        }
    }

    s_selftest.beat = dequant(s_ib->output(MODEL_OUT_BEAT), 0);
    s_selftest.beat_offset = dequant(s_ib->output(MODEL_OUT_BEAT_OFFSET), 0);
    s_selftest.music = dequant(s_ib->output(MODEL_OUT_MUSIC), 0);
    for (int i = 0; i < 4; i++) {
        s_selftest.hit[i] = dequant(s_ib->output(MODEL_OUT_HIT), i);
    }
    s_selftest.valid = ok;
    s_selftest_pass = ok;
    return ok;
}

bool beatnn_infer(const float *x, beat_infer_t *out)
{
    if (!s_ready) {
        return false;
    }
    uint32_t t0 = esp_cpu_get_cycle_count();
    uint32_t quant = 0, invoke = 0;

    float p[MODEL_FEATS];

    project(x, MODEL_X_FEATS, kProjAW, kProjAB, p);
    fe_ctx_push_vec(&s_ring_a, p);

    // Stage A beat for centre n-3
    float fb = s_a_prev;
    float a_out = 0.0f;

    if (fe_ctx_window(&s_ring_a, s_win)) {
        uint32_t c0 = esp_cpu_get_cycle_count();
        quantise(s_win, s_in_a, 1.0f / MODEL_A_IN_SCALE, MODEL_A_IN_ZERO);
        uint32_t c1 = esp_cpu_get_cycle_count();
        quant += c1 - c0;
        if (s_ia->Invoke() != kTfLiteOk) {
            s_cycles = esp_cpu_get_cycle_count() - t0;
            return false;
        }
        invoke += esp_cpu_get_cycle_count() - c1;
        a_out = dequant(s_ia->output(MODEL_A_OUT_BEAT), 0);
    }
    s_a_prev = a_out;

    float xb[MODEL_X_FEATS + 1];
    memcpy(xb, x, MODEL_X_FEATS * sizeof(float));
    xb[MODEL_X_FEATS] = fb;
    project(xb, MODEL_X_FEATS + 1, kProjBW, kProjBB, p);
    fe_ctx_push_vec(&s_ring_b, p);

    bool ok = false;
    if (fe_ctx_window(&s_ring_b, s_win)) {
        uint32_t c0 = esp_cpu_get_cycle_count();
        quantise(s_win, s_in_b, 1.0f / MODEL_B_IN_SCALE, MODEL_B_IN_ZERO);
        uint32_t c1 = esp_cpu_get_cycle_count();
        quant += c1 - c0;
        if (s_ib->Invoke() != kTfLiteOk) {
            s_cycles = esp_cpu_get_cycle_count() - t0;
            return false;
        }
        invoke += esp_cpu_get_cycle_count() - c1;

        out->beat = dequant(s_ib->output(MODEL_OUT_BEAT), 0);
        out->beat_offset = dequant(s_ib->output(MODEL_OUT_BEAT_OFFSET), 0);
        const TfLiteTensor *hit = s_ib->output(MODEL_OUT_HIT);
        for (int i = 0; i < 4; i++) {
            out->hit[i] = dequant(hit, i);
        }
        out->music = dequant(s_ib->output(MODEL_OUT_MUSIC), 0);
        ok = true;
    }
    s_quant_cycles = quant;
    s_invoke_cycles = invoke;
    s_cycles = esp_cpu_get_cycle_count() - t0;
    return ok;
}
