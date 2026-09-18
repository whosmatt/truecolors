// audio.c
#include "audio.h"
#include "frontend.h"
#include "kick.h"
#include "snare.h"
#include "beatgrid.h"
#include "board_pins.h"
#include "app_config.h"
#include "app_events.h"

#include <stdlib.h>
#include <string.h>
#include "driver/i2s_pdm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "audio";

#define BEAT_DECAY 0.85f

static i2s_chan_handle_t s_rx;
static fe_t s_fe;
static uint32_t s_notch_hz = TC_PWM_HZ;
static audio_features_t s_features;
static float s_beat_env;
static float s_grid_env;
static uint32_t s_block_n;

void audio_set_notch_hz(uint32_t hz)
{
    s_notch_hz = hz;   // may arrive before audio_init; re-applied there
    fe_set_notch_hz(&s_fe, hz);
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
        s_features.level = f.level;
        memcpy(s_features.bands, f.bands, sizeof(s_features.bands));
        s_features.spl_db = f.spl_db;

        // Kick hits are beats, snare hits extra pattern anchors; the beat
        // grid passes kicks through while unlocked and emits predicted
        // attack-aligned beats (suppressing off-grid detections) once locked.
        kick_in_t ki = {
            .flux = { f.flux[0], f.flux[1], f.flux[2] },
            .fund_rms = f.fund_rms,
            .mid_flux = f.mid_flux,
            .treble_flux = f.treble_flux,
        };
        kick_out_t ko;
        kick_block(&ki, &ko);
        beatgrid_out_t bg;
        beatgrid_block(ko.hit, ko.snare, ko.t_off, &bg);
        if (bg.beat) {
            s_beat_env = 1.0f;
        }
        if (bg.grid_beat) {
            s_grid_env = 1.0f;
        }
        // Publish before decaying, so consumers see the full 1.0 peak.
        s_features.beat = s_beat_env;
        s_features.grid = s_grid_env;
        s_beat_env *= BEAT_DECAY;
        s_grid_env *= BEAT_DECAY;
        s_features.kicks = ko.count;
        s_features.bpm = bg.bpm;

        s_block_n++;
        if (ko.hit || ko.snare || bg.grid_beat || bg.nudge != 0.0f) {
            app_beatgrid_evt_t ev = {
                .t = s_block_n,
                .block_hz = (float)FE_SAMPLE_RATE / FE_BLOCK_SAMPLES,
                .phase = bg.phase,
                .period = bg.period,
                .bpm = bg.bpm,
                .kick = ko.hit,
                .snare = ko.snare,
                .met = bg.grid_beat,
                .off = ko.t_off,
                .nudge = bg.nudge,
                .err = bg.err,
            };
            app_bus_post(EVT_BEATGRID, &ev, sizeof(ev), 0);
        }
    }
}

esp_err_t audio_init(void)
{
    fe_init(&s_fe, s_notch_hz);
    kick_init();
    beatgrid_init((float)FE_SAMPLE_RATE / FE_BLOCK_SAMPLES);

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
