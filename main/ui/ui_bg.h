#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#define UI_BG_W (800)
#define UI_BG_H (480)

uint16_t *ui_bg_scratch(void);
bool ui_bg_exists(void);
bool ui_bg_needs_load(void);
esp_err_t ui_bg_load(void);
esp_err_t ui_bg_save_scratch(void);
esp_err_t ui_bg_reserve(void);
esp_err_t ui_bg_clear(void);
void ui_bg_clobbered(void);
void ui_bg_release(void);
void ui_bg_add_release_cb(void (*cb)(void));
const lv_img_dsc_t *ui_bg_dsc(void);
