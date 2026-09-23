// audio.c
#include "audio.h"
#include "frontend.h"
#include "fe_ctx.h"
#include "beattrack.h"
#include "musicgate.h"
#include "beatnn_classes.h"
#include "board_pins.h"
#include "app_config.h"
#include "app_events.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "driver/i2s_pdm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_cpu.h"
#include "esp_log.h"

static const char *TAG = "audio";

#define BEAT_DECAY 0.85f
#define HIT_THRESH 0.5f   // counter and visualization only

static i2s_chan_handle_t s_rx;
static fe_t s_fe;
static audio_features_t s_features;
static float s_beat_env;
static float s_grid_env;
static uint32_t s_block_n;

static fe_ctx_t s_ctx;
static float s_win[FE_CTX_INPUTS];   // 2.1 KB
static audio_infer_fn s_infer;
static beat_infer_t s_last_infer;
static uint32_t s_infer_cycles;
static bool s_kick_prev_hit;
static beattrack_out_t s_track;
static musicgate_t s_gate;

static bool s_gate_open;

void audio_set_infer_hook(audio_infer_fn fn)
{
    s_infer = fn;
}

void audio_get_infer(beat_infer_t *out)
{
    if (out) {
        *out = s_last_infer;
    }
}

uint32_t audio_infer_cycles(void)
{
    return s_infer_cycles;
}

void audio_music_state(float *prob, float *fraction, bool *open)
{
    if (prob) *prob = s_gate.last;
    if (fraction) *fraction = musicgate_fraction(&s_gate);
    if (open) *open = s_gate_open;
}

void audio_track_state(bool *locked, float *strength, float *bpm, float *err)
{
    if (locked) *locked = s_track.locked;
    if (strength) *strength = s_track.strength;
    if (bpm) *bpm = s_track.bpm;
    if (err) *err = s_track.err;
}

static void audio_task(void *arg)
{
    int16_t *buf = malloc(FE_BLOCK_SAMPLES * sizeof(int16_t));
    if (!buf) {
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        size_t nbytes = 0;
        if (i2s_channel_read(s_rx, buf, FE_BLOCK_SAMPLES * sizeof(int16_t),
                             &nbytes, portMAX_DELAY) != ESP_OK || nbytes == 0) {
            continue;
        }

        fe_out_t f;
        fe_block(&s_fe, buf, nbytes / sizeof(int16_t), &f);
        audio_infer_fn infer = s_infer;
        if (infer) {
            uint32_t c0 = esp_cpu_get_cycle_count();
            fe_ctx_push(&s_ctx, &f);
            beat_infer_t bi = {0};
            if (fe_ctx_window(&s_ctx, s_win) && infer(s_win, &bi)) {
                bi.valid = true;
            }
            s_last_infer = bi;
            s_infer_cycles = esp_cpu_get_cycle_count() - c0;
        }

        s_features.level = f.level;
        memcpy(s_features.bands, f.bands, sizeof(s_features.bands));
        s_features.spl_db = f.spl_db;

        // Stage 2 takes the activation directly
        beattrack_out_t bt;
        beattrack_block(s_last_infer.valid ? s_last_infer.beat : 0.0f, &bt);

        // TODO implement gate when music head works well
        s_gate_open = musicgate_block(&s_gate,
                                      s_last_infer.valid ? s_last_infer.music : 0.0f);

        // Effects step hue on a rise in beat, provide a clean pulse
        if (bt.beat) {
            s_beat_env = 1.0f;
            if (bt.locked) {
                s_grid_env = 1.0f;
            }
        }
        s_features.beat = s_beat_env;
        s_features.grid = s_grid_env;
        s_beat_env *= BEAT_DECAY;
        s_grid_env *= BEAT_DECAY;
        s_features.bpm = bt.bpm;
        s_track = bt;

        bool kick = s_last_infer.valid && s_last_infer.hit[BEATNN_KICK] > HIT_THRESH;
        bool snare = s_last_infer.valid && s_last_infer.hit[BEATNN_SNARE] > HIT_THRESH;
        bool hihat = s_last_infer.valid && s_last_infer.hit[BEATNN_HIHAT] > HIT_THRESH;
        if (kick && !s_kick_prev_hit) {
            s_features.kicks++;
        }
        s_kick_prev_hit = kick;

        s_block_n++;
        if (kick || snare || hihat || bt.beat) {
            app_beatgrid_evt_t ev = {
                .t = s_block_n,
                .block_hz = (float)FE_SAMPLE_RATE / FE_BLOCK_SAMPLES,
                .phase = bt.period > 0.0f ? bt.period - bt.to_next : 0.0f,
                .period = bt.period,
                .bpm = bt.bpm,
                .kick = kick,
                .snare = snare,
                .hihat = hihat,
                .met = bt.beat,
                .act = s_last_infer.valid ? s_last_infer.beat : 0.0f,
                .music = s_last_infer.valid ? s_last_infer.music : 0.0f,
                .off = s_last_infer.valid ? s_last_infer.beat_offset : 0.0f,
                .err = bt.err,
            };
            app_bus_post(EVT_BEATGRID, &ev, sizeof(ev), 0);
        }
    }
}

esp_err_t audio_init(void)
{
    fe_init(&s_fe);
    fe_ctx_init(&s_ctx);
    beattrack_init();
    musicgate_init(&s_gate);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx));

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(FE_SAMPLE_RATE),
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
    ESP_LOGI(TAG, "audio ready, PDM RX %d Hz", FE_SAMPLE_RATE);
    return ESP_OK;
}

void audio_get_features(audio_features_t *out)
{
    if (out) {
        *out = s_features;
    }
}
