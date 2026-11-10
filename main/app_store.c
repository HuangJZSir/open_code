#include "app_store.h"

#include <stdio.h>
#include <string.h>

#include "bsp_board.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"

#define STORE_SD_MOUNT  "/sd"
#define STORE_INT_MOUNT "/album"
#define STORE_INT_LABEL "kids"

static const char *TAG = "store";

static char s_mount[16];
static bool s_on_sd;
static bool s_ready;
static SemaphoreHandle_t s_lock;

void app_store_lock(void)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

void app_store_unlock(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

static esp_err_t store_mount_sd(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card = NULL;

    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = 20000;

    slot_config.width = 1;
    slot_config.clk = GPIO_SDMMC_CLK;
    slot_config.cmd = GPIO_SDMMC_CMD;
    slot_config.d0 = GPIO_SDMMC_D0;
    slot_config.d1 = GPIO_NUM_NC;
    slot_config.d2 = GPIO_NUM_NC;
    slot_config.d3 = GPIO_NUM_NC;
    slot_config.d4 = GPIO_NUM_NC;
    slot_config.d5 = GPIO_NUM_NC;
    slot_config.d6 = GPIO_NUM_NC;
    slot_config.d7 = GPIO_NUM_NC;
    slot_config.cd = GPIO_NUM_NC;
    slot_config.wp = GPIO_NUM_NC;
    slot_config.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    if (esp_vfs_fat_sdmmc_mount(STORE_SD_MOUNT, &host, &slot_config,
                                &mount_config, &card) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t store_mount_internal(void)
{
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };
    wl_handle_t wl = WL_INVALID_HANDLE;

    if (esp_vfs_fat_spiflash_mount_rw_wl(STORE_INT_MOUNT, STORE_INT_LABEL,
                                        &mount_config, &wl) == ESP_OK) {
        return ESP_OK;
    }
    if (esp_vfs_fat_spiflash_format_rw_wl(STORE_INT_MOUNT, STORE_INT_LABEL) != ESP_OK) {
        return ESP_FAIL;
    }
    return esp_vfs_fat_spiflash_mount_rw_wl(STORE_INT_MOUNT, STORE_INT_LABEL,
                                            &mount_config, &wl);
}

esp_err_t app_store_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_log_level_set("sdmmc_req", ESP_LOG_WARN);
    esp_log_level_set("sdmmc_cmd", ESP_LOG_WARN);
    esp_log_level_set("sdmmc_periph", ESP_LOG_WARN);
    esp_log_level_set("sdmmc_common", ESP_LOG_WARN);
    esp_log_level_set("vfs_fat", ESP_LOG_WARN);

    s_lock = xSemaphoreCreateMutex();

    if (store_mount_sd() == ESP_OK) {
        s_on_sd = true;
        snprintf(s_mount, sizeof(s_mount), "%s", STORE_SD_MOUNT);
        ESP_LOGI(TAG, "sd card mounted");
    } else if (store_mount_internal() == ESP_OK) {
        s_on_sd = false;
        snprintf(s_mount, sizeof(s_mount), "%s", STORE_INT_MOUNT);
        ESP_LOGI(TAG, "internal flash mounted");
    } else {
        s_on_sd = false;
        snprintf(s_mount, sizeof(s_mount), "%s", STORE_INT_MOUNT);
        s_ready = true;
        ESP_LOGE(TAG, "no storage available");
        return ESP_FAIL;
    }

    s_ready = true;
    return ESP_OK;
}

bool app_store_ready(void)
{
    return s_ready;
}

bool app_store_on_sd(void)
{
    return s_on_sd;
}

const char *app_store_mount(void)
{
    return s_mount;
}

static void store_capacity(uint64_t *total, uint64_t *free_bytes)
{
    *total = 0;
    *free_bytes = 0;
    if (s_mount[0] != '\0') {
        esp_vfs_fat_info(s_mount, total, free_bytes);
    }
}

uint32_t app_store_free_kb(void)
{
    uint64_t total = 0;
    uint64_t free_bytes = 0;

    store_capacity(&total, &free_bytes);
    return (uint32_t)(free_bytes / 1024ULL);
}

uint32_t app_store_total_kb(void)
{
    uint64_t total = 0;
    uint64_t free_bytes = 0;

    store_capacity(&total, &free_bytes);
    return (uint32_t)(total / 1024ULL);
}

uint32_t app_store_used_kb(void)
{
    uint64_t total = 0;
    uint64_t free_bytes = 0;

    store_capacity(&total, &free_bytes);
    if (total < free_bytes) {
        return 0;
    }
    return (uint32_t)((total - free_bytes) / 1024ULL);
}
