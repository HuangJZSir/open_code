#include "app_album.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_codec.h"
#include "app_store.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#define APP_ALBUM_DIR    "photo"
#define APP_ALBUM_THUMBS "photo/thumb160"
#define APP_ALBUM_CACHE  "photo/cache"
#define APP_ALBUM_BG     (0x1082)
#define APP_ALBUM_PATH   (512)
#define APP_ALBUM_DEPTH  (3)
#define APP_ALBUM_DEC_W  (200)
#define APP_ALBUM_DEC_H  (120)

static const char *TAG = "album";

static char (*s_names)[APP_ALBUM_NAME];
static uint16_t *s_dec;
static uint16_t *s_thumb;
static int s_count;
static bool s_ready;
static volatile bool s_dirty;
static char s_dir[96];

bool app_album_take_dirty(void)
{
    bool d = s_dirty;

    s_dirty = false;
    return d;
}

const char *app_album_dir(void)
{
    if (s_dir[0] == '\0') {
        snprintf(s_dir, sizeof(s_dir), "%s", app_store_mount());
    }
    return s_dir;
}

static void album_sort(void)
{
    char tmp[APP_ALBUM_NAME];
    int i;
    int j;

    for (i = 1; i < s_count; i++) {
        memcpy(tmp, s_names[i], APP_ALBUM_NAME);
        j = i - 1;
        while (j >= 0 && strcasecmp(s_names[j], tmp) > 0) {
            memcpy(s_names[j + 1], s_names[j], APP_ALBUM_NAME);
            j--;
        }
        memcpy(s_names[j + 1], tmp, APP_ALBUM_NAME);
    }
}

