#include "app_dns.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#define DNS_PORT   (53)
#define DNS_MAX    (512)
#define DNS_TTL    (60)
#define DNS_STACK  (4096)
#define DNS_PRIO   (4)

static const char *TAG = "dns";

static bool s_started;
static uint32_t s_reply_ip;

static int dns_question_end(const uint8_t *buf, int len)
{
    int pos = 12;
    int guard = 0;

    while (pos < len && guard++ < 256) {
        uint8_t label = buf[pos];

        if (label == 0) {
            return pos + 5;
        }
        if ((label & 0xC0) == 0xC0) {
            return pos + 6;
        }
        pos += (int)label + 1;
    }
    return -1;
}

static void dns_task(void *arg)
{
    int sock;
    struct sockaddr_in addr;
    uint8_t query[DNS_MAX];
    uint8_t reply[DNS_MAX + 32];

    (void)arg;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket failed");
        vTaskDelete(NULL);
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DNS_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "listening on %u, answer %08x", (unsigned)DNS_PORT, (unsigned)s_reply_ip);

    for (;;) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, query, sizeof(query), 0, (struct sockaddr *)&from, &from_len);
        int qend;
        size_t out_len;

        if (len < 16) {
            continue;
        }
        if ((query[2] & 0x80) != 0) {
            continue;
        }
        qend = dns_question_end(query, len);
        if (qend <= 0 || qend > len) {
            continue;
        }

        memcpy(reply, query, (size_t)qend);
        reply[2] = 0x81;
        reply[3] = 0x80;
        reply[6] = 0x00;
        reply[7] = 0x01;
        reply[8] = 0x00;
        reply[9] = 0x00;
        reply[10] = 0x00;
        reply[11] = 0x00;
        out_len = (size_t)qend;

        reply[out_len++] = 0xC0;
        reply[out_len++] = 0x0C;
        reply[out_len++] = 0x00;
        reply[out_len++] = 0x01;
        reply[out_len++] = 0x00;
        reply[out_len++] = 0x01;
        reply[out_len++] = 0x00;
        reply[out_len++] = 0x00;
        reply[out_len++] = 0x00;
        reply[out_len++] = DNS_TTL;
        reply[out_len++] = 0x00;
        reply[out_len++] = 0x04;
        memcpy(reply + out_len, &s_reply_ip, 4);
        out_len += 4;

        sendto(sock, reply, out_len, 0, (struct sockaddr *)&from, from_len);
    }
}

esp_err_t app_dns_start(void)
{
    esp_netif_t *netif;
    esp_netif_ip_info_t info;

    if (s_started) {
        return ESP_OK;
    }

    s_reply_ip = htonl(0xC0A80401);
    netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (netif != NULL && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0) {
        s_reply_ip = info.ip.addr;
    }

    if (xTaskCreate(dns_task, "dns", DNS_STACK, NULL, DNS_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}
