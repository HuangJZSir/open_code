#include "ui_nav.h"

#include <stdbool.h>
#include <stddef.h>

#include "app_bus.h"
#include "icons.h"
#include "launcher.h"
#include "ui_music.h"
#include "ui_settings.h"
#include "ui_theme.h"

static lv_obj_t *s_parent;
static lv_obj_t *s_screens[UI_SCREEN_COUNT];
static ui_screen_t s_current = UI_SCREEN_HOME;
static lv_obj_t *s_info;
static lv_obj_t *s_toast;
static lv_obj_t *s_toast_label;
static lv_timer_t *s_toast_timer;

static void toast_hide(lv_timer_t *timer)
{
    (void)timer;

    if (s_toast != NULL) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    }
}

static void info_close_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

void ui_nav_info(const char *title, const char *text)
{
    lv_obj_t *label;

    if (s_info != NULL) {
        lv_obj_del(s_info);
        s_info = NULL;
    }

    s_info = lv_obj_create(s_parent);
    lv_obj_set_size(s_info, UI_W, UI_H);
    lv_obj_set_pos(s_info, 0, 0);
    lv_obj_set_style_bg_color(s_info, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_info, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_info, 0, 0);
    lv_obj_set_style_radius(s_info, 0, 0);
    lv_obj_set_style_pad_all(s_info, 0, 0);
    lv_obj_clear_flag(s_info, LV_OBJ_FLAG_SCROLLABLE);

    ui_button(s_info, "返回", 16, 12, 88, 44, info_close_cb, NULL);

    label = ui_label(s_info, (title != NULL) ? title : "", UI_FONT_BIG, UI_COLOR_TEXT);
    lv_obj_set_pos(label, 124, 16);

    label = ui_label(s_info, (text != NULL) ? text : "", UI_FONT_TEXT, UI_COLOR_MUTED);
    lv_obj_set_width(label, UI_W - 200);
    lv_obj_set_pos(label, 124, 96);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);

    lv_obj_move_foreground(s_info);
}

bool ui_nav_has_info(void)
{
    return s_info != NULL;
}

void ui_nav_init(lv_obj_t *parent)
{
    s_parent = parent;

    ui_theme_init();

    s_screens[UI_SCREEN_HOME] = launcher_create(parent);
    s_current = UI_SCREEN_HOME;

    s_toast = lv_obj_create(parent);
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, 56);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_80, 0);
    lv_obj_set_style_radius(s_toast, 12, 0);
    lv_obj_set_style_border_width(s_toast, 0, 0);
    lv_obj_set_style_pad_hor(s_toast, 22, 0);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -120);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    s_toast_label = ui_label(s_toast, "", UI_FONT_TEXT, UI_COLOR_TEXT);
    lv_obj_center(s_toast_label);

    s_toast_timer = lv_timer_create(toast_hide, 2000, NULL);
}

void ui_nav_toast(const char *text)
{
    if (s_toast == NULL) {
        return;
    }

    ui_set_text(s_toast_label, text);
    lv_obj_center(s_toast_label);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_toast);

    if (s_toast_timer != NULL) {
        lv_timer_reset(s_toast_timer);
    }
}

void ui_nav_show(ui_screen_t screen)
{
    int i;

    if (screen >= UI_SCREEN_COUNT) {
        return;
    }

    if (s_info != NULL) {
        lv_obj_del(s_info);
        s_info = NULL;
    }

    if (s_screens[screen] == NULL) {
        if (screen == UI_SCREEN_MUSIC) {
            s_screens[screen] = ui_music_create(s_parent);
        } else if (screen == UI_SCREEN_SETTINGS) {
            s_screens[screen] = ui_settings_create(s_parent);
        }
        if (s_screens[screen] == NULL) {
            return;
        }
    }

    for (i = 0; i < UI_SCREEN_COUNT; i++) {
        if (s_screens[i] == NULL) {
            continue;
        }
        if (i == (int)screen) {
            lv_obj_clear_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_screens[i]);
        } else {
            lv_obj_add_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    s_current = screen;

    if (screen == UI_SCREEN_MUSIC) {
        ui_music_enter();
    } else if (screen == UI_SCREEN_SETTINGS) {
        ui_settings_enter();
    } else {
        launcher_refresh();
    }

    if (s_toast != NULL) {
        lv_obj_move_foreground(s_toast);
    }
}

void ui_nav_back(void)
{
    if (s_info != NULL) {
        lv_obj_del(s_info);
        s_info = NULL;
        return;
    }
    if (s_current != UI_SCREEN_HOME) {
        ui_nav_show(UI_SCREEN_HOME);
    }
}

ui_screen_t ui_nav_current(void)
{
    return s_current;
}

bool ui_nav_is_home(void)
{
    return s_current == UI_SCREEN_HOME && s_info == NULL;
}

void ui_nav_pump(void)
{
    app_evt_t evt;

    while (app_bus_pop(&evt)) {
        if (evt.type == APP_EVT_CLIENT_CHANGED) {
            ui_nav_toast("手机已连接");
        }
    }
}
