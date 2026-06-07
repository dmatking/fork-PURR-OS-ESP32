#pragma once
// purr_idf_compat.h — Arduino API drop-in for pure ESP-IDF builds.
// Replace #include <Arduino.h> with #include "purr_idf_compat.h".
// Provides Serial, millis(), delay(), pinMode(), digitalWrite(), digitalRead(),
// ledcAttach(), ledcWrite(), String → std::string.
// Files can be ported incrementally to native IDF APIs.

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ── Time ──────────────────────────────────────────────────────────────────────
static inline uint32_t millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}
static inline uint32_t micros(void) {
    return (uint32_t)esp_timer_get_time();
}
static inline void delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}
static inline void delayMicroseconds(uint32_t us) {
    vTaskDelay(pdMS_TO_TICKS((us / 1000) + 1));
}

// ── GPIO ──────────────────────────────────────────────────────────────────────
#define INPUT         0
#define OUTPUT        1
#define INPUT_PULLUP  2
#define HIGH          1
#define LOW           0

static inline void pinMode(int pin, int mode) {
    if (pin < 0) return;
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    if (mode == OUTPUT) {
        cfg.mode = GPIO_MODE_OUTPUT;
    } else if (mode == INPUT_PULLUP) {
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    } else {
        cfg.mode = GPIO_MODE_INPUT;
    }
    gpio_config(&cfg);
}
static inline void digitalWrite(int pin, int val) {
    if (pin < 0) return;
    gpio_set_level((gpio_num_t)pin, val);
}
static inline int digitalRead(int pin) {
    if (pin < 0) return 0;
    return gpio_get_level((gpio_num_t)pin);
}

// ── LEDC (PWM) ────────────────────────────────────────────────────────────────
// Simple channel allocator — mirrors Arduino's ledcAttach/ledcWrite API.
// Supports up to 4 simultaneous PWM pins.

struct _PurrLedcPin { int pin; ledc_channel_t ch; bool used; };
static _PurrLedcPin _ledc_pins[4] = {};

static inline void ledcAttach(int pin, uint32_t freq, uint8_t resolution) {
    // Find free slot
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        if (!_ledc_pins[i].used || _ledc_pins[i].pin == pin) { slot = i; break; }
    }
    if (slot < 0) return;

    ledc_timer_config_t timer = {};
    timer.speed_mode      = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = (ledc_timer_bit_t)resolution;
    timer.timer_num       = (ledc_timer_t)slot;
    timer.freq_hz         = freq;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);

    ledc_channel_config_t ch_cfg = {};
    ch_cfg.gpio_num   = pin;
    ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_cfg.channel    = (ledc_channel_t)slot;
    ch_cfg.timer_sel  = (ledc_timer_t)slot;
    ch_cfg.duty       = 0;
    ch_cfg.hpoint     = 0;
    ledc_channel_config(&ch_cfg);

    _ledc_pins[slot] = { pin, (ledc_channel_t)slot, true };
}
static inline void ledcWrite(int pin, uint32_t duty) {
    for (int i = 0; i < 4; i++) {
        if (_ledc_pins[i].used && _ledc_pins[i].pin == pin) {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, _ledc_pins[i].ch, duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, _ledc_pins[i].ch);
            return;
        }
    }
}
static inline void ledcSetup(uint8_t, uint32_t, uint8_t) {}   // no-op (merged into attach)
static inline void ledcAttachPin(int pin, uint8_t ch) { (void)pin; (void)ch; }

// ── Serial ────────────────────────────────────────────────────────────────────
struct _PurrSerial {
    static void begin(int) {}  // IDF UART0 initialized by startup
    static void printf(const char* fmt, ...) {
        va_list args; va_start(args, fmt);
        char buf[256]; vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
        // Strip trailing newline — ESP_LOG adds its own
        int len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
        ESP_LOGI("purr", "%s", buf);
    }
    static void println(const char* s) { ESP_LOGI("purr", "%s", s); }
    static void println(int v)         { ESP_LOGI("purr", "%d", v); }
    static void print(const char* s)   { ESP_LOGI("purr", "%s", s); }
    static void print(int v)           { ESP_LOGI("purr", "%d", v); }
    static void flush()                {}
};
static _PurrSerial Serial __attribute__((unused));

// ── String ───────────────────────────────────────────────────────────────────
// Now provided by Arduino.h (class String) instead of std::string

// ── min/max/constrain ─────────────────────────────────────────────────────────
#ifndef min
#  define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#  define max(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef constrain
#  define constrain(v,lo,hi) ((v)<(lo)?(lo):((v)>(hi)?(hi):(v)))
#endif

// ── abs ───────────────────────────────────────────────────────────────────────
#ifndef abs
#  define abs(x) ((x)>=0?(x):-(x))
#endif
