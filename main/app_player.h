#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define APP_PLAYER_MAX_TRACKS 200

typedef enum {
    APP_PLAYER_STOPPED = 0,
    APP_PLAYER_PLAYING,
    APP_PLAYER_PAUSED,
} app_player_state_t;

esp_err_t app_player_init(void);

uint32_t app_player_scan(void);
uint32_t app_player_count(void);
const char *app_player_path(uint32_t index);
const char *app_player_name(uint32_t index);
const char *app_player_dir(void);

void app_player_play(uint32_t index);
void app_player_toggle(void);
void app_player_stop(void);
void app_player_next(void);
void app_player_prev(void);
void app_player_seek(uint32_t ms);

void app_player_set_repeat(bool on);
void app_player_set_shuffle(bool on);
bool app_player_repeat(void);
bool app_player_shuffle(void);

void app_player_set_volume(uint8_t percent);
uint8_t app_player_volume(void);
void app_player_test_tone(void);

app_player_state_t app_player_state(void);
uint32_t app_player_index(void);
uint32_t app_player_position_ms(void);
uint32_t app_player_total_ms(void);
uint32_t app_player_sample_rate(void);
uint8_t app_player_channels(void);
const char *app_player_status(void);
bool app_player_seeking(void);
