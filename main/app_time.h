#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t app_time_init(void);
void app_time_start_sntp(void);
void app_time_set_unix(uint32_t seconds);
bool app_time_valid(void);
uint32_t app_time_now(void);
int app_time_hour(void);
void app_time_format(char *buffer, size_t size, const char *fmt);
