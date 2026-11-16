#include "app_alarm.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "alarm";

static app_alarm_t s_alarm = {
    .on = false,
    .hour = 7,
    .min = 0,
    .track = "",
};

void app_alarm_init(void)
{
    nvs_handle_t h;
    uint8_t v8 = 0;
    size_t len = sizeof(s_alarm.track);

    if (nvs_open("alarm", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    if (nvs_get_u8(h, "on", &v8) == ESP_OK) {
        s_alarm.on = (v8 != 0);
    }
    if (nvs_get_u8(h, "h", &v8) == ESP_OK && v8 < 24) {
        s_alarm.hour = v8;
    }
    if (nvs_get_u8(h, "m", &v8) == ESP_OK && v8 < 60) {
        s_alarm.min = v8;
    }
    if (nvs_get_str(h, "track", s_alarm.track, &len) != ESP_OK) {
        s_alarm.track[0] = '\0';
    }
    nvs_close(h);
    ESP_LOGI(TAG, "loaded %02d:%02d on=%d ring=%s", s_alarm.hour, s_alarm.min, (int)s_alarm.on,
             (s_alarm.track[0] != '\0') ? s_alarm.track : "default");
}

void app_alarm_save(void)
{
    nvs_handle_t h;

    if (nvs_open("alarm", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "on", (uint8_t)(s_alarm.on ? 1 : 0));
    nvs_set_u8(h, "h", (uint8_t)s_alarm.hour);
    nvs_set_u8(h, "m", (uint8_t)s_alarm.min);
    nvs_set_str(h, "track", s_alarm.track);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "saved %02d:%02d on=%d ring=%s", s_alarm.hour, s_alarm.min, (int)s_alarm.on,
             (s_alarm.track[0] != '\0') ? s_alarm.track : "default");
}

app_alarm_t *app_alarm_get(void)
{
    return &s_alarm;
}

void app_alarm_set_time(int hour, int min)
{
    while (hour >= 24) {
        hour -= 24;
    }
    while (hour < 0) {
        hour += 24;
    }
    while (min >= 60) {
        min -= 60;
    }
    while (min < 0) {
        min += 60;
    }
    s_alarm.hour = hour;
    s_alarm.min = min;
}

void app_alarm_set_on(bool on)
{
    s_alarm.on = on;
    app_alarm_save();
}

void app_alarm_set_track(const char *name)
{
    if (name == NULL) {
        s_alarm.track[0] = '\0';
    } else {
        snprintf(s_alarm.track, sizeof(s_alarm.track), "%s", name);
    }
    app_alarm_save();
}
