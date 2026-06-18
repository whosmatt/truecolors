// storage.h STUB

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Init NVS and register the debounced persist subscriber.
esp_err_t storage_init(void);

// Restore the persisted scene into appstate via STORAGE_RESTORE patch.
esp_err_t storage_restore_scene(void);

#ifdef __cplusplus
}
#endif
