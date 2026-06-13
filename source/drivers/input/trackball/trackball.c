// trackball.c — GPIO 4-direction trackball + click input driver
//
// Supports T-Deck and T-Deck Plus. Five active-low GPIO pins with internal
// pullups. Directions emit INPUT_EVENT_POINTER deltas; click emits KEY_DOWN/UP
// with keycode 0x0028 (Enter/OK). Held-direction acceleration kicks in after
// 200 ms.
//
// Register with kernel via purr_kernel_register_input() at init.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"

#include "../../../kernel/core/purr_module.h"
#include "../../../kernel/core/purr_kernel.h"
#include "../../../kernel/catcalls/catcall_input.h"

static const char *TAG = "trackball";

// ── Pin config ────────────────────────────────────────────────────────────────

#ifndef TRACKBALL_PIN_UP
#define TRACKBALL_PIN_UP    3
#endif
#ifndef TRACKBALL_PIN_DOWN
#define TRACKBALL_PIN_DOWN  15
#endif
#ifndef TRACKBALL_PIN_LEFT
#define TRACKBALL_PIN_LEFT  1
#endif
#ifndef TRACKBALL_PIN_RIGHT
#define TRACKBALL_PIN_RIGHT 2
#endif
#ifndef TRACKBALL_PIN_CLICK
#define TRACKBALL_PIN_CLICK 0
#endif

// Held-direction acceleration: after this many ms at delta ±1, jump to ±3
#define TRACKBALL_ACCEL_MS  200

// Event queue depth
#define EVENT_QUEUE_DEPTH   16

// ── State ─────────────────────────────────────────────────────────────────────

typedef enum { DIR_UP = 0, DIR_DOWN, DIR_LEFT, DIR_RIGHT, DIR_CLICK, DIR_COUNT } dir_t;

static const gpio_num_t s_pins[DIR_COUNT] = {
    TRACKBALL_PIN_UP,
    TRACKBALL_PIN_DOWN,
    TRACKBALL_PIN_LEFT,
    TRACKBALL_PIN_RIGHT,
    TRACKBALL_PIN_CLICK,
};

static bool        s_prev_state[DIR_COUNT]; // true = pressed (pin low)
static int64_t     s_hold_start[DIR_COUNT]; // esp_timer_get_time() when press began
static QueueHandle_t s_queue;

// ── Helpers ───────────────────────────────────────────────────────────────────

static inline bool pin_pressed(dir_t d)
{
    return gpio_get_level(s_pins[d]) == 0;
}

static void enqueue(const input_event_t *ev)
{
    // Drop if queue full — caller must drain regularly
    xQueueSend(s_queue, ev, 0);
}

// ── Catcall: init ─────────────────────────────────────────────────────────────

static esp_err_t trackball_init(void)
{
    s_queue = xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "queue alloc failed");
        return ESP_ERR_NO_MEM;
    }

    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << TRACKBALL_PIN_UP)    |
                        (1ULL << TRACKBALL_PIN_DOWN)   |
                        (1ULL << TRACKBALL_PIN_LEFT)   |
                        (1ULL << TRACKBALL_PIN_RIGHT)  |
                        (1ULL << TRACKBALL_PIN_CLICK),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    for (int i = 0; i < DIR_COUNT; i++) {
        s_prev_state[i] = false;
        s_hold_start[i] = 0;
    }

    ESP_LOGI(TAG, "init OK (UP=%d DN=%d LT=%d RT=%d CLK=%d)",
             TRACKBALL_PIN_UP, TRACKBALL_PIN_DOWN,
             TRACKBALL_PIN_LEFT, TRACKBALL_PIN_RIGHT,
             TRACKBALL_PIN_CLICK);
    return ESP_OK;
}

// ── Catcall: poll_event ───────────────────────────────────────────────────────
//
// Called by consumer (UI task, etc.) — non-blocking. First drains the internal
// queue built by update_state(), then synthesises movement events for directions
// still held so the caller gets smooth motion even without a separate poll task.

