// test_laser_math.c
// Unity tests for the laser width math; runs on the host via test/host/.
#include "laser_math.h"
#include "app_config.h"

#include <stdio.h>
#include <math.h>
#include "unity.h"

static float duty(const laser_widths_t *w, int c)
{
    return (float)w->on[c] / (float)w->period;
}

static laser_widths_t compute(float r, float g, float b, float s)
{
    float rgb[3] = {r, g, b};
    laser_widths_t w;
    laser_compute_widths(rgb, s, false, TC_PERIOD_TICKS, &w);
    return w;
}

static laser_widths_t compute_ka(float r, float g, float b, float s)
{
    float rgb[3] = {r, g, b};
    laser_widths_t w;
    laser_compute_widths(rgb, s, true, TC_PERIOD_TICKS, &w);
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
    TEST_ASSERT_TRUE_MESSAGE(sum < w->period, buf);

    int32_t expect = 0;
    for (int c = 0; c < 3; c++) {
        if (w->on[c] > 0) {
            snprintf(buf, sizeof buf, "%s: cmpa[%d] packed", ctx, c);
            TEST_ASSERT_TRUE_MESSAGE(w->cmpa[c] == expect, buf);
            snprintf(buf, sizeof buf, "%s: cmpb[%d] = cmpa+on", ctx, c);
            TEST_ASSERT_TRUE_MESSAGE(w->cmpb[c] == expect + w->on[c], buf);
            snprintf(buf, sizeof buf, "%s: cmpb[%d] reachable", ctx, c);
            TEST_ASSERT_TRUE_MESSAGE(w->cmpb[c] <= w->period - 1, buf);
            expect += w->on[c] + GAP_TICKS;
        } else {
            snprintf(buf, sizeof buf, "%s: off channel %d zero width", ctx, c);
            TEST_ASSERT_TRUE_MESSAGE(w->cmpa[c] == 0 && w->cmpb[c] == 0, buf);
        }
    }
}

// Single pure color: 33% at stretch 0, 100% at stretch 1.
TEST_CASE("pure colors: 33% at stretch 0, 100% at stretch 1", "[laser_math]")
{
    laser_widths_t w;

    w = compute(1, 0, 0, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.3333f, duty(&w, 0), "red s0 = 33%");
    TEST_ASSERT_TRUE_MESSAGE(w.on[1] == 0 && w.on[2] == 0, "red s0: G,B off");
    check_invariants(&w, "red s0");

    w = compute(1, 0, 0, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 1.0f, duty(&w, 0), "red s1 = 100%");
    check_invariants(&w, "red s1");

    w = compute(0, 1, 0, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 1.0f, duty(&w, 1), "green s1 = 100%");
    TEST_ASSERT_TRUE_MESSAGE(w.on[0] == 0 && w.on[2] == 0, "green s1: R,B off");
    check_invariants(&w, "green s1");

    w = compute(0, 0, 1, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 1.0f, duty(&w, 2), "blue s1 = 100%");
    check_invariants(&w, "blue s1");
}

// Two-color mix R+G at stretch 1 -> 50/50 packed.
TEST_CASE("two-color mix packs back-to-back", "[laser_math]")
{
    laser_widths_t w;

    w = compute(1, 1, 0, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.5f, duty(&w, 0), "R+G s1: R = 50%");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.5f, duty(&w, 1), "R+G s1: G = 50%");
    TEST_ASSERT_TRUE_MESSAGE(w.cmpa[1] == w.cmpb[0], "R+G s1: G starts where R ends");
    check_invariants(&w, "R+G s1");

    w = compute(1, 1, 0, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.3333f, duty(&w, 0), "R+G s0: R = 33%");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.3333f, duty(&w, 1), "R+G s0: G = 33%");
    check_invariants(&w, "R+G s0");
}

// Full white stays 33/33/33 at any stretch.
TEST_CASE("white stays 33/33/33 across stretch", "[laser_math]")
{
    for (float s = 0.0f; s <= 1.0f; s += 0.25f) {
        laser_widths_t w = compute(1, 1, 1, s);
        char ctx[32];
        snprintf(ctx, sizeof ctx, "white s%.2f", s);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.3333f, duty(&w, 0), "white R = 33%");
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.3333f, duty(&w, 1), "white G = 33%");
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.3333f, duty(&w, 2), "white B = 33%");
        check_invariants(&w, ctx);
    }
}

TEST_CASE("normalization preserves color ratio", "[laser_math]")
{
    laser_widths_t w = compute(1.0f, 0.5f, 0.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.02f, 2.0f, (float)w.on[0] / (float)w.on[1],
                                     "ratio R:G = 2:1 preserved");
    check_invariants(&w, "ratio");
}

