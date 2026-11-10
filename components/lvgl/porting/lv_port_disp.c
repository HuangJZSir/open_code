/**
 * @file lv_port_disp_templ.c
 *
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"

/* ---- Display pipeline -------------------------------------------------------
 *
 * One RGB panel, two refresh strategies, selected by UI_LCD_PARTIAL_REFRESH.
 * They differ only in refresh granularity and in who owns the LVGL buffers.
 *
 * UI_LCD_PARTIAL_REFRESH 1 (default) - the reference firmware's architecture:
 *   single PSRAM frame buffer (no double_fb), two 800x480 PSRAM draw buffers
 *   owned by LVGL, full_refresh = false. LVGL renders and pushes only the
 *   damaged rectangle, so a keyboard key press copies ~6 kB instead of a whole
 *   800x480 frame - that is what makes typing feel instant.
 *
 * UI_LCD_PARTIAL_REFRESH 0 - the Espressif example architecture this port
 *   started from: two PSRAM frame buffers used directly as LVGL draw buffers
 *   (double_fb) and full_refresh. Every visual change (even one key) repaints
 *   and copies the whole screen.
 *
 * Both strategies keep the internal-RAM bounce buffer (UI_LCD_BOUNCE_LINES).
 * The reference does without one, and on paper that is enough, because the
 * LovyanGFX RGB bus (lib/LovyanGFX/src/lgfx/v1/platforms/esp32s3/Bus_RGB.inl)
 * programs the LCD GDMA channel itself and, in the VSYNC interrupt, resets and
 * re-arms it from the frame buffer:
 *
 *     if (intr_status & LCD_LL_EVENT_VSYNC_END) {
 *         GDMA.channel[_dma_ch].out.conf0.out_rst = 1; ... out_rst = 0;
 *         GDMA.channel[_dma_ch].out.link.addr = &_dmadesc_restart;
 *         GDMA.channel[_dma_ch].out.link.start = 1;
 *     }
 *
 * ESP-IDF's driver does the same behind CONFIG_LCD_RGB_RESTART_IN_VSYNC=y, and
 * the reference additionally runs its code and constants from PSRAM
 * (platformio.ini: custom_sdkconfig = CONFIG_SPIRAM_XIP_FROM_PSRAM=y) because,
 * in its own words, "flash fetches starve the panel DMA and the display
 * flickers"; this project has CONFIG_SPIRAM_FETCH_INSTRUCTIONS and
 * CONFIG_SPIRAM_RODATA enabled for the same reason.
 *
 * Measured on this board, a per-VSYNC restart is not enough on its own: a large
 * repaint (page switch, tab switch) writes the whole frame buffer through the
 * data cache while the GDMA is streaming that same buffer, and PSRAM stops
 * answering for long enough that the panel visibly jitters. Scanning out of
 * internal RAM removes that dependency completely, and because the bounce
 * buffers are refilled by the CPU through the same cache the frame buffer is
 * written through, no extra cache maintenance is needed. The cost is 32 kB of
 * internal RAM; lower UI_LCD_BOUNCE_LINES if the WiFi driver ever complains.
 */
#define UI_LCD_BOUNCE_LINES 10

/* 1 = partial refresh (only the damaged rectangle, reference architecture)
 * 0 = full refresh (whole screen per change, Espressif example architecture) */
#define UI_LCD_PARTIAL_REFRESH 1

/* Pixel clock for this 5.0" 800x480 panel.
 * The reference firmware runs this panel at 25 MHz (LovyanGFX freq_write) and the
 * ESP-IDF driver is stable at 15 MHz  = 34.3 Hz refresh. Do NOT go lower: at
 * 12 MHz the panel loses sync and the screen shows rolling solid colours. */
#define UI_LCD_PCLK_HZ (15 * 1000 * 1000)
#include "../../lvgl.h"
#include "bsp_backlight.h"

/*********************
 *      DEFINES
 *********************/
static const char *TAG = "lv_port_disp";
static esp_lcd_panel_handle_t rgb_panel_handle;

