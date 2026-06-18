// laser_math.h
// Pure laser width math, no hardware dependency, host-testable.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t on[3];     // on-time ticks per channel, 0 means off
    int32_t cmpa[3];   // set-high tick
    int32_t cmpb[3];   // set-low tick
    int32_t period;    // period ticks
} laser_widths_t;

// Map linear rgb (0..1) and stretch (0..1) to sequentially packed, budget-safe
// per-channel widths. Guarantees the sum of on-times never exceeds the period.
void laser_compute_widths(const float rgb[3], float stretch, laser_widths_t *out);

#ifdef __cplusplus
}
#endif
