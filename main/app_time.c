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

void app_time_start_sntp(void)
{
    if (s_sntp_started) {
        return;
    }

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "cn.pool.ntp.org");
    esp_sntp_init();
    s_sntp_started = true;
    ESP_LOGI(TAG, "sntp started");
}

void app_time_set_unix(uint32_t seconds)
{
    struct timeval tv;

    if (seconds < APP_TIME_MIN_VALID) {
        return;
    }
    if (app_time_valid() && (uint32_t)time(NULL) >= seconds) {
        return;
    }
    tv.tv_sec = (time_t)seconds;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
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
