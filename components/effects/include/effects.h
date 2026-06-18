// effects.h

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "audio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Global controls an effect may consume, exposed so the UI shows only the
// relevant sliders.
typedef enum {
    GLOBAL_COLOR      = 1 << 0,
    GLOBAL_SPEED      = 1 << 1,
    GLOBAL_AUDIO_SENS = 1 << 2,
    GLOBAL_BRIGHTNESS = 1 << 3,
    GLOBAL_STRETCH    = 1 << 4,
} effect_global_t;

typedef struct {
    const char *name;
    float min, max, def;
} effect_param_def_t;

// Resolved per-frame inputs handed to an effect's render function.
typedef struct {
    float color[3];
    float brightness;
    float stretch;
    float speed;
    float audio_sens;
    const float *params;
    float t;
    float dt;
    audio_features_t audio;
    void *state;
} effect_ctx_t;

typedef struct {
    const char *id;
    const char *display_name;
    uint32_t global_mask;
    const effect_param_def_t *params;
    size_t n_params;
    void (*init)(void *state);
    void (*render)(const effect_ctx_t *ctx, float out_rgb[3]);
    size_t state_size;
} effect_desc_t;

esp_err_t effects_init(void);

// Create the laser_render task.
esp_err_t effects_start_render(void);

// The static effect catalog, for the WS snapshot.
const effect_desc_t *effects_get_catalog(size_t *count);

#ifdef __cplusplus
}
#endif
