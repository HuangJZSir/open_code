#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define APP_STORE_NAME_LEN 24

esp_err_t app_store_init(void);
bool app_store_ready(void);
bool app_store_on_sd(void);
const char *app_store_mount(void);

uint32_t app_store_free_kb(void);
uint32_t app_store_total_kb(void);
uint32_t app_store_used_kb(void);

void app_store_lock(void);
void app_store_unlock(void);
