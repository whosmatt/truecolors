// sysmgr_safety.h
// Pure safety state machine, no hardware dependency, host-testable.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Flag bits, must match app_flag_t in app_events.h.
#define SF_VIN_LOW       (1u << 0)
#define SF_UNDERCURRENT  (1u << 1)
#define SF_PD_LOST       (1u << 2)
#define SF_OVERTEMP      (1u << 3)
#define SF_FAN_STALL     (1u << 4)

typedef struct {
    bool  pd_ok;
    float pd_current_a;
    float ntc_temp_c;
    bool  ntc_valid;
    int   fan_rpm;
    float fan_duty;
    float vin_v;
} safety_inputs_t;

typedef struct {
    bool  overtemp_latched;
    bool  fanstall_latched;
    float stall_timer_s;
    bool  boot_gate_cleared;
} safety_state_t;

typedef struct {
    float    safety_scale;
    uint32_t warn_flags;
    uint32_t err_flags;
    bool     latched;
} safety_output_t;

void safety_init(safety_state_t *st);

// Advance the machine by dt seconds.
void safety_step(safety_state_t *st, const safety_inputs_t *in,
                 float dt, safety_output_t *out);

#ifdef __cplusplus
}
#endif
