// melflux.h
// 16 mel-band flux values per block, the second half of the 28 features. 
// Needs to be C99 so truecolors-ml can compile it
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mel_filters.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEL_WINDOW  1024               // previous block + current block
#define MEL_HALF    (MEL_WINDOW / 2)   // the complex FFT length

typedef struct {
    float hann[MEL_WINDOW];
    float tw_re[MEL_HALF / 2], tw_im[MEL_HALF / 2];
    float utw_re[MEL_BINS], utw_im[MEL_BINS];   // real-FFT unpack, bins 0..MEL_BINS-1
    float re[MEL_HALF], im[MEL_HALF];
    int16_t prev[MEL_HALF];
    float prev_log[MEL_BANDS];
    bool have_prev_log;
} melflux_t;

void melflux_init(melflux_t *m);

// n samples of the current block (MEL_HALF expected); writes MEL_BANDS values.
// Zero on the first block after init
void melflux_block(melflux_t *m, const int16_t *samples, int n,
                   float out[MEL_BANDS]);

#ifdef __cplusplus
}
#endif
