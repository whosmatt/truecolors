// beatgrid.c
// Pattern locking on kick hits.
// Hits are stored and autocorrelated to find a repeating loop. 
// Once locked, two things happen:
// 1. The loop corrects incoming hits and compensates for latency
// 2. Incoming hits are tracked and either nudge loop phase or unlock if sufficiently different

#include "beatgrid.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// Tuned against drumloops TODO tune against labeled music
#define HIST         48      // kick timestamps, ~12 s of dense breakbeat
#ifndef P_MIN
#define P_MIN        28.0f   // shortest loop: one beat at ~200 bpm
#endif
#ifndef P_MAX
#define P_MAX        750.0f  // longest loop: ~8 s, two slow bars
#endif
#ifndef SPAN_MIN
#define SPAN_MIN     375.0f  // evidence must span >= 4 s of music
#endif
#ifndef MATCH_FRAC
#define MATCH_FRAC   0.75f   // fraction of hits needing a one-loop partner.
                             // Corpus frontier: 0.85 -> 0 mislocks but many
                             // no-locks on real mixes; 0.75 -> ~5% mislocks,
                             // half of them benign 8th-grid, and real music
                             // self-corrects via the off-grid unlock.
#endif
#ifndef MIN_ELIG
#define MIN_ELIG     4
#endif
#define TMPL_MAX     16      // template slots per loop
#ifndef OFFGRID_LIM
#define OFFGRID_LIM  6       // consecutive off-pattern kicks before unlock
#endif
#define PREDICT_ADV  5.0f    // fire early: confirm delay + flux rise (attack-aligned)
#define PHASE_PULL   0.4f
#define PERIOD_I     0.05f   // integral term: converges period bias to zero
                             // (phase-only tracking drifts out of tolerance)
#ifndef TOL_BASE
#define TOL_BASE     3.0f    // slot tolerance: TOL_BASE + TOL_SCALE * period
#endif
#ifndef TOL_SCALE
#define TOL_SCALE    0.01f   // corpus: 0.02 lets dense snare/hat anchors match
                             // random long periods
#endif

static float s_block_hz;
static int32_t s_now;
static int32_t s_hits[HIST];   // most recent first
static uint8_t s_htype[HIST];  // 1 = kick, 0 = snare
static int s_nhits;
static bool s_locked;
static float s_period;         // loop period P in blocks
static float s_tmpl[TMPL_MAX]; // slot offsets in [0, P), ascending
static uint8_t s_tkick[TMPL_MAX]; // slot majority type: 1 = kick slot
static int s_ntmpl;
static float s_base;           // current loop start, confirm-time base
static int s_slot;             // next template slot to fire
static int s_miss;
static int s_miss_limit;
static int s_offgrid_run;
static int32_t s_last_beat;   // dedupes prediction + detection of one kick

void beatgrid_init(float block_hz)
{
    s_block_hz = block_hz;
    s_now = 0;
    s_nhits = 0;
    s_locked = false;
    s_period = 0.0f;
    s_ntmpl = 0;
    s_miss = 0;
    s_offgrid_run = 0;
    memset(s_hits, 0, sizeof(s_hits));
}

static float tol_of(float p)
{
    return TOL_BASE + TOL_SCALE * p;
}

// How many hits have a partner one loop of p earlier (within tolerance)?
// p_refined returns the mean of the matched deltas: candidates come from
// integer block deltas, the true period usually doesn't.
static int score_period(float p, int *eligible, float *p_refined)
{
    float tol = tol_of(p);
    int matched = 0, elig = 0;
    float acc = 0.0f;
    for (int i = 0; i < s_nhits; i++) {
        float want = (float)s_hits[i] - p;
        if (want < (float)s_hits[s_nhits - 1] - tol) {
            continue;   // history doesn't reach that far back
        }
        elig++;
        for (int j = i + 1; j < s_nhits; j++) {
            if (fabsf((float)s_hits[j] - want) <= tol) {
                matched++;
                acc += (float)(s_hits[i] - s_hits[j]);
                break;
            }
            if ((float)s_hits[j] < want - tol) {
                break;
            }
        }
    }
    *eligible = elig;
    *p_refined = matched > 0 ? acc / (float)matched : p;
    return matched;
}

