// frontend.h
// Mic front end: DC block -> 4 kHz hi-cut -> band split -> per-band AGC ->
// block features
// C99 compatible, seperated to allow reuse for preprocessing in truecolors-ml
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bump on frontend changes; the model is trained against this version.
// v2: comb removed, bit-identical to v1 built with -DFE_NO_COMB.
#define FE_SPEC_VERSION 2

// Which filters this build contains. COMB keeps its bit so the mask means the
// same across versions; v2 never sets it. Hi-cut: -DFE_NO_HICUT.
#define FE_VARIANT_COMB  (1u << 0)
#define FE_VARIANT_HICUT (1u << 1)

#define FE_SAMPLE_RATE   48000
#define FE_BLOCK_SAMPLES 512

typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} fe_biquad_t;

typedef struct {
    float dc;
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

void fe_init(fe_t *fe);

// Show frontend variant
uint32_t fe_variant(void);

void fe_block(fe_t *fe, const int16_t *samples, int n, fe_out_t *out);

#ifdef __cplusplus
}
#endif
