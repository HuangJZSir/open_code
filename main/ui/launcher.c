#include "launcher.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_player.h"
#include "app_power.h"
#include "app_store.h"
#include "app_time.h"
#include "app_weather.h"
#include "app_wifi.h"
#include "esp_log.h"
#include "icons.h"
#include "ui_bg.h"
#include "ui_features.h"
#include "ui_home.h"
#include "ui_nav.h"
#include "ui_theme.h"
#include "wx_icons.h"

#define LAUNCH_COLS    4
#define LAUNCH_TILE_W  176
#define LAUNCH_TILE_H  148
#define LAUNCH_GAP_X   16
#define LAUNCH_GAP_Y   12
#define LAUNCH_X       24
#define LAUNCH_Y       104
#define LAUNCH_ICON    92
#define APP_VER        "v0.1.0"

static const char *TAG = "launcher";
#define EQ_BARS        (4)
#define EQ_MIN_H       (8)
#define EQ_MAX_H       (26)
#define EQ_BASE_Y      (8)

typedef enum {
    ACT_MUSIC = 0,
    ACT_VIDEO,
    ACT_ALBUM,
    ACT_FILES,
    ACT_CLOCK,
    ACT_CALC,
    ACT_SETTINGS,
    ACT_ABOUT,
    ACT_TODO,
} app_action_t;

typedef struct {
    const lv_img_dsc_t *icon;
    const char *name;
    const char *hint;
    uint32_t color;
    app_action_t action;
} app_entry_t;

static const app_entry_t s_apps[] = {
    {&ic_music, "音乐", "播放存储卡里的歌曲", 0x2f7cf6, ACT_MUSIC},
#if UI_VIDEO_ENABLE
    {&ic_video, "视频", "播放 SD 卡里的视频", 0xef5b5b, ACT_VIDEO},
#endif
    {&ic_photo, "相册", "看 SD 卡里的照片", 0x33c07a, ACT_ALBUM},
    {&ic_folder, "文件", "存储占用与坏文件清理", 0xf0a93b, ACT_FILES},
    {&ic_clock, "时钟", "科技表盘与今日进度", 0x6f7bf7, ACT_CLOCK},
#if UI_CALC_ENABLE
    {&ic_calc, "计算器", "加减乘除与百分比", 0x22b8cf, ACT_CALC},
#endif
    {&ic_settings, "设置", "亮度 音量 网络 关于", 0x5b6478, ACT_SETTINGS},
};

#define APP_COUNT ((int)(sizeof(s_apps) / sizeof(s_apps[0])))

static lv_obj_t *s_root;
static lv_obj_t *s_bg;
static lv_obj_t *s_bg_mask;
static lv_obj_t *s_pager;
static lv_obj_t *s_pages[3];
static int s_page_index = 2;
static lv_obj_t *s_home;
static lv_obj_t *s_wall;
static lv_obj_t *s_wall_hint;
static lv_obj_t *s_wall_batt_icon;
static lv_obj_t *s_wall_batt;
static lv_obj_t *s_apps_clock;
static bool s_wall_clean;
static lv_obj_t *s_apps_page;
static lv_obj_t *s_clock;
static lv_obj_t *s_date;
static lv_obj_t *s_info;
static lv_obj_t *s_wx_icon;
static lv_obj_t *s_wx_text;
static lv_obj_t *s_now;
static lv_obj_t *s_now_title;
static lv_obj_t *s_now_state;
static lv_obj_t *s_eq[EQ_BARS];
static bool s_eq_on;
static lv_point_t s_press_pt;
static bool s_press_valid;
static bool s_swiped;
static char s_now_text[224];

static void launcher_info_update(void)
{
    char text[96];
    const char *storage;
    const char *net;

    if (s_info == NULL) {
        return;
    }

    if (!app_store_ready()) {
        storage = "无存储";
    } else if (app_store_on_sd()) {
        storage = "SD卡";
    } else {
        storage = "内部存储";
    }

    if (app_wifi_sta_has_ip()) {
        net = "已联网";
    } else {
        net = "热点";
    }

    if (app_power_valid()) {
        snprintf(text, sizeof(text), "%s · %s · %d%%", storage, net, app_power_percent());
    } else {
        snprintf(text, sizeof(text), "%s · %s", storage, net);
    }
    ui_set_text(s_info, text);
}

