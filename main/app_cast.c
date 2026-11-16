#include "app_cast.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lv_port_disp.h"
#include "tjpgd.h"

#define CAST_SCREEN_W   (LCD_WIDTH)
#define CAST_SCREEN_H   (LCD_HEIGHT)
#define CAST_JPEG_MAX   (128 * 1024)
#define CAST_DEC_W_MAX  (640)
#define CAST_DEC_H_MAX  (480)
#define CAST_POOL_SIZE  (16 * 1024)
#define CAST_POOL_INTERNAL_NEED (96 * 1024)
#define CAST_SETTLE_MS  (60)

static const char *TAG = "cast";

static uint8_t *s_jpeg;
static uint8_t *s_pool;
static uint16_t *s_dec;
static uint16_t *s_fb;
static uint16_t s_xmap[CAST_SCREEN_W];
static size_t s_dec_cap;
static uint16_t s_dec_w;
static uint16_t s_dec_h;
static uint16_t s_box_w;
static uint16_t s_box_h;
static size_t s_jpg_pos;
static size_t s_jpg_len;
static volatile bool s_active;
static bool s_ready;

static size_t cast_read(JDEC *jd, uint8_t *buf, size_t len)
{
    (void)jd;

    if (s_jpg_pos >= s_jpg_len) {
        return 0;
    }
    if (len > s_jpg_len - s_jpg_pos) {
        len = s_jpg_len - s_jpg_pos;
    }
    if (buf != NULL) {
        memcpy(buf, s_jpeg + s_jpg_pos, len);
    }
    s_jpg_pos += len;
    return len;
}

static int cast_write(JDEC *jd, void *bitmap, JRECT *rect)
{
    const uint16_t *src = (const uint16_t *)bitmap;
    uint16_t w = (uint16_t)(rect->right - rect->left + 1);
    uint16_t h = (uint16_t)(rect->bottom - rect->top + 1);
    uint16_t y;

    (void)jd;

    for (y = 0; y < h; y++) {
        uint16_t *dst = s_dec + (size_t)(rect->top + y) * s_dec_w + rect->left;
        memcpy(dst, src, (size_t)w * sizeof(uint16_t));
        src += w;
    }
    return 1;
}

static uint8_t *cast_pool(void)
{
    if (s_pool != NULL) {
        return s_pool;
    }
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > CAST_POOL_INTERNAL_NEED) {
        s_pool = heap_caps_malloc(CAST_POOL_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_pool == NULL) {
        s_pool = heap_caps_malloc(CAST_POOL_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (s_pool == NULL) {
        ESP_LOGE(TAG, "decode pool alloc failed");
    }
    return s_pool;
}

static uint16_t *cast_fb(void)
{
    esp_lcd_panel_handle_t panel;
    void *fb = NULL;

    if (s_fb != NULL) {
        return s_fb;
    }
    panel = lv_port_disp_get_panel_handle();
    if (panel == NULL) {
        return NULL;
    }
    if (esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb) != ESP_OK || fb == NULL) {
        ESP_LOGE(TAG, "frame buffer unavailable");
        return NULL;
    }
    s_fb = (uint16_t *)fb;
    return s_fb;
}

static uint16_t *cast_dec(size_t pixels)
{
    size_t bytes = pixels * sizeof(uint16_t);

    if (s_dec != NULL && s_dec_cap >= bytes) {
        return s_dec;
    }
    if (s_dec != NULL) {
        heap_caps_free(s_dec);
        s_dec = NULL;
        s_dec_cap = 0;
    }
    s_dec = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_dec == NULL) {
        ESP_LOGE(TAG, "decode buffer %u bytes failed", (unsigned)bytes);
        return NULL;
    }
    s_dec_cap = bytes;
    return s_dec;
}

static void cast_blank(void)
{
    if (s_fb == NULL) {
        return;
    }
    memset(s_fb, 0, (size_t)CAST_SCREEN_W * CAST_SCREEN_H * sizeof(uint16_t));
    s_box_w = 0;
    s_box_h = 0;
}

