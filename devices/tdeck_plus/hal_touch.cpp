// T-Deck Plus GT911 touch HAL — embedded inline (different pins from JC3248W535 driver).
// GT911 shares the I2C bus with the keyboard on T-Deck Plus; bus is init'd here.

#include "hal/hal_touch.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TDECK_LCD_WIDTH   320
#define TDECK_LCD_HEIGHT  240

// T-Deck Plus GT911 wiring
#define GT_ADDR   0x5D
#define GT_SDA    18
#define GT_SCL    8
#define GT_INT    16
#define GT_RST    17

#define GT_REG_STATUS 0x814E
#define GT_REG_PT1    0x8150

static const char* TAG = "tdeck_touch";
static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static bool s_pressed = false;
static int16_t s_x = 0, s_y = 0;

static void _write(uint16_t reg, uint8_t val) {
    uint8_t buf[] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), val };
    i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static bool _read(uint16_t reg, uint8_t* buf, uint8_t len) {
    uint8_t addr[] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_transmit_receive(s_dev, addr, sizeof(addr), buf, len, 100) == ESP_OK;
}

extern "C" {

void mw_hal_touch_init(void) {
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port      = I2C_NUM_0;
    bus_cfg.sda_io_num    = (gpio_num_t)GT_SDA;
    bus_cfg.scl_io_num    = (gpio_num_t)GT_SCL;
    bus_cfg.clk_source    = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    i2c_new_master_bus(&bus_cfg, &s_bus);

    i2c_device_config_t dev_cfg = {};
    dev_cfg.device_address = GT_ADDR;
    dev_cfg.scl_speed_hz   = 400000;
    i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);

    gpio_set_direction((gpio_num_t)GT_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)GT_INT, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)GT_INT, 0);
    gpio_set_level((gpio_num_t)GT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)GT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_direction((gpio_num_t)GT_INT, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t id[4] = {};
    if (_read(0x8140, id, 4))
        ESP_LOGI(TAG, "GT911 ID: %c%c%c  addr=0x%02X", id[0], id[1], id[2], GT_ADDR);
    else
        ESP_LOGI(TAG, "GT911 no response at 0x%02X", GT_ADDR);
}

bool mw_hal_touch_is_recalibration_required(void) {
    return false;
}

mw_hal_touch_state_t mw_hal_touch_get_state(void) {
    uint8_t status = 0;
    if (!_read(GT_REG_STATUS, &status, 1)) {
        s_pressed = false;
        return MW_HAL_TOUCH_STATE_UP;
    }
    bool ready = (status & 0x80) != 0;
    uint8_t pts = status & 0x0F;
    if (!ready || pts == 0) {
        if (ready) _write(GT_REG_STATUS, 0);
        s_pressed = false;
        return MW_HAL_TOUCH_STATE_UP;
    }
    uint8_t buf[7] = {};
    bool ok = _read(GT_REG_PT1, buf, 7);
    _write(GT_REG_STATUS, 0);
    if (!ok) { s_pressed = false; return MW_HAL_TOUCH_STATE_UP; }

    s_x = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
    s_y = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
    s_pressed = true;
    return MW_HAL_TOUCH_STATE_DOWN;
}

bool mw_hal_touch_get_point(uint16_t *x, uint16_t *y) {
    if (mw_hal_touch_get_state() == MW_HAL_TOUCH_STATE_UP) return false;
    *x = (uint16_t)((int32_t)s_x * 4096 / TDECK_LCD_WIDTH);
    *y = (uint16_t)((int32_t)s_y * 4096 / TDECK_LCD_HEIGHT);
    return true;
}

} // extern "C"
