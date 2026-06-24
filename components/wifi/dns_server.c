// dns_server.c

#include "dns_server.h"

#include <string.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "dns";

#define DNS_PORT     53
#define DNS_MAX_LEN  512

static const uint8_t AP_IP[4] = {192, 168, 4, 1};

static TaskHandle_t s_task;
static volatile bool s_run;

static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket failed: errno %d", errno);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind :53 failed: errno %d", errno);
        close(sock);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    // 1 s recv timeout so the loop can notice s_run going false and exit.
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "captive DNS up on :53");

    uint8_t buf[DNS_MAX_LEN];
    while (s_run) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &slen);
        if (n < 12) {
            continue;             // not even a full header (or recv timeout)
        }
        if (buf[2] & 0x80) {
            continue;             // already a response, ignore
        }

        // Walk the first question's QNAME to find where it ends.
        int p = 12;
        while (p < n && buf[p] != 0) {
            p += buf[p] + 1;      // skip a length-prefixed label
            if (p >= DNS_MAX_LEN) {
                break;
            }
        }
        if (p + 5 > n) {
            continue;             // malformed: no room for null + qtype + qclass
        }
        uint16_t qtype = (buf[p + 1] << 8) | buf[p + 2];
        int qend = p + 1 + 4;     // null byte + QTYPE(2) + QCLASS(2)

        // Turn the query into a response in place.
        buf[2] |= 0x80;           // QR = 1 (response), preserve opcode/RD
        buf[3] = 0x80;            // RA = 1, RCODE = 0
        bool is_a = (qtype == 1); // only A records get an answer
        buf[6] = 0; buf[7] = is_a ? 1 : 0;          // ANCOUNT
        buf[8] = buf[9] = buf[10] = buf[11] = 0;    // NSCOUNT + ARCOUNT

        int len = qend;
        if (is_a && qend + 16 <= DNS_MAX_LEN) {
            const uint8_t ans[16] = {
                0xC0, 0x0C,                  // NAME -> pointer to offset 12
                0x00, 0x01,                  // TYPE  A
                0x00, 0x01,                  // CLASS IN
                0x00, 0x00, 0x00, 0x3C,      // TTL   60 s
                0x00, 0x04,                  // RDLENGTH 4
                AP_IP[0], AP_IP[1], AP_IP[2], AP_IP[3],
            };
            memcpy(buf + qend, ans, sizeof(ans));
            len = qend + sizeof(ans);
        }
        sendto(sock, buf, len, 0, (struct sockaddr *)&src, slen);
    }

    ESP_LOGI(TAG, "captive DNS stopped");
    close(sock);
    s_task = NULL;
    vTaskDelete(NULL);
}

void dns_server_start(void)
{
    s_run = true;       // set first: keeps a still-draining task alive
    if (s_task) {
        return;
    }
    xTaskCreate(dns_task, "captive_dns", 3072, NULL, 5, &s_task);
}

void dns_server_stop(void)
{
    s_run = false;  // task exits on its next recv timeout and self-deletes
}
