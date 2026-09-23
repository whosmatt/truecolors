// test_frontend.c
// Unity tests for the mic front end
// used to ensure the frontend used for training preprocessing matches the firmware
#include "frontend.h"

#include <math.h>
#include "unity.h"

// Feed `blocks` blocks of a unit sine and return the last block's output.
static fe_out_t run_sine(float freq, int blocks)
{
    static int16_t buf[FE_BLOCK_SAMPLES];
    fe_t fe;
    fe_out_t out = {0};
    fe_init(&fe);
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < FE_BLOCK_SAMPLES; i++) {
            float t = (float)(b * FE_BLOCK_SAMPLES + i) / FE_SAMPLE_RATE;
            buf[i] = (int16_t)(30000.0f * sinf(2.0f * (float)M_PI * freq * t));
        }
        fe_block(&fe, buf, FE_BLOCK_SAMPLES, &out);
    }
    return out;
}

TEST_CASE("hi-cut rejects above the 4 kHz corner", "[frontend]")
{
    fe_out_t pass = run_sine(1000.0f, 40);
    fe_out_t stop = run_sine(6000.0f, 40);
    TEST_ASSERT_TRUE(stop.rms < 0.2f * pass.rms);
}

// The load-time model check depends on this staying truthful.
TEST_CASE("variant reports hi-cut only", "[frontend]")
{
    TEST_ASSERT_EQUAL_UINT32(FE_VARIANT_HICUT, fe_variant());
    TEST_ASSERT_EQUAL_INT(2, FE_SPEC_VERSION);
}

TEST_CASE("front end carries no state between instances", "[frontend]")
{
    fe_out_t a = run_sine(1000.0f, 12);
    fe_out_t b = run_sine(1000.0f, 12);
    TEST_ASSERT_EQUAL_FLOAT(a.rms, b.rms);
    TEST_ASSERT_EQUAL_FLOAT(a.level, b.level);
    TEST_ASSERT_EQUAL_FLOAT(a.flux[0], b.flux[0]);
    TEST_ASSERT_EQUAL_FLOAT(a.spl_db, b.spl_db);
}

// model_meta.json records both; a model is valid only for its geometry.
TEST_CASE("block geometry matches the model contract", "[frontend]")
{
    TEST_ASSERT_EQUAL_INT(48000, FE_SAMPLE_RATE);
    TEST_ASSERT_EQUAL_INT(512, FE_BLOCK_SAMPLES);
}
