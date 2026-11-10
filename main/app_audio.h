#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t app_audio_init(void);
bool app_audio_ready(void);
bool app_audio_codec_present(void);
esp_err_t app_audio_write(const int16_t *samples, size_t stereo_frames);
esp_err_t app_audio_set_rate(uint32_t sample_rate);
uint32_t app_audio_rate(void);
