#include "bsp_backlight.h"
#include "bsp_board.h"
#include "driver/ledc.h"
#include <stdbool.h>

#define BSP_BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BSP_BL_LEDC_TIMER       LEDC_TIMER_0
#define BSP_BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BSP_BL_LEDC_RESOLUTION  LEDC_TIMER_10_BIT
#define BSP_BL_LEDC_MAX_DUTY    ((1U << 10) - 1U)
#define BSP_BL_PWM_FREQ_HZ      LCD_BACKLIGHT_PWM_FREQ_HZ

static bool backlight_initialized = false;
static uint8_t backlight_percent = 0;

esp_err_t bsp_backlight_init(void)
{
    if (backlight_initialized) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_config = {
        .speed_mode = BSP_BL_LEDC_MODE,
        .duty_resolution = BSP_BL_LEDC_RESOLUTION,
        .timer_num = BSP_BL_LEDC_TIMER,
        .freq_hz = BSP_BL_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    esp_err_t ret = ledc_timer_config(&timer_config);
    if (ret != ESP_OK) {
        return ret;
    }

    ledc_channel_config_t channel_config = {
        .gpio_num = GPIO_LCD_BL,
        .speed_mode = BSP_BL_LEDC_MODE,
        .channel = BSP_BL_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BSP_BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };

    ret = ledc_channel_config(&channel_config);
    if (ret != ESP_OK) {
        return ret;
    }

    backlight_initialized = true;
    return bsp_backlight_set_percent(100);
}

esp_err_t bsp_backlight_set_percent(uint8_t percent)
{
    if (!backlight_initialized) {
        esp_err_t ret = bsp_backlight_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (percent > 100) {
        percent = 100;
    }

    uint32_t duty = ((uint32_t)percent * BSP_BL_LEDC_MAX_DUTY) / 100U;

    esp_err_t ret = ledc_set_duty(BSP_BL_LEDC_MODE, BSP_BL_LEDC_CHANNEL, duty);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = ledc_update_duty(BSP_BL_LEDC_MODE, BSP_BL_LEDC_CHANNEL);
    if (ret != ESP_OK) {
        return ret;
    }

    backlight_percent = percent;
    return ESP_OK;
}

uint8_t bsp_backlight_get_percent(void)
{
    return backlight_percent;
}

void bsp_backlight_on(void)
{
    bsp_backlight_set_percent(100);
}

void bsp_backlight_off(void)
{
    bsp_backlight_set_percent(0);
}

