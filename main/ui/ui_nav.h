#pragma once

#include <stdbool.h>

#include "lvgl.h"

typedef enum {
    UI_SCREEN_HOME = 0,
    UI_SCREEN_MUSIC,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_COUNT,
} ui_screen_t;

void ui_nav_init(lv_obj_t *parent);
void ui_nav_show(ui_screen_t screen);
void ui_nav_back(void);
void ui_nav_info(const char *title, const char *text);
bool ui_nav_has_info(void);
ui_screen_t ui_nav_current(void);
bool ui_nav_is_home(void);
void ui_nav_toast(const char *text);
void ui_nav_pump(void);
