// Ring assembly and trunk quantisation against the training pipeline's own
// vectors. A wrong frame range is silent: the window stays in range and the
// model just does worse.
#include "fe_ctx.h"
#include "golden_ring.h"
#include "model_params.h"
#include "selftest_window.h"

#include <math.h>
#include "unity.h"

static fe_ctx_t s_c;
static float s_win[FE_CTX_INPUTS];

TEST_CASE("the ring assembles the window the model was fed", "[golden]")
{
    TEST_ASSERT_EQUAL_INT(FE_CTX_BLOCKS, GOLDEN_RING_BLOCKS);
    TEST_ASSERT_EQUAL_INT(FE_CTX_FEATS, GOLDEN_RING_FEATS);

    fe_ctx_init(&s_c);
    for (int i = 0; i < GOLDEN_RING_BLOCKS; i++) {
        fe_ctx_push_vec(&s_c, kGoldenRingA[i]);
    }
    TEST_ASSERT_TRUE(fe_ctx_window(&s_c, s_win));

    // The projections in the golden file are rounded to 5 decimals, so a value
    // can sit either side of a rounding boundary: 1 LSB, no more.
    int differ = 0;
    for (int i = 0; i < FE_CTX_INPUTS; i++) {
        int v = (int)lrintf(s_win[i] * (1.0f / MODEL_A_IN_SCALE)) + MODEL_A_IN_ZERO;
        v = v < -128 ? -128 : (v > 127 ? 127 : v);
        TEST_ASSERT_INT_WITHIN(1, kGoldenAInput[i], v);
        differ += (v != kGoldenAInput[i]);
    }
    TEST_ASSERT_TRUE(differ < FE_CTX_INPUTS / 20);
}

TEST_CASE("the window is not right by accident", "[golden]")
{
    // Shifting the ring by one block must break the match, or the test above
    // would pass on any nearby frame range.
    fe_ctx_init(&s_c);
    for (int i = 1; i < GOLDEN_RING_BLOCKS; i++) {
        fe_ctx_push_vec(&s_c, kGoldenRingA[i]);
    }
    fe_ctx_push_vec(&s_c, kGoldenRingA[GOLDEN_RING_BLOCKS - 1]);
    TEST_ASSERT_TRUE(fe_ctx_window(&s_c, s_win));

    int differ = 0;
    for (int i = 0; i < FE_CTX_INPUTS; i++) {
        int v = (int)lrintf(s_win[i] * (1.0f / MODEL_A_IN_SCALE)) + MODEL_A_IN_ZERO;
        v = v < -128 ? -128 : (v > 127 ? 127 : v);
        differ += (v != kGoldenAInput[i]);
    }
    TEST_ASSERT_TRUE(differ > FE_CTX_INPUTS / 10);
}

TEST_CASE("the projections reproduce the golden block", "[golden]")
{
    float p[MODEL_FEATS];
    for (int j = 0; j < MODEL_FEATS; j++) {
        p[j] = kProjAB[j];
    }
    for (int i = 0; i < MODEL_X_FEATS; i++) {
        for (int j = 0; j < MODEL_FEATS; j++) {
            p[j] += kGoldenX28[i] * kProjAW[i][j];
        }
    }
    for (int j = 0; j < MODEL_FEATS; j++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, kGoldenProjA[j], p[j]);
    }

    float xb[MODEL_X_FEATS + 1];
    for (int i = 0; i < MODEL_X_FEATS; i++) {
        xb[i] = kGoldenX28[i];
    }
    xb[MODEL_X_FEATS] = kGoldenFb;
    for (int j = 0; j < MODEL_FEATS; j++) {
        p[j] = kProjBB[j];
    }
    for (int i = 0; i < MODEL_X_FEATS + 1; i++) {
        for (int j = 0; j < MODEL_FEATS; j++) {
            p[j] += xb[i] * kProjBW[i][j];
        }
    }
    for (int j = 0; j < MODEL_FEATS; j++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, kGoldenProjB[j], p[j]);
    }
}
