#pragma once
// Arduino_sim.h — PC stub for <Arduino.h>
// Provides the types and functions that shell sources use.

#ifndef Arduino_sim_h
#define Arduino_sim_h

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#ifdef _WIN32
#  include <windows.h>
#  define SIM_SLEEP_MS(ms) Sleep(ms)
#else
#  include <unistd.h>
#  define SIM_SLEEP_MS(ms) usleep((ms)*1000)
#endif

// ── Arduino time ─────────────────────────────────────────────────────────────
#ifdef _WIN32
static inline uint32_t millis() { return (uint32_t)GetTickCount(); }
#else
#include <time.h>
static inline uint32_t millis() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
#endif

static inline void delay(uint32_t ms) { SIM_SLEEP_MS(ms); }
static inline void delayMicroseconds(uint32_t us) { SIM_SLEEP_MS(us / 1000 + 1); }

// ── GPIO stubs (no-ops) ───────────────────────────────────────────────────────
#define INPUT         0
#define OUTPUT        1
#define INPUT_PULLUP  2
#define HIGH          1
#define LOW           0

static inline void pinMode(int, int)          {}
static inline void digitalWrite(int, int)     {}
static inline int  digitalRead(int)           { return HIGH; }
static inline void ledcAttach(int,int,int)    {}
static inline void ledcWrite(int,int)         {}
static inline void ledcSetup(int,int,int)     {}
static inline void ledcAttachPin(int,int)     {}

// ── Serial stub ───────────────────────────────────────────────────────────────
struct _SimSerial {
    static void begin(int) {}
    static void print(const char* s)  { fputs(s, stdout); }
    static void println(const char* s){ puts(s); }
    static void printf(const char* fmt, ...) {
        va_list v; va_start(v, fmt); vprintf(fmt, v); va_end(v);
    }
    static void flush() { fflush(stdout); }
};
static _SimSerial Serial;

// ── String class (minimal) ────────────────────────────────────────────────────
#include <string>
typedef std::string String;

// ── min/max ───────────────────────────────────────────────────────────────────
#ifndef min
#  define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#  define max(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef constrain
#  define constrain(v,lo,hi) ((v)<(lo)?(lo):((v)>(hi)?(hi):(v)))
#endif

#endif // Arduino_sim_h
