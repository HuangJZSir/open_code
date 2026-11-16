#include "app_video.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "app_store.h"
#include "esp_extractor_defaults.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_video_color_convert.h"
#include "esp_gmf_video_crop.h"
#include "esp_gmf_video_scale.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_player.h"
#include "esp_timer.h"
#include "esp_video_dec_default.h"
#include "esp_video_render.h"
#include "esp_video_render_backend.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "media_lib_adapter.h"
#include "esp_lcd_panel_rgb.h"
#include "tjpgd.h"

#define VIDEO_MAX_FILES   (24)
#define VIDEO_NAME_MAX    (72)
#define VIDEO_PATH_MAX    (160)
#define VIDEO_FPS         (30)
#define VIDEO_TASK_STACK  (8192)
#define VIDEO_TASK_PRIO   (5)
#define VIDEO_TASK_CORE   (1)
#define VIDEO_SCREEN_W    (LCD_WIDTH)
#define VIDEO_SCREEN_H    (LCD_HEIGHT)
#define VIDEO_POLL_MS     (20)
#define VIDEO_PREPARE_MS  (10000)
#define VIDEO_STALL_MS    (3000)
#define VIDEO_LOG_MS      (2000)
#define VIDEO_RELEASE_MS  (3000)
#define VIDEO_TOUCH_ARM_MS (400)
#define VIDEO_PROBE_CHUNK (512)
#define VIDEO_PROBE_HEAD  (64 * 1024)
#define VIDEO_PROBE_TAIL  (256 * 1024)
#define VIDEO_H264_BASELINE (66)
#define MJPEG_FPS         (15)
#define MJPEG_MAX_W       (640)
#define MJPEG_MAX_H       (480)
#define MJPEG_POOL_SIZE   (16 * 1024)
#define MJPEG_POOL_INTERNAL (96 * 1024)
#define MJPEG_LOG_MS      (2000)
#define MJPEG_TOP_BAR     (56)

static const char *TAG = "video";

static char s_files[VIDEO_MAX_FILES][VIDEO_NAME_MAX];
static char s_path[VIDEO_PATH_MAX];
static int s_count;

static volatile bool s_active;
static volatile bool s_stop_req;
static volatile bool s_release_req;
static bool s_ready;
static bool s_media_ready;
static bool s_reused;
static const char *s_error;

static FILE *s_mj_file;
static uint8_t *s_mj_pool;
static uint16_t *s_mj_dec;
static size_t s_mj_cap;
static uint16_t s_mj_w;
static uint16_t s_mj_h;
static uint16_t s_mj_box_w;
static uint16_t s_mj_box_h;
static uint16_t s_mj_xmap[VIDEO_SCREEN_W];
static uint16_t s_mj_line[VIDEO_SCREEN_W];
static uint8_t *s_mj_iobuf;
static uint32_t s_mj_read_us;

static esp_gmf_pool_handle_t s_pool;
static esp_video_render_handle_t s_render;
static esp_player_handle_t s_player;

