#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_store.h"

typedef enum {
    APP_EVT_NONE = 0,
    APP_EVT_CLIENT_CHANGED,
} app_evt_type_t;

typedef struct {
    uint8_t type;
    char name[APP_STORE_NAME_LEN];
} app_evt_t;

void app_bus_init(void);
void app_bus_post(uint8_t type, const char *name);
bool app_bus_pop(app_evt_t *out);
