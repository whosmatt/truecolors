// test_laser_math.c
// Host unit test for the laser width math. Compile and run on the dev machine:
//   gcc -I ../include -I ../../common/include test_laser_math.c ../laser_math.c -lm -o /tmp/lt && /tmp/lt
#include "laser_math.h"
#include "app_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int g_fail = 0;
static int g_checks = 0;

static void check(int cond, const char *msg)
{
    g_checks++;
    if (!cond) {
        g_fail++;
        printf("  FAIL: %s\n", msg);
    }
}

static void check_near(float got, float want, float tol, const char *msg)
{
    g_checks++;
    if (fabsf(got - want) > tol) {
        g_fail++;
        printf("  FAIL: %s (got %.4f, want %.4f, tol %.4f)\n", msg, got, want, tol);
    }
}

static float duty(const laser_widths_t *w, int c)
{
    return (float)w->on[c] / (float)w->period;
}

static laser_widths_t compute(float r, float g, float b, float s)
{
    float rgb[3] = {r, g, b};
    laser_widths_t w;
    laser_compute_widths(rgb, s, false, &w);
    return w;
}

static laser_widths_t compute_ka(float r, float g, float b, float s)
{
    float rgb[3] = {r, g, b};
    laser_widths_t w;
    laser_compute_widths(rgb, s, true, &w);
    return w;
}

// Sum of on-times must never exceed the period, active pulses must be packed
// back-to-back without overlap, and every compare must stay at or below
// period-1: the up-counter never reaches the period tick, so a clear compare
// there never fires and the channel latches on.
static void check_invariants(const laser_widths_t *w, const char *ctx)
{
    char buf[128];
    int32_t sum = w->on[0] + w->on[1] + w->on[2];
    snprintf(buf, sizeof buf, "%s: sum on < period", ctx);
    check(sum < w->period, buf);

    int32_t expect = 0;
    for (int c = 0; c < 3; c++) {
        if (w->on[c] > 0) {
            snprintf(buf, sizeof buf, "%s: cmpa[%d] packed", ctx, c);
            check(w->cmpa[c] == expect, buf);
            snprintf(buf, sizeof buf, "%s: cmpb[%d] = cmpa+on", ctx, c);
            check(w->cmpb[c] == expect + w->on[c], buf);
            snprintf(buf, sizeof buf, "%s: cmpb[%d] reachable", ctx, c);
            check(w->cmpb[c] <= w->period - 1, buf);
            expect += w->on[c] + GAP_TICKS;
        } else {
            snprintf(buf, sizeof buf, "%s: off channel %d zero width", ctx, c);
            check(w->cmpa[c] == 0 && w->cmpb[c] == 0, buf);
        }
    }
}

