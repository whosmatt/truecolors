// test_sysmgr_safety.c
// Host unit test for the safety state machine:
//   gcc -I ../include -I ../../common/include test_sysmgr_safety.c ../sysmgr_safety.c -o /tmp/st && /tmp/st
#include "sysmgr_safety.h"
#include "app_config.h"

#include <stdio.h>
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
        printf("  FAIL: %s (got %.3f want %.3f)\n", msg, got, want);
    }
}

// A nominal, everything-good input.
static safety_inputs_t good(void)
{
    safety_inputs_t in = {
        .pd_ok = true,
        .pd_current_a = 3.0f,
        .ntc_temp_c = 30.0f,
        .ntc_valid = true,
        .fan_rpm = 2000,
        .fan_duty = 0.2f,
        .vin_v = 20.0f,
    };
    return in;
}

// Clear the boot gate with one good step.
static void prime(safety_state_t *st)
{
    safety_inputs_t g = good();
    safety_output_t o;
    safety_step(st, &g, LASER_REQUESTED_A, 0.1f, &o);
}

int main(void)
{
    safety_state_t st;
    safety_output_t out;
    safety_inputs_t in;

    // Boot gate: lasers stay off until the first good reading, then run.
    safety_init(&st);
    in = good();
    in.pd_ok = false;
    safety_step(&st, &in, LASER_REQUESTED_A, 0.1f, &out);
    check_near(out.safety_scale, 1.0f, 0.001f, "boot: no PD -> still runs");
    check((out.warn_flags & SF_PD_LOST) != 0, "boot: no PD -> warn flag");

    in = good();
    safety_step(&st, &in, LASER_REQUESTED_A, 0.1f, &out);
    check_near(out.safety_scale, 1.0f, 0.001f, "boot cleared -> scale 1");
    check(out.warn_flags == 0 && out.err_flags == 0, "nominal: no flags");

    // Overtemp latches and does not auto-recover.
    safety_init(&st);
    prime(&st);
    in = good();
    in.ntc_temp_c = 56.0f;
    safety_step(&st, &in, LASER_REQUESTED_A, 0.1f, &out);
    check_near(out.safety_scale, 0.0f, 0.001f, "overtemp -> scale 0");
    check((out.err_flags & SF_OVERTEMP) != 0, "overtemp flag set");
    check(out.latched, "overtemp latched");
    in = good();
    safety_step(&st, &in, LASER_REQUESTED_A, 0.1f, &out);
    check_near(out.safety_scale, 0.0f, 0.001f, "overtemp does not auto-recover");
    check(out.latched, "overtemp stays latched");

    // Bad NTC is treated as overtemp.
    safety_init(&st);
    in = good();
    in.ntc_valid = false;
    safety_step(&st, &in, LASER_REQUESTED_A, 0.1f, &out);
    check(out.latched && (out.err_flags & SF_OVERTEMP), "NTC invalid -> latched overtemp");

    // Fan stall: needs the debounce window before latching.
    safety_init(&st);
    prime(&st);
    in = good();
    in.fan_duty = 0.6f;
    in.fan_rpm = 0;
    safety_step(&st, &in, LASER_REQUESTED_A, STALL_DEBOUNCE_S * 0.5f, &out);
    check(!out.latched, "short stall not yet latched");
    safety_step(&st, &in, LASER_REQUESTED_A, STALL_DEBOUNCE_S, &out);
    check(out.latched && (out.err_flags & SF_FAN_STALL), "sustained stall -> latched");

    // Stall timer resets when the fan is not commanded hard.
    safety_init(&st);
    prime(&st);
    in = good();
    in.fan_duty = 0.1f;
    in.fan_rpm = 0;
    safety_step(&st, &in, LASER_REQUESTED_A, STALL_DEBOUNCE_S * 2.0f, &out);
    check(!out.latched, "rpm 0 below stall duty is not a stall");

    // Insufficient current limits proportionally.
    safety_init(&st);
    prime(&st);
    in = good();
    in.pd_current_a = 1.0f;
    safety_step(&st, &in, 2.5f, 0.1f, &out);
    check_near(out.safety_scale, 0.4f, 0.001f, "undercurrent -> scale budget/requested");
    check((out.warn_flags & SF_UNDERCURRENT) != 0, "undercurrent warn flag");
    check(!out.latched, "undercurrent is not a fault");

    // No PD: undercurrent check is skipped, full scale runs.
    safety_init(&st);
    prime(&st);
    in = good();
    in.pd_ok = false;
    in.pd_current_a = 0.0f;
    safety_step(&st, &in, LASER_REQUESTED_A, 0.1f, &out);
    check_near(out.safety_scale, 1.0f, 0.001f, "no PD -> full scale (no undercurrent throttle)");
    check((out.warn_flags & SF_UNDERCURRENT) == 0, "no PD -> no undercurrent flag");

    // Undervoltage is a warning only, lasers keep running.
    safety_init(&st);
    prime(&st);
    in = good();
    in.vin_v = 16.0f;
    safety_step(&st, &in, LASER_REQUESTED_A, 0.1f, &out);
    check((out.warn_flags & SF_VIN_LOW) != 0, "undervoltage warn flag");
    check_near(out.safety_scale, 1.0f, 0.001f, "undervoltage keeps running");

    // PD lost is a warning: output continues, flag clears when PD comes back.
    safety_init(&st);
    prime(&st);
    in = good();
    in.pd_ok = false;
    safety_step(&st, &in, LASER_REQUESTED_A, 0.1f, &out);
    check_near(out.safety_scale, 1.0f, 0.001f, "PD lost -> still runs");
    check((out.warn_flags & SF_PD_LOST) && !out.latched, "PD lost warn, not latched");
    in = good();
    safety_step(&st, &in, LASER_REQUESTED_A, 0.1f, &out);
    check_near(out.safety_scale, 1.0f, 0.001f, "PD restored -> warn clears");

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
