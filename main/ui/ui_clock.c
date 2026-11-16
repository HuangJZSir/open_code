#include "ui_clock.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "app_player.h"
#include "app_store.h"
#include "app_time.h"
#include "app_wifi.h"
#include "esp_log.h"
#include "ui_nav.h"
#include "ui_theme.h"

#define C_BG     lv_color_hex(0x0D0F13)
#define C_BG2    lv_color_hex(0x05070A)
#define C_TEXT   lv_color_hex(0xE6E8EE)
#define C_MUTED  lv_color_hex(0x8B93A7)
#define C_CARD   lv_color_hex(0x171A21)
#define C_LINE   lv_color_hex(0x232A36)
#define C_ACCENT lv_color_hex(0x2F7CF6)
#define C_SOFT   lv_color_hex(0x6B7280)
#define C_RED    lv_color_hex(0xE5484D)
#define C_FACE   lv_color_hex(0x14171E)
#define C_TICK   lv_color_hex(0x3A4152)

#define CLOCK_TICK_MS (200)
#define DIAL_DIA      (176)
#define DIAL_DX       (292)
#define DIAL_DY       (104)
#define CLOCK_VALUE   (6000)

static const char *TAG = "uiclock";

static lv_obj_t *s_root;
static lv_obj_t *s_time;
static lv_obj_t *s_sec;
static lv_obj_t *s_date;
static lv_obj_t *s_dial;
static lv_obj_t *s_song;
static lv_obj_t *s_today;
static lv_obj_t *s_today_bar;
static lv_obj_t *s_net;
static lv_obj_t *s_sync;
static lv_obj_t *s_store;
static lv_meter_indicator_t *s_hour;
static lv_meter_indicator_t *s_min;
static lv_meter_indicator_t *s_arc;
static lv_timer_t *s_timer;
static int s_last_hm = -1;
static int s_last_day = -1;
static int s_count;

static void clock_back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static lv_obj_t *clock_button(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                              lv_coord_t h, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_t *label;

    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, C_CARD, 0);
    lv_obj_set_style_bg_color(btn, C_LINE, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }

    label = ui_label(btn, text, UI_FONT_SMALL, C_TEXT);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *clock_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
    return card;
}

static lv_obj_t *clock_num(lv_obj_t *parent, const char *text, lv_coord_t dx, lv_coord_t dy)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, C_MUTED, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, dx, dy);
    return label;
}

static void clock_dial(lv_obj_t *parent)
{
    lv_meter_scale_t *scale_main;
    lv_meter_scale_t *scale_hour;
    lv_obj_t *cap;

    s_dial = lv_meter_create(parent);
    lv_obj_remove_style_all(s_dial);
    lv_obj_set_size(s_dial, DIAL_DIA, DIAL_DIA);
    lv_obj_align(s_dial, LV_ALIGN_CENTER, DIAL_DX, DIAL_DY);
    lv_obj_set_style_radius(s_dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dial, C_FACE, 0);
    lv_obj_set_style_bg_opa(s_dial, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dial, 1, 0);
    lv_obj_set_style_border_color(s_dial, C_LINE, 0);
    lv_obj_set_style_shadow_width(s_dial, 16, 0);
    lv_obj_set_style_shadow_color(s_dial, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_dial, LV_OPA_40, 0);
    lv_obj_set_style_shadow_ofs_y(s_dial, 4, 0);
    lv_obj_set_style_pad_all(s_dial, 0, 0);
    lv_obj_clear_flag(s_dial, LV_OBJ_FLAG_CLICKABLE);

    scale_main = lv_meter_add_scale(s_dial);
    lv_meter_set_scale_ticks(s_dial, scale_main, 60, 1, 5, C_TICK);
    lv_meter_set_scale_major_ticks(s_dial, scale_main, 5, 2, 9, C_MUTED, 0);
    lv_meter_set_scale_range(s_dial, scale_main, 0, CLOCK_VALUE, 360, 270);
    lv_obj_set_style_text_opa(s_dial, LV_OPA_TRANSP, LV_PART_TICKS);

    scale_hour = lv_meter_add_scale(s_dial);
    lv_meter_set_scale_range(s_dial, scale_hour, 0, 1200, 360, 270);

    s_min = lv_meter_add_needle_line(s_dial, scale_main, 4, C_TEXT, -24);
    s_hour = lv_meter_add_needle_line(s_dial, scale_hour, 6, C_TEXT, -50);
    s_arc = lv_meter_add_arc(s_dial, scale_main, 2, C_RED, -6);
    lv_meter_set_indicator_start_value(s_dial, s_arc, 0);
    lv_meter_set_indicator_end_value(s_dial, s_arc, 0);

    cap = lv_obj_create(s_dial);
    lv_obj_set_size(cap, 8, 8);
    lv_obj_center(cap);
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cap, 0, 0);
    lv_obj_set_style_bg_color(cap, C_TEXT, 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(cap, 0, 0);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_CLICKABLE);

    clock_num(s_dial, "12", 0, -60);
    clock_num(s_dial, "3", 60, 0);
    clock_num(s_dial, "6", 0, 60);
    clock_num(s_dial, "9", -60, 0);
}