static esp_err_t cast_blit(void)
{
    uint32_t fit_x;
    uint32_t fit_y;
    uint32_t fit;
    uint16_t out_w;
    uint16_t out_h;
    uint16_t ox;
    uint16_t oy;
    uint16_t x;
    uint16_t y;

    if (s_fb == NULL || s_dec_w == 0 || s_dec_h == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    fit_x = ((uint32_t)CAST_SCREEN_W << 16) / s_dec_w;
    fit_y = ((uint32_t)CAST_SCREEN_H << 16) / s_dec_h;
    fit = (fit_x < fit_y) ? fit_x : fit_y;

    out_w = (uint16_t)(((uint32_t)s_dec_w * fit) >> 16);
    out_h = (uint16_t)(((uint32_t)s_dec_h * fit) >> 16);
    if (out_w > CAST_SCREEN_W) {
        out_w = CAST_SCREEN_W;
    }
    if (out_h > CAST_SCREEN_H) {
        out_h = CAST_SCREEN_H;
    }
    if (out_w == 0 || out_h == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    ox = (uint16_t)((CAST_SCREEN_W - out_w) / 2);
    oy = (uint16_t)((CAST_SCREEN_H - out_h) / 2);

    if (out_w != s_box_w || out_h != s_box_h) {
        cast_blank();
        s_box_w = out_w;
        s_box_h = out_h;
    }

    for (x = 0; x < out_w; x++) {
        s_xmap[x] = (uint16_t)(((uint32_t)x * s_dec_w) / out_w);
    }

    for (y = 0; y < out_h; y++) {
        const uint16_t *row = s_dec + (size_t)(((uint32_t)y * s_dec_h) / out_h) * s_dec_w;
        uint16_t *dst = s_fb + (size_t)(oy + y) * CAST_SCREEN_W + ox;

        for (x = 0; x < out_w; x++) {
            dst[x] = row[s_xmap[x]];
        }
    }

    return ESP_OK;
}

esp_err_t app_cast_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    s_ready = true;
    ESP_LOGI(TAG, "ready, buffers allocate on first frame");
    return ESP_OK;
}

bool app_cast_active(void)
{
    return s_active;
}

void app_cast_stop(void)
{
    s_active = false;
}

uint8_t *app_cast_alloc_frame(size_t *capacity)
{
    if (!s_ready) {
        return NULL;
    }
    if (s_jpeg == NULL) {
        s_jpeg = heap_caps_malloc(CAST_JPEG_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_jpeg == NULL) {
            s_jpeg = heap_caps_malloc(CAST_JPEG_MAX, MALLOC_CAP_8BIT);
        }
    }
    if (s_jpeg == NULL) {
        ESP_LOGE(TAG, "jpeg buffer alloc failed");
        return NULL;
    }
    if (capacity != NULL) {
        *capacity = CAST_JPEG_MAX;
    }
    return s_jpeg;
}

esp_err_t app_cast_submit_frame(size_t length)
{
    JDEC jd;
    JRESULT rc;
    uint8_t *pool;
    uint8_t scale;
    size_t pixels;

    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (length < 8 || length > CAST_JPEG_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!s_active) {
        s_active = true;
        vTaskDelay(pdMS_TO_TICKS(CAST_SETTLE_MS));
        if (cast_fb() == NULL) {
            s_active = false;
            return ESP_ERR_INVALID_STATE;
        }
        cast_blank();
    }

    pool = cast_pool();
    if (pool == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_jpg_pos = 0;
    s_jpg_len = length;

    memset(&jd, 0, sizeof(jd));
    rc = jd_prepare(&jd, cast_read, pool, CAST_POOL_SIZE, NULL);
    if (rc != JDR_OK) {
        ESP_LOGW(TAG, "prepare failed: %d", (int)rc);
        return ESP_FAIL;
    }

    scale = 0;
    while (scale < 3 &&
           ((jd.width >> scale) > CAST_DEC_W_MAX || (jd.height >> scale) > CAST_DEC_H_MAX)) {
        scale++;
    }

    s_dec_w = (uint16_t)(jd.width >> scale);
    s_dec_h = (uint16_t)(jd.height >> scale);
    if (s_dec_w == 0 || s_dec_h == 0 || s_dec_w > CAST_DEC_W_MAX || s_dec_h > CAST_DEC_H_MAX) {
        ESP_LOGW(TAG, "unsupported size %ux%u scale %u", (unsigned)jd.width, (unsigned)jd.height, (unsigned)scale);
        return ESP_ERR_INVALID_SIZE;
    }

    pixels = (size_t)s_dec_w * s_dec_h;
    if (cast_dec(pixels) == NULL) {
        return ESP_ERR_NO_MEM;
    }

    rc = jd_decomp(&jd, cast_write, scale);
    if (rc != JDR_OK) {
        ESP_LOGW(TAG, "decode failed: %d", (int)rc);
        return ESP_FAIL;
    }

    return cast_blit();
}
