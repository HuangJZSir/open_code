#include "ui_music.h"

#include <stdio.h>
#include <string.h>

#include "app_player.h"
#include "app_settings.h"
#include "icons.h"
#include "ui_nav.h"
#include "ui_theme.h"

#define MUSIC_ROW_H     52
#define MUSIC_LIST_W    332
#define MUSIC_LIST_H    300
#define MUSIC_COVER     124

static lv_obj_t *s_root;
static lv_obj_t *s_list;
static lv_obj_t *s_rows[APP_PLAYER_MAX_TRACKS];
static uint32_t s_row_count;
static uint32_t s_show_index;
static uint32_t s_built_count;
static lv_obj_t *s_title;
static lv_obj_t *s_elapsed;
static lv_obj_t *s_total;
static lv_obj_t *s_seek;
static lv_obj_t *s_play_img;
static lv_obj_t *s_vol;
static lv_obj_t *s_vol_label;
static lv_obj_t *s_repeat;
static lv_obj_t *s_shuffle;
static lv_obj_t *s_status;
static lv_obj_t *s_count_label;
static bool s_syncing;
static bool s_seeking;

static void music_format_time(char *buf, size_t size, uint32_t ms)
{
    uint32_t total_s = ms / 1000U;
    uint32_t min = total_s / 60U;
    uint32_t sec = total_s % 60U;

    snprintf(buf, size, "%02lu:%02lu", (unsigned long)min, (unsigned long)sec);
}

static void music_row_style(lv_obj_t *row, bool active)
{
    if (row == NULL) {
        return;
    }
    lv_obj_set_style_bg_opa(row, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(row, UI_COLOR_CARD_2, 0);
    if (active) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(row, 4, 0);
        lv_obj_set_style_border_color(row, UI_COLOR_ACCENT, 0);
    } else {
        lv_obj_set_style_border_width(row, 0, 0);
    }
}

static void music_highlight(void)
{
    uint32_t i;
    uint32_t active = app_player_index();

    for (i = 0; i < s_row_count; i++) {
        music_row_style(s_rows[i], i == active);
    }
}

static void music_row_cb(lv_event_t *e)
{
    uint32_t index = (uint32_t)(intptr_t)lv_event_get_user_data(e);

    if (index < app_player_count()) {
        app_player_play(index);
    }
}

static void music_clear_rows(void)
{
    if (s_list != NULL) {
        lv_obj_clean(s_list);
    }
    s_row_count = 0;
    s_built_count = 0;
}

static void music_build_rows(void)
{
    uint32_t count = app_player_count();
    uint32_t i;

    music_clear_rows();
    if (count > APP_PLAYER_MAX_TRACKS) {
        count = APP_PLAYER_MAX_TRACKS;
    }

    for (i = 0; i < count; i++) {
        char index_text[8];
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_t *no;
        lv_obj_t *name;

        lv_obj_set_size(row, MUSIC_LIST_W, MUSIC_ROW_H);
        lv_obj_set_pos(row, 0, (lv_coord_t)(i * MUSIC_ROW_H));
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, music_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        snprintf(index_text, sizeof(index_text), "%02lu", (unsigned long)(i + 1));
        no = ui_label(row, index_text, UI_FONT_SMALL, UI_COLOR_MUTED);
        lv_obj_set_pos(no, 12, 16);
        lv_obj_clear_flag(no, LV_OBJ_FLAG_CLICKABLE);

        name = ui_label(row, app_player_name(i), UI_FONT_NAME, UI_COLOR_TEXT);
        lv_obj_set_pos(name, 44, 10);
        lv_obj_set_size(name, MUSIC_LIST_W - 56, 32);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_clear_flag(name, LV_OBJ_FLAG_CLICKABLE);

        s_rows[i] = row;
    }

    s_row_count = count;
    s_built_count = app_player_count();
    music_highlight();
}

static void music_sync_buttons(void)
{
    if (s_play_img != NULL) {
        lv_img_set_src(s_play_img, (app_player_state() == APP_PLAYER_PLAYING) ? &ic_pause : &ic_play);
    }
    if (s_repeat != NULL) {
        lv_obj_set_style_bg_color(s_repeat, app_player_repeat() ? UI_COLOR_ACCENT : UI_COLOR_CARD_2, 0);
    }
    if (s_shuffle != NULL) {
        lv_obj_set_style_bg_color(s_shuffle, app_player_shuffle() ? UI_COLOR_ACCENT : UI_COLOR_CARD_2, 0);
    }
}

