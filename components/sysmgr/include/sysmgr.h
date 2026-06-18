// sysmgr.h STUB
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Init I2C/ADC/PCNT/LEDC/status-LED and the safety gate.
esp_err_t sysmgr_init(void);

// Start the sysmgr_monitor task.
esp_err_t sysmgr_start(void);

#ifdef __cplusplus
}
#endif
