// touch_gt911.cpp — GT911 5-point I2C capacitive touch driver (pure ESP-IDF)
// Used on JC3248W535 (ESP32-S3, 3.5" 480x320).


#include "touch_gt911.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define GT911_ADDR       0x5D
#define GT911_SDA        19
#define GT911_SCL        20
#define GT911_INT        18
#define GT911_RST        38

#define GT911_REG_STATUS 0x814E
#define GT911_REG_PT1    0x8150

static i2c_master_bus_handle_t _i2c_bus = NULL;
static i2c_master_dev_handle_t _gt911_dev = NULL;

static void _write_reg(uint16_t reg, uint8_t val) {
    uint8_t buf[] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), val };
    i2c_master_transmit(_gt911_dev, buf, sizeof(buf), 100);
}

static bool _read_regs(uint16_t reg, uint8_t* buf, uint8_t len) {
    uint8_t addr[] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    esp_err_t ret = i2c_master_transmit_receive(_gt911_dev, addr, sizeof(addr), buf, len, 100);
    return ret == ESP_OK;
}

void touch_gt911_init() {
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = GT911_SDA;
    bus_cfg.scl_io_num = GT911_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;

    i2c_new_master_bus(&bus_cfg, &_i2c_bus);

    i2c_device_config_t dev_cfg = {};
    dev_cfg.device_address = GT911_ADDR;
    i2c_master_bus_add_device(_i2c_bus, &dev_cfg, &_gt911_dev);

    gpio_set_direction((gpio_num_t)GT911_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)GT911_INT, GPIO_MODE_OUTPUT);

    gpio_set_level((gpio_num_t)GT911_INT, 0);
    gpio_set_level((gpio_num_t)GT911_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)GT911_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    gpio_set_direction((gpio_num_t)GT911_INT, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t id[4] = {};
    if (_read_regs(0x8140, id, 4)) {
        ESP_LOGI("touch", "GT911 ID: %c%c%c  addr=0x%02X", id[0], id[1], id[2], GT911_ADDR);
    } else {
        ESP_LOGI("touch", "GT911 no response at 0x%02X — try 0x14", GT911_ADDR);
    }
}

bool touch_gt911_get_event(gt911_touch_event_t* ev) {
    uint8_t status = 0;
    if (!_read_regs(GT911_REG_STATUS, &status, 1)) {
        ev->pressed = false;
        return false;
    }

    bool ready  = (status & 0x80) != 0;
    uint8_t pts = (status & 0x0F);

    if (!ready || pts == 0) {
        if (ready) _write_reg(GT911_REG_STATUS, 0);
        ev->pressed = false;
        ev->points  = 0;
        return true;
    }

    uint8_t buf[7] = {};
    bool ok = _read_regs(GT911_REG_PT1, buf, 7);

    _write_reg(GT911_REG_STATUS, 0);

    if (!ok) { ev->pressed = false; return false; }

    uint16_t raw_x = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    uint16_t raw_y = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);

    ev->pressed = true;
    ev->points  = pts;
    ev->x = (int16_t)raw_x;
    ev->y = (int16_t)raw_y;
    return true;
}

