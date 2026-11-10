#include "app_player.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "app_audio.h"
#include "app_store.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define PLAYER_PATH_LEN     160
#define PLAYER_TITLE_LEN    128
#define PLAYER_SCAN_DEPTH   3
#define PLAYER_IN_SIZE      (32 * 1024)
#define PLAYER_PCM_MIN      (16 * 1024)
#define PLAYER_OUT_FRAMES   1024
#define PLAYER_RING_FRAMES  (65536U)
#define PLAYER_PREFILL      (16384U)
#define PLAYER_WRITE_FRAMES 512
#define PLAYER_TASK_STACK   (10 * 1024)
#define PLAYER_TASK_PRIO    5
#define PLAYER_OUT_STACK    (4 * 1024)
#define PLAYER_OUT_PRIO     18
#define PLAYER_CORE         1
#define PLAYER_SKIP_LIMIT   6

typedef enum {
    PLAYER_CMD_PLAY = 1,
    PLAYER_CMD_TOGGLE,
    PLAYER_CMD_STOP,
    PLAYER_CMD_NEXT,
    PLAYER_CMD_PREV,
    PLAYER_CMD_SEEK,
} player_cmd_t;

typedef struct {
    uint8_t cmd;
    uint32_t value;
} player_msg_t;

static const char *TAG = "player";

static const char *s_audio_ext[] = {
    "mp3", "flac", "wav", "mp4", "m4a", "aac", "ts",
};

static char (*s_paths)[PLAYER_PATH_LEN];
static char (*s_titles)[PLAYER_TITLE_LEN];
static uint32_t s_count;
static char s_root[32];

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static TaskHandle_t s_out_task;

static FILE *s_fp;
static esp_audio_simple_dec_handle_t s_dec;
static uint8_t *s_in;
static uint8_t *s_pcm;
static uint32_t s_pcm_size;
static int16_t s_out[PLAYER_OUT_FRAMES * 2];
static uint32_t s_out_n;
static int16_t *s_ring;
static volatile uint32_t s_ring_head;
static volatile uint32_t s_ring_tail;
static volatile uint32_t s_pending_rate;
static int16_t s_wbuf[PLAYER_WRITE_FRAMES * 2];
static volatile bool s_need_prefill;
static uint32_t s_prefill_waits;
static volatile uint32_t s_stat_dec_us;
static volatile uint32_t s_stat_io_us;
static volatile uint32_t s_stat_out_us;
static volatile uint32_t s_stat_under;
static volatile uint32_t s_stat_ring_min;
static int64_t s_stat_t0;
static volatile uint32_t s_tone_frames;
static uint32_t s_tone_step;
static uint32_t s_tone_phase;

static uint32_t s_raw_off;
static uint32_t s_raw_len;
static bool s_eof;
static bool s_info_ready;
static bool s_stream_error;
static uint8_t s_idle;

static volatile uint8_t s_state;
static volatile uint32_t s_index;
static volatile uint32_t s_rate;
static volatile uint32_t s_channels;
static volatile uint32_t s_bits;
static volatile uint32_t s_pos_frames;
static volatile uint32_t s_played_frames;
static volatile uint32_t s_total_ms;
static volatile uint32_t s_bytes_read;
static volatile uint32_t s_bytes_total;
static volatile bool s_skipping;
static volatile uint32_t s_skip_target_ms;
static volatile uint32_t s_skip_done_ms;
static uint32_t s_skip_yield_ms;
static volatile uint32_t s_fail_count;
static const char *volatile s_status;

static volatile bool s_repeat;
static volatile bool s_shuffle;
static volatile uint8_t s_volume = 70;

static void player_open(uint32_t index);
static void player_close(void);
static void player_finish(bool failed);

static void ring_reset(void)
{
    s_ring_tail = s_ring_head;
}

static uint32_t ring_used(void)
{
    return s_ring_head - s_ring_tail;
}

