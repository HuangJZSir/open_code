#include "ui_home.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_settings.h"
#include "app_time.h"
#include "app_alarm.h"
#include "ui_bg.h"
#include "ui_nav.h"
#include "app_player.h"
#include "app_power.h"
#include "app_weather.h"
#include "app_wifi.h"
#include "bsp_backlight.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "launcher.h"
#include "ui_theme.h"
#include "wx_icons.h"

#define H_BG1     lv_color_hex(0x060B16)
#define H_BG2     lv_color_hex(0x0B1526)
#define H_CARD    lv_color_hex(0x101B2E)
#define H_CARD_HI lv_color_hex(0x16233A)
#define H_BORDER  lv_color_hex(0x1E2C46)
#define H_TEXT    lv_color_hex(0xF2F6FF)
#define H_MUTED   lv_color_hex(0x8494B0)
#define H_CYAN    lv_color_hex(0x2CE0E8)

#define BAR_Y     (6)
#define BIG_Y     (52)
#define BIG_H     (236)
#define SMALL_Y   (300)
#define SMALL_H   (148)
#define L_W       (384)
#define R_X       (412)
#define R_W       (372)
#define ALM_PSRAM_MIN_KB (320)

static const char *TAG = "uihome";

static lv_obj_t *s_root;
static lv_obj_t *s_net;
static lv_obj_t *s_top_time;
static lv_obj_t *s_batt_icon;
static lv_obj_t *s_batt_text;
static lv_obj_t *s_date;
static lv_obj_t *s_bri_arc;
static lv_obj_t *s_bri_text;
static lv_obj_t *s_time;
static lv_obj_t *s_sec;
static lv_obj_t *s_ymd;
static lv_obj_t *s_wx_icon;
static lv_obj_t *s_wx_temp;
static lv_obj_t *s_wx_feels;
static lv_obj_t *s_wx_precip;
static lv_obj_t *s_wx_wind;
static lv_obj_t *s_env_temp;
static lv_obj_t *s_env_hum;
static lv_obj_t *s_env_press;
static lv_obj_t *s_sun_rise;
static lv_obj_t *s_sun_set;
static lv_obj_t *s_alarm_time;
static lv_obj_t *s_alarm_btn;
static lv_obj_t *s_alarm_pill;
static bool s_alarm_on;
static int s_last_min = -1;
static int s_alm_arm_min = -1;
static int s_alm_fired_epoch_min = -1;
static int s_alm_log_min = -1;
static bool s_alm_no_time_logged;

static lv_obj_t *home_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                           bool glow)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, H_CARD, 0);
    lv_obj_set_style_bg_grad_color(card, H_CARD_HI, 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, glow ? 2 : 1, 0);
    lv_obj_set_style_border_color(card, glow ? H_CYAN : H_BORDER, 0);
    lv_obj_set_style_border_opa(card, glow ? LV_OPA_80 : LV_OPA_60, 0);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
    if (glow) {
        lv_obj_set_style_shadow_width(card, 22, 0);
        lv_obj_set_style_shadow_color(card, H_CYAN, 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    }
    return card;
}

