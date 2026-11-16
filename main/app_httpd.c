#include "app_httpd.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app_cast.h"
#include "app_album.h"
#include "app_player.h"
#include "app_settings.h"
#include "app_store.h"
#include "app_weather.h"
#include "app_time.h"
#include "app_wifi.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#define APP_HTTP_IO_SIZE   (16 * 1024)
#define APP_HTTP_FILE_MAX  (24 * 1024 * 1024)
#define APP_HTTP_FREE_MIN_KB (3072)
#define APP_HTTP_JSON_MAX  (6144)
#define APP_HTTP_NAME_MAX  (120)
#define APP_HTTP_LIST_MAX  (60)

static const char *TAG = "httpd";

static httpd_handle_t s_server;
static char *s_iobuf;
static char s_json[APP_HTTP_JSON_MAX];

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} sb_t;

static void sb_init(sb_t *sb, char *buf, size_t cap)
{
    sb->buf = buf;
    sb->cap = cap;
    sb->len = 0;
    if (cap > 0) {
        buf[0] = '\0';
    }
}

static void sb_raw(sb_t *sb, const char *text)
{
    size_t n;

    if (sb->cap == 0 || text == NULL) {
        return;
    }

    n = strlen(text);
    if (sb->len + n >= sb->cap) {
        n = sb->cap - sb->len - 1;
    }
    if (n > 0) {
        memcpy(sb->buf + sb->len, text, n);
        sb->len += n;
        sb->buf[sb->len] = '\0';
    }
}

static void sb_num(sb_t *sb, unsigned long value)
{
    char tmp[24];

    snprintf(tmp, sizeof(tmp), "%lu", value);
    sb_raw(sb, tmp);
}

static void sb_json_str(sb_t *sb, const char *text)
{
    const char *p;

    if (text == NULL) {
        return;
    }
    for (p = text; *p != '\0'; p++) {
        char one[2];

        one[0] = *p;
        one[1] = '\0';
        if (*p == '"' || *p == '\\') {
            sb_raw(sb, "\\");
        }
        sb_raw(sb, one);
    }
}

static bool path_join(char *out, size_t size, const char *base, const char *leaf)
{
    size_t a = strlen(base);
    size_t b = strlen(leaf);

    if (a + b + 2 > size) {
        return false;
    }

    memcpy(out, base, a);
    out[a] = '/';
    memcpy(out + a + 1, leaf, b);
    out[a + 1 + b] = '\0';
    return true;
}

static esp_err_t send_text(httpd_req_t *req, const char *body, size_t len, const char *type)
{
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, len);
}

static esp_err_t send_json(httpd_req_t *req, const char *body)
{
    return send_text(req, body, strlen(body), "application/json; charset=utf-8");
}

static esp_err_t send_status_err(httpd_req_t *req, const char *status, const char *msg)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
}

static bool query_get(httpd_req_t *req, const char *key, char *out, size_t size)
{
    char query[512];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    if (httpd_query_key_value(query, key, out, size) != ESP_OK) {
        return false;
    }
    return true;
}

static bool query_long(httpd_req_t *req, const char *key, long *out)
{
    char value[16];
    char *end = NULL;
    long parsed;

    if (!query_get(req, key, value, sizeof(value))) {
        return false;
    }
    parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0') {
        return false;
    }
    *out = parsed;
    return true;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool url_decode(char *out, size_t size, const char *src)
{
    size_t len = 0;

    while (*src != '\0') {
        char c = *src++;

        if (c == '%') {
            int hi = hex_value(src[0]);
            int lo = (hi < 0) ? -1 : hex_value(src[1]);

            if (hi < 0 || lo < 0) {
                return false;
            }
            c = (char)((hi << 4) | lo);
            src += 2;
        }
        if (len + 1 >= size) {
            return false;
        }
        out[len++] = c;
    }
    out[len] = '\0';
    return len > 0;
}

static bool file_name_ok(const char *name)
{
    size_t len = strlen(name);

    if (len == 0 || len > APP_HTTP_NAME_MAX) {
        return false;
    }
    if (name[0] == '.' || strstr(name, "..") != NULL) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];

        if (c < 0x20 || c == 0x7F || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    return true;
}

static void music_dir(char *out, size_t size)
{
    snprintf(out, size, "%s/music", app_store_mount());
}

