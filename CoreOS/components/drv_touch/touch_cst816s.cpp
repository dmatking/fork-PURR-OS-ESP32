// touch_cst816s.cpp — CST816S capacitive I2C touch driver (pure ESP-IDF)

#include "touch_cst816s.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "touch";

#define CST_ADDR   0x15
#define CST_SDA    33
#define CST_SCL    32
#define CST_INT    21
#define CST_RST    25
#define CST_REG    0x01

#define MAP_SCREEN_X(rx, ry)  ((int16_t)(ry))
#define MAP_SCREEN_Y(rx, ry)  ((int16_t)(rx))

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

void touch_cst816s_init(void) {
    // Hardware reset
    gpio_set_direction((gpio_num_t)CST_RST, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)CST_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)CST_RST, 1); vTaskDelay(pdMS_TO_TICKS(50));

    gpio_set_direction((gpio_num_t)CST_INT, GPIO_MODE_INPUT);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port      = I2C_NUM_0,
        .sda_io_num    = (gpio_num_t)CST_SDA,
        .scl_io_num    = (gpio_num_t)CST_SCL,
        .clk_source    = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = { .enable_internal_pullup = true },
    };
    i2c_new_master_bus(&bus_cfg, &s_bus);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CST_ADDR,
        .scl_speed_hz    = 400000,
        .scl_wait_us     = 0,
        .flags = {},
    };
    i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);

    ESP_LOGI(TAG, "CST816S init  SDA=%d SCL=%d INT=%d RST=%d", CST_SDA, CST_SCL, CST_INT, CST_RST);
}

bool touch_cst816s_get_event(cst_touch_event_t* ev) {
    uint8_t reg = CST_REG;
    uint8_t buf[6] = {};

    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, buf, 6, 10);
    if (err != ESP_OK) {
        ev->pressed = false;
        return false;
    }

    uint8_t  fingers = buf[1];
    uint16_t raw_x   = ((uint16_t)(buf[2] & 0x0F) << 8) | buf[3];
    uint16_t raw_y   = ((uint16_t)(buf[4] & 0x0F) << 8) | buf[5];

    ev->pressed = (fingers > 0);
    ev->gesture = buf[0];
    ev->x       = MAP_SCREEN_X(raw_x, raw_y);
    ev->y       = MAP_SCREEN_Y(raw_x, raw_y);
    return true;
}
