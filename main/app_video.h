#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t app_video_init(void);
bool app_video_active(void);
void app_video_stop(void);
void app_video_release(void);
int app_video_scan(void);
const char *app_video_name(int index);
const char *app_video_last_error(void);
esp_err_t app_video_play_index(int index);
