#pragma once

#include "lvgl.h"

#define UI_W 800
#define UI_H 480
#define UI_BG_565 0x1082

#define UI_COLOR_BG        lv_color_hex(0x0d0f13)
#define UI_COLOR_CARD      lv_color_hex(0x171a21)
#define UI_COLOR_CARD_2    lv_color_hex(0x222833)
#define UI_COLOR_TEXT      lv_color_hex(0xe6e8ee)
#define UI_COLOR_MUTED     lv_color_hex(0x8b93a7)
#define UI_COLOR_ACCENT    lv_color_hex(0x2f7cf6)
#define UI_COLOR_BORDER    lv_color_hex(0x2e3542)
#define UI_COLOR_OK        lv_color_hex(0x33c07a)
#define UI_COLOR_ERR       lv_color_hex(0xe35c5c)

#define UI_COLOR_TILE_BLUE   0x2f7cf6
#define UI_COLOR_TILE_RED    0xef5b5b
#define UI_COLOR_TILE_GREEN  0x33c07a
#define UI_COLOR_TILE_AMBER  0xf0a93b
#define UI_COLOR_TILE_INDIGO 0x6f7bf7
#define UI_COLOR_TILE_CYAN   0x22b8cf
#define UI_COLOR_TILE_GRAY   0x5b6478
#define UI_COLOR_TILE_PURPLE 0xa78bfa

LV_FONT_DECLARE(ui_font_16)
LV_FONT_DECLARE(ui_font_24)
LV_FONT_DECLARE(ui_font_32)
LV_FONT_DECLARE(ui_font_cjk24)

#define UI_FONT_SMALL &ui_font_16
#define UI_FONT_TEXT  &ui_font_24
#define UI_FONT_BIG   &ui_font_32
#define UI_FONT_NAME  &ui_font_cjk24
#define UI_FONT_CLOCK &lv_font_montserrat_36
#define UI_FONT_NUM   &lv_font_montserrat_22

lv_obj_t *ui_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h);
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color);
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                    lv_event_cb_t cb, void *user_data);
lv_obj_t *ui_icon_button(lv_obj_t *parent, const void *icon, lv_coord_t x, lv_coord_t y, lv_coord_t size,
                         lv_color_t bg, lv_event_cb_t cb, void *user_data);
void ui_icon_set(lv_obj_t *image, const void *icon);
void ui_object_bg(lv_obj_t *obj, lv_color_t color, lv_opa_t opa, lv_coord_t radius);
void ui_set_text(lv_obj_t *label, const char *text);
void ui_theme_init(void);
void ui_msgbox_fonts(lv_obj_t *mbox);
