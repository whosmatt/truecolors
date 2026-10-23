// sysmgr.h STUB
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Init I2C/ADC/PCNT/LEDC/status-LED and safety.
esp_err_t sysmgr_init(void);

// Restore power if PD is good within POWER_RESTORE_WINDOW_MS.
void sysmgr_restore_power(bool saved_on);

// Start the sysmgr_monitor task.
esp_err_t sysmgr_start(void);

#ifdef __cplusplus
}
#endif
