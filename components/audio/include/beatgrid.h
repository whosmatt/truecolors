// beatgrid.h
// Second stage on top of kick detection: lock onto the repeating kick
// interval and emit predicted, attack-aligned beats. Pure block-rate math.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool beat;     // fire the effect-facing beat this block
    float bpm;     // locked grid tempo, 0 while unlocked
} beatgrid_out_t;

void beatgrid_init(float block_hz);
void beatgrid_block(bool kick_hit, beatgrid_out_t *out);

#ifdef __cplusplus
}
#endif
