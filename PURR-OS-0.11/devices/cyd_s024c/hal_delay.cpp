#include "hal/hal_delay.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rom/ets_sys.h>

extern "C" {

void mw_hal_delay_init(void) {}

void mw_hal_delay_ms(uint16_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void mw_hal_delay_us(uint32_t us) {
    ets_delay_us(us);
}

} // extern "C"
