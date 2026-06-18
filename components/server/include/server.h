// server.h

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start httpd (SPA + OTA + /ws) and register bus subscribers.
esp_err_t server_start(void);

#ifdef __cplusplus
}
#endif
