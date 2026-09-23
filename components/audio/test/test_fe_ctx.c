// Pins the block range behind every frame of the model input.
#include "fe_ctx.h"

#include <string.h>
#include "unity.h"

// Every feature equals the block index, so each frame's expected value is the
// mean of the indices it covers.
static void fill(fe_ctx_t *c, int n)
{
    fe_ctx_init(c);
    for (int i = 0; i < n; i++) {
        fe_out_t f;
        float *p = (float *)&f;
        for (int k = 0; k < FE_CTX_FEATS; k++) {
            p[k] = (float)i;
        }
        fe_ctx_push(c, &f);
    }
}

static float mean_range(int lo, int hi)   // inclusive
{
    return 0.5f * (float)(lo + hi);
}

TEST_CASE("window is refused until the ring fills", "[fe_ctx]")
{
    static fe_ctx_t c;
    static float w[FE_CTX_INPUTS];
    fill(&c, FE_CTX_BLOCKS - 1);
    TEST_ASSERT_FALSE(fe_ctx_window(&c, w));
    fe_out_t f = {0};
    fe_ctx_push(&c, &f);
    TEST_ASSERT_TRUE(fe_ctx_window(&c, w));
}

TEST_CASE("frame layout is 16 fine + 16 mid + 12 coarse", "[fe_ctx]")
{
    TEST_ASSERT_EQUAL_INT(44, FE_CTX_FRAMES);
    TEST_ASSERT_EQUAL_INT(528, FE_CTX_INPUTS);
    TEST_ASSERT_EQUAL_INT(272, FE_CTX_BLOCKS);
}

TEST_CASE("each frame covers the blocks the model was trained on", "[fe_ctx]")
{
    static fe_ctx_t c;
    static float w[FE_CTX_INPUTS];
    const int N = FE_CTX_BLOCKS;          // indices 0 .. N-1, newest is N-1
    fill(&c, N);
    TEST_ASSERT_TRUE(fe_ctx_window(&c, w));

    // Fine, oldest first: frame f is block N-16+f, so the last is t+2.
    for (int f = 0; f < FE_CTX_FINE; f++) {
        TEST_ASSERT_EQUAL_FLOAT((float)(N - 16 + f), w[f * FE_CTX_FEATS]);
    }

    // Mid, newest first: frame m is the mean of [N-20-4m, N-17-4m].
    const float *mid = w + FE_CTX_FINE * FE_CTX_FEATS;
    for (int m = 0; m < FE_CTX_MID; m++) {
        TEST_ASSERT_EQUAL_FLOAT(mean_range(N - 20 - 4 * m, N - 17 - 4 * m),
                                mid[m * FE_CTX_FEATS]);
    }

    // Coarse, newest first: frame k is the mean of [N-96-16k, N-81-16k].
    const float *co = w + (FE_CTX_FINE + FE_CTX_MID) * FE_CTX_FEATS;
    for (int k = 0; k < FE_CTX_COARSE; k++) {
        TEST_ASSERT_EQUAL_FLOAT(mean_range(N - 96 - 16 * k, N - 81 - 16 * k),
                                co[k * FE_CTX_FEATS]);
    }

    // The last coarse frame must reach the oldest block held exactly.
    TEST_ASSERT_EQUAL_FLOAT(mean_range(0, 15), co[(FE_CTX_COARSE - 1) * FE_CTX_FEATS]);
}

TEST_CASE("all 12 features carry through independently", "[fe_ctx]")
{
    static fe_ctx_t c;
    static float w[FE_CTX_INPUTS];
    fe_ctx_init(&c);
    for (int i = 0; i < FE_CTX_BLOCKS; i++) {
        fe_out_t f;
        float *p = (float *)&f;
        for (int k = 0; k < FE_CTX_FEATS; k++) {
            p[k] = (float)(i * 100 + k);
        }
        fe_ctx_push(&c, &f);
    }
    TEST_ASSERT_TRUE(fe_ctx_window(&c, w));
    const int N = FE_CTX_BLOCKS;
    for (int k = 0; k < FE_CTX_FEATS; k++) {
        TEST_ASSERT_EQUAL_FLOAT((float)((N - 1) * 100 + k),
                                w[(FE_CTX_FINE - 1) * FE_CTX_FEATS + k]);
    }
}
