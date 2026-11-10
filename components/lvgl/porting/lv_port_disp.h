/**
 * @file lv_port_disp_templ.h
 *
 */

 /*Copy this file as "lv_port_disp.h" and set this value to "1" to enable content*/
#if 1

#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lvgl.h"

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

#include "bsp_board.h"
#include "esp_timer.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void lv_port_disp_init(void);
esp_lcd_panel_handle_t lv_port_disp_get_panel_handle(void);
esp_err_t lv_port_tick_init(void);

/* Flush accounting: what LVGL actually asked the panel to push since the last
 * call. This is how "is anything repainting the whole screen?" gets answered
 * with numbers instead of a guess. Any argument may be NULL. Passing the
 * function resets the counters. */
void lv_port_disp_take_flush_stats(uint32_t *areas, uint32_t *pixels,
                                   uint32_t *max_pixels, uint32_t *max_w, uint32_t *max_h);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_PORT_DISP_TEMPL_H*/

#endif /*Disable/Enable content*/
