// MiniWin non-volatile storage — stores touch calibration data in NVS.
// Key: "mw_cal" under namespace "miniwin".

#include "hal/hal_non_vol.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

extern "C" {

static const char* NVS_NS  = "miniwin";
static const char* NVS_KEY = "mw_cal";

void mw_hal_non_vol_init(void) {
    // NVS is initialised by PURR OS before MiniWin starts — nothing extra needed here
}

void mw_hal_non_vol_load(uint8_t *data, uint16_t size) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = size;
    nvs_get_blob(h, NVS_KEY, data, &len);
    nvs_close(h);
}

void mw_hal_non_vol_save(uint8_t *data, uint16_t size) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY, data, size);
    nvs_commit(h);
    nvs_close(h);
}

} // extern "C"
