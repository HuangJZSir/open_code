#include "app_time.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"

#define APP_TIME_MIN_VALID 1700000000U

static const char *TAG = "time";

static bool s_sntp_started;

esp_err_t app_time_init(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    return ESP_OK;
}

static void time_sync_cb(struct timeval *tv)
{
    struct tm tm_now;

    if (tv == NULL || localtime_r(&tv->tv_sec, &tm_now) == NULL) {
        return;
    }
    ESP_LOGI(TAG, "sntp synced %04d-%02d-%02d %02d:%02d:%02d", tm_now.tm_year + 1900,
             tm_now.tm_mon + 1, tm_now.tm_mday, tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
}

void app_time_start_sntp(void)
{
    if (s_sntp_started) {
        if (!app_time_valid()) {
            esp_sntp_restart();
            ESP_LOGI(TAG, "sntp restart");
        }
        return;
    }

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();
    s_sntp_started = true;
    ESP_LOGI(TAG, "sntp started");
}

void app_time_set_unix(uint32_t seconds)
{
    struct timeval tv;
    uint32_t cur;

    if (seconds < APP_TIME_MIN_VALID) {
        return;
    }
    cur = (uint32_t)time(NULL);
    if (cur >= APP_TIME_MIN_VALID) {
        uint32_t diff = (cur > seconds) ? (cur - seconds) : (seconds - cur);

        if (diff < 5) {
            return;
        }
    }
    tv.tv_sec = (time_t)seconds;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "time set from client %u", (unsigned)seconds);
}

bool app_time_valid(void)
{
    return ((uint32_t)time(NULL)) >= APP_TIME_MIN_VALID;
}

uint32_t app_time_now(void)
{
    time_t now = time(NULL);

    if ((uint32_t)now < APP_TIME_MIN_VALID) {
        return 0;
    }
    return (uint32_t)now;
}

int app_time_hour(void)
{
    time_t now = time(NULL);
    struct tm tm_now;

    if (localtime_r(&now, &tm_now) == NULL) {
        return -1;
    }
    return tm_now.tm_hour;
}

void app_time_format(char *buffer, size_t size, const char *fmt)
{
    time_t now;
    struct tm tm_now;

    if (buffer == NULL || size == 0) {
        return;
    }
    buffer[0] = '\0';

    now = time(NULL);
    if (localtime_r(&now, &tm_now) == NULL) {
        return;
    }
    if ((uint32_t)now < APP_TIME_MIN_VALID) {
        return;
    }
    strftime(buffer, size, fmt, &tm_now);
}
