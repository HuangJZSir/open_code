#pragma once

#include "lvgl.h"

lv_obj_t *launcher_create(lv_obj_t *parent);
void launcher_refresh(void);
void launcher_show_apps(void);
void launcher_show_page(int index);
