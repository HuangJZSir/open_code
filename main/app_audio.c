#include "app_audio.h"

#include <string.h>

#include "bsp_board.h"
#include "bsp_i2c.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"

#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_BCLK_GPIO   ((gpio_num_t)GPIO_I2S_SCLK)
#define AUDIO_LRCK_GPIO   ((gpio_num_t)GPIO_I2S_LRCK)
#define AUDIO_DOUT_GPIO   ((gpio_num_t)GPIO_I2S_DSDIN)
#define AUDIO_CODEC_ADDR  0x18

#define ES8311_REG00  0x00
#define ES8311_REG01  0x01
#define ES8311_REG02  0x02
#define ES8311_REG03  0x03
#define ES8311_REG04  0x04
#define ES8311_REG05  0x05
#define ES8311_REG06  0x06
#define ES8311_REG07  0x07
#define ES8311_REG08  0x08
#define ES8311_REG09  0x09
#define ES8311_REG0A  0x0A
#define ES8311_REG0B  0x0B
#define ES8311_REG0C  0x0C
#define ES8311_REG0D  0x0D
#define ES8311_REG0E  0x0E
#define ES8311_REG10  0x10
#define ES8311_REG11  0x11
#define ES8311_REG12  0x12
#define ES8311_REG13  0x13
#define ES8311_REG14  0x14
#define ES8311_REG15  0x15
#define ES8311_REG16  0x16
#define ES8311_REG17  0x17
#define ES8311_REG1B  0x1B
#define ES8311_REG1C  0x1C
#define ES8311_REG32  0x32
#define ES8311_REG37  0x37
#define ES8311_REG44  0x44
#define ES8311_REG45  0x45

#define ES8311_DAC_VOL_UNITY 0xBF

#define AUDIO_DMA_DESC_NUM  8
#define AUDIO_DMA_FRAME_NUM 256

static const char *TAG = "audio";

static i2c_bus_device_handle_t s_codec;
static i2s_chan_handle_t s_tx;
static bool s_ready;
static bool s_codec_present;
static uint32_t s_rate = AUDIO_SAMPLE_RATE;