static void launcher_clock_update(void)
{
    char text[16];

    if (app_time_valid()) {
        app_time_format(text, sizeof(text), "%H:%M");
        if (s_clock != NULL) {
            ui_set_text(s_clock, text);
        }
        if (s_apps_clock != NULL) {
            ui_set_text(s_apps_clock, text);
        }
    } else {
        if (s_clock != NULL) {
            ui_set_text(s_clock, "--:--");
        }
        if (s_apps_clock != NULL) {
            ui_set_text(s_apps_clock, "--:--");
        }
    }
}

static void launcher_date_update(void)
{
    static const char *const wd[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    char text[64];
    time_t now;
    struct tm tm_now;

    if (s_date == NULL) {
        return;
    }
    if (!app_time_valid()) {
        ui_set_text(s_date, "手机打开 192.168.4.1 校时");
        return;
    }
    now = (time_t)app_time_now();
    if (localtime_r(&now, &tm_now) == NULL) {
        return;
    }
    snprintf(text, sizeof(text), "%d月%d日 %s", tm_now.tm_mon + 1, tm_now.tm_mday,
             wd[(tm_now.tm_wday >= 0 && tm_now.tm_wday < 7) ? tm_now.tm_wday : 0]);
    ui_set_text(s_date, text);
}

static void eq_anim_cb(void *var, int32_t v)
{
    lv_obj_t *bar = (lv_obj_t *)var;

    lv_obj_set_height(bar, (lv_coord_t)v);
    lv_obj_set_y(bar, EQ_BASE_Y + (EQ_MAX_H - (lv_coord_t)v));
}

static void eq_start(void)
{
    int i;

    if (s_eq_on) {
        return;
    }
    for (i = 0; i < EQ_BARS; i++) {
        lv_anim_t a;

        if (s_eq[i] == NULL) {
            continue;
        }
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_eq[i]);
        lv_anim_set_values(&a, 5, EQ_MIN_H + ((i * 7) % (EQ_MAX_H - EQ_MIN_H + 1)));
        lv_anim_set_time(&a, 260 + (uint32_t)(i * 90));
        lv_anim_set_playback_time(&a, 220 + (uint32_t)(i * 70));
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&a, eq_anim_cb);
        lv_anim_start(&a);
    }
    s_eq_on = true;
}

static void eq_stop(void)
{
    int i;

    if (!s_eq_on) {
        return;
    }
    for (i = 0; i < EQ_BARS; i++) {
        if (s_eq[i] == NULL) {
            continue;
        }
        lv_anim_del(s_eq[i], eq_anim_cb);
        lv_obj_set_height(s_eq[i], 5);
        lv_obj_set_y(s_eq[i], EQ_BASE_Y + (EQ_MAX_H - 5));
    }
    s_eq_on = false;
}

static void launcher_power_update(void)
{
    char text[16];
    int pct;

    app_power_update();
    if (s_wall_batt == NULL) {
        return;
    }
    if (!app_power_valid()) {
        ui_set_text(s_wall_batt, "--%");
        return;
    }
    pct = app_power_percent();
    snprintf(text, sizeof(text), "%d%%", pct);
    ui_set_text(s_wall_batt, text);
    lv_label_set_text(s_wall_batt_icon, (pct > 80) ? LV_SYMBOL_BATTERY_FULL :
                      (pct > 55) ? LV_SYMBOL_BATTERY_3 :
                      (pct > 30) ? LV_SYMBOL_BATTERY_2 :
                      (pct > 12) ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY);
}

static void launcher_now_update(void)
{
    app_player_state_t state = app_player_state();
    const char *title;

    if (s_now == NULL) {
        return;
    }

    if (state == APP_PLAYER_STOPPED || app_player_count() == 0) {
        lv_obj_add_flag(s_now, LV_OBJ_FLAG_HIDDEN);
        eq_stop();
        return;
    }

    lv_obj_clear_flag(s_now, LV_OBJ_FLAG_HIDDEN);
    title = app_player_name(app_player_index());
    snprintf(s_now_text, sizeof(s_now_text), "正在播放  %s", title);
    ui_set_text(s_now_title, s_now_text);
    ui_set_text(s_now_state, (state == APP_PLAYER_PLAYING) ? "播放中" : "已暂停");

    if (state == APP_PLAYER_PLAYING) {
        eq_start();
    } else {
        eq_stop();
    }
}