static esp_err_t handler_status(httpd_req_t *req)
{
    app_settings_t *cfg = app_settings_get();
    char ip[24] = "";
    long phone_time = 0;
    sb_t sb;

    app_wifi_get_ip(ip, sizeof(ip));

    if (query_long(req, "t", &phone_time) && phone_time > 0) {
        app_time_set_unix((uint32_t)phone_time);
    }

    sb_init(&sb, s_json, sizeof(s_json));
    sb_raw(&sb, "{\"free_kb\":");
    sb_num(&sb, app_store_free_kb());
    sb_raw(&sb, ",\"used_kb\":");
    sb_num(&sb, app_store_used_kb());
    sb_raw(&sb, ",\"on_sd\":");
    sb_num(&sb, app_store_on_sd() ? 1 : 0);
    sb_raw(&sb, ",\"sta\":");
    sb_num(&sb, app_wifi_sta_has_ip() ? 1 : 0);
    sb_raw(&sb, ",\"ip\":\"");
    sb_raw(&sb, ip);
    sb_raw(&sb, "\",\"ap_ssid\":\"");
    sb_raw(&sb, app_wifi_ap_ssid());
    sb_raw(&sb, "\",\"wifi_ssid\":\"");
    sb_raw(&sb, cfg->sta_ssid);
    sb_raw(&sb, "\",\"weather\":");
    sb_num(&sb, app_weather_valid() ? 1 : 0);
    sb_raw(&sb, ",\"ap_url\":\"");
    sb_raw(&sb, app_wifi_ap_url());
    sb_raw(&sb, "\",\"tracks\":");
    sb_num(&sb, app_player_count());
    sb_raw(&sb, ",\"playing\":");
    sb_num(&sb, app_player_index());
    sb_raw(&sb, ",\"state\":");
    sb_num(&sb, (unsigned long)app_player_state());
    sb_raw(&sb, ",\"volume\":");
    sb_num(&sb, cfg->volume);
    sb_raw(&sb, ",\"brightness\":");
    sb_num(&sb, cfg->brightness);
    sb_raw(&sb, ",\"now\":");
    sb_num(&sb, app_time_now());
    sb_raw(&sb, "}");

    return send_json(req, s_json);
}

static bool form_value(const char *body, const char *key, char *out, size_t size)
{
    char pattern[32];
    const char *hit;
    const char *start;
    const char *end;
    char raw[192];
    size_t len;

    snprintf(pattern, sizeof(pattern), "%s=", key);
    hit = strstr(body, pattern);
    if (hit == NULL) {
        return false;
    }
    if (hit != body && *(hit - 1) != '&') {
        return false;
    }
    start = hit + strlen(pattern);
    end = strchr(start, '&');
    len = (end != NULL) ? (size_t)(end - start) : strlen(start);
    if (len == 0 || len >= sizeof(raw)) {
        return false;
    }
    memcpy(raw, start, len);
    raw[len] = '\0';
    return url_decode(out, size, raw);
}

static esp_err_t handler_wifi(httpd_req_t *req)
{
    char body[256];
    char ssid[33];
    char pass[65];
    int total = (int)req->content_len;
    int got = 0;

    if (total <= 0 || total >= (int)sizeof(body)) {
        return send_status_err(req, "400 Bad Request", "bad size");
    }
    while (got < total) {
        int r = httpd_req_recv(req, body + got, (size_t)(total - got));

        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            return send_status_err(req, "400 Bad Request", "recv fail");
        }
        got += r;
    }
    body[got] = '\0';

    if (!form_value(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        return send_status_err(req, "400 Bad Request", "missing ssid");
    }
    pass[0] = '\0';
    form_value(body, "pass", pass, sizeof(pass));

    if (app_wifi_sta_set(ssid, pass) != ESP_OK) {
        return send_status_err(req, "500 Internal Server Error", "apply fail");
    }
    ESP_LOGI(TAG, "wifi config saved: %s", ssid);
    return send_json(req, "{\"ok\":1}");
}

static esp_err_t handler_tracks(httpd_req_t *req)
{
    uint32_t count = app_player_count();
    uint32_t shown = (count > APP_HTTP_LIST_MAX) ? APP_HTTP_LIST_MAX : count;
    uint32_t i;
    sb_t sb;

    sb_init(&sb, s_json, sizeof(s_json));
    sb_raw(&sb, "{\"count\":");
    sb_num(&sb, count);
    sb_raw(&sb, ",\"dir\":\"");
    sb_raw(&sb, app_player_dir());
    sb_raw(&sb, "\",\"tracks\":[");

    for (i = 0; i < shown; i++) {
        const char *name = app_player_name(i);
        const char *base;

        if (i > 0) {
            sb_raw(&sb, ",");
        }
        sb_raw(&sb, "{\"i\":");
        sb_num(&sb, i);
        sb_raw(&sb, ",\"name\":\"");
        for (base = name; *base != '\0'; base++) {
            char one[2];

            one[0] = *base;
            one[1] = '\0';
            if (*base == '"' || *base == '\\') {
                sb_raw(&sb, "\\");
            }
            sb_raw(&sb, one);
        }
        sb_raw(&sb, "\"}");
    }

    sb_raw(&sb, "]}");
    return send_json(req, s_json);
}