// Cluster hit offsets modulo p into template slots (mean of each cluster,
// clusters must repeat across loops). Each slot carries its majority hit
// type: kick slots emit beats, snare slots only anchor.
static int build_template(float p, float anchor)
{
    float tol = tol_of(p);
    float off[HIST];
    uint8_t typ[HIST];
    int n = 0;
    for (int i = 0; i < s_nhits; i++) {
        float o = fmodf(anchor - (float)s_hits[i], p);
        if (o < 0.0f) o += p;
        o = p - o;              // forward offset from loop start
        if (o >= p) o -= p;
        // Rotate by +tol so the anchor slot's jitter doesn't split across
        // the modulo boundary (slots sit >= refractory >> tol apart).
        o += tol;
        if (o >= p) o -= p;
        typ[n] = s_htype[i];
        off[n++] = o;
    }
    // insertion sort pairs, n <= HIST
    for (int i = 1; i < n; i++) {
        float v = off[i];
        uint8_t tv = typ[i];
        int j = i - 1;
        while (j >= 0 && off[j] > v) {
            off[j + 1] = off[j];
            typ[j + 1] = typ[j];
            j--;
        }
        off[j + 1] = v;
        typ[j + 1] = tv;
    }
    s_ntmpl = 0;
    int i = 0;
    while (i < n && s_ntmpl < TMPL_MAX) {
        int cnt = 1;
        float acc = off[i];
        int kv = typ[i];
        while (i + cnt < n && off[i + cnt] - off[i] <= tol) {
            acc += off[i + cnt];
            kv += typ[i + cnt];
            cnt++;
        }
        if (cnt >= 2) {   // slot must repeat across loops
            float v = acc / (float)cnt - tol;   // undo the rotation
            if (v < 0.0f) v += p;
            s_tkick[s_ntmpl] = (2 * kv >= cnt);
            s_tmpl[s_ntmpl++] = v;
        }
        i += cnt;
    }
    // Rotation kept clusters intact but slot order may have wrapped.
    for (int a = 1; a < s_ntmpl; a++) {
        float v = s_tmpl[a];
        uint8_t tv = s_tkick[a];
        int b = a - 1;
        while (b >= 0 && s_tmpl[b] > v) {
            s_tmpl[b + 1] = s_tmpl[b];
            s_tkick[b + 1] = s_tkick[b];
            b--;
        }
        s_tmpl[b + 1] = v;
        s_tkick[b + 1] = tv;
    }
    // Hits of one type can't confirm closer than the detector refractory;
    // closer slots are drift phantoms — keep the first of each pair.
    int w = 1;
    for (int a = 1; a < s_ntmpl; a++) {
        if (s_tmpl[a] - s_tmpl[w - 1] >= 25.0f) {
            s_tkick[w] = s_tkick[a];
            s_tmpl[w++] = s_tmpl[a];
        } else if (s_tkick[a]) {
            s_tkick[w - 1] = 1;   // merged slot keeps kick precedence
        }
    }
    if (w > 1 && p - s_tmpl[w - 1] + s_tmpl[0] < 25.0f) {
        if (s_tkick[w - 1]) {
            s_tkick[0] = 1;
        }
        w--;   // last slot wraps onto the first
    }
    s_ntmpl = w;
    return s_ntmpl;
}

static bool try_lock(void)
{
    if (s_nhits < MIN_ELIG + 2) {
        return false;
    }
    float span = (float)(s_hits[0] - s_hits[s_nhits - 1]);

    // Candidate periods: all pair deltas in range, ascending, so the first
    // success is the smallest (most parsimonious) loop.
    float cand[HIST * 4];
    int nc = 0;
    for (int i = 0; i < s_nhits && nc < (int)(sizeof(cand) / sizeof(cand[0])); i++) {
        for (int j = i + 1; j < s_nhits && nc < (int)(sizeof(cand) / sizeof(cand[0])); j++) {
            float d = (float)(s_hits[i] - s_hits[j]);
            if (d >= P_MIN && d <= P_MAX) {
                cand[nc++] = d;
            }
        }
    }
    for (int i = 1; i < nc; i++) {   // insertion sort ascending
        float v = cand[i];
        int j = i - 1;
        while (j >= 0 && cand[j] > v) {
            cand[j + 1] = cand[j];
            j--;
        }
        cand[j + 1] = v;
    }

    float last_tested = -1e9f;
    for (int c = 0; c < nc; c++) {
        float p = cand[c];
        if (p - last_tested < tol_of(p)) {
            continue;   // near-duplicate of a tested candidate
        }
        last_tested = p;
        if (span < 2.0f * p || span < SPAN_MIN) {
            continue;
        }
        int elig = 0;
        float p_ref = p;
        int m = score_period(p, &elig, &p_ref);
        if (elig >= MIN_ELIG && (float)m >= MATCH_FRAC * (float)elig) {
            if (build_template(p_ref, (float)s_hits[0]) < 1) {
                continue;
            }
            s_period = p_ref;
            // Anchor the loop so the newest hit lands on its nearest slot.
            int k_near = 0;
            float e_near = 1e9f;
            for (int k = 0; k < s_ntmpl; k++) {
                float e = -s_tmpl[k];
                e -= p * roundf(e / p);
                if (fabsf(e) < fabsf(e_near)) {
                    e_near = e;
                    k_near = k;
                }
            }
            s_base = (float)s_hits[0] - s_tmpl[k_near];
            s_slot = 0;
            while (s_base + s_tmpl[s_slot] <= (float)s_now) {
                if (++s_slot >= s_ntmpl) {
                    s_slot = 0;
                    s_base += s_period;
                }
            }
            s_miss = 0;
            s_miss_limit = 2 * s_ntmpl > 8 ? 2 * s_ntmpl : 8;
            s_offgrid_run = 0;
            return true;
        }
    }
    return false;
}

