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
#include <Arduino.h>
#include <SPIFFS.h>
#include <Preferences.h>

extern KITT kitt;
extern void  bridge_start();

// Memory warning thresholds (%)
static constexpr int MEM_WARN_90 = 90;
static constexpr int MEM_WARN_95 = 95;
static constexpr int MEM_WARN_98 = 98;
static int last_mem_warn = 0;

static Preferences prefs;

// Crash log
static void write_crash_log(const char* app, const char* reason) {
    File f = SPIFFS.open("/logs/crash.txt", "a");
    if (!f) return;
    f.printf("[%lu] %s: %s\n", millis(), app, reason);
    f.close();
}

// OTA: stage a firmware image and reboot into flasher mode
static void __attribute__((unused)) system_stage_ota(const char* src_path) {
    Serial.printf("[sys] OTA staged: %s\n", src_path);
    prefs.begin("kitt_boot", false);
    prefs.putBool("flash_flag", true);
    prefs.end();
    delay(500);
    esp_restart();
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

static void system_task(void*) {
    Serial.println("[sys] system.meow started");

    kitt.set_memory_warning_cb(on_memory_warning);
    kitt.set_crash_report_cb(on_crash_report);

    // Start the bridge (key mapping + radio handoff)
    bridge_start();

    // Scan apps and firmware
    kitt.apps_scan();
    kitt.firmware_scan();

    // ── Boot mode check: GPIO 0 held OR NVS flag → bootloader shell ─────────────
    bool bootloader_mode = false;
    {
        pinMode(0, INPUT_PULLUP);
        delay(10);
        if (digitalRead(0) == LOW) {
            Serial.println("[sys] GPIO 0 held — bootloader mode");
            bootloader_mode = true;
        }
    }
    if (!bootloader_mode) {
        prefs.begin("purr_boot", true);
        String bm    = prefs.getString("boot_mode", "");
        bool   blonly = prefs.getBool("bootloader_only", false);
        prefs.end();
        if (bm == "bootloader") {
            Serial.println("[sys] NVS boot_mode=bootloader");
            bootloader_mode = true;
            prefs.begin("purr_boot", false);
            prefs.remove("boot_mode");
            prefs.end();
        } else if (blonly) {
            Serial.println("[sys] NVS bootloader_only=true — PURR OS removed, staying in bootloader");
            bootloader_mode = true;
            // do NOT clear this flag — persists until PURR OS is reinstalled
        }
    }

    // ── Launch shell ──────────────────────────────────────────────────────────
    if (kitt.display_width() <= 128) {
        Serial.println("[sys] launching smol (OLED shell)");
#ifdef PURR_DISPLAY_SSD1306
        smol_start();
#endif
    } else if (bootloader_mode) {
        Serial.println("[sys] launching purr_bootloader (OTA flash mode)");
#ifdef PURR_HAS_BOOTLOADER
        purr_bootloader_start();
#else
        Serial.println("[sys] ERR: PURR_HAS_BOOTLOADER not compiled");
        kitt.text_print(0, "BOOTLOADER"); kitt.text_print(1, "not compiled");
#endif
    } else {
        Serial.println("[sys] launching explorer.paws");
#ifdef PURR_HAS_EXPLORER
        explorer_start();
#else
        Serial.println("[sys] ERR: PURR_HAS_EXPLORER not compiled");
        kitt.text_print(0, "PURR OS");
        kitt.text_print(1, kitt.device_name());
        kitt.text_print(2, "No shell");
#endif
    }

    // System watchdog loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        // Log current memory
        KITT::memory_stats_t mem;
        kitt.memory_get_stats(&mem);
        Serial.printf("[sys] RAM %lu/%lu KB free\n", mem.free_ram_kb, mem.total_ram_kb);
    }
}

void system_start() {
    xTaskCreatePinnedToCore(system_task, "system", 8192, nullptr, 3, nullptr, 1);
}
