// effects.c
#include "effects.h"
#include "appstate.h"
#include "laser.h"
#include "app_config.h"
#include "app_events.h"

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "effects";

#define TWO_PI 6.28318530718f

// Cached safety state. Lasers stay dark until sysmgr raises the scale.
static volatile float s_safety_scale = 0.0f;
static volatile bool  s_latched = false;

static float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static float apply_gamma(float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return powf(x, GAMMA);
}

// h,s,v in [0,1]; h wraps.
static void hsv2rgb(float h, float s, float v, float out[3])
{
    h = (h - floorf(h)) * 6.0f;
    int i = (int)h;
    float f = h - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float u = v * (1.0f - (1.0f - f) * s);
    switch (i % 6) {
        case 0:  out[0] = v; out[1] = u; out[2] = p; break;
        case 1:  out[0] = q; out[1] = v; out[2] = p; break;
        case 2:  out[0] = p; out[1] = v; out[2] = u; break;
        case 3:  out[0] = p; out[1] = q; out[2] = v; break;
        case 4:  out[0] = u; out[1] = p; out[2] = v; break;
        default: out[0] = v; out[1] = p; out[2] = q; break;
    }
}

static float rand01(void)
{
    return (float)esp_random() / (float)UINT32_MAX;
}

// ---- effects ----

static void solid_render(const effect_ctx_t *ctx, float out[3])
{
    out[0] = ctx->color[0];
    out[1] = ctx->color[1];
    out[2] = ctx->color[2];
}

static const effect_param_def_t breathe_params[] = {
    { "depth", 0.0f, 1.0f, 0.8f },
};

static void breathe_render(const effect_ctx_t *ctx, float out[3])
{
    float depth = ctx->params[0];
    float phase = 0.5f * (1.0f - cosf(TWO_PI * ctx->speed * ctx->t));
    float factor = (1.0f - depth) + depth * phase;
    for (int c = 0; c < 3; c++) {
        out[c] = ctx->color[c] * factor;
    }
}

static const effect_param_def_t audio_params[] = {
    { "band", 0.0f, 2.0f, 0.0f },
};

static void audio_render(const effect_ctx_t *ctx, float out[3])
{
    float level = clamp01(ctx->audio.level * ctx->audio_sens * 2.0f);
    for (int c = 0; c < 3; c++) {
        out[c] = ctx->color[c] * level;
    }
}

// Full hue cycle, ignores the set color.
static void rainbow_render(const effect_ctx_t *ctx, float out[3])
{
    float hue = ctx->speed * 0.1f * ctx->t;
    hsv2rgb(hue, 1.0f, 1.0f, out);
}

static const effect_param_def_t rainbow_window_params[] = {
    { "window", 0.0f, 1.0f, 0.25f },
    { "center", 0.0f, 1.0f, 0.5f },
};

// Hue fades back and forth across a window of the wheel.
static void rainbow_window_render(const effect_ctx_t *ctx, float out[3])
{
    float window = ctx->params[0];
    float center = ctx->params[1];
    float sweep = 0.5f * (1.0f - cosf(TWO_PI * ctx->speed * 0.1f * ctx->t));
    float hue = center + window * (sweep - 0.5f);
    hsv2rgb(hue, 1.0f, 1.0f, out);
}

typedef struct {
    float remaining;   // seconds left in the current pulse
    float color[3];
} discombobulate_state_t;

static void discombobulate_pick(discombobulate_state_t *s)
{
    static const float palette[4][3] = {
        {1, 0, 0}, {0, 1, 1}, {0, 0, 1}, {0, 0, 0},  // red, cyan, blue, off
    };
    float freq = 7.0f + rand01() * 8.0f;   // 7-15 Hz
    s->remaining = 0.5f / freq;            // square-wave half-cycle
    int idx = (int)(rand01() * 4.0f);
    if (idx > 3) idx = 3;
    memcpy(s->color, palette[idx], sizeof(s->color));
}

static void discombobulate_init(void *state)
{
    discombobulate_pick(state);
}

// Randomized strobe through red/cyan/blue/off.
static void discombobulate_render(const effect_ctx_t *ctx, float out[3])
{
    discombobulate_state_t *s = ctx->state;
    s->remaining -= ctx->dt;
    if (s->remaining <= 0.0f) {
        discombobulate_pick(s);
    }
    memcpy(out, s->color, sizeof(s->color));
}

