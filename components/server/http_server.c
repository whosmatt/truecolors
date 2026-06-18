// http_server.c
#include "server.h"
#include "ws_internal.h"
#include "app_config.h"

#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "esp_log.h"

static const char *TAG = "server";

static void on_socket_close(httpd_handle_t hd, int sockfd)
{
    ws_on_close(sockfd);
    close(sockfd);
}

static const char k_placeholder[] =
    "<!doctype html><meta charset=utf-8><title>TrueColors</title>"
    "<body style=\"background:#111;color:#eee;font-family:system-ui;text-align:center;padding:3rem\">"
    "<h1>TrueColors</h1><p>UI build not embedded yet.</p>";

static esp_err_t spa_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, k_placeholder, HTTPD_RESP_USE_STRLEN);
}

esp_err_t server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = WS_MAX_CLIENTS;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.close_fn = on_socket_close;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    ws_start(server);

    static const httpd_uri_t spa_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = spa_handler,
    };
    httpd_register_uri_handler(server, &spa_uri);

    ESP_LOGI(TAG, "server started");
    return ESP_OK;
}
