#include "melflux.h"

#include <math.h>
#include <string.h>

// C99 compatible
#define MEL_PI 3.14159265358979323846f

void melflux_init(melflux_t *m)
{
    memset(m, 0, sizeof(*m));

    // hanning window, denominator MEL_WINDOW-1, not MEL_WINDOW
    for (int i = 0; i < MEL_WINDOW; i++) {
        m->hann[i] = 0.5f - 0.5f * cosf(2.0f * MEL_PI * (float)i /
                                        (float)(MEL_WINDOW - 1));
    }
    for (int k = 0; k < MEL_HALF / 2; k++) {
        float a = -2.0f * MEL_PI * (float)k / (float)MEL_HALF;
        m->tw_re[k] = cosf(a);
        m->tw_im[k] = sinf(a);
    }
    for (int k = 0; k < MEL_BINS; k++) {
        float a = -2.0f * MEL_PI * (float)k / (float)MEL_WINDOW;
        m->utw_re[k] = cosf(a);
        m->utw_im[k] = sinf(a);
    }
}

static void fft(melflux_t *m)
{
    float *re = m->re, *im = m->im;

    for (int i = 1, j = 0; i < MEL_HALF; i++) {
        int bit = MEL_HALF >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j |= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }

    for (int len = 2; len <= MEL_HALF; len <<= 1) {
        int half = len >> 1, step = MEL_HALF / len;
        for (int i = 0; i < MEL_HALF; i += len) {
            for (int k = 0; k < half; k++) {
                float wr = m->tw_re[k * step], wi = m->tw_im[k * step];
                int a = i + k, b = a + half;
                float vr = re[b] * wr - im[b] * wi;
                float vi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - vr; im[b] = im[a] - vi;
                re[a] += vr;        im[a] += vi;
            }
        }
    }
}

void melflux_block(melflux_t *m, const int16_t *samples, int n,
                   float out[MEL_BANDS])
{
    if (n > MEL_HALF) {
        n = MEL_HALF;
    }

    // Pack the 1024-point real window as MEL_HALF complex points
    // even -> real, odd -> imaginary
    for (int i = 0; i < MEL_HALF; i++) {
        int e = 2 * i, o = e + 1;
        float se = (e < MEL_HALF) ? (float)m->prev[e]
                                  : ((e - MEL_HALF < n) ? (float)samples[e - MEL_HALF] : 0.0f);
        float so = (o < MEL_HALF) ? (float)m->prev[o]
                                  : ((o - MEL_HALF < n) ? (float)samples[o - MEL_HALF] : 0.0f);
        m->re[i] = se * (1.0f / 32768.0f) * m->hann[e];
        m->im[i] = so * (1.0f / 32768.0f) * m->hann[o];
    }

    fft(m);

    // Unpack the real spectrum, bins 0..MEL_BINS-1 (4 kHz is bin 85).
    float power[MEL_BINS];
    for (int k = 0; k < MEL_BINS; k++) {
        int kn = (MEL_HALF - k) & (MEL_HALF - 1);
        float ar = m->re[k],  ai = m->im[k];
        float br = m->re[kn], bi = -m->im[kn];
        float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);
        float dr = 0.5f * (ar - br), di = 0.5f * (ai - bi);
        float orr = di, oi = -dr;                 // -j * (Z[k] - conj(Z[N-k])) / 2
        float wr = m->utw_re[k], wi = m->utw_im[k];
        float xr = er + (orr * wr - oi * wi);
        float xi = ei + (orr * wi + oi * wr);
        power[k] = xr * xr + xi * xi;
    }

    const float *w = kMelWeights;
    for (int b = 0; b < MEL_BANDS; b++) {
        float e = 0.0f;
        int s = kMelStart[b], len = kMelLen[b];
        for (int i = 0; i < len; i++) {
            e += power[s + i] * w[i];
        }
        w += len;
        float l = log10f(e + 1e-10f);
        float d = l - m->prev_log[b];
        out[b] = (m->have_prev_log && d > 0.0f) ? d : 0.0f;
        m->prev_log[b] = l;
    }
    m->have_prev_log = true;

    memset(m->prev, 0, sizeof(m->prev));
    memcpy(m->prev, samples, (size_t)n * sizeof(int16_t));
}
