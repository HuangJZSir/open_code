#include <stdio.h>
#include <string.h>

#include "app_alarm.h"
#include "app_album.h"
#include "app_audio.h"
#include "app_bus.h"
#include "app_cast.h"
#include "app_httpd.h"
#include "app_player.h"
#include "app_power.h"
#include "app_settings.h"
#include "app_store.h"
#include "app_time.h"
#include "app_video.h"
#include "app_weather.h"
#include "app_wifi.h"
#include "bsp_backlight.h"
#include "bsp_i2c.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lvgl.h"
#include "ui/ui_bg.h"
#include "ui/ui_nav.h"
#include "ui/ui_theme.h"

#define APP_LVGL_TASK_STACK 12288
#define APP_LVGL_TASK_STACK_BIG 16384
#define APP_LVGL_CORE       0

static const char *TAG = "app";

static volatile uint32_t s_last_activity;
static bool s_screen_off;

static void note_activity(void)
{
    s_last_activity = xTaskGetTickCount();
    if (app_video_active()) {
        app_video_stop();
    }
#if APP_CAST_ENABLE
    if (app_cast_active()) {
        app_cast_stop();
    }
#endif
}

static void night_timer_cb(lv_timer_t *timer)
{
    int hour = app_time_hour();
    uint32_t idle_ms;
    bool off;

    (void)timer;

    idle_ms = (uint32_t)(xTaskGetTickCount() - s_last_activity) * portTICK_PERIOD_MS;
    off = app_settings_night_window(hour) && idle_ms > 60000U;

    if (off == s_screen_off) {
        return;
    }
    s_screen_off = off;
    if (off) {
        bsp_backlight_off();
    } else {
        app_settings_apply_brightness();
    }
}

static lv_obj_t *s_boot_splash;

static lv_obj_t *boot_splash_show(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *box;
    lv_obj_t *label;

    box = lv_obj_create(scr);
    lv_obj_set_size(box, UI_W, UI_H);
    lv_obj_set_pos(box, 0, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x0A1220), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    label = lv_label_create(box);
    lv_label_set_text(label, "加载中");
    lv_obj_set_style_text_font(label, UI_FONT_BIG, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE6E8EE), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -18);

    label = lv_label_create(box);
    lv_label_set_text(label, "音乐播放器 · 正在准备");
    lv_obj_set_style_text_font(label, UI_FONT_TEXT, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x8B93A7), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 28);

    lv_refr_now(NULL);
    ESP_LOGI(TAG, "boot splash shown");
    return box;
}

static void wifi_client_cb(bool connected)
{
    if (connected) {
        app_bus_post(APP_EVT_CLIENT_CHANGED, NULL);
    }
}

static void lvgl_task(void *arg)
{
#if APP_CAST_ENABLE
    bool casted = false;
#endif
    bool video = false;

    (void)arg;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
#if APP_CAST_ENABLE
        if (app_cast_active()) {
            casted = true;
            continue;
        }
        if (casted) {
            casted = false;
            lv_obj_invalidate(lv_scr_act());
        }
#endif
        if (app_video_active()) {
            video = true;
            continue;
        }
        if (video) {
            video = false;
            lv_obj_invalidate(lv_scr_act());
        }
        lv_task_handler();
        ui_nav_pump();
    }
}

void app_main(void)
{
    esp_err_t err;
    app_settings_t *cfg;

    err = app_settings_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "settings init failed: %s", esp_err_to_name(err));
    }

    app_time_init();
    app_alarm_init();
    app_bus_init();
    app_store_init();
    if (app_album_init() != ESP_OK) {
        ESP_LOGE(TAG, "album buffer not available");
    }
    note_activity();

    err = bsp_i2c_init(I2C_NUM_0, 400000);
    app_power_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c init failed: %s", esp_err_to_name(err));
    }

    if (app_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "audio path not initialised");
    }

    if (app_player_init() != ESP_OK) {
        ESP_LOGE(TAG, "player init failed");
    }

    cfg = app_settings_get();
    app_player_set_volume(cfg->volume);
    app_player_set_repeat(cfg->repeat);
    app_player_set_shuffle(cfg->shuffle);

    {
        uint32_t tracks = app_player_scan();

        ESP_LOGI(TAG, "tracks: %lu on %s", (unsigned long)tracks, app_player_dir());
    }

    lv_init();
    if (ui_bg_reserve() != ESP_OK) {
        ESP_LOGE(TAG, "no psram for 800x480 canvas");
    }
    lv_port_disp_init();
    lv_port_indev_init();
    lv_port_tick_init();
    lv_port_indev_set_activity_cb(note_activity);

    s_boot_splash = boot_splash_show();
    app_settings_apply_brightness();

#if APP_CAST_ENABLE
    if (app_cast_init() != ESP_OK) {
        ESP_LOGW(TAG, "cast not available");
    }
#endif

    if (app_video_init() != ESP_OK) {
        ESP_LOGW(TAG, "video not available");
    }

    ui_nav_init(lv_scr_act());
    if (s_boot_splash != NULL) {
        lv_obj_move_foreground(s_boot_splash);
    }
    lv_refr_now(NULL);
    if (s_boot_splash != NULL) {
        lv_obj_del(s_boot_splash);
        s_boot_splash = NULL;
        lv_refr_now(NULL);
        ESP_LOGI(TAG, "boot splash removed");
    }
    app_settings_apply_brightness();

    err = app_wifi_init();
    if (err == ESP_OK) {
        app_time_start_sntp();
    } else {
        ESP_LOGE(TAG, "wifi init failed: %s", esp_err_to_name(err));
    }
    app_wifi_set_client_cb(wifi_client_cb);

    err = app_httpd_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed: %s", esp_err_to_name(err));
    }

    app_weather_start();

    lv_timer_create(night_timer_cb, 10000, NULL);

    {
        const char *sample = "音乐";
        lv_font_glyph_dsc_t gd;
        bool glyph_ok;

        memset(&gd, 0, sizeof(gd));
        glyph_ok = lv_font_get_glyph_dsc(UI_FONT_TEXT, &gd, 0x4E50, 0);
        printf("[ui] utf8=%02x %02x %02x glyph_ok=%d box=%dx%d ofs=%d,%d bpp=%d\n",
               (unsigned char)sample[0], (unsigned char)sample[1], (unsigned char)sample[2],
               glyph_ok ? 1 : 0, (int)gd.box_w, (int)gd.box_h,
               (int)gd.ofs_x, (int)gd.ofs_y, (int)gd.bpp);
        fflush(stdout);
    }

    printf("[hw] internal %uK max %uK dma %uK psram %uK/%uK max %uK storage %s codec %s\n",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024),
           app_store_on_sd() ? "sd" : "flash",
           app_audio_codec_present() ? "ES8311" : "none");
    fflush(stdout);

    if (xTaskCreatePinnedToCoreWithCaps(lvgl_task, "lvgl", APP_LVGL_TASK_STACK, NULL, 3, NULL,
                                        APP_LVGL_CORE,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGW(TAG, "lvgl internal stack failed, retry from psram");
        if (xTaskCreatePinnedToCoreWithCaps(lvgl_task, "lvgl", APP_LVGL_TASK_STACK_BIG, NULL, 3, NULL,
                                            APP_LVGL_CORE,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
            ESP_LOGE(TAG, "lvgl task create failed");
        }
    }

    printf("[hw] after lvgl task internal %uK max %uK\n",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    fflush(stdout);
}
