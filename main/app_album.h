#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define APP_ALBUM_MAX     (400)
#define APP_ALBUM_NAME    (64)
#define APP_ALBUM_THUMB_W (160)
#define APP_ALBUM_THUMB_H (90)
#define APP_ALBUM_FULL_W  (800)
#define APP_ALBUM_FULL_H  (480)

esp_err_t app_album_init(void);
int app_album_scan(void);
int app_album_count(void);
const char *app_album_name(int index);
bool app_album_path(int index, char *out, size_t size);
esp_err_t app_album_load(int index, uint16_t *dst, int dst_w, int dst_h);
esp_err_t app_album_thumb(int index, uint16_t *dst, int dst_w, int dst_h);
esp_err_t app_album_add(const char *name);
esp_err_t app_album_add_file(const char *tmp_path, const char *name);
esp_err_t app_album_delete(int index);
bool app_album_take_dirty(void);
const char *app_album_dir(void);
