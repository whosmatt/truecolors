// frontend.h
// Mic front end: DC block -> coil-whine comb -> 4 kHz hi-cut -> band split ->
// per-band AGC -> block features
// C99 compatible, seperated to allow reuse for preprocessing in truecolors-ml
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bump on frontend changes, model is trained against this version
#define FE_SPEC_VERSION 1

// Filters can be compiled out with -DFE_NO_COMB / -DFE_NO_HICUT
#define FE_VARIANT_COMB  (1u << 0)
#define FE_VARIANT_HICUT (1u << 1)

#define FE_SAMPLE_RATE   48000   // divides evenly by every PWM frequency
#define FE_BLOCK_SAMPLES 512
#define FE_COMB_MAX      600

typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} fe_biquad_t;

typedef struct {
    float dc;
    float comb[FE_COMB_MAX];
    volatile int comb_n;   // fe_set_notch_hz runs on another task than fe_block
    int comb_i;
    fe_biquad_t hicut[4];
    float lp_bass, lp_treble;
    float peak[4];
    float lp_low[4];
    float kick_prev[3], kick_peak[3];
    float mid_prev, tre_prev;
    float spl_ms;
} fe_t;

// One analysis block. Levels and fluxes are AGC-normalized; fluxes are
// half-wave rectified and gated on room silence.
typedef struct {
    float level;        // 0..1 broadband
    float bands[3];     // 0..1 bass / mid / treble
    float rms;          // broadband block rms, pre-AGC
    float flux[3];      // kick sub-bands 40-80 / 80-140 / 140-220 Hz
    float fund_rms;     // 40-80 Hz level, AGC-normalized
    float mid_flux;     // 200 Hz - 2 kHz
    float treble_flux;  // 2-4 kHz
    float spl_db;       // slow-averaged, datasheet calibrated
} fe_out_t;

void fe_init(fe_t *fe, uint32_t notch_hz);

// Show frontend variant
uint32_t fe_variant(void);

// Align the coil-whine comb to the laser PWM frequency. Call on PWM change.
void fe_set_notch_hz(fe_t *fe, uint32_t hz);

void fe_block(fe_t *fe, const int16_t *samples, int n, fe_out_t *out);

#ifdef __cplusplus
}
#endif