static esp_err_t codec_write(uint8_t reg, uint8_t value)
{
    if (s_codec == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_bus_write_bytes(s_codec, reg, 1, &value);
}

static esp_err_t codec_read(uint8_t reg, uint8_t *value)
{
    if (s_codec == NULL || value == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_bus_read_bytes(s_codec, reg, 1, value);
}

static void codec_update_bits(uint8_t reg, uint8_t and_mask, uint8_t or_mask)
{
    uint8_t value = 0;

    if (codec_read(reg, &value) != ESP_OK) {
        return;
    }
    value = (uint8_t)((value & and_mask) | or_mask);
    codec_write(reg, value);
}

static bool codec_probe(void)
{
    for (int attempt = 0; attempt < 5; attempt++) {
        uint8_t echo = 0;

        (void)codec_write(ES8311_REG32, ES8311_DAC_VOL_UNITY);
        if (codec_read(ES8311_REG32, &echo) == ESP_OK && echo == ES8311_DAC_VOL_UNITY) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

static esp_err_t codec_init(void)
{
    uint8_t regv = 0;

    if (bsp_i2c_add_device(&s_codec, AUDIO_CODEC_ADDR) != ESP_OK || s_codec == NULL) {
        s_codec = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    s_codec_present = codec_probe();
    if (!s_codec_present) {
        return ESP_ERR_NOT_FOUND;
    }

    codec_write(ES8311_REG44, 0x08);
    codec_write(ES8311_REG44, 0x08);

    codec_write(ES8311_REG01, 0x30);
    codec_write(ES8311_REG02, 0x00);
    codec_write(ES8311_REG03, 0x10);
    codec_write(ES8311_REG16, 0x24);
    codec_write(ES8311_REG04, 0x10);
    codec_write(ES8311_REG05, 0x00);
    codec_write(ES8311_REG0B, 0x00);
    codec_write(ES8311_REG0C, 0x00);
    codec_write(ES8311_REG10, 0x1F);
    codec_write(ES8311_REG11, 0x7F);

    codec_write(ES8311_REG00, 0x80);
    codec_update_bits(ES8311_REG00, 0xBF, 0x00);

    codec_write(ES8311_REG01, 0xBF);

    if (codec_read(ES8311_REG02, &regv) == ESP_OK) {
        regv = (uint8_t)((regv & 0x07) | 0x18);
        codec_write(ES8311_REG02, regv);
    }
    codec_write(ES8311_REG05, 0x00);
    if (codec_read(ES8311_REG03, &regv) == ESP_OK) {
        regv = (uint8_t)((regv & 0x80) | 0x10);
        codec_write(ES8311_REG03, regv);
    }
    if (codec_read(ES8311_REG04, &regv) == ESP_OK) {
        regv = (uint8_t)((regv & 0x80) | 0x10);
        codec_write(ES8311_REG04, regv);
    }
    if (codec_read(ES8311_REG07, &regv) == ESP_OK) {
        regv = (uint8_t)(regv & 0xC0);
        codec_write(ES8311_REG07, regv);
    }
    codec_write(ES8311_REG08, 0xFF);
    if (codec_read(ES8311_REG06, &regv) == ESP_OK) {
        regv = (uint8_t)((regv & 0xE0) | 0x03);
        codec_write(ES8311_REG06, regv);
    }

    codec_write(ES8311_REG13, 0x10);
    codec_write(ES8311_REG1B, 0x0A);
    codec_write(ES8311_REG1C, 0x6A);
    codec_write(ES8311_REG44, 0x58);

    if (codec_read(ES8311_REG09, &regv) == ESP_OK) {
        regv = (uint8_t)((regv & 0xFC) | 0x0C);
        regv = (uint8_t)(regv & ~0x40);
        codec_write(ES8311_REG09, regv);
    }
    if (codec_read(ES8311_REG0A, &regv) == ESP_OK) {
        regv = (uint8_t)((regv & 0xFC) | 0x0C);
        regv = (uint8_t)(regv | 0x40);
        codec_write(ES8311_REG0A, regv);
    }

    codec_write(ES8311_REG17, 0xBF);
    codec_write(ES8311_REG0E, 0x02);
    codec_write(ES8311_REG12, 0x00);
    codec_write(ES8311_REG14, 0x1A);
    codec_write(ES8311_REG0D, 0x01);
    codec_write(ES8311_REG15, 0x40);
    codec_write(ES8311_REG37, 0x08);
    codec_write(ES8311_REG45, 0x00);
    codec_write(ES8311_REG32, ES8311_DAC_VOL_UNITY);

    return ESP_OK;
}

static esp_err_t i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_BCLK_GPIO,
            .ws = AUDIO_LRCK_GPIO,
            .dout = AUDIO_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    esp_err_t err;

    chan_cfg.dma_desc_num = AUDIO_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = AUDIO_DMA_FRAME_NUM;
    chan_cfg.auto_clear = true;

    err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (err != ESP_OK) {
        s_tx = NULL;
        return err;
    }
    err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err != ESP_OK) {
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return err;
    }
    return ESP_OK;
}

esp_err_t app_audio_init(void)
{
    esp_err_t err;

    s_ready = false;
    s_codec_present = false;

    err = i2s_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s init failed: %d", (int)err);
        return err;
    }

    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s enable failed: %d", (int)err);
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return err;
    }

    if (codec_init() != ESP_OK) {
        ESP_LOGW(TAG, "ES8311 not answering at 0x%02X, clocks still running", AUDIO_CODEC_ADDR);
        s_ready = true;
        return ESP_OK;
    }

    s_ready = true;
    ESP_LOGI(TAG, "ES8311 ready, %d Hz, clocks free running", AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

bool app_audio_ready(void)
{
    return s_ready;
}

bool app_audio_codec_present(void)
{
    return s_codec_present;
}

esp_err_t app_audio_write(const int16_t *samples, size_t stereo_frames)
{
    size_t bytes;
    size_t written = 0;

    if (!s_ready || s_tx == NULL || samples == NULL || stereo_frames == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    bytes = stereo_frames * 2 * sizeof(int16_t);
    while (written < bytes) {
        size_t chunk = 0;
        esp_err_t err = i2s_channel_write(s_tx, (const uint8_t *)samples + written,
                                          bytes - written, &chunk, pdMS_TO_TICKS(200));

        if (err != ESP_OK || chunk == 0) {
            return (err == ESP_OK) ? ESP_FAIL : err;
        }
        written += chunk;
    }
    return ESP_OK;
}

esp_err_t app_audio_set_rate(uint32_t sample_rate)
{
    i2s_std_clk_config_t clk_cfg;
    esp_err_t err;

    if (!s_ready || s_tx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sample_rate < 8000) {
        sample_rate = 8000;
    }
    if (sample_rate > 96000) {
        sample_rate = 96000;
    }
    if (sample_rate == s_rate) {
        return ESP_OK;
    }

    clk_cfg = (i2s_std_clk_config_t)I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);

    err = i2s_channel_disable(s_tx);
    if (err == ESP_OK) {
        err = i2s_channel_reconfig_std_clock(s_tx, &clk_cfg);
    }
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_tx);
    }
    if (err == ESP_OK) {
        s_rate = sample_rate;
        ESP_LOGI(TAG, "sample rate -> %lu Hz", (unsigned long)sample_rate);
    }
    return err;
}

uint32_t app_audio_rate(void)
{
    return s_rate;
}
