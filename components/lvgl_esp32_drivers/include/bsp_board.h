#ifndef _BSP_BOARD_H_
#define _BSP_BOARD_H_

#include "driver/gpio.h"

#define FUNC_LCD_EN     (1)

#define LCD_4r3_480x272   0   // 4.3 inch 480x272
#define LCD_4r3_800x480   0   // 4.3 inch 800x480
#define LCD_5r0_800x480   1   // 5.0 inch 800x480
#define LCD_5r0_480x854   0   // 5.0 inch 480x854
#define LCD_7r0_800x480   0   // 7.0 inch 800x480
#define LCD_7r0_1024x600  0   // 7.0 inch 1024x600
#define LCD_4r0_480x480   0   // 4.0 inch 480x480

extern esp_err_t ctpfalg; // Touch controller type

// Touch controller type
#define GT911      1
#define FT62XX     2
#define CST3240    3

#if (LCD_4r3_480x272 == 1)
#define LCD_WIDTH       (480)
#define LCD_HEIGHT      (272)
#elif (LCD_5r0_800x480 == 1)
#define LCD_WIDTH       (800)
#define LCD_HEIGHT      (480)
#elif (LCD_5r0_480x854 == 1)
#define LCD_WIDTH       (480)
#define LCD_HEIGHT      (854)
#elif (LCD_7r0_800x480 == 1)
#define LCD_WIDTH       (800)
#define LCD_HEIGHT      (480)
#elif (LCD_4r3_800x480 == 1)
#define LCD_WIDTH       (800)
#define LCD_HEIGHT      (480)
#elif (LCD_7r0_1024x600 == 1)
#define LCD_WIDTH       (1024)
#define LCD_HEIGHT      (600)
#elif (LCD_4r0_480x480 == 1)
#define LCD_WIDTH       (480)
#define LCD_HEIGHT      (480)
#endif

/*
 * Board IO mapping.
 *
 * The LCD panel data pins are labeled B3..B7, G2..G7 and R3..R7.
 * The ESP32-S3 RGB peripheral uses RGB565 in the order
 * B0..B4, G0..G5, R0..R4, so the low software bit is connected to
 * the lowest available panel bit (for example B0 -> panel B3).
 */
#define GPIO_LCD_BL     (GPIO_NUM_4)
#define LCD_BACKLIGHT_PWM_FREQ_HZ  (20000)
#define LCD_BACKLIGHT_PWM_MAX_PERCENT (100)
#define GPIO_LCD_RST    (GPIO_NUM_NC)

#define GPIO_LCD_DE     (GPIO_NUM_40)
#define GPIO_LCD_VSYNC  (GPIO_NUM_39)
#define GPIO_LCD_HSYNC  (GPIO_NUM_38)
#define GPIO_LCD_PCLK   (GPIO_NUM_45)

// Blue: panel B3..B7 -> RGB565 B0..B4
#define GPIO_LCD_B0     (GPIO_NUM_13)
#define GPIO_LCD_B1     (GPIO_NUM_14)
#define GPIO_LCD_B2     (GPIO_NUM_21)
#define GPIO_LCD_B3     (GPIO_NUM_47)
#define GPIO_LCD_B4     (GPIO_NUM_48)

// Green: panel G2..G7 -> RGB565 G0..G5
#define GPIO_LCD_G0     (GPIO_NUM_3)
#define GPIO_LCD_G1     (GPIO_NUM_46)
#define GPIO_LCD_G2     (GPIO_NUM_9)
#define GPIO_LCD_G3     (GPIO_NUM_10)
#define GPIO_LCD_G4     (GPIO_NUM_11)
#define GPIO_LCD_G5     (GPIO_NUM_12)

// Red: panel R3..R7 -> RGB565 R0..R4
#define GPIO_LCD_R0     (GPIO_NUM_17)
#define GPIO_LCD_R1     (GPIO_NUM_18)
#define GPIO_LCD_R2     (GPIO_NUM_8)
#define GPIO_LCD_R3     (GPIO_NUM_19)
#define GPIO_LCD_R4     (GPIO_NUM_20)

// Shared I2C bus
#define FUNC_I2C_EN     (1)
#define GPIO_I2C_SCL    (GPIO_NUM_16)
#define GPIO_I2C_SDA    (GPIO_NUM_15)

// Touch controller. It shares the board I2C bus.
#define GPIO_TOUCH_INT      (GPIO_NUM_41)
#define GPIO_TOUCH_RST      (GPIO_NUM_NC)
#define GPIO_TOUCH_I2C_SCL  (GPIO_I2C_SCL)
#define GPIO_TOUCH_I2C_SDA  (GPIO_I2C_SDA)

// SD card (1-bit SDMMC)
#define SDMMC_BUS_WIDTH (1)
#define GPIO_SDMMC_CLK  (GPIO_NUM_6)
#define GPIO_SDMMC_CMD  (GPIO_NUM_7)
#define GPIO_SDMMC_D0   (GPIO_NUM_5)
#define GPIO_SDMMC_D1   (GPIO_NUM_NC)
#define GPIO_SDMMC_D2   (GPIO_NUM_NC)
#define GPIO_SDMMC_D3   (GPIO_NUM_NC)
#define GPIO_SDMMC_DET  (GPIO_NUM_NC)

// I2S audio pins (available on the board connector)
#define GPIO_I2S_SCLK   (GPIO_NUM_1)
#define GPIO_I2S_LRCK   (GPIO_NUM_2)
#define GPIO_I2S_DSDIN  (GPIO_NUM_42)

#endif

