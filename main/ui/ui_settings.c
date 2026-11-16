#include "ui_settings.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_player.h"
#include "app_settings.h"
#include "app_store.h"
#include "app_time.h"
#include "app_wifi.h"
#include "bsp_backlight.h"
#include "esp_heap_caps.h"
#include "launcher.h"
#include "ui_bg.h"
#include "ui_nav.h"
#include "ui_theme.h"

#define C_BG     lv_color_hex(0x0D0F13)
#define C_BG2    lv_color_hex(0x05070A)
#define C_TEXT   lv_color_hex(0xE6E8EE)
#define C_MUTED  lv_color_hex(0x8B93A7)
#define C_ROW    lv_color_hex(0x171A21)
#define C_CARD   lv_color_hex(0x232A36)
#define C_ACCENT lv_color_hex(0x2F7CF6)

#define SET_ROW_H   (62)
#define SET_ROW_GAP (6)
#define SET_HDR_H   (40)
#define SET_LIST_W  (736)
#define SET_BADGE   (38)

static lv_obj_t *s_root;
static lv_obj_t *s_list;
static lv_obj_t *s_wlan_val;
static lv_obj_t *s_pass_val;
static lv_obj_t *s_vol_val;
static lv_obj_t *s_bri_val;
static lv_obj_t *s_night_val;
static lv_obj_t *s_clock_sw;
static lv_obj_t *s_store_val;
static lv_obj_t *s_bg_val;
static lv_obj_t *s_sheet;
static lv_obj_t *s_sheet_title;
static lv_obj_t *s_sheet_val;
static lv_obj_t *s_sheet_slider;
static lv_coord_t s_y;
static int s_sheet_kind;
static bool s_syncing;
static int s_pending;
static lv_timer_t *s_ask_timer;

static void settings_ask_clear_cb(lv_timer_t *timer)
{
    (void)timer;
    s_pending = 0;
}

static const uint8_t s_night_start[] = {0xFF, 22, 23, 1};
static const uint8_t s_night_end[] = {0xFF, 7, 8, 7};
static const char *const s_night_text[] = {"关闭", "22:00 - 07:00", "23:00 - 08:00", "01:00 - 07:00"};

static void settings_back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static lv_obj_t *settings_back_button(lv_obj_t *parent)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_t *label;

    lv_obj_set_pos(btn, 20, 14);
    lv_obj_set_size(btn, 84, 40);
    lv_obj_set_style_bg_color(btn, C_CARD, 0);
    lv_obj_set_style_bg_color(btn, C_MUTED, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, settings_back_cb, LV_EVENT_CLICKED, NULL);

    label = ui_label(btn, "返回", UI_FONT_SMALL, C_TEXT);
    lv_obj_center(label);
    return btn;
}

static void settings_group(lv_obj_t *parent, const char *title)
{
    lv_obj_t *label = ui_label(parent, title, UI_FONT_SMALL, C_MUTED);

    lv_obj_set_pos(label, 8, s_y + 8);
    s_y += SET_HDR_H;
}

static lv_obj_t *settings_row(lv_obj_t *parent, const char *symbol, uint32_t color, const char *title,
                              lv_event_cb_t cb, int id, lv_obj_t **value_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_t *badge;
    lv_obj_t *icon;
    lv_obj_t *label;
    lv_obj_t *arrow;
    lv_obj_t *value;

    lv_obj_set_pos(row, 0, s_y);
    lv_obj_set_size(row, SET_LIST_W, SET_ROW_H);
    lv_obj_set_style_bg_color(row, C_ROW, 0);
    lv_obj_set_style_bg_color(row, C_CARD, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    if (cb != NULL) {
        lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, (void *)(intptr_t)id);
    }

    badge = lv_obj_create(row);
    lv_obj_set_pos(badge, 12, (SET_ROW_H - SET_BADGE) / 2);
    lv_obj_set_size(badge, SET_BADGE, SET_BADGE);
    lv_obj_set_style_radius(badge, 11, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);

    icon = lv_label_create(badge);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(color), 0);
    lv_obj_center(icon);

    label = ui_label(row, title, UI_FONT_TEXT, C_TEXT);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 64, 0);

    arrow = lv_label_create(row);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(arrow, C_MUTED, 0);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -14, 0);

    value = ui_label(row, "", UI_FONT_SMALL, C_MUTED);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, -40, 0);
    if (value_out != NULL) {
        *value_out = value;
    }

    s_y += SET_ROW_H + SET_ROW_GAP;
    return row;
}

