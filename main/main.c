#include <stdio.h>
#include <string.h>

#include "app_audio.h"
#include "app_bus.h"
#include "app_httpd.h"
#include "app_player.h"
#include "app_settings.h"
#include "app_store.h"
#include "app_time.h"
#include "app_wifi.h"
#include "bsp_backlight.h"
#include "bsp_i2c.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lvgl.h"
#include "ui/ui_nav.h"
#include "ui/ui_theme.h"

#define APP_LVGL_TASK_STACK 16384
#define APP_LVGL_CORE       0

static const char *TAG = "app";

static volatile uint32_t s_last_activity;
static bool s_screen_off;

static void note_activity(void)
{
    s_last_activity = xTaskGetTickCount();
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

static void wifi_client_cb(bool connected)
{
    if (connected) {
        app_bus_post(APP_EVT_CLIENT_CHANGED, NULL);
    }
}

static void lvgl_task(void *arg)
{
    (void)arg;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
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
    app_bus_init();
    app_store_init();
    note_activity();

    err = bsp_i2c_init(I2C_NUM_0, 400000);
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
    lv_port_disp_init();
    lv_port_indev_init();
    lv_port_tick_init();
    lv_port_indev_set_activity_cb(note_activity);

    ui_nav_init(lv_scr_act());

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

    printf("[hw] internal %uK dma %uK psram %uK storage %s codec %s\n",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           app_store_on_sd() ? "sd" : "flash",
           app_audio_codec_present() ? "ES8311" : "none");
    fflush(stdout);

    if (xTaskCreatePinnedToCore(lvgl_task, "lvgl", APP_LVGL_TASK_STACK, NULL, 3, NULL,
                                APP_LVGL_CORE) != pdPASS) {
        ESP_LOGE(TAG, "lvgl task create failed");
    }
}
