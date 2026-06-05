// touch_gt911.cpp — GT911 5-point I2C capacitive touch driver
// Used on JC3248W535 (ESP32-S3, 3.5" 480x320).
// WIP — verify pins and I2C address on hardware.

#ifdef PURR_HAS_TOUCH_GT911

#include "touch_gt911.h"
#include <Arduino.h>
#include <Wire.h>

#define GT911_ADDR       0x5D   // flip to 0x14 if unresponsive
#define GT911_SDA        19
#define GT911_SCL        20
#define GT911_INT        18
#define GT911_RST        38

// GT911 register map (relevant subset)
#define GT911_REG_STATUS 0x814E  // touch status: bit7=buffer ready, bits[3:0]=point count
#define GT911_REG_PT1    0x8150  // first touch point (8 bytes: x_lo, x_hi, y_lo, y_hi, sz_lo, sz_hi, track_id, 0)

static TwoWire _wire(0);

static void _write_reg(uint16_t reg, uint8_t val) {
    _wire.beginTransmission(GT911_ADDR);
    _wire.write((uint8_t)(reg >> 8));
    _wire.write((uint8_t)(reg & 0xFF));
    _wire.write(val);
    _wire.endTransmission();
}

static bool _read_regs(uint16_t reg, uint8_t* buf, uint8_t len) {
    _wire.beginTransmission(GT911_ADDR);
    _wire.write((uint8_t)(reg >> 8));
    _wire.write((uint8_t)(reg & 0xFF));
    if (_wire.endTransmission(false) != 0) return false;
    if (_wire.requestFrom((uint8_t)GT911_ADDR, len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = _wire.read();
    return true;
}

void touch_gt911_init() {
    // Hard reset sequence
    pinMode(GT911_RST, OUTPUT);
    pinMode(GT911_INT, OUTPUT);

    // Pull INT low before reset to select I2C address 0x5D
    digitalWrite(GT911_INT, LOW);
    digitalWrite(GT911_RST, LOW);  delay(20);
    digitalWrite(GT911_RST, HIGH); delay(10);

    // Float INT so GT911 can use it as interrupt output
    pinMode(GT911_INT, INPUT);
    delay(50);

    _wire.begin(GT911_SDA, GT911_SCL, 400000);

    // Read product ID to confirm comms (should read "911\0")
    uint8_t id[4] = {};
    if (_read_regs(0x8140, id, 4)) {
        Serial.printf("[touch] GT911 ID: %c%c%c  addr=0x%02X\n",
                      id[0], id[1], id[2], GT911_ADDR);
    } else {
        Serial.printf("[touch] GT911 no response at 0x%02X — try 0x14\n", GT911_ADDR);
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
        // Clear buffer-ready flag
        if (ready) _write_reg(GT911_REG_STATUS, 0);
        ev->pressed = false;
        ev->points  = 0;
        return true;
    }

    // Read first touch point
    uint8_t buf[7] = {};
    bool ok = _read_regs(GT911_REG_PT1, buf, 7);

    // Clear buffer-ready flag
    _write_reg(GT911_REG_STATUS, 0);

    if (!ok) { ev->pressed = false; return false; }

    uint16_t raw_x = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    uint16_t raw_y = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);

    ev->pressed = true;
    ev->points  = pts;
    // GT911 reports in display coordinates directly — landscape 480x320
    ev->x = (int16_t)raw_x;
    ev->y = (int16_t)raw_y;
    return true;
}

#endif  // PURR_HAS_TOUCH_GT911
