// sysmgr_safety.c
#include "sysmgr_safety.h"
#include "app_config.h"

void safety_init(safety_state_t *st)
{
    st->overtemp_latched = false;
    st->fanstall_latched = false;
    st->stall_timer_s = 0.0f;
    st->boot_gate_cleared = false;
}

void safety_step(safety_state_t *st, const safety_inputs_t *in,
                 float requested_a, float dt, safety_output_t *out)
{
    uint32_t warn = 0;
    uint32_t err = 0;

    // A bad NTC reading is treated as overtemp: thermal protection is gone.
    bool overtemp = (!in->ntc_valid) || (in->ntc_temp_c >= T_LIMIT_C);
    if (overtemp) {
        st->overtemp_latched = true;
    }

    // Fan stall: no tach pulses while the fan is commanded hard, sustained.
    if (in->fan_duty >= STALL_DUTY && in->fan_rpm == 0) {
        st->stall_timer_s += dt;
        if (st->stall_timer_s >= STALL_DEBOUNCE_S) {
            st->fanstall_latched = true;
        }
    } else {
        st->stall_timer_s = 0.0f;
    }

    if (st->overtemp_latched) {
        err |= SF_OVERTEMP;
    }
    if (st->fanstall_latched) {
        err |= SF_FAN_STALL;
    }
    bool latched = st->overtemp_latched || st->fanstall_latched;

    bool conditions_ok = in->pd_ok && in->ntc_valid && in->ntc_temp_c < T_LIMIT_C;
    if (conditions_ok) {
        st->boot_gate_cleared = true;
    }

    float scale;
    if (latched) {
        scale = 0.0f;
    } else if (!in->pd_ok) {
        scale = 0.0f;
        err |= SF_PD_LOST;
    } else if (!st->boot_gate_cleared) {
        scale = 0.0f;
    } else {
        scale = 1.0f;
        if (requested_a > 0.0f && in->pd_current_a < requested_a) {
            scale = in->pd_current_a / requested_a;
            if (scale < 0.0f) scale = 0.0f;
            if (scale > 1.0f) scale = 1.0f;
            warn |= SF_UNDERCURRENT;
        }
    }

    if (in->vin_v < VIN_WARN_V) {
        warn |= SF_VIN_LOW;
    }

    out->safety_scale = scale;
    out->warn_flags = warn;
    out->err_flags = err;
    out->latched = latched;
}
