#include "ui_features.h"

#if UI_CALC_ENABLE

#include "ui_calc.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "ui_nav.h"
#include "ui_theme.h"

#define C_BG      lv_color_hex(0xF5F6F8)
#define C_TEXT    lv_color_hex(0x111114)
#define C_MUTED   lv_color_hex(0x9AA0A6)
#define C_DIGIT   lv_color_hex(0xFFFFFF)
#define C_FUNC    lv_color_hex(0xE3E6EB)
#define C_OP      lv_color_hex(0x2F7CF6)
#define C_EQ      lv_color_hex(0xFF9500)
#define C_WHITE   lv_color_hex(0xFFFFFF)

#define CALC_COLS     (4)
#define CALC_ROWS     (5)
#define CALC_X        (24)
#define CALC_Y        (196)
#define CALC_GAP      (8)
#define CALC_BTN_W    (182)
#define CALC_BTN_H    (50)
#define CALC_BUF      (24)

static const char *TAG = "uicalc";

static lv_obj_t *s_root;
static lv_obj_t *s_display;
static lv_obj_t *s_history;
static char s_buf[CALC_BUF];
static char s_hist[64];
static double s_acc;
static double s_value;
static char s_op;
static bool s_typing;
static bool s_error;

typedef struct {
    const char *text;
    char key;
    uint32_t color;
    uint32_t text_color;
    int span;
} calc_key_t;

static const calc_key_t s_keys[CALC_ROWS * CALC_COLS] = {
    {"AC", 'C', 0xE3E6EB, 0x111114, 1},
    {"+/-", 'N', 0xE3E6EB, 0x111114, 1},
    {"%", 'P', 0xE3E6EB, 0x111114, 1},
    {"÷", '/', 0x2F7CF6, 0xFFFFFF, 1},
    {"7", '7', 0xFFFFFF, 0x111114, 1},
    {"8", '8', 0xFFFFFF, 0x111114, 1},
    {"9", '9', 0xFFFFFF, 0x111114, 1},
    {"×", '*', 0x2F7CF6, 0xFFFFFF, 1},
    {"4", '4', 0xFFFFFF, 0x111114, 1},
    {"5", '5', 0xFFFFFF, 0x111114, 1},
    {"6", '6', 0xFFFFFF, 0x111114, 1},
    {"−", '-', 0x2F7CF6, 0xFFFFFF, 1},
    {"1", '1', 0xFFFFFF, 0x111114, 1},
    {"2", '2', 0xFFFFFF, 0x111114, 1},
    {"3", '3', 0xFFFFFF, 0x111114, 1},
    {"+", '+', 0x2F7CF6, 0xFFFFFF, 1},
    {"0", '0', 0xFFFFFF, 0x111114, 2},
    {".", '.', 0xFFFFFF, 0x111114, 1},
    {"=", '=', 0xFF9500, 0xFFFFFF, 1},
};

static void calc_back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void calc_fmt(double v, char *out, size_t size)
{
    if (v != v) {
        snprintf(out, size, "错误");
        return;
    }
    if (v > 1e15 || v < -1e15) {
        snprintf(out, size, "溢出");
        return;
    }
    snprintf(out, size, "%.10g", v);
}

static double calc_apply(double a, char op, double b, bool *ok)
{
    *ok = true;
    switch (op) {
    case '+':
        return a + b;
    case '-':
        return a - b;
    case '*':
        return a * b;
    case '/':
        if (b == 0.0) {
            *ok = false;
            return 0.0;
        }
        return a / b;
    default:
        return b;
    }
}

static double calc_current(void)
{
    if (s_typing) {
        return strtod(s_buf, NULL);
    }
    return s_value;
}

