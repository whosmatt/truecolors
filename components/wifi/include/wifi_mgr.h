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

// Add and connect to a new network
esp_err_t wifi_add_network(const char *ssid, const char *pass);

// ESP_ERR_NOT_FOUND if unknown. Reconnects elsewhere if it is the current one.
esp_err_t wifi_forget_network(const char *ssid);

// SSIDs of the known networks, never passwords. Returns the count.
int wifi_known_networks(char (*ssids)[33], int max);

// Blocking scan, fills up to max entries, returns the count.
int wifi_scan(wifi_ap_info_t *out, int max);

// Current connectivity state for the WS snapshot.
void wifi_get_state(app_wifi_evt_t *out);

#ifdef __cplusplus
}
#endif