static void update_state(void)
{
    int64_t now = esp_timer_get_time(); // microseconds

    // Directions: UP/DOWN/LEFT/RIGHT
    static const dir_t dirs[4]    = { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };
    static const int8_t dx[4]     = {  0,  0, -1,  1 };
    static const int8_t dy[4]     = { -1,  1,  0,  0 };

    for (int i = 0; i < 4; i++) {
        dir_t d = dirs[i];
        bool pressed = pin_pressed(d);

        if (pressed && !s_prev_state[d]) {
            // Fresh press — record start time
            s_hold_start[d] = now;
        }

        if (pressed) {
            // Emit pointer event every call while held
            bool accel = (now - s_hold_start[d]) > (int64_t)(TRACKBALL_ACCEL_MS * 1000);
            int16_t step = accel ? 3 : 1;
            input_event_t ev = {
                .type    = INPUT_EVENT_POINTER,
                .keycode = 0,
                .delta_x = dx[i] * step,
                .delta_y = dy[i] * step,
                .modifiers = 0,
            };
            enqueue(&ev);
        }

        s_prev_state[d] = pressed;
    }

    // Click
    {
        bool pressed = pin_pressed(DIR_CLICK);
        if (pressed && !s_prev_state[DIR_CLICK]) {
            input_event_t ev = {
                .type      = INPUT_EVENT_KEY_DOWN,
                .keycode   = 0x0028,
                .delta_x   = 0,
                .delta_y   = 0,
                .modifiers = 0,
            };
            enqueue(&ev);
            s_hold_start[DIR_CLICK] = now;
        } else if (!pressed && s_prev_state[DIR_CLICK]) {
            input_event_t ev = {
                .type      = INPUT_EVENT_KEY_UP,
                .keycode   = 0x0028,
                .delta_x   = 0,
                .delta_y   = 0,
                .modifiers = 0,
            };
            enqueue(&ev);
        }
        s_prev_state[DIR_CLICK] = pressed;
    }
}

static bool trackball_poll_event(input_event_t *out)
{
    if (!s_queue || !out) return false;

    // Sample hardware and push into queue
    update_state();

    // Return one event to caller
    return xQueueReceive(s_queue, out, 0) == pdTRUE;
}

// ── Catcall: deinit ───────────────────────────────────────────────────────────

static esp_err_t trackball_deinit(void)
{
    if (s_queue) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    // Reset GPIO to defaults (high-Z input, no pullup)
    for (int i = 0; i < DIR_COUNT; i++) {
        gpio_reset_pin(s_pins[i]);
    }
    ESP_LOGI(TAG, "deinit OK");
    return ESP_OK;
}

// ── Catcall descriptor ────────────────────────────────────────────────────────

static const catcall_input_t s_catcall = {
    .name            = "trackball",
    .catcall_version = CATCALL_INPUT_VERSION,
    .init            = trackball_init,
    .poll_event      = trackball_poll_event,
    .deinit          = trackball_deinit,
};

// ── Module lifecycle ──────────────────────────────────────────────────────────

static int module_init(void)
{
    esp_err_t ret = trackball_init();
    if (ret != ESP_OK) return -1;
    purr_kernel_register_input(&s_catcall);
    return 0;
}

static void module_deinit(void)
{
    trackball_deinit();
}

// ── Module header ─────────────────────────────────────────────────────────────

purr_module_header_t purr_module = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_DRIVER,
    .load_priority     = PURR_PRIORITY_IMPORTANT,
    .name              = "trackball",
    .version           = "0.1.0",
    .kernel_min        = "0.9.0",
    .kernel_max        = "",
    .provided_catcalls = CATCALL_FLAG_INPUT,
    .required_catcalls = 0,
    .init              = module_init,
    .deinit            = module_deinit,
};
