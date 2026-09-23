#include "tempo.h"

#include <math.h>
#include <string.h>

#ifndef FRAC_STEP
#define FRAC_STEP    0.05f
#endif   // fractional lag grid
#define SUB_ACCEPT   0.6f    // accept L/k while it still scores this fraction
#define SUB_ROUNDS   3
#define JOINT_SPAN   0.01f   // +/-1% around the chosen lag
#define JOINT_STEPS  41
#define PHASE_STEP   0.1f

static float s_work[TEMPO_WIN_BLOCKS];   // window, oldest first, mean removed
static float s_ac[TEMPO_AC_MAX];

void tempo_init(tempo_buf_t *b)
{
    memset(b, 0, sizeof(*b));
}

void tempo_push(tempo_buf_t *b, float activation)
{
    b->ring[b->head] = activation;
    b->head = (b->head + 1) % TEMPO_WIN_BLOCKS;
    if (b->n < TEMPO_WIN_BLOCKS) {
        b->n++;
    }
}

static float interp(const float *v, int len, float x)
{
    if (x < 0.0f || x >= (float)(len - 1)) {
        return 0.0f;
    }
    int i = (int)x;
    float f = x - (float)i;
    return v[i] + f * (v[i + 1] - v[i]);
}

static float score_lag(float L)
{
    return interp(s_ac, TEMPO_AC_MAX, L) +
           0.5f * interp(s_ac, TEMPO_AC_MAX, 2.0f * L) +
           (1.0f / 3.0f) * interp(s_ac, TEMPO_AC_MAX, 3.0f * L);
}

bool tempo_estimate(const tempo_buf_t *b, tempo_est_t *out)
{
    out->valid = false;
    if (b->n < TEMPO_WIN_BLOCKS) {
        return false;
    }
    const int n = TEMPO_WIN_BLOCKS;

    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float v = b->ring[(b->head + i) % TEMPO_WIN_BLOCKS];
        s_work[i] = v;
        sum += v;
    }
    float mean = sum / (float)n;
    for (int i = 0; i < n; i++) {
        s_work[i] -= mean;
    }

    // Unbiased: each lag divided by its overlap, not by n. Direct rather than
    // by FFT -- same quantity, cheaper for 314 of 750 lags.
    for (int L = 0; L < TEMPO_AC_MAX; L++) {
        int m = n - L;
        if (m <= 0) {
            s_ac[L] = 0.0f;
            continue;
        }
        float acc = 0.0f;
        for (int i = 0; i < m; i++) {
            acc += s_work[i] * s_work[i + L];
        }
        s_ac[L] = acc / (float)m;
    }
    if (s_ac[0] <= 0.0f) {
        return false;   // silence: no variation to correlate
    }

    const float lag_lo = 60.0f * TEMPO_BLOCK_HZ / TEMPO_BPM_MAX;
    const float lag_hi = 60.0f * TEMPO_BLOCK_HZ / TEMPO_BPM_MIN;

    // Fractional grid: on integers, 174 BPM (32.33 blocks) loses to lag 97.
    float best_L = lag_lo, best_s = -1e30f;
    for (float L = lag_lo; L <= lag_hi; L += FRAC_STEP) {
        float s = score_lag(L);
        if (s > best_s) {
            best_s = s;
            best_L = L;
        }
    }

    // Looped material correlates as strongly at the bar as at the beat.
    for (int r = 0; r < SUB_ROUNDS; r++) {
        bool moved = false;
        for (int k = 2; k <= 3; k++) {
            float cand = best_L / (float)k;
            if (cand < lag_lo) {
                continue;
            }
            if (score_lag(cand) >= SUB_ACCEPT * score_lag(best_L)) {
                best_L = cand;
                moved = true;
                break;
            }
        }
        if (!moved) {
            break;
        }
    }

    // Joint: a period wrong by 0.1% drifts a whole block over the window, so
    // the best phase for it alone sits ~10 ms off.
    float top = -1e30f, top_lag = best_L, top_phase = 0.0f;
    int top_hits = 0;
    for (int i = 0; i < JOINT_STEPS; i++) {
        float lag = best_L * (1.0f - JOINT_SPAN +
                              2.0f * JOINT_SPAN * (float)i / (float)(JOINT_STEPS - 1));
        if (lag < 1.0f) {
            continue;
        }
        for (float ph = 0.0f; ph < lag; ph += PHASE_STEP) {
            float acc = 0.0f;
            int hits = 0;
            for (float t = ph; t < (float)(n - 1); t += lag) {
                acc += interp(s_work, n, t);
                hits++;
            }
            if (hits && acc > top) {
                top = acc;
                top_lag = lag;
                top_phase = ph;
                top_hits = hits;
            }
        }
    }

    out->valid = true;
    out->period = top_lag;
    out->phase = top_phase;
    out->bpm = 60.0f * TEMPO_BLOCK_HZ / top_lag;
    // Mean, not sum: a sum scales with how many beats fit the window.
    out->strength = top_hits ? top / (float)top_hits : 0.0f;
    return true;
}

float tempo_next_beat_in(const tempo_est_t *e)
{
    if (!e->valid || e->period <= 0.0f) {
        return 0.0f;
    }

    float t = e->phase;
    float last = (float)(TEMPO_WIN_BLOCKS - 1);
    float k = floorf((last - t) / e->period) + 1.0f;
    return t + k * e->period - last;
}