esp_err_t app_album_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    s_names = heap_caps_calloc(APP_ALBUM_MAX, APP_ALBUM_NAME, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_dec = heap_caps_malloc((size_t)APP_ALBUM_DEC_W * APP_ALBUM_DEC_H * sizeof(uint16_t),
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_thumb = heap_caps_malloc((size_t)APP_ALBUM_THUMB_W * APP_ALBUM_THUMB_H * sizeof(uint16_t),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_names == NULL || s_dec == NULL || s_thumb == NULL) {
        ESP_LOGE(TAG, "no psram for album names=%p dec=%p thumb=%p", s_names, s_dec, s_thumb);
        return ESP_ERR_NO_MEM;
    }
    s_ready = true;
    ESP_LOGI(TAG, "buffer ready dec=%u thumb=%u", (unsigned)(APP_ALBUM_DEC_W * APP_ALBUM_DEC_H * 2),
             (unsigned)(APP_ALBUM_THUMB_W * APP_ALBUM_THUMB_H * 2));
    return ESP_OK;
}

static void album_mkdir(const char *path)
{
    if (access(path, F_OK) != 0) {
        mkdir(path, 0777);
    }
}

static void album_scan_dir(const char *rel, int depth)
{
    char full[APP_ALBUM_PATH];
    DIR *dir;
    struct dirent *ent;

    if (rel[0] == '\0') {
        snprintf(full, sizeof(full), "%s", app_store_mount());
    } else {
        snprintf(full, sizeof(full), "%s/%s", app_store_mount(), rel);
    }
    dir = opendir(full);
    if (dir == NULL) {
        return;
    }

    while ((ent = readdir(dir)) != NULL && s_count < APP_ALBUM_MAX) {
        char child[APP_ALBUM_PATH];
        size_t len;

        if (ent->d_name[0] == '.') {
            continue;
        }
        if (rel[0] == '\0') {
            snprintf(child, sizeof(child), "%s", ent->d_name);
        } else {
            snprintf(child, sizeof(child), "%s/%s", rel, ent->d_name);
        }
        len = strlen(child);

        if (app_codec_supported(ent->d_name)) {
            if (len >= APP_ALBUM_NAME) {
                ESP_LOGW(TAG, "name too long, skip: %s", child);
                continue;
            }
            memcpy(s_names[s_count], child, len + 1);
            s_count++;
            continue;
        }
        if (depth >= APP_ALBUM_DEPTH) {
            continue;
        }
        if (strncasecmp(ent->d_name, "thumb", 5) == 0) {
            continue;
        }
        ESP_LOGI(TAG, "tree: %s/ (dir)", child);
        album_scan_dir(child, depth + 1);
    }
    closedir(dir);
}

int app_album_scan(void)
{
    char dir[96] = {0};
    char thumbs[96] = {0};
    int i;

    s_count = 0;
    if (!app_store_ready()) {
        return 0;
    }
    if (!s_ready && app_album_init() != ESP_OK) {
        return 0;
    }
    snprintf(dir, sizeof(dir), "%s/%s", app_store_mount(), APP_ALBUM_DIR);
    album_mkdir(dir);
    snprintf(thumbs, sizeof(thumbs), "%s/%s", app_store_mount(), APP_ALBUM_THUMBS);
    album_mkdir(thumbs);
    snprintf(thumbs, sizeof(thumbs), "%s/%s", app_store_mount(), APP_ALBUM_CACHE);
    album_mkdir(thumbs);

    album_scan_dir("", 0);
    album_sort();
    s_dirty = false;

    ESP_LOGI(TAG, "found %d photo(s) on %s", s_count, app_store_mount());
    for (i = 0; i < s_count && i < 20; i++) {
        char full[APP_ALBUM_PATH];
        uint8_t head[4] = {0};
        FILE *fp;
        size_t got = 0;
        long size = -1;

        snprintf(full, sizeof(full), "%s/%s", app_store_mount(), s_names[i]);
        fp = fopen(full, "rb");
        if (fp != NULL) {
            got = fread(head, 1, sizeof(head), fp);
            fseek(fp, 0, SEEK_END);
            size = ftell(fp);
            fclose(fp);
        }
        ESP_LOGI(TAG, "  [%d] %s size=%ld head=%02x%02x%02x%02x n=%u", i, s_names[i], size,
                 head[0], head[1], head[2], head[3], (unsigned)got);
    }
    return s_count;
}

int app_album_count(void)
{
    return s_count;
}

const char *app_album_name(int index)
{
    if (index < 0 || index >= s_count) {
        return NULL;
    }
    return s_names[index];
}

bool app_album_path(int index, char *out, size_t size)
{
    if (out == NULL || index < 0 || index >= s_count) {
        return false;
    }
    snprintf(out, size, "%s/%s", app_store_mount(), s_names[index]);
    return true;
}

static bool album_thumb_path_by_name(const char *name, char *out, size_t size)
{
    char base[APP_ALBUM_NAME];
    const char *leaf;
    char *dot;

    if (name == NULL || out == NULL) {
        return false;
    }
    leaf = strrchr(name, '/');
    leaf = (leaf != NULL) ? leaf + 1 : name;
    snprintf(base, sizeof(base), "%s", leaf);
    dot = strrchr(base, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    snprintf(out, size, "%s/%s/%s.565", app_store_mount(), APP_ALBUM_THUMBS, base);
    return true;
}

static bool album_thumb_path(int index, char *out, size_t size)
{
    if (index < 0 || index >= s_count) {
        return false;
    }
    return album_thumb_path_by_name(s_names[index], out, size);
}

static bool album_cache_path_by_name(const char *name, char *out, size_t size)
{
    char base[APP_ALBUM_NAME];
    const char *leaf;
    char *dot;

    if (name == NULL || out == NULL) {
        return false;
    }
    leaf = strrchr(name, '/');
    leaf = (leaf != NULL) ? leaf + 1 : name;
    snprintf(base, sizeof(base), "%s", leaf);
    dot = strrchr(base, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    snprintf(out, size, "%s/%s/%s.565", app_store_mount(), APP_ALBUM_CACHE, base);
    return true;
}

esp_err_t app_album_add_file(const char *tmp_path, const char *name)
{
    char path[160] = {0};
    char thumb[160] = {0};
    char dirs[96] = {0};
    esp_err_t err;
    int64_t t0;

    if (tmp_path == NULL || name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!app_codec_supported(name)) {
        remove(tmp_path);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!s_ready && app_album_init() != ESP_OK) {
        remove(tmp_path);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "add %s start", name);
    snprintf(path, sizeof(path), "%s/%s/%s", app_store_mount(), APP_ALBUM_DIR, name);
    album_thumb_path_by_name(name, thumb, sizeof(thumb));
    snprintf(dirs, sizeof(dirs), "%s/%s", app_store_mount(), APP_ALBUM_DIR);
    album_mkdir(dirs);
    snprintf(dirs, sizeof(dirs), "%s/%s", app_store_mount(), APP_ALBUM_THUMBS);
    album_mkdir(dirs);
    snprintf(dirs, sizeof(dirs), "%s/%s", app_store_mount(), APP_ALBUM_CACHE);
    album_mkdir(dirs);

    t0 = esp_timer_get_time();
    err = app_codec_decode(tmp_path, s_dec, APP_ALBUM_DEC_W, APP_ALBUM_DEC_H, APP_ALBUM_BG);
    ESP_LOGI(TAG, "add %s decode=%d in %u ms", name, (int)err,
             (unsigned)((esp_timer_get_time() - t0) / 1000));
    if (err != ESP_OK) {
        app_codec_log_file(tmp_path);
        remove(tmp_path);
        return err;
    }

    t0 = esp_timer_get_time();
    app_codec_thumb(s_dec, APP_ALBUM_DEC_W, APP_ALBUM_DEC_H, s_thumb, APP_ALBUM_THUMB_W, APP_ALBUM_THUMB_H);
    ESP_LOGI(TAG, "add %s thumb build in %u ms", name, (unsigned)((esp_timer_get_time() - t0) / 1000));

    t0 = esp_timer_get_time();
    err = app_codec_write565(thumb, s_thumb, APP_ALBUM_THUMB_W, APP_ALBUM_THUMB_H);
    ESP_LOGI(TAG, "add %s thumb write=%d in %u ms", name, (int)err,
             (unsigned)((esp_timer_get_time() - t0) / 1000));

    t0 = esp_timer_get_time();
    if (strcmp(tmp_path, path) != 0) {
        remove(path);
        if (rename(tmp_path, path) != 0) {
            remove(tmp_path);
            ESP_LOGW(TAG, "add %s rename fail", name);
            return ESP_FAIL;
        }
    }
    ESP_LOGI(TAG, "add %s rename in %u ms", name, (unsigned)((esp_timer_get_time() - t0) / 1000));
    s_dirty = true;
    ESP_LOGI(TAG, "add %s ok", name);
    return ESP_OK;
}

esp_err_t app_album_add(const char *name)
{
    char path[160] = {0};

    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(path, sizeof(path), "%s/%s/%s", app_store_mount(), APP_ALBUM_DIR, name);
    return app_album_add_file(path, name);
}

esp_err_t app_album_load(int index, uint16_t *dst, int dst_w, int dst_h)
{
    char path[160] = {0};
    char cache[160] = {0};

    if (!app_album_path(index, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (album_cache_path_by_name(s_names[index], cache, sizeof(cache))) {
        if (app_codec_read565(cache, dst, dst_w, dst_h) == ESP_OK) {
            return ESP_OK;
        }
    }
    return app_codec_decode(path, dst, dst_w, dst_h, APP_ALBUM_BG);
}

esp_err_t app_album_thumb(int index, uint16_t *dst, int dst_w, int dst_h)
{
    char path[160] = {0};
    char thumb[160] = {0};
    esp_err_t err;

    if (dst == NULL || !album_thumb_path(index, thumb, sizeof(thumb))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (app_codec_read565(thumb, dst, dst_w, dst_h) == ESP_OK) {
        ESP_LOGI(TAG, "thumb cached %s", s_names[index]);
        return ESP_OK;
    }

    if (!app_album_path(index, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }
    err = app_codec_decode(path, dst, dst_w, dst_h, APP_ALBUM_BG);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "thumb decode fail %s err=%d", path, (int)err);
        app_codec_log_file(path);
        return err;
    }
    app_codec_write565(thumb, dst, dst_w, dst_h);
    ESP_LOGI(TAG, "thumb made %s", path);
    return ESP_OK;
}

esp_err_t app_album_delete(int index)
{
    char path[160] = {0};
    char thumb[160] = {0};

    if (!app_album_path(index, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (album_thumb_path(index, thumb, sizeof(thumb))) {
        remove(thumb);
    }
    if (remove(path) != 0) {
        return ESP_FAIL;
    }
    s_dirty = true;
    ESP_LOGI(TAG, "deleted %s", path);
    return ESP_OK;
}
