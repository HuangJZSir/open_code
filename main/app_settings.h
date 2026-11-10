#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint8_t brightness;
    uint8_t volume;
    bool clock_on;
    bool repeat;
    bool shuffle;
    uint8_t night_start;
    uint8_t night_end;
    char sta_ssid[33];
    char sta_pass[65];
    char ap_ssid[33];
    char ap_pass[33];
} app_settings_t;

esp_err_t app_settings_init(void);
app_settings_t *app_settings_get(void);
esp_err_t app_settings_save(void);

void app_settings_set_brightness(uint8_t percent);
void app_settings_set_volume(uint8_t percent);
void app_settings_set_clock(bool on);
void app_settings_set_repeat(bool on);
void app_settings_set_shuffle(bool on);
void app_settings_set_night(uint8_t start_hour, uint8_t end_hour);
void app_settings_apply_brightness(void);
bool app_settings_night_window(int hour);
