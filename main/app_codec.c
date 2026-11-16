#include "app_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lodepng.h"
#include "tjpgd.h"

#define APP_CODEC_WORK_SIZE (32 * 1024)
#define APP_CODEC_IO_SIZE   (32768)
#define APP_CODEC_PNG_LIMIT (1536 * 1024)

typedef struct {
    FILE *fp;
    uint16_t *dst;
    int dst_w;
    int dst_h;
    int img_w;
    int img_h;
    int16_t *col_map;
    int16_t *row_map;
} codec_ctx_t;

static const char *TAG = "codec";

void app_codec_fill(uint16_t *dst, int width, int height, uint16_t color)
{
    uint32_t pattern = ((uint32_t)color << 16) | color;
    uint32_t *p = (uint32_t *)dst;
    size_t count = (size_t)width * (size_t)height / 2;
    size_t i;

    if (dst == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        p[i] = pattern;
    }
}

static bool read_head(const char *path, uint8_t *head, size_t len)
{
    FILE *fp = fopen(path, "rb");
    bool ok;

    if (fp == NULL) {
        return false;
    }
    ok = fread(head, 1, len, fp) == len;
    fclose(fp);
    return ok;
}

static bool is_png(const char *path)
{
    uint8_t head[4];

    if (!read_head(path, head, sizeof(head))) {
        return false;
    }
    return head[0] == 0x89 && head[1] == 0x50 && head[2] == 0x4E && head[3] == 0x47;
}

static esp_err_t png_info(const char *path, uint16_t *width, uint16_t *height)
{
    uint8_t head[24];
    uint32_t w;
    uint32_t h;

    if (!read_head(path, head, sizeof(head))) {
        return ESP_ERR_NOT_FOUND;
    }
    if (head[12] != 'I' || head[13] != 'H' || head[14] != 'D' || head[15] != 'R') {
        return ESP_ERR_INVALID_RESPONSE;
    }
    w = ((uint32_t)head[16] << 24) | ((uint32_t)head[17] << 16) | ((uint32_t)head[18] << 8) | head[19];
    h = ((uint32_t)head[20] << 24) | ((uint32_t)head[21] << 16) | ((uint32_t)head[22] << 8) | head[23];
    if (w == 0 || h == 0 || w > 0xFFFF || h > 0xFFFF) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *width = (uint16_t)w;
    *height = (uint16_t)h;
    return ESP_OK;
}