static void launcher_bg_released(void)
{
    if (s_bg != NULL) {
        lv_img_set_src(s_bg, NULL);
        lv_obj_add_flag(s_bg, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_bg_mask != NULL) {
        lv_obj_add_flag(s_bg_mask, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_root != NULL) {
        lv_obj_invalidate(s_root);
    }
    ESP_LOGI(TAG, "wallpaper detached after release");
}

static void launcher_apply_bg(void)
{
    const lv_img_dsc_t *dsc = NULL;

    if (s_bg == NULL || s_bg_mask == NULL) {
        return;
    }
    if (!app_store_ready()) {
        return;
    }

    if (ui_bg_exists()) {
        if (ui_bg_needs_load()) {
            lv_obj_add_flag(s_bg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_bg_mask, LV_OBJ_FLAG_HIDDEN);
            lv_refr_now(NULL);
            ui_bg_load();
        }
        dsc = ui_bg_dsc();
    }

    if (dsc != NULL) {
        lv_img_set_src(s_bg, dsc);
        lv_obj_clear_flag(s_bg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_bg_mask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(s_bg);
        lv_obj_move_background(s_bg_mask);
    } else {
        lv_obj_add_flag(s_bg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_bg_mask, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_invalidate(s_root);
}

static uint32_t s_bg_retry_ctr;
static uint32_t s_bg_retry_thresh = 4;

static void launcher_bg_retry(void)
{
    if (!ui_bg_needs_load()) {
        s_bg_retry_ctr = 0;
        s_bg_retry_thresh = 4;
        return;
    }
    if (app_player_state() != APP_PLAYER_STOPPED) {
        s_bg_retry_ctr = 0;
        return;
    }
    if (++s_bg_retry_ctr < s_bg_retry_thresh) {
        return;
    }
    s_bg_retry_ctr = 0;
    ESP_LOGI(TAG, "wallpaper needs reload, restoring");
    launcher_apply_bg();
    if (ui_bg_needs_load()) {
        if (s_bg_retry_thresh < 20) {
            s_bg_retry_thresh = 20;
        }
        ESP_LOGW(TAG, "wallpaper still not loaded, backing off");
    }
}

static void launcher_weather_update(void)
{
    const app_weather_t *w = app_weather_get();
    char text[64];

    if (s_wx_icon == NULL || s_wx_text == NULL) {
        return;
    }
    if (!app_weather_valid()) {
        if (app_wifi_sta_has_ip()) {
            lv_obj_add_flag(s_wx_icon, LV_OBJ_FLAG_HIDDEN);
            ui_set_text(s_wx_text, "天气获取中");
        } else {
            lv_obj_add_flag(s_wx_icon, LV_OBJ_FLAG_HIDDEN);
            ui_set_text(s_wx_text, "手机网页配置 WiFi 后显示天气");
        }
        return;
    }

    lv_img_set_src(s_wx_icon, wx_icon_for(w->icon));
    lv_obj_clear_flag(s_wx_icon, LV_OBJ_FLAG_HIDDEN);
    snprintf(text, sizeof(text), "%d° %s%s", w->temp, w->text, app_weather_stale() ? " (旧)" : "");
    ui_set_text(s_wx_text, text);
}

static void launcher_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_root == NULL || lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    launcher_clock_update();
    launcher_date_update();
    launcher_info_update();
    launcher_power_update();
    launcher_now_update();
    launcher_weather_update();
    launcher_bg_retry();
    ui_home_update();
}

static void now_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_show(UI_SCREEN_MUSIC);
}

static void app_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);

    if (index < 0 || index >= APP_COUNT) {
        return;
    }

    switch (s_apps[index].action) {
    case ACT_MUSIC:
        ui_nav_show(UI_SCREEN_MUSIC);
        break;
    case ACT_SETTINGS:
        ui_nav_show(UI_SCREEN_SETTINGS);
        break;
    case ACT_VIDEO:
        ui_nav_show(UI_SCREEN_VIDEO);
        break;
    case ACT_ALBUM:
        ui_nav_show(UI_SCREEN_ALBUM);
        break;
    case ACT_FILES:
        ui_nav_show(UI_SCREEN_FILES);
        break;
    case ACT_CLOCK:
        ui_nav_show(UI_SCREEN_CLOCK);
        break;
    case ACT_CALC:
        ui_nav_show(UI_SCREEN_CALC);
        break;
    default:
        ui_nav_info(s_apps[index].name, s_apps[index].hint);
        break;
    }
}

static void app_tile(lv_obj_t *parent, int i)
{
    lv_coord_t x = LAUNCH_X + ((i % LAUNCH_COLS) * (LAUNCH_TILE_W + LAUNCH_GAP_X));
    lv_coord_t y = LAUNCH_Y + ((i / LAUNCH_COLS) * (LAUNCH_TILE_H + LAUNCH_GAP_Y));
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_t *icon_bg;
    lv_obj_t *icon;
    lv_obj_t *label;

    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, LAUNCH_TILE_W, LAUNCH_TILE_H);
    lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(tile, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_radius(tile, 20, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tile, app_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    icon_bg = lv_obj_create(tile);
    lv_obj_set_size(icon_bg, LAUNCH_ICON, LAUNCH_ICON);
    lv_obj_set_pos(icon_bg, (LAUNCH_TILE_W - LAUNCH_ICON) / 2, 12);
    ui_object_bg(icon_bg, lv_color_hex(s_apps[i].color), LV_OPA_COVER, 26);
    lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_CLICKABLE);

    icon = lv_img_create(icon_bg);
    lv_img_set_src(icon, s_apps[i].icon);
    lv_obj_center(icon);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);

    label = ui_label(tile, s_apps[i].name, UI_FONT_TEXT, UI_COLOR_TEXT);
    lv_obj_set_width(label, LAUNCH_TILE_W - 12);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
}

