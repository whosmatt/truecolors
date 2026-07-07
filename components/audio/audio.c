// audio.c
#include "audio.h"
#include "board_pins.h"

#include <math.h>
#include <string.h>
#include "driver/i2s_pdm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "audio";

#define SAMPLE_RATE   48000
#define BLOCK_SAMPLES 512
#define AVG_ALPHA     0.05f
#define BEAT_DECAY    0.85f
#define BEAT_RATIO    1.5f

// AGC: each band is normalized against its own peak tracker
#define AGC_RELEASE   0.99852f  // peak halves in ~5 s (per 10.7 ms block)
#define AGC_MIN_REF   0.003f    // ~-50 dBFS, below this the room is silent
#define ENV_RELEASE   0.08f     // output envelope, ~130 ms release
#define DC_K          0.00065f  // DC blocker pole, ~5 Hz
#define LP_BASS_K     0.026f    // one-pole low-pass, ~200 Hz
#define LP_TREBLE_K   0.23f     // one-pole low-pass, ~2 kHz (treble = s - lp)

static i2s_chan_handle_t s_rx;
static audio_features_t s_features;
static float s_avg_energy;
static float s_beat_env;
static float s_dc;
static float s_lp_bass;
static float s_lp_treble;
static float s_peak[3];
static float s_env[3];

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

        float sum[3] = { 0.0f, 0.0f, 0.0f };   // bass, broadband, treble
        for (int i = 0; i < n; i++) {
            float s = buf[i] / 32768.0f;
            s_dc += DC_K * (s - s_dc);
            s -= s_dc;
            s_lp_bass += LP_BASS_K * (s - s_lp_bass);
            s_lp_treble += LP_TREBLE_K * (s - s_lp_treble);
            float hi = s - s_lp_treble;
            sum[0] += s_lp_bass * s_lp_bass;
            sum[1] += s * s;
            sum[2] += hi * hi;
        }

        for (int b = 0; b < 3; b++) {
            float rms = sqrtf(sum[b] / n);
            s_peak[b] = fmaxf(rms, s_peak[b] * AGC_RELEASE);
            float lv = clamp01(rms / fmaxf(s_peak[b], AGC_MIN_REF));
            // Instant attack, musical release.
            s_env[b] = lv > s_env[b] ? lv : s_env[b] + (lv - s_env[b]) * ENV_RELEASE;
            s_features.bands[b] = s_env[b];
        }
        s_features.level = s_features.bands[1];

        // Beat: broadband transient over the running average, gated so noise
        // in a silent room can't ratio-trigger.
        float rms = sqrtf(sum[1] / n);
        s_avg_energy = s_avg_energy * (1.0f - AVG_ALPHA) + rms * AVG_ALPHA;
        if (rms > AGC_MIN_REF && rms > s_avg_energy * BEAT_RATIO) {
            s_beat_env = 1.0f;
        }
        s_beat_env *= BEAT_DECAY;
        s_features.beat = s_beat_env;
    }
}

esp_err_t audio_init(void)
{
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
