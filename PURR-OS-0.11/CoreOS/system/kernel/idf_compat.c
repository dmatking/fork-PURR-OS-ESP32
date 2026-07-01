#include "esp_idf_version.h"
#include <stdint.h>

/* arduino-esp32 3.x calls esp_timer_impl_update_apb_freq() after a CPU
   frequency change. IDF 5.x removed this private function and handles APB
   updates internally. Stub it to keep the linker happy. */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void esp_timer_impl_update_apb_freq(uint32_t apb_ticks_per_us) {
    (void)apb_ticks_per_us;
}
#endif
