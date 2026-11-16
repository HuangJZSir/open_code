#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define APP_CAST_ENABLE  0

esp_err_t app_cast_init(void);
bool app_cast_active(void);
void app_cast_stop(void);
uint8_t *app_cast_alloc_frame(size_t *capacity);
esp_err_t app_cast_submit_frame(size_t length);