/* Flush accounting, see lv_port_disp_take_flush_stats(). Written from the LVGL
 * task only; read from the same task when the periodic report runs. */
static uint32_t flush_area_count;
static uint32_t flush_pixel_count;
static uint32_t flush_pixel_max;
static uint32_t flush_max_w;
static uint32_t flush_max_h;

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/
// we use two semaphores to sync the VSYNC event and the LVGL task, to avoid potential tearing effect
#if CONFIG_EXAMPLE_AVOID_TEAR_EFFECT_WITH_SEM
SemaphoreHandle_t sem_vsync_end;
SemaphoreHandle_t sem_gui_ready;
#endif

/**********************
 *      MACROS
 **********************/
static bool rgb_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_data);
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void lv_port_disp_init(void)
{
    static lv_disp_drv_t disp_drv; // contains callback functions

#if CONFIG_EXAMPLE_AVOID_TEAR_EFFECT_WITH_SEM
    /* Create semaphores */
    sem_vsync_end = xSemaphoreCreateBinary();
    assert(sem_vsync_end);
    sem_gui_ready = xSemaphoreCreateBinary();
    assert(sem_gui_ready);
#endif

    ESP_ERROR_CHECK(bsp_backlight_init());

    ESP_LOGI(TAG, "Install RGB LCD panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16, // RGB565 in parallel mode, thus 16bit in width
        .psram_trans_align = 64,
        /* Bounce buffer depth, in pixels of one scan line.
         * Two buffers are allocated in internal RAM; the LCD GDMA always scans
         * out of them and the driver refills them from the PSRAM frame buffer.
         * That decouples the panel from live PSRAM bandwidth: a WiFi burst, an
         * SD write or the big repaint of a page switch can no longer make the
         * picture shift or jitter, it can at worst delay one refill.
         * 10 lines = 2 x 16 kB = 32 kB of internal RAM. This is the depth the
         * board shipped with before partial refresh was introduced. */
        .bounce_buffer_size_px = UI_LCD_BOUNCE_LINES * LCD_WIDTH,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num = GPIO_NUM_NC,
        .pclk_gpio_num = GPIO_LCD_PCLK,
        .vsync_gpio_num = GPIO_LCD_VSYNC,
        .hsync_gpio_num = GPIO_LCD_HSYNC,
        .de_gpio_num = GPIO_LCD_DE,
        .data_gpio_nums = {
            GPIO_LCD_B0,
            GPIO_LCD_B1,
            GPIO_LCD_B2,
            GPIO_LCD_B3,
            GPIO_LCD_B4,
            GPIO_LCD_G0,
            GPIO_LCD_G1,
            GPIO_LCD_G2,
            GPIO_LCD_G3,
            GPIO_LCD_G4,
            GPIO_LCD_G5,
            GPIO_LCD_R0,
            GPIO_LCD_R1,
            GPIO_LCD_R2,
            GPIO_LCD_R3,
            GPIO_LCD_R4,
        },
        .timings = {
            .h_res = LCD_WIDTH, .v_res = LCD_HEIGHT,
    // The following parameters should refer to LCD spec
#if ((LCD_5r0_800x480==1)||(LCD_4r3_800x480==1))
            .pclk_hz = UI_LCD_PCLK_HZ,
            .hsync_back_porch = 42,
            .hsync_front_porch = 20,
            .hsync_pulse_width = 1,
            .vsync_back_porch = 12,
            .vsync_front_porch = 4,
            .vsync_pulse_width = 10,
#elif(LCD_7r0_800x480==1) 
            .pclk_hz = 15 * 1000 * 1000,
            .hsync_back_porch = 32,
            .hsync_front_porch = 10,
            .hsync_pulse_width = 16,
            .vsync_back_porch = 12,
            .vsync_front_porch = 15,
            .vsync_pulse_width = 3,            
#elif(LCD_4r0_480x480==1) 
            .pclk_hz = 10 * 1000 * 1000,
            .hsync_back_porch = 32,
            .hsync_front_porch = 10,
            .hsync_pulse_width = 16,
            .vsync_back_porch = 12,
            .vsync_front_porch = 15,
            .vsync_pulse_width = 3,
            
#elif(LCD_5r0_480x854==1) //EP5008S
			.pclk_hz = 12 * 1000 * 1000,
			.hsync_back_porch = 30,
			.hsync_front_porch = 12,
			.hsync_pulse_width = 6,
			.vsync_back_porch = 30,
			.vsync_front_porch = 12,
			.vsync_pulse_width = 1,

#elif(LCD_4r3_480x272==1) 
            .pclk_hz = 10 * 1000 * 1000,
            .hsync_back_porch = 43,
            .hsync_front_porch = 75,
            .hsync_pulse_width = 4,
            .vsync_back_porch = 12,
            .vsync_front_porch = 8,
            .vsync_pulse_width = 4,



#endif

#if ((LCD_4r0_480x480==1)||(LCD_5r0_480x854==1))//鏃堕挓鏋佹€?
            .flags.pclk_active_neg = false, //true, // RGB data is clocked out on falling edge
#else
			.flags.pclk_active_neg = true,
#endif

        },
        .flags.fb_in_psram = true, // allocate frame buffer in PSRAM
/* Double banking belongs to the full-refresh strategy only: partial refresh
 * needs LVGL to own the draw buffers, so the panel gets a single frame buffer.
 * (CONFIG_EXAMPLE_DOUBLE_FB from sdkconfig is intentionally ignored here.) */
#if !UI_LCD_PARTIAL_REFRESH
        .flags.double_fb = true, // swapped at VSYNC: tear free full refresh
#endif // !UI_LCD_PARTIAL_REFRESH
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    rgb_panel_handle = panel_handle;

    /* Register event callbacks */
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = rgb_on_vsync_event,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, &disp_drv));

    /* Initialize RGB LCD panel */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    ESP_LOGI(TAG, "Turn on LCD backlight");
    bsp_backlight_on();
    ESP_LOGI(TAG, "LCD resolution: %dx%d", LCD_WIDTH, LCD_HEIGHT);
    /* Which pipeline is active - the first thing to check when the picture
     * tears or the UI feels slow. */
    ESP_LOGI(TAG, "refresh: %s, bounce %d lines, %s draw buffers",
             UI_LCD_PARTIAL_REFRESH ? "partial (damaged area only)" : "full screen",
             (int)UI_LCD_BOUNCE_LINES,
             UI_LCD_PARTIAL_REFRESH ? "own" : "frame");

    /* initialize LVGL draw buffers */
    static lv_disp_draw_buf_t disp_buf;
    void *buf1 = NULL;
    void *buf2 = NULL;
