// laser.h
// MCPWM RGB laser driver
// Staggered PWM with optional stretching to 100% combined duty

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configure MCPWM (timer, 3 operators), all widths 0, generators forced LOW.
esp_err_t laser_init(void);

// Set the three channels (linear 0..1) and the temporal stretch (0..1).
// Computes staggered widths and loads them atomically on the next TEZ.
void laser_set(float r, float g, float b, float stretch);

// Force all generators LOW immediately (bypasses TEZ) on a FAULT transition.
void laser_emergency_off(void);

#ifdef __cplusplus
}
#endif
