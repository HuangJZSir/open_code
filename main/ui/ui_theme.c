#include "ui_theme.h"

#include <string.h>

lv_obj_t *ui_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 14, 0);
    lv_obj_set_style_pad_all(obj, 12, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                    lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);

    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_2, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 11, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *label = ui_label(btn, text, UI_FONT_TEXT, UI_COLOR_TEXT);

    (void)label;
    lv_obj_center(label);

    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

void ui_object_bg(lv_obj_t *obj, lv_color_t color, lv_opa_t opa, lv_coord_t radius)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *ui_icon_button(lv_obj_t *parent, const void *icon, lv_coord_t x, lv_coord_t y, lv_coord_t size,
                         lv_color_t bg, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_t *img;

    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, size, size);
    ui_object_bg(btn, bg, LV_OPA_COVER, size / 2);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

    img = lv_img_create(btn);
    lv_img_set_src(img, icon);
    lv_obj_center(img);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);

    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

void ui_icon_set(lv_obj_t *image, const void *icon)
{
    if (image == NULL) {
        return;
    }
    lv_img_set_src(image, icon);
}

void ui_set_text(lv_obj_t *label, const char *text)
{
    if (label == NULL) {
        return;
    }
    if (text == NULL) {
        text = "";
    }
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

void ui_msgbox_fonts(lv_obj_t *mbox)
{
    lv_obj_t *title;
    lv_obj_t *text;
    lv_obj_t *btns;

    if (mbox == NULL) {
        return;
    }

    title = lv_msgbox_get_title(mbox);
    text = lv_msgbox_get_text(mbox);
    btns = lv_msgbox_get_btns(mbox);

    if (title != NULL) {
        lv_obj_set_style_text_font(title, UI_FONT_TEXT, 0);
        lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    }
    if (text != NULL) {
        lv_obj_set_style_text_font(text, UI_FONT_TEXT, 0);
        lv_obj_set_style_text_color(text, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (btns != NULL) {
        lv_obj_set_style_text_font(btns, UI_FONT_TEXT, 0);
        lv_obj_set_style_text_color(btns, UI_COLOR_TEXT, 0);
    }
}

void ui_theme_init(void)
{
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *theme;
    lv_obj_t *scr;

    if (disp == NULL) {
        return;
    }

    theme = lv_theme_default_init(disp, UI_COLOR_ACCENT, UI_COLOR_OK, true, UI_FONT_SMALL);
    if (theme != NULL) {
        lv_disp_set_theme(disp, theme);
    }

    scr = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}
