#include "app_files.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_store.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define FILES_DEPTH (3)
#define FILES_PATH  (256)

static const char *TAG = "files";

static app_files_stat_t s_stat;
static char (*s_junk)[APP_FILES_NAME];
static uint32_t *s_junk_size;
static char (*s_bad)[APP_FILES_NAME];
static uint32_t *s_bad_size;

static bool files_tables(void)
{
    if (s_junk != NULL && s_bad != NULL && s_junk_size != NULL && s_bad_size != NULL) {
        return true;
    }
    s_junk = heap_caps_calloc(APP_FILES_MAX, APP_FILES_NAME, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_bad = heap_caps_calloc(APP_FILES_MAX, APP_FILES_NAME, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_junk_size = heap_caps_calloc(APP_FILES_MAX, sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_bad_size = heap_caps_calloc(APP_FILES_MAX, sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_junk == NULL || s_bad == NULL || s_junk_size == NULL || s_bad_size == NULL) {
        ESP_LOGE(TAG, "no psram for file tables");
        return false;
    }
    return true;
}

static bool files_join(char *out, size_t size, const char *base, const char *leaf)
{
    size_t bl = strlen(base);
    size_t ll = strlen(leaf);

    if (bl + ll + 2 > size) {
        return false;
    }
    memcpy(out, base, bl);
    out[bl] = '/';
    memcpy(out + bl + 1, leaf, ll + 1);
    return true;
}

static bool files_copy(char *out, size_t size, const char *src)
{
    size_t len = strlen(src);

    if (len + 1 > size) {
        return false;
    }
    memcpy(out, src, len + 1);
    return true;
}

static const char *files_ext(const char *name)
{
    const char *dot = strrchr(name, '.');

    return (dot != NULL) ? dot : "";
}

static bool files_is_music(const char *ext)
{
    return strcasecmp(ext, ".mp3") == 0 || strcasecmp(ext, ".flac") == 0 ||
           strcasecmp(ext, ".wav") == 0 || strcasecmp(ext, ".m4a") == 0 ||
           strcasecmp(ext, ".aac") == 0 || strcasecmp(ext, ".ts") == 0 ||
           strcasecmp(ext, ".ogg") == 0;
}

static bool files_is_video(const char *ext)
{
    return strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".mjpeg") == 0 ||
           strcasecmp(ext, ".mjpg") == 0 || strcasecmp(ext, ".avi") == 0 ||
           strcasecmp(ext, ".mov") == 0 || strcasecmp(ext, ".mkv") == 0;
}

static bool files_is_photo(const char *ext)
{
    return strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0 ||
           strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".bmp") == 0 ||
           strcasecmp(ext, ".gif") == 0;
}

static bool files_is_junk(const char *ext)
{
    return strcasecmp(ext, ".565") == 0 || strcasecmp(ext, ".tmp") == 0;
}

static bool files_head_ok(const char *full, const char *ext)
{
    uint8_t head[4] = {0};
    FILE *fp;
    size_t got;

    fp = fopen(full, "rb");
    if (fp == NULL) {
        return false;
    }
    got = fread(head, 1, sizeof(head), fp);
    fclose(fp);
    if (got < 4) {
        return false;
    }
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0 ||
        strcasecmp(ext, ".mjpeg") == 0 || strcasecmp(ext, ".mjpg") == 0) {
        return head[0] == 0xFF && head[1] == 0xD8;
    }
    if (strcasecmp(ext, ".png") == 0) {
        return head[0] == 0x89 && head[1] == 0x50 && head[2] == 0x4E && head[3] == 0x47;
    }
    if (strcasecmp(ext, ".bmp") == 0) {
        return head[0] == 0x42 && head[1] == 0x4D;
    }
    if (strcasecmp(ext, ".gif") == 0) {
        return head[0] == 'G' && head[1] == 'I' && head[2] == 'F';
    }
    return true;
}

static void files_add_junk(const char *rel, uint32_t size)
{
    if (s_stat.junk_count >= APP_FILES_MAX) {
        return;
    }
    if (!files_copy(s_junk[s_stat.junk_count], APP_FILES_NAME, rel)) {
        return;
    }
    s_junk_size[s_stat.junk_count] = size;
    s_stat.junk_count++;
    s_stat.junk += size;
}

static void files_add_bad(const char *rel, uint32_t size)
{
    if (s_stat.bad_count >= APP_FILES_MAX) {
        return;
    }
    if (!files_copy(s_bad[s_stat.bad_count], APP_FILES_NAME, rel)) {
        return;
    }
    s_bad_size[s_stat.bad_count] = size;
    s_stat.bad_count++;
    s_stat.bad += size;
}

