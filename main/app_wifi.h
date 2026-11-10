#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef void (*app_wifi_client_cb_t)(bool connected);

esp_err_t app_wifi_init(void);
esp_err_t app_wifi_sta_apply(void);
esp_err_t app_wifi_sta_set(const char *ssid, const char *password);
bool app_wifi_sta_connected(void);
bool app_wifi_sta_has_ip(void);
bool app_wifi_get_ip(char *buffer, size_t size);
bool app_wifi_get_url(char *buffer, size_t size);
const char *app_wifi_ap_ssid(void);
const char *app_wifi_ap_pass(void);
const char *app_wifi_ap_url(void);
int app_wifi_client_count(void);
void app_wifi_set_client_cb(app_wifi_client_cb_t cb);