static void settings_sheet_close(void)
{
    if (s_sheet != NULL) {
        lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);
    }
}

static void settings_sheet_apply(void)
{
    int32_t v = lv_slider_get_value(s_sheet_slider);

    if (s_sheet_kind == 1) {
        app_settings_set_volume((uint8_t)v);
        app_player_set_volume((uint8_t)v);
    } else if (s_sheet_kind == 2) {
        bsp_backlight_set_percent((uint8_t)v);
        app_settings_set_brightness((uint8_t)v);
    }
}

static void settings_sheet_slider_cb(lv_event_t *e)
{
    int32_t v = lv_slider_get_value(lv_event_get_target(e));
    char buf[16];

    snprintf(buf, sizeof(buf), "%ld", (long)v);
    lv_label_set_text(s_sheet_val, buf);
    settings_sheet_apply();
}

static void settings_sheet_done_cb(lv_event_t *e)
{
    char buf[16];

    (void)e;
    settings_sheet_close();
    if (s_sheet_kind == 1) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)app_settings_get()->volume);
        ui_set_text(s_vol_val, buf);
    } else if (s_sheet_kind == 2) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)app_settings_get()->brightness);
        ui_set_text(s_bri_val, buf);
    }
}

static void settings_sheet_open(int kind, const char *title, int min, int max, int value)
{
    char buf[16];

    if (s_sheet == NULL) {
        return;
    }
    s_sheet_kind = kind;
    ui_set_text(s_sheet_title, title);
    lv_slider_set_range(s_sheet_slider, min, max);
    lv_slider_set_value(s_sheet_slider, value, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d", value);
    lv_label_set_text(s_sheet_val, buf);
    lv_obj_clear_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_sheet);
}

static void settings_row_cb(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    app_settings_t *cfg = app_settings_get();
    char buf[384];

    switch (id) {
    case 1:
        if (app_wifi_sta_has_ip()) {
            char ip[24];

            app_wifi_get_ip(ip, sizeof(ip));
            snprintf(buf, sizeof(buf), "已连接路由器\nIP %s\n热点 %s\n密码 %s\n网址 %s", ip,
                     app_wifi_ap_ssid(), app_wifi_ap_pass(), app_wifi_ap_url());
        } else {
            snprintf(buf, sizeof(buf), "只开了热点，设备没有外网\n热点 %s\n密码 %s\n网址 %s",
                     app_wifi_ap_ssid(), app_wifi_ap_pass(), app_wifi_ap_url());
        }
        ui_nav_info("WLAN", buf);
        break;
    case 2:
        snprintf(buf, sizeof(buf), "热点名称 %s\n热点密码 %s\n浏览器打开 %s", app_wifi_ap_ssid(),
                 app_wifi_ap_pass(), app_wifi_ap_url());
        ui_nav_info("热点", buf);
        break;
    case 3:
        settings_sheet_open(1, "播放音量", 0, 100, cfg->volume);
        break;
    case 4:
        settings_sheet_open(2, "屏幕亮度", 10, 100, cfg->brightness);
        break;
    case 5: {
        size_t i;
        size_t count = sizeof(s_night_start) / sizeof(s_night_start[0]);
        size_t next = 0;

        for (i = 0; i < count; i++) {
            if (s_night_start[i] == cfg->night_start && s_night_end[i] == cfg->night_end) {
                next = (i + 1) % count;
                break;
            }
        }
        app_settings_set_night(s_night_start[next], s_night_end[next]);
        ui_set_text(s_night_val, s_night_text[next]);
        ui_nav_toast(s_night_text[next]);
        break;
    }
    case 7:
        snprintf(buf, sizeof(buf), "已用 %lu MB，剩余 %lu MB\n存储介质 %s\n曲目 %lu 首",
                 (unsigned long)(app_store_used_kb() / 1024),
                 (unsigned long)(app_store_free_kb() / 1024),
                 app_store_on_sd() ? "SD 卡" : "内部存储",
                 (unsigned long)app_player_count());
        ui_nav_info("存储空间", buf);
        break;
    case 8:
        snprintf(buf, sizeof(buf),
                 "音乐播放器 v0.1.0\n屏幕 800x480 RGB565\n音频 ES8311 + I2S 48kHz\n"
                 "解码 mp3 flac wav mp4 mjpeg\nPSRAM 空闲 %u KB\n内部 RAM 空闲 %u KB\n"
                 "存储 %s 剩余 %lu MB\n热点 %s 密码 %s\n网址 %s\n时间 %s",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 app_store_on_sd() ? "SD 卡" : "内部存储",
                 (unsigned long)(app_store_free_kb() / 1024),
                 app_wifi_ap_ssid(), app_wifi_ap_pass(), app_wifi_ap_url(),
                 app_time_valid() ? "已同步" : "未同步");
        ui_nav_info("关于", buf);
        break;
    case 9:
        if (!ui_bg_exists()) {
            ui_nav_toast("当前是纯色背景，去相册选一张图");
            break;
        }
        if (s_pending != 9) {
            s_pending = 9;
            ui_nav_toast("再点一次恢复纯色背景");
            if (s_ask_timer != NULL) {
                lv_timer_reset(s_ask_timer);
            }
            break;
        }
        s_pending = 0;
        ui_bg_clear();
        ui_set_text(s_bg_val, "纯色");
        launcher_refresh();
        ui_nav_toast("已恢复纯色背景");
        break;
    default:
        break;
    }
}