void beatgrid_block(bool kick_hit, bool snare_hit, beatgrid_out_t *out)
{
    s_now++;
    out->beat = false;
    bool hit = kick_hit || snare_hit;

    if (hit) {
        memmove(&s_hits[1], &s_hits[0], (HIST - 1) * sizeof(s_hits[0]));
        memmove(&s_htype[1], &s_htype[0], (HIST - 1) * sizeof(s_htype[0]));
        s_hits[0] = s_now;
        s_htype[0] = kick_hit ? 1 : 0;
        if (s_nhits < HIST) {
            s_nhits++;
        }
    }

    if (!s_locked) {
        if (kick_hit) {
            out->beat = true;
            s_last_beat = s_now;
        }
        if (hit) {
            s_locked = try_lock();
        }
        out->bpm = 0.0f;
        out->period = 0.0f;
        return;
    }

    if (hit) {
        // Detected kicks always shine through, locked or not: the grid adds
        // predicted slots and timing, it must not eat fills and variations
        // (the kick detector's false-positive source — snares — is now
        // classified away, so suppression buys nothing).
        if (kick_hit && s_now - s_last_beat > 15) {
            out->beat = true;
            s_last_beat = s_now;
        }
        s_miss = 0;   // kicks are arriving, keep coasting through fills
        // Circular distance to the nearest template slot.
        float rel = fmodf((float)s_now - s_base, s_period);
        if (rel < 0.0f) rel += s_period;
        float best = 1e9f;
        for (int i = 0; i < s_ntmpl; i++) {
            float e = rel - s_tmpl[i];
            e -= s_period * roundf(e / s_period);
            if (fabsf(e) < fabsf(best)) {
                best = e;
            }
        }
        float tol = tol_of(s_period);
        float neutral = 0.25f * s_period / (float)s_ntmpl;
        if (neutral < 3.0f * tol) {
            neutral = 3.0f * tol;
        }
        if (fabsf(best) <= 2.0f * tol) {
            // Hit detection isn't perfect, track and nudge phase to maintain the loop
            float pull = fabsf(best) <= tol ? PHASE_PULL : 0.5f * PHASE_PULL;
            s_base += pull * best;
            s_period += PERIOD_I * best;
            s_offgrid_run = 0;
        } else if (fabsf(best) <= neutral) {
            // Accent/double near a slot: neutral, no penalty, no pull.
            // This is needed for stability in breakbeats, but needs more work
        } else if (++s_offgrid_run >= OFFGRID_LIM) {
            s_locked = false;   // the pattern really changed; keep history
            s_period = 0.0f;
        }
        // Off-pattern hits only influence the unlock accounting above.
    }

    // Snare slots anchor the grid but only kick slots emit beats.
    if (s_locked && (float)s_now >= s_base + s_tmpl[s_slot] - PREDICT_ADV) {
        if (s_tkick[s_slot] && s_now - s_last_beat > 15) {
            out->beat = true;
            s_last_beat = s_now;
        }
        if (++s_slot >= s_ntmpl) {
            s_slot = 0;
            s_base += s_period;
        }
        if (++s_miss > s_miss_limit) {
            s_locked = false;   // hitless loops: breakdown or track end
            s_period = 0.0f;
        }
    }

    int nkick = 0;
    for (int k = 0; k < s_ntmpl; k++) {
        nkick += s_tkick[k];
    }
    out->bpm = s_locked ? 60.0f * s_block_hz * (float)nkick / s_period : 0.0f;
    out->period = s_locked ? s_period : 0.0f;
}
