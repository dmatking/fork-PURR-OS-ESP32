// watchdog — NVS heartbeat monitor. Restarts KITT if stale > 3000ms.
// Runs as a FreeRTOS task spawned early in the boot partition.
// Never touches display, radios, or main application heap.

#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

#define WDT_CHECK_INTERVAL_MS  1000
#define WDT_STALE_TIMEOUT_MS   3000

static Preferences prefs;
static uint32_t last_seen_hb = 0;

static bool kitt_ready() {
    prefs.begin("kitt_boot", true);
    bool ready = prefs.getBool("kitt_ready", false);
    prefs.end();
    return ready;
}

static uint32_t read_heartbeat() {
    prefs.begin("kitt_hb", true);
    uint32_t hb = prefs.getUInt("kitt_hb", 0);
    prefs.end();
    return hb;
}

static void trigger_kitt_restart() {
    Serial.println("[wdt] KITT heartbeat stale — restarting");
    // Clear ready flag so KITT re-runs full init on restart
    prefs.begin("kitt_boot", false);
    prefs.putBool("kitt_ready", false);
    prefs.end();
    esp_restart();
}

static void watchdog_task(void*) {
    // Wait for KITT to signal ready before monitoring
    Serial.println("[wdt] waiting for KITT ready...");
    while (!kitt_ready()) vTaskDelay(pdMS_TO_TICKS(500));

    last_seen_hb = read_heartbeat();
    Serial.println("[wdt] KITT ready, monitoring heartbeat");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(WDT_CHECK_INTERVAL_MS));

        uint32_t hb  = read_heartbeat();
        uint32_t now = millis();

        if (hb != last_seen_hb) {
            last_seen_hb = hb;
        } else {
            // Heartbeat unchanged — calculate age
            // Use millis() delta as a proxy since hb timestamp IS millis()
            uint32_t age = now - hb;
            if (age > WDT_STALE_TIMEOUT_MS) {
                trigger_kitt_restart();
                // esp_restart() above never returns, but the vTaskDelay below
                // acts as a safety net in case it's called in a test context.
                vTaskDelay(pdMS_TO_TICKS(5000));
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("[wdt] watchdog boot");
    xTaskCreatePinnedToCore(watchdog_task, "watchdog", 4096, nullptr, 1, nullptr, 1);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(10000));
}