static bool ring_push(const int16_t *src, uint32_t frames)
{
    while (frames > 0) {
        uint32_t free_frames = PLAYER_RING_FRAMES - ring_used();
        uint32_t n;
        uint32_t pos;
        uint32_t first;

        if (free_frames == 0) {
            if (s_state != APP_PLAYER_PLAYING) {
                return false;
            }
            vTaskDelay(1);
            continue;
        }

        n = (frames < free_frames) ? frames : free_frames;
        pos = s_ring_head & (PLAYER_RING_FRAMES - 1);
        first = PLAYER_RING_FRAMES - pos;
        if (first > n) {
            first = n;
        }
        memcpy(s_ring + (pos * 2), src, (size_t)first * 4);
        if (n > first) {
            memcpy(s_ring, src + (first * 2), (size_t)(n - first) * 4);
        }
        s_ring_head += n;
        src += n * 2;
        frames -= n;
    }
    return true;
}

static uint32_t ring_pop(int16_t *dst, uint32_t max_frames)
{
    uint32_t used = ring_used();
    uint32_t n = (used < max_frames) ? used : max_frames;
    uint32_t pos;
    uint32_t first;

    if (n == 0) {
        return 0;
    }

    pos = s_ring_tail & (PLAYER_RING_FRAMES - 1);
    first = PLAYER_RING_FRAMES - pos;
    if (first > n) {
        first = n;
    }
    memcpy(dst, s_ring + (pos * 2), (size_t)first * 4);
    if (n > first) {
        memcpy(dst + (first * 2), s_ring, (size_t)(n - first) * 4);
    }
    s_ring_tail += n;
    return n;
}

