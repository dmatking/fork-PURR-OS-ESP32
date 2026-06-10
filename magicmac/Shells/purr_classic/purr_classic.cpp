#include "purr_classic.h"
#include "umac_core.h"
#include "purr_ipc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Display HAL for rendering
extern void mw_hal_lcd_monochrome_bitmap_clip(
    int16_t image_start_x, int16_t image_start_y,
    uint16_t bitmap_width, uint16_t bitmap_height,
    int16_t clip_start_x, int16_t clip_start_y,
    int16_t clip_width, int16_t clip_height,
    uint32_t fg_colour, uint32_t bg_colour,
    const uint8_t *image_data);

extern int16_t mw_hal_lcd_get_display_width(void);
extern int16_t mw_hal_lcd_get_display_height(void);

static const char *TAG = "purr_classic";

// ---------------------------------------------------------------------------
// Frame callback — drv_umac calls this each time the Mac framebuffer changes.
// Mac Plus: 512x342 1bpp (monochrome, black-on-white)
// Render directly using display HAL monochrome bitmap function
// ---------------------------------------------------------------------------
static void _on_frame(const uint8_t *buf, int w, int h)
{
    if (!buf || w == 0 || h == 0) return;

    // Get display dimensions
    int16_t disp_w = mw_hal_lcd_get_display_width();  // T-Deck: 320 (landscape)
    int16_t disp_h = mw_hal_lcd_get_display_height(); // T-Deck: 240 (landscape)

    // Mac framebuffer: 512x342 → Display: 320x240
    // Crop Mac center to fit display (leave some sides off-screen)
    // Offset to center: ((320-512)/2, (240-342)/2) = (-96, -51)
    int16_t offset_x = (disp_w - w) / 2;   // -96 (shows center 320px of 512px)
    int16_t offset_y = (disp_h - h) / 2;   // -51 (shows center 240px of 342px)

    // Mac framebuffer is stored as 1bpp, black-on-white
    // Set foreground (pixels set to 1) = black (0x000000)
    // Set background (pixels set to 0) = white (0xFFFFFF)
    mw_hal_lcd_monochrome_bitmap_clip(
        offset_x,           // image_start_x
        offset_y,           // image_start_y
        w,                  // bitmap_width (512)
        h,                  // bitmap_height (342)
        0,                  // clip_start_x
        0,                  // clip_start_y
        disp_w,             // clip_width
        disp_h,             // clip_height
        0x000000,           // fg_colour = black
        0xFFFFFF,           // bg_colour = white
        buf                 // image_data
    );
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
