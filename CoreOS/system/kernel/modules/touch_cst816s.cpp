// touch_cst816s.cpp — CST816S capacitive I2C touch driver
// Used on CYD ESP32-2432S024C (2.4" capacitive variant).

#ifdef PURR_HAS_TOUCH_CST816S

#include "touch_cst816s.h"
#include <Arduino.h>
#include <Wire.h>

#define CST_ADDR   0x15
#define CST_SDA    33
#define CST_SCL    32
#define CST_INT    21
#define CST_RST    25
#define CST_REG    0x01   // first data register (6 bytes: gesture, fingers, XH, XL, YH, YL)

// Landscape coordinate mapping for TFT rotation=1 (USB on right, 320×240).
// CST816S reports portrait coords: raw_x=0..239, raw_y=0..319.
// Adjust these if touch appears mirrored after testing.
#define MAP_SCREEN_X(rx, ry)  ((int16_t)(ry))          // portrait y → landscape x
#define MAP_SCREEN_Y(rx, ry)  ((int16_t)(rx))          // portrait x → landscape y

static TwoWire _wire(0);

void touch_cst816s_init() {
    pinMode(CST_RST, OUTPUT);
    digitalWrite(CST_RST, LOW);  delay(10);
    digitalWrite(CST_RST, HIGH); delay(50);

    pinMode(CST_INT, INPUT);

    _wire.begin(CST_SDA, CST_SCL, 400000);
    Serial.printf("[touch] CST816S init  SDA=%d SCL=%d INT=%d RST=%d\n",
                  CST_SDA, CST_SCL, CST_INT, CST_RST);
}

bool touch_cst816s_get_event(cst_touch_event_t* ev) {
    uint8_t buf[6] = {};

    _wire.beginTransmission(CST_ADDR);
    _wire.write(CST_REG);
    if (_wire.endTransmission(false) != 0) {
        ev->pressed = false;
        return false;
    }
    if (_wire.requestFrom((uint8_t)CST_ADDR, (uint8_t)6) != 6) {
        ev->pressed = false;
        return false;
    }
    for (int i = 0; i < 6; i++) buf[i] = _wire.read();

    uint8_t  fingers = buf[1];
    uint16_t raw_x   = ((uint16_t)(buf[2] & 0x0F) << 8) | buf[3];
    uint16_t raw_y   = ((uint16_t)(buf[4] & 0x0F) << 8) | buf[5];

    ev->pressed = (fingers > 0);
    ev->gesture = buf[0];
    ev->x       = MAP_SCREEN_X(raw_x, raw_y);
    ev->y       = MAP_SCREEN_Y(raw_x, raw_y);
    return true;
}

#endif  // PURR_HAS_TOUCH_CST816S
