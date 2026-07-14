#pragma once
#include "esp_err.h"

// Public surface for kernel_tab5_boot.c calling this directly before the
// module loader runs — same "baked-in Layer 0" pattern as gt911.h/
// st7789.h on T-Deck Plus. One physical BSP object (espp::M5StackTab5)
// answers both the display and touch catcalls, since the espp component's
// own ILI9881C-vs-ST7123 auto-detect lives inside that one object either
// way — this header/its .cpp is the only place that knows that.

// Brings up display (MIPI-DSI panel, auto-detecting ILI9881C vs ST7123
// hardware revision), touch, and registers both catcall_display_t and
// catcall_touch_t. Returns 0 on success. SD card init is separate (see
// m5tab5_bsp_sdcard_init()) since a device may want SD up before or after
// UI-relevant drivers depending on boot ordering, same as every other
// specialized kernel's own phase split.
int m5tab5_bsp_drv_init(void);
void m5tab5_bsp_drv_deinit(void);

// SD card (SDMMC 4-bit mode) — mounts at /sdcard. Returns 0 on success.
// Separate from m5tab5_bsp_drv_init() so kernel_tab5_boot.c can order it
// relative to purr_kernel_set_sd_available()/ensure_sd_dirs() the same way
// every other specialized kernel's own phase-0 SD mount does.
int m5tab5_bsp_sdcard_init(void);
