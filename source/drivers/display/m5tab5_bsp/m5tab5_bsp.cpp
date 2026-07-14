// m5tab5_bsp.cpp — M5Stack Tab5 display+touch+SD, wrapping the real
// hardware-tested espp/m5stack-tab5 BSP instead of hand-transcribing
// MIPI-DSI panel timing / ILI9881C-vs-ST7123 init sequences by hand.
// Same "C++ library wrapped behind a plain-C catcall" shape already
// proven in this codebase by sx1262_rl.cpp (RadioLib) — the vendored/
// managed library owns the hardware-specific bring-up, this file only
// adapts its API surface to catcall_display_t/catcall_touch_t.
//
// espp::M5StackTab5 is a singleton (::get()) that internally auto-detects
// which display revision is actually present (DisplayController::ILI9881
// vs ::ST7123) via detect_display_controller() — both drivers are compiled
// into this one component, and which one actually talks to the panel is
// decided at runtime, not at build time. See device.pcat's own comment
// for the IO-expander (PI4IOE5V6408 @ 0x43) reset-line wiring this
// depends on — handled internally by espp, not duplicated here.
//
// Phase 1 scope: display + touch + SD only. No physical Tab5 in hand yet
// this pass — build-verified against the real espp API surface, not
// hardware-tested. Exact method names/signatures below were confirmed
// against the actual espp/m5stack-tab5 v1.0.35 header (not guessed), but
// this file has not been run on real hardware.

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "driver/ledc.h"
#include "../../../kernel/core/purr_kernel.h"
#include "../../../kernel/catcalls/catcall_display.h"
#include "../../../kernel/catcalls/catcall_touch.h"
#include "m5tab5_bsp.h"
#include <string.h>
#include <stdio.h>
}

#include "m5stack-tab5.hpp"

static const char *TAG = "m5tab5_bsp";

#define PANEL_WIDTH  720
#define PANEL_HEIGHT 1280

// ── Backlight (direct LEDC PWM on GPIO22 — not routed through espp) ────────
// Confirmed as a plain GPIO, not behind the PI4IOE5V6408 IO-expander (only
// LCD_RST/TP_RST/SPK_EN live there) — simple enough to drive directly
// rather than depend on an unconfirmed espp backlight accessor.
#define BACKLIGHT_GPIO   22
#define BACKLIGHT_LEDC_CH  LEDC_CHANNEL_0
#define BACKLIGHT_LEDC_TIMER LEDC_TIMER_0

static bool s_backlight_ready = false;

static void backlight_init(void) {
    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode      = LEDC_LOW_SPEED_MODE;
    timer_cfg.timer_num       = BACKLIGHT_LEDC_TIMER;
    timer_cfg.duty_resolution = LEDC_TIMER_8_BIT;
    timer_cfg.freq_hz         = 5000;
    timer_cfg.clk_cfg         = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer_cfg) != ESP_OK) return;

    ledc_channel_config_t ch_cfg = {};
    ch_cfg.gpio_num   = BACKLIGHT_GPIO;
    ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_cfg.channel    = BACKLIGHT_LEDC_CH;
    ch_cfg.timer_sel  = BACKLIGHT_LEDC_TIMER;
    ch_cfg.duty       = 255;   // full brightness by default
    ch_cfg.hpoint     = 0;
    if (ledc_channel_config(&ch_cfg) != ESP_OK) return;

    s_backlight_ready = true;
}

static esp_err_t m5tab5_set_brightness(uint8_t level) {
    if (!s_backlight_ready) return ESP_ERR_INVALID_STATE;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CH, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CH);
    return ESP_OK;
}

// ── Shared BSP state ────────────────────────────────────────────────────

static bool s_ready = false;

// ── catcall_display_t ───────────────────────────────────────────────────

static esp_err_t m5tab5_display_init(const display_config_t *cfg) {
    (void)cfg;
    // Real bring-up already happened in m5tab5_bsp_drv_init() (baked-in,
    // called directly by kernel_tab5_boot.c before this catcall is even
    // registered) — matches st7789.c/gt911.c's own pattern for Layer 0
    // drivers on T-Deck Plus, where the catcall's .init is a formality,
    // not the real trigger.
    return s_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static esp_err_t m5tab5_push_pixels(int x, int y, int w, int h, const uint16_t *data) {
    if (!s_ready || !data || w <= 0 || h <= 0) return ESP_ERR_INVALID_STATE;
    // write_lcd_lines()'s (xe, ye) are exclusive end coordinates — matches
    // the underlying esp_lcd_panel_draw_bitmap() convention it wraps.
    espp::M5StackTab5::get().write_lcd_lines(x, y, x + w, y + h,
                                              reinterpret_cast<const uint8_t *>(data), 0);
    return ESP_OK;
}

static esp_err_t m5tab5_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (!s_ready || w <= 0 || h <= 0) return ESP_ERR_INVALID_STATE;
    // One row's worth of solid color, reused for every line — avoids a
    // full w*h allocation for a large fill (up to 720*1280 on this panel).
    uint16_t *line = (uint16_t *)heap_caps_malloc((size_t)w * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!line) return ESP_ERR_NO_MEM;
    for (int i = 0; i < w; i++) line[i] = color;

    for (int row = 0; row < h; row++) {
        espp::M5StackTab5::get().write_lcd_lines(x, y + row, x + w, y + row + 1,
                                                  reinterpret_cast<const uint8_t *>(line), 0);
    }
    heap_caps_free(line);
    return ESP_OK;
}

