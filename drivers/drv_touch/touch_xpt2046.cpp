#include "touch_xpt2046.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"

// CYD touch SPI pins
static const gpio_num_t T_MOSI = GPIO_NUM_32;
static const gpio_num_t T_MISO = GPIO_NUM_39;  // input-only GPIO
static const gpio_num_t T_SCLK = GPIO_NUM_25;
static const gpio_num_t T_CS   = GPIO_NUM_33;
static const gpio_num_t T_IRQ  = GPIO_NUM_36;  // input-only GPIO

// Raw ADC calibration for CYD 320x240 landscape.
// Wider than the true edge values so extrapolation handles the last few pixels
// instead of clamping early (which caused coordinate drift near screen edges).
static const int32_t X_RAW_MIN = 150;
static const int32_t X_RAW_MAX = 3950;
static const int32_t Y_RAW_MIN = 150;
static const int32_t Y_RAW_MAX = 3950;

// XPT2046 channel commands (12-bit, differential, power down between conversions)
static const uint8_t CMD_X  = 0xD0;
static const uint8_t CMD_Y  = 0x90;
static const uint8_t CMD_Z1 = 0xB0;

// ── Software SPI ──────────────────────────────────────────────────────────────

static inline void pin_set(gpio_num_t pin, int level) {
    gpio_set_level(pin, level);
}

static inline int pin_get(gpio_num_t pin) {
    return gpio_get_level(pin);
}

static void spi_write_byte(uint8_t b) {
    for (int i = 7; i >= 0; i--) {
        pin_set(T_SCLK, 0);
        pin_set(T_MOSI, (b >> i) & 1);
        ets_delay_us(1);
        pin_set(T_SCLK, 1);
        ets_delay_us(1);
    }
}

static uint16_t spi_read16(void) {
    uint16_t result = 0;
    for (int i = 15; i >= 0; i--) {
        pin_set(T_SCLK, 0);
        ets_delay_us(1);
        if (pin_get(T_MISO)) result |= (1u << i);
        pin_set(T_SCLK, 1);
        ets_delay_us(1);
    }
    return result;
}

static uint16_t xpt_read_channel(uint8_t cmd) {
    pin_set(T_CS, 0);
    spi_write_byte(cmd);
    uint16_t raw = spi_read16() >> 3;  // 12-bit result in bits [14:3]
    pin_set(T_CS, 1);
    return raw & 0x0FFF;
}

// ── Coordinate mapping ────────────────────────────────────────────────────────

// Signed linear map with post-clamp — allows extrapolation past the calibration
// endpoints so edge pixels are reachable even if the raw ADC never quite hits
// X_RAW_MIN/MAX on this particular panel.
static uint16_t map_range(int32_t raw, int32_t raw_min, int32_t raw_max, uint16_t out_max) {
    int32_t v = (raw - raw_min) * (int32_t)out_max / (raw_max - raw_min);
    if (v < 0)              v = 0;
    if (v > (int32_t)out_max) v = (int32_t)out_max;
    return (uint16_t)v;
}

// ── Public API ────────────────────────────────────────────────────────────────

void touch_xpt2046_init(void) {
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << T_MOSI) | (1ULL << T_SCLK) | (1ULL << T_CS),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);

    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << T_MISO) | (1ULL << T_IRQ),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);

    pin_set(T_CS,   1);
    pin_set(T_SCLK, 0);
    pin_set(T_MOSI, 0);
}

void touch_xpt2046_deinit(void) {
    pin_set(T_CS, 1);
}

bool touch_xpt2046_get_event(xpt_touch_event_t *out) {
    if (pin_get(T_IRQ) != 0) {
        out->pressed = false;
        return false;
    }

    uint16_t z1 = xpt_read_channel(CMD_Z1);
    if (z1 < 50) {
        out->pressed = false;
        return false;
    }

    int32_t rx = 0, ry = 0;
    for (int i = 0; i < 8; i++) {
        rx += xpt_read_channel(CMD_X);
        ry += xpt_read_channel(CMD_Y);
    }
    rx /= 8;
    ry /= 8;

    // CYD landscape: raw X → screen Y, raw Y → screen X (rotated 90°)
    out->x       = map_range(ry, Y_RAW_MIN, Y_RAW_MAX, 319);
    out->y       = map_range(rx, X_RAW_MIN, X_RAW_MAX, 239);
    out->pressed = true;
    return true;
}

#ifdef PURR_HAS_LVGL
void touch_xpt2046_lvgl_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    xpt_touch_event_t ev;
    if (touch_xpt2046_get_event(&ev) && ev.pressed) {
        data->point.x = (lv_coord_t)ev.x;
        data->point.y = (lv_coord_t)ev.y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
#endif

#include "purr_sys_drv.h"
static sys_drv_t s_xpt2046_drv = {
    .name="touch:xpt2046",.subsystem="touch",.enabled=false,
    .init=touch_xpt2046_init,.tick=NULL,.deinit=touch_xpt2046_deinit,.cmd=NULL
};
void touch_xpt2046_drv_register(bool enabled){s_xpt2046_drv.enabled=enabled;sys_drv_register(&s_xpt2046_drv);}