static void files_walk(const char *rel, int depth)
{
    char dir_full[FILES_PATH];
    char child[FILES_PATH];
    char full[FILES_PATH];
    DIR *dir;
    struct dirent *ent;

    if (rel[0] == '\0') {
        files_copy(dir_full, sizeof(dir_full), app_store_mount());
    } else if (!files_join(dir_full, sizeof(dir_full), app_store_mount(), rel)) {
        return;
    }
    dir = opendir(dir_full);
    if (dir == NULL) {
        return;
    }

    while ((ent = readdir(dir)) != NULL) {
        struct stat st;
        const char *ext;
        uint32_t size;

        if (ent->d_name[0] == '.') {
            continue;
        }
        if (strcasecmp(ent->d_name, "System Volume Information") == 0) {
            continue;
        }
        if (rel[0] == '\0') {
            if (!files_copy(child, sizeof(child), ent->d_name)) {
                continue;
            }
        } else if (!files_join(child, sizeof(child), rel, ent->d_name)) {
            continue;
        }
        if (!files_join(full, sizeof(full), app_store_mount(), child)) {
            continue;
        }
        if (stat(full, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (depth < FILES_DEPTH) {
                files_walk(child, depth + 1);
            }
            continue;
        }

        size = (uint32_t)st.st_size;
        ext = files_ext(ent->d_name);
        s_stat.file_count++;
        if (files_is_music(ext)) {
            s_stat.music += size;
        } else if (files_is_video(ext)) {
            s_stat.video += size;
        } else if (files_is_photo(ext)) {
            s_stat.photo += size;
        } else if (strcasecmp(ext, ".565") == 0) {
            s_stat.cache += size;
        } else {
            s_stat.other += size;
        }

        if (files_is_junk(ext)) {
            files_add_junk(child, size);
        } else if (size == 0 || !files_head_ok(full, ext)) {
            if (files_is_photo(ext) || files_is_video(ext)) {
                files_add_bad(child, size);
            }
        }
    }
    closedir(dir);
}

const app_files_stat_t *app_files_scan(void)
{
    memset(&s_stat, 0, sizeof(s_stat));
    if (!app_store_ready()) {
        return &s_stat;
    }
    if (!files_tables()) {
        return &s_stat;
    }
    s_stat.total_kb = app_store_total_kb();
    s_stat.free_kb = app_store_free_kb();
    files_walk("", 0);
    ESP_LOGI(TAG, "scan: %d files, junk=%d bad=%d free=%luKB total=%luKB", s_stat.file_count,
             s_stat.junk_count, s_stat.bad_count, (unsigned long)s_stat.free_kb,
             (unsigned long)s_stat.total_kb);
    return &s_stat;
}

const app_files_stat_t *app_files_stat(void)
{
    return &s_stat;
}

const char *app_files_junk_name(int index)
{
    if (index < 0 || index >= s_stat.junk_count) {
        return NULL;
    }
    return s_junk[index];
}

uint64_t app_files_junk_size(int index)
{
    if (index < 0 || index >= s_stat.junk_count) {
        return 0;
    }
    return s_junk_size[index];
}

const char *app_files_bad_name(int index)
{
    if (index < 0 || index >= s_stat.bad_count) {
        return NULL;
    }
    return s_bad[index];
}

uint64_t app_files_bad_size(int index)
{
    if (index < 0 || index >= s_stat.bad_count) {
        return 0;
    }
    return s_bad_size[index];
}

static int files_remove_table(char (*table)[APP_FILES_NAME], int count)
{
    char full[FILES_PATH];
    int i;
    int done = 0;

    for (i = 0; i < count; i++) {
        if (!files_join(full, sizeof(full), app_store_mount(), table[i])) {
            continue;
        }
        if (unlink(full) == 0) {
            done++;
        } else {
            ESP_LOGW(TAG, "remove fail %s", full);
        }
    }
    ESP_LOGI(TAG, "removed %d of %d", done, count);
    return done;
}

int app_files_clean_junk(void)
{
    int done;

    if (!files_tables()) {
        return 0;
    }
    done = files_remove_table(s_junk, s_stat.junk_count);
    app_files_scan();
    return done;
}

int app_files_delete_bad(void)
{
    int done;

    if (!files_tables()) {
        return 0;
    }
    done = files_remove_table(s_bad, s_stat.bad_count);
    app_files_scan();
    return done;
}

void app_files_release(void)
{
    if (s_junk != NULL) {
        heap_caps_free(s_junk);
        s_junk = NULL;
    }
    if (s_bad != NULL) {
        heap_caps_free(s_bad);
        s_bad = NULL;
    }
    if (s_junk_size != NULL) {
        heap_caps_free(s_junk_size);
        s_junk_size = NULL;
    }
    if (s_bad_size != NULL) {
        heap_caps_free(s_bad_size);
        s_bad_size = NULL;
    }
    memset(&s_stat, 0, sizeof(s_stat));
}
