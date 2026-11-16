#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define APP_FILES_MAX  (120)
#define APP_FILES_NAME (96)

typedef struct {
    uint64_t music;
    uint64_t video;
    uint64_t photo;
    uint64_t cache;
    uint64_t other;
    uint64_t junk;
    uint64_t bad;
    int junk_count;
    int bad_count;
    int file_count;
    uint32_t total_kb;
    uint32_t free_kb;
} app_files_stat_t;

const app_files_stat_t *app_files_scan(void);
const char *app_files_junk_name(int index);
uint64_t app_files_junk_size(int index);
const char *app_files_bad_name(int index);
uint64_t app_files_bad_size(int index);
int app_files_clean_junk(void);
int app_files_delete_bad(void);
void app_files_release(void);
