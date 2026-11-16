#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool valid;
    int temp;
    int feels;
    int humidity;
    int pressure;
    int precip;
    int wind_speed;
    char text[32];
    char icon[8];
    char wind[32];
    bool sun_valid;
    int sunrise_h;
    int sunrise_m;
    int sunset_h;
    int sunset_m;
} app_weather_t;

void app_weather_start(void);
bool app_weather_valid(void);
bool app_weather_stale(void);
const app_weather_t *app_weather_get(void);
esp_err_t app_weather_fetch(void);
