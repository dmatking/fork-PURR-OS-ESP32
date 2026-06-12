#include <stdint.h>
#include "hal/hal_timer.h"
#include "esp_timer.h"

extern "C" {

volatile uint32_t mw_tick_counter = 0;

static esp_timer_handle_t s_timer = nullptr;

static void _tick_cb(void*) {
    mw_tick_counter = mw_tick_counter + 1;
}

void mw_hal_timer_init(void) {
    const esp_timer_create_args_t args = {
        .callback        = _tick_cb,
        .arg             = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "mw_tick",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &s_timer);
    esp_timer_start_periodic(s_timer, 20000ULL);
}

void mw_hal_timer_fired(void) {}

} // extern "C"
