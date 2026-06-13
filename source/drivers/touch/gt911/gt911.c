// gt911.c — GT911 capacitive touch driver for PURR OS
// Interface: I2C, address 0x5D (primary) or 0x14 (alternate)
//
// Used on:
//   tdeck_plus — SDA=18, SCL=8, INT=16, RST=-1 (not connected)
//
// Register map:
//   0x814E = touch status (bits[3:0] = touch count; write 0 to clear)
//   0x8150 = first touch point data (5 bytes):
//            byte0: track_id
//            byte1: x_low
//            byte2: x_high
//            byte3: y_low
//            byte4: y_high
//
// INT pin:
//   0xFF or -1  → polling mode (reads status register each call)
//   valid GPIO  → falling-edge ISR sets data-ready flag
//
// RST pin:
//   0xFF or -1  → not connected, skip reset pulse
//   valid GPIO  → pulse low 20ms then high during init
//
// I2C: 400 kHz
// ESP-IDF v5.x (i2c_master_* APIs)

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#include "../../kernel/catcalls/catcall_touch.h"
#include "../../kernel/core/purr_module.h"
#include "../../kernel/core/purr_kernel.h"

static const char *TAG = "gt911";

// ── Forward declarations ──────────────────────────────────────────────────────

static const catcall_touch_t s_catcall;
static esp_err_t gt911_init(const touch_config_t *cfg);
static bool      gt911_read_point(uint16_t *x, uint16_t *y);
static bool      gt911_is_pressed(void);
static esp_err_t gt911_deinit(void);

// ── Constants ─────────────────────────────────────────────────────────────────

#define GT911_I2C_ADDR_PRIMARY   0x5D
#define GT911_I2C_ADDR_ALT       0x14
#define GT911_I2C_FREQ_HZ        400000

#define GT911_REG_STATUS         0x814E
#define GT911_REG_POINT1         0x8150

// Status register bitmask
#define GT911_STATUS_TOUCH_MASK  0x0F
#define GT911_STATUS_BUFFER_STAT 0x80   // buffer ready bit

// Default pins (tdeck_plus)
#define GT911_DEFAULT_SDA    18
#define GT911_DEFAULT_SCL     8
#define GT911_DEFAULT_INT    16
#define GT911_DEFAULT_RST  0xFF   // not connected

// ── Touch point struct ────────────────────────────────────────────────────────

typedef struct __attribute__((packed)) {
    uint8_t  track_id;
    uint8_t  x_low;
    uint8_t  x_high;
    uint8_t  y_low;
    uint8_t  y_high;
} gt911_point_t;

// ── Module state ──────────────────────────────────────────────────────────────

static i2c_master_bus_handle_t  s_bus_handle  = NULL;
static i2c_master_dev_handle_t  s_dev_handle  = NULL;
static int                      s_int_pin     = -1;
static int                      s_rst_pin     = -1;
static volatile bool            s_data_ready  = false;
static bool                     s_poll_mode   = true;
static bool                     s_initialized = false;

// ── GPIO ISR ──────────────────────────────────────────────────────────────────

static void IRAM_ATTR gt911_int_isr(void *arg)
{
    s_data_ready = true;
}

// ── I2C helpers ───────────────────────────────────────────────────────────────

// Write to a 16-bit register address (big-endian)
static esp_err_t gt911_write_reg(uint16_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[2 + len];
    buf[0] = (uint8_t)(reg >> 8);
    buf[1] = (uint8_t)(reg & 0xFF);
    memcpy(&buf[2], data, len);
    return i2c_master_transmit(s_dev_handle, buf, sizeof(buf), pdMS_TO_TICKS(50));
}

// Read from a 16-bit register address
static esp_err_t gt911_read_reg(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_transmit_receive(s_dev_handle, addr, sizeof(addr),
                                       data, len, pdMS_TO_TICKS(50));
}

// Clear the touch status register (write 0x00 to 0x814E)
static esp_err_t gt911_clear_status(void)
{
    uint8_t zero = 0x00;
    return gt911_write_reg(GT911_REG_STATUS, &zero, 1);
}

// ── catcall_touch_t implementation ────────────────────────────────────────────

