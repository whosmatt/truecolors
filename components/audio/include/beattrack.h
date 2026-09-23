// beattrack.h
// Keeps a continuous grid between the estimator's batch fits, which carry no
// state of their own.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool beat;
    bool locked;
    float bpm;
    float period;   // blocks
    float to_next;  // blocks until the next predicted beat
    float err;      // last observation's phase correction (blocks)
    float strength;
} beattrack_out_t;

void beattrack_init(void);
esp_err_t beattrack_start(void);

// Once per block from the audio task, with the model's beat activation.
void beattrack_block(float activation, beattrack_out_t *out);

#ifdef __cplusplus
}
#endif
