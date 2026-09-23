// Checks fe_ctx assembly and the folded quantisation against the training
// pipeline's own vectors. A wrong frame range is silent: the window stays in
// range and the model just does worse.
#include "fe_ctx.h"
#include "golden_frames.h"

#include <math.h>
#include <string.h>
#include "unity.h"

static bool window_at(int centre, float *out)
{
    static fe_ctx_t c;
    fe_ctx_init(&c);
    for (int i = 0; i < GOLDEN_NFRAMES; i++) {
        fe_out_t f;
        memcpy(&f, &kGoldenFrames[i * GOLDEN_FEATS], sizeof(f));
        fe_ctx_push(&c, &f);
        if (i == centre + FE_CTX_LOOKAHEAD) {
            return fe_ctx_window(&c, out);
        }
    }
    return false;
}

static void check_quant(const float *win, const signed char *want)
{
    for (int f = 0; f < FE_CTX_FRAMES; f++) {
        for (int k = 0; k < FE_CTX_FEATS; k++) {
            int i = f * FE_CTX_FEATS + k;
            int v = (int)lrintf((win[i] - kGoldenNormMean[k]) * kGoldenQuantMul[k]) +
                    GOLDEN_IN_ZERO;
            v = v < -128 ? -128 : (v > 127 ? 127 : v);
            TEST_ASSERT_EQUAL_INT(want[i], v);
        }
    }
}

static void check_ring_case(int centre, const float *want_win, const signed char *want_q)
{
    static float win[FE_CTX_INPUTS];
    TEST_ASSERT_TRUE(window_at(centre, win));

    // Fine frames are copied, so they match exactly; the averaged sections
    // differ in the last bits by accumulation order.
    for (int i = 0; i < FE_CTX_FINE * FE_CTX_FEATS; i++) {
        TEST_ASSERT_EQUAL_FLOAT(want_win[i], win[i]);
    }
    for (int i = FE_CTX_FINE * FE_CTX_FEATS; i < FE_CTX_INPUTS; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, want_win[i], win[i]);
    }
    check_quant(win, want_q);
}

TEST_CASE("beat case assembles and quantises identically", "[golden]")
{
    check_ring_case(GOLDEN_CENTRE_0, kGoldenWindow0, kGoldenInt8_0);
}

TEST_CASE("no_beat case assembles and quantises identically", "[golden]")
{
    check_ring_case(GOLDEN_CENTRE_1, kGoldenWindow1, kGoldenInt8_1);
}

// non_music and silence come from other takes, so only the quantisation path
// is under test.
TEST_CASE("non_music window quantises identically", "[golden]")
{
    TEST_ASSERT_EQUAL_INT(-1, GOLDEN_CENTRE_2);
    check_quant(kGoldenWindow2, kGoldenInt8_2);
}

TEST_CASE("silence window quantises identically", "[golden]")
{
    TEST_ASSERT_EQUAL_INT(-1, GOLDEN_CENTRE_3);
    check_quant(kGoldenWindow3, kGoldenInt8_3);
}