static esp_err_t gt911_init(const touch_config_t *cfg)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_OK;
    }

    int sda   = cfg ? cfg->sda_pin  : GT911_DEFAULT_SDA;
    int scl   = cfg ? cfg->scl_pin  : GT911_DEFAULT_SCL;
    s_int_pin = cfg ? (cfg->int_pin != 0xFF ? (int)cfg->int_pin : -1) : GT911_DEFAULT_INT;
    s_rst_pin = cfg ? (cfg->rst_pin != 0xFF ? (int)cfg->rst_pin : -1) : -1;
    int port  = cfg ? cfg->i2c_port : 0;

    s_poll_mode = (s_int_pin < 0);

    // RST pulse if pin is connected
    if (s_rst_pin >= 0) {
        gpio_config_t rst_cfg = {
            .pin_bit_mask = (1ULL << s_rst_pin),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&rst_cfg), TAG, "RST gpio config failed");
        gpio_set_level(s_rst_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(s_rst_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(100));  // GT911 needs ~100ms after reset
    }

    // I2C master bus
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = (i2c_port_num_t)port,
        .sda_io_num        = sda,
        .scl_io_num        = scl,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus_handle),
                        TAG, "I2C bus init failed");

    // Try primary address first, fall back to alternate
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = GT911_I2C_ADDR_PRIMARY,
        .scl_speed_hz    = GT911_I2C_FREQ_HZ,
    };
    esp_err_t ret = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "0x5D failed (%s), trying 0x14", esp_err_to_name(ret));
        dev_cfg.device_address = GT911_I2C_ADDR_ALT;
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &s_dev_handle),
                            TAG, "I2C add device failed (both addresses)");
    }

    // Clear any stale status
    gt911_clear_status();

    // INT pin setup
    if (!s_poll_mode) {
        gpio_config_t int_cfg = {
            .pin_bit_mask = (1ULL << s_int_pin),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_NEGEDGE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "INT gpio config failed");
        gpio_install_isr_service(0);
        gpio_isr_handler_add((gpio_num_t)s_int_pin, gt911_int_isr, NULL);
    }

    s_initialized = true;
    ESP_LOGI(TAG, "GT911 initialized (SDA=%d, SCL=%d, INT=%d, RST=%d, poll=%d)",
             sda, scl, s_int_pin, s_rst_pin, s_poll_mode);
    purr_kernel_register_touch(&s_catcall);
    return ESP_OK;
}

static bool gt911_is_pressed(void)
{
    if (!s_initialized) return false;

    if (!s_poll_mode) {
        // INT pin: active low = finger down
        return gpio_get_level(s_int_pin) == 0;
    }

    // Polling: read status register
    uint8_t status = 0;
    if (gt911_read_reg(GT911_REG_STATUS, &status, 1) != ESP_OK) return false;

    // Buffer-ready bit must be set and touch count > 0
    if (!(status & GT911_STATUS_BUFFER_STAT)) return false;
    return (status & GT911_STATUS_TOUCH_MASK) > 0;
}

static bool gt911_read_point(uint16_t *x, uint16_t *y)
{
    if (!s_initialized || !x || !y) return false;

    // In interrupt mode: only proceed if ISR flagged data or INT pin is still low
    if (!s_poll_mode && !s_data_ready) {
        if (gpio_get_level(s_int_pin) != 0) return false;
        // INT is still asserted — fall through and read
    }

    // Read status register
    uint8_t status = 0;
    if (gt911_read_reg(GT911_REG_STATUS, &status, 1) != ESP_OK) return false;

    // Check buffer ready + at least one touch
    if (!(status & GT911_STATUS_BUFFER_STAT)) return false;
    uint8_t touch_count = status & GT911_STATUS_TOUCH_MASK;
    if (touch_count == 0) {
        gt911_clear_status();
        s_data_ready = false;
        return false;
    }

    // Read first touch point (5 bytes at 0x8150)
    gt911_point_t pt;
    esp_err_t ret = gt911_read_reg(GT911_REG_POINT1, (uint8_t *)&pt, sizeof(pt));
    if (ret != ESP_OK) {
        gt911_clear_status();
        s_data_ready = false;
        return false;
    }

    *x = (uint16_t)pt.x_low | ((uint16_t)pt.x_high << 8);
    *y = (uint16_t)pt.y_low | ((uint16_t)pt.y_high << 8);

    // Mandatory: clear status so the GT911 can report the next event
    gt911_clear_status();
    s_data_ready = false;
    return true;
}

static esp_err_t gt911_deinit(void)
{
    if (!s_initialized) return ESP_OK;

    if (!s_poll_mode && s_int_pin >= 0) {
        gpio_isr_handler_remove((gpio_num_t)s_int_pin);
    }
    if (s_dev_handle) {
        i2c_master_bus_rm_device(s_dev_handle);
        s_dev_handle = NULL;
    }
    if (s_bus_handle) {
        i2c_del_master_bus(s_bus_handle);
        s_bus_handle = NULL;
    }
    s_initialized = false;
    return ESP_OK;
}

// ── Static catcall descriptor ─────────────────────────────────────────────────

static const catcall_touch_t s_catcall = {
    .name            = "gt911",
    .catcall_version = CATCALL_TOUCH_VERSION,
    .init            = gt911_init,
    .read_point      = gt911_read_point,
    .is_pressed      = gt911_is_pressed,
    .deinit          = gt911_deinit,
};

// ── Module lifecycle ──────────────────────────────────────────────────────────

static int gt911_drv_init(void)
{
    touch_config_t default_cfg = {
        .i2c_port = 0,
        .sda_pin  = GT911_DEFAULT_SDA,
        .scl_pin  = GT911_DEFAULT_SCL,
        .int_pin  = GT911_DEFAULT_INT,
        .rst_pin  = GT911_DEFAULT_RST,
    };
    esp_err_t ret = gt911_init(&default_cfg);
    return (ret == ESP_OK) ? 0 : -1;
}

static void gt911_drv_deinit(void)
{
    gt911_deinit();
}

// ── PURR module header ────────────────────────────────────────────────────────

purr_module_header_t purr_module = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_DRIVER,
    .load_priority     = PURR_PRIORITY_REQUIRED,
    .name              = "gt911",
    .version           = "1.0.0",
    .kernel_min        = "0.9.0",
    .kernel_max        = "",
    .provided_catcalls = CATCALL_FLAG_TOUCH,
    .required_catcalls = 0,
    .init              = gt911_drv_init,
    .deinit            = gt911_drv_deinit,
};