static esp_err_t handler_photos(httpd_req_t *req){
    int count = app_album_scan();
    int shown = (count > APP_HTTP_LIST_MAX) ? APP_HTTP_LIST_MAX : count;
    int i;
    sb_t sb;

    sb_init(&sb, s_json, sizeof(s_json));
    sb_raw(&sb, "{\"count\":");
    sb_num(&sb, (unsigned long)count);
    sb_raw(&sb, ",\"dir\":\"");
    sb_raw(&sb, app_album_dir());
    sb_raw(&sb, "\",\"photos\":[");
    for (i = 0; i < shown; i++) {
        const char *name = app_album_name(i);

        if (name == NULL) {
            continue;
        }
        if (i > 0) {
            sb_raw(&sb, ",");
        }
        sb_raw(&sb, "{\"i\":");
        sb_num(&sb, (unsigned long)i);
        sb_raw(&sb, ",\"name\":\"");
        sb_json_str(&sb, name);
        sb_raw(&sb, "\"}");
    }
    sb_raw(&sb, "]}");
    return send_json(req, s_json);
}

static esp_err_t handler_photo_delete(httpd_req_t *req)
{
    char value[16];
    long index = -1;
    esp_err_t err;

    if (!query_get(req, "index", value, sizeof(value))) {
        return send_status_err(req, "400 Bad Request", "missing index");
    }
    index = strtol(value, NULL, 10);
    if (index < 0 || index >= app_album_count()) {
        return send_status_err(req, "400 Bad Request", "bad index");
    }
    err = app_album_delete((int)index);
    if (err != ESP_OK) {
        return send_status_err(req, "500 Internal Server Error", "delete fail");
    }
    return send_json(req, "{\"ok\":1}");
}

static esp_err_t handler_rescan(httpd_req_t *req)
{
    uint32_t count = app_player_scan();

    snprintf(s_json, sizeof(s_json), "{\"ok\":1,\"count\":%lu}", (unsigned long)count);
    return send_json(req, s_json);
}

static esp_err_t handler_control(httpd_req_t *req)
{
    char action[16];
    long index = 0;
    bool has_index;

    if (!query_get(req, "action", action, sizeof(action))) {
        return send_status_err(req, "400 Bad Request", "missing action");
    }
    has_index = query_long(req, "index", &index);

    if (strcmp(action, "toggle") == 0) {
        app_player_toggle();
    } else if (strcmp(action, "play") == 0) {
        if (has_index && index >= 0) {
            app_player_play((uint32_t)index);
        } else {
            app_player_toggle();
        }
    } else if (strcmp(action, "pause") == 0) {
        if (app_player_state() == APP_PLAYER_PLAYING) {
            app_player_toggle();
        }
    } else if (strcmp(action, "next") == 0) {
        app_player_next();
    } else if (strcmp(action, "prev") == 0) {
        app_player_prev();
    } else if (strcmp(action, "stop") == 0) {
        app_player_stop();
    } else {
        return send_status_err(req, "400 Bad Request", "bad action");
    }

    return send_json(req, "{\"ok\":1}");
}

static esp_err_t handler_delete(httpd_req_t *req)
{
    long index = -1;
    const char *path;

    if (!query_long(req, "index", &index) || index < 0 || (uint32_t)index >= app_player_count()) {
        return send_status_err(req, "400 Bad Request", "bad index");
    }

    if ((uint32_t)index == app_player_index() && app_player_state() != APP_PLAYER_STOPPED) {
        app_player_stop();
    }

    path = app_player_path((uint32_t)index);
    if (remove(path) != 0) {
        return send_status_err(req, "500 Internal Server Error", "delete failed");
    }

    app_player_scan();
    return send_json(req, "{\"ok\":1}");
}

