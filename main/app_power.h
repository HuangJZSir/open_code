#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

void app_power_init(void);
bool app_power_valid(void);
int app_power_percent(void);
int app_power_mv(void);
int app_power_ma(void);
void app_power_update(void);
