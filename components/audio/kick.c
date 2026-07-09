// kick.c
// Reasonably low latency kick drum detector.
// Selects candidates based on attack, and confirms based on the shape of the decay.

#include "kick.h"
#include "snare.h"

// Parameters derived from a 100 kick + 150 snare/rim/clap sample high variety curated dataset (2026-07-09)
#define REFRACTORY  30       // blocks, 320 ms at 93.75 Hz -> max 187.5 bpm
#define CONFIRM_BLK 3        // decision delay, ~32 ms
#define THRESH_ABS  0.7f     // weighted flux floor (corpus p2 = 0.84)
#define EMA_FACTOR  0.6f     // candidates must reach this fraction of recent kicks
#define EMA_MAX     1.4f     // cap: keeps the gate at <= corpus p2 even after a
                             // loud transient inflates a hit's strength
#define EMA_DECAY   0.998f   // expectation halves in ~3.7 s, re-arms quickly
#define BAND_MIN    0.15f    // k0 p2 = 0.19; one upper band must move too
#define W_FUND      1.4f     // 40-80 Hz dominates the strength
#define W_BODY      1.0f     // 80-140 Hz
#define W_CLICK     0.6f     // 140-220 Hz
#define MT_VETO     1.8f     // joint (mid+treble)/strength cap (corpus p98 = 1.73)
#define SUSTAIN     0.15f    // fund(a+3)/fund(a) floor (corpus p2 = 0.17)
#define RING_MAX    0.9f     // accumulated mid+treble ring vs strength (p98 = 0.73)
#ifndef SNARE_ABS
#define SNARE_ABS   0.35f     // candidate floor for mid/treble-dominant attacks
#endif

static int s_since_k = REFRACTORY;   // per-type refractories: a snare must
static int s_since_s = REFRACTORY;   // not block the kick right after it
static int s_pending = -1;   // blocks since anchor, -1 when idle
static kick_in_t s_anchor;   // features at the anchored block
static float s_cand_total;
static bool s_cand_kick;     // anchor passed the kick candidate gates
static bool s_cand_snare;    // snare path was armed at anchor time
static float s_ring;
static float s_ema;
static uint32_t s_count;
static uint32_t s_snares;

void kick_init(void)
{
    s_since_k = REFRACTORY;
    s_since_s = REFRACTORY;
    s_pending = -1;
    s_ema = 0.0f;
    s_count = 0;
    s_snares = 0;
}

static float strength_of(const kick_in_t *in)
{
    return W_FUND * in->flux[0] + W_BODY * in->flux[1] + W_CLICK * in->flux[2];
}

static bool kick_gates(const kick_in_t *in, float strength)
{
    return in->flux[0] > BAND_MIN &&
           (in->flux[1] > BAND_MIN || in->flux[2] > BAND_MIN) &&
           strength > THRESH_ABS &&
           strength > EMA_FACTOR * s_ema &&
           in->mid_flux + in->treble_flux < MT_VETO * strength;
}

void kick_block(const kick_in_t *in, kick_out_t *out)
{
    out->hit = false;
    out->snare = false;

    if (s_since_k < REFRACTORY) {
        s_since_k++;
    }
    if (s_since_s < REFRACTORY) {
        s_since_s++;
    }

    float strength = strength_of(in);
    float total = strength + in->mid_flux + in->treble_flux;

    if (s_pending >= 0) {
        // Anchor to the strongest block so we only see the decay, not the
        // attack. Kick-qualified blocks take precedence: the mid/treble click
        // peaks on a different block than the kick strength.
        bool kq = s_since_k >= REFRACTORY && kick_gates(in, strength);
        bool better = s_cand_kick
                      ? (kq && strength > strength_of(&s_anchor))
                      : (kq || total > s_cand_total);
        if (better) {
            s_pending = 0;
            s_anchor = *in;
            s_cand_total = total;
            s_cand_kick = kq;
            s_cand_snare = s_cand_snare || s_since_s >= REFRACTORY;
            s_ring = 0.0f;
        } else {
            s_pending++;
            s_ring += in->mid_flux + in->treble_flux;
            if (s_pending >= CONFIRM_BLK) {
                float a_str = strength_of(&s_anchor);
                bool kick_ok = s_cand_kick &&
                               in->fund_rms > SUSTAIN * s_anchor.fund_rms &&
                               s_ring < RING_MAX * a_str;
                if (kick_ok) {
                    s_since_k = 0;
                    s_count++;
                    float s = a_str < EMA_MAX ? a_str : EMA_MAX;
                    s_ema = s_ema <= 0.0f ? s : 0.7f * s_ema + 0.3f * s;
                    out->hit = true;
                } else if (s_cand_snare && snare_classify(&s_anchor)) {
                    s_since_s = 0;
                    s_snares++;
                    out->snare = true;
                }
                s_pending = -1;
            }
        }
    } else {
        bool kick_cand = s_since_k >= REFRACTORY && kick_gates(in, strength);
        bool snare_cand = s_since_s >= REFRACTORY &&
                          in->mid_flux + in->treble_flux > SNARE_ABS;
        if (kick_cand || snare_cand) {
            s_pending = 0;
            s_anchor = *in;
            s_cand_total = total;
            s_cand_kick = kick_cand;
            s_cand_snare = snare_cand;
            s_ring = 0.0f;
        }
    }

    if (out->hit == false) {
        s_ema *= EMA_DECAY;
    }
    out->count = s_count;
    out->snares = s_snares;
}