static void log_heap(const char *stage)
{
    ESP_LOGI(TAG, "%s psram %uK internal %uK", stage,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
}

static bool video_ext_ok(const char *name)
{
    static const char *const exts[] = {".mp4", ".avi", ".mjpeg", ".mjpg", ".mpj"};
    const char *dot = strrchr(name, '.');
    int i;

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

static uint32_t video_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static bool video_avcc_hit(FILE *fp, long at, uint8_t *profile)
{
    uint8_t head[8];
    uint8_t size[4];

    if (at < 4) {
        return false;
    }
    if (fseek(fp, at - 4, SEEK_SET) != 0 || fread(size, 1, sizeof(size), fp) != sizeof(size)) {
        return false;
    }
    if (video_be32(size) < 15 || video_be32(size) > 4096) {
        return false;
    }
    if (fseek(fp, at + 4, SEEK_SET) != 0 || fread(head, 1, sizeof(head), fp) != sizeof(head)) {
        return false;
    }
    if (head[0] != 1) {
        return false;
    }
    *profile = head[1];
    return true;
}

static bool video_scan_avcc(FILE *fp, long base, long len, uint8_t *profile)
{
    uint8_t buf[VIDEO_PROBE_CHUNK + 3];
    long end = base + len;
    long pos = base;
    size_t carry = 0;
    uint8_t first = 0;
    bool any = false;

    while (pos < end) {
        long left = end - pos;
        size_t want = left > VIDEO_PROBE_CHUNK ? VIDEO_PROBE_CHUNK : (size_t)left;
        size_t got;
        size_t total;
        size_t i;

        if (fseek(fp, pos, SEEK_SET) != 0) {
            break;
        }
        got = fread(buf + carry, 1, want, fp);
        if (got == 0) {
            break;
        }
        total = carry + got;
        for (i = 0; i + 4 <= total; i++) {
            uint8_t cur = 0;

            if (buf[i] != 'a' || buf[i + 1] != 'v' || buf[i + 2] != 'c' || buf[i + 3] != 'C') {
                continue;
            }
            if (!video_avcc_hit(fp, pos - (long)carry + (long)i, &cur)) {
                continue;
            }
            if (cur == VIDEO_H264_BASELINE) {
                *profile = cur;
                return true;
            }
            if (!any) {
                any = true;
                first = cur;
            }
        }
        carry = total > 3 ? 3 : total;
        memmove(buf, buf + total - carry, carry);
        pos += (long)got;
    }
    if (any) {
        *profile = first;
        return true;
    }
    return false;
}

static bool video_read_profile(const char *path, uint8_t *profile)
{
    FILE *fp;
    long size;
    bool found = false;

    *profile = 0;
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        return false;
    }
    {
        long head = size > VIDEO_PROBE_HEAD ? VIDEO_PROBE_HEAD : size;
        long tail = size > VIDEO_PROBE_TAIL ? VIDEO_PROBE_TAIL : size;
        uint8_t cur = 0;

        if (video_scan_avcc(fp, 0, head, &cur)) {
            found = true;
            *profile = cur;
        }
        if ((!found || *profile != VIDEO_H264_BASELINE) && size > tail) {
            if (video_scan_avcc(fp, size - tail, tail, &cur) && (!found || cur == VIDEO_H264_BASELINE)) {
                found = true;
                *profile = cur;
            }
        }
    }
    fclose(fp);
    return found;
}

static esp_err_t video_check_codec(const char *path)
{
    uint8_t profile = 0;

    if (!video_read_profile(path, &profile)) {
        return ESP_OK;
    }
    if (profile == VIDEO_H264_BASELINE) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "h264 profile %u not baseline", (unsigned)profile);
    if (profile == 77) {
        s_error = "视频是 H.264 Main 编码，只支持 Baseline";
    } else if (profile == 100) {
        s_error = "视频是 H.264 High 编码，只支持 Baseline";
    } else if (profile == 88) {
        s_error = "视频是 H.264 Extended 编码，只支持 Baseline";
    } else {
        s_error = "视频不是 H.264 Baseline 编码，无法播放";
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t video_pool_build(void)
{
    esp_gmf_element_handle_t el = NULL;
    esp_imgfx_scale_cfg_t scale_cfg = {
        .filter_type = ESP_IMGFX_SCALE_FILTER_TYPE_BILINEAR,
    };
    esp_imgfx_crop_cfg_t crop_cfg = {};
    esp_imgfx_color_convert_cfg_t color_cfg = {
        .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601,
    };

    if (esp_gmf_pool_init(&s_pool) != ESP_GMF_ERR_OK) {
        return ESP_FAIL;
    }
    if (esp_gmf_video_scale_init(&scale_cfg, &el) == ESP_GMF_ERR_OK) {
        esp_gmf_pool_register_element(s_pool, el, NULL);
    }
    el = NULL;
    if (esp_gmf_video_crop_init(&crop_cfg, &el) == ESP_GMF_ERR_OK) {
        esp_gmf_pool_register_element(s_pool, el, NULL);
    }
    el = NULL;
    if (esp_gmf_video_color_convert_init(&color_cfg, &el) == ESP_GMF_ERR_OK) {
        esp_gmf_pool_register_element(s_pool, el, NULL);
    }
    return ESP_OK;
}

static esp_player_err_t video_event_cb(esp_player_event_msg_t *msg, void *ctx)
{
    esp_player_error_source_t src = ESP_PLAYER_ERROR_SOURCE_NONE;

    (void)ctx;

    if (msg == NULL || msg->event_type != ESP_PLAYER_EVENT_ERROR) {
        return ESP_PLAYER_ERR_OK;
    }
    if (msg->data != NULL && msg->data_len >= sizeof(src)) {
        memcpy(&src, msg->data, sizeof(src));
    }
    ESP_LOGE(TAG, "error source %d", (int)src);
    if (src == ESP_PLAYER_ERROR_SOURCE_VIDEO_DECODER) {
        if (s_error == NULL) {
            s_error = "视频编码不支持，只支持 H.264 Baseline";
        }
    } else if (src == ESP_PLAYER_ERROR_SOURCE_EXTRACTOR) {
        s_error = "视频文件读不了";
    } else if (s_error == NULL) {
        s_error = "播放失败";
    }
    return ESP_PLAYER_ERR_OK;
}

static void video_teardown(void)
{
    if (s_player != NULL) {
        esp_player_set_event_cb(s_player, NULL, NULL);
        esp_player_deinit(s_player);
        s_player = NULL;
    }
    if (s_render != NULL) {
        esp_video_render_destroy(s_render);
        s_render = NULL;
    }
    if (s_pool != NULL) {
        esp_gmf_pool_deinit(s_pool);
        s_pool = NULL;
    }
}

static esp_err_t video_setup(void)
{
    esp_lcd_panel_handle_t panel = lv_port_disp_get_panel_handle();
    esp_video_render_cfg_t render_cfg = {
        .pool = NULL,
        .fps = VIDEO_FPS,
    };
    esp_video_render_lcd_cfg_t lcd_cfg = {};
    esp_video_render_backend_cfg_t backend_cfg = {};
    esp_player_config_t player_cfg = ESP_PLAYER_CONFIG_DEFAULT();

    if (panel == NULL) {
        ESP_LOGE(TAG, "no panel");
        return ESP_FAIL;
    }

    if (!s_media_ready) {
        media_lib_add_default_adapter();
        esp_extractor_register_default();
        esp_video_dec_register_default();
        s_media_ready = true;
    }

    if (video_pool_build() != ESP_OK) {
        ESP_LOGE(TAG, "pool build failed");
        goto fail;
    }

    render_cfg.pool = s_pool;
    if (esp_video_render_create(&render_cfg, &s_render) != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "render create failed");
        goto fail;
    }

    lcd_cfg.lcd_type = ESP_VIDEO_RENDER_LCD_TYPE_RGB;
    lcd_cfg.fb_num = 1;
    lcd_cfg.out_format = ESP_VIDEO_RENDER_FORMAT_RGB565;
    lcd_cfg.width = VIDEO_SCREEN_W;
    lcd_cfg.height = VIDEO_SCREEN_H;
    lcd_cfg.lcd_handle = panel;
    lcd_cfg.io_handle = NULL;
    backend_cfg.ops = esp_video_render_get_lcd_backend();
    backend_cfg.cfg = &lcd_cfg;
    backend_cfg.cfg_size = sizeof(lcd_cfg);

    log_heap("before display");
    if (esp_video_render_set_display(s_render, &backend_cfg) != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "set display failed");
        goto fail;
    }
    log_heap("after display");

    player_cfg.audio_render_hd = NULL;
    player_cfg.video_render_hd = s_render;
    if (esp_player_init(&player_cfg, &s_player) != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "player init failed");
        goto fail;
    }
    esp_player_set_event_cb(s_player, video_event_cb, NULL);
    ESP_LOGI(TAG, "ready %dx%d fps %d", VIDEO_SCREEN_W, VIDEO_SCREEN_H, VIDEO_FPS);
    return ESP_OK;