static const effect_desc_t s_registry[] = {
    {
        .id = "solid", .display_name = "Solid",
        .global_mask = GLOBAL_COLOR | GLOBAL_BRIGHTNESS | GLOBAL_STRETCH,
        .params = NULL, .n_params = 0,
        .render = solid_render,
    },
    {
        .id = "breathe", .display_name = "Breathe",
        .global_mask = GLOBAL_COLOR | GLOBAL_BRIGHTNESS | GLOBAL_STRETCH | GLOBAL_SPEED,
        .params = breathe_params, .n_params = 1,
        .render = breathe_render,
    },
    {
        .id = "audio", .display_name = "Audio",
        .global_mask = GLOBAL_COLOR | GLOBAL_BRIGHTNESS | GLOBAL_STRETCH | GLOBAL_AUDIO_SENS,
        .params = audio_params, .n_params = 1,
        .render = audio_render,
    },
    {
        .id = "rainbow", .display_name = "Rainbow",
        .global_mask = GLOBAL_BRIGHTNESS | GLOBAL_STRETCH | GLOBAL_SPEED,
        .params = NULL, .n_params = 0,
        .render = rainbow_render,
    },
    {
        .id = "rainbow_window", .display_name = "Rainbow Window",
        .global_mask = GLOBAL_BRIGHTNESS | GLOBAL_STRETCH | GLOBAL_SPEED,
        .params = rainbow_window_params, .n_params = 2,
        .render = rainbow_window_render,
    },
    {
        .id = "discombobulate", .display_name = "Discombobulate",
        .global_mask = GLOBAL_BRIGHTNESS | GLOBAL_STRETCH,
        .params = NULL, .n_params = 0,
        .init = discombobulate_init,
        .render = discombobulate_render,
        .state_size = sizeof(discombobulate_state_t),
    },
};
#define EFFECT_COUNT (sizeof(s_registry) / sizeof(s_registry[0]))

static uint8_t s_state_blob[64];

const effect_desc_t *effects_get_catalog(size_t *count)
{
    if (count) {
        *count = EFFECT_COUNT;
    }
    return s_registry;
}

static uint16_t clamp_effect(uint16_t id)
{
    return id < EFFECT_COUNT ? id : 0;
}

static void enter_effect(const effect_desc_t *eff)
{
    memset(s_state_blob, 0, sizeof(s_state_blob));
    if (eff->init) {
        eff->init(s_state_blob);
    }
}

// On a user-initiated switch, reset params to the new effect's defaults and
// broadcast them so all clients track the change.
static void reset_params(const effect_desc_t *eff)
{
    app_patch_t p = { .mask = STATE_F_PARAMS };
    for (size_t i = 0; i < EFFECT_PARAM_MAX; i++) {
        p.params[i] = (i < eff->n_params) ? eff->params[i].def : 0.0f;
    }
    appstate_apply_patch(APP_SRC_INTERNAL, &p);
}

static void on_safety_changed(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const app_safety_evt_t *e = data;
    if (e) {
        s_safety_scale = e->safety_scale;
        s_latched = e->latched;
    }
}

static void render_task(void *arg)
{
    app_state_t st;
    appstate_get(&st);
    uint16_t cur = clamp_effect(st.effect_id);
    const effect_desc_t *eff = &s_registry[cur];
    enter_effect(eff);

    int64_t last = esp_timer_get_time();
    float t = 0.0f;
    const TickType_t period = pdMS_TO_TICKS(1000 / RENDER_HZ);
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        appstate_get(&st);
        int64_t now = esp_timer_get_time();
        float dt = (float)(now - last) / 1e6f;
        last = now;

        uint16_t want = clamp_effect(st.effect_id);
        if (want != cur) {
            cur = want;
            eff = &s_registry[cur];
            enter_effect(eff);
            reset_params(eff);
            appstate_get(&st);
            t = 0.0f;
        } else {
            t += dt;
        }

        effect_ctx_t ctx = {
            .brightness = st.brightness,
            .stretch = st.stretch,
            .speed = st.speed,
            .audio_sens = st.audio_sens,
            .params = st.params,
            .t = t,
            .dt = dt,
            .state = s_state_blob,
        };
        memcpy(ctx.color, st.color, sizeof(ctx.color));
        audio_get_features(&ctx.audio);

        float rgb[3];
        eff->render(&ctx, rgb);

        float bri_lin = apply_gamma(st.brightness);
        float scale = (st.power && !s_latched) ? s_safety_scale : 0.0f;
        float out[3];
        for (int c = 0; c < 3; c++) {
            out[c] = apply_gamma(clamp01(rgb[c])) * bri_lin * scale;
        }
        laser_set(out[0], out[1], out[2], st.stretch);

        vTaskDelayUntil(&wake, period);
    }
}

esp_err_t effects_init(void)
{
    return app_bus_subscribe(EVT_SAFETY_CHANGED, on_safety_changed, NULL);
}

esp_err_t effects_start_render(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(render_task, "laser_render", 4096, NULL,
                                            18, NULL, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "render task create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
