#include "app_power.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bsp_i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "i2c_bus.h"
#include "nvs.h"

#define INA226_ADDR        (0x40)
#define INA226_REG_CONF    (0x00)
#define INA226_REG_BUS     (0x02)
#define INA226_REG_CURR    (0x04)
#define INA226_REG_CAL     (0x05)
#define INA226_REG_MANUF   (0xFE)
#define INA226_REG_DIE     (0xFF)

#define INA226_SHUNT_MOHM  (10)
#define INA226_MAX_MA      (8000)
#define INA226_CAL         ((uint16_t)(5120000ULL / (INA226_SHUNT_MOHM * INA226_MAX_MA / 1000)))

#define BATT_CAPACITY_MAH  (2000.0f)
#define BATT_CURRENT_SIGN  (1)
#define SOC_FULL_MV        (4150)
#define SOC_EMPTY_MV       (3300)
#define SOC_FULL_MA        (60)
#define SOC_IDLE_MA        (40)
#define SOC_OCV_SYNC_SEC   (90)
#define SOC_SAVE_STEP      (1)

static const char *TAG = "power";

static i2c_bus_device_handle_t s_dev;
static bool s_ready;
static bool s_valid;
static int s_mv;
static int s_ma;
static int s_percent;
static int64_t s_last_us;
static int64_t s_int_us;
static int64_t s_idle_since_us;
static float s_soc;
static float s_used_mah;
static int s_saved_soc = -1;

static esp_err_t ina_write(uint8_t reg, uint16_t value)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(value >> 8);
    buf[1] = (uint8_t)(value & 0xFF);
    return i2c_bus_write_bytes(s_dev, reg, 2, buf);
}

static esp_err_t ina_read(uint8_t reg, uint16_t *value)
{
    uint8_t buf[2];
    esp_err_t err = i2c_bus_read_bytes(s_dev, reg, 2, buf);

    if (err != ESP_OK) {
        return err;
    }
    *value = (uint16_t)((buf[0] << 8) | buf[1]);
    return ESP_OK;
}

static int soc_from_ocv(int mv)
{
    static const struct {
        int mv;
        int pct;
    } curve[] = {
        {4200, 100}, {4100, 92}, {4000, 84}, {3930, 76}, {3870, 68},
        {3820, 60}, {3790, 52}, {3760, 44}, {3730, 35}, {3700, 26},
        {3660, 17}, {3600, 9}, {3450, 3}, {3300, 0},
    };
    size_t i;

    if (mv >= curve[0].mv) {
        return 100;
    }
    for (i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
        if (mv >= curve[i].mv) {
            int hi_mv = curve[i - 1].mv;
            int hi_pct = curve[i - 1].pct;
            int lo_mv = curve[i].mv;
            int lo_pct = curve[i].pct;

            return lo_pct + (mv - lo_mv) * (hi_pct - lo_pct) / (hi_mv - lo_mv);
        }
    }
    return 0;
}

