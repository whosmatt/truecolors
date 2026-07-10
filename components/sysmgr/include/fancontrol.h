// fancontrol.h STUB

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configure the LEDC fan channel (fan stays off via R26 until commanded).
esp_err_t fancontrol_init(void);

// Step the PID with the latest temperature, measured rpm and dt; returns
// commanded duty 0..1 (FAN_KICK_DUTY while the fan reads 0 rpm).
float fancontrol_update(float temp_c, int fan_rpm, float dt);

#ifdef __cplusplus
}
#endif