static esp_err_t handler_upload(httpd_req_t *req)
{
    char raw[APP_HTTP_NAME_MAX * 3 + 8];
    char name[APP_HTTP_NAME_MAX + 8];
    char kind[16];
    char dir[96];
    char path[200];
    bool photo;
    int total;
    int got = 0;
    FILE *file;
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "upload req %s len=%d", req->uri, (int)req->content_len);

    if (!query_get(req, "name", raw, sizeof(raw))) {
        return send_status_err(req, "400 Bad Request", "missing name");
    }
    if (!url_decode(name, sizeof(name), raw) || !file_name_ok(name)) {
        return send_status_err(req, "400 Bad Request", "bad name");
    }

    total = (int)req->content_len;
    if (total <= 0 || total > APP_HTTP_FILE_MAX) {
        return send_status_err(req, "400 Bad Request", "bad size");
    }

    photo = query_get(req, "dir", kind, sizeof(kind)) && strcmp(kind, "photo") == 0;
    if (photo) {
        snprintf(dir, sizeof(dir), "%s/photo", app_store_mount());
        if (app_store_free_kb() < APP_HTTP_FREE_MIN_KB) {
            return send_status_err(req, "507 Insufficient Storage", "no space");
        }
    } else {
        music_dir(dir, sizeof(dir));
    }

    if (mkdir(dir, 0777) != 0 && errno != EEXIST) {
        return send_status_err(req, "507 Insufficient Storage", "mkdir fail");
    }

    if (photo) {
        snprintf(path, sizeof(path), "%s/.tmp", dir);
    } else if (!path_join(path, sizeof(path), dir, name)) {
        return send_status_err(req, "500 Internal Server Error", "path");
    }

    app_store_lock();
    file = fopen(path, "wb");
    if (file == NULL) {
        app_store_unlock();
        return send_status_err(req, "507 Insufficient Storage", "open fail");
    }

    ESP_LOGI(TAG, "upload start %s: %d bytes -> %s", name, total, dir);
    {
        int64_t t0 = esp_timer_get_time();
        int stalls = 0;
        int last_log = 0;

        while (got < total) {
            int chunk = total - got;
            int r;

            if (chunk > APP_HTTP_IO_SIZE) {
                chunk = APP_HTTP_IO_SIZE;
            }
            r = httpd_req_recv(req, s_iobuf, (size_t)chunk);
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                if (++stalls > 3) {
                    ESP_LOGW(TAG, "upload stalled at %d/%d", got, total);
                    err = ESP_FAIL;
                    break;
                }
                continue;
            }
            stalls = 0;
            if (r <= 0) {
                ESP_LOGW(TAG, "upload %s aborted by client at %d/%d (r=%d)", name, got, total, r);
                err = ESP_FAIL;
                break;
            }
            if (fwrite(s_iobuf, 1, (size_t)r, file) != (size_t)r) {
                ESP_LOGW(TAG, "upload %s write fail at %d/%d", name, got, total);
                err = ESP_FAIL;
                break;
            }
            got += r;
            if (got - last_log >= (512 * 1024)) {
                last_log = got;
                ESP_LOGI(TAG, "upload %s: %d/%d bytes", name, got, total);
            }
        }

        ESP_LOGI(TAG, "upload %s: %d/%d bytes in %u ms", name, got, total,
                 (unsigned)((esp_timer_get_time() - t0) / 1000));
    }

    fclose(file);
    if (err != ESP_OK) {
        remove(path);
    }
    app_store_unlock();
    ESP_LOGI(TAG, "upload recv done %s got=%d/%d tmp=%s", name, got, total, path);

    if (err != ESP_OK) {
        return send_status_err(req, "500 Internal Server Error", "write fail");
    }

    if (photo) {
        int64_t t1 = esp_timer_get_time();
        esp_err_t aerr = app_album_add_file(path, name);
        char msg[48];

        ESP_LOGI(TAG, "album add %s in %u ms (%d) free=%uKB", name,
                 (unsigned)((esp_timer_get_time() - t1) / 1000), (int)aerr,
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
        if (aerr != ESP_OK) {
            snprintf(msg, sizeof(msg), "err %d", (int)aerr);
            ESP_LOGW(TAG, "upload fail %s -> %s", name, msg);
            if (aerr == ESP_ERR_NO_MEM) {
                return send_status_err(req, "507 Insufficient Storage", msg);
            }
            if (aerr == ESP_ERR_NOT_SUPPORTED || aerr == ESP_FAIL ||
                aerr == ESP_ERR_INVALID_RESPONSE || aerr == ESP_ERR_INVALID_SIZE) {
                return send_status_err(req, "415 Unsupported Media Type", msg);
            }
            return send_status_err(req, "500 Internal Server Error", msg);
        }
        snprintf(s_json, sizeof(s_json), "{\"ok\":1}");
        ESP_LOGI(TAG, "upload reply ok %s", name);
        return send_json(req, s_json);
    }
    app_player_scan();
    snprintf(s_json, sizeof(s_json), "{\"ok\":1,\"count\":%lu}", (unsigned long)app_player_count());
    return send_json(req, s_json);
}

static const char s_manifest_json[] =
    "{\"name\":\"音乐播放器\",\"short_name\":\"音乐盒\",\"start_url\":\"/\","
    "\"display\":\"standalone\",\"orientation\":\"portrait\","
    "\"background_color\":\"#0d0f13\",\"theme_color\":\"#0d0f13\"}";

