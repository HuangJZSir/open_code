#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool on;
    int hour;
    int min;
    char track[64];
} app_alarm_t;

void app_alarm_init(void);
app_alarm_t *app_alarm_get(void);
void app_alarm_save(void);
void app_alarm_set_time(int hour, int min);
void app_alarm_set_on(bool on);
void app_alarm_set_track(const char *name);
