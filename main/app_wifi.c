#include "app_wifi.h"

#include <stdio.h>
#include <string.h>

#include "app_settings.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"

#define APP_WIFI_AP_URL "http://192.168.4.1/"
#define APP_WIFI_AP_CHANNEL 6

static const char *TAG = "wifi";

static bool s_started;
static bool s_connected;
static bool s_connecting;
static bool s_want_sta;
static bool s_has_ip;
static char s_ip[16];
static char s_ap_ssid[33];
static char s_ap_pass[33];
static int s_clients;
static esp_timer_handle_t s_timer;
static app_wifi_client_cb_t s_client_cb;

static void wifi_client_notify(bool connected)
{
    if (s_client_cb != NULL) {
        s_client_cb(connected);
    }
}

static void wifi_sta_start_connect(void)
{
    wifi_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%.*s",
             (int)sizeof(cfg.sta.ssid) - 1, app_settings_get()->sta_ssid);
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%.*s",
             (int)sizeof(cfg.sta.password) - 1, app_settings_get()->sta_pass);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;

    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) {
        ESP_LOGW(TAG, "sta config failed");
        return;
    }
    s_want_sta = true;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        s_connected = true;
        s_connecting = false;
        ESP_LOGI(TAG, "sta connected");
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_connecting = false;
        s_has_ip = false;
        s_ip[0] = '\0';
        ESP_LOGI(TAG, "sta disconnected");
        return;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;

        s_has_ip = true;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        s_connected = true;
        s_connecting = false;
        ESP_LOGI(TAG, "sta ip %s", s_ip);
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        s_clients++;
        ESP_LOGI(TAG, "client joined, total %d", s_clients);
        wifi_client_notify(true);
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        if (s_clients > 0) {
            s_clients--;
        }
        ESP_LOGI(TAG, "client left, total %d", s_clients);
        wifi_client_notify(s_clients > 0);
        return;
    }
}

static void wifi_timer_cb(void *arg)
{
    (void)arg;

    if (!s_started || !s_want_sta || s_connected || s_connecting) {
        return;
    }
    s_connecting = true;
    if (esp_wifi_connect() != ESP_OK) {
        s_connecting = false;
    }
}

esp_err_t app_wifi_init(void)
{
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t ap_cfg;
    uint8_t mac[6];
    app_settings_t *cfg = app_settings_get();
    esp_err_t err;

    if (s_started) {
        return ESP_OK;
    }

    err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (esp_netif_create_default_wifi_ap() == NULL) {
        return ESP_FAIL;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }

    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        return err;
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL);

    if (esp_wifi_get_mac(WIFI_IF_AP, mac) == ESP_OK &&
        strcmp(cfg->ap_ssid, "MusicBox") == 0) {
        snprintf(cfg->ap_ssid, sizeof(cfg->ap_ssid), "MusicBox-%02X%02X", mac[4], mac[5]);
        app_settings_save();
    }
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", cfg->ap_ssid);
    snprintf(s_ap_pass, sizeof(s_ap_pass), "%s", cfg->ap_pass);

    memset(&ap_cfg, 0, sizeof(ap_cfg));
    snprintf((char *)ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid), "%.*s",
             (int)sizeof(ap_cfg.ap.ssid) - 1, s_ap_ssid);
    snprintf((char *)ap_cfg.ap.password, sizeof(ap_cfg.ap.password), "%.*s",
             (int)sizeof(ap_cfg.ap.password) - 1, s_ap_pass);
    ap_cfg.ap.ssid_len = (uint8_t)strlen(s_ap_ssid);
    ap_cfg.ap.channel = APP_WIFI_AP_CHANNEL;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.pmf_cfg.required = false;

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    s_started = true;
    ESP_LOGI(TAG, "ap ssid %s pass %s url %s", s_ap_ssid, s_ap_pass, APP_WIFI_AP_URL);

    if (cfg->sta_ssid[0] != '\0') {
        app_wifi_sta_apply();
    }

    {
        esp_timer_create_args_t args = {
            .callback = wifi_timer_cb,
            .name = "wifi_retry",
        };
        if (esp_timer_create(&args, &s_timer) == ESP_OK) {
            esp_timer_start_periodic(s_timer, 2000000);
        }
    }

    return ESP_OK;
}

esp_err_t app_wifi_sta_apply(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (app_settings_get()->sta_ssid[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    wifi_sta_start_connect();
    s_connecting = true;
    if (esp_wifi_connect() != ESP_OK) {
        s_connecting = false;
    }
    return ESP_OK;
}

esp_err_t app_wifi_sta_set(const char *ssid, const char *password)
{
    app_settings_t *cfg = app_settings_get();

    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(cfg->sta_ssid, sizeof(cfg->sta_ssid), "%s", ssid);
    snprintf(cfg->sta_pass, sizeof(cfg->sta_pass), "%s", (password != NULL) ? password : "");
    app_settings_save();

    return app_wifi_sta_apply();
}

bool app_wifi_sta_connected(void)
{
    return s_connected;
}

bool app_wifi_sta_has_ip(void)
{
    return s_has_ip;
}

bool app_wifi_get_ip(char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return false;
    }
    buffer[0] = '\0';
    if (!s_has_ip) {
        return false;
    }
    snprintf(buffer, size, "%s", s_ip);
    return true;
}

bool app_wifi_get_url(char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return false;
    }
    if (s_has_ip) {
        snprintf(buffer, size, "http://%s/", s_ip);
        return true;
    }
    snprintf(buffer, size, "%s", APP_WIFI_AP_URL);
    return false;
}

const char *app_wifi_ap_ssid(void)
{
    return s_ap_ssid;
}

const char *app_wifi_ap_pass(void)
{
    return s_ap_pass;
}

const char *app_wifi_ap_url(void)
{
    return APP_WIFI_AP_URL;
}

int app_wifi_client_count(void)
{
    return s_clients;
}

void app_wifi_set_client_cb(app_wifi_client_cb_t cb)
{
    s_client_cb = cb;
}
