#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t app_httpd_start(void);
bool app_httpd_running(void);
