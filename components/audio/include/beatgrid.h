// beatgrid.h
// Second stage on top of kick detection: lock onto the repeating kick
// interval and emit predicted, attack-aligned beats. Pure block-rate math.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool beat;      // fire the effect-facing beat this block
    bool grid_beat; // metronome: evenly spaced ticks at the locked kick rate
                    // (= bpm), anchored on a kick slot; never fires unlocked
    float bpm;      // locked kick rate (slots/min), 0 while unlocked
    float period;   // locked loop period in blocks, 0 while unlocked
    float phase;    // blocks into the locked loop [0, period); 0 unlocked
    float nudge;    // PLL phase shift applied this block (blocks), 0 otherwise
    float err;      // phase error of this block's matched hit (blocks)
} beatgrid_out_t;

void beatgrid_init(float block_hz);
// Kick hits are beats; snare hits are additional pattern anchors (they
// stabilize the grid but only kick-typed slots emit beats). hit_off is the
// hit's peak-interpolated time relative to this block (blocks, <= 0).
void beatgrid_block(bool kick_hit, bool snare_hit, float hit_off,
                    beatgrid_out_t *out);

#ifdef __cplusplus
}
#endif
