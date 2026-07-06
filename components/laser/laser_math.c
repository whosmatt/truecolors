// laser_math.c
#include "laser_math.h"
#include "app_config.h"

#include <math.h>

static float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

void laser_compute_widths(const float rgb[3], float stretch, laser_widths_t *out)
{
    const float T = (float)TC_PERIOD_TICKS;
    const float gap = (float)GAP_TICKS;
    // The up-counter tops out at period-1, a compare at the period tick never
    // fires. Reserve the wrap tick so every clear compare stays reachable.
    const float t_avail = T - 3.0f * gap - 1.0f;
    const float slot = t_avail / 3.0f;
    const float s = clamp01(stretch);

    float on_f[3];
    float sum = 0.0f;
    for (int c = 0; c < 3; c++) {
        on_f[c] = clamp01(rgb[c]) * slot * (1.0f + 2.0f * s);
        sum += on_f[c];
    }

    // Dynamic packing: scale to fit the period, preserving the color ratio.
    if (sum > t_avail && sum > 0.0f) {
        float k = t_avail / sum;
        for (int c = 0; c < 3; c++) {
            on_f[c] *= k;
        }
    }

    int32_t on[3];
    for (int c = 0; c < 3; c++) {
        int32_t v = (int32_t)lroundf(on_f[c]);
        if (v < 0) v = 0;
        if (v > (int32_t)t_avail) v = (int32_t)t_avail;
        on[c] = v;
    }

    // Brightness floor: a zero channel stays off, a sub-floor channel snaps up.
    for (int c = 0; c < 3; c++) {
        if (on[c] > 0 && on[c] < MIN_ON_TICKS) {
            on[c] = MIN_ON_TICKS;
        }
    }

    // Re-fit if snapping pushed the sum over budget. Non-overlap wins over floor.
    int32_t isum = on[0] + on[1] + on[2];
    int32_t budget = (int32_t)t_avail;
    if (isum > budget) {
        for (int c = 0; c < 3; c++) {
            if (on[c] > 0) {
                on[c] = (int32_t)((int64_t)on[c] * budget / isum);
            }
        }
    }

    // Sequential back-to-back packing.
    int32_t gap_i = (int32_t)gap;
    int32_t acc = 0;
    for (int c = 0; c < 3; c++) {
        if (on[c] > 0) {
            out->cmpa[c] = acc;
            out->cmpb[c] = acc + on[c];
            acc += on[c] + gap_i;
        } else {
            out->cmpa[c] = 0;
            out->cmpb[c] = 0;
        }
        out->on[c] = on[c];
    }
    out->period = (int32_t)T;
}
