#include "ui_album.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_album.h"
#include "app_codec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ui_bg.h"
#include "ui_nav.h"
#include "ui_theme.h"

#define ALBUM_COLS       (4)
#define ALBUM_ROWS       (3)
#define ALBUM_PER_PAGE   (ALBUM_COLS * ALBUM_ROWS)
#define ALBUM_CELL_W     (APP_ALBUM_THUMB_W)
#define ALBUM_CELL_H     (APP_ALBUM_THUMB_H)
#define ALBUM_GAP        (16)
#define ALBUM_X0         ((UI_W - (ALBUM_COLS * ALBUM_CELL_W + (ALBUM_COLS - 1) * ALBUM_GAP)) / 2)
#define ALBUM_Y0         (78)
#define ALBUM_SLIDE_MS   (5000)
#define ALBUM_FADE_MS    (160)
#define ALBUM_SLOT_565   (0x39E7)

static const char *TAG = "album";

typedef struct {
    lv_obj_t *box;
    lv_obj_t *img;
    lv_obj_t *tag;
    lv_img_dsc_t dsc;
    uint16_t *buf;
    int index;
} album_cell_t;

static lv_obj_t *s_root;
static lv_obj_t *s_grid;
static lv_obj_t *s_count;
static lv_obj_t *s_page_label;
static lv_obj_t *s_prev_page_btn;
static lv_obj_t *s_next_page_btn;
static album_cell_t s_cells[ALBUM_PER_PAGE];

static lv_obj_t *s_view;
static lv_obj_t *s_canvas;
static lv_obj_t *s_hint;
static lv_obj_t *s_pos_label;
static lv_obj_t *s_slide_btn;
static uint16_t *s_fb;
static int s_view_index;
static bool s_slide_on;
static lv_timer_t *s_slide_timer;
static int s_page;
static int s_pages;

