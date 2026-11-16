#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t app_codec_info(const char *path, uint16_t *width, uint16_t *height);
esp_err_t app_codec_decode(const char *path, uint16_t *dst, int dst_w, int dst_h, uint16_t bg);
esp_err_t app_codec_thumb(const uint16_t *src, int src_w, int src_h, uint16_t *dst, int dst_w, int dst_h);
esp_err_t app_codec_read565(const char *path, uint16_t *dst, int width, int height);
esp_err_t app_codec_write565(const char *path, const uint16_t *src, int width, int height);
void app_codec_fill(uint16_t *dst, int width, int height, uint16_t color);
bool app_codec_supported(const char *name);
void app_codec_log_file(const char *path);