#if !UI_LCD_PARTIAL_REFRESH
    ESP_LOGI(TAG, "Use frame buffers as LVGL draw buffers");
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2));
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_WIDTH * LCD_HEIGHT);
#else
    ESP_LOGI(TAG, "Allocate separate LVGL draw buffers from PSRAM");
    /* Same shape as the reference firmware (config.h: BUFFER_LINES 480): two
     * full-screen draw buffers in PSRAM. A small damaged area is rendered into
     * them and flushed on its own, while a full page change (tab switch) still
     * needs a single pass instead of being chopped into chunks.
     * Cost: 2 x 768 kB of PSRAM. */
    const uint16_t fact = LCD_HEIGHT;
    buf1 = heap_caps_malloc(LCD_WIDTH * fact * sizeof(lv_color_t), MALLOC_CAP_SPIRAM); // MALLOC_CAP_SPIRAM
    assert(buf1);
    buf2 = heap_caps_malloc(LCD_WIDTH * fact * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf2);
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_WIDTH * fact);
#endif // !UI_LCD_PARTIAL_REFRESH

    /* Register display driver to LVGL */
    lv_disp_drv_init(&disp_drv);

    /*Set the resolution of the display*/
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    /*Used to copy the buffer's content to the display*/
    disp_drv.flush_cb = lvgl_flush_cb;
    /*Set a display buffer*/
    disp_drv.draw_buf = &disp_buf;

    disp_drv.user_data = panel_handle;

