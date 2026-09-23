#include "frontend.h"

#include <math.h>
#include <string.h>

// SPL from the datasheet sensitivity: -26 dBFS @ 94 dB SPL, 1 kHz (dBFS is
// referenced to a full-scale sine, hence the +3.01 rms correction).
#define MIC_SENS_DBFS (-26.0f)
#define SPL_TAU_S     1.5f      // slow meter ballistics

// AGC: each band is normalized against its own peak tracker
#define AGC_RELEASE   0.99852f  // peak halves in ~5 s (per 10.7 ms block)
#define AGC_MIN_REF   0.003f    // ~-50 dBFS, below this the room is silent
#define DC_K          0.00065f  // DC blocker pole, ~5 Hz
#define HC_FC_HZ      4000.0f   // hi-cut corner, 8th-order Butterworth (48 dB/oct)
#define LP_BASS_K     0.026f    // one-pole low-pass, ~200 Hz
#define LP_TREBLE_K   0.23f     // one-pole low-pass, ~2 kHz; mid = lp2k - lp200,
                                // treble = s - lp2k (2-4 kHz under the hi-cut)

// Kick sub-bands for the kick detector: one-pole corners at 40 / 80 / 140 /
// 220 Hz, sub-bands are neighbor differences (fundamental / body / click).
#define LP_K40        0.00522f
#define LP_K80        0.01041f
#define LP_K140       0.01816f
#define LP_K220       0.02839f

// workaround for -std=c99 used by truecolors-ml
#define FE_PI 3.14159265358979323846f

static void biquad_lp_init(fe_biquad_t *f, float fc, float q)
{
    float w = 2.0f * FE_PI * fc / FE_SAMPLE_RATE;
    float cw = cosf(w);
    float alpha = sinf(w) / (2.0f * q);
    float a0 = 1.0f + alpha;
    f->b0 = (1.0f - cw) * 0.5f / a0;
    f->b1 = (1.0f - cw) / a0;
    f->b2 = f->b0;
    f->a1 = -2.0f * cw / a0;
    f->a2 = (1.0f - alpha) / a0;
    f->z1 = 0.0f;
    f->z2 = 0.0f;
}

static inline float biquad_run(fe_biquad_t *f, float x)
{
    float y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

static float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

void fe_init(fe_t *fe)
{
    // 8th-order Butterworth section Qs.
    static const float kQ[4] = { 0.5098f, 0.6013f, 0.9000f, 2.5629f };

    memset(fe, 0, sizeof(*fe));
    for (int i = 0; i < 4; i++) {
        biquad_lp_init(&fe->hicut[i], HC_FC_HZ, kQ[i]);
    }
}

uint32_t fe_variant(void)
{
    return 0u
#ifndef FE_NO_HICUT
        | FE_VARIANT_HICUT
#endif
        ;
}

void fe_block(fe_t *fe, const int16_t *samples, int n, fe_out_t *out)
{

    float sum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // bass, mid, treble, broadband
    float sum_kick[3] = { 0.0f, 0.0f, 0.0f };    // 40-80 / 80-140 / 140-220 Hz
    float sum_raw = 0.0f;                        // pre-filter, for the SPL meter
    for (int i = 0; i < n; i++) {
        float s = samples[i] / 32768.0f;
        fe->dc += DC_K * (s - fe->dc);
        s -= fe->dc;
        sum_raw += s * s;

        // Hi-cut
#ifndef FE_NO_HICUT
        s = biquad_run(&fe->hicut[0], s);
        s = biquad_run(&fe->hicut[1], s);
        s = biquad_run(&fe->hicut[2], s);
        s = biquad_run(&fe->hicut[3], s);
#endif

        fe->lp_bass += LP_BASS_K * (s - fe->lp_bass);
        fe->lp_treble += LP_TREBLE_K * (s - fe->lp_treble);
        float mid = fe->lp_treble - fe->lp_bass;
        float hi = s - fe->lp_treble;
        sum[0] += fe->lp_bass * fe->lp_bass;
        sum[1] += mid * mid;
        sum[2] += hi * hi;
        sum[3] += s * s;

        fe->lp_low[0] += LP_K40 * (s - fe->lp_low[0]);
        fe->lp_low[1] += LP_K80 * (s - fe->lp_low[1]);
        fe->lp_low[2] += LP_K140 * (s - fe->lp_low[2]);
        fe->lp_low[3] += LP_K220 * (s - fe->lp_low[3]);
        float k0 = fe->lp_low[1] - fe->lp_low[0];
        float k1 = fe->lp_low[2] - fe->lp_low[1];
        float k2 = fe->lp_low[3] - fe->lp_low[2];
        sum_kick[0] += k0 * k0;
        sum_kick[1] += k1 * k1;
        sum_kick[2] += k2 * k2;
    }

    // Raw per-block levels; attack/release shaping happens per effect.
    float brms[4];
    for (int b = 0; b < 4; b++) {
        brms[b] = sqrtf(sum[b] / n);
        fe->peak[b] = fmaxf(brms[b], fe->peak[b] * AGC_RELEASE);
        float lv = clamp01(brms[b] / fmaxf(fe->peak[b], AGC_MIN_REF));
        if (b < 3) {
            out->bands[b] = lv;
        } else {
            out->level = lv;
        }
    }
    float rms = brms[3];
    out->rms = rms;

    // Kick detector inputs, each normalized by its own AGC peak and gated on
    // room silence.
    for (int b = 0; b < 3; b++) {
        float krms = sqrtf(sum_kick[b] / n);
        fe->kick_peak[b] = fmaxf(krms, fe->kick_peak[b] * AGC_RELEASE);
        float fl = (krms - fe->kick_prev[b]) / fmaxf(fe->kick_peak[b], AGC_MIN_REF);
        fe->kick_prev[b] = krms;
        out->flux[b] = (fl > 0.0f && rms > AGC_MIN_REF) ? fl : 0.0f;
        if (b == 0) {
            out->fund_rms = krms / fmaxf(fe->kick_peak[b], AGC_MIN_REF);
        }
    }
    float mfl = (brms[1] - fe->mid_prev) / fmaxf(fe->peak[1], AGC_MIN_REF);
    float tfl = (brms[2] - fe->tre_prev) / fmaxf(fe->peak[2], AGC_MIN_REF);
    fe->mid_prev = brms[1];
    fe->tre_prev = brms[2];
    out->mid_flux = mfl > 0.0f ? mfl : 0.0f;
    out->treble_flux = tfl > 0.0f ? tfl : 0.0f;

    // Slow-averaged SPL from the raw (unfiltered) signal.
    float k = 1.0f - expf(-((float)n / FE_SAMPLE_RATE) / SPL_TAU_S);
    fe->spl_ms += k * (sum_raw / n - fe->spl_ms);
    out->spl_db = 94.0f - MIC_SENS_DBFS + 3.01f +
                  10.0f * log10f(fe->spl_ms + 1e-12f);
}
