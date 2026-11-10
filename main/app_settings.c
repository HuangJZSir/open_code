#include "app_settings.h"

#include <stdio.h>
#include <string.h>

#include "bsp_backlight.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define APP_SETTINGS_NS "album"
#define APP_SETTINGS_KEY "cfg"

static const char *TAG = "settings";

static app_settings_t s_cfg;
static nvs_handle_t s_nvs;
static bool s_ready;

static void settings_defaults(app_settings_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->brightness = 100;
    cfg->volume = 70;
    cfg->clock_on = true;
    cfg->repeat = true;
    cfg->shuffle = false;
    cfg->night_start = 0xFF;
    cfg->night_end = 0xFF;
    snprintf(cfg->ap_ssid, sizeof(cfg->ap_ssid), "MusicBox");
    snprintf(cfg->ap_pass, sizeof(cfg->ap_pass), "12345678");
}

static void settings_sanitize(app_settings_t *cfg)
{
    if (cfg->brightness < 10) {
        cfg->brightness = 10;
    }
    if (cfg->brightness > 100) {
        cfg->brightness = 100;
    }
    if (cfg->volume > 100) {
        cfg->volume = 100;
    }
    if (cfg->night_start > 23) {
        cfg->night_start = 0xFF;
    }
    if (cfg->night_end > 23) {
        cfg->night_end = 0xFF;
    }
    if (cfg->ap_ssid[0] == '\0') {
        snprintf(cfg->ap_ssid, sizeof(cfg->ap_ssid), "MusicBox");
    }
    if (cfg->ap_pass[0] == '\0' || strlen(cfg->ap_pass) < 8) {
        snprintf(cfg->ap_pass, sizeof(cfg->ap_pass), "12345678");
    }
    cfg->sta_ssid[sizeof(cfg->sta_ssid) - 1] = '\0';
    cfg->sta_pass[sizeof(cfg->sta_pass) - 1] = '\0';
    cfg->ap_ssid[sizeof(cfg->ap_ssid) - 1] = '\0';
    cfg->ap_pass[sizeof(cfg->ap_pass) - 1] = '\0';
}

esp_err_t app_settings_init(void)
{
    esp_err_t err;
    size_t len = sizeof(s_cfg);

    if (s_ready) {
        return ESP_OK;
    }

    settings_defaults(&s_cfg);

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        s_ready = true;
        return err;
    }

    err = nvs_open(APP_SETTINGS_NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        s_ready = true;
        return err;
    }

    if (nvs_get_blob(s_nvs, APP_SETTINGS_KEY, &s_cfg, &len) != ESP_OK || len != sizeof(s_cfg)) {
        settings_defaults(&s_cfg);
        app_settings_save();
    }

    settings_sanitize(&s_cfg);
    s_ready = true;
    app_settings_apply_brightness();
    return ESP_OK;
}

app_settings_t *app_settings_get(void)
{
    return &s_cfg;
}

esp_err_t app_settings_save(void)
{
    esp_err_t err;

    if (s_nvs == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    settings_sanitize(&s_cfg);
    err = nvs_set_blob(s_nvs, APP_SETTINGS_KEY, &s_cfg, sizeof(s_cfg));
    if (err != ESP_OK) {
        return err;
    }
    return nvs_commit(s_nvs);
}

void app_settings_apply_brightness(void)
{
    bsp_backlight_set_percent(s_cfg.brightness);
}

void app_settings_set_brightness(uint8_t percent)
{
    if (percent < 10) {
        percent = 10;
    }
    if (percent > 100) {
        percent = 100;
    }
    s_cfg.brightness = percent;
    bsp_backlight_set_percent(percent);
    app_settings_save();
}

void app_settings_set_volume(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_cfg.volume = percent;
    app_settings_save();
}

void app_settings_set_clock(bool on)
{
    s_cfg.clock_on = on;
    app_settings_save();
}

void app_settings_set_repeat(bool on)
{
    s_cfg.repeat = on;
    app_settings_save();
}

void app_settings_set_shuffle(bool on)
{
    s_cfg.shuffle = on;
    app_settings_save();
}

void app_settings_set_night(uint8_t start_hour, uint8_t end_hour)
{
    s_cfg.night_start = (start_hour > 23) ? 0xFF : start_hour;
    s_cfg.night_end = (end_hour > 23) ? 0xFF : end_hour;
    app_settings_save();
}

bool app_settings_night_window(int hour)
{
    if (hour < 0 || hour > 23) {
        return false;
    }
    if (s_cfg.night_start > 23 || s_cfg.night_end > 23) {
        return false;
    }
    if (s_cfg.night_start == s_cfg.night_end) {
        return false;
    }
    if (s_cfg.night_start < s_cfg.night_end) {
        return hour >= s_cfg.night_start && hour < s_cfg.night_end;
    }
    return hour >= s_cfg.night_start || hour < s_cfg.night_end;
}
