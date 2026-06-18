// fancontrol.h STUB

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configure the LEDC fan channel (fan stays off via R26 until commanded).
esp_err_t fancontrol_init(void);

// Step the PID with the latest temperature and dt; returns commanded duty 0..1.
float fancontrol_update(float temp_c, float dt);

#ifdef __cplusplus
}
#endif
