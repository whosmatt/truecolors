// kick.h
// Onset detector/classifier on the low sub-band fluxes: one state machine
// anchors each attack and classifies it as kick or snare at confirm time,
// so both hit types share the same latency. Pure block-rate math.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One analysis block of detector inputs, all AGC-normalized and >= 0.
typedef struct {
    float flux[3];     // kick sub-band fluxes: 40-80 / 80-140 / 140-220 Hz
    float fund_rms;    // 40-80 Hz level, for the post-attack sustain check
    float mid_flux;    // 200 Hz - 2 kHz
    float treble_flux; // 2-4 kHz
} kick_in_t;

typedef struct {
    bool hit;          // kick confirmed this block
    bool snare;        // snare/clap/rim confirmed this block (never with hit)
    uint32_t count;    // total kicks since boot
    uint32_t snares;   // total snare-class hits since boot
} kick_out_t;

void kick_init(void);
void kick_block(const kick_in_t *in, kick_out_t *out);

#ifdef __cplusplus
}
#endif
