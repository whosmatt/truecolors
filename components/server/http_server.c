// http_server.c
#include "server.h"
#include "ws_internal.h"
#include "web_assets.h"
#include "app_config.h"
#include "wifi_mgr.h"

#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_secure_boot.h"
#include "esp_heap_caps.h"
#include "psa/crypto.h"
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
    // Captive portal in AP mode
    app_wifi_evt_t net;
    wifi_get_state(&net);
    if (net.mode == WIFI_MODE_AP_PROVISION) {
        char host[64] = {0};
        if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
            strncmp(host, "192.168.4.1", 11) != 0) {
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
            return httpd_resp_send(req, NULL, 0);
        }
    }

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

// Reject bad OTA early. esp_ota_end does the final check anyways, so this is not critical
static bool signature_ok(const uint8_t *img, size_t len)
{
    size_t signed_len = len - sizeof(ets_secure_boot_signature_t);
    uint8_t digest[32];
    size_t digest_len;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256, img, signed_len, digest, sizeof(digest),
                         &digest_len) != PSA_SUCCESS) {
        return false;
    }
    const ets_secure_boot_signature_t *sig = (const void *)(img + signed_len);
    return esp_secure_boot_verify_sbv2_signature_block(sig, digest, NULL) == ESP_OK;
}

#define OTA_CHUNK (64 * 1024)

typedef struct {
    httpd_req_t *req;   // async copy, answered once the write is done
    const esp_partition_t *part;
    uint8_t *img;
    size_t len;
} ota_job_t;

static volatile bool s_ota_busy;

static void ota_write_task(void *arg)
{
    ota_job_t *j = arg;
    esp_ota_handle_t ota;
    esp_err_t err = esp_ota_begin(j->part, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (err == ESP_OK) {
        for (size_t off = 0; err == ESP_OK && off < j->len; off += OTA_CHUNK) {
            size_t n = j->len - off < OTA_CHUNK ? j->len - off : OTA_CHUNK;
            err = esp_ota_write(ota, j->img + off, n);
            ws_broadcast_ota("write", (float)(off + n) / j->len);
        }
        if (err != ESP_OK) {
            esp_ota_abort(ota);
        } else if ((err = esp_ota_end(ota)) == ESP_OK) {
            err = esp_ota_set_boot_partition(j->part);
        }
    }
    free(j->img);

    if (err == ESP_OK) {
        ws_broadcast_ota("done", 1.0f);
        httpd_resp_sendstr(j->req, "ok");
    } else {
        ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
        ws_broadcast_ota("error", 0.0f);
        httpd_resp_send_err(j->req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota write failed");
    }
    httpd_req_async_handler_complete(j->req);
    free(j);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA applied, rebooting");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    s_ota_busy = false;
    vTaskDelete(NULL);
}

static esp_err_t ota_handler(httpd_req_t *req)
{
    if (s_ota_busy) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "ota in progress");
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no ota partition");
        return ESP_FAIL;
    }

    // Buffer the entire image in PSRAM for signature verification
    size_t len = req->content_len;
    const size_t sig_len = sizeof(ets_secure_boot_signature_t);
    if (len <= sig_len || len % sig_len || len > part->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad image size");
        return ESP_FAIL;
    }
    uint8_t *img = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!img) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_FAIL;
    }

    for (size_t got = 0; got < len;) {
        int n = httpd_req_recv(req, (char *)img + got, len - got);
        if (n <= 0) {
            free(img);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
            return ESP_FAIL;
        }
        got += n;
    }

    if (!signature_ok(img, len)) {
        free(img);
        ESP_LOGW(TAG, "OTA rejected: signature");
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "bad signature");
        return ESP_FAIL;
    }

    ota_job_t *job = malloc(sizeof(*job));
    if (!job || httpd_req_async_handler_begin(req, &job->req) != ESP_OK) {
        free(job);
        free(img);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_FAIL;
    }
    job->part = part;
    job->img = img;
    job->len = len;
    s_ota_busy = true;
    ws_broadcast_ota("write", 0.0f);
    if (xTaskCreatePinnedToCore(ota_write_task, "ota_write", 8192, job, 3, NULL, 0) != pdPASS) {
        s_ota_busy = false;
        httpd_resp_send_err(job->req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        httpd_req_async_handler_complete(job->req);
        free(img);
        free(job);
        return ESP_FAIL;
    }
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
    config.core_id = 0;   // keep core 1 for audio

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
