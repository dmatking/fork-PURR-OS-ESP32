// Sits between KITT and userland apps. Spawns the appropriate shell at boot.

#include "../kernel/kitt.h"
#ifdef PURR_DISPLAY_SSD1306
#  include "../../apps/smol/smol.h"
#endif
#ifdef PURR_HAS_BOOTLOADER
#  include "../kernel/modules/purr_bootloader.h"
#endif
#ifdef PURR_HAS_EXPLORER
#  include "../kernel/modules/explorer.h"
#endif
#ifdef PURR_HAS_BLACKBERRY_UI
#  include "../kernel/modules/blackberry_ui.h"
#endif
#ifdef PURR_HAS_PARTITION_MGR
#  include "../kernel/modules/partition_manager.h"
#endif
#include <Arduino.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_app_desc.h>

extern KITT kitt;
extern void  bridge_start();

// Memory warning thresholds (%)
static constexpr int MEM_WARN_90 = 90;
static constexpr int MEM_WARN_95 = 95;
static constexpr int MEM_WARN_98 = 98;
static int last_mem_warn = 0;

// Crash log
static void write_crash_log(const char* app, const char* reason) {
    File f = SPIFFS.open("/logs/crash.txt", "a");
    if (!f) return;
    f.printf("[%lu] %s: %s\n", millis(), app, reason);
    f.close();
}

// Memory monitor callback (registered with KITT)
static void on_memory_warning(int pct) {
    if (pct >= MEM_WARN_98 && last_mem_warn < MEM_WARN_98) {
        Serial.println("[sys] MEM CRITICAL 98%");
        kitt.process_kill("explorer");
        last_mem_warn = pct;
    } else if (pct >= MEM_WARN_95 && last_mem_warn < MEM_WARN_95) {
        Serial.println("[sys] MEM WARN 95%");
        last_mem_warn = pct;
    } else if (pct >= MEM_WARN_90 && last_mem_warn < MEM_WARN_90) {
        Serial.println("[sys] MEM WARN 90%");
        last_mem_warn = pct;
    }
}

// Crash report callback
static void on_crash_report(const char* app, const char* reason) {
    Serial.printf("[sys] crash: %s — %s\n", app, reason);
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
    Serial.println("[sys] system.meow started");

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

        // Read crash-loop counter
        Preferences bl_prefs;
        bl_prefs.begin(PURR_BL_NVS_NS, false);
        uint8_t boot_tries = bl_prefs.getUChar(PURR_BL_NVS_KEY, 0);

        bool sos_mode = is_purr && (boot_tries >= PURR_SOS_THRESHOLD);

        if (is_purr && !sos_mode) {
            // Increment before chainloading — cleared by ota_0 on successful init
            bl_prefs.putUChar(PURR_BL_NVS_KEY, (uint8_t)(boot_tries + 1));
            bl_prefs.end();
            Serial.printf("[sys] PURR firmware OK (attempt %u/%u) — chainloading\n",
                          boot_tries + 1, PURR_SOS_THRESHOLD);
            pm_launch(0);  // never returns
        }
        bl_prefs.end();

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
    if (kitt.display_width() <= 128) {
        Serial.println("[sys] launching smol (OLED shell)");
#ifdef PURR_DISPLAY_SSD1306
        smol_start();
#endif
    } else {
#ifdef PURR_HAS_BLACKBERRY_UI
        Serial.println("[sys] launching BlackberryUI");
        blackberry_ui_start();
#elif defined(PURR_HAS_EXPLORER)
        Serial.println("[sys] launching explorer.paws");
        explorer_start();
#else
        Serial.println("[sys] ERR: no shell compiled");
        kitt.text_print(0, "PURR OS");
        kitt.text_print(1, kitt.device_name());
        kitt.text_print(2, "No shell");
#endif
    }

    // System watchdog loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        KITT::memory_stats_t mem;
        kitt.memory_get_stats(&mem);
        Serial.printf("[sys] RAM %lu/%lu KB free\n", mem.free_ram_kb, mem.total_ram_kb);
    }
#endif  // PURR_IS_BOOTLOADER_IMG
}

void system_start() {
    xTaskCreatePinnedToCore(system_task, "system", 8192, nullptr, 3, nullptr, 1);
}