static void music_timer_cb(lv_timer_t *timer)
{
    uint32_t pos;
    uint32_t total;
    char buf[24];
    app_player_state_t state;

    (void)timer;

    if (s_root == NULL || lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    if (app_player_count() != s_built_count) {
        music_build_rows();
    }

    state = app_player_state();
    pos = app_player_position_ms();
    total = app_player_total_ms();

    ui_set_text(s_title, (app_player_count() > 0) ? app_player_name(app_player_index()) : "没有找到歌曲");

    music_format_time(buf, sizeof(buf), pos);
    ui_set_text(s_elapsed, buf);

    if (total > 0) {
        music_format_time(buf, sizeof(buf), total);
        ui_set_text(s_total, buf);
    } else {
        ui_set_text(s_total, "--:--");
    }

    if (s_seek != NULL && !s_seeking) {
        uint32_t value = (total > 0) ? (uint32_t)(((uint64_t)pos * 1000ULL) / total) : 0;

        if (value > 1000) {
            value = 1000;
        }
        lv_slider_set_value(s_seek, (int32_t)value, LV_ANIM_OFF);
    }

    if (app_player_index() != s_show_index) {
        s_show_index = app_player_index();
        music_highlight();
    }

    music_sync_buttons();

    if (app_player_seeking()) {
        ui_set_text(s_status, "跳转中");
    } else if (state == APP_PLAYER_PAUSED) {
        ui_set_text(s_status, "已暂停");
    } else {
        ui_set_text(s_status, app_player_status());
    }

    if (s_vol_label != NULL && !s_syncing) {
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)app_player_volume());
        ui_set_text(s_vol_label, buf);
    }
}

static void music_back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void music_rescan_cb(lv_event_t *e)
{
    uint32_t count;

    (void)e;

    count = app_player_scan();
    music_build_rows();
    ui_music_enter();
    if (count == 0) {
        ui_nav_toast("没有找到歌曲");
    } else {
        char buf[40];

        snprintf(buf, sizeof(buf), "找到 %lu 首", (unsigned long)count);
        ui_nav_toast(buf);
    }
}

static void music_play_cb(lv_event_t *e)
{
    (void)e;

    if (app_player_count() == 0) {
        ui_nav_toast("没有找到歌曲");
        return;
    }
    app_player_toggle();
    music_sync_buttons();
}

static void music_prev_cb(lv_event_t *e)
{
    (void)e;
    app_player_prev();
}

static void music_next_cb(lv_event_t *e)
{
    (void)e;
    app_player_next();
}

static void music_repeat_cb(lv_event_t *e)
{
    (void)e;
    app_player_set_repeat(!app_player_repeat());
    app_settings_set_repeat(app_player_repeat());
    music_sync_buttons();
}

static void music_shuffle_cb(lv_event_t *e)
{
    (void)e;
    app_player_set_shuffle(!app_player_shuffle());
    app_settings_set_shuffle(app_player_shuffle());
    music_sync_buttons();
}

static void music_seek_press_cb(lv_event_t *e)
{
    (void)e;
    s_seeking = true;
}

static void music_seek_release_cb(lv_event_t *e)
{
    uint32_t total = app_player_total_ms();
    int32_t value = lv_slider_get_value(lv_event_get_target(e));

    s_seeking = false;

    if (total == 0 || value <= 0) {
        return;
    }
    if (value > 1000) {
        value = 1000;
    }
    app_player_seek((uint32_t)(((uint64_t)total * (uint64_t)value) / 1000ULL));
}

static void music_vol_cb(lv_event_t *e)
{
    int32_t value = lv_slider_get_value(lv_event_get_target(e));
    char buf[8];

    if (value < 0) {
        value = 0;
    }
    if (value > 100) {
        value = 100;
    }

    app_player_set_volume((uint8_t)value);
    snprintf(buf, sizeof(buf), "%ld%%", (long)value);
    ui_set_text(s_vol_label, buf);

    if (!s_syncing && lv_event_get_code(e) == LV_EVENT_RELEASED) {
        app_settings_set_volume((uint8_t)value);
    }
}

static lv_obj_t *music_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = ui_card(parent, x, y, w, h);

    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_radius(card, 18, 0);
    return card;
}