static void settings_clock_cb(lv_event_t *e)
{
    if (s_syncing) {
        return;
    }
    app_settings_set_clock(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

lv_obj_t *ui_settings_create(lv_obj_t *parent)
{
    lv_obj_t *title;
    lv_obj_t *row;

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, UI_W, UI_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, C_BG, 0);
    lv_obj_set_style_bg_grad_color(s_root, C_BG2, 0);
    lv_obj_set_style_bg_grad_dir(s_root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    settings_back_button(s_root);
    title = ui_label(s_root, "设置", UI_FONT_BIG, C_TEXT);
    lv_obj_set_pos(title, 124, 18);

    s_list = lv_obj_create(s_root);
    lv_obj_set_pos(s_list, 32, 68);
    lv_obj_set_size(s_list, SET_LIST_W, UI_H - 76);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_bottom(s_list, 12, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    s_y = 0;

    settings_group(s_list, "无线");
    settings_row(s_list, LV_SYMBOL_WIFI, UI_COLOR_TILE_BLUE, "WLAN", settings_row_cb, 1, &s_wlan_val);
    settings_row(s_list, LV_SYMBOL_EDIT, UI_COLOR_TILE_INDIGO, "热点密码", settings_row_cb, 2, &s_pass_val);

    settings_group(s_list, "媒体");
    settings_row(s_list, LV_SYMBOL_VOLUME_MAX, UI_COLOR_TILE_GREEN, "音量", settings_row_cb, 3, &s_vol_val);
    settings_row(s_list, LV_SYMBOL_EYE_OPEN, UI_COLOR_TILE_AMBER, "亮度", settings_row_cb, 4, &s_bri_val);

    settings_group(s_list, "显示");
    settings_row(s_list, LV_SYMBOL_EYE_CLOSE, UI_COLOR_TILE_PURPLE, "夜间息屏", settings_row_cb, 5,
                 &s_night_val);
    settings_row(s_list, LV_SYMBOL_IMAGE, UI_COLOR_TILE_BLUE, "首页背景", settings_row_cb, 9,
                 &s_bg_val);

    row = settings_row(s_list, LV_SYMBOL_HOME, UI_COLOR_TILE_CYAN, "主页时钟", NULL, 6, NULL);
    s_clock_sw = lv_switch_create(row);
    lv_obj_set_size(s_clock_sw, 56, 30);
    lv_obj_align(s_clock_sw, LV_ALIGN_RIGHT_MID, -44, 0);
    lv_obj_set_style_bg_color(s_clock_sw, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_clock_sw, settings_clock_cb, LV_EVENT_VALUE_CHANGED, NULL);

    settings_group(s_list, "系统");
    settings_row(s_list, LV_SYMBOL_SD_CARD, UI_COLOR_TILE_RED, "存储空间", settings_row_cb, 7,
                 &s_store_val);
    settings_row(s_list, LV_SYMBOL_LIST, UI_COLOR_TILE_GRAY, "关于", settings_row_cb, 8, NULL);

    s_ask_timer = lv_timer_create(settings_ask_clear_cb, 4000, NULL);

    s_sheet = lv_obj_create(s_root);
    lv_obj_set_size(s_sheet, 520, 200);
    lv_obj_align(s_sheet, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_sheet, C_ROW, 0);
    lv_obj_set_style_bg_opa(s_sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_sheet, C_CARD, 0);
    lv_obj_set_style_border_width(s_sheet, 1, 0);
    lv_obj_set_style_radius(s_sheet, 18, 0);
    lv_obj_set_style_shadow_width(s_sheet, 24, 0);
    lv_obj_set_style_shadow_color(s_sheet, lv_color_hex(0x9AA0A6), 0);
    lv_obj_set_style_shadow_opa(s_sheet, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(s_sheet, 0, 0);
    lv_obj_clear_flag(s_sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);

    s_sheet_title = ui_label(s_sheet, "音量", UI_FONT_TEXT, C_TEXT);
    lv_obj_set_pos(s_sheet_title, 24, 22);
    s_sheet_val = ui_label(s_sheet, "0", UI_FONT_BIG, C_ACCENT);
    lv_obj_align(s_sheet_val, LV_ALIGN_TOP_RIGHT, -28, 16);

    s_sheet_slider = lv_slider_create(s_sheet);
    lv_obj_set_pos(s_sheet_slider, 24, 96);
    lv_obj_set_size(s_sheet_slider, 472, 16);
    lv_slider_set_range(s_sheet_slider, 0, 100);
    lv_obj_set_style_bg_color(s_sheet_slider, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_sheet_slider, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_sheet_slider, C_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(s_sheet_slider, settings_sheet_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    {
        lv_obj_t *btn = lv_obj_create(s_sheet);
        lv_obj_t *label;

        lv_obj_set_pos(btn, 340, 132);
        lv_obj_set_size(btn, 156, 48);
        lv_obj_set_style_bg_color(btn, C_ACCENT, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, settings_sheet_done_cb, LV_EVENT_CLICKED, NULL);
        label = ui_label(btn, "完成", UI_FONT_TEXT, lv_color_hex(0xFFFFFF));
        lv_obj_center(label);
    }

    return s_root;
}

void ui_settings_enter(void)
{
    app_settings_t *cfg = app_settings_get();
    char buf[32];
    size_t i;

    if (s_root == NULL) {
        return;
    }

    s_syncing = true;

    ui_set_text(s_wlan_val, app_wifi_sta_has_ip() ? "已联网" : "仅热点");
    ui_set_text(s_pass_val, app_wifi_ap_pass());

    snprintf(buf, sizeof(buf), "%u", (unsigned)cfg->volume);
    ui_set_text(s_vol_val, buf);
    snprintf(buf, sizeof(buf), "%u", (unsigned)cfg->brightness);
    ui_set_text(s_bri_val, buf);

    ui_set_text(s_night_val, s_night_text[0]);
    for (i = 0; i < sizeof(s_night_start) / sizeof(s_night_start[0]); i++) {
        if (s_night_start[i] == cfg->night_start && s_night_end[i] == cfg->night_end) {
            ui_set_text(s_night_val, s_night_text[i]);
            break;
        }
    }

    if (cfg->clock_on) {
        lv_obj_add_state(s_clock_sw, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_clock_sw, LV_STATE_CHECKED);
    }

    s_syncing = false;

    snprintf(buf, sizeof(buf), "剩余 %lu MB", (unsigned long)(app_store_free_kb() / 1024));
    ui_set_text(s_store_val, buf);
    ui_set_text(s_bg_val, ui_bg_exists() ? "自定义" : "纯色");

    s_pending = 0;
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
}