static const char s_index_html[] =
    "<!doctype html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<meta name=\"theme-color\" content=\"#0d0f13\">"
    "<meta name=\"mobile-web-app-capable\" content=\"yes\">"
    "<meta name=\"apple-mobile-web-app-capable\" content=\"yes\">"
    "<meta name=\"apple-mobile-web-app-status-bar-style\" content=\"black-translucent\">"
    "<meta name=\"apple-mobile-web-app-title\" content=\"音乐盒\">"
    "<link rel=\"manifest\" href=\"/manifest.json\">"
    "<title>音乐播放器</title><style>"
    "body{margin:0;font:16px/1.6 -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
    "background:#0d0f13;color:#e6e8ee}"
    "h1{font-size:20px;margin:0;padding:18px 16px 6px}"
    "h1 i{color:#8b93a7;font-size:14px;font-style:normal}"
    ".bar{display:flex;flex-wrap:wrap;gap:8px;align-items:center;padding:8px 16px}"
    "ul{list-style:none;margin:0;padding:8px 16px}"
    "li{display:flex;justify-content:space-between;align-items:center;background:#171a21;"
    "border:1px solid #2e3542;border-radius:12px;padding:10px 14px;margin-bottom:8px}"
    "li span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
    "li em{color:#8b93a7;font-size:13px;font-style:normal;margin-left:10px}"
    "button{padding:10px 16px;border-radius:10px;border:1px solid #2e3542;"
    "background:#222833;color:#e6e8ee;font-size:15px}"
    "button:active{background:#2f7cf6}"
    "label.btn{display:inline-block;padding:10px 16px;border-radius:10px;border:1px solid #2e3542;"
    "background:#222833;color:#e6e8ee;font-size:15px}"
    "label.btn:active{background:#2f7cf6}"
    "input.pick{position:absolute;left:-9999px;top:0;width:1px;height:1px;opacity:0}"
    "h2{font-size:16px;margin:16px 16px 4px;color:#8b93a7;font-weight:400}"
    "#msg{color:#2f7cf6;padding:0 16px;min-height:22px;font-size:14px}"
    "#pst{color:#8b93a7;font-size:13px}"
    "#pd{color:#8b93a7;padding:0 16px;font-size:13px}"
    "input[type=file]{font-size:14px;color:#8b93a7}"
    "</style></head><body><h1>音乐播放器 <i id=\"st\"></i></h1>"
    "<div class=\"bar\"><input type=\"file\" id=\"f\" multiple "
    "accept=\".mp3,.flac,.wav,.mp4,.m4a,.aac,.ts,audio/*\"><button id=\"u\">上传到设备</button></div>"
    "<div class=\"bar\"><input class=\"pick\" type=\"file\" id=\"pf\" multiple "
    "accept=\".jpg,.jpeg,.png,image/*\"><label class=\"btn\" id=\"pu\" for=\"pf\">选择照片并上传</label>"
    "<span id=\"pst\"></span></div>"
    "<div class=\"bar\" style=\"padding-top:0;color:#8b93a7;font-size:13px\">"
    "若「选择照片」点不动，说明这是在热点登录小窗里，请用手机浏览器打开 "
    "<b style=\"color:#2f7cf6\">http://192.168.4.1/</b></div>"
    "<div class=\"bar\" style=\"padding-top:0\"><button id=\"cp\">复制地址到浏览器</button>"
    "<span id=\"cps\" style=\"color:#8b93a7;font-size:13px\"></span></div>"
    "<h2>WiFi（连路由器才能显示天气）</h2>"
    "<div class=\"bar\"><input id=\"ws\" placeholder=\"WiFi 名称\" style=\"flex:1;min-width:140px;"
    "padding:10px;border-radius:10px;border:1px solid #2e3542;background:#171a21;color:#e6e8ee\">"
    "<input id=\"wp\" type=\"password\" placeholder=\"WiFi 密码\" style=\"flex:1;min-width:140px;"
    "padding:10px;border-radius:10px;border:1px solid #2e3542;background:#171a21;color:#e6e8ee\">"
    "<button id=\"wsv\">保存并连接</button><span id=\"wst\" style=\"color:#8b93a7;font-size:13px\"></span></div>"
    "<div class=\"bar\"><button data-a=\"prev\">上一首</button>"
    "<button data-a=\"toggle\">播放 / 暂停</button>"
    "<button data-a=\"next\">下一首</button>"
    "<button data-a=\"stop\">停止</button>"
    "<button id=\"r\">刷新</button></div>"
