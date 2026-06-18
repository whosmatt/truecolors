// wifi_mgr.h STUB

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start Wi-Fi (STA, falling back to AP provisioning) + mDNS. (Stub in step 1.)
esp_err_t wifi_start(void);

// Enter AP / provisioning mode (button press or WS command).
esp_err_t wifi_enter_ap(void);

#ifdef __cplusplus
}
#endif
