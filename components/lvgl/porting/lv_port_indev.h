
/**
 * @file lv_port_indev_templ.h
 *
 */

 /*Copy this file as "lv_port_indev.h" and set this value to "1" to enable content*/
#if 1

#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../../lvgl.h"

#include "gt911.h"
#include "FT62XX.h"
#include "CST3240.h"



#include "esp_err.h"
#include "esp_log.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void lv_port_indev_init(void);

/* Application hooks, registered explicitly at start-up.
 *
 * These started out as weak symbols: an empty stub in lv_port_indev.c and the
 * real function in main/ui/ui_feedback.c. That silently does nothing, because
 * a weak definition inside the component archive satisfies the reference, so
 * the linker never pulls the application object that holds the strong one -
 * the map file proves it:
 *
 *   app_touch_feedback   esp-idf/lvgl/liblvgl.a(lv_port_indev.c.obj)
 *
 * i.e. the empty stub was the one that ran, and every tap was silent while
 * nothing in the UI looked wrong. Registering the callback removes the
 * guesswork: either the pointer is set or it is not. */
typedef void (*lv_port_indev_tap_cb_t)(lv_coord_t x, lv_coord_t y);
typedef void (*lv_port_indev_activity_cb_t)(void);

/* Called once per finger-down, in display coordinates. */
void lv_port_indev_set_tap_cb(lv_port_indev_tap_cb_t cb);

/* Called on every finger-down, before the coordinates are read. The power
 * manager uses it to notice that the user is still there. */
void lv_port_indev_set_activity_cb(lv_port_indev_activity_cb_t cb);
/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_PORT_INDEV_TEMPL_H*/

#endif /*Disable/Enable content*/
