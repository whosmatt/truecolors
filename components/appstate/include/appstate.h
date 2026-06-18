// appstate.h STUB
//
// Holds ONLY persistent/user scene state (no live telemetry — that lives in
// sysmgr). Continuous values are float 0..1 so the pipeline never quantizes
// color before the final tick mapping in laser_set. One mutex; one writer
// (appstate_apply_patch, added in implementation step 2).
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "app_events.h"   // app_src_t

#ifdef __cplusplus
extern "C" {
#endif

#define EFFECT_PARAM_MAX 8   // per-effect param arena (floats)

// Bits for app_state_changed_evt_t.changed_mask.
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
    float    brightness;              // 0..1
    float    stretch;                 // 0..1
    uint16_t effect_id;
    float    color[3];                // rgb 0..1 (device-native)
    float    speed;                   // 0..1
    float    audio_sens;              // 0..1
    float    params[EFFECT_PARAM_MAX];// active effect's params
    uint32_t seq;                     // monotonic version counter
} app_state_t;

// Initialize the model + its mutex with sane defaults. (Stub in step 1.)
esp_err_t appstate_init(void);

// Copy the current scene out under the lock (render task does this per frame).
void appstate_get(app_state_t *out);

#ifdef __cplusplus
}
#endif
