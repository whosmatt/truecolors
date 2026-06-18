// storage.h

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Open NVS and create the debounced scene-persist timer.
esp_err_t storage_init(void);

// Subscribe for scene changes and apply any persisted scene.
esp_err_t storage_restore_scene(void);

#ifdef __cplusplus
}
#endif
