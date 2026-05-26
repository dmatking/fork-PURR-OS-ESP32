#include "touch_mxt336t.h"
#include <Arduino.h>
#include <Wire.h>

// mXT336T touch controller — I2C read of T9/T100 message objects.
// Full Atmel Object Protocol implementation would require the MXT datasheet.
// This driver reads the interrupt pin and basic XY from the T100 object.

static bool mxt_ok = false;
static mxt_touch_event_t last_event = {0, 0, false, 0};
static volatile bool mxt_int_fired  = false;

static void IRAM_ATTR mxt_isr() {
    mxt_int_fired = true;
}

static bool mxt_read_t100(mxt_touch_event_t* out) {
    // T100 MultiTouch message — object address TBD per mXT config report.
    // Placeholder: read 8 bytes from I2C address.
    Wire.beginTransmission(MXT_I2C_ADDR);
    Wire.write(0x00);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) return false;

    Wire.requestFrom(MXT_I2C_ADDR, 8);
    if (Wire.available() < 8) return false;

    uint8_t buf[8];
    for (int i = 0; i < 8; i++) buf[i] = Wire.read();

    // Byte layout from mXT336T T100 message (simplified):
    // buf[2:3] = X LSB/MSB, buf[4:5] = Y LSB/MSB, buf[1] bit0 = touch status
    out->contact_id = buf[0] & 0x0F;
    out->pressed    = (buf[1] & 0x01) != 0;
    out->x          = buf[2] | ((uint16_t)buf[3] << 8);
    out->y          = buf[4] | ((uint16_t)buf[5] << 8);
    return true;
}

void touch_mxt336t_init() {
    pinMode(MXT_INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(MXT_INT_PIN), mxt_isr, FALLING);

    // Check device is present
    Wire.beginTransmission(MXT_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.printf("[touch] mXT336T not found at 0x%02X\n", MXT_I2C_ADDR);
        return;
    }

    mxt_ok = true;
    Serial.println("[touch] mXT336T OK");
}

void touch_mxt336t_update() {
    if (!mxt_ok || !mxt_int_fired) return;
    mxt_int_fired = false;
    mxt_read_t100(&last_event);
}

void touch_mxt336t_deinit() {
    detachInterrupt(digitalPinToInterrupt(MXT_INT_PIN));
    mxt_ok = false;
}

bool touch_mxt336t_get_event(mxt_touch_event_t* out) {
    if (!mxt_ok) return false;
    *out = last_event;
    return last_event.pressed;
}
