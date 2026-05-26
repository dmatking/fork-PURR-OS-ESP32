// system.meow — app lifecycle manager, memory monitor, OTA handoff broker.
// Sits between KITT and userland apps. Spawns explorer.meow at boot.

#include "../kernel/kitt.h"
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
static void system_stage_ota(const char* src_path) {
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

    // Launch explorer as the default shell
    // Chooses smol.meow on small displays, explorer.meow on large ones
    const char* shell = (kitt.display_width() <= 128)
        ? "/apps/smol.meow"
        : "/apps/explorer.meow";

    Serial.printf("[sys] launching shell: %s\n", shell);
    if (!kitt.app_launch(shell)) {
        Serial.println("[sys] ERR: shell launch failed (MicroPython runtime pending)");
        kitt.text_print(0, "PURR OS");
        kitt.text_print(1, kitt.device_name());
        kitt.text_print(2, "No shell");
    }

    // System watchdog loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        // Restart explorer if it crashed
        if (!kitt.process_running(shell)) {
            Serial.printf("[sys] shell died, relaunching: %s\n", shell);
            kitt.app_launch(shell);
        }

        // Log current memory
        KITT::memory_stats_t mem;
        kitt.memory_get_stats(&mem);
        Serial.printf("[sys] RAM %lu/%lu KB free\n", mem.free_ram_kb, mem.total_ram_kb);
    }
}

void system_start() {
    xTaskCreatePinnedToCore(system_task, "system", 8192, nullptr, 3, nullptr, 1);
}
