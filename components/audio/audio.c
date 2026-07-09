// audio.c
#include "audio.h"
#include "kick.h"
#include "snare.h"
#include "beatgrid.h"
#include "board_pins.h"
#include "app_config.h"

#include <math.h>
#include <string.h>
#include "driver/i2s_pdm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "audio";

#define SAMPLE_RATE   48000
#define BLOCK_SAMPLES 512
#define BEAT_DECAY    0.85f

// SPL from the datasheet sensitivity: -26 dBFS @ 94 dB SPL, 1 kHz (dBFS is
// referenced to a full-scale sine, hence the +3.01 rms correction).
#define MIC_SENS_DBFS (-26.0f)
#define SPL_TAU_S     1.5f      // slow meter ballistics

// AGC: each band is normalized against its own peak tracker
#define AGC_RELEASE   0.99852f  // peak halves in ~5 s (per 10.7 ms block)
#define AGC_MIN_REF   0.003f    // ~-50 dBFS, below this the room is silent
#define DC_K          0.00065f  // DC blocker pole, ~5 Hz
#define HC_FC_HZ      4000.0f   // hi-cut corner, 8th-order Butterworth (48 dB/oct)
#define LP_BASS_K     0.026f    // one-pole low-pass, ~200 Hz
#define LP_TREBLE_K   0.23f     // one-pole low-pass, ~2 kHz; mid = lp2k - lp200,
                                // treble = s - lp2k (2-4 kHz under the hi-cut)

// Kick sub-bands for the kick detector: one-pole corners at 40 / 80 / 140 /
// 220 Hz, sub-bands are neighbor differences (fundamental / body / click).
#define LP_K40        0.00522f
#define LP_K80        0.01041f
#define LP_K140       0.01816f
#define LP_K220       0.02839f

// Coil whine heavily couples into the mic and needs to be filtered out. Analysis shows a combination of clean harmonics of the MCPWM frequency and additional high frequency metallic resonance from the inductor core. 
// Strategy: The PWM harmonics are precisely and efficently eliminated by a single negative mode comb filter linked to the MCPWM frequency. The high frequencies are removed by a biquad hi-cut at 4kHz 48dB/oct.  

#define COMB_MAX      600

typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} biquad_t;

static i2s_chan_handle_t s_rx;
static audio_features_t s_features;
static float s_beat_env;
static float s_dc;
static biquad_t s_hicut[4];
static float s_lp_bass;
static float s_lp_treble;
static float s_peak[4];
static float s_lp_low[4];
static float s_kick_prev[3];
static float s_kick_peak[3];
static float s_mid_prev;
static float s_tre_prev;
static float s_spl_ms;
static float s_comb[COMB_MAX];
static volatile int s_comb_n = SAMPLE_RATE / TC_PWM_HZ;
static int s_comb_i;

static void biquad_lp_init(biquad_t *f, float fc, float q)
{
    float w = 2.0f * (float)M_PI * fc / SAMPLE_RATE;
    float cw = cosf(w);
    float alpha = sinf(w) / (2.0f * q);
    float a0 = 1.0f + alpha;
    f->b0 = (1.0f - cw) * 0.5f / a0;
    f->b1 = (1.0f - cw) / a0;
    f->b2 = f->b0;
    f->a1 = -2.0f * cw / a0;
    f->a2 = (1.0f - alpha) / a0;
    f->z1 = 0.0f;
    f->z2 = 0.0f;
}

