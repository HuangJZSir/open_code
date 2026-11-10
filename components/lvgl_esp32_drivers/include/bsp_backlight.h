#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t bsp_backlight_init(void);
esp_err_t bsp_backlight_set_percent(uint8_t percent);
uint8_t bsp_backlight_get_percent(void);
void bsp_backlight_on(void);
void bsp_backlight_off(void);