void launcher_show_page(int index)
{
    int i;

    if (index < 0 || index > 2) {
        return;
    }
    s_page_index = index;
    for (i = 0; i < 3; i++) {
        if (s_pages[i] == NULL) {
            continue;
        }
        if (i == index) {
            lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void wall_tap_cb(lv_event_t *e)
{
    (void)e;
    s_wall_clean = !s_wall_clean;
    if (s_wall_clean) {
        lv_obj_add_flag(s_clock, LV_OBJ_FLAG_HIDDEN);
        if (s_date != NULL) {
            lv_obj_add_flag(s_date, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_wall_hint != NULL) {
            lv_obj_add_flag(s_wall_hint, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_wall_batt != NULL) {
            lv_obj_add_flag(s_wall_batt, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_wall_batt_icon != NULL) {
            lv_obj_add_flag(s_wall_batt_icon, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_clear_flag(s_clock, LV_OBJ_FLAG_HIDDEN);
        if (s_date != NULL) {
            lv_obj_clear_flag(s_date, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_wall_hint != NULL) {
            lv_obj_clear_flag(s_wall_hint, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_wall_batt != NULL) {
            lv_obj_clear_flag(s_wall_batt, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_wall_batt_icon != NULL) {
            lv_obj_clear_flag(s_wall_batt_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void pager_press_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();

    (void)e;
    if (indev != NULL) {
        lv_indev_get_point(indev, &s_press_pt);
        s_press_valid = true;
    }
}


static void pager_pressing_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t pt;
    int dx;
    int dy;
    int adx;
    int ady;

    (void)e;
    if (indev == NULL || !s_press_valid || s_swiped) {
        return;
    }
    lv_indev_get_point(indev, &pt);
    dx = (int)pt.x - (int)s_press_pt.x;
    dy = (int)pt.y - (int)s_press_pt.y;
    adx = (dx < 0) ? -dx : dx;
    ady = (dy < 0) ? -dy : dy;
    if (adx < 90 || adx < ady * 2) {
        return;
    }
    s_swiped = true;
    if (dx > 0) {
        launcher_show_page(s_page_index - 1);
    } else {
        launcher_show_page(s_page_index + 1);
    }
}
static void pager_release_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t pt;
    int dx;
    int dy;
    int adx;
    int ady;

    (void)e;
    if (indev == NULL || !s_press_valid) {
        return;
    }
    s_press_valid = false;
    lv_indev_get_point(indev, &pt);
    dx = (int)pt.x - (int)s_press_pt.x;
    dy = (int)pt.y - (int)s_press_pt.y;
    adx = (dx < 0) ? -dx : dx;
    ady = (dy < 0) ? -dy : dy;

    if (adx < 60 || adx < ady * 2) {
        return;
    }
    if (dx > 0) {
        launcher_show_page(s_page_index - 1);
    } else {
        launcher_show_page(s_page_index + 1);
    }
}

static lv_obj_t *launcher_page(lv_obj_t *parent, lv_coord_t x)
{
    lv_obj_t *page = lv_obj_create(parent);

    lv_obj_set_pos(page, x, 0);
    lv_obj_set_size(page, UI_W, UI_H);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

lv_obj_t *launcher_create(lv_obj_t *parent)
{
    lv_obj_t *title;
    lv_obj_t *hint;
    lv_obj_t *bar;
    int i;

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, UI_W, UI_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_bg = lv_img_create(s_root);
    lv_obj_set_pos(s_bg, 0, 0);
    lv_obj_add_flag(s_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_bg, LV_OBJ_FLAG_SCROLLABLE);

    s_bg_mask = lv_obj_create(s_root);
    lv_obj_set_size(s_bg_mask, UI_W, UI_H);
    lv_obj_set_pos(s_bg_mask, 0, 0);
    lv_obj_set_style_bg_color(s_bg_mask, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_bg_mask, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_bg_mask, 0, 0);
    lv_obj_set_style_radius(s_bg_mask, 0, 0);
    lv_obj_set_style_pad_all(s_bg_mask, 0, 0);
    lv_obj_clear_flag(s_bg_mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_bg_mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_bg_mask, LV_OBJ_FLAG_HIDDEN);

    s_pager = lv_obj_create(s_root);
    lv_obj_set_pos(s_pager, 0, 0);
    lv_obj_set_size(s_pager, UI_W, UI_H);
    lv_obj_set_style_bg_opa(s_pager, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pager, 0, 0);
    lv_obj_set_style_radius(s_pager, 0, 0);
    lv_obj_set_style_pad_all(s_pager, 0, 0);
    lv_obj_set_scroll_dir(s_pager, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(s_pager, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(s_pager, LV_SCROLLBAR_MODE_OFF);

    s_home = ui_home_create(s_pager, 0);
    s_wall = launcher_page(s_pager, 0);
    s_apps_page = launcher_page(s_pager, 0);

    s_pages[0] = s_apps_page;
    s_pages[1] = s_wall;
    s_pages[2] = s_home;
    for (i = 0; i < 3; i++) {
        if (s_pages[i] == NULL) {
            continue;
        }
        lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(s_pages[i], pager_press_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(s_pages[i], pager_pressing_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(s_pages[i], pager_release_cb, LV_EVENT_RELEASED, NULL);
    }

    s_clock = ui_label(s_wall, "--:--", UI_FONT_DIGIT, lv_color_hex(0xF2F4F8));
    lv_obj_align(s_clock, LV_ALIGN_TOP_MID, 0, 116);
    lv_obj_clear_flag(s_clock, LV_OBJ_FLAG_CLICKABLE);

    s_date = ui_label(s_wall, "", UI_FONT_BIG, lv_color_hex(0xD6DAE2));
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, 326);
    lv_obj_clear_flag(s_date, LV_OBJ_FLAG_CLICKABLE);

    hint = ui_label(s_wall, LV_SYMBOL_LEFT "  右滑查看应用", UI_FONT_SMALL, lv_color_hex(0xB9BFCA));
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_clear_flag(hint, LV_OBJ_FLAG_CLICKABLE);
    s_wall_batt_icon = lv_label_create(s_wall);
    lv_label_set_text(s_wall_batt_icon, LV_SYMBOL_BATTERY_3);
    lv_obj_set_style_text_font(s_wall_batt_icon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_wall_batt_icon, lv_color_hex(0x2CE0E8), 0);
    lv_obj_set_pos(s_wall_batt_icon, 690, 20);
    lv_obj_clear_flag(s_wall_batt_icon, LV_OBJ_FLAG_CLICKABLE);
    s_wall_batt = ui_label(s_wall, "--%", UI_FONT_SMALL, lv_color_hex(0xF2F4F8));
    lv_obj_set_pos(s_wall_batt, 714, 20);
    lv_obj_clear_flag(s_wall_batt, LV_OBJ_FLAG_CLICKABLE);
    s_wall_hint = hint;
    lv_obj_add_event_cb(s_wall, wall_tap_cb, LV_EVENT_CLICKED, NULL);

    title = ui_label(s_apps_page, "我的应用", UI_FONT_BIG, UI_COLOR_TEXT);
    lv_obj_set_pos(title, 24, 20);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);

    s_apps_clock = ui_label(s_apps_page, "--:--", UI_FONT_BIG, UI_COLOR_TEXT);
    lv_obj_set_pos(s_apps_clock, 172, 22);
    lv_obj_clear_flag(s_apps_clock, LV_OBJ_FLAG_CLICKABLE);

    s_info = ui_label(s_apps_page, "", UI_FONT_SMALL, UI_COLOR_MUTED);
    lv_obj_align(s_info, LV_ALIGN_TOP_RIGHT, -24, 26);
    lv_obj_clear_flag(s_info, LV_OBJ_FLAG_CLICKABLE);

    s_wx_icon = lv_img_create(s_apps_page);
    lv_img_set_zoom(s_wx_icon, 128);
    lv_obj_set_pos(s_wx_icon, 300, 10);
    lv_obj_add_flag(s_wx_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_wx_icon, LV_OBJ_FLAG_CLICKABLE);

    s_wx_text = ui_label(s_apps_page, "", UI_FONT_TEXT, UI_COLOR_TEXT);
    lv_obj_set_pos(s_wx_text, 356, 20);
    lv_obj_clear_flag(s_wx_text, LV_OBJ_FLAG_CLICKABLE);

    for (i = 0; i < APP_COUNT; i++) {
        app_tile(s_apps_page, i);
    }

    bar = lv_obj_create(s_apps_page);
    lv_obj_set_pos(bar, 24, 424);
    lv_obj_set_size(bar, 752, 44);
    lv_obj_set_style_bg_color(bar, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_90, 0);
    lv_obj_set_style_border_color(bar, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_radius(bar, 12, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, UI_COLOR_CARD_2, LV_STATE_PRESSED);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(bar, now_cb, LV_EVENT_CLICKED, NULL);
    s_now = bar;

    s_now_title = ui_label(bar, "正在播放", UI_FONT_NAME, UI_COLOR_TEXT);
    lv_obj_set_pos(s_now_title, 16, 5);
    lv_obj_set_width(s_now_title, 620);
    lv_label_set_long_mode(s_now_title, LV_LABEL_LONG_DOT);
    lv_obj_clear_flag(s_now_title, LV_OBJ_FLAG_CLICKABLE);

    s_now_state = ui_label(bar, "", UI_FONT_SMALL, UI_COLOR_ACCENT);
    lv_obj_align(s_now_state, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_clear_flag(s_now_state, LV_OBJ_FLAG_CLICKABLE);

    for (i = 0; i < EQ_BARS; i++) {
        lv_obj_t *eq = lv_obj_create(bar);

        lv_obj_set_pos(eq, 586 + i * 9, EQ_BASE_Y + (EQ_MAX_H - 5));
        lv_obj_set_size(eq, 4, 5);
        lv_obj_set_style_bg_color(eq, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_bg_opa(eq, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(eq, 0, 0);
        lv_obj_set_style_radius(eq, 2, 0);
        lv_obj_set_style_pad_all(eq, 0, 0);
        lv_obj_clear_flag(eq, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(eq, LV_OBJ_FLAG_CLICKABLE);
        s_eq[i] = eq;
    }

    launcher_clock_update();
    launcher_date_update();
    launcher_info_update();
    launcher_power_update();
    launcher_now_update();
    launcher_weather_update();
    ui_home_update();
    launcher_apply_bg();

    launcher_show_page(2);

    ui_bg_add_release_cb(launcher_bg_released);

    lv_timer_create(launcher_timer_cb, 500, NULL);
    return s_root;
}

void launcher_refresh(void)
{
    launcher_clock_update();
    launcher_date_update();
    launcher_info_update();
    launcher_power_update();
    launcher_now_update();
    launcher_weather_update();
    ui_home_update();
    launcher_apply_bg();
}

void launcher_show_apps(void)
{
    launcher_show_page(0);
}
