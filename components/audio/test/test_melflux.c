// Mel flux against blocks of the golden song; see temp_ML2/specs/mel-spec.md.
#include "melflux.h"

#include <math.h>
#include "unity.h"
#include "golden_mel.h"

static melflux_t s_m;

TEST_CASE("mel flux reproduces the golden blocks", "[melflux]")
{
    melflux_init(&s_m);
    for (int b = 0; b < MELT_BLOCKS; b++) {
        float out[MEL_BANDS];
        melflux_block(&s_m, kMelPcm[b], 512, out);
        if (b < MELT_EXACT) {
            continue;   // run-in: no previous window yet
        }
        for (int k = 0; k < MEL_BANDS; k++) {
            TEST_ASSERT_FLOAT_WITHIN(1e-4f, kMelExpect[b][k], out[k]);
        }
    }
}

TEST_CASE("mel flux is zero on the first block and never negative", "[melflux]")
{
    melflux_init(&s_m);
    float out[MEL_BANDS];
    melflux_block(&s_m, kMelPcm[0], 512, out);
    for (int k = 0; k < MEL_BANDS; k++) {
        TEST_ASSERT_EQUAL_FLOAT(0.0f, out[k]);
    }
    for (int b = 1; b < MELT_BLOCKS; b++) {
        melflux_block(&s_m, kMelPcm[b], 512, out);
        for (int k = 0; k < MEL_BANDS; k++) {
            TEST_ASSERT_TRUE(out[k] >= 0.0f);
        }
    }
}

TEST_CASE("the filterbank stays inside the spectrum it is read from", "[melflux]")
{
    int end = 0;
    for (int b = 0; b < MEL_BANDS; b++) {
        TEST_ASSERT_TRUE(kMelStart[b] >= 0);
        TEST_ASSERT_TRUE(kMelStart[b] + kMelLen[b] <= MEL_BINS);
        end += kMelLen[b];
    }
    TEST_ASSERT_EQUAL_INT(MEL_WEIGHTS, end);
    TEST_ASSERT_TRUE(MEL_BINS <= MEL_HALF + 1);
}
