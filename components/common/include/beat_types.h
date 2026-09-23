// beat_types.h
// Shared so the audio task can drive a model without depending on the
// component that owns it.
#pragma once

#include <stdbool.h>

typedef struct {
    float beat;         // 0..1 activation; the grid consumes this directly
    float beat_offset;  // 0..1, sub-block onset position
    float hit[4];       // kick / snare / hihat / none
    float music;        // 0..1 per block; decide on a window, not one block
    bool valid;
} beat_infer_t;
