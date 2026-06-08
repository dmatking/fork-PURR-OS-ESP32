// Arduino.h — minimal shim for TFT_eSPI, pure ESP-IDF environment.
// Only APIs actually used by TFT_eSPI and its bundled extensions.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include <cstdlib>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"

// ── Types ─────────────────────────────────────────────────────────────────────
typedef uint8_t  byte;
typedef uint32_t word;
typedef bool     boolean;

// ── Timing ────────────────────────────────────────────────────────────────────
inline uint32_t millis()            { return (uint32_t)(esp_timer_get_time() / 1000); }
inline uint32_t micros()            { return (uint32_t)(esp_timer_get_time()); }
inline void delay(uint32_t ms)      { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }
inline void delayMicroseconds(uint32_t us) { esp_rom_delay_us(us); }

// ── Logic levels / pin modes ──────────────────────────────────────────────────
#define HIGH 1
#define LOW  0
#define INPUT        GPIO_MODE_INPUT
#define OUTPUT       GPIO_MODE_OUTPUT
#define INPUT_PULLUP 0x10

inline void pinMode(uint8_t pin, uint8_t mode) {
    if (mode == INPUT_PULLUP) {
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY);
    } else {
        gpio_set_direction((gpio_num_t)pin, (gpio_mode_t)mode);
    }
}
inline void digitalWrite(uint8_t pin, uint8_t val) { gpio_set_level((gpio_num_t)pin, val); }
inline uint8_t digitalRead(uint8_t pin) { return (uint8_t)gpio_get_level((gpio_num_t)pin); }
inline void yield() { taskYIELD(); }

// ── Math ──────────────────────────────────────────────────────────────────────
using std::round;
using std::abs;
using std::sqrt;

inline long random(long max_val) {
    return (max_val > 0) ? (rand() % max_val) : 0;
}
inline long random(long min_val, long max_val) {
    return (max_val > min_val) ? (min_val + rand() % (max_val - min_val)) : min_val;
}

inline char* ltoa(long val, char* buf, int base) {
    char tmp[34]; int i = 0; bool neg = (base == 10 && val < 0);
    unsigned long uv = neg ? -(unsigned long)val : (unsigned long)val;
    if (uv == 0) { tmp[i++] = '0'; }
    else { while (uv) { tmp[i++] = "0123456789abcdef"[uv % base]; uv /= base; } }
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
#ifndef constrain
#  define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif

// ── PROGMEM / AVR compat (no-op on ESP32) ────────────────────────────────────
#define PROGMEM
#ifndef pgm_read_byte
#  define pgm_read_byte(addr)   (*(const uint8_t  *)(addr))
#  define pgm_read_word(addr)   (*(const uint16_t *)(addr))
#  define pgm_read_dword(addr)  (*(const uint32_t *)(addr))
#  define pgm_read_float(addr)  (*(const float    *)(addr))
#  define pgm_read_ptr(addr)    (*(const void* const*)(addr))
#endif
#ifndef digitalPinToBitMask
#  define digitalPinToBitMask(pin)     (1UL << ((pin) & 31))
#  define digitalPinToPort(pin)        (0)
#  define portOutputRegister(port)     ((volatile uint32_t*)0)
#endif

// ── SPI ───────────────────────────────────────────────────────────────────────
#include "driver/spi_master.h"

struct SPISettings {
    SPISettings() {}
    SPISettings(uint32_t, uint8_t, uint8_t) {}
};

class SPIClass {
public:
    void begin() {}

    // TFT_eSPI calls spi.begin(SCK, MISO, MOSI, SS) to set up GPIO matrix routing.
    // Without this the SPI peripheral clock is never enabled and no data reaches
    // the display even though the register writes appear to succeed.
    void begin(int8_t sck, int8_t miso, int8_t mosi, int8_t /*ss*/) {
        spi_bus_config_t cfg = {};
        cfg.mosi_io_num   = mosi;
        cfg.miso_io_num   = miso;
        cfg.sclk_io_num   = sck;
        cfg.quadwp_io_num = -1;
        cfg.quadhd_io_num = -1;
        // USE_HSPI_PORT → SPI2_HOST (HSPI, SPI2 peripheral)
        // Otherwise     → SPI3_HOST (VSPI, SPI3 peripheral)
#ifdef USE_HSPI_PORT
        spi_bus_initialize(SPI2_HOST, &cfg, SPI_DMA_DISABLED);
#else
        spi_bus_initialize(SPI3_HOST, &cfg, SPI_DMA_DISABLED);
#endif
    }

    void end()                           {}
    void beginTransaction(SPISettings)   {}
    void endTransaction()                {}
    void setFrequency(uint32_t)          {}
    void setDataMode(uint8_t)            {}
    void setBitOrder(uint8_t)            {}
    uint8_t  transfer(uint8_t v)         { return v; }
    uint16_t transfer16(uint16_t v)      { return v; }
    void writeBytes(const uint8_t* b, uint32_t n) { (void)b; (void)n; }
};
extern SPIClass SPI;

// ── String ────────────────────────────────────────────────────────────────────
class String {
    char*  buf_ = nullptr;
    size_t len_ = 0;
    size_t cap_ = 0;

    void grow(size_t needed) {
        if (needed >= cap_) {
            cap_ = needed + 32;
            char* nb = (char*)realloc(buf_, cap_);
            if (nb) buf_ = nb;
        }
    }
public:
    String() {}
    String(const char* s) { if (s) { len_ = strlen(s); grow(len_); if (buf_) memcpy(buf_, s, len_ + 1); } }
    String(const String& o) : String(o.c_str()) {}
    ~String() { free(buf_); }

    String& operator=(const char* s) {
        if (!s) { len_ = 0; if (buf_) buf_[0] = '\0'; return *this; }
        len_ = strlen(s); grow(len_);
        if (buf_) memcpy(buf_, s, len_ + 1);
        return *this;
    }
    String& operator=(const String& o) { return *this = o.c_str(); }

    const char* c_str()   const { return buf_ ? buf_ : ""; }
    size_t      length()  const { return len_; }
    bool        isEmpty() const { return len_ == 0; }

    bool operator==(const char*   s) const { return strcmp(c_str(), s ? s : "") == 0; }
    bool operator==(const String& o) const { return strcmp(c_str(), o.c_str())   == 0; }
    bool operator!=(const char*   s) const { return !(*this == s); }
    bool operator!=(const String& o) const { return !(*this == o); }

    String& operator+=(const char* s) {
        if (!s) return *this;
        size_t sl = strlen(s); grow(len_ + sl);
        if (buf_) { memcpy(buf_ + len_, s, sl + 1); len_ += sl; }
        return *this;
    }
    String& operator+=(const String& o) { return *this += o.c_str(); }
    String& operator+=(char c)          { char t[2] = {c, 0}; return *this += t; }

    void toCharArray(char* dst, size_t sz) const {
        if (!dst || sz == 0) return;
        size_t n = (len_ < sz - 1) ? len_ : sz - 1;
        if (buf_) memcpy(dst, buf_, n);
        dst[n] = '\0';
    }
    int indexOf(char c) const {
        const char* p = buf_ ? strchr(buf_, c) : nullptr;
        return p ? (int)(p - buf_) : -1;
    }
    String substring(size_t start, size_t end = (size_t)-1) const {
        if (!buf_ || start >= len_) return String();
        if (end > len_) end = len_;
        char tmp[end - start + 1];
        memcpy(tmp, buf_ + start, end - start);
        tmp[end - start] = '\0';
        return String(tmp);
    }
};