fail:
    video_teardown();
    return ESP_FAIL;
}

static bool video_log_tracks(void)
{
    esp_player_track_info_t info;
    uint16_t num = 0;
    uint16_t i;
    bool logged = false;

    if (esp_player_get_track_num(s_player, ESP_PLAYER_TRACK_TYPE_VIDEO, &num) != ESP_PLAYER_ERR_OK) {
        return false;
    }
    ESP_LOGI(TAG, "video tracks %u", (unsigned)num);
    for (i = 0; i < num; i++) {
        memset(&info, 0, sizeof(info));
        if (esp_player_get_track_info(s_player, ESP_PLAYER_TRACK_TYPE_VIDEO, i, &info) != ESP_PLAYER_ERR_OK) {
            ESP_LOGW(TAG, "track %u info failed", (unsigned)i);
            continue;
        }
        ESP_LOGI(TAG, "track %u codec %c%c%c%c 0x%08x %ux%u fps %u br %u",
                 (unsigned)i,
                 (char)(info.video_info.format & 0xFF),
                 (char)((info.video_info.format >> 8) & 0xFF),
                 (char)((info.video_info.format >> 16) & 0xFF),
                 (char)((info.video_info.format >> 24) & 0xFF),
                 (unsigned)info.video_info.format,
                 (unsigned)info.video_info.width,
                 (unsigned)info.video_info.height,
                 (unsigned)info.video_info.fps,
                 (unsigned)info.video_info.bitrate);
        logged = true;
    }
    return logged;
}