static void m5tab5_get_info(display_info_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->width          = PANEL_WIDTH;
    out->height          = PANEL_HEIGHT;
    out->bits_per_pixel = 16;
    snprintf(out->name, sizeof(out->name), "m5tab5_bsp");
}

static esp_err_t m5tab5_display_deinit(void) {
    return ESP_OK;
}

static const catcall_display_t s_display_catcall = {
    .name            = "m5tab5_bsp",
    .catcall_version = CATCALL_DISPLAY_VERSION,
    .init            = m5tab5_display_init,
    .push_pixels     = m5tab5_push_pixels,
    .fill_rect       = m5tab5_fill_rect,
    .set_brightness  = m5tab5_set_brightness,
    .get_info        = m5tab5_get_info,
    .deinit          = m5tab5_display_deinit,
};

// ── catcall_touch_t ──────────────────────────────────────────────────────

static esp_err_t m5tab5_touch_init(const touch_config_t *cfg) {
    (void)cfg;
    return s_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static bool m5tab5_read_point(uint16_t *x, uint16_t *y) {
    if (!s_ready || !x || !y) return false;
    uint8_t num_points = 0, btn_state = 0;
    espp::M5StackTab5::get().touchpad_read(&num_points, x, y, &btn_state);
    return num_points > 0;
}

static bool m5tab5_is_pressed(void) {
    if (!s_ready) return false;
    uint16_t x = 0, y = 0;
    uint8_t num_points = 0, btn_state = 0;
    espp::M5StackTab5::get().touchpad_read(&num_points, &x, &y, &btn_state);
    return num_points > 0;
}

static esp_err_t m5tab5_touch_deinit(void) {
    return ESP_OK;
}

static const catcall_touch_t s_touch_catcall = {
    .name            = "m5tab5_bsp",
    .catcall_version = CATCALL_TOUCH_VERSION,
    .init            = m5tab5_touch_init,
    .read_point      = m5tab5_read_point,
    .is_pressed      = m5tab5_is_pressed,
    .deinit          = m5tab5_touch_deinit,
};

// ── Lifecycle ────────────────────────────────────────────────────────────

extern "C" int m5tab5_bsp_drv_init(void) {
    auto &bsp = espp::M5StackTab5::get();

    if (!bsp.initialize_lcd()) {
        ESP_LOGE(TAG, "initialize_lcd() failed");
        return -1;
    }
    if (!bsp.initialize_display(PANEL_WIDTH * PANEL_HEIGHT / 10)) {
        ESP_LOGE(TAG, "initialize_display() failed");
        return -1;
    }

    auto controller = bsp.detect_display_controller();
    ESP_LOGI(TAG, "display controller detected: %s", bsp.get_display_controller_name());
    if (controller == espp::M5StackTab5::DisplayController::UNKNOWN) {
        ESP_LOGW(TAG, "display controller unrecognized — continuing, but this is unexpected");
    }

    if (!bsp.initialize_touch()) {
        ESP_LOGW(TAG, "initialize_touch() failed — continuing without touch");
    }

    backlight_init();
    if (s_backlight_ready) m5tab5_set_brightness(255);

    s_ready = true;
    purr_kernel_register_display(&s_display_catcall);
    purr_kernel_register_touch(&s_touch_catcall);
    ESP_LOGI(TAG, "ready (%dx%d)", PANEL_WIDTH, PANEL_HEIGHT);
    return 0;
}

extern "C" void m5tab5_bsp_drv_deinit(void) {
    s_ready = false;
}

extern "C" int m5tab5_bsp_sdcard_init(void) {
    espp::M5StackTab5::SdCardConfig sd_cfg{};
    sd_cfg.format_if_mount_failed = false;
    sd_cfg.max_files              = 5;
    sd_cfg.allocation_unit_size   = 16 * 1024;

    if (!espp::M5StackTab5::get().initialize_sdcard(sd_cfg)) {
        ESP_LOGW(TAG, "SD card init failed — continuing without SD");
        return -1;
    }
    if (!espp::M5StackTab5::get().is_sd_card_available()) {
        ESP_LOGW(TAG, "SD card not available after init");
        return -1;
    }
    ESP_LOGI(TAG, "SD card ready");
    return 0;
}