static bool ext_match(const char *name)
{
    const char *dot = strrchr(name, '.');
    size_t i;

    if (dot == NULL) {
        return false;
    }
    dot++;
    for (i = 0; i < sizeof(s_audio_ext) / sizeof(s_audio_ext[0]); i++) {
        if (strcasecmp(dot, s_audio_ext[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void title_from_name(const char *name, char *out, size_t size)
{
    size_t len = strlen(name);

    while (len > 0 && name[len - 1] == ' ') {
        len--;
    }
    if (len >= size) {
        len = size - 1;
    }
    memcpy(out, name, len);
    out[len] = '\0';

    for (size_t i = 0; i < len; i++) {
        if (out[i] == '.') {
            out[i] = '\0';
            len = i;
            break;
        }
    }
    while (len > 0 && out[len - 1] == ' ') {
        out[len - 1] = '\0';
        len--;
    }
    if (len == 0) {
        snprintf(out, size, "%s", name);
    }
}

static bool track_add(const char *path, const char *name)
{
    const char *base;

    if (s_count >= APP_PLAYER_MAX_TRACKS) {
        return false;
    }
    if (strlen(path) >= PLAYER_PATH_LEN) {
        return false;
    }

    base = strrchr(name, '/');
    base = (base != NULL) ? (base + 1) : name;

    snprintf(s_paths[s_count], PLAYER_PATH_LEN, "%s", path);
    title_from_name(base, s_titles[s_count], PLAYER_TITLE_LEN);
    s_count++;
    return true;
}

static void scan_dir(const char *dir, int depth)
{
    DIR *handle;
    struct dirent *entry;

    if (s_count >= APP_PLAYER_MAX_TRACKS) {
        return;
    }

    handle = opendir(dir);
    if (handle == NULL) {
        return;
    }

    while ((entry = readdir(handle)) != NULL) {
        char full[PLAYER_PATH_LEN];
        struct stat st;

        if (entry->d_name[0] == '.' || entry->d_name[0] == '_' ||
            entry->d_name[0] == '~') {
            continue;
        }
        if (snprintf(full, sizeof(full), "%s/%s", dir, entry->d_name) >= (int)sizeof(full)) {
            continue;
        }
        if (stat(full, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (depth < PLAYER_SCAN_DEPTH) {
                scan_dir(full, depth + 1);
            }
            continue;
        }
        if (!S_ISREG(st.st_mode) || !ext_match(entry->d_name)) {
            continue;
        }
        if (!track_add(full, entry->d_name)) {
            break;
        }
    }

    closedir(handle);
}

static void track_sort(void)
{
    uint32_t i;

    for (i = 1; i < s_count; i++) {
        char path[PLAYER_PATH_LEN];
        char title[PLAYER_TITLE_LEN];
        uint32_t j = i;

        memcpy(path, s_paths[i], PLAYER_PATH_LEN);
        memcpy(title, s_titles[i], PLAYER_TITLE_LEN);

        while (j > 0 && strcasecmp(s_titles[j - 1], title) > 0) {
            memcpy(s_paths[j], s_paths[j - 1], PLAYER_PATH_LEN);
            memcpy(s_titles[j], s_titles[j - 1], PLAYER_TITLE_LEN);
            j--;
        }
        memcpy(s_paths[j], path, PLAYER_PATH_LEN);
        memcpy(s_titles[j], title, PLAYER_TITLE_LEN);
    }
}

static esp_audio_simple_dec_type_t dec_type_of(const char *path)
{
    const char *dot = strrchr(path, '.');

    if (dot == NULL) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
    }
    dot++;

    if (strcasecmp(dot, "mp3") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    }
    if (strcasecmp(dot, "flac") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
    }
    if (strcasecmp(dot, "wav") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    }
    if (strcasecmp(dot, "mp4") == 0 || strcasecmp(dot, "m4a") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
    }
    if (strcasecmp(dot, "aac") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    }
    if (strcasecmp(dot, "ts") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_TS;
    }
    return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
}

static int16_t sample_to_s16(const uint8_t *ptr, uint32_t bytes)
{
    if (bytes >= 3) {
        return (int16_t)((ptr[bytes - 2] << 8) | ptr[bytes - 1]);
    }
    return (int16_t)((ptr[0] | (ptr[1] << 8)));
}

static int16_t apply_gain(int16_t value)
{
    int32_t v = (int32_t)value * (int32_t)s_volume;
    int32_t out = v / 100;

    if (out > 32767) {
        out = 32767;
    }
    if (out < -32768) {
        out = -32768;
    }
    return (int16_t)out;
}

static void player_reset_stream(void)
{
    s_raw_off = 0;
    s_raw_len = 0;
    s_eof = false;
    s_info_ready = false;
    s_stream_error = false;
    s_idle = 0;
    s_out_n = 0;
    s_pos_frames = 0;
    s_played_frames = 0;
    s_total_ms = 0;
    s_bytes_read = 0;
    s_rate = 48000;
    s_channels = 2;
    s_bits = 16;
}

static void player_close(void)
{
    if (s_dec != NULL) {
        esp_audio_simple_dec_close(s_dec);
        s_dec = NULL;
    }
    if (s_fp != NULL) {
        fclose(s_fp);
        s_fp = NULL;
    }
}

static void player_flush(void)
{
    if (s_out_n == 0) {
        return;
    }
    if (!s_skipping && s_ring != NULL) {
        (void)ring_push(s_out, s_out_n);
    }
    s_out_n = 0;
}

static void player_render(uint8_t *data, uint32_t bytes)
{
    uint32_t bytes_per_sample = (s_bits + 7) / 8;
    uint32_t frame_bytes;
    uint32_t frames;
    uint32_t i;

    if (bytes_per_sample == 0) {
        bytes_per_sample = 2;
    }
    frame_bytes = bytes_per_sample * s_channels;
    if (frame_bytes == 0) {
        return;
    }
    frames = bytes / frame_bytes;

    for (i = 0; i < frames; i++) {
        const uint8_t *p = data + (i * frame_bytes);
        int16_t left = sample_to_s16(p, bytes_per_sample);
        int16_t right = (s_channels >= 2) ? sample_to_s16(p + bytes_per_sample, bytes_per_sample)
                                          : left;

        s_out[s_out_n * 2] = apply_gain(left);
        s_out[s_out_n * 2 + 1] = apply_gain(right);
        s_out_n++;
        if (s_out_n >= PLAYER_OUT_FRAMES) {
            player_flush();
        }
    }

    s_pos_frames += frames;

    if (s_skipping) {
        uint32_t step_ms = (uint32_t)(((uint64_t)frames * 1000ULL) / (s_rate == 0 ? 48000 : s_rate));

        s_skip_done_ms += step_ms;
        s_skip_yield_ms += step_ms;
        if (s_skip_yield_ms >= 200) {
            s_skip_yield_ms = 0;
            vTaskDelay(1);
        }
        if (s_skip_done_ms >= s_skip_target_ms) {
            s_skipping = false;
            s_status = "播放中";
        }
    } else if (s_bytes_read > 0 && s_pos_frames > 0 && s_rate > 0) {
        uint32_t pos_ms = (uint32_t)(((uint64_t)s_pos_frames * 1000ULL) / s_rate);

        if (pos_ms > 1500 && s_bytes_total > 0) {
            uint32_t total = (uint32_t)(((uint64_t)pos_ms * s_bytes_total) / s_bytes_read);

            if (total > s_total_ms) {
                s_total_ms = total;
            }
        }
    }
}

static void player_info_read(void)
{
    esp_audio_simple_dec_info_t info;

    memset(&info, 0, sizeof(info));
    if (esp_audio_simple_dec_get_info(s_dec, &info) != ESP_AUDIO_ERR_OK) {
        return;
    }
    if (info.sample_rate == 0) {
        return;
    }

    s_rate = info.sample_rate;
    s_channels = (info.channel == 0) ? 2 : info.channel;
    s_bits = (info.bits_per_sample == 0) ? 16 : info.bits_per_sample;

    if (s_rate != app_audio_rate()) {
        uint32_t waited;

        s_pending_rate = s_rate;
        for (waited = 0; waited < 100; waited++) {
            if (s_pending_rate == 0) {
                break;
            }
            vTaskDelay(1);
        }
    }

    ESP_LOGI(TAG, "stream: %lu Hz, %lu ch, %lu bit, i2s %lu Hz",
             (unsigned long)s_rate, (unsigned long)s_channels, (unsigned long)s_bits,
             (unsigned long)app_audio_rate());
}

static void player_tone_fill(uint32_t frames)
{
    uint32_t i;

    for (i = 0; i < frames; i++) {
        int32_t value;

        s_tone_phase += s_tone_step;
        if (s_tone_phase >= 65536U) {
            s_tone_phase -= 65536U;
        }
        if (s_tone_phase < 32768U) {
            value = (int32_t)s_tone_phase - 16384;
        } else {
            value = 49152 - (int32_t)s_tone_phase;
        }
        s_wbuf[i * 2] = apply_gain((int16_t)value);
        s_wbuf[i * 2 + 1] = apply_gain((int16_t)value);
    }
}

static void player_out_task(void *arg)
{
    (void)arg;

    s_stat_t0 = esp_timer_get_time();

    for (;;) {
        uint32_t frames;
        uint32_t used;
        int64_t t0;

        if (s_tone_frames > 0) {
            uint32_t n = (s_tone_frames > PLAYER_WRITE_FRAMES) ? PLAYER_WRITE_FRAMES : s_tone_frames;

            player_tone_fill(n);
            (void)app_audio_write(s_wbuf, n);
            s_tone_frames -= n;
            continue;
        }

        if (s_pending_rate != 0 && ring_used() == 0) {
            uint32_t rate = s_pending_rate;

            (void)app_audio_set_rate(rate);
            s_pending_rate = 0;
        }

        if (s_state != APP_PLAYER_PLAYING) {
            s_need_prefill = true;
            s_prefill_waits = 0;
            vTaskDelay(1);
            continue;
        }

        used = ring_used();
        if (used < s_stat_ring_min) {
            s_stat_ring_min = used;
        }

        if (s_need_prefill) {
            if (used >= PLAYER_PREFILL || ++s_prefill_waits > 300) {
                s_need_prefill = false;
                s_prefill_waits = 0;
            } else {
                vTaskDelay(1);
                continue;
            }
        }

        frames = ring_pop(s_wbuf, PLAYER_WRITE_FRAMES);
        if (frames == 0) {
            s_stat_under++;
            vTaskDelay(1);
            continue;
        }

        t0 = esp_timer_get_time();
        if (app_audio_write(s_wbuf, frames) == ESP_OK) {
            s_played_frames += frames;
        }
        s_stat_out_us += (uint32_t)(esp_timer_get_time() - t0);

        if (esp_timer_get_time() - s_stat_t0 >= 5000000) {
            uint32_t span_ms = (uint32_t)((esp_timer_get_time() - s_stat_t0) / 1000);

            ESP_LOGI(TAG, "perf %lums: dec %lums io %lums, out %lums, ring %lu, min %lu, under %lu",
                     (unsigned long)span_ms,
                     (unsigned long)(s_stat_dec_us / 1000),
                     (unsigned long)(s_stat_io_us / 1000),
                     (unsigned long)(s_stat_out_us / 1000),
                     (unsigned long)ring_used(),
                     (unsigned long)((s_stat_ring_min == 0xFFFFFFFFU) ? 0 : s_stat_ring_min),
                     (unsigned long)s_stat_under);
            s_stat_dec_us = 0;
            s_stat_io_us = 0;
            s_stat_out_us = 0;
            s_stat_under = 0;
            s_stat_ring_min = 0xFFFFFFFFU;
            s_stat_t0 = esp_timer_get_time();
        }
    }
}

static bool player_pump(void)
{
    esp_audio_simple_dec_raw_t raw;
    esp_audio_simple_dec_out_t out;
    esp_audio_err_t ret;
    uint32_t consumed;
    uint32_t decoded;
    bool progressed;

    if (s_raw_len == 0 && !s_eof) {
        int64_t io_t0 = esp_timer_get_time();
        size_t got = fread(s_in, 1, PLAYER_IN_SIZE, s_fp);

        s_stat_io_us += (uint32_t)(esp_timer_get_time() - io_t0);
        s_raw_off = 0;
        s_raw_len = (uint32_t)got;
        s_bytes_read += (uint32_t)got;
        if (got < PLAYER_IN_SIZE) {
            s_eof = true;
        }
    }

    memset(&raw, 0, sizeof(raw));
    raw.buffer = s_in + s_raw_off;
    raw.len = s_raw_len;
    raw.eos = s_eof;

    memset(&out, 0, sizeof(out));
    out.buffer = s_pcm;
    out.len = s_pcm_size;

    ret = esp_audio_simple_dec_process(s_dec, &raw, &out);

    if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
        uint32_t need = (out.needed_size > s_pcm_size) ? out.needed_size : (s_pcm_size * 2);
        uint8_t *grown = heap_caps_realloc(s_pcm, need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

        if (grown == NULL) {
            grown = heap_caps_realloc(s_pcm, need, MALLOC_CAP_8BIT);
        }
        if (grown == NULL) {
            s_status = "内存不足";
            s_stream_error = true;
            return false;
        }
        s_pcm = grown;
        s_pcm_size = need;
        return true;
    }

    consumed = raw.consumed;
    decoded = out.decoded_size;

    if (consumed == 0 && decoded == 0 && s_eof && s_raw_len == 0) {
        return false;
    }

    if (ret != ESP_AUDIO_ERR_OK && ret != ESP_AUDIO_ERR_DATA_LACK) {
        if (s_eof) {
            return false;
        }
        s_status = "解码失败";
        s_stream_error = true;
        ESP_LOGW(TAG, "decode error %d", (int)ret);
        return false;
    }

    s_raw_off += consumed;
    s_raw_len -= consumed;

    if (decoded > 0) {
        if (!s_info_ready) {
            s_info_ready = true;
            player_info_read();
        }
        player_render(out.buffer, decoded);
    }

    progressed = (consumed > 0) || (decoded > 0);
    if (!progressed) {
        if (++s_idle > 6) {
            ESP_LOGW(TAG, "decoder stalled at %lu/%lu",
                     (unsigned long)s_bytes_read, (unsigned long)s_bytes_total);
            return false;
        }
    } else {
        s_idle = 0;
    }

    return true;
}

static void player_begin_stream(void)
{
    player_reset_stream();
    ring_reset();
    s_skipping = false;
    s_skip_target_ms = 0;
    s_skip_done_ms = 0;
    s_skip_yield_ms = 0;
}

static void player_open(uint32_t index)
{
    esp_audio_simple_dec_type_t type;
    esp_audio_simple_dec_cfg_t cfg;
    struct stat st;

    player_close();
    player_begin_stream();

    if (index >= s_count) {
        s_state = APP_PLAYER_STOPPED;
        s_status = "没有曲目";
        return;
    }

    s_index = index;
    type = dec_type_of(s_paths[index]);
    if (type == ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) {
        s_state = APP_PLAYER_STOPPED;
        s_status = "格式不支持";
        return;
    }

    s_fp = fopen(s_paths[index], "rb");
    if (s_fp == NULL) {
        s_state = APP_PLAYER_STOPPED;
        s_status = "文件打不开";
        ESP_LOGW(TAG, "open failed: %s", s_paths[index]);
        return;
    }

    if (stat(s_paths[index], &st) == 0) {
        s_bytes_total = (uint32_t)st.st_size;
    } else {
        s_bytes_total = 0;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.dec_type = type;
    cfg.dec_cfg = NULL;
    cfg.cfg_size = 0;

    if (esp_audio_simple_dec_open(&cfg, &s_dec) != ESP_AUDIO_ERR_OK) {
        s_dec = NULL;
        player_close();
        s_state = APP_PLAYER_STOPPED;
        s_status = "解码器打不开";
        return;
    }

    s_state = APP_PLAYER_PLAYING;
    s_status = "播放中";
    ESP_LOGI(TAG, "play %lu %s", (unsigned long)index, s_paths[index]);
}

static uint32_t player_pick_next(uint32_t index, bool forward)
{
    uint32_t count = s_count;

    if (count == 0) {
        return 0;
    }
    if (s_shuffle && count > 1) {
        uint32_t pick = esp_random() % count;

        if (pick == index) {
            pick = (pick + 1) % count;
        }
        return pick;
    }
    if (forward) {
        if (index + 1 < count) {
            return index + 1;
        }
        return s_repeat ? 0 : count;
    }
    return (index == 0) ? (count - 1) : (index - 1);
}

static void player_finish(bool failed)
{
    uint32_t next;
    uint32_t waited;

    player_flush();

    s_need_prefill = false;

    if (!failed) {
        for (waited = 0; waited < 400; waited++) {
            if (ring_used() == 0) {
                break;
            }
            vTaskDelay(1);
        }
    }
    ring_reset();

    player_close();

    if (failed) {
        s_fail_count++;
        if (s_fail_count > PLAYER_SKIP_LIMIT) {
            s_fail_count = 0;
            s_state = APP_PLAYER_STOPPED;
            s_status = "多首无法播放";
            return;
        }
    } else {
        s_fail_count = 0;
    }

    next = player_pick_next(s_index, true);
    if (next >= s_count) {
        s_state = APP_PLAYER_STOPPED;
        s_status = "播放完毕";
        return;
    }
    player_open(next);
}

static void player_do_seek(uint32_t ms)
{
    bool was_paused;

    if (s_count == 0) {
        return;
    }
    if (s_state != APP_PLAYER_PLAYING && s_state != APP_PLAYER_PAUSED) {
        return;
    }

    was_paused = (s_state == APP_PLAYER_PAUSED);

    player_open(s_index);
    if (s_state != APP_PLAYER_PLAYING) {
        return;
    }
    s_skip_target_ms = ms;
    s_skip_done_ms = 0;
    s_skipping = true;

    if (was_paused) {
        s_state = APP_PLAYER_PAUSED;
        s_status = "已暂停";
    } else {
        s_status = "跳转中";
    }
}

static void player_handle(const player_msg_t *msg)
{
    switch (msg->cmd) {
    case PLAYER_CMD_PLAY:
        s_fail_count = 0;
        player_open(msg->value);
        break;
    case PLAYER_CMD_TOGGLE:
        if (s_state == APP_PLAYER_PLAYING) {
            s_state = APP_PLAYER_PAUSED;
            s_status = "已暂停";
        } else if (s_state == APP_PLAYER_PAUSED) {
            s_state = APP_PLAYER_PLAYING;
            s_status = "播放中";
        } else if (s_count > 0) {
            s_fail_count = 0;
            player_open(s_index < s_count ? s_index : 0);
        }
        break;
    case PLAYER_CMD_STOP:
        player_close();
        player_reset_stream();
        ring_reset();
        s_skipping = false;
        s_state = APP_PLAYER_STOPPED;
        s_status = "已停止";
        break;
    case PLAYER_CMD_NEXT:
        if (s_count > 0) {
            uint32_t next = player_pick_next(s_index, true);

            s_fail_count = 0;
            player_open(next >= s_count ? 0 : next);
        }
        break;
    case PLAYER_CMD_PREV:
        if (s_count > 0) {
            s_fail_count = 0;
            player_open(player_pick_next(s_index, false));
        }
        break;
    case PLAYER_CMD_SEEK:
        player_do_seek(msg->value);
        break;
    default:
        break;
    }
}

static void player_task(void *arg)
{
    player_msg_t msg;

    (void)arg;

    for (;;) {
        if (s_state == APP_PLAYER_PLAYING) {
            int64_t t0;

            while (xQueueReceive(s_queue, &msg, 0) == pdTRUE) {
                player_handle(&msg);
            }
            if (s_state != APP_PLAYER_PLAYING) {
                continue;
            }

            t0 = esp_timer_get_time();
            if (!player_pump()) {
                s_stat_dec_us += (uint32_t)(esp_timer_get_time() - t0);
                player_finish(s_stream_error);
            } else {
                s_stat_dec_us += (uint32_t)(esp_timer_get_time() - t0);
                player_flush();
            }
        } else {
            player_flush();
            if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(40)) == pdTRUE) {
                player_handle(&msg);
            }
        }
    }
}

static void player_send(uint8_t cmd, uint32_t value)
{
    player_msg_t msg;

    if (s_queue == NULL) {
        return;
    }
    msg.cmd = cmd;
    msg.value = value;
    (void)xQueueSend(s_queue, &msg, 0);
}

esp_err_t app_player_init(void)
{
    if (s_queue != NULL) {
        return ESP_OK;
    }

    s_status = "就绪";
    s_state = APP_PLAYER_STOPPED;
    s_volume = 70;

    s_paths = heap_caps_malloc((size_t)APP_PLAYER_MAX_TRACKS * PLAYER_PATH_LEN,
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_titles = heap_caps_malloc((size_t)APP_PLAYER_MAX_TRACKS * PLAYER_TITLE_LEN,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_in = heap_caps_malloc(PLAYER_IN_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_in == NULL) {
        s_in = heap_caps_malloc(PLAYER_IN_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    s_pcm = heap_caps_malloc(PLAYER_PCM_MIN, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_pcm == NULL) {
        s_pcm = heap_caps_malloc(PLAYER_PCM_MIN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    s_ring = heap_caps_malloc((size_t)PLAYER_RING_FRAMES * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (s_paths == NULL || s_titles == NULL || s_in == NULL || s_pcm == NULL || s_ring == NULL) {
        ESP_LOGE(TAG, "buffer alloc failed");
        return ESP_ERR_NO_MEM;
    }
    s_pcm_size = PLAYER_PCM_MIN;
    s_ring_head = 0;
    s_ring_tail = 0;
    s_pending_rate = 0;
    s_stat_ring_min = 0xFFFFFFFFU;
    s_stat_dec_us = 0;
    s_stat_out_us = 0;
    s_stat_under = 0;
    s_need_prefill = true;

    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();

    s_queue = xQueueCreate(8, sizeof(player_msg_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(player_task, "player", PLAYER_TASK_STACK, NULL, PLAYER_TASK_PRIO,
                                &s_task, PLAYER_CORE) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_FAIL;
    }
    if (xTaskCreatePinnedToCore(player_out_task, "player_out", PLAYER_OUT_STACK, NULL, PLAYER_OUT_PRIO,
                                &s_out_task, PLAYER_CORE) != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ring %lu frames, dma writer prio %d on core %d",
             (unsigned long)PLAYER_RING_FRAMES, PLAYER_OUT_PRIO, PLAYER_CORE);
    return ESP_OK;
}

uint32_t app_player_scan(void)
{
    const char *mount = app_store_mount();
    uint32_t waited;

    if (s_state != APP_PLAYER_STOPPED) {
        player_send(PLAYER_CMD_STOP, 0);
        for (waited = 0; waited < 120; waited++) {
            if (s_state == APP_PLAYER_STOPPED) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    s_count = 0;
    if (mount == NULL || mount[0] == '\0') {
        return 0;
    }
    snprintf(s_root, sizeof(s_root), "%s", mount);

    scan_dir(s_root, 0);
    track_sort();

    s_index = 0;
    player_reset_stream();
    ring_reset();
    ESP_LOGI(TAG, "scan %s -> %lu track(s)", s_root, (unsigned long)s_count);
    return s_count;
}

uint32_t app_player_count(void)
{
    return s_count;
}

const char *app_player_path(uint32_t index)
{
    if (index >= s_count) {
        return "";
    }
    return s_paths[index];
}

const char *app_player_name(uint32_t index)
{
    if (index >= s_count) {
        return "";
    }
    return s_titles[index];
}

const char *app_player_dir(void)
{
    return s_root;
}

void app_player_play(uint32_t index)
{
    if (index < s_count) {
        player_send(PLAYER_CMD_PLAY, index);
    }
}

void app_player_toggle(void)
{
    player_send(PLAYER_CMD_TOGGLE, 0);
}

void app_player_stop(void)
{
    player_send(PLAYER_CMD_STOP, 0);
}

void app_player_next(void)
{
    player_send(PLAYER_CMD_NEXT, 0);
}

void app_player_prev(void)
{
    player_send(PLAYER_CMD_PREV, 0);
}

void app_player_seek(uint32_t ms)
{
    player_send(PLAYER_CMD_SEEK, ms);
}

void app_player_set_repeat(bool on)
{
    s_repeat = on;
}

void app_player_set_shuffle(bool on)
{
    s_shuffle = on;
}

bool app_player_repeat(void)
{
    return s_repeat;
}

bool app_player_shuffle(void)
{
    return s_shuffle;
}

void app_player_set_volume(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_volume = percent;
}

uint8_t app_player_volume(void)
{
    return s_volume;
}

app_player_state_t app_player_state(void)
{
    return (app_player_state_t)s_state;
}

uint32_t app_player_index(void)
{
    return (s_index < s_count) ? s_index : 0;
}

uint32_t app_player_position_ms(void)
{
    uint32_t rate = (s_rate == 0) ? 48000 : s_rate;

    return (uint32_t)(((uint64_t)s_played_frames * 1000ULL) / rate);
}

uint32_t app_player_total_ms(void)
{
    return s_total_ms;
}

uint32_t app_player_sample_rate(void)
{
    return s_rate;
}

uint8_t app_player_channels(void)
{
    return (uint8_t)s_channels;
}

const char *app_player_status(void)
{
    return (const char *)s_status;
}

bool app_player_seeking(void)
{
    return s_skipping;
}

void app_player_test_tone(void)
{
    uint32_t rate = app_audio_rate();

    if (rate == 0) {
        rate = 48000;
    }

    s_tone_step = (uint32_t)(((uint64_t)1000 * 65536ULL) / rate);
    s_tone_phase = 0;
    s_tone_frames = rate * 3U;

    ESP_LOGI(TAG, "tone test: %lu frames at %lu Hz", (unsigned long)s_tone_frames,
             (unsigned long)rate);
}
