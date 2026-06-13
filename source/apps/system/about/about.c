// about.c — PURR OS about screen (.claw)
// Uses purr_win.h — compatible with KittenUI (LVGL) and MiniWin.
// Shows: OS version, device chip, free RAM, uptime, active drivers.

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "purr_win.h"
#include "purr_kernel.h"
#include "purr_module.h"

static purr_win_t s_win   = 0;
static purr_wid_t s_out   = 0;   // scrollable textarea holding all info
static TaskHandle_t s_updater = NULL;
static bool s_running = false;

// ── Info builder ──────────────────────────────────────────────────────────────

static void build_info(char *buf, size_t sz) {
    size_t pos = 0;

#define APPEND(fmt, ...) \
    do { pos += snprintf(buf + pos, sz - pos, fmt "\n", ##__VA_ARGS__); } while(0)

    APPEND("╔══════════════════════╗");
    APPEND("║     PURR OS v%s    ║", PURR_KERNEL_VERSION);
    APPEND("║     KITT v%s       ║", KITT_VERSION);
    APPEND("╚══════════════════════╝");
    APPEND("");

    // Chip info
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const char *model = "ESP32";
    if      (chip.model == CHIP_ESP32S3) model = "ESP32-S3";
    else if (chip.model == CHIP_ESP32S2) model = "ESP32-S2";
    else if (chip.model == CHIP_ESP32C3) model = "ESP32-C3";
    APPEND("Chip:    %s r%d  %d cores", model, chip.revision, chip.cores);

    // Flash size
    uint32_t flash_sz = 0;
    esp_flash_get_size(NULL, &flash_sz);
    APPEND("Flash:   %lu MB", (unsigned long)(flash_sz / (1024 * 1024)));

    // RAM
    uint32_t free_ram = purr_kernel_free_ram();
    APPEND("Free RAM:%lu KB", (unsigned long)(free_ram / 1024));

    // Uptime
    uint64_t up_s = purr_kernel_uptime_ms() / 1000ULL;
    uint32_t h = (uint32_t)(up_s / 3600);
    uint32_t m = (uint32_t)((up_s % 3600) / 60);
    uint32_t s = (uint32_t)(up_s % 60);
    APPEND("Uptime:  %02luh %02lum %02lus",
           (unsigned long)h, (unsigned long)m, (unsigned long)s);

    APPEND("");
    APPEND("── Drivers ──");

    // Active catcall registrations
    const catcall_display_t *disp = purr_kernel_display();
    APPEND("Display: %s", disp ? disp->name : "none");

    const catcall_touch_t *touch = purr_kernel_touch();
    APPEND("Touch:   %s", touch ? touch->name : "none");

    const catcall_input_t *input = purr_kernel_input();
    APPEND("Input:   %s", input ? input->name : "none");

    const catcall_radio_t *radio = purr_kernel_radio();
    APPEND("Radio:   %s", radio ? radio->name : "none");

    const catcall_gps_t *gps = purr_kernel_gps();
    APPEND("GPS:     %s", gps ? gps->name : "none");

    const catcall_ui_t *ui = purr_kernel_ui();
    APPEND("UI:      %s", ui ? ui->name : "none");

    APPEND("");
    APPEND("SD card: %s", purr_kernel_sd_available() ? "mounted" : "not mounted");

    APPEND("");
    APPEND("PURR OS — meow.");

#undef APPEND
}

// ── Refresh task ──────────────────────────────────────────────────────────────
// Updates uptime/RAM every 5 seconds while app is open.

static void updater_task(void *arg) {
    (void)arg;
    char buf[1024];
    while (s_running) {
        build_info(buf, sizeof(buf));
        purr_win_textarea_set(s_out, buf);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    vTaskDelete(NULL);
}

// ── Close button ─────────────────────────────────────────────────────────────

static void on_close(purr_wid_t w, purr_event_t e, void *u) {
    (void)w; (void)e; (void)u;
    purr_win_hide(s_win);
}

// ── App lifecycle ─────────────────────────────────────────────────────────────

static int about_init(void) {
    s_win = purr_win_create("About PURR OS");

    s_out = purr_win_textarea(s_win, 100, 85);

    purr_wid_t row = purr_win_row(s_win, 4);
    purr_win_button(s_win, "Close", on_close, NULL);
    purr_win_layout_end(row);

    // Initial fill
    char buf[1024];
    build_info(buf, sizeof(buf));
    purr_win_textarea_set(s_out, buf);

    purr_win_show(s_win);

    s_running = true;
    xTaskCreate(updater_task, "about_upd", 2048, NULL, 3, &s_updater);
    return 0;
}

static void about_deinit(void) {
    s_running = false;
    if (s_updater) {
        vTaskDelay(pdMS_TO_TICKS(200));
        s_updater = NULL;
    }
    purr_win_destroy(s_win);
    s_win = 0;
}

// ── Module header ─────────────────────────────────────────────────────────────

PURR_MODULE_REGISTER(about) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_APP,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "about",
    .version           = "1.0.0",
    .kernel_min        = "0.9.0",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = about_init,
    .deinit            = about_deinit,
};
