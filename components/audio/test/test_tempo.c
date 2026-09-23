// Stage-2 estimator: recovering a known period and phase.
#include "tempo.h"

#include <math.h>
#include "unity.h"

static tempo_buf_t s_b;

// Triangular pulses one block wide at the given tempo and phase.
static void feed(float bpm, float phase_blocks, int count)
{
    float period = 60.0f * TEMPO_BLOCK_HZ / bpm;
    tempo_init(&s_b);
    for (int i = 0; i < count; i++) {
        float nearest = roundf(((float)i - phase_blocks) / period) * period + phase_blocks;
        float d = fabsf((float)i - nearest);
        tempo_push(&s_b, d < 1.0f ? 1.0f - d : 0.0f);
    }
}

TEST_CASE("lag range brackets the tempo range", "[tempo]")
{
    // The array bounds are literals; this keeps them honest.
    float lo = 60.0f * TEMPO_BLOCK_HZ / TEMPO_BPM_MAX;
    float hi = 60.0f * TEMPO_BLOCK_HZ / TEMPO_BPM_MIN;
    TEST_ASSERT_TRUE(hi < (float)TEMPO_LAG_MAX);
    TEST_ASSERT_TRUE(lo > 1.0f);
    TEST_ASSERT_TRUE(3.0f * hi < (float)(TEMPO_AC_MAX - 1));
}

TEST_CASE("no estimate until the window fills", "[tempo]")
{
    tempo_est_t e;
    feed(120.0f, 0.0f, TEMPO_WIN_BLOCKS - 1);
    TEST_ASSERT_FALSE(tempo_estimate(&s_b, &e));
    tempo_push(&s_b, 0.0f);
    TEST_ASSERT_TRUE(tempo_estimate(&s_b, &e));
}

TEST_CASE("silence yields no estimate", "[tempo]")
{
    tempo_est_t e;
    tempo_init(&s_b);
    for (int i = 0; i < TEMPO_WIN_BLOCKS; i++) {
        tempo_push(&s_b, 0.0f);
    }
    TEST_ASSERT_FALSE(tempo_estimate(&s_b, &e));
}

TEST_CASE("recovers tempo across the range", "[tempo]")
{
    const float bpms[] = { 90.0f, 120.0f, 128.0f, 140.0f, 174.0f, 200.0f };
    for (unsigned i = 0; i < sizeof(bpms) / sizeof(*bpms); i++) {
        feed(bpms[i], 7.3f, TEMPO_WIN_BLOCKS);
        tempo_est_t e;
        TEST_ASSERT_TRUE(tempo_estimate(&s_b, &e));
        TEST_ASSERT_FLOAT_WITHIN(bpms[i] * 0.005f, bpms[i], e.bpm);
    }
}

// 174 BPM is 32.33 blocks; on an integer grid lag 97 outscores it.
TEST_CASE("174 bpm does not collapse to a subharmonic", "[tempo]")
{
    feed(174.0f, 0.0f, TEMPO_WIN_BLOCKS);
    tempo_est_t e;
    TEST_ASSERT_TRUE(tempo_estimate(&s_b, &e));
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 174.0f, e.bpm);
}

TEST_CASE("phase lands on the pulses", "[tempo]")
{
    const float bpm = 128.0f, ph = 7.3f;
    feed(bpm, ph, TEMPO_WIN_BLOCKS);
    tempo_est_t e;
    TEST_ASSERT_TRUE(tempo_estimate(&s_b, &e));
    float period = 60.0f * TEMPO_BLOCK_HZ / bpm;
    float err = fmodf(e.phase - ph + 10.0f * period, period);
    if (err > period / 2.0f) {
        err -= period;
    }
    TEST_ASSERT_TRUE(fabsf(err) < 1.0f);          // within one block
}

TEST_CASE("next beat is ahead of the newest sample", "[tempo]")
{
    feed(120.0f, 3.0f, TEMPO_WIN_BLOCKS);
    tempo_est_t e;
    TEST_ASSERT_TRUE(tempo_estimate(&s_b, &e));
    float nb = tempo_next_beat_in(&e);
    TEST_ASSERT_TRUE(nb > 0.0f);
    TEST_ASSERT_TRUE(nb <= e.period + 0.001f);
}