static void calc_show(void)
{
    char text[CALC_BUF];
    size_t len;

    if (s_error) {
        ui_set_text(s_display, "错误");
        lv_obj_set_style_text_font(s_display, UI_FONT_BIG, 0);
        ui_set_text(s_history, "");
        return;
    }
    if (s_typing) {
        snprintf(text, sizeof(text), "%s", s_buf);
    } else {
        calc_fmt(s_value, text, sizeof(text));
    }
    len = strlen(text);
    if (len <= 8) {
        lv_obj_set_style_text_font(s_display, UI_FONT_DIGIT, 0);
        lv_obj_align(s_display, LV_ALIGN_TOP_RIGHT, -28, 2);
    } else if (len <= 13) {
        lv_obj_set_style_text_font(s_display, UI_FONT_SEC, 0);
        lv_obj_align(s_display, LV_ALIGN_TOP_RIGHT, -28, 59);
    } else {
        lv_obj_set_style_text_font(s_display, UI_FONT_BIG, 0);
        lv_obj_align(s_display, LV_ALIGN_TOP_RIGHT, -28, 80);
    }
    ui_set_text(s_display, text);
}

static void calc_press(char key)
{
    char text[CALC_BUF];
    char left[CALC_BUF];
    char right[CALC_BUF];
    double v;
    bool ok;

    if (key == 'C') {
        s_buf[0] = '\0';
        s_hist[0] = '\0';
        s_acc = 0.0;
        s_value = 0.0;
        s_op = 0;
        s_typing = false;
        s_error = false;
        ui_set_text(s_history, "");
        calc_show();
        return;
    }
    if (s_error) {
        return;
    }

    if (key >= '0' && key <= '9') {
        if (!s_typing) {
            s_buf[0] = '\0';
            s_typing = true;
        }
        if (strlen(s_buf) < CALC_BUF - 1) {
            size_t n = strlen(s_buf);

            s_buf[n] = key;
            s_buf[n + 1] = '\0';
        }
        calc_show();
        return;
    }

    if (key == '.') {
        if (!s_typing) {
            snprintf(s_buf, sizeof(s_buf), "0.");
            s_typing = true;
        } else if (strchr(s_buf, '.') == NULL && strlen(s_buf) < CALC_BUF - 1) {
            strcat(s_buf, ".");
        }
        calc_show();
        return;
    }

    if (key == 'N') {
        if (s_typing) {
            if (s_buf[0] == '-') {
                memmove(s_buf, s_buf + 1, strlen(s_buf));
            } else if (strlen(s_buf) < CALC_BUF - 1) {
                memmove(s_buf + 1, s_buf, strlen(s_buf) + 1);
                s_buf[0] = '-';
            }
        } else {
            s_value = -s_value;
        }
        calc_show();
        return;
    }

    if (key == 'P') {
        v = calc_current() / 100.0;
        s_typing = false;
        s_value = v;
        calc_show();
        return;
    }

    if (key == '+' || key == '-' || key == '*' || key == '/') {
        if (s_op != 0 && s_typing) {
            s_acc = calc_apply(s_acc, s_op, calc_current(), &ok);
            if (!ok) {
                s_error = true;
                calc_show();
                return;
            }
            calc_fmt(s_acc, s_buf, sizeof(s_buf));
            s_value = s_acc;
        } else if (s_typing) {
            s_acc = calc_current();
            s_value = s_acc;
        } else {
            s_acc = s_value;
        }
        s_typing = false;
        s_op = key;
        calc_fmt(s_acc, left, sizeof(left));
        snprintf(s_hist, sizeof(s_hist), "%s %c", left, s_op);
        ui_set_text(s_history, s_hist);
        calc_show();
        return;
    }

    if (key == '=') {
        if (s_op == 0) {
            s_typing = false;
            s_value = calc_current();
            calc_show();
            return;
        }
        v = calc_current();
        calc_fmt(s_acc, left, sizeof(left));
        calc_fmt(v, right, sizeof(right));
        snprintf(s_hist, sizeof(s_hist), "%s %c %s =", left, s_op, right);
        s_acc = calc_apply(s_acc, s_op, v, &ok);
        s_op = 0;
        s_typing = false;
        s_value = s_acc;
        if (!ok) {
            s_error = true;
            calc_show();
            return;
        }
        calc_fmt(s_value, text, sizeof(text));
        ui_set_text(s_history, s_hist);
        calc_show();
        return;
    }
}

