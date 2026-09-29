#include "beattrack.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "fe_ctx.h"
#include "tempo.h"

static const char *TAG = "beattrack";

// refit every 0.5 s on 8 s, hold grid until there is a better fit for the last 4 s
#define ESTIMATE_EVERY_MS 500
#define HOLD_SPAN         375     // 4 s the two grids are compared over
#define HOLD_MARGIN       0.02f   // contrast a candidate must win by
#define HOLD_FLOOR        0.03f   // below this the held grid stopped explaining
#define SAME_LAG_FRAC     0.015f  // candidate refines the held grid rather
#define SAME_PHASE_BLOCKS 1.5f    // ... than replacing it
// settle time for startup because stage A needs to fully fill the stage B buffer first
// around 14s
#define SETTLE_BLOCKS     550
#define REFRACTORY        0.5f    // of a period, between emitted beats
#define FREE_THRESH       0.5f    // unlocked: fire on activation

static tempo_buf_t s_buf;
static uint32_t s_block;          // audio blocks since init, audio task only

// seqlock for observations
static volatile uint32_t s_seq;
static volatile float s_obs_period, s_obs_to_next, s_obs_strength;
static volatile uint32_t s_obs_at;
static uint32_t s_seen_seq;

// blocks until next beat for tracking
static bool s_locked;
static float s_period, s_to_next, s_err, s_strength;
static float s_since_beat;
static float s_prev_act;

void beattrack_init(void)
{
    tempo_init(&s_buf);
    s_block = 0;
    s_seq = 0;
    s_seen_seq = 0;
    s_locked = false;
    s_period = 0.0f;
    s_to_next = 0.0f;
    s_err = 0.0f;
    s_strength = 0.0f;
    s_since_beat = 1e6f;
    s_prev_act = 0.0f;
}

// Latch to what the estimator has published instead of slowly filtering
static void apply_observation(float period, float to_next, float strength, uint32_t at)
{
    s_strength = strength;
    if (period <= 1.0f) {
        return;
    }

    // Age the observation from block `at`; the difference is an exact integer.
    float aged = to_next - (float)(s_block - at);
    aged = fmodf(aged, period);
    if (aged <= 0.0f) {
        aged += period;
    }

    if (s_locked) {
        float err = aged - s_to_next;
        if (err > period * 0.5f) err -= period;
        if (err < -period * 0.5f) err += period;
        s_err = err;
    } else {
        s_err = 0.0f;
    }

    s_period = period;
    s_to_next = aged;
    s_locked = strength >= HOLD_FLOOR;
}

void beattrack_block(float activation, beattrack_out_t *out)
{
    tempo_push(&s_buf, activation);
    s_block++;
    s_since_beat += 1.0f;

    uint32_t seq = s_seq;
    if ((seq & 1u) == 0u && seq != s_seen_seq) {
        float p = s_obs_period, tn = s_obs_to_next, st = s_obs_strength;
        uint32_t at = s_obs_at;
        if (s_seq == seq) {
            s_seen_seq = seq;
            apply_observation(p, tn, st, at);
        }
    }

    memset(out, 0, sizeof(*out));
    bool fire = false;

    if (s_locked && s_period > 1.0f) {
        s_to_next -= 1.0f;
        if (s_to_next <= 0.0f) {
            // A phase correction can pull the next beat close to the last one.
            if (s_since_beat >= REFRACTORY * s_period) {
                fire = true;
            }
            s_to_next += s_period;
        }
        out->to_next = s_to_next;
        out->bpm = 60.0f * TEMPO_BLOCK_HZ / s_period;
        out->period = s_period;
    } else if (activation >= FREE_THRESH && s_prev_act < FREE_THRESH &&
               s_since_beat >= 4.0f) {
        fire = true;
    }
    s_prev_act = activation;

    if (fire) {
        s_since_beat = 0.0f;
    }
    out->beat = fire;
    out->locked = s_locked;
    out->err = s_err;
    out->strength = s_strength;
}

// Floating grid for estimator, doesn't use absolute time to avoid degradation
static bool s_hold;
static float s_hold_period, s_hold_to_next, s_hold_contrast;
static uint32_t s_hold_at;
// to_next carried from block `from` to block `to`, wrapped into (0, period].
static float age_to(float to_next, float period, uint32_t from, uint32_t to)
{
    float v = fmodf(to_next - (float)(to - from), period);
    return v <= 0.0f ? v + period : v;
}

static bool same_grid(float period_a, float to_next_a, float period_b, float to_next_b)
{
    if (fabsf(period_a - period_b) > SAME_LAG_FRAC * period_b) {
        return false;
    }
    float d = fmodf(to_next_a - to_next_b, period_b);
    if (d < 0.0f) {
        d += period_b;
    }
    if (d > period_b * 0.5f) {
        d -= period_b;
    }
    return fabsf(d) <= SAME_PHASE_BLOCKS;
}

static void estimator_task(void *arg)
{
    static tempo_buf_t snap;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(ESTIMATE_EVERY_MS));

        // Copied while the audio task writes: a single aligned float store,
        // so the worst case is a snapshot straddling one block.
        if (s_block < SETTLE_BLOCKS + TEMPO_WIN_BLOCKS) {
            continue;
        }
        uint32_t at = s_block;
        memcpy(&snap, &s_buf, sizeof(snap));

        tempo_est_t e;
        if (!tempo_estimate(&snap, &e)) {
            continue;
        }
        float cand_to_next = tempo_next_beat_in(&e);
        float cand_c = tempo_contrast(&snap, e.period, cand_to_next, HOLD_SPAN);

        bool take = true;
        float aged = 0.0f, held_c = 0.0f;
        if (s_hold) {
            aged = age_to(s_hold_to_next, s_hold_period, s_hold_at, at);
            held_c = tempo_contrast(&snap, s_hold_period, aged, HOLD_SPAN);

            // Refine on the same grid, replace only on a clearly better candidate
            take = same_grid(e.period, cand_to_next, s_hold_period, aged) ||
                   cand_c > held_c + HOLD_MARGIN ||
                   held_c < HOLD_FLOOR;
        }

        if (take) {
            s_hold_period = e.period;
            s_hold_to_next = cand_to_next;
            s_hold_contrast = cand_c;
        } else {
            s_hold_to_next = aged;
            s_hold_contrast = held_c;
        }
        s_hold_at = at;
        s_hold = true;

        // Activation time runs FE_CTX_LOOKAHEAD blocks behind audio time.
        float to_next = s_hold_to_next - (float)FE_CTX_LOOKAHEAD;
        while (to_next <= 0.0f) {
            to_next += s_hold_period;
        }

        s_seq++;                       // odd
        s_obs_period = s_hold_period;
        s_obs_to_next = to_next;
        s_obs_strength = s_hold_contrast;
        s_obs_at = at;
        s_seq++;                       // even
    }
}

esp_err_t beattrack_start(void)
{
    // Pin audio to core1
    if (xTaskCreatePinnedToCore(estimator_task, "beattrack", 4096, NULL, 3, NULL, 1)
        != pdPASS) {
        ESP_LOGE(TAG, "estimator task create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
