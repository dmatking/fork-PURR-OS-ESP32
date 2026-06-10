#include "purr_classic.h"
#include "umac_core.h"
#include "purr_ipc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "purr_classic";

// ---------------------------------------------------------------------------
// Frame callback — drv_umac calls this each time the Mac framebuffer changes.
// Converts the 1-bit Mac framebuffer to the display's native format and blits.
// Mac Plus native: 512x342 1bpp. CYD display: 240x320 16bpp (ILI9341).
// We scale down and invert (Mac is black-on-white).
// ---------------------------------------------------------------------------
static void _on_frame(const uint8_t *buf, int w, int h)
{
    // TODO: 1bpp → RGB565 conversion + scale to 240x320
    // For now, forward raw buffer pointer to display driver for stub testing
    (void)buf; (void)w; (void)h;
}

// ---------------------------------------------------------------------------
// Touch callback — wired from drv_touch interrupt to Mac ADB mouse
// ---------------------------------------------------------------------------
static void _on_touch(int x, int y, bool pressed)
{
    umac_mouse_event(x, y, pressed);
}

// ---------------------------------------------------------------------------
// Shell entry point
// ---------------------------------------------------------------------------
void purr_classic_start(void)
{
    ESP_LOGI(TAG, "starting MagicMac shell");

    // Init IPC bridge (must come before umac so the window is registered)
    purr_ipc_init();

    // Init emulator with ROM from SD card (/sdcard/magicmac/mac.rom)
    // Falls back to SPIFFS if SD card ROM not found
    const char *rom_path = "/sdcard/magicmac/mac.rom";
    FILE *f = fopen(rom_path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "SD card ROM not found, trying SPIFFS fallback");
        rom_path = UMAC_ROM_PATH;  // Falls back to /spiffs/mac.rom
    } else {
        fclose(f);
    }

    umac_config_t cfg = {
        .rom_path = rom_path,
        .ram_size = UMAC_RAM_SIZE,
        .verbose  = false,
    };
    if (umac_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "umac init failed — ROM missing from %s", rom_path);
        return;
    }

    ESP_LOGI(TAG, "umac initialized with ROM from %s", rom_path);

    // Wire frame output
    umac_set_frame_callback(_on_frame);

    // Wire touch (drv_touch callback registration — adjust to actual API)
    // touch_set_callback(_on_touch);

    // Launch IPC dispatch task on core 1 (same as system_task, low priority)
    xTaskCreatePinnedToCore(purr_ipc_task, "purr_ipc", 4096, NULL, 1, NULL, 1);

    // Launch emulator — runs on core 0, does not return
    umac_start();
}
