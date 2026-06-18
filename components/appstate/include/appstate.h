// appstate.h
// Central scene state. One mutex, one writer via appstate_apply_patch.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "app_events.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EFFECT_PARAM_MAX 8

// Bits for app_patch_t.mask and app_state_changed_evt_t.changed_mask.
typedef enum {
    STATE_F_POWER      = (1 << 0),
    STATE_F_BRIGHTNESS = (1 << 1),
    STATE_F_STRETCH    = (1 << 2),
    STATE_F_EFFECT     = (1 << 3),
    STATE_F_COLOR      = (1 << 4),
    STATE_F_SPEED      = (1 << 5),
    STATE_F_AUDIO_SENS = (1 << 6),
    STATE_F_PARAMS     = (1 << 7),
} state_field_t;

typedef struct {
    bool     power;
    float    brightness;
    float    stretch;
    uint16_t effect_id;
    float    color[3];
    float    speed;
    float    audio_sens;
    float    params[EFFECT_PARAM_MAX];
    uint32_t seq;
} app_state_t;

// A partial update. Only fields whose bit is set in mask are applied.
typedef struct {
    uint32_t mask;
    bool     power;
    float    brightness;
    float    stretch;
    uint16_t effect_id;
    float    color[3];
    float    speed;
    float    audio_sens;
    float    params[EFFECT_PARAM_MAX];
} app_patch_t;

esp_err_t appstate_init(void);

// Copy the current scene out under the lock.
void appstate_get(app_state_t *out);

// Validate, clamp and apply a patch, then post EVT_STATE_CHANGED if anything
// actually changed. The only writer of scene state.
esp_err_t appstate_apply_patch(app_src_t src, const app_patch_t *patch);

#ifdef __cplusplus
}
#endif
