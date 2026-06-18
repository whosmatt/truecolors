// effects.h

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the effect registry. (Stub in step 1.)
esp_err_t effects_init(void);

// Create the laser_render task (active effect -> gamma -> brightness ->
// safety scale -> laser_set). Started from app_main in step 4.
esp_err_t effects_start_render(void);

#ifdef __cplusplus
}
#endif