int main(void)
{
    laser_widths_t w;

    printf("laser_math host tests (period=%d ticks, MIN_ON=%d ticks)\n",
           (int)TC_PERIOD_TICKS, (int)MIN_ON_TICKS);

    // Single pure color: 33%% at stretch 0, 100%% at stretch 1.
    w = compute(1, 0, 0, 0.0f);
    check_near(duty(&w, 0), 0.3333f, 0.01f, "red s0 = 33%");
    check(w.on[1] == 0 && w.on[2] == 0, "red s0: G,B off");
    check_invariants(&w, "red s0");

    w = compute(1, 0, 0, 1.0f);
    check_near(duty(&w, 0), 1.0f, 0.01f, "red s1 = 100%");
    check_invariants(&w, "red s1");

    // Pure green at stretch 1 -> green continuously on.
    w = compute(0, 1, 0, 1.0f);
    check_near(duty(&w, 1), 1.0f, 0.01f, "green s1 = 100%");
    check(w.on[0] == 0 && w.on[2] == 0, "green s1: R,B off");
    check_invariants(&w, "green s1");

    w = compute(0, 0, 1, 1.0f);
    check_near(duty(&w, 2), 1.0f, 0.01f, "blue s1 = 100%");
    check_invariants(&w, "blue s1");

    // Two-color mix R+G at stretch 1 -> 50/50 packed.
    w = compute(1, 1, 0, 1.0f);
    check_near(duty(&w, 0), 0.5f, 0.01f, "R+G s1: R = 50%");
    check_near(duty(&w, 1), 0.5f, 0.01f, "R+G s1: G = 50%");
    check(w.cmpa[1] == w.cmpb[0], "R+G s1: G starts where R ends");
    check_invariants(&w, "R+G s1");

    w = compute(1, 1, 0, 0.0f);
    check_near(duty(&w, 0), 0.3333f, 0.01f, "R+G s0: R = 33%");
    check_near(duty(&w, 1), 0.3333f, 0.01f, "R+G s0: G = 33%");
    check_invariants(&w, "R+G s0");

    // Full white stays 33/33/33 at any stretch.
    for (float s = 0.0f; s <= 1.0f; s += 0.25f) {
        w = compute(1, 1, 1, s);
        char ctx[32];
        snprintf(ctx, sizeof ctx, "white s%.2f", s);
        check_near(duty(&w, 0), 0.3333f, 0.01f, "white R = 33%");
        check_near(duty(&w, 1), 0.3333f, 0.01f, "white G = 33%");
        check_near(duty(&w, 2), 0.3333f, 0.01f, "white B = 33%");
        check_invariants(&w, ctx);
    }

    // Normalize preserves the R:G:B ratio.
    w = compute(1.0f, 0.5f, 0.0f, 1.0f);
    check_near((float)w.on[0] / (float)w.on[1], 2.0f, 0.02f, "ratio R:G = 2:1 preserved");
    check_invariants(&w, "ratio");

    // Floor: zero stays zero, sub-min snaps up to MIN_ON_TICKS.
    w = compute(0.0f, 0.0f, 0.0f, 0.0f);
    check(w.on[0] == 0 && w.on[1] == 0 && w.on[2] == 0, "all-zero stays off");

    w = compute(0.0001f, 0.0f, 0.0f, 0.0f);
    check(w.on[0] == (int32_t)MIN_ON_TICKS, "tiny red snaps to MIN_ON");
    check_invariants(&w, "floor snap");

    // Clamp out-of-range input.
    w = compute(2.0f, -1.0f, 0.5f, 2.0f);
    check_invariants(&w, "out-of-range clamp");

    // Keepalive: dark channels idle at the MIN_ON floor instead of turning
    // off, so the driver bias never decays into low-power shutdown.
    w = compute_ka(0.0f, 0.0f, 0.0f, 0.0f);
    check(w.on[0] == (int32_t)MIN_ON_TICKS && w.on[1] == (int32_t)MIN_ON_TICKS &&
          w.on[2] == (int32_t)MIN_ON_TICKS, "keepalive black: all at MIN_ON");
    check_invariants(&w, "keepalive black");

    w = compute_ka(1.0f, 0.0f, 0.0f, 0.0f);
    check_near(duty(&w, 0), 0.3333f, 0.01f, "keepalive red = 33%");
    check(w.on[1] == (int32_t)MIN_ON_TICKS && w.on[2] == (int32_t)MIN_ON_TICKS,
          "keepalive red: dark G,B at MIN_ON");
    check_invariants(&w, "keepalive red");

    // Keepalive under full stretch: refit shrinks the floor but every channel
    // keeps pulsing, and nothing may occupy the whole period.
    w = compute_ka(1.0f, 0.0f, 0.0f, 1.0f);
    check(w.on[1] > 0 && w.on[2] > 0, "keepalive red s1: G,B still pulse");
    check_invariants(&w, "keepalive red s1");

    w = compute_ka(1.0f, 1.0f, 0.0f, 1.0f);
    check(w.on[2] > 0, "keepalive R+G s1: B still pulses");
    check_invariants(&w, "keepalive R+G s1");

    // Crossfade corner regression: two hot channels with stretch past 1/4
    // engage dynamic packing, which used to pack the last pulse out to the
    // period tick and latch the channel on (rainbow flash at the 50/50 mark).
    w = compute(1.0f, 0.974f, 0.0f, 0.26f);
    check_invariants(&w, "corner s0.26");

    // Sweep crossfades across the stretch range, two- and three-channel.
    for (int si = 0; si <= 20; si++) {
        float s = si / 20.0f;
        for (int fi = 0; fi <= 100; fi++) {
            float f = fi / 100.0f;
            char ctx[48];
            snprintf(ctx, sizeof ctx, "sweep s%.2f f%.2f 2ch", s, f);
            w = compute(1.0f, f, 0.0f, s);
            check_invariants(&w, ctx);
            snprintf(ctx, sizeof ctx, "sweep s%.2f f%.2f 3ch", s, f);
            w = compute(f, 0.5f, 1.0f, s);
            check_invariants(&w, ctx);
        }
    }

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
