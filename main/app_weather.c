#include "app_weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_time.h"
#include "app_wifi.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "miniz.h"
#include <sys/socket.h>
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern esp_err_t esp_crt_bundle_attach(void *conf);

#define WEATHER_BUF     (4096)
#define WEATHER_TIMEOUT (15000)
#define WEATHER_POLL_MS (60000)

#ifndef CONFIG_APP_WEATHER_INTERVAL_MIN
#define CONFIG_APP_WEATHER_INTERVAL_MIN 30
#endif
#ifndef CONFIG_APP_WEATHER_KEY
#define CONFIG_APP_WEATHER_KEY ""
#endif
#ifndef CONFIG_APP_WEATHER_HOST
#define CONFIG_APP_WEATHER_HOST "devapi.qweather.com"
#endif
#ifndef CONFIG_APP_WEATHER_LOCATION
#define CONFIG_APP_WEATHER_LOCATION "101010100"
#endif

static const char *TAG = "weather";

static app_weather_t s_weather;
static int64_t s_last_us;
static char s_buf[WEATHER_BUF];
static char s_raw[WEATHER_BUF];
static int s_len;
static bool s_busy;

static int weather_gunzip(const unsigned char *in, int in_len, char *out, int out_size)
{
    int off;
    unsigned char flg;
    size_t got;

    if (in_len < 18 || in[0] != 0x1F || in[1] != 0x8B || in[2] != 0x08) {
        return -1;
    }
    flg = in[3];
    off = 10;
    if (flg & 0x04) {
        int xlen;

        if (off + 2 > in_len) {
            return -1;
        }
        xlen = in[off] | ((int)in[off + 1] << 8);
        off += 2 + xlen;
    }
    if (flg & 0x08) {
        while (off < in_len && in[off] != 0) {
            off++;
        }
        off++;
    }
    if (flg & 0x10) {
        while (off < in_len && in[off] != 0) {
            off++;
        }
        off++;
    }
    if (flg & 0x02) {
        off += 2;
    }
    if (off >= in_len) {
        return -1;
    }

    {
        tinfl_decompressor *dec;
        size_t in_used = (size_t)(in_len - off);
        size_t out_used = (size_t)out_size - 1;
        tinfl_status st;

        dec = heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (dec == NULL) {
            return -1;
        }
        tinfl_init(dec);
        st = tinfl_decompress(dec, (const mz_uint8 *)(in + off), &in_used, (mz_uint8 *)out,
                              (mz_uint8 *)out, &out_used, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        heap_caps_free(dec);
        if (st != TINFL_STATUS_DONE) {
            ESP_LOGW(TAG, "inflate status %d", (int)st);
            return -1;
        }
        got = out_used;
    }
    out[got] = '\0';
    return (int)got;
}

static esp_err_t weather_http_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != NULL && evt->data_len > 0) {
        int room = WEATHER_BUF - 1 - s_len;

        if (evt->data_len <= room) {
            memcpy(s_raw + s_len, evt->data, (size_t)evt->data_len);
            s_len += evt->data_len;
            s_raw[s_len] = '\0';
        }
    }
    return ESP_OK;
}

bool app_weather_valid(void)
{
    return s_weather.valid;
}

bool app_weather_stale(void)
{
    int64_t limit = (int64_t)CONFIG_APP_WEATHER_INTERVAL_MIN * 60 * 1000000;

    if (!s_weather.valid) {
        return true;
    }
    return (esp_timer_get_time() - s_last_us) > limit;
}

const app_weather_t *app_weather_get(void)
{
    return &s_weather;
}