static uint32_t video_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool video_touch_down(void)
{
    return lv_port_indev_poll_touch(NULL, NULL);
}

static void video_wait_release(void)
{
    uint32_t t0 = video_now_ms();

    if (!video_touch_down()) {
        return;
    }
    while (video_now_ms() - t0 < VIDEO_RELEASE_MS) {
        if (!video_touch_down()) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(VIDEO_POLL_MS));
    }
}

static bool video_is_mjpeg(const char *name)
{
    static const char *const exts[] = {".mjpeg", ".mjpg", ".mpj"};
    const char *dot = strrchr(name, '.');
    int i;

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

static size_t mjpeg_read(JDEC *jd, uint8_t *buf, size_t len)
{
    int64_t t0;
    size_t got;

    (void)jd;

    if (s_mj_file == NULL) {
        return 0;
    }
    if (buf == NULL) {
        return fseek(s_mj_file, (long)len, SEEK_CUR) == 0 ? len : 0;
    }
    t0 = esp_timer_get_time();
    got = fread(buf, 1, len, s_mj_file);
    s_mj_read_us += (uint32_t)(esp_timer_get_time() - t0);
    return got;
}

static int mjpeg_write(JDEC *jd, void *bitmap, JRECT *rect)
{
    const uint16_t *src = (const uint16_t *)bitmap;
    uint16_t w = (uint16_t)(rect->right - rect->left + 1);
    uint16_t h = (uint16_t)(rect->bottom - rect->top + 1);
    uint16_t y;

    (void)jd;

    for (y = 0; y < h; y++) {
        uint16_t *dst = s_mj_dec + (size_t)(rect->top + y) * s_mj_w + rect->left;

        memcpy(dst, src, (size_t)w * sizeof(uint16_t));
        src += w;
    }
    return 1;
}

static bool mjpeg_next_frame(void)
{
    int prev = 0;
    int c;

    for (;;) {
        c = fgetc(s_mj_file);
        if (c == EOF) {
            return false;
        }
        if (prev == 0xFF && c == 0xD8) {
            return fseek(s_mj_file, -2, SEEK_CUR) == 0;
        }
        prev = c;
    }
}

static uint8_t *mjpeg_pool(void)
{
    if (s_mj_pool != NULL) {
        return s_mj_pool;
    }
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > MJPEG_POOL_INTERNAL) {
        s_mj_pool = heap_caps_malloc(MJPEG_POOL_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_mj_pool == NULL) {
        s_mj_pool = heap_caps_malloc(MJPEG_POOL_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (s_mj_pool == NULL) {
        ESP_LOGE(TAG, "mjpeg pool alloc failed");
    }
    return s_mj_pool;
}

static uint16_t *mjpeg_dec(size_t pixels)
{
    size_t bytes = pixels * sizeof(uint16_t);

    if (s_mj_dec != NULL && s_mj_cap >= bytes) {
        return s_mj_dec;
    }
    if (s_mj_dec != NULL) {
        heap_caps_free(s_mj_dec);
        s_mj_dec = NULL;
        s_mj_cap = 0;
    }
    s_mj_dec = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_mj_dec == NULL) {
        ESP_LOGE(TAG, "mjpeg decode buffer %u bytes failed", (unsigned)bytes);
        return NULL;
    }
    s_mj_cap = bytes;
    return s_mj_dec;
}

static void mjpeg_release(void)
{
    if (s_mj_dec != NULL) {
        heap_caps_free(s_mj_dec);
        s_mj_dec = NULL;
        s_mj_cap = 0;
    }
    if (s_mj_pool != NULL) {
        heap_caps_free(s_mj_pool);
        s_mj_pool = NULL;
    }
    if (s_mj_iobuf != NULL) {
        heap_caps_free(s_mj_iobuf);
        s_mj_iobuf = NULL;
    }
    s_mj_box_w = 0;
    s_mj_box_h = 0;
}

static uint16_t *mjpeg_fb(void)
{
    esp_lcd_panel_handle_t panel = lv_port_disp_get_panel_handle();
    void *fb = NULL;

    if (panel == NULL) {
        return NULL;
    }
    if (esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb) != ESP_OK || fb == NULL) {
        return NULL;
    }
    return (uint16_t *)fb;
}

static esp_err_t mjpeg_blit(uint16_t *fb)
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
    uint16_t last_src = 0xFFFF;
    uint16_t area_h = (uint16_t)(VIDEO_SCREEN_H - MJPEG_TOP_BAR);

    if (fb == NULL || s_mj_dec == NULL || s_mj_w == 0 || s_mj_h == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    fit_x = ((uint32_t)VIDEO_SCREEN_W << 16) / s_mj_w;
    fit_y = ((uint32_t)area_h << 16) / s_mj_h;
    fit = (fit_x < fit_y) ? fit_x : fit_y;

    out_w = (uint16_t)(((uint32_t)s_mj_w * fit) >> 16);
    out_h = (uint16_t)(((uint32_t)s_mj_h * fit) >> 16);
    if (out_w > VIDEO_SCREEN_W) {
        out_w = VIDEO_SCREEN_W;
    }
    if (out_h > area_h) {
        out_h = area_h;
    }
    if (out_w == 0 || out_h == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    ox = (uint16_t)((VIDEO_SCREEN_W - out_w) / 2);
    oy = (uint16_t)(MJPEG_TOP_BAR + (area_h - out_h) / 2);

    if (out_w != s_mj_box_w || out_h != s_mj_box_h) {
        memset(fb + (size_t)MJPEG_TOP_BAR * VIDEO_SCREEN_W, 0,
               (size_t)area_h * VIDEO_SCREEN_W * sizeof(uint16_t));
        s_mj_box_w = out_w;
        s_mj_box_h = out_h;
    }

    for (x = 0; x < out_w; x++) {
        s_mj_xmap[x] = (uint16_t)(((uint32_t)x * s_mj_w) / out_w);
    }

    for (y = 0; y < out_h; y++) {
        uint16_t src_y = (uint16_t)(((uint32_t)y * s_mj_h) / out_h);
        uint16_t *dst = fb + (size_t)(oy + y) * VIDEO_SCREEN_W + ox;

        if (src_y != last_src) {
            const uint16_t *row = s_mj_dec + (size_t)src_y * s_mj_w;

            for (x = 0; x < out_w; x++) {
                s_mj_line[x] = row[s_mj_xmap[x]];
            }
            last_src = src_y;
        }
        memcpy(dst, s_mj_line, (size_t)out_w * sizeof(uint16_t));
    }

    return ESP_OK;
}

static void mjpeg_task(void *arg)
{
    uint16_t *fb;
    uint32_t frames = 0;
    uint32_t t0;
    uint32_t log_ms;
    uint32_t next_ms;

    (void)arg;

    s_mj_file = fopen(s_path, "rb");
    if (s_mj_file == NULL) {
        ESP_LOGE(TAG, "open %s failed", s_path);
        s_error = "视频文件读不了";
        goto done;
    }
    if (mjpeg_pool() == NULL) {
        s_error = "内存不足";
        goto done;
    }
    fb = mjpeg_fb();
    if (fb == NULL) {
        s_error = "屏幕缓冲不可用";
        goto done;
    }

    ESP_LOGI(TAG, "mjpeg play %s", s_path);
    log_heap("mjpeg");
    t0 = video_now_ms();
    log_ms = t0;
    next_ms = t0;

    while (s_stop_req == false && s_error == NULL) {
        JDEC jd;
        JRESULT rc;
        uint32_t now;
        uint8_t scale = 0;

        if (!mjpeg_next_frame()) {
            break;
        }
        memset(&jd, 0, sizeof(jd));
        rc = jd_prepare(&jd, mjpeg_read, s_mj_pool, MJPEG_POOL_SIZE, NULL);
        if (rc != JDR_OK) {
            ESP_LOGW(TAG, "mjpeg prepare %d", (int)rc);
            if (frames == 0) {
                s_error = "不是有效的 MJPEG 文件";
            }
            break;
        }
        while (scale < 3 && ((jd.width >> scale) > MJPEG_MAX_W || (jd.height >> scale) > MJPEG_MAX_H)) {
            scale++;
        }
        s_mj_w = (uint16_t)(jd.width >> scale);
        s_mj_h = (uint16_t)(jd.height >> scale);
        if (s_mj_w == 0 || s_mj_h == 0 || s_mj_w > VIDEO_SCREEN_W || s_mj_h > VIDEO_SCREEN_H) {
            ESP_LOGW(TAG, "mjpeg bad size %ux%u", (unsigned)jd.width, (unsigned)jd.height);
            break;
        }
        if (mjpeg_dec((size_t)s_mj_w * s_mj_h) == NULL) {
            s_error = "内存不足";
            break;
        }
        rc = jd_decomp(&jd, mjpeg_write, scale);
        if (rc != JDR_OK) {
            ESP_LOGW(TAG, "mjpeg decomp %d", (int)rc);
            break;
        }
        if (mjpeg_blit(fb) != ESP_OK) {
            ESP_LOGW(TAG, "mjpeg blit failed");
            break;
        }
        frames++;

        if (frames > 1 && video_touch_down()) {
            ESP_LOGI(TAG, "touch stop");
            s_stop_req = true;
            break;
        }

        now = video_now_ms();
        next_ms += (1000U / MJPEG_FPS);
        if ((int32_t)(now - next_ms) > 0) {
            next_ms = now;
        } else {
            vTaskDelay(pdMS_TO_TICKS(next_ms - now));
        }

        now = video_now_ms();
        if (now - log_ms >= MJPEG_LOG_MS) {
            log_ms = now;
            ESP_LOGI(TAG, "mjpeg frames %u fps %u %ux%u", (unsigned)frames,
                     (unsigned)((now - t0) ? (frames * 1000U / (now - t0)) : 0),
                     (unsigned)s_mj_w, (unsigned)s_mj_h);
        }
    }
    ESP_LOGI(TAG, "mjpeg done, frames %u", (unsigned)frames);

done:
    if (s_mj_file != NULL) {
        fclose(s_mj_file);
        s_mj_file = NULL;
    }
    video_wait_release();
    mjpeg_release();
    log_heap("mjpeg end");
    s_release_req = false;
    s_stop_req = false;
    s_active = false;
    vTaskDelete(NULL);
}

static esp_err_t video_start(const esp_player_data_src_t *src)
{
    esp_player_err_t ret = esp_player_set_data_src(s_player, src);

    if (ret == ESP_PLAYER_ERR_OK) {
        ret = esp_player_run(s_player);
    }
    if (ret == ESP_PLAYER_ERR_OK) {
        return ESP_OK;
    }
    ESP_LOGE(TAG, "start %d", (int)ret);
    if (!s_reused) {
        return ESP_FAIL;
    }
    ESP_LOGW(TAG, "reuse failed, rebuild");
    video_teardown();
    if (video_setup() != ESP_OK) {
        return ESP_FAIL;
    }
    s_reused = false;
    ret = esp_player_set_data_src(s_player, src);
    if (ret == ESP_PLAYER_ERR_OK) {
        ret = esp_player_run(s_player);
    }
    if (ret != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "restart %d", (int)ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void video_task(void *arg)
{
    esp_player_data_src_t src = {
        .url = s_path,
        .av_mask = ESP_PLAYER_MASK_VIDEO,
        .sync_mode = ESP_PLAYER_SYNC_MODE_VIDEO,
    };
    esp_player_state_t state = ESP_PLAYER_STATE_IDLE;
    uint64_t duration = 0;
    uint64_t pos = 0;
    uint64_t last_pos = 0;
    uint32_t t0;
    uint32_t pos_ms;
    uint32_t log_ms;
    bool progressed = false;
    bool logged = false;

    (void)arg;

    if (video_start(&src) != ESP_OK) {
        s_error = "打开视频失败";
        goto done;
    }

    esp_player_get_duration(s_player, &duration);
    ESP_LOGI(TAG, "playing %s duration %llu ms", s_path, (unsigned long long)duration);
    log_heap("play");

    t0 = video_now_ms();
    pos_ms = t0;
    log_ms = t0;
    while (s_stop_req == false && s_error == NULL) {
        uint32_t now = video_now_ms();

        if (esp_player_get_state(s_player, &state) != ESP_PLAYER_ERR_OK) {
            break;
        }
        if (state == ESP_PLAYER_STATE_FINISHED || state == ESP_PLAYER_STATE_STOPPED ||
            state == ESP_PLAYER_STATE_ERROR) {
            break;
        }
        if (now - t0 > VIDEO_TOUCH_ARM_MS && video_touch_down()) {
            ESP_LOGI(TAG, "touch stop");
            s_stop_req = true;
            break;
        }
        if (state == ESP_PLAYER_STATE_PLAYING || state == ESP_PLAYER_STATE_PAUSED) {
            if (!logged) {
                logged = video_log_tracks();
            }
            if (esp_player_get_play_time(s_player, &pos) == ESP_PLAYER_ERR_OK && pos != last_pos) {
                last_pos = pos;
                pos_ms = now;
                progressed = true;
            }
            if (progressed && now - pos_ms > VIDEO_STALL_MS) {
                ESP_LOGE(TAG, "stalled at %llu ms", (unsigned long long)last_pos);
                s_error = "视频解码跟不上";
                break;
            }
            if (now - log_ms >= VIDEO_LOG_MS) {
                log_ms = now;
                ESP_LOGI(TAG, "pos %llu/%llu ms", (unsigned long long)last_pos,
                         (unsigned long long)duration);
            }
        } else if (now - t0 > VIDEO_PREPARE_MS) {
            ESP_LOGE(TAG, "prepare timeout, state %d", (int)state);
            s_error = "视频打不开";
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(VIDEO_POLL_MS));
    }
    ESP_LOGI(TAG, "state %d", (int)state);
    esp_player_stop(s_player);
    video_wait_release();

done:
    if (s_release_req || s_error != NULL) {
        video_teardown();
    }
    log_heap("end");
    s_release_req = false;
    s_stop_req = false;
    s_active = false;
    vTaskDelete(NULL);
}

esp_err_t app_video_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    s_ready = true;
    ESP_LOGI(TAG, "ready");
    return ESP_OK;
}

bool app_video_active(void)
{
    return s_active;
}

void app_video_stop(void)
{
    s_stop_req = true;
}

void app_video_release(void)
{
    if (s_active) {
        s_release_req = true;
        s_stop_req = true;
        return;
    }
    mjpeg_release();
    video_teardown();
}

int app_video_scan(void)
{
    DIR *dir;
    struct dirent *ent;
    const char *root;

    s_count = 0;
    if (!app_store_ready()) {
        return 0;
    }
    root = app_store_mount();
    dir = opendir(root);
    if (dir == NULL) {
        return 0;
    }

    while ((ent = readdir(dir)) != NULL && s_count < VIDEO_MAX_FILES) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        if (!video_ext_ok(ent->d_name)) {
            continue;
        }
        if (strlen(ent->d_name) >= VIDEO_NAME_MAX) {
            continue;
        }
        snprintf(s_files[s_count], VIDEO_NAME_MAX, "%s", ent->d_name);
        ESP_LOGI(TAG, "  [%d] %s", s_count, s_files[s_count]);
        s_count++;
    }

    closedir(dir);
    ESP_LOGI(TAG, "found %d file(s) on %s", s_count, root);
    return s_count;
}

const char *app_video_name(int index)
{
    if (index < 0 || index >= s_count) {
        return NULL;
    }
    return s_files[index];
}

const char *app_video_last_error(void)
{
    return s_error;
}

esp_err_t app_video_play_index(int index)
{
    if (!s_ready || index < 0 || index >= s_count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_active) {
        return ESP_ERR_INVALID_STATE;
    }
    snprintf(s_path, sizeof(s_path), "%s/%s", app_store_mount(), s_files[index]);
    s_error = NULL;
    if (video_is_mjpeg(s_files[index])) {
        video_teardown();
        s_stop_req = false;
        s_active = true;
        if (xTaskCreatePinnedToCore(mjpeg_task, "mjpeg", VIDEO_TASK_STACK, NULL,
                                    VIDEO_TASK_PRIO, NULL, VIDEO_TASK_CORE) != pdPASS) {
            s_active = false;
            s_error = "视频任务创建失败";
            return ESP_ERR_NO_MEM;
        }
        return ESP_OK;
    }
    if (video_check_codec(s_path) != ESP_OK) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_reused = (s_player != NULL);
    if (s_player == NULL && video_setup() != ESP_OK) {
        s_reused = false;
        s_error = "视频初始化失败";
        return ESP_FAIL;
    }
    s_stop_req = false;
    s_active = true;

    if (xTaskCreatePinnedToCore(video_task, "video", VIDEO_TASK_STACK, NULL,
                                VIDEO_TASK_PRIO, NULL, VIDEO_TASK_CORE) != pdPASS) {
        s_active = false;
        s_error = "视频任务创建失败";
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
