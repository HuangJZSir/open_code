#include "ui_video.h"

#include <stdint.h>

#include "app_video.h"
#include "ui_nav.h"
#include "ui_theme.h"

#define VIDEO_ROW_H   (52)
#define VIDEO_ROW_GAP (8)
#define VIDEO_POLL_MS (200)
#define VIDEO_BAR_H   (56)

static lv_obj_t *s_root;
static lv_obj_t *s_list;
static lv_obj_t *s_player;
static bool s_playing;

static void back_cb(lv_event_t *e)
{
    (void)e;
    app_video_release();
    ui_nav_back();
}

static void player_cb(lv_event_t *e)
{
    (void)e;
    app_video_stop();
}

static void player_create(void)
{
    lv_obj_t *hint;
    lv_obj_t *bar;
    lv_obj_t *title;

    s_player = lv_obj_create(s_root);
    lv_obj_set_size(s_player, UI_W, UI_H);
    lv_obj_set_pos(s_player, 0, 0);
    lv_obj_set_style_bg_color(s_player, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_player, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_player, 0, 0);
    lv_obj_set_style_radius(s_player, 0, 0);
    lv_obj_set_style_pad_all(s_player, 0, 0);
    lv_obj_clear_flag(s_player, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_player, player_cb, LV_EVENT_CLICKED, NULL);

    bar = lv_obj_create(s_player);
    lv_obj_set_size(bar, UI_W, VIDEO_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    ui_button(bar, "返回", 12, 6, 96, 44, player_cb, NULL);
    title = ui_label(bar, "点击画面可退出", UI_FONT_TEXT, UI_COLOR_MUTED);
    lv_obj_set_pos(title, 124, 14);

    hint = ui_label(s_player, "视频加载中...", UI_FONT_NAME, UI_COLOR_MUTED);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, VIDEO_BAR_H / 2);
}

static void item_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    const char *error;

    if (s_player == NULL) {
        player_create();
    }
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_player, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_player);
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);

    if (app_video_play_index(index) != ESP_OK) {
        error = app_video_last_error();
        ui_nav_toast(error != NULL ? error : "视频无法播放");
        lv_obj_add_flag(s_player, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(s_root);
        return;
    }
    s_playing = true;
}

static void poll_cb(lv_timer_t *timer)
{
    const char *error;

    (void)timer;

    if (!s_playing) {
        return;
    }
    if (app_video_active()) {
        return;
    }
    s_playing = false;
    if (s_player != NULL) {
        lv_obj_add_flag(s_player, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_list != NULL) {
        lv_obj_clear_flag(s_list, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_invalidate(s_root);

    error = app_video_last_error();
    if (error != NULL) {
        ui_nav_toast(error);
    }
}

static void video_list_build(void)
{
    lv_obj_t *hint;
    int count;
    int i;

    lv_obj_clean(s_list);
    count = app_video_scan();
    if (count == 0) {
        hint = ui_label(s_list, "SD 卡里没有视频文件", UI_FONT_TEXT, UI_COLOR_MUTED);
        lv_obj_set_pos(hint, 8, 8);
        return;
    }
    for (i = 0; i < count; i++) {
        lv_coord_t y = (lv_coord_t)(i * (VIDEO_ROW_H + VIDEO_ROW_GAP));

        ui_button(s_list, app_video_name(i), 8, (lv_coord_t)(y + 4),
                  UI_W - 64, VIDEO_ROW_H, item_cb, (void *)(intptr_t)i);
    }
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
}

void ui_video_enter(void)
{
    if (s_list == NULL) {
        return;
    }
    if (s_playing) {
        return;
    }
    if (s_player != NULL) {
        lv_obj_add_flag(s_player, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(s_list, LV_OBJ_FLAG_HIDDEN);
    video_list_build();
}

lv_obj_t *ui_video_create(lv_obj_t *parent)
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

    ui_button(s_root, "返回", 16, 12, 88, 44, back_cb, NULL);
    title = ui_label(s_root, "视频", UI_FONT_BIG, UI_COLOR_TEXT);
    lv_obj_set_pos(title, 124, 16);

    s_list = lv_obj_create(s_root);
    lv_obj_set_pos(s_list, 16, 72);
    lv_obj_set_size(s_list, UI_W - 32, UI_H - 88);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);

    video_list_build();

    lv_timer_create(poll_cb, VIDEO_POLL_MS, NULL);
    return s_root;
}
