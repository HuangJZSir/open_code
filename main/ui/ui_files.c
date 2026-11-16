#include "ui_files.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_files.h"
#include "esp_log.h"
#include "ui_nav.h"
#include "ui_theme.h"

#define FILES_LIST_ROWS (6)
#define FILES_TEXT      (96)
#define FILES_ASK_MS    (4000)

static const char *TAG = "uifiles";

static lv_obj_t *s_root;
static lv_obj_t *s_used;
static lv_obj_t *s_free;
static lv_obj_t *s_bar;
static lv_obj_t *s_cat[5];
static lv_obj_t *s_junk_line;
static lv_obj_t *s_bad_line;
static lv_obj_t *s_status;
static lv_obj_t *s_list;
static int s_pending;
static lv_timer_t *s_ask_timer;

static void files_back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void files_fmt(uint64_t bytes, char *out, size_t size)
{
    if (bytes >= 1073741824ull) {
        unsigned whole = (unsigned)(bytes / 1073741824ull);
        unsigned frac = (unsigned)(((bytes % 1073741824ull) * 10) / 1073741824ull);

        snprintf(out, size, "%u.%u GB", whole, frac);
    } else if (bytes >= 1048576ull) {
        unsigned mb = (unsigned)(bytes / 1048576ull);
        unsigned frac = (unsigned)(((bytes % 1048576ull) * 10) / 1048576ull);

        if (mb >= 1000) {
            snprintf(out, size, "%u MB", mb);
        } else {
            snprintf(out, size, "%u.%u MB", mb, frac);
        }
    } else if (bytes >= 1024ull) {
        snprintf(out, size, "%u KB", (unsigned)(bytes / 1024ull));
    } else {
        snprintf(out, size, "%u B", (unsigned)bytes);
    }
}

static void files_reset_pending(void)
{
    s_pending = 0;
}

static void files_ask_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    files_reset_pending();
}

static void files_build_list(const app_files_stat_t *st)
{
    char text[FILES_TEXT];
    char size[16];
    int i;
    int row = 0;

    if (s_list == NULL) {
        return;
    }
    lv_obj_clean(s_list);

    for (i = 0; i < st->bad_count && row < FILES_LIST_ROWS; i++) {
        lv_obj_t *label;

        files_fmt(app_files_bad_size(i), size, sizeof(size));
        snprintf(text, sizeof(text), "%s  %s", app_files_bad_name(i), size);
        label = ui_label(s_list, text, UI_FONT_SMALL, UI_COLOR_ERR);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, 420);
        lv_obj_set_pos(label, 0, row * 26);
        row++;
    }
    for (i = 0; i < st->junk_count && row < FILES_LIST_ROWS; i++) {
        lv_obj_t *label;

        files_fmt(app_files_junk_size(i), size, sizeof(size));
        snprintf(text, sizeof(text), "%s  %s", app_files_junk_name(i), size);
        label = ui_label(s_list, text, UI_FONT_SMALL, UI_COLOR_MUTED);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, 420);
        lv_obj_set_pos(label, 0, row * 26);
        row++;
    }
    if (row == 0) {
        ui_label(s_list, "没有需要清理的东西", UI_FONT_SMALL, UI_COLOR_MUTED);
    }
}

static void files_refresh(void)
{
    const app_files_stat_t *st;
    char text[FILES_TEXT];
    char a[16];
    char b[16];
    uint64_t used;
    int pct;

    if (s_status != NULL) {
        ui_set_text(s_status, "扫描中…");
        lv_refr_now(NULL);
    }

    st = app_files_scan();

    used = ((uint64_t)st->total_kb > (uint64_t)st->free_kb)
               ? ((uint64_t)(st->total_kb - st->free_kb) * 1024ull)
               : 0ull;
    files_fmt(used, a, sizeof(a));
    files_fmt((uint64_t)st->total_kb * 1024ull, b, sizeof(b));
    snprintf(text, sizeof(text), "已用 %s / 共 %s", a, b);
    ui_set_text(s_used, text);

    files_fmt((uint64_t)st->free_kb * 1024ull, a, sizeof(a));
    snprintf(text, sizeof(text), "剩余 %s", a);
    ui_set_text(s_free, text);

    if (st->total_kb > 0) {
        pct = (int)((used * 100ull) / ((uint64_t)st->total_kb * 1024ull));
    } else {
        pct = 0;
    }
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);

    files_fmt(st->music, a, sizeof(a));
    snprintf(text, sizeof(text), "音乐 %s", a);
    ui_set_text(s_cat[0], text);
    files_fmt(st->video, a, sizeof(a));
    snprintf(text, sizeof(text), "视频 %s", a);
    ui_set_text(s_cat[1], text);
    files_fmt(st->photo, a, sizeof(a));
    snprintf(text, sizeof(text), "照片 %s", a);
    ui_set_text(s_cat[2], text);
    files_fmt(st->cache, a, sizeof(a));
    snprintf(text, sizeof(text), "缓存 %s", a);
    ui_set_text(s_cat[3], text);
    files_fmt(st->other, a, sizeof(a));
    snprintf(text, sizeof(text), "其它 %s", a);
    ui_set_text(s_cat[4], text);

    files_fmt(st->junk, a, sizeof(a));
    snprintf(text, sizeof(text), "垃圾 %d 个 · %s", st->junk_count, a);
    ui_set_text(s_junk_line, text);

    files_fmt(st->bad, a, sizeof(a));
    snprintf(text, sizeof(text), "坏文件 %d 个 · %s", st->bad_count, a);
    ui_set_text(s_bad_line, text);

    snprintf(text, sizeof(text), "共 %d 个文件", st->file_count);
    ui_set_text(s_status, text);

    files_build_list(st);
    ESP_LOGI(TAG, "refresh: %d files junk=%d bad=%d", st->file_count, st->junk_count, st->bad_count);
}