TEST_CASE("all-zero input stays off", "[laser_math]")
{
    laser_widths_t w = compute(0.0f, 0.0f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE_MESSAGE(w.on[0] == 0 && w.on[1] == 0 && w.on[2] == 0,
                             "all-zero stays off");
}

// Floor: sub-min width snaps up to MIN_ON_TICKS.
TEST_CASE("sub-min width snaps up to MIN_ON", "[laser_math]")
{
    laser_widths_t w = compute(0.001f, 0.0f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE_MESSAGE(w.on[0] == (int32_t)MIN_ON_TICKS, "tiny red snaps to MIN_ON");
    check_invariants(&w, "floor snap");
}

TEST_CASE("out-of-range input is clamped", "[laser_math]")
{
    laser_widths_t w = compute(2.0f, -1.0f, 0.5f, 2.0f);
    check_invariants(&w, "out-of-range clamp");
}

// Keepalive: dark channels idle at the MIN_ON floor instead of turning off,
// so the driver bias never decays into low-power shutdown.
TEST_CASE("keepalive floors dark channels at MIN_ON", "[laser_math]")
{
    laser_widths_t w;

    w = compute_ka(0.0f, 0.0f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE_MESSAGE(w.on[0] == (int32_t)MIN_ON_TICKS &&
                             w.on[1] == (int32_t)MIN_ON_TICKS &&
                             w.on[2] == (int32_t)MIN_ON_TICKS,
                             "keepalive black: all at MIN_ON");
    check_invariants(&w, "keepalive black");

    w = compute_ka(1.0f, 0.0f, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.3333f, duty(&w, 0), "keepalive red = 33%");
    TEST_ASSERT_TRUE_MESSAGE(w.on[1] == (int32_t)MIN_ON_TICKS &&
                             w.on[2] == (int32_t)MIN_ON_TICKS,
                             "keepalive red: dark G,B at MIN_ON");
    check_invariants(&w, "keepalive red");

    // Under full stretch the refit shrinks the floor but every channel keeps
    // pulsing, and nothing may occupy the whole period.
    w = compute_ka(1.0f, 0.0f, 0.0f, 1.0f);
    TEST_ASSERT_TRUE_MESSAGE(w.on[1] > 0 && w.on[2] > 0, "keepalive red s1: G,B still pulse");
    check_invariants(&w, "keepalive red s1");

    w = compute_ka(1.0f, 1.0f, 0.0f, 1.0f);
    TEST_ASSERT_TRUE_MESSAGE(w.on[2] > 0, "keepalive R+G s1: B still pulses");
    check_invariants(&w, "keepalive R+G s1");
}

// Crossfade corner regression: two hot channels with stretch past 1/4 engage
// dynamic packing, which used to pack the last pulse out to the period tick
// and latch the channel on (rainbow flash at the 50/50 mark).
TEST_CASE("crossfade corner cannot latch a channel", "[laser_math]")
{
    laser_widths_t w = compute(1.0f, 0.974f, 0.0f, 0.26f);
    check_invariants(&w, "corner s0.26");
}

// Sweep crossfades across the stretch range, two- and three-channel.
TEST_CASE("stretch/fade sweep holds invariants", "[laser_math]")
{
    for (int si = 0; si <= 20; si++) {
        float s = si / 20.0f;
        for (int fi = 0; fi <= 100; fi++) {
            float f = fi / 100.0f;
            char ctx[48];
            laser_widths_t w;
            snprintf(ctx, sizeof ctx, "sweep s%.2f f%.2f 2ch", s, f);
            w = compute(1.0f, f, 0.0f, s);
            check_invariants(&w, ctx);
            snprintf(ctx, sizeof ctx, "sweep s%.2f f%.2f 3ch", s, f);
            w = compute(f, 0.5f, 1.0f, s);
            check_invariants(&w, ctx);
        }
    }
}

// Runtime periods: invariants hold at every dropdown frequency
// (120 Hz, 240 Hz, 480 Hz).
TEST_CASE("invariants hold at all runtime periods", "[laser_math]")
{
    static const int32_t periods[] = { 41666, 20833, 10416 };
    for (size_t pi = 0; pi < sizeof(periods) / sizeof(periods[0]); pi++) {
        for (int fi = 0; fi <= 20; fi++) {
            float f = fi / 20.0f;
            float rgb[3] = { 1.0f, f, 0.25f };
            char ctx[48];
            laser_widths_t w;
            snprintf(ctx, sizeof ctx, "period %d f%.2f", (int)periods[pi], f);
            laser_compute_widths(rgb, f, true, periods[pi], &w);
            check_invariants(&w, ctx);
        }
    }
}
