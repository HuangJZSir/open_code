#include "app_bus.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static QueueHandle_t s_queue;

void app_bus_init(void)
{
    if (s_queue == NULL) {
        s_queue = xQueueCreate(24, sizeof(app_evt_t));
    }
}

void app_bus_post(uint8_t type, const char *name)
{
    app_evt_t evt;

    if (s_queue == NULL) {
        return;
    }
    memset(&evt, 0, sizeof(evt));
    evt.type = type;
    if (name != NULL) {
        snprintf(evt.name, sizeof(evt.name), "%s", name);
    }
    (void)xQueueSend(s_queue, &evt, 0);
}

bool app_bus_pop(app_evt_t *out)
{
    if (s_queue == NULL || out == NULL) {
        return false;
    }
    return xQueueReceive(s_queue, out, 0) == pdTRUE;
}
