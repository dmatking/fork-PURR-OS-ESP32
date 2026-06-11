// Waveshare 1.69" CST816S touch HAL — pins embedded inline (different from CYD S024C).
// Verify SDA/SCL/INT/RST against your specific board revision before flashing.

#include "hal/hal_touch.h"
#include "display_st7789.h"    // for ST7789_TFT_WIDTH / ST7789_TFT_HEIGHT
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Waveshare ESP32-S3 1.69" CST816S wiring — verified against schematic
#define WS_CST_ADDR  0x15
#define WS_CST_SDA   11
#define WS_CST_SCL   10
#define WS_CST_INT   46
#define WS_CST_RST   -1  // no RST pin exposed on this board
#define WS_CST_REG   0x01

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static bool s_pressed = false;
static int16_t s_x = 0, s_y = 0;

extern "C" {

void mw_hal_touch_init(void) {
#if WS_CST_RST >= 0
    gpio_set_direction((gpio_num_t)WS_CST_RST, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)WS_CST_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)WS_CST_RST, 1); vTaskDelay(pdMS_TO_TICKS(50));
#else
    vTaskDelay(pdMS_TO_TICKS(50));
#endif

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port      = I2C_NUM_0;
    bus_cfg.sda_io_num    = (gpio_num_t)WS_CST_SDA;
    bus_cfg.scl_io_num    = (gpio_num_t)WS_CST_SCL;
    bus_cfg.clk_source    = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    i2c_new_master_bus(&bus_cfg, &s_bus);

    i2c_device_config_t dev_cfg = {};
    dev_cfg.device_address = WS_CST_ADDR;
    dev_cfg.scl_speed_hz   = 400000;
    i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
}

bool mw_hal_touch_is_recalibration_required(void) {
    return false;
}

mw_hal_touch_state_t mw_hal_touch_get_state(void) {
    uint8_t reg = WS_CST_REG;
    uint8_t buf[6] = {};
    if (i2c_master_transmit_receive(s_dev, &reg, 1, buf, 6, 20) != ESP_OK) {
        s_pressed = false;
        return MW_HAL_TOUCH_STATE_UP;
    }
    uint8_t gesture = buf[0];
    uint8_t points  = buf[1] & 0x0F;
    (void)gesture;
    if (points == 0) {
        s_pressed = false;
        return MW_HAL_TOUCH_STATE_UP;
    }
    // CST816S reports raw x,y — for Waveshare portrait 240x280 no axis swap needed
    s_x = (int16_t)(((buf[2] & 0x0F) << 8) | buf[3]);
    s_y = (int16_t)(((buf[4] & 0x0F) << 8) | buf[5]);
    s_pressed = true;
    return MW_HAL_TOUCH_STATE_DOWN;
}

bool mw_hal_touch_get_point(uint16_t *x, uint16_t *y) {
    if (mw_hal_touch_get_state() == MW_HAL_TOUCH_STATE_UP) return false;
    *x = (uint16_t)((int32_t)s_x * 4096 / ST7789_TFT_WIDTH);
    *y = (uint16_t)((int32_t)s_y * 4096 / ST7789_TFT_HEIGHT);
    return true;
}

} // extern "C"
