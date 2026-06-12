#include "touch_mxt336t.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char* TAG = "touch_mxt";

static bool mxt_ok = false;
static mxt_touch_event_t last_event = {0, 0, false, 0};
static volatile bool mxt_int_fired  = false;

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

static void IRAM_ATTR mxt_isr(void*) {
    mxt_int_fired = true;
}

static bool mxt_read_t100(mxt_touch_event_t* out) {
    uint8_t reg[2] = {0x00, 0x00};
    uint8_t buf[8] = {};

    if (i2c_master_transmit(s_dev, reg, 2, 10) != ESP_OK) return false;
    if (i2c_master_receive(s_dev, buf, 8, 10)  != ESP_OK) return false;

    out->contact_id = buf[0] & 0x0F;
    out->pressed    = (buf[1] & 0x01) != 0;
    out->x          = buf[2] | ((uint16_t)buf[3] << 8);
    out->y          = buf[4] | ((uint16_t)buf[5] << 8);
    return true;
}

void touch_mxt336t_init() {
    gpio_set_direction((gpio_num_t)MXT_INT_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)MXT_INT_PIN, GPIO_PULLUP_ONLY);
    gpio_set_intr_type((gpio_num_t)MXT_INT_PIN, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)MXT_INT_PIN, mxt_isr, nullptr);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = I2C_NUM_0,
        .sda_io_num        = (gpio_num_t)MXT_SDA_PIN,
        .scl_io_num        = (gpio_num_t)MXT_SCL_PIN,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags             = { .enable_internal_pullup = true },
    };
    i2c_new_master_bus(&bus_cfg, &s_bus);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = MXT_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };
    i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);

    uint8_t probe = 0;
    if (i2c_master_receive(s_dev, &probe, 1, 10) != ESP_OK) {
        ESP_LOGE(TAG, "mXT336T not found at 0x%02X", MXT_I2C_ADDR);
        return;
    }
    mxt_ok = true;
    ESP_LOGI(TAG, "mXT336T OK");
}

void touch_mxt336t_tick() {
    if (!mxt_ok || !mxt_int_fired) return;
    mxt_int_fired = false;
    mxt_read_t100(&last_event);
}

void touch_mxt336t_deinit() {
    gpio_isr_handler_remove((gpio_num_t)MXT_INT_PIN);
    mxt_ok = false;
}

bool touch_mxt336t_get_event(mxt_touch_event_t* out) {
    if (!mxt_ok) return false;
    *out = last_event;
    return last_event.pressed;
}

#include "purr_sys_drv.h"
static sys_drv_t s_mxt336t_drv = {
    .name="touch:mxt336t",.subsystem="touch",.enabled=false,
    .init=touch_mxt336t_init,.tick=touch_mxt336t_tick,.deinit=touch_mxt336t_deinit,.cmd=NULL
};
void touch_mxt336t_drv_register(bool enabled){s_mxt336t_drv.enabled=enabled;sys_drv_register(&s_mxt336t_drv);}