lv_obj_t *ui_music_create(lv_obj_t *parent)
{
    lv_obj_t *card;
    lv_obj_t *label;
    lv_obj_t *cover;
    lv_obj_t *icon;
    char buf[48];

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, UI_W, UI_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x0d0f13), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(s_root, lv_color_hex(0x151b26), 0);
    lv_obj_set_style_bg_grad_dir(s_root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    ui_button(s_root, "返回", 16, 10, 88, 44, music_back_cb, NULL);

    label = ui_label(s_root, "音乐", UI_FONT_BIG, UI_COLOR_TEXT);
    lv_obj_set_pos(label, 120, 14);

    snprintf(buf, sizeof(buf), "共 %lu 首", (unsigned long)app_player_count());
    s_count_label = ui_label(s_root, buf, UI_FONT_SMALL, UI_COLOR_MUTED);
    lv_obj_set_pos(s_count_label, 210, 26);

    ui_button(s_root, "重新扫描", 656, 10, 128, 44, music_rescan_cb, NULL);

    card = music_card(s_root, 16, 64, 348, 380);
    s_list = lv_obj_create(card);
    lv_obj_set_pos(s_list, 8, 8);
    lv_obj_set_size(s_list, MUSIC_LIST_W, MUSIC_LIST_H);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_radius(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_ACTIVE);

    s_repeat = ui_button(card, "循环", 8, 322, 104, 44, music_repeat_cb, NULL);
    s_shuffle = ui_button(card, "随机", 120, 322, 104, 44, music_shuffle_cb, NULL);

    card = music_card(s_root, 376, 64, 408, 380);

    cover = lv_obj_create(card);
    lv_obj_set_pos(cover, (408 - MUSIC_COVER) / 2, 16);
    lv_obj_set_size(cover, MUSIC_COVER, MUSIC_COVER);
    lv_obj_set_style_bg_color(cover, lv_color_hex(0x2f7cf6), 0);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(cover, lv_color_hex(0x1b4f8a), 0);
    lv_obj_set_style_bg_grad_dir(cover, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(cover, 30, 0);
    lv_obj_set_style_border_width(cover, 0, 0);
    lv_obj_set_style_pad_all(cover, 0, 0);
    lv_obj_set_style_shadow_width(cover, 0, 0);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_CLICKABLE);

    icon = lv_img_create(cover);
    lv_img_set_src(icon, &ic_music);
    lv_img_set_zoom(icon, 400);
    lv_obj_center(icon);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);

    s_title = ui_label(card, "没有找到歌曲", UI_FONT_NAME, UI_COLOR_TEXT);
    lv_obj_set_pos(s_title, 20, 148);
    lv_obj_set_size(s_title, 368, 34);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);

    s_status = ui_label(card, "", UI_FONT_SMALL, UI_COLOR_ACCENT);
    lv_obj_align(s_status, LV_ALIGN_TOP_RIGHT, -20, 16);

    s_elapsed = ui_label(card, "00:00", UI_FONT_SMALL, UI_COLOR_MUTED);
    lv_obj_set_pos(s_elapsed, 20, 190);

    s_total = ui_label(card, "--:--", UI_FONT_SMALL, UI_COLOR_MUTED);
    lv_obj_align(s_total, LV_ALIGN_TOP_RIGHT, -20, 190);

    s_seek = lv_slider_create(card);
    lv_obj_set_pos(s_seek, 20, 220);
    lv_obj_set_size(s_seek, 368, 10);
    lv_slider_set_range(s_seek, 0, 1000);
    lv_slider_set_value(s_seek, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_seek, UI_COLOR_CARD_2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_seek, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_seek, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_seek, UI_COLOR_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_seek, 0, 0);
    lv_obj_set_style_radius(s_seek, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(s_seek, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_add_event_cb(s_seek, music_seek_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_seek, music_seek_release_cb, LV_EVENT_RELEASED, NULL);

    ui_icon_button(card, &ic_prev, 84, 254, 56, UI_COLOR_CARD_2, music_prev_cb, NULL);

    {
        lv_obj_t *play_btn = ui_icon_button(card, &ic_play, 160, 238, 88, UI_COLOR_ACCENT, music_play_cb, NULL);

        s_play_img = lv_obj_get_child(play_btn, 0);
    }

    ui_icon_button(card, &ic_next, 268, 254, 56, UI_COLOR_CARD_2, music_next_cb, NULL);

    icon = lv_img_create(card);
    lv_img_set_src(icon, &ic_speaker);
    lv_img_set_zoom(icon, 128);
    lv_obj_set_pos(icon, 22, 340);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);

    s_vol = lv_slider_create(card);
    lv_obj_set_pos(s_vol, 60, 348);
    lv_obj_set_size(s_vol, 240, 12);
    lv_slider_set_range(s_vol, 0, 100);
    lv_slider_set_value(s_vol, app_player_volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_vol, UI_COLOR_CARD_2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_vol, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_vol, UI_COLOR_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(s_vol, music_vol_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_vol, music_vol_cb, LV_EVENT_RELEASED, NULL);

    s_vol_label = ui_label(card, "70%", UI_FONT_SMALL, UI_COLOR_MUTED);
    lv_obj_set_pos(s_vol_label, 312, 340);

    music_build_rows();
    music_sync_buttons();
    s_show_index = app_player_index();

    lv_timer_create(music_timer_cb, 300, NULL);
    return s_root;
}

void ui_music_enter(void)
{
    char buf[48];

    if (s_root == NULL) {
        return;
    }

    if (app_player_count() != s_built_count) {
        music_build_rows();
    }

    snprintf(buf, sizeof(buf), "共 %lu 首", (unsigned long)app_player_count());
    ui_set_text(s_count_label, buf);

    s_syncing = true;
    lv_slider_set_value(s_vol, app_player_volume(), LV_ANIM_OFF);
    s_syncing = false;

    s_show_index = app_player_index();
    music_highlight();
    music_sync_buttons();

    if (s_show_index < s_row_count && s_rows[s_show_index] != NULL) {
        lv_obj_scroll_to_view(s_rows[s_show_index], LV_ANIM_OFF);
    }
}

void ui_music_leave(void)
{
}
