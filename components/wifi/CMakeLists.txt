idf_component_register(
    SRCS "wifi_mgr.c" "dns_server.c"
    INCLUDE_DIRS "include"
    REQUIRES common
    PRIV_REQUIRES esp_wifi esp_netif esp_event storage espressif__mdns lwip
)
