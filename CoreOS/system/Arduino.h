// Arduino.h — Minimal Arduino API compatibility layer for ESP-IDF

#pragma once

#include <cstdint>
#include <cstring>
#include "driver/gpio.h"

// Standard BSD string functions
inline size_t strlcpy(char* dst, const char* src, size_t dsize) {
    size_t len = strlen(src);
    if (dsize > 0) {
        size_t copy_len = (len >= dsize) ? dsize - 1 : len;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }
    return len;
}
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include "esp_log.h"
#include "String.h"
#include "Print.h"

// Pin modes
#define INPUT  GPIO_MODE_INPUT
#define OUTPUT GPIO_MODE_OUTPUT

// Logic levels
#define HIGH 1
#define LOW  0

// GPIO functions
inline void pinMode(uint8_t pin, uint8_t mode) {
    gpio_set_direction((gpio_num_t)pin, (gpio_mode_t)mode);
}

inline void digitalWrite(uint8_t pin, uint8_t val) {
    gpio_set_level((gpio_num_t)pin, val);
}

inline uint8_t digitalRead(uint8_t pin) {
    return gpio_get_level((gpio_num_t)pin);
}

// Timing
inline void delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

inline void delayMicroseconds(uint32_t us) {
    ets_delay_us(us);
}

inline uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

inline uint32_t micros() {
    return (uint32_t)esp_timer_get_time();
}

// LEDC PWM (for backlight)
inline void ledcSetup(uint8_t channel, uint32_t freq, uint8_t resolution) {
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.timer_num = (ledc_timer_t)(channel & 0x0F);
    timer.duty_resolution = (ledc_timer_bit_t)resolution;
    timer.freq_hz = freq;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);
}

inline void ledcAttachPin(uint8_t pin, uint8_t channel) {
    ledc_channel_config_t ch_cfg = {};
    ch_cfg.gpio_num = pin;
    ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_cfg.channel = (ledc_channel_t)(channel & 0x0F);
    ch_cfg.intr_type = LEDC_INTR_DISABLE;
    ch_cfg.timer_sel = (ledc_timer_t)(channel & 0x0F);
    ch_cfg.duty = 0;
    ch_cfg.hpoint = 0;
    ledc_channel_config(&ch_cfg);
}

inline void ledcWrite(uint8_t channel, uint8_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)(channel & 0x0F), duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)(channel & 0x0F));
}

// Serial API
class SerialClass : public Print {
public:
    void begin(uint32_t baud) { (void)baud; }
};

extern SerialClass Serial;

// SPI class
class SPIClass {
private:
    spi_device_handle_t _handle = nullptr;

public:
    SPIClass() = default;

    void begin(int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1, int8_t cs = -1) {
        if (sck < 0 || mosi < 0) return;

        spi_bus_config_t bus_cfg = {};
        bus_cfg.mosi_io_num = mosi;
        bus_cfg.miso_io_num = miso;
        bus_cfg.sclk_io_num = sck;
        bus_cfg.quadwp_io_num = -1;
        bus_cfg.quadhd_io_num = -1;
        bus_cfg.max_transfer_sz = 320 * 240 * 2;
        bus_cfg.flags = SPICOMMON_BUSFLAG_MASTER;
        spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);

        spi_device_interface_config_t dev_cfg = {};
        dev_cfg.mode = 0;
        dev_cfg.clock_speed_hz = 40000000;
        dev_cfg.spics_io_num = cs;
        dev_cfg.queue_size = 7;
        spi_bus_add_device(SPI2_HOST, &dev_cfg, &_handle);
    }

    void writeBytes(const uint8_t* data, uint32_t len) {
        if (!_handle) return;
        spi_transaction_t tx = {};
        tx.tx_buffer = data;
        tx.length = len * 8;
        spi_device_transmit(_handle, &tx);
    }
};

extern SPIClass SPI;
