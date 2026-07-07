// appstate.c
#include "appstate.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "appstate";

static app_state_t s_state = {
    .power = false,
    .brightness = 0.6f,
    .stretch = 0.0f,
    .effect_id = 0,
    .color = {1.0f, 1.0f, 1.0f},
    .speed = 0.5f,
    .audio_sens = 0.5f,
    .seq = 0,
};

static SemaphoreHandle_t s_lock;

static float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

esp_err_t appstate_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

void appstate_get(app_state_t *out)
{
    if (!out) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(out, &s_state, sizeof(*out));
    xSemaphoreGive(s_lock);
}

esp_err_t appstate_apply_patch(app_src_t src, const app_patch_t *patch)
{
    if (!patch) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t changed = 0;
    uint32_t seq = 0;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (patch->mask & STATE_F_POWER && patch->power != s_state.power) {
        s_state.power = patch->power;
        changed |= STATE_F_POWER;
    }
    if (patch->mask & STATE_F_BRIGHTNESS) {
        float v = clamp01(patch->brightness);
        if (v != s_state.brightness) {
            s_state.brightness = v;
            changed |= STATE_F_BRIGHTNESS;
        }
    }
    if (patch->mask & STATE_F_STRETCH) {
        float v = clamp01(patch->stretch);
        if (v != s_state.stretch) {
            s_state.stretch = v;
            changed |= STATE_F_STRETCH;
        }
    }
    if (patch->mask & STATE_F_EFFECT && patch->effect_id != s_state.effect_id) {
        s_state.effect_id = patch->effect_id;
        changed |= STATE_F_EFFECT;
    }
    if (patch->mask & STATE_F_COLOR) {
        for (int i = 0; i < 3; i++) {
            float v = clamp01(patch->color[i]);
            if (v != s_state.color[i]) {
                s_state.color[i] = v;
                changed |= STATE_F_COLOR;
            }
        }
    }
    if (patch->mask & STATE_F_SPEED) {
        float v = clamp01(patch->speed);
        if (v != s_state.speed) {
            s_state.speed = v;
            changed |= STATE_F_SPEED;
        }
    }
    if (patch->mask & STATE_F_AUDIO_SENS) {
        float v = clamp01(patch->audio_sens);
        if (v != s_state.audio_sens) {
            s_state.audio_sens = v;
            changed |= STATE_F_AUDIO_SENS;
        }
    }
    if (patch->mask & STATE_F_PARAMS) {
        if (memcmp(s_state.params, patch->params, sizeof(s_state.params)) != 0) {
            memcpy(s_state.params, patch->params, sizeof(s_state.params));
            changed |= STATE_F_PARAMS;
        }
    }

    if (changed) {
        seq = ++s_state.seq;
    }
    xSemaphoreGive(s_lock);

    if (changed) {
        app_state_changed_evt_t evt = { .seq = seq, .changed_mask = changed, .src = src,
                                        .origin = patch->origin };
        esp_err_t err = app_bus_post(EVT_STATE_CHANGED, &evt, sizeof(evt), 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "post EVT_STATE_CHANGED failed: %s", esp_err_to_name(err));
        }
    }
    return ESP_OK;
}
