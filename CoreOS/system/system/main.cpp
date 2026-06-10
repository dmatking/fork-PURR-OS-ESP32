// Sits between KITT and userland apps. Spawns the appropriate shell at boot.

#include "../kernel/kitt.h"
#if defined(PURR_DISPLAY_SSD1306) || defined(PURR_FORCE_KITTEN_UI)
#  include "../../apps/kitten_ui/kitten_ui.h"
#endif
#ifdef PURR_HAS_BOOTLOADER
#  include "../kernel/modules/purr_bootloader.h"
#endif
#ifdef PURR_HAS_EXPLORER
#  include "../kernel/modules/explorer.h"
#endif
#ifdef PURR_HAS_CLASSICMAC
#  include "../kernel/modules/classicmac.h"
#endif
#ifdef PURR_HAS_BLACKBERRY_UI
#  include "../kernel/modules/blackberry_ui.h"
#endif
#ifdef PURR_HAS_PARTITION_MGR
#  include "../kernel/modules/partition_manager.h"
#endif
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "sys";

extern KITT kitt;
extern void  bridge_start();

// Memory warning thresholds (%)
static constexpr int MEM_WARN_90 = 90;
static constexpr int MEM_WARN_95 = 95;
static constexpr int MEM_WARN_98 = 98;
static int last_mem_warn = 0;

// Crash log
static void write_crash_log(const char* app, const char* reason) {
    FILE* f = fopen("/spiffs/logs/crash.txt", "a");
    if (!f) return;
    fprintf(f, "[%lu] %s: %s\n", (unsigned long)(esp_timer_get_time() / 1000ULL), app, reason);
    fclose(f);
}

// Memory monitor callback (registered with KITT)
static void on_memory_warning(int pct) {
    if (pct >= MEM_WARN_98 && last_mem_warn < MEM_WARN_98) {
        ESP_LOGE(TAG, "MEM CRITICAL 98%");
        kitt.process_kill("explorer");
        last_mem_warn = pct;
    } else if (pct >= MEM_WARN_95 && last_mem_warn < MEM_WARN_95) {
        ESP_LOGW(TAG, "MEM WARN 95%");
        last_mem_warn = pct;
    } else if (pct >= MEM_WARN_90 && last_mem_warn < MEM_WARN_90) {
        ESP_LOGW(TAG, "MEM WARN 90%");
        last_mem_warn = pct;
    }
}

// Crash report callback
static void on_crash_report(const char* app, const char* reason) {
    ESP_LOGE(TAG, "crash: %s — %s", app, reason);
    write_crash_log(app, reason);
}

#ifdef PURR_IS_BOOTLOADER_IMG
// Returns true if ota_0 contains a PURR OS firmware image.
// Reads esp_app_desc_t directly from flash — no boot required.
static bool ota0_is_purr() {
    const esp_partition_t* p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!p) return false;
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(p, &desc) != ESP_OK) return false;
    return strcmp(desc.project_name, "purr_os_core") == 0;
}

// NVS namespace shared between factory kernel and ota_0 for crash-loop detection.
// Factory increments "boot_tries" before each chainload; ota_0 clears it on
// successful KITT init. If ota_0 crashes before clearing, the counter grows.
// At >= PURR_SOS_THRESHOLD consecutive failed boots, SOS mode is shown instead
// of auto-booting.
#define PURR_SOS_THRESHOLD 3
#define PURR_BL_NVS_NS     "purr_bl"
#define PURR_BL_NVS_KEY    "boot_tries"
#endif