static void soc_load(void)
{
    nvs_handle_t h;
    int32_t stored = -1;

    if (nvs_open("power", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    if (nvs_get_i32(h, "soc", &stored) == ESP_OK && stored >= 0 && stored <= 100) {
        s_soc = (float)stored;
        s_saved_soc = (int)stored;
        ESP_LOGI(TAG, "restored soc=%d%%", (int)stored);
    }
    nvs_close(h);
}

static void soc_save(void)
{
    nvs_handle_t h;
    int now = (int)(s_soc + 0.5f);

    if (!s_valid) {
        return;
    }
    if (now < 0) {
        now = 0;
    }
    if (now > 100) {
        now = 100;
    }
    if (s_saved_soc >= 0 && (now > s_saved_soc ? now - s_saved_soc : s_saved_soc - now) < SOC_SAVE_STEP) {
        return;
    }
    if (nvs_open("power", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_i32(h, "soc", now) == ESP_OK) {
        nvs_commit(h);
        s_saved_soc = now;
    }
    nvs_close(h);
}

static void soc_integrate(int ma, int mv, float dt_s)
{
    float delta_mah = (float)ma * BATT_CURRENT_SIGN * dt_s / 3600.0f;

    s_used_mah += delta_mah;
    s_soc -= delta_mah * 100.0f / BATT_CAPACITY_MAH;

    if (mv >= SOC_FULL_MV && (ma < SOC_FULL_MA && ma > -SOC_FULL_MA)) {
        s_used_mah = 0.0f;
        s_soc = 100.0f;
    } else if (mv <= SOC_EMPTY_MV && ma > 0) {
        s_used_mah = BATT_CAPACITY_MAH;
        s_soc = 0.0f;
    } else if (ma < SOC_IDLE_MA && ma > -SOC_IDLE_MA) {
        int ocv = soc_from_ocv(mv);

        if (s_idle_since_us == 0) {
            s_idle_since_us = esp_timer_get_time();
        } else if ((esp_timer_get_time() - s_idle_since_us) > (int64_t)SOC_OCV_SYNC_SEC * 1000000) {
            s_soc += (ocv - s_soc) * 0.05f;
            s_used_mah = (100.0f - s_soc) * BATT_CAPACITY_MAH / 100.0f;
        }
    } else {
        s_idle_since_us = 0;
    }

    if (s_soc > 100.0f) {
        s_soc = 100.0f;
    }
    if (s_soc < 0.0f) {
        s_soc = 0.0f;
    }
}

void app_power_init(void)
{
    uint16_t manuf = 0;
    uint16_t die = 0;
    esp_err_t err;

    if (s_ready) {
        return;
    }
    {
        uint8_t found[16];
        uint8_t n = bsp_i2c_scan(found, sizeof(found));
        uint8_t i;
        char list[80] = {0};

        for (i = 0; i < n; i++) {
            char one[8];

            snprintf(one, sizeof(one), " %02x", found[i]);
            strncat(list, one, sizeof(list) - strlen(list) - 1);
        }
        ESP_LOGI(TAG, "i2c devices:%s", (n > 0) ? list : " none");
    }

    {
        uint8_t addr;

        for (addr = INA226_ADDR; addr <= INA226_ADDR + 0x0F; addr++) {
            uint16_t id = 0;

            if (bsp_i2c_add_device(&s_dev, addr) != ESP_OK) {
                s_dev = NULL;
                continue;
            }
            if (ina_read(INA226_REG_MANUF, &id) == ESP_OK && id == 0x5449) {
                ESP_LOGI(TAG, "ina226 found at 0x%02x", addr);
                break;
            }
            i2c_bus_device_delete(&s_dev);
            s_dev = NULL;
        }
        if (s_dev == NULL) {
            ESP_LOGW(TAG, "ina226 not found on 0x40..0x4f");
            return;
        }
    }

    err = ina_read(INA226_REG_MANUF, &manuf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ina226 read failed: %s", esp_err_to_name(err));
        i2c_bus_device_delete(&s_dev);
        s_dev = NULL;
        return;
    }
    ina_read(INA226_REG_DIE, &die);
    ESP_LOGI(TAG, "ina226 manuf=0x%04x die=0x%04x shunt=%dmohm cap=%.0fmAh", manuf, die,
             INA226_SHUNT_MOHM, (double)BATT_CAPACITY_MAH);

    ina_write(INA226_REG_CONF, 0x4527);
    ina_write(INA226_REG_CAL, INA226_CAL);
    s_ready = true;

    soc_load();
    app_power_update();
    if (!s_valid) {
        return;
    }
    if (s_soc <= 0.0f || s_soc > 100.0f) {
        s_soc = (float)soc_from_ocv(s_mv);
        s_used_mah = (100.0f - s_soc) * BATT_CAPACITY_MAH / 100.0f;
    }
    ESP_LOGI(TAG, "initial soc=%.1f%% from %dmV", (double)s_soc, s_mv);
}

void app_power_update(void)
{
    uint16_t bus = 0;
    uint16_t cur = 0;
    int64_t now = esp_timer_get_time();
    float dt_s;

    if (!s_ready) {
        return;
    }
    if (s_int_us == 0) {
        s_int_us = now;
    }
    if (s_valid && (now - s_last_us) < 200000) {
        return;
    }
    s_last_us = now;

    if (ina_read(INA226_REG_BUS, &bus) != ESP_OK) {
        s_valid = false;
        return;
    }
    if (ina_read(INA226_REG_CURR, &cur) != ESP_OK) {
        s_valid = false;
        return;
    }

    s_mv = (int)((uint32_t)bus * 125u / 100u);
    s_ma = (int)(int16_t)cur;

    dt_s = (float)(now - s_int_us) / 1000000.0f;
    s_int_us = now;
    if (dt_s > 0.0f && dt_s < 60.0f) {
        soc_integrate(s_ma, s_mv, dt_s);
    }

    s_valid = true;
    s_percent = (int)(s_soc + 0.5f);
    soc_save();
}

bool app_power_valid(void)
{
    return s_valid;
}

int app_power_percent(void)
{
    return s_percent;
}

int app_power_mv(void)
{
    return s_mv;
}

int app_power_ma(void)
{
    return s_ma;
}