static esp_err_t jpeg_info_sof(const char *path, uint16_t *width, uint16_t *height, uint8_t *sof)
{
    FILE *fp;
    int c;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (fgetc(fp) != 0xFF || fgetc(fp) != 0xD8) {
        fclose(fp);
        return ESP_ERR_INVALID_RESPONSE;
    }

    for (;;) {
        c = fgetc(fp);
        if (c == EOF) {
            break;
        }
        if (c != 0xFF) {
            continue;
        }
        do {
            c = fgetc(fp);
        } while (c == 0xFF);
        if (c == EOF) {
            break;
        }
        if (c == 0xD8 || c == 0xD9 || (c >= 0xD0 && c <= 0xD7) || c == 0x01) {
            continue;
        }
        {
            int hi = fgetc(fp);
            int lo = fgetc(fp);
            int len = (hi << 8) | lo;

            if (hi == EOF || lo == EOF || len < 2) {
                break;
            }
            if ((c >= 0xC0 && c <= 0xC3) || (c >= 0xC5 && c <= 0xC7) ||
                (c >= 0xC9 && c <= 0xCB) || (c >= 0xCD && c <= 0xCF)) {
                int prec = fgetc(fp);
                int h1 = fgetc(fp);
                int h2 = fgetc(fp);
                int w1 = fgetc(fp);
                int w2 = fgetc(fp);

                (void)prec;
                *height = (uint16_t)((h1 << 8) | h2);
                *width = (uint16_t)((w1 << 8) | w2);
                if (sof != NULL) {
                    *sof = (uint8_t)c;
                }
                fclose(fp);
                if (*width == 0 || *height == 0) {
                    return ESP_ERR_INVALID_RESPONSE;
                }
                return ESP_OK;
            }
            if (c == 0xDA) {
                break;
            }
            fseek(fp, (long)(len - 2), SEEK_CUR);
        }
    }

    fclose(fp);
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t jpeg_info(const char *path, uint16_t *width, uint16_t *height)
{
    return jpeg_info_sof(path, width, height, NULL);
}

esp_err_t app_codec_info(const char *path, uint16_t *width, uint16_t *height)
{
    if (path == NULL || width == NULL || height == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (is_png(path)) {
        return png_info(path, width, height);
    }
    return jpeg_info(path, width, height);
}

bool app_codec_supported(const char *name)
{
    static const char *const exts[] = {".jpg", ".jpeg", ".png"};
    const char *dot;
    int i;

    if (name == NULL) {
        return false;
    }
    dot = strrchr(name, '.');
    if (dot == NULL) {
        return false;
    }
    for (i = 0; i < (int)(sizeof(exts) / sizeof(exts[0])); i++) {
        if (strcasecmp(dot, exts[i]) == 0) {
            return true;
        }
    }
    return false;
}

static size_t codec_in(JDEC *jd, uint8_t *buff, size_t ndata)
{
    codec_ctx_t *ctx = (codec_ctx_t *)jd->device;

    if (buff != NULL) {
        return fread(buff, 1, ndata, ctx->fp);
    }
    fseek(ctx->fp, (long)ndata, SEEK_CUR);
    return ndata;
}

static int codec_out(JDEC *jd, void *bitmap, JRECT *rect)
{
    codec_ctx_t *ctx = (codec_ctx_t *)jd->device;
    const uint16_t *src = (const uint16_t *)bitmap;
    int w = rect->right - rect->left + 1;
    int h = rect->bottom - rect->top + 1;
    int y;

    for (y = 0; y < h; y++) {
        int sy = rect->top + y;
        int dy;
        const uint16_t *row;
        uint16_t *out;
        int x;

        if (sy < 0 || sy >= ctx->img_h) {
            continue;
        }
        dy = ctx->row_map[sy];
        if (dy < 0 || dy >= ctx->dst_h) {
            continue;
        }
        row = src + (size_t)y * (size_t)w;
        out = ctx->dst + (size_t)dy * (size_t)ctx->dst_w;

        for (x = 0; x < w; x++) {
            int sx = rect->left + x;
            int dx;

            if (sx < 0 || sx >= ctx->img_w) {
                continue;
            }
            dx = ctx->col_map[sx];
            if (dx < 0 || dx >= ctx->dst_w) {
                continue;
            }
            out[dx] = row[x];
        }
    }
    return 1;
}

static void fit_rect(int img_w, int img_h, int dst_w, int dst_h, int *out_w, int *out_h, int *off_x, int *off_y)
{
    if ((long)img_w * dst_h > (long)img_h * dst_w) {
        *out_w = dst_w;
        *out_h = (img_h * dst_w) / img_w;
    } else {
        *out_h = dst_h;
        *out_w = (img_w * dst_h) / img_h;
    }
    if (*out_w < 1) {
        *out_w = 1;
    }
    if (*out_h < 1) {
        *out_h = 1;
    }
    if (*out_w > dst_w) {
        *out_w = dst_w;
    }
    if (*out_h > dst_h) {
        *out_h = dst_h;
    }
    *off_x = (dst_w - *out_w) / 2;
    *off_y = (dst_h - *out_h) / 2;
}

static const char *jresult_str(JRESULT rc)
{
    switch (rc) {
    case JDR_INTR:
        return "interrupted";
    case JDR_INP:
        return "input read fail";
    case JDR_MEM1:
        return "work buffer too small";
    case JDR_MEM2:
        return "mcu buffer too small";
    case JDR_PAR:
        return "bad parameter";
    case JDR_FMT1:
        return "bad data";
    case JDR_FMT2:
        return "bad resolution";
    case JDR_FMT3:
        return "unsupported format (progressive/arithmetic)";
    default:
        return "ok";
    }
}

static esp_err_t jpeg_decode(const char *path, uint16_t *dst, int dst_w, int dst_h, uint16_t bg)
{
    FILE *fp;
    void *work = NULL;
    uint8_t *iobuf = NULL;
    int16_t *col_map = NULL;
    int16_t *row_map = NULL;
    JDEC jd;
    JRESULT rc;
    codec_ctx_t ctx;
    uint8_t scale = 0;
    int img_w;
    int img_h;
    int out_w;
    int out_h;
    int off_x;
    int off_y;
    int x;
    int y;
    long file_size;
    esp_err_t err = ESP_FAIL;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    iobuf = heap_caps_malloc(APP_CODEC_IO_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (iobuf == NULL) {
        iobuf = heap_caps_malloc(APP_CODEC_IO_SIZE, MALLOC_CAP_8BIT);
    }
    if (iobuf != NULL) {
        setvbuf(fp, (char *)iobuf, _IOFBF, APP_CODEC_IO_SIZE);
    }

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (file_size < 128) {
        goto out;
    }

    work = heap_caps_malloc(APP_CODEC_WORK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (work == NULL) {
        work = heap_caps_malloc(APP_CODEC_WORK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (work == NULL) {
        ESP_LOGW(TAG, "no memory for tjpgd work buffer %d bytes", (int)APP_CODEC_WORK_SIZE);
        goto out;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.fp = fp;
    ctx.dst = dst;
    ctx.dst_w = dst_w;
    ctx.dst_h = dst_h;

    rc = jd_prepare(&jd, codec_in, work, APP_CODEC_WORK_SIZE, &ctx);
    ESP_LOGI(TAG, "jpeg %s prepare=%d size=%ld dst=%dx%d work=%d free=%u", path, (int)rc, file_size,
             dst_w, dst_h, (int)APP_CODEC_WORK_SIZE,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    if (rc != JDR_OK) {
        ESP_LOGW(TAG, "jd_prepare failed %d (%s) file=%s", (int)rc, jresult_str(rc), path);
        {
            uint16_t iw = 0;
            uint16_t ih = 0;
            uint8_t sof = 0;

            if (jpeg_info_sof(path, &iw, &ih, &sof) == ESP_OK) {
                ESP_LOGW(TAG, "  jpeg %ux%u sof=0x%02x (%s) work=%d", iw, ih, sof,
                         (sof == 0xC0) ? "baseline" : (sof == 0xC2) ? "progressive" : "other",
                         (int)APP_CODEC_WORK_SIZE);
            }
        }
        goto out;
    }

    {
        int max_dim = (jd.width > jd.height) ? (int)jd.width : (int)jd.height;

        if (max_dim > 3200) {
            scale = 2;
        } else if (max_dim > 800) {
            scale = 1;
        }
    }

    img_w = ((int)jd.width + (1 << scale) - 1) >> scale;
    img_h = ((int)jd.height + (1 << scale) - 1) >> scale;
    if (img_w <= 0 || img_h <= 0) {
        goto out;
    }

    fit_rect(img_w, img_h, dst_w, dst_h, &out_w, &out_h, &off_x, &off_y);
    ctx.img_w = img_w;
    ctx.img_h = img_h;

    col_map = heap_caps_malloc(sizeof(int16_t) * (size_t)img_w, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    row_map = heap_caps_malloc(sizeof(int16_t) * (size_t)img_h, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (col_map == NULL || row_map == NULL) {
        goto out;
    }

    for (x = 0; x < img_w; x++) {
        col_map[x] = (int16_t)(off_x + (int)(((long)x * out_w) / img_w));
    }
    for (y = 0; y < img_h; y++) {
        row_map[y] = (int16_t)(off_y + (int)(((long)y * out_h) / img_h));
    }

    app_codec_fill(dst, dst_w, dst_h, bg);

    ctx.col_map = col_map;
    ctx.row_map = row_map;

    {
        int64_t t0 = esp_timer_get_time();

        rc = jd_decomp(&jd, codec_out, scale);
        ESP_LOGI(TAG, "jpeg decomp=%d %dx%d scale=%d -> %dx%d in %u ms", (int)rc, img_w, img_h,
                 (int)scale, out_w, out_h, (unsigned)((esp_timer_get_time() - t0) / 1000));
    }
    if (rc != JDR_OK) {
        ESP_LOGW(TAG, "jd_decomp failed %d (%s)", (int)rc, jresult_str(rc));
        goto out;
    }

    err = ESP_OK;

out:
    if (col_map != NULL) {
        heap_caps_free(col_map);
    }
    if (row_map != NULL) {
        heap_caps_free(row_map);
    }
    if (work != NULL) {
        heap_caps_free(work);
    }
    if (iobuf != NULL) {
        setvbuf(fp, NULL, _IONBF, 0);
        heap_caps_free(iobuf);
    }
    fclose(fp);
    return err;
}

static esp_err_t png_decode(const char *path, uint16_t *dst, int dst_w, int dst_h, uint16_t bg)
{
    unsigned char *rgba = NULL;
    unsigned w = 0;
    unsigned h = 0;
    uint16_t info_w = 0;
    uint16_t info_h = 0;
    unsigned rc;
    size_t need;
    int out_w;
    int out_h;
    int off_x;
    int off_y;
    int x;
    int y;

    if (png_info(path, &info_w, &info_h) != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    w = info_w;
    h = info_h;
    need = (size_t)w * (size_t)h * 4;
    if (need == 0 || need > APP_CODEC_PNG_LIMIT) {
        ESP_LOGW(TAG, "png %ux%u needs %u bytes rgba, over limit %d", w, h, (unsigned)need,
                 (int)APP_CODEC_PNG_LIMIT);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < need + (256 * 1024)) {
        ESP_LOGW(TAG, "png %ux%u needs %u bytes, psram free %u", w, h, (unsigned)need,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return ESP_ERR_NO_MEM;
    }

    rc = lodepng_decode32_file(&rgba, &w, &h, path);
    if (rc != 0 || rgba == NULL) {
        ESP_LOGW(TAG, "png decode failed %u", rc);
        if (rgba != NULL) {
            free(rgba);
        }
        return ESP_FAIL;
    }

    fit_rect((int)w, (int)h, dst_w, dst_h, &out_w, &out_h, &off_x, &off_y);
    app_codec_fill(dst, dst_w, dst_h, bg);

    for (y = 0; y < out_h; y++) {
        const unsigned char *row = rgba + ((size_t)((unsigned)y * h / (unsigned)out_h)) * w * 4;
        uint16_t *out = dst + (size_t)(off_y + y) * dst_w + off_x;

        for (x = 0; x < out_w; x++) {
            const unsigned char *p = row + ((size_t)((unsigned)x * w / (unsigned)out_w)) * 4;

            out[x] = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
        }
    }

    free(rgba);
    return ESP_OK;
}

esp_err_t app_codec_decode(const char *path, uint16_t *dst, int dst_w, int dst_h, uint16_t bg)
{
    if (path == NULL || dst == NULL || dst_w <= 0 || dst_h <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (is_png(path)) {
        return png_decode(path, dst, dst_w, dst_h, bg);
    }
    return jpeg_decode(path, dst, dst_w, dst_h, bg);
}

void app_codec_log_file(const char *path)
{
    FILE *fp;
    uint8_t b[8] = {0};
    size_t n = 0;
    long size = 0;

    if (path == NULL) {
        return;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        ESP_LOGW(TAG, "file %s cannot open", path);
        return;
    }
    n = fread(b, 1, sizeof(b), fp);
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    ESP_LOGW(TAG, "file %s size=%ld head=%02x %02x %02x %02x %02x %02x %02x %02x (n=%u)",
             path, size, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], (unsigned)n);
}

esp_err_t app_codec_thumb(const uint16_t *src, int src_w, int src_h, uint16_t *dst, int dst_w, int dst_h)
{
    int y;

    if (src == NULL || dst == NULL || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    for (y = 0; y < dst_h; y++) {
        int sy0 = (int)(((long)y * src_h) / dst_h);
        int sy1 = (int)(((long)(y + 1) * src_h) / dst_h);
        int sy;
        int x;

        if (sy1 <= sy0) {
            sy1 = sy0 + 1;
        }
        if (sy1 > src_h) {
            sy1 = src_h;
        }

        for (x = 0; x < dst_w; x++) {
            int sx0 = (int)(((long)x * src_w) / dst_w);
            int sx1 = (int)(((long)(x + 1) * src_w) / dst_w);
            uint32_t sum_r = 0;
            uint32_t sum_g = 0;
            uint32_t sum_b = 0;
            uint32_t count = 0;
            int sx;

            if (sx1 <= sx0) {
                sx1 = sx0 + 1;
            }
            if (sx1 > src_w) {
                sx1 = src_w;
            }

            for (sy = sy0; sy < sy1; sy++) {
                const uint16_t *row = src + (size_t)sy * (size_t)src_w;

                for (sx = sx0; sx < sx1; sx++) {
                    uint16_t px = row[sx];

                    sum_r += (px >> 11) & 0x1F;
                    sum_g += (px >> 5) & 0x3F;
                    sum_b += px & 0x1F;
                    count++;
                }
            }

            if (count == 0) {
                dst[(size_t)y * dst_w + x] = 0;
            } else {
                uint32_t r = sum_r / count;
                uint32_t g = sum_g / count;
                uint32_t b = sum_b / count;

                dst[(size_t)y * dst_w + x] = (uint16_t)((r << 11) | (g << 5) | b);
            }
        }
    }
    return ESP_OK;
}

esp_err_t app_codec_read565(const char *path, uint16_t *dst, int width, int height)
{
    FILE *fp;
    size_t want;
    size_t got;

    if (path == NULL || dst == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    want = (size_t)width * (size_t)height * sizeof(uint16_t);

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    got = fread(dst, 1, want, fp);
    fclose(fp);

    return (got == want) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t app_codec_write565(const char *path, const uint16_t *src, int width, int height)
{
    FILE *fp;
    size_t want;
    size_t put;

    if (path == NULL || src == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    want = (size_t)width * (size_t)height * sizeof(uint16_t);

    fp = fopen(path, "wb");
    if (fp == NULL) {
        return ESP_FAIL;
    }
    put = fwrite(src, 1, want, fp);
    fclose(fp);

    return (put == want) ? ESP_OK : ESP_FAIL;
}
