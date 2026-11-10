#include "ui_settings.h"

#include <stdint.h>
#include <stdio.h>

#include "app_player.h"
#include "app_settings.h"
#include "app_store.h"
#include "app_wifi.h"
#include "bsp_backlight.h"
#include "ui_nav.h"
#include "ui_theme.h"

static lv_obj_t *s_root;
static lv_obj_t *s_bright;
static lv_obj_t *s_bright_val;
static lv_obj_t *s_volume;
static lv_obj_t *s_volume_val;
static lv_obj_t *s_clock;
static lv_obj_t *s_night;
static lv_obj_t *s_storage;
static lv_obj_t *s_network;
static lv_obj_t *s_player;
static lv_obj_t *s_url;
static bool s_syncing;

static const uint8_t s_night_start[] = {0xFF, 22, 23, 1};
static const uint8_t s_night_end[] = {0xFF, 7, 8, 7};

static lv_obj_t *settings_key(lv_obj_t *parent, const char *text, lv_coord_t y)
{
    lv_obj_t *label = ui_label(parent, text, UI_FONT_TEXT, UI_COLOR_TEXT);

    lv_obj_set_pos(label, 28, y);
    return label;
}

static void settings_back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void settings_night_cb(lv_event_t *e)
{
    uint16_t index = lv_dropdown_get_selected(lv_event_get_target(e));

    if (s_syncing) {
        return;
    }
    if (index < sizeof(s_night_start) / sizeof(s_night_start[0])) {
        app_settings_set_night(s_night_start[index], s_night_end[index]);
    }
}

static void settings_bright_cb(lv_event_t *e)
{
    int32_t value = lv_slider_get_value(lv_event_get_target(e));
    char buf[16];

    snprintf(buf, sizeof(buf), "%ld", (long)value);
    lv_label_set_text(s_bright_val, buf);

    if (s_syncing) {
        return;
    }
    bsp_backlight_set_percent((uint8_t)value);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        app_settings_set_brightness((uint8_t)value);
    }
}

static void settings_volume_cb(lv_event_t *e)
{
    int32_t value = lv_slider_get_value(lv_event_get_target(e));
    char buf[16];

    snprintf(buf, sizeof(buf), "%ld", (long)value);
    lv_label_set_text(s_volume_val, buf);

    app_player_set_volume((uint8_t)value);

    if (s_syncing) {
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        app_settings_set_volume((uint8_t)value);
    }
}

