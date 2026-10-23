// wifi_mgr.c
#include "wifi_mgr.h"
#include "app_config.h"
#include "app_events.h"
#include "storage.h"

#include <stdlib.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "mdns.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "dhcpserver/dhcpserver.h"
#include "dns_server.h"

static const char *TAG = "wifi";

#define STA_MAX_RETRY 6
#define SCAN_MAX      20

ESP_EVENT_DEFINE_BASE(WIFI_MGR_EVENT);
typedef struct {
    bool forgot;
    char ssid[33];
} list_change_t;

static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static int s_retry;
static wifi_net_t s_cand[WIFI_NETS_MAX];   // known networks, strongest first
static int s_ncand, s_icand;
static bool s_scanning;
static app_wifi_evt_t s_state = { .mode = WIFI_MODE_BOOT, .hostname = DEVICE_HOSTNAME };

static void publish_state(void)
{
    app_bus_post(EVT_WIFI_STATE, &s_state, sizeof(s_state), 0);
}

// Set captive portal DNS
static void configure_ap_dns(void)
{
    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ipaddr_addr("192.168.4.1");

    esp_netif_dhcps_stop(s_ap_netif);  // options can only change while stopped
    esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    dhcps_offer_t offer_dns = OFFER_DNS;   // advertise ourselves as DNS server
    esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET,
                           ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns, sizeof(offer_dns));
    esp_netif_dhcps_start(s_ap_netif);
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

    configure_ap_dns();   // hand out our IP as DNS for the captive portal
    dns_server_start();   // hijack lookups -> 192.168.4.1

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

    dns_server_stop();  // leave captive-portal mode

    s_retry = 0;
    s_state.mode = WIFI_MODE_CONNECTING;
    strncpy(s_state.ssid, ssid, sizeof(s_state.ssid) - 1);
    s_state.ip[0] = '\0';
    publish_state();
    esp_wifi_connect();
}

static void try_candidate(void)
{
    if (s_icand >= s_ncand) {
        ESP_LOGW(TAG, "no known network reachable, falling back to AP");
        start_ap();
        return;
    }
    start_sta(s_cand[s_icand].ssid, s_cand[s_icand].pass);
}

// Orders by strength and includes hidden at the end
static void order_candidates(void)
{
    int8_t best[WIFI_NETS_MAX];
    for (int i = 0; i < s_ncand; i++) {
        best[i] = INT8_MIN;
    }
    uint16_t num = SCAN_MAX;
    wifi_ap_record_t *recs = calloc(num, sizeof(*recs));
    if (recs && esp_wifi_scan_get_ap_records(&num, recs) == ESP_OK) {
        for (int r = 0; r < num; r++) {
            for (int i = 0; i < s_ncand; i++) {
                if (strcmp((const char *)recs[r].ssid, s_cand[i].ssid) == 0 && recs[r].rssi > best[i]) {
                    best[i] = recs[r].rssi;
                }
            }
        }
    } else {
        esp_wifi_clear_ap_list();
    }
    free(recs);

    for (int i = 1; i < s_ncand; i++) {
        for (int j = i; j > 0 && best[j] > best[j - 1]; j--) {
            wifi_net_t t = s_cand[j]; s_cand[j] = s_cand[j - 1]; s_cand[j - 1] = t;
            int8_t b = best[j]; best[j] = best[j - 1]; best[j - 1] = b;
        }
    }
}

static void connect_known(void)
{
    s_ncand = storage_load_wifi_nets(s_cand);
    s_icand = 0;
    if (s_ncand == 0) {
        start_ap();
        return;
    }
    s_scanning = true;
    esp_wifi_disconnect();
    dns_server_stop();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_state.mode = WIFI_MODE_CONNECTING;
    s_state.ssid[0] = '\0';
    s_state.ip[0] = '\0';
    publish_state();
    if (esp_wifi_scan_start(NULL, false) != ESP_OK) {
        s_scanning = false;
        try_candidate();
    }
}

static void on_list_change(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const list_change_t *c = data;
    bool current = s_state.mode != WIFI_MODE_AP_PROVISION && strcmp(c->ssid, s_state.ssid) == 0;
    if (c->forgot ? current : s_state.mode != WIFI_MODE_CONNECTED) {
        connect_known();
    } else {
        publish_state();   // the known list changed
    }
}

static esp_err_t change_list(bool forgot, const char *ssid)
{
    list_change_t c = { .forgot = forgot };
    strlcpy(c.ssid, ssid, sizeof(c.ssid));
    return esp_event_post(WIFI_MGR_EVENT, 0, &c, sizeof(c), pdMS_TO_TICKS(100));
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_state.mode == WIFI_MODE_AP_PROVISION || s_scanning) {
            return;
        }
        if (s_retry++ < STA_MAX_RETRY) {
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "\"%s\" failed", s_state.ssid);
            if (s_state.mode == WIFI_MODE_CONNECTED) {
                connect_known();   // lost a working link: rescan with the current list
            } else {
                s_icand++;
                try_candidate();
            }
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        if (s_scanning) {
            s_scanning = false;
            order_candidates();
            try_candidate();
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
    mdns_instance_name_set("truecolors");
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
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_MGR_EVENT, ESP_EVENT_ANY_ID, on_list_change, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    mdns_start();

    connect_known();
    return ESP_OK;
}

esp_err_t wifi_enter_ap(void)
{
    start_ap();
    return ESP_OK;
}

static int find_net(const wifi_net_t *nets, int n, const char *ssid)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(nets[i].ssid, ssid) == 0) {
            return i;
        }
    }
    return -1;
}

esp_err_t wifi_add_network(const char *ssid, const char *pass)
{
    wifi_net_t nets[WIFI_NETS_MAX];
    int n = storage_load_wifi_nets(nets);
    int i = find_net(nets, n, ssid);
    if (i < 0) {
        if (n == WIFI_NETS_MAX) {
            return ESP_ERR_NO_MEM;
        }
        i = n++;
    }
    strlcpy(nets[i].ssid, ssid, sizeof(nets[i].ssid));
    strlcpy(nets[i].pass, pass, sizeof(nets[i].pass));
    esp_err_t err = storage_save_wifi_nets(nets, n);
    memset(nets, 0, sizeof(nets));
    return err == ESP_OK ? change_list(false, ssid) : err;
}

esp_err_t wifi_forget_network(const char *ssid)
{
    wifi_net_t nets[WIFI_NETS_MAX];
    int n = storage_load_wifi_nets(nets);
    int i = find_net(nets, n, ssid);
    if (i < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    memmove(&nets[i], &nets[i + 1], (n - i - 1) * sizeof(nets[0]));
    esp_err_t err = storage_save_wifi_nets(nets, n - 1);
    memset(nets, 0, sizeof(nets));
    return err == ESP_OK ? change_list(true, ssid) : err;
}

int wifi_known_networks(char (*ssids)[33], int max)
{
    wifi_net_t nets[WIFI_NETS_MAX];
    int n = storage_load_wifi_nets(nets);
    if (n > max) {
        n = max;
    }
    for (int i = 0; i < n; i++) {
        memcpy(ssids[i], nets[i].ssid, sizeof(ssids[i]));
    }
    memset(nets, 0, sizeof(nets));
    return n;
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
