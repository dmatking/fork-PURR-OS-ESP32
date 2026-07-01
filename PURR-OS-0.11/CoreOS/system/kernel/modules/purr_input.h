#pragma once
// purr_input.h — unified PURR OS input event queue
//
// All hardware input sources (touch, keyboard, trackball, encoder, BT HID) post
// purr_input_event_t events into a single FreeRTOS queue. Consumers (MiniWin
// tick, KittenUI, scripts) read from this queue without caring about hardware.
//
// Usage:
//   Post:   purr_input_post(&evt);
//   Poll:   if (purr_input_poll(&evt)) { handle(evt); }
//   Block:  purr_input_wait(&evt, ms);

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Event types ───────────────────────────────────────────────────────────────

typedef enum {
    PURR_INPUT_NONE   = 0,
    PURR_INPUT_KEY,       // keycode in .key (ASCII or nav code below)
    PURR_INPUT_TOUCH_DN,  // touch press at .x .y (raw display pixels)
    PURR_INPUT_TOUCH_UP,  // touch release
    PURR_INPUT_TOUCH_MV,  // touch drag at .x .y
    PURR_INPUT_SCROLL,    // rotary encoder / scroll; delta in .delta (signed)
} purr_input_type_t;

// Nav key codes — match MiniWin MW_KEY_PRESSED_MESSAGE conventions
#define PURR_KEY_UP       0x01
#define PURR_KEY_DOWN     0x02
#define PURR_KEY_LEFT     0x03
#define PURR_KEY_RIGHT    0x04
#define PURR_KEY_ENTER    0x0D
#define PURR_KEY_ESC      0x1B
#define PURR_KEY_BACK     0x08
#define PURR_KEY_DEL      0x7F

typedef struct {
    purr_input_type_t type;
    union {
        uint8_t  key;        // PURR_INPUT_KEY: ASCII or PURR_KEY_* nav code
        struct { int16_t x, y; };  // PURR_INPUT_TOUCH_*
        int16_t  delta;      // PURR_INPUT_SCROLL: positive = clockwise / down
    };
    uint32_t timestamp_ms;  // esp_timer_get_time() / 1000 at post time
} purr_input_event_t;

// ── Lifecycle ─────────────────────────────────────────────────────────────────

// Call once from app_main before any input sources are registered.
// queue_depth: max events buffered (recommend 32).
void purr_input_init(uint32_t queue_depth);

// ── Post (called from HAL drivers / ISRs) ─────────────────────────────────────

// Post from task context (blocks 0 ticks if full — events are dropped).
bool purr_input_post(const purr_input_event_t *evt);

// Post from ISR (use inside interrupt handlers).
bool purr_input_post_from_isr(const purr_input_event_t *evt);

// Convenience wrappers
static inline bool purr_input_post_key(uint8_t key) {
    purr_input_event_t e = { .type = PURR_INPUT_KEY, .key = key };
    return purr_input_post(&e);
}
static inline bool purr_input_post_touch(purr_input_type_t t, int16_t x, int16_t y) {
    purr_input_event_t e = { .type = t };
    e.x = x; e.y = y;
    return purr_input_post(&e);
}

// ── Consume ───────────────────────────────────────────────────────────────────

// Non-blocking poll. Returns true if an event was available.
bool purr_input_poll(purr_input_event_t *out);

// Blocking wait up to timeout_ms. Returns true if event received before timeout.
bool purr_input_wait(purr_input_event_t *out, uint32_t timeout_ms);

// Drain all events (e.g. after modal dialog dismissal).
void purr_input_flush(void);

#ifdef __cplusplus
}
#endif