static void weather_parse(void)
{
    cJSON *root;
    cJSON *now;
    cJSON *item;
    const char *body = s_buf;

    while (*body == ' ' || *body == '\t' || *body == '\r' || *body == '\n') {
        body++;
    }
    if ((unsigned char)body[0] == 0xEF && (unsigned char)body[1] == 0xBB) {
        body += 3;
    }

    root = cJSON_Parse(body);
    if (root == NULL) {
        ESP_LOGW(TAG, "json parse failed, len=%d head=%02x %02x %02x %02x %02x %02x %02x %02x", s_len,
                 (unsigned char)s_buf[0], (unsigned char)s_buf[1], (unsigned char)s_buf[2],
                 (unsigned char)s_buf[3], (unsigned char)s_buf[4], (unsigned char)s_buf[5],
                 (unsigned char)s_buf[6], (unsigned char)s_buf[7]);
        ESP_LOGW(TAG, "body: %.160s", s_buf);
        return;
    }

    item = cJSON_GetObjectItem(root, "code");
    if (item == NULL || item->valuestring == NULL || strcmp(item->valuestring, "200") != 0) {
        ESP_LOGW(TAG, "api code %s", (item != NULL && item->valuestring != NULL) ? item->valuestring : "?");
        cJSON_Delete(root);
        return;
    }

    now = cJSON_GetObjectItem(root, "now");
    if (now == NULL) {
        ESP_LOGW(TAG, "no now field");
        cJSON_Delete(root);
        return;
    }

    {
        bool keep_sun = s_weather.sun_valid;
        int rise_h = s_weather.sunrise_h;
        int rise_m = s_weather.sunrise_m;
        int set_h = s_weather.sunset_h;
        int set_m = s_weather.sunset_m;

        memset(&s_weather, 0, sizeof(s_weather));
        s_weather.sun_valid = keep_sun;
        s_weather.sunrise_h = rise_h;
        s_weather.sunrise_m = rise_m;
        s_weather.sunset_h = set_h;
        s_weather.sunset_m = set_m;
    }
    item = cJSON_GetObjectItem(now, "temp");
    if (item != NULL && item->valuestring != NULL) {
        s_weather.temp = atoi(item->valuestring);
    }
    item = cJSON_GetObjectItem(now, "feelsLike");
    if (item != NULL && item->valuestring != NULL) {
        s_weather.feels = atoi(item->valuestring);
    }
    item = cJSON_GetObjectItem(now, "pressure");
    if (item != NULL && item->valuestring != NULL) {
        s_weather.pressure = atoi(item->valuestring);
    }
    item = cJSON_GetObjectItem(now, "precip");
    if (item != NULL && item->valuestring != NULL) {
        s_weather.precip = (int)atof(item->valuestring);
    }
    item = cJSON_GetObjectItem(now, "windSpeed");
    if (item != NULL && item->valuestring != NULL) {
        s_weather.wind_speed = atoi(item->valuestring);
    }
    item = cJSON_GetObjectItem(now, "humidity");
    if (item != NULL && item->valuestring != NULL) {
        s_weather.humidity = atoi(item->valuestring);
    }
    item = cJSON_GetObjectItem(now, "text");
    if (item != NULL && item->valuestring != NULL) {
        snprintf(s_weather.text, sizeof(s_weather.text), "%s", item->valuestring);
    }
    item = cJSON_GetObjectItem(now, "icon");
    if (item != NULL && item->valuestring != NULL) {
        snprintf(s_weather.icon, sizeof(s_weather.icon), "%s", item->valuestring);
    }
    {
        cJSON *dir = cJSON_GetObjectItem(now, "windDir");
        cJSON *scale = cJSON_GetObjectItem(now, "windScale");

        snprintf(s_weather.wind, sizeof(s_weather.wind), "%s %s级",
                 (dir != NULL && dir->valuestring != NULL) ? dir->valuestring : "",
                 (scale != NULL && scale->valuestring != NULL) ? scale->valuestring : "");
    }

    s_weather.valid = true;
    s_last_us = esp_timer_get_time();
    cJSON_Delete(root);

    ESP_LOGI(TAG, "now %d C %s icon=%s humidity=%d wind=%s", s_weather.temp, s_weather.text,
             s_weather.icon, s_weather.humidity, s_weather.wind);
}

