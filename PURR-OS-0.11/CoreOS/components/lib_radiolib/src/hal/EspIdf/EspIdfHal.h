#pragma once

#include "../../Hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// EspIdfHal — RadioLib HAL for pure ESP-IDF (no Arduino).
// IDF types are hidden from callers so this header needs no esp_driver_spi.
class EspIdfHal : public RadioLibHal {
public:
    // host: SPI2_HOST=1, SPI3_HOST=2
    explicit EspIdfHal(int host, int sck, int miso, int mosi, int cs,
                       uint32_t freq_hz = 4000000);

    void init() override;
    void term() override;
    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t pin, void (*cb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t pin) override;
    void delay(RadioLibTime_t ms) override;
    void delayMicroseconds(RadioLibTime_t us) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override;
    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override;
    void spiEndTransaction() override;
    void spiEnd() override;
    void yield() override;

private:
    int      _host;
    int      _sck, _miso, _mosi, _cs;
    uint32_t _freq_hz;
    void*    _dev = nullptr;  // spi_device_handle_t (opaque to callers)
};