static void album_free_cells(void)
{
    int i;

    for (i = 0; i < ALBUM_PER_PAGE; i++) {
        if (s_cells[i].buf != NULL) {
            heap_caps_free(s_cells[i].buf);
            s_cells[i].buf = NULL;
        }
        if (s_cells[i].box != NULL) {
            lv_obj_add_flag(s_cells[i].box, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void album_free_fb(void)
{
    if (s_canvas != NULL) {
        lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    }
}

static void album_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void album_fade_in(lv_obj_t *obj)
{
    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, ALBUM_FADE_MS);
    lv_anim_set_exec_cb(&a, album_opa_cb);
    lv_anim_start(&a);
}

static void album_update_pos(void)
{
    char text[48];

    if (s_pos_label == NULL) {
        return;
    }
    if (s_view_index >= 0 && s_view_index < app_album_count()) {
        snprintf(text, sizeof(text), "%d / %d", s_view_index + 1, app_album_count());
    } else {
        snprintf(text, sizeof(text), "0 / 0");
    }
    ui_set_text(s_pos_label, text);
}

static void album_show_photo(int index)
{
    int count = app_album_count();

    if (count <= 0 || s_fb == NULL) {
        return;
    }
    if (index < 0) {
        index = count - 1;
    }
    if (index >= count) {
        index = 0;
    }
    s_view_index = index;
    album_update_pos();

    if (s_hint != NULL) {
        lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        ui_set_text(s_hint, "加载中...");
        lv_refr_now(NULL);
    }

    if (app_album_load(index, s_fb, UI_W, UI_H) != ESP_OK) {
        ESP_LOGW(TAG, "load %d failed", index);
        app_codec_fill(s_fb, UI_W, UI_H, UI_BG_565);
        if (s_hint != NULL) {
            ui_set_text(s_hint, "这张照片打不开");
        }
    } else if (s_hint != NULL) {
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_invalidate(s_canvas);
    album_fade_in(s_canvas);
}

static void album_bg_released(void)
{
    if (s_view != NULL) {
        lv_obj_add_flag(s_view, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_canvas != NULL) {
        lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    }
    s_fb = NULL;
    ESP_LOGW(TAG, "photo buffer released by player");
}

static void album_open_view(int index)
{
    if (s_canvas == NULL) {
        return;
    }
    if (s_fb == NULL) {
        s_fb = ui_bg_scratch();
        if (s_fb == NULL) {
            ESP_LOGE(TAG, "no psram for photo buffer, free=%uKB largest=%uKB",
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                     (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
            ui_nav_toast("内存不足，看不了大图");
            return;
        }
        app_codec_fill(s_fb, UI_W, UI_H, UI_BG_565);
        lv_canvas_set_buffer(s_canvas, s_fb, UI_W, UI_H, LV_IMG_CF_TRUE_COLOR);
    }
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_view);
    album_show_photo(index);
}

static void album_close_view(void)
{
    if (s_view != NULL) {
        lv_obj_add_flag(s_view, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_fb != NULL) {
        ui_bg_clobbered();
    }
    album_free_fb();
    if (s_grid != NULL) {
        lv_obj_clear_flag(s_grid, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_page_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void album_cell_cb(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);

    if (slot < 0 || slot >= ALBUM_PER_PAGE) {
        return;
    }
    if (s_cells[slot].index < 0) {
        return;
    }
    album_open_view(s_cells[slot].index);
}

static void album_prev_cb(lv_event_t *e)
{
    (void)e;
    album_show_photo(s_view_index - 1);
}

static void album_next_cb(lv_event_t *e)
{
    (void)e;
    album_show_photo(s_view_index + 1);
}

static void album_close_cb(lv_event_t *e)
{
    (void)e;
    album_close_view();
}

static void album_setbg_cb(lv_event_t *e)
{
    esp_err_t err;

    (void)e;

    if (s_view_index < 0 || s_view_index >= app_album_count() || s_fb == NULL) {
        ui_nav_toast("先打开一张照片");
        return;
    }
    err = ui_bg_save_scratch();
    if (err == ESP_OK) {
        ui_nav_toast("已设为首页背景");
    } else {
        ui_nav_toast("设置失败，检查存储卡");
    }
}

static void album_slide_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);

    s_slide_on = !s_slide_on;
    if (s_slide_on) {
        ui_set_text(btn, "幻灯片 开");
    } else {
        ui_set_text(btn, "幻灯片 关");
    }
    (void)btn;
}

static void album_page_build(void)
{
    int count = app_album_count();
    int base = s_page * ALBUM_PER_PAGE;
    char text[96];
    int shown = 0;
    int failed = 0;
    int i;

    album_free_cells();

    if (s_pages <= 0) {
        s_pages = 1;
    }
    snprintf(text, sizeof(text), "第 %d/%d 页", s_page + 1, s_pages);
    ui_set_text(s_page_label, text);

    for (i = 0; i < ALBUM_PER_PAGE; i++) {
        int index = base + i;
        esp_err_t terr;

        if (index >= count) {
            continue;
        }
        s_cells[i].buf = heap_caps_malloc((size_t)ALBUM_CELL_W * ALBUM_CELL_H * sizeof(uint16_t),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_cells[i].buf == NULL) {
            ESP_LOGW(TAG, "no psram for thumb %d", i);
            failed++;
            continue;
        }
        app_codec_fill(s_cells[i].buf, ALBUM_CELL_W, ALBUM_CELL_H, ALBUM_SLOT_565);
        terr = app_album_thumb(index, s_cells[i].buf, ALBUM_CELL_W, ALBUM_CELL_H);
        if (terr != ESP_OK) {
            heap_caps_free(s_cells[i].buf);
            s_cells[i].buf = NULL;
            failed++;
            ESP_LOGW(TAG, "skip [%d] %s (cannot decode)", index, app_album_name(index));
            continue;
        }

        s_cells[i].dsc.header.always_zero = 0;
        s_cells[i].dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        s_cells[i].dsc.header.w = ALBUM_CELL_W;
        s_cells[i].dsc.header.h = ALBUM_CELL_H;
        s_cells[i].dsc.data_size = (uint32_t)ALBUM_CELL_W * ALBUM_CELL_H * 2;
        s_cells[i].dsc.data = (const uint8_t *)s_cells[i].buf;
        s_cells[i].index = index;

        lv_img_set_src(s_cells[i].img, &s_cells[i].dsc);
        if (s_cells[i].tag != NULL) {
            lv_obj_add_flag(s_cells[i].tag, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(s_cells[i].box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(s_cells[i].box);
        lv_refr_now(NULL);
        shown++;
    }

    if (failed > 0) {
        snprintf(text, sizeof(text), "共 %d 张 · %d 张打不开(需转JPEG)", count, failed);
    } else {
        snprintf(text, sizeof(text), "共 %d 张", count);
    }
    ui_set_text(s_count, text);

    if (s_prev_page_btn != NULL && s_next_page_btn != NULL) {
        if (s_pages > 1) {
            lv_obj_clear_flag(s_prev_page_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_next_page_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_prev_page_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_next_page_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    ESP_LOGI(TAG, "page %d/%d: %d shown, %d skipped, total %d", s_page + 1, s_pages, shown, failed, count);
}

static void album_back_cb(lv_event_t *e)
{
    (void)e;
    album_close_view();
    album_free_cells();
    ui_nav_back();
}

static void album_prev_page_cb(lv_event_t *e)
{
    (void)e;
    if (s_page > 0) {
        s_page--;
        album_page_build();
    }
}

static void album_next_page_cb(lv_event_t *e)
{
    (void)e;
    if (s_page + 1 < s_pages) {
        s_page++;
        album_page_build();
    }
}

static void album_slide_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (!s_slide_on) {
        return;
    }
    if (s_view == NULL || lv_obj_has_flag(s_view, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    album_show_photo(s_view_index + 1);
}

static lv_obj_t *album_make_cell(lv_obj_t *parent, int slot)
{
    int col = slot % ALBUM_COLS;
    int row = slot / ALBUM_COLS;
    lv_coord_t x = (lv_coord_t)(ALBUM_X0 + col * (ALBUM_CELL_W + ALBUM_GAP));
    lv_coord_t y = (lv_coord_t)(ALBUM_Y0 + row * (ALBUM_CELL_H + ALBUM_GAP));
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_t *img;

    lv_obj_set_size(box, ALBUM_CELL_W, ALBUM_CELL_H);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_color(box, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(box, album_cell_cb, LV_EVENT_CLICKED, (void *)(intptr_t)slot);

    img = lv_img_create(box);
    lv_obj_set_pos(img, 0, 0);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);

    s_cells[slot].box = box;
    s_cells[slot].img = img;
    s_cells[slot].tag = ui_label(box, "", UI_FONT_SMALL, UI_COLOR_TEXT);
    lv_obj_align(s_cells[slot].tag, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_cells[slot].tag, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_cells[slot].tag, LV_OBJ_FLAG_CLICKABLE);
    s_cells[slot].buf = NULL;
    s_cells[slot].index = -1;
    return box;
}

static void album_build_view(lv_obj_t *parent)
{
    s_view = lv_obj_create(parent);
    lv_obj_set_size(s_view, UI_W, UI_H);
    lv_obj_set_pos(s_view, 0, 0);
    lv_obj_set_style_bg_color(s_view, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_view, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_view, 0, 0);
    lv_obj_set_style_radius(s_view, 0, 0);
    lv_obj_set_style_pad_all(s_view, 0, 0);
    lv_obj_clear_flag(s_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_view, LV_OBJ_FLAG_HIDDEN);

    s_canvas = lv_canvas_create(s_view);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);

    ui_button(s_view, "返回", 16, 12, 96, 44, album_close_cb, NULL);
    ui_button(s_view, "上一张", 16, 424, 120, 44, album_prev_cb, NULL);
    ui_button(s_view, "下一张", 148, 424, 120, 44, album_next_cb, NULL);
    ui_button(s_view, "设为首页背景", 280, 424, 176, 44, album_setbg_cb, NULL);
    s_slide_btn = ui_button(s_view, "幻灯片 关", 660, 424, 124, 44, album_slide_cb, NULL);

    s_pos_label = ui_label(s_view, "0 / 0", UI_FONT_TEXT, UI_COLOR_TEXT);
    lv_obj_set_pos(s_pos_label, 132, 20);

    s_hint = ui_label(s_view, "", UI_FONT_NAME, UI_COLOR_TEXT);
    lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

void ui_album_enter(void)
{
    int count;

    if (s_root == NULL) {
        return;
    }
    album_close_view();
    count = app_album_scan();
    ESP_LOGI(TAG, "enter: %d photo(s) in %s", count, app_album_dir());
    s_pages = (count + ALBUM_PER_PAGE - 1) / ALBUM_PER_PAGE;
    if (s_pages <= 0) {
        s_pages = 1;
    }
    s_page = 0;
    if (count == 0) {
        char text[96];

        album_free_cells();
        ui_set_text(s_count, "共 0 张");
        snprintf(text, sizeof(text), "%s 里没有 jpg/png 照片", app_album_dir());
        ui_set_text(s_page_label, text);
        return;
    }
    album_page_build();
}

static void album_watch_cb(lv_timer_t *timer)
{
    int count;

    (void)timer;

    if (s_root == NULL || lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (s_view != NULL && !lv_obj_has_flag(s_view, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (!app_album_take_dirty()) {
        return;
    }
    count = app_album_scan();
    ESP_LOGI(TAG, "rescan: %d photo(s)", count);
    s_pages = (count + ALBUM_PER_PAGE - 1) / ALBUM_PER_PAGE;
    if (s_pages <= 0) {
        s_pages = 1;
    }
    if (s_page >= s_pages) {
        s_page = s_pages - 1;
    }
    album_page_build();
}

lv_obj_t *ui_album_create(lv_obj_t *parent)
{
    lv_obj_t *title;
    int i;

    app_album_init();
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, UI_W, UI_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_grid = lv_obj_create(s_root);
    lv_obj_set_pos(s_grid, 0, 0);
    lv_obj_set_size(s_grid, UI_W, UI_H);
    lv_obj_set_style_bg_opa(s_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_grid, 0, 0);
    lv_obj_set_style_pad_all(s_grid, 0, 0);
    lv_obj_clear_flag(s_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_grid, LV_OBJ_FLAG_CLICKABLE);

    for (i = 0; i < ALBUM_PER_PAGE; i++) {
        album_make_cell(s_grid, i);
    }

    ui_button(s_root, "返回", 690, 12, 84, 44, album_back_cb, NULL);
    title = ui_label(s_root, "相册", UI_FONT_BIG, UI_COLOR_TEXT);
    lv_obj_set_pos(title, 28, 16);

    s_count = ui_label(s_root, "共 0 张", UI_FONT_SMALL, UI_COLOR_MUTED);
    lv_obj_set_pos(s_count, 116, 28);

    s_prev_page_btn = ui_button(s_root, "上一页", 28, 400, 120, 44, album_prev_page_cb, NULL);
    s_next_page_btn = ui_button(s_root, "下一页", 652, 400, 120, 44, album_next_page_cb, NULL);
    s_page_label = ui_label(s_root, "第 1/1 页", UI_FONT_TEXT, UI_COLOR_MUTED);
    lv_obj_set_pos(s_page_label, 348, 410);

    album_build_view(s_root);

    ui_bg_add_release_cb(album_bg_released);
    s_slide_timer = lv_timer_create(album_slide_timer_cb, ALBUM_SLIDE_MS, NULL);
    lv_timer_create(album_watch_cb, 600, NULL);

    ESP_LOGI(TAG, "ready");
    return s_root;
}
