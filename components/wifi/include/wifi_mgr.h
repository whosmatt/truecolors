// wifi_mgr.h

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "app_events.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char    ssid[33];
    int8_t  rssi;
    int     auth;
} wifi_ap_info_t;

// Start Wi-Fi in STA mode, falling back to AP provisioning, plus mDNS.
esp_err_t wifi_start(void);

// Enter AP / provisioning mode.
esp_err_t wifi_enter_ap(void);

// Store credentials and (re)connect as a station.
esp_err_t wifi_provision(const char *ssid, const char *pass);

// Blocking scan, fills up to max entries, returns the count.
int wifi_scan(wifi_ap_info_t *out, int max);

// Current connectivity state for the WS snapshot.
void wifi_get_state(app_wifi_evt_t *out);

#ifdef __cplusplus
}
#endif