esp_err_t app_weather_fetch(void)
{
    char url[512];
    esp_http_client_config_t cfg;
    esp_http_client_handle_t client;
    esp_err_t err;

    if (CONFIG_APP_WEATHER_KEY[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    if (!app_wifi_sta_has_ip() || s_busy) {
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(url, sizeof(url), "https://%s/v7/weather/now?location=%s&lang=zh&gzip=n&key=%s",
             CONFIG_APP_WEATHER_HOST, CONFIG_APP_WEATHER_LOCATION, CONFIG_APP_WEATHER_KEY);
    ESP_LOGI(TAG, "fetch host=%s location=%s internal=%uKB psram=%uKB", CONFIG_APP_WEATHER_HOST,
             CONFIG_APP_WEATHER_LOCATION,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    {
        struct addrinfo hints;
        struct addrinfo *res = NULL;
        char ip[46] = "?";

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(CONFIG_APP_WEATHER_HOST, "443", &hints, &res) == 0 && res != NULL) {
            struct sockaddr_in *in = (struct sockaddr_in *)res->ai_addr;

            inet_ntoa_r(in->sin_addr, ip, sizeof(ip));
            freeaddrinfo(res);
            ESP_LOGI(TAG, "dns %s -> %s", CONFIG_APP_WEATHER_HOST, ip);
        } else {
            ESP_LOGW(TAG, "dns failed for %s", CONFIG_APP_WEATHER_HOST);
            return ESP_ERR_HTTP_CONNECT;
        }
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.url = url;
    cfg.event_handler = weather_http_cb;
    cfg.timeout_ms = WEATHER_TIMEOUT;
    cfg.buffer_size = 1024;
    cfg.buffer_size_tx = 512;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    s_len = 0;
    s_raw[0] = '\0';
    s_buf[0] = '\0';
    s_busy = true;

    client = esp_http_client_init(&cfg);
    if (client == NULL) {
        s_busy = false;
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Accept", "*/*");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    esp_http_client_set_header(client, "User-Agent", "esp32-s3-musicbox");

    ESP_LOGI(TAG, "heap before perform ok=%d", (int)heap_caps_check_integrity_all(true));
    err = esp_http_client_perform(client);
    ESP_LOGI(TAG, "heap after perform ok=%d", (int)heap_caps_check_integrity_all(true));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "http failed: %s (%s)", esp_err_to_name(err),
                 esp_err_to_name(esp_http_client_get_errno(client)));
        ESP_LOGW(TAG, "如果提示证书校验失败，请在 menuconfig 打开 Component config -> ESP-TLS -> "
                      "Allow potentially insecure options -> Skip server certificate verification");
    } else {
        int status = esp_http_client_get_status_code(client);

        if (status == 200) {
            if (s_len > 2 && (unsigned char)s_raw[0] == 0x1F && (unsigned char)s_raw[1] == 0x8B) {
                int n = weather_gunzip((const unsigned char *)s_raw, s_len, s_buf, WEATHER_BUF);

                if (n < 0) {
                    ESP_LOGW(TAG, "gunzip failed, raw=%d", s_len);
                    s_buf[0] = '\0';
                    s_len = 0;
                } else {
                    ESP_LOGI(TAG, "gunzip %d -> %d bytes", s_len, n);
                    s_len = n;
                }
            } else {
                memcpy(s_buf, s_raw, (size_t)s_len + 1);
            }
            weather_parse();
        } else {
            ESP_LOGW(TAG, "http status %d body=%.120s", status, s_raw);
        }
    }
    ESP_LOGI(TAG, "heap before cleanup ok=%d", (int)heap_caps_check_integrity_all(true));
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "heap after cleanup ok=%d", (int)heap_caps_check_integrity_all(true));
    s_busy = false;
    return err;
}

static void weather_parse_sun(void)
{
    cJSON *root;
    cJSON *item;
    const char *body = s_buf;

    struct {
        int h;
        int m;
    } sun[2];
    int i;
    static const char *const keys[2] = {"sunrise", "sunset"};

    while (*body == ' ' || *body == '\t' || *body == '\r' || *body == '\n') {
        body++;
    }
    root = cJSON_Parse(body);
    if (root == NULL) {
        ESP_LOGW(TAG, "sun parse failed, len=%d", s_len);
        return;
    }
    item = cJSON_GetObjectItem(root, "code");
    if (item == NULL || item->valuestring == NULL || strcmp(item->valuestring, "200") != 0) {
        cJSON_Delete(root);
        return;
    }
    for (i = 0; i < 2; i++) {
        cJSON *v;
        cJSON *daily;

        sun[i].h = 0;
        sun[i].m = 0;
        daily = cJSON_GetObjectItem(root, "daily");
        if (daily != NULL && cJSON_IsArray(daily)) {
            v = cJSON_GetObjectItem(cJSON_GetArrayItem(daily, 0), keys[i]);
        } else {
            v = cJSON_GetObjectItem(root, keys[i]);
        }
        if (v != NULL && v->valuestring != NULL) {
            int hh = 0;
            int mm = 0;

            if (sscanf(v->valuestring, "%d:%d", &hh, &mm) == 2) {
                sun[i].h = hh;
                sun[i].m = mm;
            }
        }
    }
    cJSON_Delete(root);
    s_weather.sunrise_h = sun[0].h;
    s_weather.sunrise_m = sun[0].m;
    s_weather.sunset_h = sun[1].h;
    s_weather.sunset_m = sun[1].m;
    s_weather.sun_valid = (sun[0].h > 0 || sun[1].h > 0);
    ESP_LOGI(TAG, "sun %02d:%02d - %02d:%02d", s_weather.sunrise_h, s_weather.sunrise_m,
             s_weather.sunset_h, s_weather.sunset_m);
}

static esp_err_t weather_fetch_sun(void)
{
    char url[512];
    char date[48];
    esp_http_client_config_t cfg;
    esp_http_client_handle_t client;
    esp_err_t err;
    time_t now;
    struct tm tm_now;

    now = (time_t)app_time_now();
    if (now == 0 || localtime_r(&now, &tm_now) == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    snprintf(date, sizeof(date), "%04d%02d%02d", tm_now.tm_year + 1900, tm_now.tm_mon + 1,
             tm_now.tm_mday);
    snprintf(url, sizeof(url), "https://%s/v7/weather/3d?location=%s&lang=zh&gzip=n&key=%s",
             CONFIG_APP_WEATHER_HOST, CONFIG_APP_WEATHER_LOCATION, CONFIG_APP_WEATHER_KEY);

    memset(&cfg, 0, sizeof(cfg));
    cfg.url = url;
    cfg.event_handler = weather_http_cb;
    cfg.timeout_ms = WEATHER_TIMEOUT;
    cfg.buffer_size = 1024;
    cfg.buffer_size_tx = 512;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    s_len = 0;
    s_raw[0] = '\0';
    s_buf[0] = '\0';

    client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    err = esp_http_client_perform(client);
    if (err == ESP_OK && esp_http_client_get_status_code(client) == 200) {
        if (s_len > 2 && (unsigned char)s_raw[0] == 0x1F && (unsigned char)s_raw[1] == 0x8B) {
            int n = weather_gunzip((const unsigned char *)s_raw, s_len, s_buf, WEATHER_BUF);

            s_len = (n > 0) ? n : 0;
            if (n < 0) {
                s_buf[0] = '\0';
            }
        } else {
            memcpy(s_buf, s_raw, (size_t)s_len + 1);
        }
        weather_parse_sun();
    } else {
        ESP_LOGW(TAG, "sun http failed: %s status=%d", esp_err_to_name(err),
                 esp_http_client_get_status_code(client));
    }
    esp_http_client_cleanup(client);
    return err;
}

static void weather_task(void *arg)
{
    bool warned = false;

    (void)arg;

    if (CONFIG_APP_WEATHER_KEY[0] == '\0') {
        ESP_LOGW(TAG, "no API key, set it in menuconfig -> Weather (QWeather)");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "task started, host=%s location=%s interval=%d min", CONFIG_APP_WEATHER_HOST,
             CONFIG_APP_WEATHER_LOCATION, CONFIG_APP_WEATHER_INTERVAL_MIN);

    for (;;) {
        if (!app_wifi_sta_has_ip()) {
            if (!warned) {
                ESP_LOGI(TAG, "waiting for wifi station ip");
                warned = true;
            }
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        warned = false;

        if (app_weather_stale()) {
            app_weather_fetch();
            ESP_LOGI(TAG, "stack %u bytes left after fetch",
                     (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
        }

        if (!s_weather.sun_valid) {
            ESP_LOGI(TAG, "sun missing, fetching");
            weather_fetch_sun();
            ESP_LOGI(TAG, "stack %u bytes left after sun, valid=%d",
                     (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
                     (int)s_weather.sun_valid);
        }

        vTaskDelay(pdMS_TO_TICKS(s_weather.sun_valid ? WEATHER_POLL_MS : 20000));
    }
}

void app_weather_start(void)
{
    if (xTaskCreate(weather_task, "weather", 10240, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "task create failed, weather disabled");
    }
}
