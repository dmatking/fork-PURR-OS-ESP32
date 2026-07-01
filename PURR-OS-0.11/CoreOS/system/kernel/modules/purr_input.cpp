// purr_input.cpp — unified PURR OS input event queue (FreeRTOS queue wrapper)

#include "purr_input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "input";
static QueueHandle_t s_queue = NULL;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

void purr_input_init(uint32_t queue_depth) {
    if (s_queue) return;
    s_queue = xQueueCreate((UBaseType_t)queue_depth, sizeof(purr_input_event_t));
    if (!s_queue) ESP_LOGE(TAG, "queue create failed");
}

bool purr_input_post(const purr_input_event_t *evt) {
    if (!s_queue || !evt) return false;
    purr_input_event_t e = *evt;
    if (e.timestamp_ms == 0) e.timestamp_ms = now_ms();
    return xQueueSend(s_queue, &e, 0) == pdTRUE;
}

bool purr_input_post_from_isr(const purr_input_event_t *evt) {
    if (!s_queue || !evt) return false;
    BaseType_t woken = pdFALSE;
    bool ok = xQueueSendFromISR(s_queue, evt, &woken) == pdTRUE;
    portYIELD_FROM_ISR(woken);
    return ok;
}

bool purr_input_poll(purr_input_event_t *out) {
    if (!s_queue || !out) return false;
    return xQueueReceive(s_queue, out, 0) == pdTRUE;
}

bool purr_input_wait(purr_input_event_t *out, uint32_t timeout_ms) {
    if (!s_queue || !out) return false;
    TickType_t ticks = (timeout_ms == UINT32_MAX)
                       ? portMAX_DELAY
                       : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(s_queue, out, ticks) == pdTRUE;
}

void purr_input_flush(void) {
    if (!s_queue) return;
    xQueueReset(s_queue);
}
