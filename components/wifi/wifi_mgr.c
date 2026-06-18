// wifi_mgr.c
#include "wifi_mgr.h"
#include "app_config.h"
#include "app_events.h"
#include "storage.h"

#include <string.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "mdns.h"
#include "esp_log.h"

static const char *TAG = "wifi";

#define STA_MAX_RETRY 6

static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static int s_retry;
static app_wifi_evt_t s_state = { .mode = WIFI_MODE_BOOT, .hostname = DEVICE_HOSTNAME };

static void publish_state(void)
{
    app_bus_post(EVT_WIFI_STATE, &s_state, sizeof(s_state), 0);
}

static void start_ap(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[33];
    snprintf(ssid, sizeof(ssid), "truecolors-%02x%02x", mac[4], mac[5]);

    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_state.mode = WIFI_MODE_AP_PROVISION;
    strncpy(s_state.ssid, ssid, sizeof(s_state.ssid) - 1);
    strcpy(s_state.ip, "192.168.4.1");
    ESP_LOGI(TAG, "AP mode: %s", ssid);
    publish_state();
}

static void start_sta(const char *ssid, const char *pass)
{
    wifi_config_t sta = {0};
    strncpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    strncpy((char *)sta.sta.password, pass, sizeof(sta.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_retry = 0;
    s_state.mode = WIFI_MODE_CONNECTING;
    strncpy(s_state.ssid, ssid, sizeof(s_state.ssid) - 1);
    s_state.ip[0] = '\0';
    publish_state();
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_state.mode == WIFI_MODE_AP_PROVISION) {
            return;
        }
        if (s_retry++ < STA_MAX_RETRY) {
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "STA connect failed, falling back to AP");
            start_ap();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = data;
        s_retry = 0;
        s_state.mode = WIFI_MODE_CONNECTED;
        esp_ip4addr_ntoa(&evt->ip_info.ip, s_state.ip, sizeof(s_state.ip));
        ESP_LOGI(TAG, "connected, ip %s", s_state.ip);
        publish_state();
    }
}

static void mdns_start(void)
{
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mdns init failed");
        return;
    }
    mdns_hostname_set(DEVICE_HOSTNAME);
    mdns_instance_name_set("TrueColors");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
}

esp_err_t wifi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(s_sta_netif, DEVICE_HOSTNAME);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    mdns_start();

    char ssid[33] = {0};
    char pass[65] = {0};
    if (storage_load_wifi(ssid, sizeof(ssid), pass, sizeof(pass))) {
        start_sta(ssid, pass);
    } else {
        start_ap();
    }
    return ESP_OK;
}

esp_err_t wifi_enter_ap(void)
{
    start_ap();
    return ESP_OK;
}

esp_err_t wifi_provision(const char *ssid, const char *pass)
{
    esp_err_t err = storage_save_wifi(ssid, pass);
    if (err != ESP_OK) {
        return err;
    }
    esp_wifi_disconnect();
    start_sta(ssid, pass);
    return ESP_OK;
}

int wifi_scan(wifi_ap_info_t *out, int max)
{
    if (esp_wifi_scan_start(NULL, true) != ESP_OK) {
        return 0;
    }
    uint16_t num = max;
    wifi_ap_record_t *recs = calloc(num, sizeof(wifi_ap_record_t));
    if (!recs) {
        return 0;
    }
    esp_wifi_scan_get_ap_records(&num, recs);

    int n = num < max ? num : max;
    for (int i = 0; i < n; i++) {
        strncpy(out[i].ssid, (char *)recs[i].ssid, sizeof(out[i].ssid) - 1);
        out[i].ssid[sizeof(out[i].ssid) - 1] = '\0';
        out[i].rssi = recs[i].rssi;
        out[i].auth = recs[i].authmode;
    }
    free(recs);
    return n;
}

void wifi_get_state(app_wifi_evt_t *out)
{
    if (out) {
        *out = s_state;
    }
}
