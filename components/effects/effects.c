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

static volatile bool s_epilepsy_safe = true;

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

// Inverse of hsv2rgb, for effects that rotate the main color's hue.
static void rgb2hsv(const float in[3], float *h, float *s, float *v)
{
    float mx = fmaxf(in[0], fmaxf(in[1], in[2]));
    float mn = fminf(in[0], fminf(in[1], in[2]));
    float d = mx - mn;
    *v = mx;
    *s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 0.0f) {
        *h = 0.0f;
        return;
    }
    float hh;
    if (mx == in[0]) {
        hh = (in[1] - in[2]) / d;
    } else if (mx == in[1]) {
        hh = 2.0f + (in[2] - in[0]) / d;
    } else {
        hh = 4.0f + (in[0] - in[1]) / d;
    }
    hh /= 6.0f;
    *h = hh - floorf(hh);
}

// Attack/release envelope follower, time constants in seconds.
static float env_follow(float env, float target, float atk, float rel, float dt)
{
    float tau = target > env ? atk : rel;
    float k = tau > 0.001f ? 1.0f - expf(-dt / tau) : 1.0f;
    return env + (target - env) * k;
}

// Crossfade bass -> mid -> treble on a 0..2 slider.
static float band_level(const effect_ctx_t *ctx, float b)
{
    if (b < 0.0f) b = 0.0f;
    if (b > 2.0f) b = 2.0f;
    int lo = b < 1.0f ? 0 : 1;
    float f = b - lo;
    return ctx->audio.bands[lo] * (1.0f - f) + ctx->audio.bands[lo + 1] * f;
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

static const char *const s_band_labels[] = { "bass", "mid", "treble", NULL };

static const effect_param_def_t breathe_params[] = {
    { "depth", 0.0f, 1.0f, 0.8f, .unit = "%", .unit_scale = 100.0f },
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

typedef struct {
    float env;
    float hue_off;
    float hue_vis;    // crossfaded follower of hue_off
    float prev_beat;
    float since_step;
} audio_state_t;

static const effect_param_def_t audio_params[] = {
    { "band", 0.0f, 2.0f, 0.0f, .labels = s_band_labels },
    // beat pulse mixed into the brightness follower; 100% = a beat alone
    // reaches full brightness
    { "beat → brightness", 0.0f, 2.0f, 0.0f, .unit = "%", .unit_scale = 100.0f },
    { "beat hue step", 0.0f, 1.0f, 0.0f, .unit = "°", .unit_scale = 360.0f },
    { "attack", 0.0f, 0.3f, 0.01f,
      .safe_min = 0.10f, .unit = "ms", .unit_scale = 1000.0f },
    { "release", 0.02f, 1.5f, 0.15f,
      .safe_min = 0.60f, .unit = "ms", .unit_scale = 1000.0f },
};

// Level-reactive main color. step > 0 rotates an internal hue offset on each
// detected beat; the set color itself is never modified.
static void audio_render(const effect_ctx_t *ctx, float out[3])
{
    audio_state_t *s = ctx->state;
    float step = ctx->params[2];

    s->since_step += ctx->dt;
    if (ctx->audio.beat > s->prev_beat + 0.1f && s->since_step > 0.25f) {
        s->hue_off += step;
        s->hue_off -= floorf(s->hue_off);
        s->since_step = 0.0f;
    }
    s->prev_beat = ctx->audio.beat;

    // Band RMS (scaled by sens) and the beat pulse mix into one drive signal before atk/rel follower
    float drive = band_level(ctx, ctx->params[0]) * ctx->audio_sens * 2.0f +
                  ctx->audio.beat * ctx->params[1];
    s->env = env_follow(s->env, drive, ctx->params[3], ctx->params[4], ctx->dt);
    float level = clamp01(s->env);

    float col[3] = { ctx->color[0], ctx->color[1], ctx->color[2] };
    if (step > 0.0f) {
        // Quick crossfade (~30 ms tau) masks the kick-detection latency and
        // softens the step without blurring it.
        float dh = s->hue_off - s->hue_vis;
        dh -= floorf(dh + 0.5f);
        s->hue_vis += dh * (1.0f - expf(-ctx->dt / 0.03f));
        s->hue_vis -= floorf(s->hue_vis);

        float h, sat, v;
        rgb2hsv(ctx->color, &h, &sat, &v);
        hsv2rgb(h + s->hue_vis, sat, v, col);
    }
    for (int c = 0; c < 3; c++) {
        out[c] = col[c] * level;
    }
}

typedef struct {
    float env[3];
} spectrum_state_t;

static const effect_param_def_t spectrum_params[] = {
    { "attack", 0.0f, 0.3f, 0.01f,
      .safe_min = 0.10f, .unit = "ms", .unit_scale = 1000.0f },
    { "release", 0.02f, 1.5f, 0.15f,
      .safe_min = 0.60f, .unit = "ms", .unit_scale = 1000.0f },
    { "hue", 0.0f, 1.0f, 0.0f, .unit = "°", .unit_scale = 360.0f },
};

// Bass, mid, treble on three hues 120° apart; hue 0 = plain R, G, B.
static void spectrum_render(const effect_ctx_t *ctx, float out[3])
{
    spectrum_state_t *s = ctx->state;
    out[0] = out[1] = out[2] = 0.0f;
    for (int c = 0; c < 3; c++) {
        s->env[c] = env_follow(s->env[c], ctx->audio.bands[c],
                               ctx->params[0], ctx->params[1], ctx->dt);
        float level = clamp01(s->env[c] * ctx->audio_sens * 2.0f);
        float col[3];
        hsv2rgb(ctx->params[2] + c / 3.0f, 1.0f, 1.0f, col);
        for (int k = 0; k < 3; k++) {
            out[k] += col[k] * level;   // rotated bands can share an output
        }                               // channel; render loop clamps the sum
    }
}

typedef struct {
    float ph[3];
    float lvl;
} lava_state_t;

static const effect_param_def_t lava_params[] = {
    { "window", 0.0f, 1.0f, 0.25f, .unit = "°", .unit_scale = 360.0f },
    { "center", 0.0f, 1.0f, 0.5f, .unit = "°", .unit_scale = 360.0f },
    { "audio", 0.0f, 2.0f, 0.0f, .unit = "%", .unit_scale = 100.0f },
    { "band", 0.0f, 2.0f, 0.0f, .labels = s_band_labels },
};

// multi-lfo color drift with audio coupled to speed
static void lava_render(const effect_ctx_t *ctx, float out[3])
{
    static const float freq[3] = { 1.0f / 41.0f, 1.0f / 59.0f, 1.0f / 83.0f };
    lava_state_t *s = ctx->state;

    s->lvl = env_follow(s->lvl, band_level(ctx, ctx->params[3]), 0.05f, 0.3f, ctx->dt);
    // Squared level for contrast; full audio slider on a loud hit stirs the
    // drift at ~25x so it visibly reacts despite the slow LFO periods.
    float rate = 2.0f * ctx->speed * (1.0f + ctx->params[2] * 12.0f * s->lvl * s->lvl);
    for (int i = 0; i < 3; i++) {
        s->ph[i] += TWO_PI * freq[i] * rate * ctx->dt;
        if (s->ph[i] > TWO_PI) s->ph[i] -= TWO_PI;
    }

    float hue = ctx->params[1] +
                ctx->params[0] * (0.3f * sinf(s->ph[0]) + 0.2f * sinf(s->ph[1]));
    float v = 0.75f + 0.25f * sinf(s->ph[2]);
    hsv2rgb(hue, 1.0f, v, out);
}


// Full hue cycle, ignores the set color.
static void rainbow_render(const effect_ctx_t *ctx, float out[3])
{
    float hue = ctx->speed * 0.1f * ctx->t;
    hsv2rgb(hue, 1.0f, 1.0f, out);
}

static const effect_param_def_t rainbow_window_params[] = {
    { "window", 0.0f, 1.0f, 0.25f, .unit = "°", .unit_scale = 360.0f },
    { "center", 0.0f, 1.0f, 0.5f, .unit = "°", .unit_scale = 360.0f },
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
    bool blue;         // current half-cycle, toggles red <-> blue
} discombobulate_state_t;

static const effect_param_def_t discombobulate_params[] = {
    // mean flash rate: pick() draws 2 + speed*(13..23) Hz; default = 7 Hz
    { "speed", 0.0f, 1.0f, 0.2778f,
      .unit = "Hz", .unit_scale = 18.0f, .unit_offset = 2.0f },
    { "ratio", 0.0f, 1.0f, 0.5f, .unit = "%", .unit_scale = 100.0f },
};

static void discombobulate_pick(discombobulate_state_t *s, float speed, float ratio)
{
    float freq = 2.0f + speed * (13.0f + rand01() * 10.0f);
    s->blue = !s->blue;
    s->remaining += (s->blue ? (1.0f - ratio) : ratio) / freq;
}

// Red/blue alternating strobe.
static void discombobulate_render(const effect_ctx_t *ctx, float out[3])
{
    discombobulate_state_t *s = ctx->state;
    float speed = clamp01(ctx->params[0]);
    float ratio = clamp01(ctx->params[1]);
    s->remaining -= ctx->dt;
    while (s->remaining <= 0.0f) {
        discombobulate_pick(s, speed, ratio);
    }
    out[0] = s->blue ? 0.0f : 1.0f;
    out[1] = 0.0f;
    out[2] = s->blue ? 1.0f : 0.0f;
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
        .speed_unit = "Hz", .speed_scale = 1.0f,
    },
    {
        .id = "audio", .display_name = "Audio",
        .global_mask = GLOBAL_COLOR | GLOBAL_BRIGHTNESS | GLOBAL_STRETCH | GLOBAL_AUDIO_SENS,
        .params = audio_params, .n_params = 5,
        .render = audio_render,
        .state_size = sizeof(audio_state_t),
        .keepalive = true,
    },
    {
        .id = "rainbow", .display_name = "Rainbow",
        .global_mask = GLOBAL_BRIGHTNESS | GLOBAL_STRETCH | GLOBAL_SPEED,
        .params = NULL, .n_params = 0,
        .render = rainbow_render,
        .speed_unit = "Hz", .speed_scale = 0.1f,
    },
    {
        .id = "rainbow_window", .display_name = "Rainbow Window",
        .global_mask = GLOBAL_BRIGHTNESS | GLOBAL_STRETCH | GLOBAL_SPEED,
        .params = rainbow_window_params, .n_params = 2,
        .render = rainbow_window_render,
        .speed_unit = "Hz", .speed_scale = 0.1f,
    },
    {
        .id = "discombobulate", .display_name = "Discombobulate",
        .global_mask = GLOBAL_BRIGHTNESS | GLOBAL_STRETCH,
        .params = discombobulate_params, .n_params = 2,
        .render = discombobulate_render,
        .state_size = sizeof(discombobulate_state_t),
        .keepalive = true,
        .epilepsy_unsafe = true,
    },
    {
        .id = "spectrum", .display_name = "Spectrum",
        .global_mask = GLOBAL_BRIGHTNESS | GLOBAL_STRETCH | GLOBAL_AUDIO_SENS,
        .params = spectrum_params, .n_params = 3,
        .render = spectrum_render,
        .state_size = sizeof(spectrum_state_t),
        .keepalive = true,
    },
    {
        .id = "lava", .display_name = "Lava Drift",
        .global_mask = GLOBAL_BRIGHTNESS | GLOBAL_STRETCH | GLOBAL_SPEED,
        .params = lava_params, .n_params = 4,
        .render = lava_render,
        .state_size = sizeof(lava_state_t),
        // LFO periods are fixed; speed is a rate multiplier, not a frequency
        .speed_unit = "×", .speed_scale = 2.0f,
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

        bool epi_safe = s_epilepsy_safe;
        if (epi_safe) {
            // st is a local copy; clamp params to their declared safe range.
            for (size_t i = 0; i < eff->n_params; i++) {
                const effect_param_def_t *d = &eff->params[i];
                if (d->safe_min > d->min && st.params[i] < d->safe_min) {
                    st.params[i] = d->safe_min;
                }
                if (d->safe_max != 0.0f && st.params[i] > d->safe_max) {
                    st.params[i] = d->safe_max;
                }
            }
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
            .epilepsy_safe = epi_safe,
        };
        memcpy(ctx.color, st.color, sizeof(ctx.color));
        audio_get_features(&ctx.audio);

        bool blocked = epi_safe && eff->epilepsy_unsafe;
        float rgb[3] = { 0.0f, 0.0f, 0.0f };
        if (!blocked) {
            eff->render(&ctx, rgb);
        }

        float bri_lin = apply_gamma(st.brightness);
        float scale = (st.power && !s_latched) ? s_safety_scale : 0.0f;
        // Keepalive only while the light is actually running: the power
        // switch, safety latch, boot gate and brightness zero stay true black.
        bool keepalive = !blocked && eff->keepalive && scale > 0.0f && st.brightness > 0.0f;
        float out[3];
        for (int c = 0; c < 3; c++) {
            out[c] = apply_gamma(clamp01(rgb[c])) * bri_lin * scale;
        }
        laser_set(out[0], out[1], out[2], st.stretch, keepalive);

        vTaskDelayUntil(&wake, period);
    }
}

void effects_set_epilepsy_safe(bool on)
{
    s_epilepsy_safe = on;
}

bool effects_get_epilepsy_safe(void)
{
    return s_epilepsy_safe;
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
