// http_server.c
#include "server.h"
#include "ws_internal.h"
#include "web_assets.h"
#include "app_config.h"

#include <string.h>
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "server";

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[]   asm("_binary_index_html_gz_end");

static void on_socket_close(httpd_handle_t hd, int sockfd)
{
    ws_on_close(sockfd);
    close(sockfd);
}

static esp_err_t spa_handler(httpd_req_t *req)
{
    char inm[80];
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK &&
        strcmp(inm, WEB_INDEX_ETAG) == 0) {
        httpd_resp_set_status(req, "304 Not Modified");
        httpd_resp_set_hdr(req, "ETag", WEB_INDEX_ETAG);
        return httpd_resp_send(req, NULL, 0);
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "ETag", WEB_INDEX_ETAG);
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, (const char *)index_html_gz_start,
                           index_html_gz_end - index_html_gz_start);
}

static esp_err_t ota_handler(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no ota partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota;
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin failed");
        return ESP_FAIL;
    }

    char buf[1024];
    int remaining = req->content_len;
    while (remaining > 0) {
        int got = httpd_req_recv(req, buf, remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf));
        if (got <= 0) {
            esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
            return ESP_FAIL;
        }
        if (esp_ota_write(ota, buf, got) != ESP_OK) {
            esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota write failed");
            return ESP_FAIL;
        }
        remaining -= got;
    }

    if (esp_ota_end(ota) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota finalize failed");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "ok");
    ESP_LOGI(TAG, "OTA applied, rebooting");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

esp_err_t server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // esp_ota_end() verifies the image (SHA256 + esp_image logging) on the
    // httpd task stack; the 4 KB default overflows during OTA finalize.
    config.stack_size = 8192;
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

    static const httpd_uri_t ota_uri = {
        .uri = "/api/ota",
        .method = HTTP_POST,
        .handler = ota_handler,
    };
    httpd_register_uri_handler(server, &ota_uri);

    static const httpd_uri_t spa_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = spa_handler,
    };
    httpd_register_uri_handler(server, &spa_uri);

    ESP_LOGI(TAG, "server started");
    return ESP_OK;
}
