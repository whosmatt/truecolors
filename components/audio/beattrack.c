#include "beattrack.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "fe_ctx.h"
#include "tempo.h"

static const char *TAG = "beattrack";

#define ESTIMATE_EVERY_MS 1000
// Untuned: only the batch fit underneath has been measured.
#define LOCK_STRENGTH     0.15f
#define UNLOCK_STRIKES    3
#define PERIOD_GAIN       0.25f
#define PHASE_GAIN        0.25f
#define SETTLE_BLOCKS     375     // 4 s of AGC convergence, discarded
#define BIG_ERR_FRAC      0.15f   // phase jump needing confirmation
#define AGREE_FRAC        0.01f   // two observations this close agree
#define DISAGREE_FRAC     0.02f   // ... and this far from the tracked period
#define REFRACTORY        0.5f    // of a period, between emitted beats
#define FREE_THRESH       0.5f    // unlocked: fire on the activation

static tempo_buf_t s_buf;
static uint32_t s_block;          // audio blocks since init, audio task only

// Seqlock: odd sequence while writing, so the reader never sees a partial
// observation.
static volatile uint32_t s_seq;
static volatile float s_obs_period, s_obs_to_next, s_obs_strength;
static volatile uint32_t s_obs_at;
static uint32_t s_seen_seq;

// Audio task only. Blocks-until-next-beat rather than an absolute time: a
// float block counter loses 0.25 blocks of resolution after ~10 h.
static bool s_locked;
static float s_period, s_to_next, s_err, s_strength;
static float s_since_beat;
static int s_strikes;
static float s_prev_act;
static float s_last_obs_period;   // for the agreement test
static float s_pending_err;       // large phase error awaiting confirmation

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
    s_strikes = 0;
    s_prev_act = 0.0f;
    s_last_obs_period = 0.0f;
    s_pending_err = 0.0f;
}

static void apply_observation(float period, float to_next, float strength, uint32_t at)
{
    s_strength = strength;
    if (strength < LOCK_STRENGTH) {
        if (s_locked && ++s_strikes >= UNLOCK_STRIKES) {
            s_locked = false;
            s_strikes = 0;
        }
        return;
    }
    s_strikes = 0;
    if (period <= 1.0f) {
        return;
    }

    // Age the observation from block `at`; the difference is an exact integer.
    float aged = to_next - (float)(s_block - at);
    aged = fmodf(aged, period);
    if (aged <= 0.0f) {
        aged += period;
    }

    // Acquire only on two agreeing observations: one bad window locks the
    // wrong tempo, and the gains below take tens of seconds to walk back.
    bool agrees = s_last_obs_period > 0.0f &&
                  fabsf(period - s_last_obs_period) < AGREE_FRAC * period;
    float prev_obs = s_last_obs_period;
    s_last_obs_period = period;

    if (!s_locked) {
        if (!agrees) {
            return;
        }
        s_period = period;
        s_to_next = aged;
        s_locked = true;
        s_err = 0.0f;
        return;
    }

    // Agreeing with each other but not with the tracked period: snap.
    if (agrees && fabsf(period - s_period) > DISAGREE_FRAC * s_period &&
        fabsf(prev_obs - s_period) > DISAGREE_FRAC * s_period) {
        s_period = period;
        s_to_next = aged;
        s_err = 0.0f;
        return;
    }

    float err = aged - s_to_next;
    if (err > period * 0.5f) err -= period;
    if (err < -period * 0.5f) err += period;

    // An octave jump is a re-lock, not a nudge.
    if (fabsf(period - s_period) > 0.25f * s_period) {
        s_period = period;
        s_to_next = aged;
        s_err = 0.0f;
        return;
    }
    s_period += PERIOD_GAIN * (period - s_period);
    s_err = err;

    // A large jump is usually one ambiguous window, not the beat moving.
    if (fabsf(err) > BIG_ERR_FRAC * period) {
        bool confirms = s_pending_err != 0.0f &&
                        (s_pending_err > 0.0f) == (err > 0.0f) &&
                        fabsf(err - s_pending_err) < BIG_ERR_FRAC * period;
        s_pending_err = err;
        if (!confirms) {
            return;
        }
        s_to_next += err;
        s_pending_err = 0.0f;
    } else {
        s_pending_err = 0.0f;
        s_to_next += PHASE_GAIN * err;
    }

    while (s_to_next <= 0.0f) {
        s_to_next += s_period;
    }
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

        // Activation time runs FE_CTX_LOOKAHEAD blocks behind audio time.
        float to_next = tempo_next_beat_in(&e) - (float)FE_CTX_LOOKAHEAD;
        while (to_next <= 0.0f) {
            to_next += e.period;
        }

        s_seq++;                       // odd
        s_obs_period = e.period;
        s_obs_to_next = to_next;
        s_obs_strength = e.strength;
        s_obs_at = at;
        s_seq++;                       // even
    }
}

esp_err_t beattrack_start(void)
{
    // Core 1 with the audio chain, below the audio task: an estimate spans
    // several blocks and must not delay one. Core 0 stays for network work.
    if (xTaskCreatePinnedToCore(estimator_task, "beattrack", 4096, NULL, 3, NULL, 1)
        != pdPASS) {
        ESP_LOGE(TAG, "estimator task create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
