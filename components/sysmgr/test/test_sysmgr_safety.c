// test_sysmgr_safety.c
// Unity tests for the safety state machine; runs on the host via test/host/.
#include "sysmgr_safety.h"
#include "app_config.h"

#include "unity.h"

// A nominal, everything-good input.
static safety_inputs_t good(void)
{
    safety_inputs_t in = {
        .pd_ok = true,
        .pd_current_a = 3.0f,
        .ntc_temp_c = 30.0f,
        .ntc_valid = true,
        .fan_rpm = 2000,
        .vin_v = 20.0f,
    };
    return in;
}

// Clear the boot gate with one good step.
static void prime(safety_state_t *st)
{
    safety_inputs_t g = good();
    safety_output_t o;
    safety_step(st, &g, 0.1f, &o);
}

// Boot gate: lasers run once the first good reading arrives; a missing PD
// source at boot is only a warning.
TEST_CASE("boot: PD loss warns, laser still gated by NTC only", "[sysmgr_safety]")
{
    safety_state_t st;
    safety_output_t out;
    safety_inputs_t in;

    safety_init(&st);
    in = good();
    in.pd_ok = false;
    safety_step(&st, &in, 0.1f, &out);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 1.0f, out.safety_scale, "boot: no PD -> still runs");
    TEST_ASSERT_TRUE_MESSAGE(out.warn_flags & SF_PD_LOST, "boot: no PD -> warn flag");

    in = good();
    safety_step(&st, &in, 0.1f, &out);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 1.0f, out.safety_scale, "boot cleared -> scale 1");
    TEST_ASSERT_TRUE_MESSAGE(out.warn_flags == 0 && out.err_flags == 0, "nominal: no flags");
}

TEST_CASE("overtemp latches and does not auto-recover", "[sysmgr_safety]")
{
    safety_state_t st;
    safety_output_t out;
    safety_inputs_t in;

    safety_init(&st);
    prime(&st);
    in = good();
    in.ntc_temp_c = 51.0f;
    safety_step(&st, &in, 0.1f, &out);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 0.0f, out.safety_scale, "overtemp -> scale 0");
    TEST_ASSERT_TRUE_MESSAGE(out.err_flags & SF_OVERTEMP, "overtemp flag set");
    TEST_ASSERT_TRUE_MESSAGE(out.latched, "overtemp latched");

    in = good();
    safety_step(&st, &in, 0.1f, &out);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 0.0f, out.safety_scale, "overtemp does not auto-recover");
    TEST_ASSERT_TRUE_MESSAGE(out.latched, "overtemp stays latched");
}

// A bad NTC reading means thermal protection is gone: treated as overtemp.
TEST_CASE("invalid NTC latches as overtemp", "[sysmgr_safety]")
{
    safety_state_t st;
    safety_output_t out;
    safety_inputs_t in;

    safety_init(&st);
    in = good();
    in.ntc_valid = false;
    safety_step(&st, &in, 0.1f, &out);
    TEST_ASSERT_TRUE_MESSAGE(out.latched && (out.err_flags & SF_OVERTEMP),
                             "NTC invalid -> latched overtemp");
}

// Fan stall: silent tach warns after STALL_WARN_S while the kick tries a
// start, and only latches an error after STALL_FAIL_S.
TEST_CASE("fan stall: warn during spin-up, latch on failed start", "[sysmgr_safety]")
{
    safety_state_t st;
    safety_output_t out;
    safety_inputs_t in;

    safety_init(&st);
    prime(&st);
    in = good();
    in.fan_rpm = 0;
    safety_step(&st, &in, STALL_WARN_S * 0.5f, &out);
    TEST_ASSERT_TRUE_MESSAGE((out.warn_flags & SF_FAN_STALL) == 0 && !out.latched,
                             "brief tach silence: no flags");
    safety_step(&st, &in, STALL_WARN_S, &out);
    TEST_ASSERT_TRUE_MESSAGE(out.warn_flags & SF_FAN_STALL, "sustained silence -> warn");
    TEST_ASSERT_TRUE_MESSAGE(!out.latched && out.err_flags == 0, "spin-up attempt is not an error");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 1.0f, out.safety_scale, "warning keeps running");

    // Recovery during the attempt clears the warning without latching.
    in.fan_rpm = 500;
    safety_step(&st, &in, 0.1f, &out);
    TEST_ASSERT_TRUE_MESSAGE((out.warn_flags & SF_FAN_STALL) == 0 && !out.latched,
                             "fan recovered -> warn clears, no latch");

    // A failed start attempt latches.
    in.fan_rpm = 0;
    safety_step(&st, &in, STALL_FAIL_S, &out);
    TEST_ASSERT_TRUE_MESSAGE(out.latched && (out.err_flags & SF_FAN_STALL),
                             "failed start -> latched");
    in = good();
    safety_step(&st, &in, 0.1f, &out);
    TEST_ASSERT_TRUE_MESSAGE(out.latched, "fan stall stays latched");
}

