// sysmgr.h STUB
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Init I2C/ADC/PCNT/LEDC/status-LED and safety.
esp_err_t sysmgr_init(void);

// Restore power if PD is good within POWER_RESTORE_WINDOW_MS.
void sysmgr_restore_power(bool saved_on);

// Arm or cancel a pending button confirmation
// Result is sent via EVT_CONFIRM
void sysmgr_confirm_arm(void);
void sysmgr_confirm_cancel(void);

// Start the sysmgr_monitor task.
esp_err_t sysmgr_start(void);

#ifdef __cplusplus
}
#endif