#if APP_CAST_ENABLE
    "<div class=\"bar\"><input class=\"pick\" type=\"file\" id=\"cf\" accept=\"image/*\" "
    "capture=\"environment\"><label class=\"btn\" id=\"cam\" for=\"cf\">手机拍照投屏</label>"
    "<button id=\"camstop\">退出投屏</button><span id=\"cst\"></span></div>"
#endif
    "<div id=\"msg\"></div><h2 id=\"ht\">歌曲</h2><ul id=\"l\"></ul>"
    "<h2 id=\"hp\">相册</h2><div id=\"pd\"></div><ul id=\"pl\"></ul><script>"
    "var m=document.getElementById('msg');"
    "function say(t){m.textContent=t}"
    "function tnow(){return Math.floor(Date.now()/1000)}"
    "function load(){fetch('api/status?t='+tnow()).then(function(r){return r.json()}).then(function(d){"
    "document.getElementById('st').textContent='共 '+d.tracks+' 首 · 剩余 '"
    "+Math.round(d.free_kb/1024)+' MB · '+(d.on_sd?'SD卡':'内部存储')+' · '"
    "+(d.sta?'WiFi已连接':'无外网');"
    "document.getElementById('ws').value=d.wifi_ssid||'';});"
    "fetch('api/tracks').then(function(r){return r.json()}).then(function(d){"
    "var l=document.getElementById('l');l.innerHTML='';"
    "if(!d.tracks.length){l.innerHTML='<li><span>还没有歌曲，先上传几首吧</span></li>'}"
    "d.tracks.forEach(function(t){var li=document.createElement('li');"
    "li.innerHTML='<span>'+t.name+'</span>';"
    "var b=document.createElement('button');b.textContent='删除';"
    "b.onclick=function(){fetch('api/delete?index='+t.i,{method:'POST'}).then(function(){load()})};"
    "li.appendChild(b);l.appendChild(li)});"
    "if(d.count>d.tracks.length){var li=document.createElement('li');"
    "li.innerHTML='<span>还有 '+(d.count-d.tracks.length)+' 首未显示</span>';l.appendChild(li)}})}"
    "function loadPhotos(){fetch('api/photos').then(function(r){return r.json()}).then(function(d){"
    "document.getElementById('pd').textContent='共 '+d.count+' 张  ·  '+d.dir;"
    "var l=document.getElementById('pl');l.innerHTML='';"
    "if(!d.photos.length){l.innerHTML='<li><span>相册里还没有照片</span></li>';return}"
    "d.photos.forEach(function(p){var li=document.createElement('li');"
    "li.innerHTML='<span>'+p.name+'</span>';"
    "var b=document.createElement('button');b.textContent='删除';"
    "b.onclick=function(){fetch('api/photo/delete?index='+p.i,{method:'POST'}).then(function(){loadPhotos()})};"
    "li.appendChild(b);l.appendChild(li)});"
    "if(d.count>d.photos.length){var li=document.createElement('li');"
    "li.innerHTML='<span>还有 '+(d.count-d.photos.length)+' 张未显示</span>';l.appendChild(li)}})}"
    "document.getElementById('u').onclick=function(){"
    "var files=document.getElementById('f').files;if(!files.length){say('先选择文件');return}"
    "var i=0;function next(){if(i>=files.length){say('上传完成');"
    "fetch('api/rescan',{method:'POST'}).then(load);return}"
    "var f=files[i++];say('上传中 '+i+'/'+files.length+' '+f.name);"
    "fetch('api/upload?name='+encodeURIComponent(f.name),{method:'POST',body:f})"
    ".then(function(r){if(!r.ok){say('上传失败 '+f.name);return}"
    "say('已上传 '+f.name);next()}).catch(function(){say('上传失败 '+f.name)})}next()};"
    "document.getElementById('r').onclick=function(){"
    "fetch('api/rescan',{method:'POST'}).then(load)};"
    "function shrink(f,cb){"
    "if(f.type.indexOf('image/')!==0&&!/\\.(jpg|jpeg|png)$/i.test(f.name)){cb(null,'');return}"
    "var img=new Image();"
    "img.onload=function(){var iw=img.naturalWidth||img.width,ih=img.naturalHeight||img.height;"
    "var scale=Math.min(800/iw,480/ih);"
    "var dw=Math.max(1,Math.round(iw*scale)),dh=Math.max(1,Math.round(ih*scale));"
    "var cv=document.createElement('canvas');cv.width=800;cv.height=480;"
    "var ctx=cv.getContext('2d');"
    "ctx.fillStyle='#101010';ctx.fillRect(0,0,800,480);"
    "ctx.imageSmoothingEnabled=true;ctx.imageSmoothingQuality='high';"
    "ctx.drawImage(img,Math.round((800-dw)/2),Math.round((480-dh)/2),dw,dh);"
    "URL.revokeObjectURL(img.src);"
    "cv.toBlob(function(b){cb(b,iw+'x'+ih+'->'+dw+'x'+dh)},'image/jpeg',0.85)};"
    "img.onerror=function(){URL.revokeObjectURL(img.src);cb(null,'')};"
    "img.src=URL.createObjectURL(f)}"
    "document.getElementById('pf').onchange=function(){"
    "var files=document.getElementById('pf').files;"
    "if(!files.length){document.getElementById('pst').textContent='没选到文件';return}"
    "var i=0,bad=[];"
    "function np(){if(i>=files.length){var t='完成 '+(files.length-bad.length)+'/'+files.length+' 张';"
    "if(bad.length){t+='，失败: '+bad.join(', ')}"
    "document.getElementById('pst').textContent=t;"
    "document.getElementById('pf').value='';loadPhotos();return}"
    "var f=files[i++],st=document.getElementById('pst');"
    "if(/\\.(heic|heif)$/i.test(f.name)){st.textContent='跳过 '+f.name+'：HEIC 不支持，相机请设为“兼容性最佳”';bad.push(f.name+' (HEIC)');np();return}"
    "st.textContent='压缩中 '+i+'/'+files.length+' '+f.name;"
    "shrink(f,function(b,size){"
    "if(!b){bad.push(f.name+' (读取失败)');np();return}"
    "var name=f.name.replace(/\\.(png|jpe?g|heic|heif)$/i,'')+'.jpg';"
    "function sendPhoto(tryLeft){"
    "var x=new XMLHttpRequest();"
    "x.open('POST','api/upload?dir=photo&name='+encodeURIComponent(name));"
    "x.timeout=90000;"
    "x.upload.onprogress=function(e){if(!e.lengthComputable){return}"
    "if(e.loaded>=e.total){st.textContent='已发送，设备处理中…';return}"
    "st.textContent='上传中 '+i+'/'+files.length+'  '+Math.round(e.loaded*100/e.total)+'%  '+(size?size+' / ':'')+Math.round(e.total/1024)+'KB'};"
    "x.onload=function(){if(x.status>=200&&x.status<300){st.textContent='完成 '+name;loadPhotos()}else{bad.push(name+' (HTTP '+x.status+' '+(x.responseText||'')+')')}np()};"
    "x.onerror=x.ontimeout=function(){if(tryLeft>0){st.textContent='连接中断，重试 '+name;setTimeout(function(){sendPhoto(tryLeft-1)},1000)}else{bad.push(name+' (连接中断)');np()}};"
    "x.send(b)}"
    "sendPhoto(2)})}"
    "np()};"
    "document.getElementById('cp').onclick=function(){"
    "var u=location.origin+'/',s=document.getElementById('cps');"
    "function done(){s.textContent='地址已复制，粘贴到浏览器打开即可上传'}"
    "if(navigator.clipboard&&navigator.clipboard.writeText){"
    "navigator.clipboard.writeText(u).then(done).catch(function(){fallback()})}else{fallback()}"
    "function fallback(){var t=document.createElement('textarea');t.value=u;"
    "document.body.appendChild(t);t.select();"
    "try{document.execCommand('copy');done()}catch(e){s.textContent='请手动记下 '+u}"
    "document.body.removeChild(t)}};"
    "document.getElementById('wsv').onclick=function(){"
    "var s=document.getElementById('ws').value,p=document.getElementById('wp').value,"
    "st=document.getElementById('wst');"
    "if(!s){st.textContent='请填 WiFi 名称';return}"
    "st.textContent='保存中…';"
    "fetch('api/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
    "body:'ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p)})"
    ".then(function(r){st.textContent=r.ok?'已保存，正在连接…':'保存失败 '+r.status;load()})"
    ".catch(function(){st.textContent='保存失败'})};"
    "document.querySelectorAll('button[data-a]').forEach(function(b){"
    "b.onclick=function(){fetch('api/control?action='+b.dataset.a,{method:'POST'}).then(load)}});"
