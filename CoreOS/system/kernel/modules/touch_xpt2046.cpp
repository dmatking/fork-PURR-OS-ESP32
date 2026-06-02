#include "touch_xpt2046.h"
#include <Arduino.h>

// CYD touch SPI pins
static const int8_t T_MOSI = 32;
static const int8_t T_MISO = 39;  // input-only GPIO — no OUTPUT
static const int8_t T_SCLK = 25;
static const int8_t T_CS   = 33;
static const int8_t T_IRQ  = 36;  // input-only GPIO

// Raw ADC calibration for CYD 320×240 landscape.
// Adjust if touch feels off — read raw values with verbose mode.
static const uint16_t X_RAW_MIN = 200;
static const uint16_t X_RAW_MAX = 3900;
static const uint16_t Y_RAW_MIN = 200;
static const uint16_t Y_RAW_MAX = 3900;

// XPT2046 channel commands (12-bit, differential, power down between conversions)
static const uint8_t CMD_X = 0xD0;  // A2:A1:A0 = 101
static const uint8_t CMD_Y = 0x90;  // A2:A1:A0 = 001
static const uint8_t CMD_Z1 = 0xB0; // A2:A1:A0 = 011 — touch pressure

// ── Software SPI ──────────────────────────────────────────────────────────────

static void spi_write_byte(uint8_t b) {
    for (int i = 7; i >= 0; i--) {
        digitalWrite(T_SCLK, LOW);
        digitalWrite(T_MOSI, (b >> i) & 1);
        delayMicroseconds(1);
        digitalWrite(T_SCLK, HIGH);
        delayMicroseconds(1);
    }
}

static uint16_t spi_read16() {
    uint16_t result = 0;
    for (int i = 15; i >= 0; i--) {
        digitalWrite(T_SCLK, LOW);
        delayMicroseconds(1);
        if (digitalRead(T_MISO)) result |= (1u << i);
        digitalWrite(T_SCLK, HIGH);
        delayMicroseconds(1);
    }
    return result;
}

static uint16_t xpt_read_channel(uint8_t cmd) {
    digitalWrite(T_CS, LOW);
    spi_write_byte(cmd);
    uint16_t raw = spi_read16() >> 3;  // 12-bit result in bits [14:3]
    digitalWrite(T_CS, HIGH);
    return raw & 0x0FFF;
}

// ── Coordinate mapping ────────────────────────────────────────────────────────

static uint16_t map_clamp(uint16_t raw, uint16_t raw_min, uint16_t raw_max, uint16_t out_max) {
    if (raw <= raw_min) return 0;
    if (raw >= raw_max) return out_max;
    return (uint16_t)((uint32_t)(raw - raw_min) * out_max / (raw_max - raw_min));
}

// ── Public API ────────────────────────────────────────────────────────────────

void touch_xpt2046_init() {
    pinMode(T_MOSI, OUTPUT);
    pinMode(T_SCLK, OUTPUT);
    pinMode(T_CS,   OUTPUT);
    pinMode(T_MISO, INPUT);
    pinMode(T_IRQ,  INPUT);

    digitalWrite(T_CS,   HIGH);
    digitalWrite(T_SCLK, LOW);
    digitalWrite(T_MOSI, LOW);

    Serial.println("[touch] XPT2046 OK (soft SPI)");
}

void touch_xpt2046_deinit() {
    digitalWrite(T_CS, HIGH);
}

bool touch_xpt2046_get_event(xpt_touch_event_t* out) {
    // IRQ line low = finger down
    if (digitalRead(T_IRQ) != LOW) {
        out->pressed = false;
        return false;
    }

    // Read pressure to confirm real touch vs noise
    uint16_t z1 = xpt_read_channel(CMD_Z1);
    if (z1 < 50) {
        out->pressed = false;
        return false;
    }

    // Average 4 samples to reduce jitter
    uint32_t rx = 0, ry = 0;
    for (int i = 0; i < 4; i++) {
        rx += xpt_read_channel(CMD_X);
        ry += xpt_read_channel(CMD_Y);
    }
    rx /= 4;
    ry /= 4;

    // CYD landscape: raw X → screen Y, raw Y → screen X (rotated 90°)
    out->x       = map_clamp((uint16_t)ry, Y_RAW_MIN, Y_RAW_MAX, 319);
    out->y       = map_clamp((uint16_t)rx, X_RAW_MIN, X_RAW_MAX, 239);
    out->pressed = true;
    return true;
}

#ifdef PURR_HAS_LVGL
void touch_xpt2046_lvgl_read(lv_indev_drv_t* drv, lv_indev_data_t* data) {
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