#if UI_LCD_PARTIAL_REFRESH
    /* Partial refresh: only the invalidated rectangle is rendered and pushed,
     * like the reference firmware. Requires the draw buffers NOT to be the
     * panel frame buffers, otherwise the DMA would race LVGL. */
    disp_drv.full_refresh = false;
#else
    disp_drv.full_refresh = true;
#endif

    /*Finally register the driver*/
    lv_disp_drv_register(&disp_drv);

}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static bool rgb_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_data)
{
    BaseType_t high_task_awoken = pdFALSE;
#if CONFIG_EXAMPLE_AVOID_TEAR_EFFECT_WITH_SEM
    if (xSemaphoreTakeFromISR(sem_gui_ready, &high_task_awoken) == pdTRUE)
    {
        xSemaphoreGiveFromISR(sem_vsync_end, &high_task_awoken);
    }
#endif
    return high_task_awoken == pdTRUE;
}

void lv_port_disp_take_flush_stats(uint32_t *areas, uint32_t *pixels,
                                   uint32_t *max_pixels, uint32_t *max_w, uint32_t *max_h)
{
    if (areas)      *areas = flush_area_count;
    if (pixels)     *pixels = flush_pixel_count;
    if (max_pixels) *max_pixels = flush_pixel_max;
    if (max_w)      *max_w = flush_max_w;
    if (max_h)      *max_h = flush_max_h;

    flush_area_count = 0;
    flush_pixel_count = 0;
    flush_pixel_max = 0;
    flush_max_w = 0;
    flush_max_h = 0;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
#if CONFIG_EXAMPLE_AVOID_TEAR_EFFECT_WITH_SEM
    xSemaphoreGive(sem_gui_ready);
    xSemaphoreTake(sem_vsync_end, portMAX_DELAY);
#endif
    // pass the draw buffer to the driver
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    {
        uint32_t w = (uint32_t)(offsetx2 - offsetx1 + 1);
        uint32_t h = (uint32_t)(offsety2 - offsety1 + 1);
        uint32_t pixels = w * h;

        flush_area_count++;
        flush_pixel_count += pixels;
        if (pixels > flush_pixel_max) {
            flush_pixel_max = pixels;
            flush_max_w = w;
            flush_max_h = h;
        }
    }
    lv_disp_flush_ready(drv);
}

/*OPTIONAL: GPU INTERFACE*/

/*If your MCU has hardware accelerator (GPU) then you can use it to fill a memory with a color*/
// static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//                     const lv_area_t * fill_area, lv_color_t color)
//{
//     /*It's an example code which should be done by your GPU*/
//     int32_t x, y;
//     dest_buf += dest_width * fill_area->y1; /*Go to the first line*/
//
//     for(y = fill_area->y1; y <= fill_area->y2; y++) {
//         for(x = fill_area->x1; x <= fill_area->x2; x++) {
//             dest_buf[x] = color;
//         }
//         dest_buf+=dest_width;    /*Go to the next line*/
//     }
// }

/**
 * @brief Task to generate ticks for LVGL.
 *
 * @param pvParam Not used.
 */
static void lv_tick_inc_cb(void *data)
{
    uint32_t tick_inc_period_ms = *((uint32_t *)data);

    lv_tick_inc(tick_inc_period_ms);
}

/**
 * @brief Create tick task for LVGL.
 *
 * @return esp_err_t
 */
esp_err_t lv_port_tick_init(void)
{
    static const uint32_t tick_inc_period_ms = 2;
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = lv_tick_inc_cb,
        .arg = &tick_inc_period_ms,
        // .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick", /* name is optional, but may help identify the timer when debugging */
                             // .skip_unhandled_events = true,
    };

    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    /* The timer has been created but is not running yet. Start the timer now */
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, tick_inc_period_ms * 1000));

    return ESP_OK;
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
esp_lcd_panel_handle_t lv_port_disp_get_panel_handle(void)
{
    return rgb_panel_handle;
}
