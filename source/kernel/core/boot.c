// boot.c — PURR OS kernel boot entry
//
// Boot sequence:
//   1. NVS init
//   2. Mount flash VFS at /flash  (SPIFFS partition — contains core .purr blobs)
//   3. Mount SD VFS at /sdcard    (FAT — optional, used as fallback + user content)
//   4. Scan /flash/modules + /flash/drivers in priority order (P1→P2→P3)
//      — P1 (REQUIRED): panic if missing from both flash and SD
//      — P2 (IMPORTANT): warn if missing, continue
//      — P3 (OPTIONAL): silent skip
//   5. Scan /sdcard/modules + /sdcard/drivers for extras not on flash
//   6. Kernel spine parks in idle loop — everything else runs in module tasks

#include "purr_kernel.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "purr_boot";

// ── Flash VFS (SPIFFS) ────────────────────────────────────────────────────────

static void mount_flash_vfs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/flash",
        .partition_label        = NULL,   // uses first SPIFFS partition in table
        .max_files              = 12,
        .format_if_mount_failed = false,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed (%s) — no flash VFS", esp_err_to_name(ret));
    } else {
        size_t total = 0, used = 0;
        esp_spiffs_info(NULL, &total, &used);
        ESP_LOGI(TAG, "flash VFS: %u KB used / %u KB total",
                 (unsigned)(used / 1024), (unsigned)(total / 1024));
    }
}

// ── SD card VFS (FAT) ────────────────────────────────────────────────────────
//
// SD is always optional. If not present (no card, no SPI config), boot continues
// without it. The SD SPI bus/pins are configured at build time from device.pcat.
// If SD mounts, the kernel sets purr_kernel_set_sd_available(true) so modules
// can check before trying to open /sdcard paths.

static void mount_sd_vfs(void)
{
    // The SD driver module (drv_sd) handles hardware init and mounts the
    // FAT filesystem via esp_vfs_fat_sdspi_mount. When it succeeds it calls
    // purr_kernel_set_sd_available(true).
    //
    // boot.c doesn't touch SPI or pins directly — that's a driver concern.
    // We just note whether /sdcard is usable after modules load.
    ESP_LOGI(TAG, "SD mount delegated to drv_sd module");
}

// ── Ensure standard SD directory layout ──────────────────────────────────────

static void ensure_sd_dirs(void)
{
    // Only attempt if SD is mounted
    if (!purr_kernel_sd_available()) return;

    const char *dirs[] = {
        "/sdcard/apps",           // user apps (.meow, .paws, .claw)
        "/sdcard/modules",        // kernel module fallbacks (.purr)
        "/sdcard/drivers",        // driver fallbacks (.purr, flat or typed)
        "/sdcard/drivers/display",
        "/sdcard/drivers/touch",
        "/sdcard/drivers/input",
        "/sdcard/drivers/radio",
        "/sdcard/drivers/gps",
        "/sdcard/magicmac",       // MagicMac ROM + config
        "/sdcard/magidos",        // MagiDOS disk images + config
        "/sdcard/system",         // system files
        "/sdcard/system/logs",    // kernel + module logs
        NULL
    };

    for (int i = 0; dirs[i]; i++) {
        struct stat st;
        if (stat(dirs[i], &st) != 0) {
            mkdir(dirs[i], 0755);
        }
    }
}

// ── app_main ──────────────────────────────────────────────────────────────────

void app_main(void)
{
    ESP_LOGI(TAG, "PURR OS %s / KITT %s  booting...", PURR_KERNEL_VERSION, KITT_VERSION);

    // NVS for WiFi + settings persistence
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    mount_flash_vfs();
    mount_sd_vfs();
    ensure_sd_dirs();

    // ── Phase 1: load all built-in modules from the .purr_modules section ──────
    //
    // All drivers and system modules compiled into the firmware are registered
    // via PURR_MODULE_REGISTER() and land in the .purr_modules linker section.
    // The kernel iterates that section sorted by priority (P1→P2→P3).
    // P1 modules that fail to init trigger purr_kernel_panic().
    ESP_LOGI(TAG, "=== phase 1: static modules ===");
    purr_kernel_load_static_modules();

    // ── Phase 2: SD extras (optional modules not compiled in) ─────────────────
    //
    // User-supplied .purr blobs on SD card. Always treated as OPTIONAL
    // regardless of their header priority — third-party or user-added modules.
    if (purr_kernel_sd_available()) {
        ESP_LOGI(TAG, "=== phase 2: SD extras ===");
        purr_kernel_scan_modules("/sdcard/modules", NULL);
        purr_kernel_scan_modules("/sdcard/drivers", NULL);
    }

    // Sanity checks — warn if critical modules never registered catcalls
    if (!purr_kernel_display()) {
        ESP_LOGW(TAG, "no display catcall registered after module load");
    }
    if (!purr_kernel_get_module("app_manager")) {
        ESP_LOGW(TAG, "app_manager not loaded — no apps will run");
    }

    ESP_LOGI(TAG, "boot complete — %u bytes free", (unsigned)purr_kernel_free_ram());

    // Kernel spine parks here. All work happens in module FreeRTOS tasks.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