static lv_obj_t *home_symbol(lv_obj_t *parent, const char *symbol, lv_coord_t x, lv_coord_t y,
                             uint32_t color, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static lv_obj_t *home_text(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y,
                           const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = ui_label(parent, text, font, lv_color_hex(color));

    lv_obj_set_pos(label, x, y);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static void home_apps_cb(lv_event_t *e)
{
    (void)e;
    launcher_show_apps();
}

static void alm_pill_sync(void)
{
    if (s_alarm_pill != NULL) {
        lv_obj_set_style_bg_color(s_alarm_pill, s_alarm_on ? H_CYAN : H_BORDER, 0);
    }
    if (s_alarm_btn != NULL) {
        ui_set_text(s_alarm_btn, s_alarm_on ? "开启" : "关闭");
    }
}

static void alm_mark_armed(void)
{
    time_t now = (time_t)app_time_now();
    struct tm tm_now;

    if (now != (time_t)0 && localtime_r(&now, &tm_now) != NULL) {
        s_alm_arm_min = tm_now.tm_hour * 100 + tm_now.tm_min;
    } else {
        s_alm_arm_min = -1;
    }
    s_alm_fired_epoch_min = -1;
    s_alm_log_min = -1;
}

static void home_alarm_cb(lv_event_t *e)
{
    (void)e;

    s_alarm_on = !s_alarm_on;
    app_alarm_set_on(s_alarm_on);
    if (s_alarm_on) {
        alm_mark_armed();
    }
    alm_pill_sync();
}

static void home_light_cb(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, (lv_coord_t)v);
}

static lv_obj_t *home_light(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *bar = lv_obj_create(parent);

    lv_obj_set_pos(bar, 24, y);
    lv_obj_set_size(bar, 86, 3);
    lv_obj_set_style_bg_color(bar, H_CYAN, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_shadow_width(bar, 10, 0);
    lv_obj_set_style_shadow_color(bar, H_CYAN, 0);
    lv_obj_set_style_shadow_opa(bar, LV_OPA_60, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    return bar;
}

static void home_light_start(lv_obj_t *bar, int32_t to, uint32_t ms)
{
    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_values(&a, 24, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_playback_time(&a, ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, home_light_cb);
    lv_anim_start(&a);
}

static void home_fade_cb(void *var, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void home_sweep_cb(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, (lv_coord_t)v);
}

static lv_obj_t *home_sweep(lv_obj_t *parent, lv_coord_t y, lv_coord_t to, uint32_t ms)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_anim_t a;

    lv_obj_set_pos(bar, 20, y);
    lv_obj_set_size(bar, 78, 3);
    lv_obj_set_style_bg_color(bar, H_CYAN, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_70, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_shadow_width(bar, 12, 0);
    lv_obj_set_style_shadow_color(bar, H_CYAN, 0);
    lv_obj_set_style_shadow_opa(bar, LV_OPA_50, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_values(&a, 20, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_playback_time(&a, ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, home_sweep_cb);
    lv_anim_start(&a);
    return bar;
}

static void home_fade_start(lv_obj_t *label)
{
    lv_anim_t a;

    if (label == NULL) {
        return;
    }
    lv_anim_init(&a);
    lv_anim_set_var(&a, label);
    lv_anim_set_values(&a, 168, 255);
    lv_anim_set_time(&a, 3000);
    lv_anim_set_playback_time(&a, 3000);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, home_fade_cb);
    lv_anim_start(&a);
}


static lv_obj_t *s_alm_panel;
static lv_obj_t *s_ring_panel;
static lv_obj_t *s_ring_time;
static lv_timer_t *s_ring_auto_timer;
static lv_obj_t *s_alm_list_panel;
static lv_obj_t *s_alm_list_box;
static lv_obj_t *s_alm_label;
static lv_obj_t *s_alm_mode;
static int s_alm_h = 7;
static int s_alm_m = 0;
static bool s_alm_use_sd = true;
static lv_timer_t *s_alm_tone_timer;
static lv_timer_t *s_alm_retry_timer;
static bool s_alm_bg_released;
static int s_alm_play_idx = -1;
static int s_alm_tone_left;
static int s_alm_track = -1;
static char s_alm_track_name[64];

static void alm_sync(void)
{
    char text[16];

    snprintf(text, sizeof(text), "%02d:%02d", s_alm_h, s_alm_m);
    if (s_alarm_time != NULL) {
        ui_set_text(s_alarm_time, text);
    }
    if (s_alm_label != NULL) {
        ui_set_text(s_alm_label, text);
    }
    if (s_alm_mode != NULL) {
        if (!s_alm_use_sd || s_alm_track < 0) {
            ui_set_text(s_alm_mode, "铃声 默认提示音");
        } else if ((uint32_t)s_alm_track < app_player_count()) {
            char name[48];

            snprintf(name, sizeof(name), "%s", app_player_name(s_alm_track));
            ui_set_text(s_alm_mode, name);
        } else {
            ui_set_text(s_alm_mode, "铃声 默认提示音");
        }
    }
}

static void alm_stop_tone(void)
{
    if (s_alm_tone_timer != NULL) {
        lv_timer_del(s_alm_tone_timer);
        s_alm_tone_timer = NULL;
    }
    if (s_alm_retry_timer != NULL) {
        lv_timer_del(s_alm_retry_timer);
        s_alm_retry_timer = NULL;
    }
    s_alm_play_idx = -1;
    app_player_stop();
}

static void alm_ring_close(void)
{
    if (s_ring_auto_timer != NULL) {
        lv_timer_del(s_ring_auto_timer);
        s_ring_auto_timer = NULL;
    }
    if (s_ring_panel != NULL) {
        lv_obj_add_flag(s_ring_panel, LV_OBJ_FLAG_HIDDEN);
    }
    alm_stop_tone();
}

static void alm_ring_close_cb(lv_event_t *e)
{
    (void)e;
    alm_ring_close();
}

static void alm_ring_auto_cb(lv_timer_t *timer)
{
    (void)timer;
    s_ring_auto_timer = NULL;
    alm_ring_close();
}

static void alm_ring_show(void)
{
    char text[16];

    if (s_ring_panel == NULL) {
        return;
    }
    snprintf(text, sizeof(text), "%02d:%02d", s_alm_h, s_alm_m);
    if (s_ring_time != NULL) {
        ui_set_text(s_ring_time, text);
    }
    launcher_show_page(2);
    lv_obj_clear_flag(s_ring_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ring_panel);
    if (s_ring_auto_timer == NULL) {
        s_ring_auto_timer = lv_timer_create(alm_ring_auto_cb, 120000, NULL);
    }
}

static void alm_tone_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_alm_tone_left-- <= 0) {
        alm_stop_tone();
        return;
    }
    app_player_test_tone();
}

static void alm_retry_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    s_alm_retry_timer = NULL;

    if (s_alm_bg_released) {
        return;
    }
    if (!app_player_last_oom() || app_player_state() != APP_PLAYER_STOPPED) {
        return;
    }
    s_alm_bg_released = true;
    ESP_LOGW(TAG, "alarm audio hit psram limit, releasing wallpaper and retrying");
    ui_bg_release();
    if (s_alm_play_idx >= 0 && (uint32_t)s_alm_play_idx < app_player_count()) {
        app_player_play((uint32_t)s_alm_play_idx);
    }
}

static void alm_fire(void)
{
    int idx = -1;

    if (s_alm_use_sd && s_alm_track >= 0 && (uint32_t)s_alm_track < app_player_count()) {
        idx = s_alm_track;
    }
    if (idx < 0 && s_alm_track_name[0] != '\0') {
        uint32_t i;

        for (i = 0; i < app_player_count(); i++) {
            const char *n = app_player_name(i);

            if (n != NULL && strcmp(n, s_alm_track_name) == 0) {
                idx = (int)i;
                break;
            }
        }
    }

    ESP_LOGI(TAG, "alarm fire: use_sd=%d idx=%d count=%lu name=%s", (int)s_alm_use_sd, idx,
             (unsigned long)app_player_count(), s_alm_track_name);

    if (idx >= 0) {
        uint32_t free_kb = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);

        ESP_LOGI(TAG, "alarm psram before=%uKB", free_kb);
        s_alm_bg_released = false;
        if (free_kb < ALM_PSRAM_MIN_KB) {
            ui_bg_release();
            s_alm_bg_released = true;
            ESP_LOGW(TAG, "alarm psram after=%uKB",
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
        }
        s_alm_play_idx = idx;
        app_player_play((uint32_t)idx);
        if (s_alm_retry_timer == NULL) {
            s_alm_retry_timer = lv_timer_create(alm_retry_cb, 3000, NULL);
        } else {
            lv_timer_reset(s_alm_retry_timer);
        }
        ui_nav_toast("闹钟时间到");
        alm_ring_show();
        ESP_LOGI(TAG, "alarm playing track %d", idx);
        return;
    }

    s_alm_tone_left = 20;
    if (s_alm_tone_timer == NULL) {
        s_alm_tone_timer = lv_timer_create(alm_tone_cb, 3000, NULL);
    }
    app_player_test_tone();
    ui_nav_toast("闹钟时间到");
    alm_ring_show();
    ESP_LOGI(TAG, "alarm ringing with default tone for 60s");
}

static void alm_check_cb(lv_timer_t *timer)
{
    time_t now;
    struct tm tm_now;
    int now_min;
    int set_min;
    int32_t epoch_min;

    (void)timer;

    if (!app_time_valid()) {
        if (!s_alm_no_time_logged) {
            s_alm_no_time_logged = true;
            ESP_LOGW(TAG, "alarm: time not synced yet, waiting");
        }
        return;
    }
    now = (time_t)app_time_now();
    if (localtime_r(&now, &tm_now) == NULL) {
        return;
    }

    now_min = tm_now.tm_hour * 100 + tm_now.tm_min;
    set_min = s_alm_h * 100 + s_alm_m;
    if (now_min != set_min) {
        return;
    }

    if (!s_alarm_on) {
        if (s_alm_log_min != now_min) {
            s_alm_log_min = now_min;
            ESP_LOGI(TAG, "alarm %02d:%02d skipped: switch off", s_alm_h, s_alm_m);
        }
        return;
    }
    if (now_min == s_alm_arm_min) {
        if (s_alm_log_min != now_min) {
            s_alm_log_min = now_min;
            ESP_LOGI(TAG, "alarm %02d:%02d skipped: armed in this same minute", s_alm_h, s_alm_m);
        }
        return;
    }
    if (s_alm_panel != NULL && !lv_obj_has_flag(s_alm_panel, LV_OBJ_FLAG_HIDDEN)) {
        if (s_alm_log_min != now_min) {
            s_alm_log_min = now_min;
            ESP_LOGI(TAG, "alarm %02d:%02d skipped: setting panel open", s_alm_h, s_alm_m);
        }
        return;
    }

    epoch_min = (int32_t)(now / 60);
    if (s_alm_fired_epoch_min == epoch_min) {
        return;
    }
    s_alm_fired_epoch_min = epoch_min;
    ESP_LOGI(TAG, "alarm %02d:%02d matched, firing", s_alm_h, s_alm_m);
    alm_fire();
}
static void alm_adj(int dh, int dm)
{
    s_alm_m += dm;
    s_alm_h += dh;
    while (s_alm_m >= 60) {
        s_alm_m -= 60;
        s_alm_h++;
    }
    while (s_alm_m < 0) {
        s_alm_m += 60;
        s_alm_h--;
    }
    while (s_alm_h >= 24) {
        s_alm_h -= 24;
    }
    while (s_alm_h < 0) {
        s_alm_h += 24;
    }
    alm_mark_armed();
    alm_sync();
}

static void alm_btn_cb(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);

    switch (id) {
    case 0:
        alm_adj(1, 0);
        break;
    case 1:
        alm_adj(-1, 0);
        break;
    case 2:
        alm_adj(0, 1);
        break;
    case 3:
        alm_adj(0, -1);
        break;
    case 4:
        s_alm_use_sd = !s_alm_use_sd;
        alm_sync();
        break;
    case 5:
        app_alarm_set_time(s_alm_h, s_alm_m);
        s_alarm_on = true;
        app_alarm_set_on(true);
        alm_mark_armed();
        alm_pill_sync();
        ESP_LOGI(TAG, "alarm armed by setting %02d:%02d", s_alm_h, s_alm_m);
        if (s_alm_panel != NULL) {
            lv_obj_add_flag(s_alm_panel, LV_OBJ_FLAG_HIDDEN);
        }
        break;
    case 6:
        alm_stop_tone();
        break;
    case 7:
        if (s_alm_list_panel != NULL) {
            lv_obj_add_flag(s_alm_list_panel, LV_OBJ_FLAG_HIDDEN);
        }
        break;
    default:
        break;
    }
}

static void home_alarm_open_cb(lv_event_t *e)
{
    (void)e;
    if (s_alm_panel == NULL) {
        return;
    }
    alm_sync();
    lv_obj_clear_flag(s_alm_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_alm_panel);
}

static void alm_small_btn(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y, int id)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_t *l;

    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, 84, 52);
    lv_obj_set_style_bg_color(b, H_CARD_HI, 0);
    lv_obj_set_style_bg_color(b, H_CYAN, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, H_BORDER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b, alm_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)id);
    lv_obj_add_event_cb(b, alm_btn_cb, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(intptr_t)id);
    l = ui_label(b, text, UI_FONT_TEXT, H_TEXT);
    lv_obj_center(l);
}

