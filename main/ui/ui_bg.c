#include "ui_bg.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_store.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ui_theme.h"

#define UI_BG_BYTES ((size_t)UI_BG_W * (size_t)UI_BG_H * 2u)

static const char *TAG = "uibg";

static uint16_t *s_buf;
static lv_img_dsc_t s_dsc;
static bool s_loaded;
static bool s_clobbered;
static bool s_dsc_ready;
static void (*s_release_cbs[4])(void);
static int s_release_cb_n;

static void bg_dsc_bind(uint16_t *buf)
{
    if (buf == NULL) {
        s_dsc_ready = false;
        return;
    }
    s_dsc.header.always_zero = 0;
    s_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_dsc.header.w = UI_BG_W;
    s_dsc.header.h = UI_BG_H;
    s_dsc.data_size = (uint32_t)UI_BG_BYTES;
    s_dsc.data = (const uint8_t *)buf;
    s_dsc_ready = true;
}

static bool bg_path(char *out, size_t size)
{
    int n;

    if (!app_store_ready()) {
        return false;
    }
    n = snprintf(out, size, "%s/.bg.565", app_store_mount());
    return (n > 0) && ((size_t)n < size);
}

uint16_t *ui_bg_scratch(void)
{
    if (s_buf != NULL) {
        return s_buf;
    }
    s_buf = heap_caps_malloc(UI_BG_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_buf == NULL) {
        ESP_LOGE(TAG, "no psram for %u bytes", (unsigned)UI_BG_BYTES);
        return NULL;
    }
    ESP_LOGI(TAG, "scratch %u bytes ready", (unsigned)UI_BG_BYTES);
    return s_buf;
}

bool ui_bg_exists(void)
{
    char path[96];
    struct stat st;

    if (!bg_path(path, sizeof(path))) {
        return false;
    }
    if (stat(path, &st) != 0) {
        return false;
    }
    return st.st_size == (off_t)UI_BG_BYTES;
}

bool ui_bg_needs_load(void)
{
    return ui_bg_exists() && (!s_loaded || s_clobbered);
}

esp_err_t ui_bg_load(void)
{
    char path[96];
    FILE *fp;
    size_t got;
    uint16_t *buf = ui_bg_scratch();

    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (!bg_path(path, sizeof(path))) {
        return ESP_ERR_INVALID_STATE;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    got = fread(buf, 1, UI_BG_BYTES, fp);
    if (got != UI_BG_BYTES) {
        ESP_LOGW(TAG, "read %u/%u bytes, retrying", (unsigned)got, (unsigned)UI_BG_BYTES);
        if (fseek(fp, 0, SEEK_SET) == 0) {
            got = fread(buf, 1, UI_BG_BYTES, fp);
        }
    }
    fclose(fp);
    if (got != UI_BG_BYTES) {
        ESP_LOGW(TAG, "read %u/%u bytes, keep file and retry later", (unsigned)got,
                 (unsigned)UI_BG_BYTES);
        s_loaded = false;
        s_dsc_ready = false;
        return ESP_ERR_INVALID_SIZE;
    }

    bg_dsc_bind(buf);
    s_loaded = true;
    s_clobbered = false;
    ESP_LOGI(TAG, "wallpaper loaded");
    return ESP_OK;
}

esp_err_t ui_bg_save_scratch(void)
{
    char path[96];
    char tmp[96];
    FILE *fp;
    size_t put;

    if (s_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (!bg_path(path, sizeof(path))) {
        return ESP_ERR_INVALID_STATE;
    }
    snprintf(tmp, sizeof(tmp), "%s/.bg.tmp", app_store_mount());
    fp = fopen(tmp, "wb");
    if (fp == NULL) {
        ESP_LOGW(TAG, "open %s failed", tmp);
        return ESP_FAIL;
    }
    put = fwrite(s_buf, 1, UI_BG_BYTES, fp);
    fclose(fp);
    if (put != UI_BG_BYTES) {
        remove(tmp);
        ESP_LOGW(TAG, "write %u/%u", (unsigned)put, (unsigned)UI_BG_BYTES);
        return ESP_FAIL;
    }
    if (rename(tmp, path) != 0) {
        remove(path);
        if (rename(tmp, path) != 0) {
            remove(tmp);
            ESP_LOGW(TAG, "rename failed");
            return ESP_FAIL;
        }
    }
    bg_dsc_bind(s_buf);
    s_loaded = true;
    s_clobbered = false;
    ESP_LOGI(TAG, "wallpaper saved");
    return ESP_OK;
}

esp_err_t ui_bg_reserve(void)
{
    if (ui_bg_scratch() == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "canvas reserved for wallpaper and photo viewer");
    return ESP_OK;
}

esp_err_t ui_bg_clear(void)
{
    char path[96];

    if (!bg_path(path, sizeof(path))) {
        return ESP_ERR_INVALID_STATE;
    }
    if (remove(path) != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    s_loaded = false;
    ESP_LOGI(TAG, "wallpaper cleared");
    return ESP_OK;
}

void ui_bg_clobbered(void)
{
    s_clobbered = true;
}

void ui_bg_add_release_cb(void (*cb)(void))
{
    if (cb == NULL || s_release_cb_n >= (int)(sizeof(s_release_cbs) / sizeof(s_release_cbs[0]))) {
        return;
    }
    s_release_cbs[s_release_cb_n++] = cb;
}

void ui_bg_release(void)
{
    int i;

    if (s_buf != NULL) {
        heap_caps_free(s_buf);
        s_buf = NULL;
        s_loaded = false;
        s_dsc_ready = false;
        s_clobbered = true;
        ESP_LOGI(TAG, "wallpaper buffer released");
    }
    for (i = 0; i < s_release_cb_n; i++) {
        s_release_cbs[i]();
    }
}

const lv_img_dsc_t *ui_bg_dsc(void)
{
    if (!s_dsc_ready || s_clobbered || !s_loaded) {
        return NULL;
    }
    return &s_dsc;
}
