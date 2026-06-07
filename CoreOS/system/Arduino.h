// Arduino.h — Minimal Arduino API compatibility layer for ESP-IDF

#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
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
#define INPUT        GPIO_MODE_INPUT
#define OUTPUT       GPIO_MODE_OUTPUT
#define INPUT_PULLUP   0x10
#define INPUT_PULLDOWN 0x11

// Logic levels
#define HIGH 1
#define LOW  0

// GPIO functions
inline void pinMode(uint8_t pin, uint8_t mode) {
    if (mode == INPUT_PULLUP) {
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY);
    } else if (mode == INPUT_PULLDOWN) {
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLDOWN_ONLY);
    } else {
        gpio_set_direction((gpio_num_t)pin, (gpio_mode_t)mode);
    }
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
    void printf(const char* fmt, ...);
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

    uint8_t transfer(uint8_t val) {
        if (!_handle) return 0;
        spi_transaction_t tx = {};
        uint8_t rx = 0;
        tx.tx_buffer = &val;
        tx.rx_buffer = &rx;
        tx.length = 8;
        spi_device_transmit(_handle, &tx);
        return rx;
    }

    uint16_t transfer16(uint16_t val) {
        uint16_t be = (val >> 8) | (val << 8);
        if (!_handle) return 0;
        spi_transaction_t tx = {};
        uint16_t rx = 0;
        tx.tx_buffer = &be;
        tx.rx_buffer = &rx;
        tx.length = 16;
        spi_device_transmit(_handle, &tx);
        return (rx >> 8) | (rx << 8);
    }

    void beginTransaction(uint32_t /*settings*/) {}
    void endTransaction() {}

    void setFrequency(uint32_t /*freq*/) {}
    void setDataMode(uint8_t /*mode*/) {}
    void setBitOrder(uint8_t /*order*/) {}
};

extern SPIClass SPI;

// ── Arduino math / stdlib helpers ─────────────────────────────────────────────
#include <cstdlib>

inline long random(long max_val) {
    return (max_val > 0) ? (rand() % max_val) : 0;
}
inline long random(long min_val, long max_val) {
    return (max_val > min_val) ? (min_val + rand() % (max_val - min_val)) : min_val;
}

inline char* ltoa(long val, char* buf, int base) {
    char tmp[34]; int i = 0; bool neg = (base == 10 && val < 0);
    unsigned long uval = neg ? -(unsigned long)val : (unsigned long)val;
    if (uval == 0) { tmp[i++] = '0'; }
    else { while (uval) { tmp[i++] = "0123456789abcdef"[uval % base]; uval /= base; } }
    if (neg) tmp[i++] = '-';
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0'; return buf;
}

template<typename T> inline T arduinoMin(T a, T b) { return a < b ? a : b; }
template<typename T> inline T arduinoMax(T a, T b) { return a > b ? a : b; }
#ifndef min
#  define min(a, b) arduinoMin((a), (b))
#endif
#ifndef max
#  define max(a, b) arduinoMax((a), (b))
#endif

// constrain(x, lo, hi)
#ifndef constrain
#  define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif

// On ESP32 there's no port register concept; pins > 31 map to GPIO_OUT1 register.
#ifndef digitalPinToBitMask
#  define digitalPinToBitMask(pin) (1UL << ((pin) & 31))
#endif
#ifndef digitalPinToPort
#  define digitalPinToPort(pin) (0)
#endif
#ifndef portOutputRegister
#  define portOutputRegister(port) ((volatile uint32_t*)0)
#endif

// ── AVR/Arduino compat macros used by TFT_eSPI and other libs ─────────────────
// On ESP32 flash and RAM share the same address space — pgm_read_* are no-ops.
#ifndef pgm_read_byte
#  define pgm_read_byte(addr)   (*((const uint8_t  *)(addr)))
#endif
#ifndef pgm_read_word
#  define pgm_read_word(addr)   (*((const uint16_t *)(addr)))
#endif
#ifndef pgm_read_dword
#  define pgm_read_dword(addr)  (*((const uint32_t *)(addr)))
#endif
#ifndef PROGMEM
#  define PROGMEM
#endif
#ifndef F
#  define F(s) (s)
#endif

// yield() — cooperative multitasking hook; FreeRTOS taskYIELD on ESP32.
inline void yield() { taskYIELD(); }
