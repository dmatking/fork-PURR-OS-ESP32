#pragma once
// Wire_sim.h — I2C stub for <Wire.h>
#include <stdint.h>
class TwoWire {
public:
    TwoWire(int = 0) {}
    void begin(int, int, uint32_t = 100000) {}
    void beginTransmission(uint8_t) {}
    uint8_t endTransmission(bool = true) { return 1; }
    uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
    void write(uint8_t) {}
    uint8_t read() { return 0; }
};
static TwoWire Wire(0);
