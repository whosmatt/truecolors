// wifi_mgr.h STUB

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start Wi-Fi in STA mode, falling back to AP provisioning, plus mDNS.
esp_err_t wifi_start(void);

// Enter AP / provisioning mode.
esp_err_t wifi_enter_ap(void);

#ifdef __cplusplus
}
#endif