static void clock_tick_cb(lv_timer_t *timer)
{
    time_t now;
    struct tm tm_now;
    struct timeval tv;
    char text[64];
    int sec;
    int ms;
    int32_t sec_v;
    int32_t min_v;
    int32_t hour_v;

    (void)timer;

    if (s_root == NULL || lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    s_count++;

    if (!app_time_valid()) {
        ui_set_text(s_time, "--:--");
        ui_set_text(s_sec, "--");
        ui_set_text(s_date, "手机打开 192.168.4.1 自动校时");
        lv_meter_set_indicator_value(s_dial, s_hour, 0);
        lv_meter_set_indicator_value(s_dial, s_min, 0);
        lv_meter_set_indicator_end_value(s_dial, s_arc, 0);
        lv_bar_set_value(s_today_bar, 0, LV_ANIM_OFF);
        return;
    }

    now = (time_t)app_time_now();
    if (localtime_r(&now, &tm_now) == NULL) {
        return;
    }
    gettimeofday(&tv, NULL);
    sec = tm_now.tm_sec;
    ms = (int)(tv.tv_usec / 1000);

    sec_v = (int32_t)(((int64_t)(sec * 1000 + ms) * CLOCK_VALUE) / 60000);
    min_v = (int32_t)(((int64_t)((tm_now.tm_min * 60 + sec) * 1000 + ms) * CLOCK_VALUE) / 3600000);
    hour_v = (int32_t)(((int64_t)((tm_now.tm_hour % 12) * 3600 + tm_now.tm_min * 60 + sec) * 1200) / 43200);

    lv_meter_set_indicator_value(s_dial, s_min, min_v);
    lv_meter_set_indicator_value(s_dial, s_hour, hour_v);
    lv_meter_set_indicator_end_value(s_dial, s_arc, sec_v);

    snprintf(text, sizeof(text), "%02d", sec);
    ui_set_text(s_sec, text);

    if (tm_now.tm_hour * 100 + tm_now.tm_min != s_last_hm) {
        int pct = (tm_now.tm_hour * 3600 + tm_now.tm_min * 60 + sec) * 100 / 86400;

        s_last_hm = tm_now.tm_hour * 100 + tm_now.tm_min;
        snprintf(text, sizeof(text), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
        ui_set_text(s_time, text);
        snprintf(text, sizeof(text), "%d%%", pct);
        ui_set_text(s_today, text);
        lv_bar_set_value(s_today_bar, pct, LV_ANIM_OFF);
    }

    if (tm_now.tm_mday != s_last_day) {
        static const char *const wd[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
        int w = (tm_now.tm_wday >= 0 && tm_now.tm_wday < 7) ? tm_now.tm_wday : 0;

        s_last_day = tm_now.tm_mday;
        snprintf(text, sizeof(text), "%s  %d月%d日", wd[w], tm_now.tm_mon + 1, tm_now.tm_mday);
        ui_set_text(s_date, text);
    }

    if (s_count % 5 == 0) {
        uint32_t index;
        unsigned kb;

        if (app_player_count() == 0) {
            ui_set_text(s_song, "没有歌曲");
        } else {
            index = app_player_index();
            if (index >= app_player_count()) {
                index = 0;
            }
            ui_set_text(s_song, app_player_name(index));
        }

        kb = app_store_free_kb();
        if (kb >= 1048576u) {
            snprintf(text, sizeof(text), "SD 剩余 %u.%u GB", kb / 1048576u, (kb % 1048576u) * 10u / 1048576u);
        } else {
            snprintf(text, sizeof(text), "SD 剩余 %u MB", kb / 1024u);
        }
        ui_set_text(s_store, text);

        ui_set_text(s_net, app_wifi_sta_has_ip() ? "WiFi 已连接" : "热点模式");
        ui_set_text(s_sync, "已校时");
    }
}

void ui_clock_enter(void)
{
    if (s_root == NULL) {
        return;
    }
    s_last_hm = -1;
    s_last_day = -1;
    s_count = 0;
    clock_tick_cb(s_timer);
}

lv_obj_t *ui_clock_create(lv_obj_t *parent)
{
    lv_obj_t *divider;
    lv_obj_t *card;
    lv_obj_t *label;

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

    clock_button(s_root, "返回", 20, 16, 84, 40, clock_back_cb);

    clock_dial(s_root);

    s_time = ui_label(s_root, "--:--", UI_FONT_DIGIT, C_TEXT);
    lv_obj_align(s_time, LV_ALIGN_CENTER, -30, -72);

    s_sec = ui_label(s_root, "--", UI_FONT_SEC, C_SOFT);
    lv_obj_align(s_sec, LV_ALIGN_CENTER, 296, -30);

    s_date = ui_label(s_root, "", UI_FONT_BIG, C_TEXT);
    lv_obj_align(s_date, LV_ALIGN_CENTER, -40, 44);

    divider = lv_obj_create(s_root);
    lv_obj_set_pos(divider, 32, 344);
    lv_obj_set_size(divider, 736, 1);
    lv_obj_set_style_bg_color(divider, C_LINE, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);

    card = clock_card(s_root, 32, 360, 200, 56);
    label = ui_label(card, "正在播放", UI_FONT_SMALL, C_MUTED);
    lv_obj_set_pos(label, 12, 6);
    s_song = ui_label(card, "-", UI_FONT_SMALL, C_TEXT);
    lv_obj_set_width(s_song, 176);
    lv_label_set_long_mode(s_song, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_song, 12, 26);

    card = clock_card(s_root, 248, 360, 340, 56);
    label = ui_label(card, "今日已过", UI_FONT_SMALL, C_MUTED);
    lv_obj_set_pos(label, 12, 6);
    s_today = ui_label(card, "0%", UI_FONT_TEXT, C_TEXT);
    lv_obj_align(s_today, LV_ALIGN_TOP_RIGHT, -12, 2);
    s_today_bar = lv_bar_create(card);
    lv_obj_set_pos(s_today_bar, 12, 36);
    lv_obj_set_size(s_today_bar, 316, 6);
    lv_bar_set_range(s_today_bar, 0, 100);
    lv_bar_set_value(s_today_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_today_bar, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_today_bar, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_today_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    divider = lv_obj_create(s_root);
    lv_obj_set_pos(divider, 32, 432);
    lv_obj_set_size(divider, 736, 1);
    lv_obj_set_style_bg_color(divider, C_LINE, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);

    s_net = ui_label(s_root, "热点模式", UI_FONT_SMALL, C_MUTED);
    lv_obj_set_pos(s_net, 32, 444);

    s_sync = ui_label(s_root, "未校时", UI_FONT_SMALL, C_MUTED);
    lv_obj_align(s_sync, LV_ALIGN_TOP_MID, 0, 444);

    s_store = ui_label(s_root, "SD -", UI_FONT_SMALL, C_MUTED);
    lv_obj_align(s_store, LV_ALIGN_TOP_RIGHT, -32, 444);

    s_timer = lv_timer_create(clock_tick_cb, CLOCK_TICK_MS, NULL);

    ESP_LOGI(TAG, "ready");
    return s_root;
}