// Undercurrent policy: warning only, no brightness limiting.
TEST_CASE("undercurrent warns at full scale", "[sysmgr_safety]")
{
    safety_state_t st;
    safety_output_t out;
    safety_inputs_t in;

    safety_init(&st);
    prime(&st);
    in = good();
    in.pd_current_a = 1.0f;
    safety_step(&st, &in, 0.1f, &out);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 1.0f, out.safety_scale, "undercurrent keeps full scale");
    TEST_ASSERT_TRUE_MESSAGE(out.warn_flags & SF_UNDERCURRENT, "undercurrent warn flag");
    TEST_ASSERT_TRUE_MESSAGE(!out.latched, "undercurrent is not a fault");
}

// The requirement follows vin: 2.25 A is enough at 20 V, not at 12 V.
TEST_CASE("undercurrent threshold follows vin", "[sysmgr_safety]")
{
    safety_state_t st;
    safety_output_t out;
    safety_inputs_t in;

    safety_init(&st);
    prime(&st);
    in = good();
    in.pd_current_a = 2.25f;
    safety_step(&st, &in, 0.1f, &out);
    TEST_ASSERT_TRUE_MESSAGE((out.warn_flags & SF_UNDERCURRENT) == 0, "2.25 A ok at 20 V");
    in.vin_v = 12.0f;
    safety_step(&st, &in, 0.1f, &out);
    TEST_ASSERT_TRUE_MESSAGE(out.warn_flags & SF_UNDERCURRENT, "2.25 A undercurrent at 12 V");
}

TEST_CASE("undercurrent check skipped without PD", "[sysmgr_safety]")
{
    safety_state_t st;
    safety_output_t out;
    safety_inputs_t in;

    safety_init(&st);
    prime(&st);
    in = good();
    in.pd_ok = false;
    in.pd_current_a = 0.0f;
    safety_step(&st, &in, 0.1f, &out);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 1.0f, out.safety_scale,
                                     "no PD -> full scale (no undercurrent throttle)");
    TEST_ASSERT_TRUE_MESSAGE((out.warn_flags & SF_UNDERCURRENT) == 0, "no PD -> no undercurrent flag");
}

TEST_CASE("undervoltage warns only", "[sysmgr_safety]")
{
    safety_state_t st;
    safety_output_t out;
    safety_inputs_t in;

    safety_init(&st);
    prime(&st);
    in = good();
    in.vin_v = 16.0f;
    safety_step(&st, &in, 0.1f, &out);
    TEST_ASSERT_TRUE_MESSAGE(out.warn_flags & SF_VIN_LOW, "undervoltage warn flag");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 1.0f, out.safety_scale, "undervoltage keeps running");
}

TEST_CASE("PD lost warns and clears on restore", "[sysmgr_safety]")
{
    safety_state_t st;
    safety_output_t out;
    safety_inputs_t in;

    safety_init(&st);
    prime(&st);
    in = good();
    in.pd_ok = false;
    safety_step(&st, &in, 0.1f, &out);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 1.0f, out.safety_scale, "PD lost -> still runs");
    TEST_ASSERT_TRUE_MESSAGE((out.warn_flags & SF_PD_LOST) && !out.latched,
                             "PD lost warn, not latched");
    in = good();
    safety_step(&st, &in, 0.1f, &out);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 1.0f, out.safety_scale, "PD restored -> warn clears");
}
