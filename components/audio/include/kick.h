// kick.h
// Kick drum detector on the low sub-band fluxes. Pure block-rate math.
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
    float mid_flux;    // 200 Hz - 2 kHz, snare body veto
    float treble_flux; // 2-4 kHz, snare wire veto
} kick_in_t;

typedef struct {
    bool hit;        // true on the block where a kick is confirmed
    uint32_t count;  // total kicks since boot (temporary telemetry counter)
} kick_out_t;

void kick_init(void);
void kick_block(const kick_in_t *in, kick_out_t *out);

#ifdef __cplusplus
}
#endif
