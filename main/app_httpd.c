#include "app_httpd.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app_player.h"
#include "app_settings.h"
#include "app_store.h"
#include "app_wifi.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"

#define APP_HTTP_IO_SIZE   (16 * 1024)
#define APP_HTTP_FILE_MAX  (24 * 1024 * 1024)
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
    sb_t sb;

    app_wifi_get_ip(ip, sizeof(ip));

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
    sb_raw(&sb, "\",\"ap_url\":\"");
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
    sb_raw(&sb, "}");

    return send_json(req, s_json);
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
    char dir[96];
    char path[200];
    int total;
    int got = 0;
    FILE *file;
    esp_err_t err = ESP_OK;

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

    music_dir(dir, sizeof(dir));
    if (!path_join(path, sizeof(path), dir, name)) {
        return send_status_err(req, "500 Internal Server Error", "path");
    }

    if (mkdir(dir, 0777) != 0 && errno != EEXIST) {
        return send_status_err(req, "507 Insufficient Storage", "mkdir fail");
    }

    app_store_lock();
    file = fopen(path, "wb");
    if (file == NULL) {
        app_store_unlock();
        return send_status_err(req, "507 Insufficient Storage", "open fail");
    }

    while (got < total) {
        int chunk = total - got;
        int r;

        if (chunk > APP_HTTP_IO_SIZE) {
            chunk = APP_HTTP_IO_SIZE;
        }
        r = httpd_req_recv(req, s_iobuf, (size_t)chunk);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            err = ESP_FAIL;
            break;
        }
        if (fwrite(s_iobuf, 1, (size_t)r, file) != (size_t)r) {
            err = ESP_FAIL;
            break;
        }
        got += r;
    }

    fclose(file);
    if (err != ESP_OK) {
        remove(path);
    }
    app_store_unlock();

    if (err != ESP_OK) {
        return send_status_err(req, "500 Internal Server Error", "write fail");
    }

    ESP_LOGI(TAG, "upload %s: %d bytes", name, got);
    app_player_scan();
    snprintf(s_json, sizeof(s_json), "{\"ok\":1,\"count\":%lu}", (unsigned long)app_player_count());
    return send_json(req, s_json);
}

static const char s_index_html[] =
    "<!doctype html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
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
    "#msg{color:#2f7cf6;padding:0 16px;min-height:22px;font-size:14px}"
    "input[type=file]{font-size:14px;color:#8b93a7}"
    "</style></head><body><h1>音乐播放器 <i id=\"st\"></i></h1>"
    "<div class=\"bar\"><input type=\"file\" id=\"f\" multiple "
    "accept=\".mp3,.flac,.wav,.mp4,.m4a,.aac,.ts,audio/*\"><button id=\"u\">上传到设备</button></div>"
    "<div class=\"bar\"><button data-a=\"prev\">上一首</button>"
    "<button data-a=\"toggle\">播放 / 暂停</button>"
    "<button data-a=\"next\">下一首</button>"
    "<button data-a=\"stop\">停止</button>"
    "<button id=\"r\">刷新</button></div>"
    "<div id=\"msg\"></div><ul id=\"l\"></ul><script>"
    "var m=document.getElementById('msg');"
    "function say(t){m.textContent=t}"
    "function load(){fetch('api/status').then(function(r){return r.json()}).then(function(d){"
    "document.getElementById('st').textContent='共 '+d.tracks+' 首 · 剩余 '"
    "+Math.round(d.free_kb/1024)+' MB · '+(d.on_sd?'SD卡':'内部存储')});"
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
    "document.querySelectorAll('button[data-a]').forEach(function(b){"
    "b.onclick=function(){fetch('api/control?action='+b.dataset.a,{method:'POST'}).then(load)}});"
    "load();</script></body></html>";

static esp_err_t handler_index(httpd_req_t *req)
{
    return send_text(req, s_index_html, sizeof(s_index_html) - 1, "text/html; charset=utf-8");
}

static esp_err_t handler_catchall(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t app_httpd_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    static const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = handler_index},
        {.uri = "/api/status", .method = HTTP_GET, .handler = handler_status},
        {.uri = "/api/tracks", .method = HTTP_GET, .handler = handler_tracks},
        {.uri = "/api/rescan", .method = HTTP_POST, .handler = handler_rescan},
        {.uri = "/api/control", .method = HTTP_POST, .handler = handler_control},
        {.uri = "/api/delete", .method = HTTP_POST, .handler = handler_delete},
        {.uri = "/api/upload", .method = HTTP_POST, .handler = handler_upload},
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
    config.max_uri_handlers = 10;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.recv_wait_timeout = 20;
    config.send_wait_timeout = 20;

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