#if APP_CAST_ENABLE
    "var cf=document.getElementById('cf');var cst=document.getElementById('cst');"
    "cf.onchange=function(){var file=cf.files[0];if(!file){return}cst.textContent='处理中...';"
    "var img=new Image();"
    "img.onload=function(){var w=640,h=Math.round(w*img.naturalHeight/img.naturalWidth);"
    "if(h>480){h=480;w=Math.round(h*img.naturalWidth/img.naturalHeight)}"
    "var c=document.createElement('canvas');c.width=w;c.height=h;"
    "c.getContext('2d').drawImage(img,0,0,w,h);"
    "c.toBlob(function(b){cst.textContent='发送中...';"
    "fetch('api/cast',{method:'POST',body:b,headers:{'Content-Type':'image/jpeg'}})"
    ".then(function(r){if(r.ok){cst.textContent='已投屏 '+w+'x'+h}else{cst.textContent='失败 '+r.status}})"
    ".catch(function(){cst.textContent='发送失败'})},'image/jpeg',0.75);"
    "URL.revokeObjectURL(img.src)};"
    "img.onerror=function(){cst.textContent='图片读取失败'};"
    "img.src=URL.createObjectURL(file);cf.value=''};"
    "document.getElementById('camstop').onclick=function(){"
    "fetch('api/cast/stop',{method:'POST'}).then(function(){cst.textContent='已退出投屏'})"
    ".catch(function(){cst.textContent='退出失败'})};"