static void system_task(void*) {
    ESP_LOGI(TAG, "system.meow started");

    kitt.set_memory_warning_cb(on_memory_warning);
    kitt.set_crash_report_cb(on_crash_report);

    bridge_start();

#ifdef PURR_IS_BOOTLOADER_IMG
    // ── Factory kernel boot decision ─────────────────────────────────────────
    // Priority order:
    //   1. GPIO 0 held at power-on → force bootloader UI
    //   2. Crash loop (boot_tries >= threshold) → SOS mode
    //   3. Valid PURR firmware in ota_0 → fast-path chainload
    //   4. Anything else → bootloader UI
    {
        pinMode(0, INPUT_PULLUP);
        delay(20);
        bool force_bl = (digitalRead(0) == LOW);

        bool is_purr = !force_bl && ota0_is_purr();

        // Read crash-loop counter from NVS
        nvs_flash_init();
        nvs_handle_t nvs;
        uint8_t boot_tries = 0;
        if (nvs_open(PURR_BL_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_get_u8(nvs, PURR_BL_NVS_KEY, &boot_tries);
        }

        bool sos_mode = is_purr && (boot_tries >= PURR_SOS_THRESHOLD);

        if (is_purr && !sos_mode) {
            nvs_set_u8(nvs, PURR_BL_NVS_KEY, (uint8_t)(boot_tries + 1));
            nvs_commit(nvs);
            nvs_close(nvs);
            Serial.printf("[sys] PURR firmware OK (attempt %u/%u) — chainloading\n",
                          boot_tries + 1, PURR_SOS_THRESHOLD);
            pm_launch(0);  // never returns
        }
        nvs_close(nvs);

        if (force_bl)
            Serial.println("[sys] GPIO 0 held — forcing bootloader UI");
        else if (sos_mode)
            Serial.printf("[sys] crash loop detected (%u failed boots) — SOS mode\n", boot_tries);
        else
            Serial.println("[sys] ota_0 is not PURR firmware — bootloader UI");

        purr_bootloader_start(sos_mode, boot_tries);
    }
#else
    // ── Full OS image (ota_0) ────────────────────────────────────────────────
    kitt.apps_scan();
    kitt.firmware_scan();

    // GPIO 0 held at boot → reboot to factory recovery instead of launching OS
#ifdef PURR_HAS_PARTITION_MGR
    {
        pinMode(0, INPUT_PULLUP);
        delay(10);
        if (digitalRead(0) == LOW) {
            Serial.println("[sys] GPIO 0 held — rebooting to factory");
            pm_boot_to_factory();  // never returns
        }
    }
#endif

    // ── Launch shell ─────────────────────────────────────────────────────────
#if defined(PURR_DISPLAY_SSD1306) || defined(PURR_FORCE_KITTEN_UI)
    ESP_LOGI("sys", "launching KittenUI");
    kitten_ui_start();
#else
    {
        ESP_LOGI("sys", "shells: TEMP disabled (linker symbol debug pending)");
        /* DISABLED PENDING LINKER DEBUG
#ifdef PURR_HAS_BLACKBERRY_UI
        Serial.println("[sys] registering BlackberryUI shell");
        blackberry_ui_start();
#endif
#ifdef PURR_HAS_EXPLORER
        Serial.println("[sys] registering Explorer shell");
        explorer_start();
#endif
#ifdef PURR_HAS_CLASSICMAC
        Serial.println("[sys] registering ClassicMac shell");
        classicmac_start();
#endif
#if !defined(PURR_HAS_BLACKBERRY_UI) && !defined(PURR_HAS_EXPLORER)
        Serial.println("[sys] ERR: no shell compiled");
        kitt.text_print(0, "PURR OS");
        kitt.text_print(1, kitt.device_name());
        kitt.text_print(2, "No shell");
#endif
        purr_wm_start();
        */
    }

    // System watchdog loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        KITT::memory_stats_t mem;
        kitt.memory_get_stats(&mem);
        Serial.printf("[sys] RAM %lu/%lu KB free\n", mem.free_ram_kb, mem.total_ram_kb);
    }
#endif  // PURR_DISPLAY_SSD1306 || PURR_FORCE_KITTEN_UI
#endif  // PURR_IS_BOOTLOADER_IMG
}

void system_start() {
    xTaskCreatePinnedToCore(system_task, "system", 8192, nullptr, 3, nullptr, 1);
}
