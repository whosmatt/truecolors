// kick.c
// Reasonably low latency kick drum detector. 
// Selects candidates based on attack, and confirms based on the shape of the decay.

#include "kick.h"

// Parameters derived from a 100 sample high variety curated dataset (2026-07-09)
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

static int s_since = REFRACTORY;
static int s_pending = -1;   // blocks since candidate, -1 when idle
static float s_cand_strength;
static float s_cand_fund;
static float s_ring;
static float s_ema;
static uint32_t s_count;

void kick_init(void)
{
    s_since = REFRACTORY;
    s_pending = -1;
    s_ema = 0.0f;
    s_count = 0;
}

void kick_block(const kick_in_t *in, kick_out_t *out)
{
    out->hit = false;

    if (s_since < REFRACTORY) {
        s_since++;
    }

    float strength = W_FUND * in->flux[0] + W_BODY * in->flux[1] +
                     W_CLICK * in->flux[2];

    if (s_pending >= 0) {
        // anchor to strongest block so we only see the decay, not the attack
        if (strength > s_cand_strength) {
            s_pending = 0;
            s_cand_strength = strength;
            s_cand_fund = in->fund_rms;
            s_ring = 0.0f;
        } else {
            s_pending++;
            s_ring += in->mid_flux + in->treble_flux;
            if (s_pending >= CONFIRM_BLK) {
                bool confirmed = in->fund_rms > SUSTAIN * s_cand_fund &&
                                 s_ring < RING_MAX * s_cand_strength;
                if (confirmed) {
                    s_since = 0;
                    s_count++;
                    float s = s_cand_strength < EMA_MAX ? s_cand_strength : EMA_MAX;
                    s_ema = s_ema <= 0.0f ? s : 0.7f * s_ema + 0.3f * s;
                    out->hit = true;
                }
                s_pending = -1;
            }
        }
    } else {
        bool cand = s_since >= REFRACTORY &&
                    in->flux[0] > BAND_MIN &&
                    (in->flux[1] > BAND_MIN || in->flux[2] > BAND_MIN) &&
                    strength > THRESH_ABS &&
                    strength > EMA_FACTOR * s_ema &&
                    in->mid_flux + in->treble_flux < MT_VETO * strength;
        if (cand) {
            s_pending = 0;
            s_cand_strength = strength;
            s_cand_fund = in->fund_rms;
            s_ring = 0.0f;
        }
    }

    if (out->hit == false) {
        s_ema *= EMA_DECAY;
    }
    out->count = s_count;
}
