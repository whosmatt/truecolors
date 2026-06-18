idf_component_register(
    SRCS "http_server.c" "ws.c"
    INCLUDE_DIRS "include"
    REQUIRES common appstate esp_http_server
    PRIV_REQUIRES effects wifi storage espressif__cjson esp_system lwip
)