static void calc_key_cb(lv_event_t *e)
{
    char key = (char)(intptr_t)lv_event_get_user_data(e);

    calc_press(key);
}

static lv_obj_t *calc_button(lv_obj_t *parent, const calc_key_t *k, lv_coord_t x, lv_coord_t y, lv_coord_t w)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_t *label;
    bool is_op = (k->key == '/' || k->key == '*' || k->key == '-' || k->key == '+' || k->key == '=');

    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, CALC_BTN_H);
    lv_obj_set_style_bg_color(btn, lv_color_hex(k->color), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xC9CDD4), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, calc_key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)k->key);

    label = ui_label(btn, k->text, is_op ? UI_FONT_BIG : UI_FONT_TEXT, lv_color_hex(k->text_color));
    lv_obj_center(label);
    return btn;
}

lv_obj_t *ui_calc_create(lv_obj_t *parent)
{
    int row;
    int col;
    int i;

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, UI_W, UI_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, C_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    {
        lv_obj_t *back = lv_obj_create(s_root);
        lv_obj_t *label;

        lv_obj_set_pos(back, 20, 16);
        lv_obj_set_size(back, 84, 40);
        lv_obj_set_style_bg_color(back, C_FUNC, 0);
        lv_obj_set_style_bg_color(back, C_MUTED, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(back, 0, 0);
        lv_obj_set_style_radius(back, 12, 0);
        lv_obj_set_style_pad_all(back, 0, 0);
        lv_obj_clear_flag(back, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(back, calc_back_cb, LV_EVENT_CLICKED, NULL);
        label = ui_label(back, "返回", UI_FONT_SMALL, C_TEXT);
        lv_obj_center(label);
    }

    s_history = ui_label(s_root, "", UI_FONT_TEXT, C_MUTED);
    lv_obj_align(s_history, LV_ALIGN_TOP_LEFT, 28, 30);

    s_display = ui_label(s_root, "0", UI_FONT_DIGIT, C_TEXT);
    lv_obj_align(s_display, LV_ALIGN_TOP_RIGHT, -28, 2);

    i = 0;
    for (row = 0; row < CALC_ROWS; row++) {
        col = 0;
        for (; col < CALC_COLS && i < CALC_ROWS * CALC_COLS; col++) {
            const calc_key_t *k = &s_keys[i++];
            lv_coord_t x = CALC_X + col * (CALC_BTN_W + CALC_GAP);
            lv_coord_t y = CALC_Y + row * (CALC_BTN_H + CALC_GAP);
            lv_coord_t w = CALC_BTN_W;

            if (k->span == 2) {
                w = CALC_BTN_W * 2 + CALC_GAP;
                col++;
            }
            calc_button(s_root, k, x, y, w);
        }
    }

    s_buf[0] = '\0';
    s_hist[0] = '\0';
    s_acc = 0.0;
    s_value = 0.0;
    s_op = 0;
    s_typing = false;
    s_error = false;

    ESP_LOGI(TAG, "ready");
    return s_root;
}

void ui_calc_enter(void)
{
    char text[CALC_BUF];

    if (s_root == NULL) {
        return;
    }
    s_buf[0] = '\0';
    s_hist[0] = '\0';
    s_acc = 0.0;
    s_value = 0.0;
    s_op = 0;
    s_typing = false;
    s_error = false;
    snprintf(text, sizeof(text), "0");
    snprintf(s_buf, sizeof(s_buf), "%s", text);
    s_buf[0] = '\0';
    ui_set_text(s_history, "");
    calc_show();
}

#else

typedef int ui_calc_disabled_t;

#endif