#endif
    "load();loadPhotos();setInterval(load,60000);</script></body></html>";

#if APP_CAST_ENABLE
static esp_err_t handler_cast(httpd_req_t *req)
{
    size_t capacity = 0;
    size_t total;
    size_t got = 0;
    uint8_t *buf;
    int want;

    buf = app_cast_alloc_frame(&capacity);
    if (buf == NULL) {
        return send_status_err(req, "503 Service Unavailable", "cast not ready");
    }

    want = (int)req->content_len;
    if (want <= 0) {
        return send_status_err(req, "400 Bad Request", "empty body");
    }
    total = (size_t)want;
    if (total > capacity) {
        return send_status_err(req, "413 Payload Too Large", "frame too big");
    }

    while (got < total) {
        size_t chunk = total - got;
        int r;

        if (chunk > APP_HTTP_IO_SIZE) {
            chunk = APP_HTTP_IO_SIZE;
        }
        r = httpd_req_recv(req, (char *)buf + got, chunk);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            return send_status_err(req, "400 Bad Request", "recv fail");
        }
        got += (size_t)r;
    }

    if (app_cast_submit_frame(got) != ESP_OK) {
        return send_status_err(req, "415 Unsupported Media Type", "decode fail");
    }
    return send_json(req, "{\"ok\":1}");
}

static esp_err_t handler_cast_stop(httpd_req_t *req)
{
    app_cast_stop();
    return send_json(req, "{\"ok\":1}");
}
#endif

static esp_err_t handler_index(httpd_req_t *req)
{
    return send_text(req, s_index_html, sizeof(s_index_html) - 1, "text/html; charset=utf-8");
}

static esp_err_t handler_manifest(httpd_req_t *req)
{
    return send_text(req, s_manifest_json, sizeof(s_manifest_json) - 1,
                     "application/manifest+json; charset=utf-8");
}

static esp_err_t handler_catchall(httpd_req_t *req)
{
    ESP_LOGI(TAG, "catchall %s method=%d", req->uri, (int)req->method);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t app_httpd_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    static const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = handler_index},
        {.uri = "/manifest.json", .method = HTTP_GET, .handler = handler_manifest},
        {.uri = "/api/status", .method = HTTP_GET, .handler = handler_status},
        {.uri = "/api/tracks", .method = HTTP_GET, .handler = handler_tracks},
        {.uri = "/api/rescan", .method = HTTP_POST, .handler = handler_rescan},
        {.uri = "/api/control", .method = HTTP_POST, .handler = handler_control},
        {.uri = "/api/delete", .method = HTTP_POST, .handler = handler_delete},
        {.uri = "/api/upload", .method = HTTP_POST, .handler = handler_upload},
        {.uri = "/api/photos", .method = HTTP_GET, .handler = handler_photos},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = handler_wifi},
        {.uri = "/api/photo/delete", .method = HTTP_POST, .handler = handler_photo_delete},
#if APP_CAST_ENABLE
        {.uri = "/api/cast", .method = HTTP_POST, .handler = handler_cast},
        {.uri = "/api/cast/stop", .method = HTTP_POST, .handler = handler_cast_stop},
#endif
    };

    if (s_server != NULL) {
        return ESP_OK;
    }

    s_iobuf = heap_caps_malloc(APP_HTTP_IO_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_iobuf == NULL) {
        s_iobuf = heap_caps_malloc(APP_HTTP_IO_SIZE, MALLOC_CAP_8BIT);
    }
    if (s_iobuf == NULL) {
        ESP_LOGE(TAG, "buffer alloc failed");
        return ESP_ERR_NO_MEM;
    }

    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        s_server = NULL;
        return ESP_FAIL;
    }

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }

    httpd_uri_t catchall = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = handler_catchall,
    };
    httpd_register_uri_handler(s_server, &catchall);

    ESP_LOGI(TAG, "server started");
    return ESP_OK;
}

bool app_httpd_running(void)
{
    return s_server != NULL;
}
