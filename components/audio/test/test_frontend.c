// test_frontend.c
// Unity tests for the mic front end
// used to ensure the frontend used for training preprocessing matches the firmware
#include "frontend.h"

#include <math.h>
#include "unity.h"

// Feed `blocks` blocks of a unit sine and return the last block's output.
static fe_out_t run_sine(uint32_t notch_hz, float freq, int blocks)
{
    static int16_t buf[FE_BLOCK_SAMPLES];
    fe_t fe;
    fe_out_t out = {0};
    fe_init(&fe, notch_hz);
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < FE_BLOCK_SAMPLES; i++) {
            float t = (float)(b * FE_BLOCK_SAMPLES + i) / FE_SAMPLE_RATE;
            buf[i] = (int16_t)(30000.0f * sinf(2.0f * (float)M_PI * freq * t));
        }
        fe_block(&fe, buf, FE_BLOCK_SAMPLES, &out);
    }
    return out;
}

TEST_CASE("comb nulls the PWM fundamental", "[frontend]")
{
    fe_out_t on = run_sine(240, 240.0f, 40);   // comb aligned to the tone
    fe_out_t off = run_sine(480, 240.0f, 40);  // comb aligned elsewhere
    TEST_ASSERT_TRUE(on.rms < 0.02f * off.rms);
}

TEST_CASE("hi-cut rejects above the 4 kHz corner", "[frontend]")
{
    fe_out_t pass = run_sine(480, 1000.0f, 40);
    fe_out_t stop = run_sine(480, 6000.0f, 40);
    TEST_ASSERT_TRUE(stop.rms < 0.2f * pass.rms);
}

TEST_CASE("front end carries no state between instances", "[frontend]")
{
    fe_out_t a = run_sine(480, 1000.0f, 12);
    fe_out_t b = run_sine(480, 1000.0f, 12);
    TEST_ASSERT_EQUAL_FLOAT(a.rms, b.rms);
    TEST_ASSERT_EQUAL_FLOAT(a.level, b.level);
    TEST_ASSERT_EQUAL_FLOAT(a.flux[0], b.flux[0]);
    TEST_ASSERT_EQUAL_FLOAT(a.spl_db, b.spl_db);
}

TEST_CASE("sample rate divides every PWM frequency", "[frontend]")
{
    const int hz[] = {120, 240, 480};
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT(0, FE_SAMPLE_RATE % hz[i]);
        TEST_ASSERT_TRUE(FE_SAMPLE_RATE / hz[i] <= FE_COMB_MAX);
    }
}