static inline float biquad_run(biquad_t *f, float x)
{
    float y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

void audio_set_notch_hz(uint32_t hz)
{
    int n = hz ? (int)((SAMPLE_RATE + hz / 2) / hz) : 1;
    if (n < 1) n = 1;
    if (n > COMB_MAX) n = COMB_MAX;
    if (n != s_comb_n) {
        memset(s_comb, 0, sizeof(s_comb));
        s_comb_n = n;
    }
}

static float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static void audio_task(void *arg)
{
    int16_t *buf = malloc(BLOCK_SAMPLES * sizeof(int16_t));
    if (!buf) {
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        size_t nbytes = 0;
        if (i2s_channel_read(s_rx, buf, BLOCK_SAMPLES * sizeof(int16_t),
                             &nbytes, portMAX_DELAY) != ESP_OK || nbytes == 0) {
            continue;
        }
        int n = nbytes / sizeof(int16_t);

        int cn = s_comb_n;
        if (s_comb_i >= cn) {
            s_comb_i = 0;
        }

        float sum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // bass, mid, treble, broadband
        float sum_kick[3] = { 0.0f, 0.0f, 0.0f };    // 40-80 / 80-140 / 140-220 Hz
        float sum_raw = 0.0f;                        // pre-filter, for the SPL meter
        for (int i = 0; i < n; i++) {
            float s = buf[i] / 32768.0f;
            s_dc += DC_K * (s - s_dc);
            s -= s_dc;
            sum_raw += s * s;

            // Comb filter
            float d = s_comb[s_comb_i];
            s_comb[s_comb_i] = s;
            if (++s_comb_i >= cn) s_comb_i = 0;
            s -= d;

            // Hi-cut
            s = biquad_run(&s_hicut[0], s);
            s = biquad_run(&s_hicut[1], s);
            s = biquad_run(&s_hicut[2], s);
            s = biquad_run(&s_hicut[3], s);

            s_lp_bass += LP_BASS_K * (s - s_lp_bass);
            s_lp_treble += LP_TREBLE_K * (s - s_lp_treble);
            float mid = s_lp_treble - s_lp_bass;
            float hi = s - s_lp_treble;
            sum[0] += s_lp_bass * s_lp_bass;
            sum[1] += mid * mid;
            sum[2] += hi * hi;
            sum[3] += s * s;

            s_lp_low[0] += LP_K40 * (s - s_lp_low[0]);
            s_lp_low[1] += LP_K80 * (s - s_lp_low[1]);
            s_lp_low[2] += LP_K140 * (s - s_lp_low[2]);
            s_lp_low[3] += LP_K220 * (s - s_lp_low[3]);
            float k0 = s_lp_low[1] - s_lp_low[0];
            float k1 = s_lp_low[2] - s_lp_low[1];
            float k2 = s_lp_low[3] - s_lp_low[2];
            sum_kick[0] += k0 * k0;
            sum_kick[1] += k1 * k1;
            sum_kick[2] += k2 * k2;
        }

        // Raw per-block levels; attack/release shaping happens per effect.
        float brms[4];
        for (int b = 0; b < 4; b++) {
            brms[b] = sqrtf(sum[b] / n);
            s_peak[b] = fmaxf(brms[b], s_peak[b] * AGC_RELEASE);
            float lv = clamp01(brms[b] / fmaxf(s_peak[b], AGC_MIN_REF));
            if (b < 3) {
                s_features.bands[b] = lv;
            } else {
                s_features.level = lv;
            }
        }
        float rms = brms[3];

        // Kick detector inputs, each normalized by its own AGC peak and gated
        // on room silence.
        kick_in_t ki;
        for (int b = 0; b < 3; b++) {
            float krms = sqrtf(sum_kick[b] / n);
            s_kick_peak[b] = fmaxf(krms, s_kick_peak[b] * AGC_RELEASE);
            float fl = (krms - s_kick_prev[b]) / fmaxf(s_kick_peak[b], AGC_MIN_REF);
            s_kick_prev[b] = krms;
            ki.flux[b] = (fl > 0.0f && rms > AGC_MIN_REF) ? fl : 0.0f;
            if (b == 0) {
                ki.fund_rms = krms / fmaxf(s_kick_peak[b], AGC_MIN_REF);
            }
        }
        float mfl = (brms[1] - s_mid_prev) / fmaxf(s_peak[1], AGC_MIN_REF);
        float tfl = (brms[2] - s_tre_prev) / fmaxf(s_peak[2], AGC_MIN_REF);
        s_mid_prev = brms[1];
        s_tre_prev = brms[2];
        ki.mid_flux = mfl > 0.0f ? mfl : 0.0f;
        ki.treble_flux = tfl > 0.0f ? tfl : 0.0f;

        // Kick hits are beats, snare hits extra pattern anchors; the beat
        // grid passes kicks through while unlocked and emits predicted
        // attack-aligned beats (suppressing off-grid detections) once locked.
        kick_out_t ko;
        kick_block(&ki, &ko);
        beatgrid_out_t bg;
        beatgrid_block(ko.hit, ko.snare, &bg);
        if (bg.beat) {
            s_beat_env = 1.0f;
        }
        // Publish before decaying, so consumers see the full 1.0 peak.
        s_features.beat = s_beat_env;
        s_beat_env *= BEAT_DECAY;
        s_features.kicks = ko.count;
        s_features.bpm = bg.bpm;

        // Slow-averaged SPL from the raw (unfiltered) signal.
        float k = 1.0f - expf(-((float)n / SAMPLE_RATE) / SPL_TAU_S);
        s_spl_ms += k * (sum_raw / n - s_spl_ms);
        s_features.spl_db = 94.0f - MIC_SENS_DBFS + 3.01f +
                            10.0f * log10f(s_spl_ms + 1e-12f);
    }
}

esp_err_t audio_init(void)
{
    // 8th-order Butterworth section Qs.
    static const float kQ[4] = { 0.5098f, 0.6013f, 0.9000f, 2.5629f };
    for (int i = 0; i < 4; i++) {
        biquad_lp_init(&s_hicut[i], HC_FC_HZ, kQ[i]);
    }
    kick_init();
    beatgrid_init((float)SAMPLE_RATE / BLOCK_SAMPLES);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx));

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = PIN_MIC_CLK,
            .din = PIN_MIC_DATA,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(s_rx, &pdm_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx));

    if (xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 5, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "audio task create failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "audio ready, PDM RX %d Hz", SAMPLE_RATE);
    return ESP_OK;
}

void audio_get_features(audio_features_t *out)
{
    if (out) {
        *out = s_features;
    }
}
