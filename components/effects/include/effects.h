// effects.h

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t effects_init(void);

// Create the laser_render task.
esp_err_t effects_start_render(void);

#ifdef __cplusplus
}
#endif