static lv_obj_t *s_alm_list_panel;
static lv_obj_t *s_alm_list_box;


static void alm_pick_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    s_alm_track = idx;
    snprintf(s_alm_track_name, sizeof(s_alm_track_name), "%s",
             (idx >= 0 && app_player_name((uint32_t)idx) != NULL) ? app_player_name((uint32_t)idx) : "");
    app_alarm_set_track((idx >= 0) ? s_alm_track_name : NULL);
    s_alm_use_sd = (idx >= 0);
    alm_sync();
    if (s_alm_list_panel != NULL) {
        lv_obj_add_flag(s_alm_list_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void alm_list_row(lv_obj_t *parent, const char *text, int idx, lv_coord_t y)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_t *l;

    lv_obj_set_pos(row, 0, y);
    lv_obj_set_size(row, 496, 44);
    lv_obj_set_style_bg_color(row, H_CARD_HI, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, H_CYAN, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, alm_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    l = ui_label(row, text, UI_FONT_SMALL, H_TEXT);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, 464);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
}

static void alm_list_open(void)
{
    uint32_t i;
    uint32_t count;
    lv_coord_t y = 0;

    if (s_alm_list_panel == NULL || s_alm_list_box == NULL) {
        return;
    }
    lv_obj_clean(s_alm_list_box);
    alm_list_row(s_alm_list_box, "默认提示音（1kHz）", -1, y);
    y += 44;

    count = app_player_count();
    for (i = 0; i < count && i < 40; i++) {
        alm_list_row(s_alm_list_box, app_player_name(i), (int)i, y);
        y += 44;
    }
    lv_obj_clear_flag(s_alm_list_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_alm_list_panel);
}

static void alm_open_pick_cb(lv_event_t *e)
{
    (void)e;
    alm_list_open();
}

static void home_alarm_list(lv_obj_t *page)
{
    lv_obj_t *panel = lv_obj_create(page);
    lv_obj_t *l;

    lv_obj_set_pos(panel, 120, 30);
    lv_obj_set_size(panel, 560, 420);
    lv_obj_set_style_bg_color(panel, H_CARD, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, H_CYAN, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    s_alm_list_panel = panel;

    l = ui_label(panel, "选择闹钟铃声", UI_FONT_TEXT, H_TEXT);
    lv_obj_set_pos(l, 28, 16);

    {
        lv_obj_t *cl = lv_obj_create(panel);
        lv_obj_t *cll;

        lv_obj_set_pos(cl, 448, 12);
        lv_obj_set_size(cl, 88, 40);
        lv_obj_set_style_bg_color(cl, H_CARD_HI, 0);
        lv_obj_set_style_bg_opa(cl, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(cl, H_BORDER, 0);
        lv_obj_set_style_border_width(cl, 1, 0);
        lv_obj_set_style_radius(cl, 12, 0);
        lv_obj_set_style_pad_all(cl, 0, 0);
        lv_obj_clear_flag(cl, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(cl, alm_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)7);
        cll = ui_label(cl, "返回", UI_FONT_SMALL, H_TEXT);
        lv_obj_center(cll);
    }

    s_alm_list_box = lv_obj_create(panel);
    lv_obj_set_pos(s_alm_list_box, 24, 60);
    lv_obj_set_size(s_alm_list_box, 504, 340);
    lv_obj_set_style_bg_opa(s_alm_list_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_alm_list_box, 0, 0);
    lv_obj_set_style_pad_all(s_alm_list_box, 0, 0);
    lv_obj_set_scroll_dir(s_alm_list_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_alm_list_box, LV_SCROLLBAR_MODE_AUTO);
}

static void home_alarm_panel(lv_obj_t *page)
{
    lv_obj_t *panel = lv_obj_create(page);
    lv_obj_t *l;
    lv_obj_t *b;

    lv_obj_set_pos(panel, 120, 78);
    lv_obj_set_size(panel, 560, 336);
    lv_obj_set_style_bg_color(panel, H_CARD, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, H_CYAN, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 22, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    s_alm_panel = panel;

    l = ui_label(panel, "设置闹钟", UI_FONT_TEXT, H_MUTED);
    lv_obj_set_pos(l, 30, 18);

    s_alm_label = ui_label(panel, "07:00", UI_FONT_MID, H_TEXT);
    lv_obj_set_pos(s_alm_label, 26, 52);

    alm_small_btn(panel, "+时", 330, 52, 0);
    alm_small_btn(panel, "-时", 330, 114, 1);
    alm_small_btn(panel, "+分", 424, 52, 2);
    alm_small_btn(panel, "-分", 424, 114, 3);

    {
        lv_obj_t *row = lv_obj_create(panel);
        lv_obj_t *cap;
        lv_obj_t *arrow;

        lv_obj_set_pos(row, 28, 184);
        lv_obj_set_size(row, 504, 46);
        lv_obj_set_style_bg_color(row, H_CARD_HI, 0);
        lv_obj_set_style_bg_color(row, H_CYAN, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, H_BORDER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, alm_open_pick_cb, LV_EVENT_CLICKED, NULL);

        cap = ui_label(row, "铃声", UI_FONT_SMALL, H_MUTED);
        lv_obj_align(cap, LV_ALIGN_LEFT_MID, 14, 0);

        s_alm_mode = ui_label(row, "默认提示音", UI_FONT_TEXT, H_CYAN);
        lv_obj_set_pos(s_alm_mode, 70, 7);
        lv_label_set_long_mode(s_alm_mode, LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_alm_mode, 360);

        arrow = lv_label_create(row);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_font(arrow, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(arrow, H_MUTED, 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -14, 0);
    }

    l = ui_label(panel, "点铃声可选择SD卡歌曲", UI_FONT_SMALL, H_MUTED);
    lv_obj_set_pos(l, 30, 236);

    b = lv_obj_create(panel);
    lv_obj_set_pos(b, 300, 262);
    lv_obj_set_size(b, 128, 52);
    lv_obj_set_style_bg_color(b, H_CYAN, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b, alm_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)5);
    l = ui_label(b, "完成", UI_FONT_TEXT, lv_color_hex(0x0A1220));
    lv_obj_center(l);

    b = lv_obj_create(panel);
    lv_obj_set_pos(b, 440, 262);
    lv_obj_set_size(b, 96, 52);
    lv_obj_set_style_bg_color(b, H_CARD_HI, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, H_BORDER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b, alm_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)6);
    l = ui_label(b, "停止", UI_FONT_SMALL, H_TEXT);
    lv_obj_center(l);

    home_alarm_list(page);
    alm_sync();
}
lv_obj_t *ui_home_create(lv_obj_t *parent, lv_coord_t x)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_t *card;
    lv_obj_t *label;
    lv_obj_t *line;
    lv_obj_t *btn;

    lv_obj_set_pos(page, x, 0);
    lv_obj_set_size(page, UI_W, UI_H);
    lv_obj_set_style_bg_color(page, H_BG1, 0);
    lv_obj_set_style_bg_grad_color(page, H_BG2, 0);
    lv_obj_set_style_bg_grad_dir(page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    s_root = page;

    home_symbol(page, LV_SYMBOL_WIFI, 18, BAR_Y + 6, 0xF2F6FF, &lv_font_montserrat_22);
    s_net = home_text(page, "热点模式", 52, BAR_Y + 8, UI_FONT_SMALL, 0xF2F6FF);

    s_top_time = home_text(page, "--:--", 250, BAR_Y + 4, UI_FONT_TEXT, 0xF2F6FF);

    s_date = home_text(page, "--", 300, BAR_Y + 8, UI_FONT_SMALL, 0xF2F6FF);
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, BAR_Y + 8);

    s_bri_arc = lv_arc_create(page);
    lv_obj_set_size(s_bri_arc, 30, 30);
    lv_obj_set_pos(s_bri_arc, 700, BAR_Y + 3);
    lv_arc_set_rotation(s_bri_arc, 270);
    lv_arc_set_bg_angles(s_bri_arc, 0, 360);
    lv_arc_set_range(s_bri_arc, 0, 100);
    lv_arc_set_value(s_bri_arc, 78);
    lv_obj_set_style_bg_opa(s_bri_arc, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(s_bri_arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_bri_arc, H_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_bri_arc, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_bri_arc, H_CYAN, LV_PART_INDICATOR);
    lv_obj_clear_flag(s_bri_arc, LV_OBJ_FLAG_CLICKABLE);

    s_batt_icon = home_symbol(page, LV_SYMBOL_BATTERY_3, 566, BAR_Y + 8, 0x2CE0E8, &lv_font_montserrat_16);
    s_batt_text = home_text(page, "--%", 586, BAR_Y + 8, UI_FONT_SMALL, 0xF2F6FF);
    home_text(page, "背光", 648, BAR_Y + 8, UI_FONT_SMALL, 0xF2F6FF);
    s_bri_text = home_text(page, "100%", 734, BAR_Y + 8, UI_FONT_SMALL, 0xF2F6FF);

    card = home_card(page, 16, BIG_Y, L_W, BIG_H, true);
    s_time = home_text(card, "--:--", 24, 14, UI_FONT_MID, 0xF2F6FF);
    home_fade_start(s_time);
    s_sec = home_text(card, "秒 --", 24, 0, UI_FONT_TEXT, 0xF2F6FF);
    lv_obj_align(s_sec, LV_ALIGN_BOTTOM_LEFT, 24, -14);
    s_ymd = home_text(card, "----.--.--", 0, 0, UI_FONT_TEXT, 0xF2F6FF);
    lv_obj_align(s_ymd, LV_ALIGN_BOTTOM_LEFT, 150, -16);

    card = home_card(page, R_X, BIG_Y, R_W, BIG_H, false);
    s_wx_icon = lv_img_create(card);
    lv_obj_set_pos(s_wx_icon, 12, 4);
    lv_obj_add_flag(s_wx_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_wx_icon, LV_OBJ_FLAG_CLICKABLE);

    s_wx_temp = home_text(card, "--°", 120, 28, UI_FONT_SEC, 0xF2F6FF);
    s_wx_feels = home_text(card, "体感 --°C", 0, 0, UI_FONT_TEXT, 0xC6D2E8);
    lv_obj_align(s_wx_feels, LV_ALIGN_TOP_RIGHT, -18, 56);




    home_sweep(card, 4, R_W - 100, 2800);
    home_sweep(card, BIG_H - 7, R_W - 100, 3600);

    line = lv_obj_create(card);
    lv_obj_set_pos(line, 22, 118);
    lv_obj_set_size(line, R_W - 44, 1);
    lv_obj_set_style_bg_color(line, H_BORDER, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);

    home_symbol(card, LV_SYMBOL_TINT, 24, 136, 0x6FA8FF, &lv_font_montserrat_22);
    label = home_text(card, "降水", 60, 148, UI_FONT_TEXT, 0xE8EEFB);
    (void)label;
    s_wx_precip = home_text(card, "--%", 60, 184, UI_FONT_TEXT, 0xF2F6FF);

    home_symbol(card, LV_SYMBOL_UPLOAD, 212, 138, 0x6FA8FF, &lv_font_montserrat_16);
    label = home_text(card, "风速", 240, 148, UI_FONT_TEXT, 0xE8EEFB);
    (void)label;
    s_wx_wind = home_text(card, "--km/h", 240, 184, UI_FONT_TEXT, 0xF2F6FF);

    card = home_card(page, 16, SMALL_Y, 248, SMALL_H, false);
    home_symbol(card, LV_SYMBOL_TINT, 18, 14, 0x6FA8FF, &lv_font_montserrat_22);
    home_text(card, "环境", 18, 48, UI_FONT_SMALL, 0x8494B0);
    s_env_temp = home_text(card, "--°C", 18, 74, UI_FONT_BIG, 0xF2F6FF);
    s_env_hum = home_text(card, "--%", 140, 24, UI_FONT_TEXT, 0xF2F6FF);
    home_text(card, "湿度", 140, 60, UI_FONT_SMALL, 0x8494B0);
    home_text(card, "气压", 18, 116, UI_FONT_SMALL, 0x8494B0);
    s_env_press = home_text(card, "--hPa", 74, 110, UI_FONT_TEXT, 0xF2F6FF);

    card = home_card(page, 276, SMALL_Y, 248, SMALL_H, false);
    home_symbol(card, LV_SYMBOL_UP, 20, 26, 0xF0C24B, &lv_font_montserrat_22);
    s_sun_rise = home_text(card, "日出 --:--", 54, 26, UI_FONT_TEXT, 0xF2F6FF);
    home_symbol(card, LV_SYMBOL_DOWN, 20, 84, 0xE8A33C, &lv_font_montserrat_22);
    s_sun_set = home_text(card, "日落 --:--", 54, 84, UI_FONT_TEXT, 0xF2F6FF);

    card = home_card(page, 536, SMALL_Y, 248, SMALL_H, false);
    home_text(card, "闹钟", 20, 18, UI_FONT_TEXT, 0xF2F6FF);
    home_symbol(card, LV_SYMBOL_BELL, 200, 16, 0x2CE0E8, &lv_font_montserrat_22);
    s_alarm_time = home_text(card, "07:00", 18, 66, UI_FONT_BIG, 0xF2F6FF);
    lv_obj_add_flag(s_alarm_time, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_alarm_time, home_alarm_open_cb, LV_EVENT_CLICKED, NULL);

    btn = lv_obj_create(card);
    lv_obj_set_pos(btn, 146, 70);
    lv_obj_set_size(btn, 76, 34);
    lv_obj_set_style_bg_color(btn, H_BORDER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 17, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, home_alarm_cb, LV_EVENT_CLICKED, NULL);
    s_alarm_pill = btn;
    s_alarm_btn = ui_label(btn, "关闭", UI_FONT_SMALL, lv_color_hex(0x0A1220));
    lv_obj_center(s_alarm_btn);

    {
        app_alarm_t *alm = app_alarm_get();
        uint32_t i;

        s_alarm_on = alm->on;
        s_alm_h = alm->hour;
        s_alm_m = alm->min;
        snprintf(s_alm_track_name, sizeof(s_alm_track_name), "%s", alm->track);
        s_alm_track = -1;
        s_alm_use_sd = (s_alm_track_name[0] != 0);
        for (i = 0; i < app_player_count(); i++) {
            const char *n = app_player_name(i);

            if (n != NULL && strcmp(n, s_alm_track_name) == 0) {
                s_alm_track = (int)i;
                break;
            }
        }
        alm_pill_sync();
        ESP_LOGI(TAG, "alarm loaded %02d:%02d on=%d ring=%s track=%d", s_alm_h, s_alm_m,
                 (int)s_alarm_on, (s_alm_track_name[0] != '\0') ? s_alm_track_name : "default",
                 s_alm_track);
    }

    home_alarm_panel(page);
    lv_timer_create(alm_check_cb, 1000, NULL);

    {
        lv_obj_t *ring = lv_obj_create(page);
        lv_obj_t *rl;
        lv_obj_t *rb;

        lv_obj_set_pos(ring, 200, 120);
        lv_obj_set_size(ring, 400, 240);
        lv_obj_set_style_bg_color(ring, H_CARD_HI, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(ring, H_CYAN, 0);
        lv_obj_set_style_border_width(ring, 3, 0);
        lv_obj_set_style_radius(ring, 24, 0);
        lv_obj_set_style_pad_all(ring, 0, 0);
        lv_obj_set_style_shadow_width(ring, 30, 0);
        lv_obj_set_style_shadow_color(ring, H_CYAN, 0);
        lv_obj_set_style_shadow_opa(ring, LV_OPA_30, 0);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
        s_ring_panel = ring;

        rl = lv_label_create(ring);
        lv_label_set_text(rl, LV_SYMBOL_BELL);
        lv_obj_set_style_text_font(rl, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(rl, H_CYAN, 0);
        lv_obj_align(rl, LV_ALIGN_TOP_MID, 0, 16);

        rl = ui_label(ring, "定闹钟的时间到了", UI_FONT_TEXT, H_TEXT);
        lv_obj_align(rl, LV_ALIGN_TOP_MID, 0, 48);

        s_ring_time = ui_label(ring, "07:00", UI_FONT_SEC, H_CYAN);
        lv_obj_align(s_ring_time, LV_ALIGN_TOP_MID, 0, 80);

        rb = lv_obj_create(ring);
        lv_obj_set_pos(rb, 100, 176);
        lv_obj_set_size(rb, 200, 50);
        lv_obj_set_style_bg_color(rb, H_CYAN, 0);
        lv_obj_set_style_bg_opa(rb, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(rb, 0, 0);
        lv_obj_set_style_radius(rb, 25, 0);
        lv_obj_set_style_pad_all(rb, 0, 0);
        lv_obj_clear_flag(rb, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(rb, alm_ring_close_cb, LV_EVENT_CLICKED, NULL);
        rl = ui_label(rb, "确认", UI_FONT_TEXT, lv_color_hex(0x0A1220));
        lv_obj_center(rl);
    }

    btn = lv_obj_create(page);
    lv_obj_set_pos(btn, 692, 452);
    lv_obj_set_size(btn, 92, 26);
    lv_obj_set_style_bg_color(btn, H_CARD_HI, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_90, 0);
    lv_obj_set_style_border_color(btn, H_BORDER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 13, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, home_apps_cb, LV_EVENT_CLICKED, NULL);
    label = ui_label(btn, LV_SYMBOL_LEFT " 应用", UI_FONT_SMALL, H_CYAN);
    lv_obj_center(label);

    ESP_LOGI(TAG, "dashboard ready");
    return page;
}

void ui_home_update(void)
{
    const app_weather_t *w;
    app_settings_t *cfg;
    char text[64];
    time_t now;
    struct tm tm_now;
    static const char *const wd[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

    if (s_root == NULL) {
        return;
    }

    cfg = app_settings_get();
    app_power_update();
    if (app_power_valid()) {
        char batt[16];
        int pct = app_power_percent();

        snprintf(batt, sizeof(batt), "%d%%", pct);
        ui_set_text(s_batt_text, batt);
        lv_label_set_text(s_batt_icon, (pct > 80) ? LV_SYMBOL_BATTERY_FULL :
                        (pct > 55) ? LV_SYMBOL_BATTERY_3 :
                        (pct > 30) ? LV_SYMBOL_BATTERY_2 :
                        (pct > 12) ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY);
    } else {
        ui_set_text(s_batt_text, "--");
    }

    lv_arc_set_value(s_bri_arc, (int16_t)cfg->brightness);
    snprintf(text, sizeof(text), "%u%%", (unsigned)cfg->brightness);
    ui_set_text(s_bri_text, text);

    if (app_wifi_sta_has_ip()) {
        char ip[24] = "";

        app_wifi_get_ip(ip, sizeof(ip));
        snprintf(text, sizeof(text), "WiFi %s", ip);
    } else {
        snprintf(text, sizeof(text), "热点 %s", app_wifi_ap_ssid());
    }
    ui_set_text(s_net, text);

    if (!app_time_valid()) {
        ui_set_text(s_time, "--:--");
        ui_set_text(s_sec, "秒 --");
        ui_set_text(s_ymd, "----.--.--");
        ui_set_text(s_date, "手机打开 192.168.4.1 校时");
    } else {
        now = (time_t)app_time_now();
        if (localtime_r(&now, &tm_now) != NULL) {
            if (tm_now.tm_hour * 100 + tm_now.tm_min != s_last_min) {
                s_last_min = tm_now.tm_hour * 100 + tm_now.tm_min;
                snprintf(text, sizeof(text), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
                ui_set_text(s_time, text);
                snprintf(text, sizeof(text), "%04d.%02d.%02d", tm_now.tm_year + 1900,
                         tm_now.tm_mon + 1, tm_now.tm_mday);
                ui_set_text(s_ymd, text);
                snprintf(text, sizeof(text), "%s  %d月%d日",
                         wd[(tm_now.tm_wday >= 0 && tm_now.tm_wday < 7) ? tm_now.tm_wday : 0],
                         tm_now.tm_mon + 1, tm_now.tm_mday);
                ui_set_text(s_date, text);
            }
            snprintf(text, sizeof(text), "秒 %02d", tm_now.tm_sec);
            ui_set_text(s_sec, text);

            if (s_top_time != NULL) {
                char hhmm[8];

                snprintf(hhmm, sizeof(hhmm), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
                ui_set_text(s_top_time, hhmm);
            }
        }
    }

    w = app_weather_get();
    if (!app_weather_valid()) {
        lv_obj_add_flag(s_wx_icon, LV_OBJ_FLAG_HIDDEN);
        ui_set_text(s_wx_temp, "--°");
        ui_set_text(s_wx_feels, app_wifi_sta_has_ip() ? "获取中" : "未联网");
        ui_set_text(s_wx_precip, "--%");
        ui_set_text(s_wx_wind, "--km/h");
        ui_set_text(s_env_temp, "--°C");
        ui_set_text(s_env_hum, "--%");
        ui_set_text(s_env_press, "--hPa");
        ui_set_text(s_sun_rise, "日出 --:--");
        ui_set_text(s_sun_set, "日落 --:--");
        return;
    }

    lv_img_set_src(s_wx_icon, wx_icon_for(w->icon));
    lv_obj_clear_flag(s_wx_icon, LV_OBJ_FLAG_HIDDEN);
    snprintf(text, sizeof(text), "%d°", w->temp);
    ui_set_text(s_wx_temp, text);
    snprintf(text, sizeof(text), "体感 %d°C", w->feels);
    ui_set_text(s_wx_feels, text);
    snprintf(text, sizeof(text), "%d%%", w->precip);
    ui_set_text(s_wx_precip, text);
    snprintf(text, sizeof(text), "%dkm/h", w->wind_speed);
    ui_set_text(s_wx_wind, text);

    snprintf(text, sizeof(text), "%d°C", w->feels);
    ui_set_text(s_env_temp, text);
    snprintf(text, sizeof(text), "%d%%", w->humidity);
    ui_set_text(s_env_hum, text);
    snprintf(text, sizeof(text), "%dhPa", w->pressure);
    ui_set_text(s_env_press, text);

    if (w->sun_valid) {
        snprintf(text, sizeof(text), "日出 %02d:%02d", w->sunrise_h, w->sunrise_m);
        ui_set_text(s_sun_rise, text);
        snprintf(text, sizeof(text), "日落 %02d:%02d", w->sunset_h, w->sunset_m);
        ui_set_text(s_sun_set, text);
    }
}
