#include "launcher.h"

#include <stdio.h>
#include <string.h>

#include "app_player.h"
#include "app_store.h"
#include "app_time.h"
#include "app_wifi.h"
#include "icons.h"
#include "ui_nav.h"
#include "ui_theme.h"

#define LAUNCH_COLS    4
#define LAUNCH_TILE_W  176
#define LAUNCH_TILE_H  148
#define LAUNCH_GAP_X   16
#define LAUNCH_GAP_Y   12
#define LAUNCH_X       24
#define LAUNCH_Y       104
#define LAUNCH_ICON    92
#define APP_VER        "v0.1.0"

typedef enum {
    ACT_MUSIC = 0,
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
    {&ic_video, "视频", "视频播放还在做", 0xef5b5b, ACT_TODO},
    {&ic_photo, "图片", "图片浏览还在做", 0x33c07a, ACT_TODO},
    {&ic_folder, "文件", "文件管理还在做", 0xf0a93b, ACT_TODO},
    {&ic_clock, "时钟", "时钟与闹钟还在做", 0x6f7bf7, ACT_TODO},
    {&ic_calc, "计算器", "计算器还在做", 0x22b8cf, ACT_TODO},
    {&ic_settings, "设置", "亮度 音量 网络", 0x5b6478, ACT_SETTINGS},
    {&ic_info, "关于", "设备信息", 0xa78bfa, ACT_ABOUT},
};

#define APP_COUNT ((int)(sizeof(s_apps) / sizeof(s_apps[0])))

static lv_obj_t *s_root;
static lv_obj_t *s_clock;
static lv_obj_t *s_info;
static lv_obj_t *s_now;
static lv_obj_t *s_now_title;
static lv_obj_t *s_now_state;
static char s_about[320];
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

    snprintf(text, sizeof(text), "%s · %s", storage, net);
    ui_set_text(s_info, text);
}

static void launcher_clock_update(void)
{
    char text[16];

    if (s_clock == NULL) {
        return;
    }

    if (app_time_valid()) {
        app_time_format(text, sizeof(text), "%H:%M");
        ui_set_text(s_clock, text);
    } else {
        ui_set_text(s_clock, "--:--");
    }
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
        return;
    }

    lv_obj_clear_flag(s_now, LV_OBJ_FLAG_HIDDEN);
    title = app_player_name(app_player_index());
    snprintf(s_now_text, sizeof(s_now_text), "正在播放  %s", title);
    ui_set_text(s_now_title, s_now_text);
    ui_set_text(s_now_state, (state == APP_PLAYER_PLAYING) ? "播放中" : "已暂停");
}

static void launcher_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_root == NULL || lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    launcher_clock_update();
    launcher_info_update();
    launcher_now_update();
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
    case ACT_ABOUT:
        ui_nav_info("关于", s_about);
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
    lv_obj_set_style_bg_color(tile, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tile, UI_COLOR_CARD_2, LV_STATE_PRESSED);
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

static void build_about_text(void)
{
    char storage[80];

    if (!app_store_ready()) {
        snprintf(storage, sizeof(storage), "无存储卡");
    } else {
        snprintf(storage, sizeof(storage), "%s 已用 %lu MB 剩余 %lu MB",
                 app_store_on_sd() ? "SD卡" : "内部存储",
                 (unsigned long)(app_store_used_kb() / 1024),
                 (unsigned long)(app_store_free_kb() / 1024));
    }

    snprintf(s_about, sizeof(s_about),
             "存储 %s\n网络 热点 %s 密码 %s\n音频 ES8311 + I2S 48kHz\n解码 mp3 flac wav mp4\n"
             "版本 open_code %s",
             storage, app_wifi_ap_ssid(), app_wifi_ap_pass(), APP_VER);
}

lv_obj_t *launcher_create(lv_obj_t *parent)
{
    lv_obj_t *title;
    lv_obj_t *welcome;
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

    s_clock = ui_label(s_root, "--:--", UI_FONT_CLOCK, UI_COLOR_TEXT);
    lv_obj_set_pos(s_clock, 24, 0);
    lv_obj_clear_flag(s_clock, LV_OBJ_FLAG_CLICKABLE);

    s_info = ui_label(s_root, "", UI_FONT_SMALL, UI_COLOR_MUTED);
    lv_obj_align(s_info, LV_ALIGN_TOP_RIGHT, -24, 18);
    lv_obj_clear_flag(s_info, LV_OBJ_FLAG_CLICKABLE);

    title = ui_label(s_root, "我的应用", UI_FONT_BIG, UI_COLOR_TEXT);
    lv_obj_set_pos(title, 24, 52);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);

    welcome = ui_label(s_root, "Welcome to YTLZ", UI_FONT_TEXT, UI_COLOR_MUTED);
    lv_obj_align(welcome, LV_ALIGN_TOP_RIGHT, -24, 60);
    lv_obj_clear_flag(welcome, LV_OBJ_FLAG_CLICKABLE);

    for (i = 0; i < APP_COUNT; i++) {
        app_tile(s_root, i);
    }

    bar = lv_obj_create(s_root);
    lv_obj_set_pos(bar, 24, 424);
    lv_obj_set_size(bar, 752, 44);
    lv_obj_set_style_bg_color(bar, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
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

    build_about_text();
    launcher_clock_update();
    launcher_info_update();
    launcher_now_update();

    lv_timer_create(launcher_timer_cb, 500, NULL);
    return s_root;
}

void launcher_refresh(void)
{
    build_about_text();
    launcher_clock_update();
    launcher_info_update();
    launcher_now_update();
}