static void files_junk_cb(lv_event_t *e)
{
    int done;

    (void)e;

    if (s_pending != 1) {
        s_pending = 1;
        ui_set_text(s_status, "再点一次清理垃圾");
        if (s_ask_timer != NULL) {
            lv_timer_reset(s_ask_timer);
        }
        return;
    }
    files_reset_pending();
    ui_set_text(s_status, "正在清理…");
    lv_refr_now(NULL);
    done = app_files_clean_junk();
    files_refresh();
    if (done > 0) {
        ui_nav_toast("垃圾已清理");
    } else {
        ui_nav_toast("没有可清理的文件");
    }
}

static void files_bad_cb(lv_event_t *e)
{
    int done;

    (void)e;

    if (s_pending != 2) {
        s_pending = 2;
        ui_set_text(s_status, "再点一次删除坏文件");
        if (s_ask_timer != NULL) {
            lv_timer_reset(s_ask_timer);
        }
        return;
    }
    files_reset_pending();
    ui_set_text(s_status, "正在删除…");
    lv_refr_now(NULL);
    done = app_files_delete_bad();
    files_refresh();
    if (done > 0) {
        ui_nav_toast("坏文件已删除");
    } else {
        ui_nav_toast("没有坏文件");
    }
}

static void files_rescan_cb(lv_event_t *e)
{
    (void)e;
    files_reset_pending();
    files_refresh();
}

void ui_files_enter(void)
{
    if (s_root == NULL) {
        return;
    }
    files_reset_pending();
    files_refresh();
}

lv_obj_t *ui_files_create(lv_obj_t *parent)
{
    lv_obj_t *title;
    lv_obj_t *cap;
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

    ui_button(s_root, "返回", 16, 12, 88, 44, files_back_cb, NULL);
    title = ui_label(s_root, "文件", UI_FONT_BIG, UI_COLOR_TEXT);
    lv_obj_set_pos(title, 120, 16);

    ui_card(s_root, 16, 72, 768, 88);
    s_used = ui_label(s_root, "已用 - / 共 -", UI_FONT_TEXT, UI_COLOR_TEXT);
    lv_obj_set_pos(s_used, 32, 84);
    s_free = ui_label(s_root, "剩余 -", UI_FONT_TEXT, UI_COLOR_MUTED);
    lv_obj_set_pos(s_free, 560, 84);

    s_bar = lv_bar_create(s_root);
    lv_obj_set_pos(s_bar, 32, 128);
    lv_obj_set_size(s_bar, 736, 12);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, UI_COLOR_CARD_2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);

    ui_card(s_root, 16, 172, 300, 216);
    cap = ui_label(s_root, "占用", UI_FONT_SMALL, UI_COLOR_MUTED);
    lv_obj_set_pos(cap, 32, 182);
    for (i = 0; i < 5; i++) {
        s_cat[i] = ui_label(s_root, "-", UI_FONT_TEXT, UI_COLOR_TEXT);
        lv_obj_set_pos(s_cat[i], 32, 212 + (i * 34));
    }

    ui_card(s_root, 332, 172, 452, 216);
    cap = ui_label(s_root, "问题文件", UI_FONT_SMALL, UI_COLOR_MUTED);
    lv_obj_set_pos(cap, 348, 182);

    s_junk_line = ui_label(s_root, "垃圾 0 个", UI_FONT_SMALL, UI_COLOR_TEXT);
    lv_obj_set_pos(s_junk_line, 348, 206);
    s_bad_line = ui_label(s_root, "坏文件 0 个", UI_FONT_SMALL, UI_COLOR_ERR);
    lv_obj_set_pos(s_bad_line, 348, 228);

    s_list = lv_obj_create(s_root);
    lv_obj_set_pos(s_list, 348, 254);
    lv_obj_set_size(s_list, 424, 122);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    ui_button(s_root, "重新扫描", 16, 404, 132, 44, files_rescan_cb, NULL);
    ui_button(s_root, "清理垃圾", 160, 404, 132, 44, files_junk_cb, NULL);
    ui_button(s_root, "删除坏文件", 304, 404, 152, 44, files_bad_cb, NULL);

    s_status = ui_label(s_root, "", UI_FONT_SMALL, UI_COLOR_MUTED);
    lv_obj_set_pos(s_status, 472, 418);

    s_ask_timer = lv_timer_create(files_ask_timer_cb, FILES_ASK_MS, NULL);

    ESP_LOGI(TAG, "ready");
    return s_root;
}
