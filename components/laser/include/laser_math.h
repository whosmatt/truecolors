// laser_math.h
// Pure laser width math, no hardware dependency, host-testable.
#pragma once

#include <stdbool.h>
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
// per-channel widths. The MCPWM up-counter counts [0, period-1], so every
// compare is kept at or below period-1; a width of period-1 means fully on
// (laser.c drives it DC instead of leaving a one-tick notch).
// keepalive floors zero channels at MIN_ON_TICKS instead of turning them off.
void laser_compute_widths(const float rgb[3], float stretch, bool keepalive,
                          laser_widths_t *out);

#ifdef __cplusplus
}
#endif