static void settings_clock_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);

    if (s_syncing) {
        return;
    }
    app_settings_set_clock(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void settings_tone_cb(lv_event_t *e)
{
    (void)e;
    app_player_stop();
    app_player_test_tone();
    ui_nav_toast("播放 1kHz 测试音");
}

lv_obj_t *ui_settings_create(lv_obj_t *parent)
{
    lv_obj_t *title;

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, UI_W, UI_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    title = ui_label(s_root, "设置", UI_FONT_BIG, UI_COLOR_TEXT);
    lv_obj_set_pos(title, 24, 12);

    ui_button(s_root, "返回", 688, 10, 88, 44, settings_back_cb, NULL);

    settings_key(s_root, "屏幕亮度", 76);
    s_bright = lv_slider_create(s_root);
    lv_obj_set_pos(s_bright, 200, 90);
    lv_obj_set_size(s_bright, 420, 16);
    lv_slider_set_range(s_bright, 10, 100);
    lv_obj_set_style_bg_color(s_bright, UI_COLOR_CARD_2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bright, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bright, UI_COLOR_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(s_bright, settings_bright_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_bright, settings_bright_cb, LV_EVENT_RELEASED, NULL);

    s_bright_val = ui_label(s_root, "100", UI_FONT_TEXT, UI_COLOR_MUTED);
    lv_obj_set_pos(s_bright_val, 640, 86);

    settings_key(s_root, "播放音量", 132);
    s_volume = lv_slider_create(s_root);
    lv_obj_set_pos(s_volume, 200, 146);
    lv_obj_set_size(s_volume, 420, 16);
    lv_slider_set_range(s_volume, 0, 100);
    lv_obj_set_style_bg_color(s_volume, UI_COLOR_CARD_2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_volume, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_volume, UI_COLOR_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(s_volume, settings_volume_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_volume, settings_volume_cb, LV_EVENT_RELEASED, NULL);

    s_volume_val = ui_label(s_root, "70", UI_FONT_TEXT, UI_COLOR_MUTED);
    lv_obj_set_pos(s_volume_val, 640, 142);

    settings_key(s_root, "夜间息屏", 188);
    s_night = lv_dropdown_create(s_root);
    lv_dropdown_set_options(s_night, "关闭\n22:00 - 07:00\n23:00 - 08:00\n01:00 - 07:00");
    lv_obj_set_pos(s_night, 200, 188);
    lv_obj_set_size(s_night, 240, 52);
    lv_obj_set_style_bg_color(s_night, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(s_night, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_night, UI_COLOR_BORDER, 0);
    lv_obj_set_style_text_font(s_night, UI_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_night, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_night, UI_FONT_TEXT, LV_PART_INDICATOR);
    lv_obj_add_event_cb(s_night, settings_night_cb, LV_EVENT_VALUE_CHANGED, NULL);

    settings_key(s_root, "主页时钟", 254);
    s_clock = lv_switch_create(s_root);
    lv_obj_set_pos(s_clock, 200, 260);
    lv_obj_set_size(s_clock, 64, 34);
    lv_obj_set_style_bg_color(s_clock, UI_COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_clock, settings_clock_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_player = ui_label(s_root, "", UI_FONT_SMALL, UI_COLOR_MUTED);
    lv_obj_set_pos(s_player, 28, 322);

    ui_button(s_root, "测试音", 620, 244, 156, 44, settings_tone_cb, NULL);

    s_storage = ui_label(s_root, "", UI_FONT_SMALL, UI_COLOR_MUTED);
    lv_obj_set_pos(s_storage, 28, 356);

    s_network = ui_label(s_root, "", UI_FONT_SMALL, UI_COLOR_MUTED);
    lv_obj_set_width(s_network, 744);
    lv_label_set_long_mode(s_network, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_network, 28, 390);

    s_url = ui_label(s_root, "", UI_FONT_SMALL, UI_COLOR_ACCENT);
    lv_obj_set_pos(s_url, 28, 430);

    return s_root;
}

void ui_settings_enter(void)
{
    app_settings_t *cfg = app_settings_get();
    char buf[192];
    char ip[24];

    if (s_root == NULL) {
        return;
    }

    s_syncing = true;

    for (size_t i = 0; i < sizeof(s_night_start) / sizeof(s_night_start[0]); i++) {
        if (s_night_start[i] == cfg->night_start && s_night_end[i] == cfg->night_end) {
            lv_dropdown_set_selected(s_night, (uint16_t)i);
            break;
        }
    }

    lv_slider_set_value(s_bright, cfg->brightness, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%u", (unsigned)cfg->brightness);
    lv_label_set_text(s_bright_val, buf);

    lv_slider_set_value(s_volume, cfg->volume, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%u", (unsigned)cfg->volume);
    lv_label_set_text(s_volume_val, buf);
    app_player_set_volume(cfg->volume);

    if (cfg->clock_on) {
        lv_obj_add_state(s_clock, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_clock, LV_STATE_CHECKED);
    }

    s_syncing = false;

    snprintf(buf, sizeof(buf), "曲目 %lu 首，格式 mp3 flac wav mp4",
             (unsigned long)app_player_count());
    lv_label_set_text(s_player, buf);

    snprintf(buf, sizeof(buf), "存储：%s 已用 %lu MB，剩余 %lu MB",
             app_store_on_sd() ? "SD卡" : "内部存储",
             (unsigned long)(app_store_used_kb() / 1024),
             (unsigned long)(app_store_free_kb() / 1024));
    lv_label_set_text(s_storage, buf);

    if (app_wifi_get_ip(ip, sizeof(ip))) {
        snprintf(buf, sizeof(buf), "网络：已联网 %s，热点 %s 密码 %s",
                 ip, app_wifi_ap_ssid(), app_wifi_ap_pass());
    } else {
        snprintf(buf, sizeof(buf), "网络：热点 %s，密码 %s",
                 app_wifi_ap_ssid(), app_wifi_ap_pass());
    }
    lv_label_set_text(s_network, buf);

    snprintf(buf, sizeof(buf), "手机浏览器打开 %s 上传歌曲", app_wifi_ap_url());
    lv_label_set_text(s_url, buf);
}
