// emergency — KITT-independent recovery shell.
// Triggered by holding BOOT button at power-on before KITT partition loads.
// Exposes device via MTP so a PC can drop a fresh KITT image.
// Can reflash KITT itself — survives a bricked main partition.

#include <Arduino.h>
#include <SPIFFS.h>
#include <Update.h>

// Reuse the display stub directly — no KITT, no LVGL
#include "../../../system/kernel/modules/display_ssd1306.h"
#include "../../../system/kernel/modules/display_ili9488.h"

// Trigger pin: hold BOOT (GPIO0) during power-on
#define EMERGENCY_TRIGGER_PIN 0

static bool ssd1306_mode = false;

static void em_print(uint8_t row, const char* text) {
    if (ssd1306_mode)
        display_ssd1306_text(row, text);
    Serial.printf("[EMERGENCY] row%d: %s\n", row, text);
}

static bool flash_recovery_image(const char* path) {
    File f = SPIFFS.open(path, "r");
    if (!f) return false;
    if (!Update.begin(f.size())) { f.close(); return false; }
    uint8_t buf[512];
    while (f.available()) {
        size_t n = f.read(buf, sizeof(buf));
        if (Update.write(buf, n) != n) { f.close(); Update.abort(); return false; }
    }
    f.close();
    return Update.end(true);
}

void setup() {
    Serial.begin(115200);
    delay(200);

    // Only activate if trigger pin held low at boot
    pinMode(EMERGENCY_TRIGGER_PIN, INPUT_PULLUP);
    if (digitalRead(EMERGENCY_TRIGGER_PIN) != LOW) {
        // Normal boot — should never reach here (emergency partition is separate),
        // but bail cleanly in case of mis-boot.
        Serial.println("[EMERGENCY] trigger not held — idle");
        return;
    }

    SPIFFS.begin(true);

    // Try SSD1306 first (Heltec V3 default)
    Wire.begin(17, 18);
    ssd1306_mode = true;
    display_ssd1306_init();

    em_print(0, "PURR EMERGENCY");
    em_print(1, "Recovery shell");
    em_print(2, "Drop .bin in");
    em_print(3, "/recovery/");

    // MTP mode would mount here via tinyusb — deferred to tinyusb integration
    em_print(4, "USB: MTP pending");

    // Poll for recovery image
    uint32_t start = millis();
    while (millis() - start < 60000) {
        if (SPIFFS.exists("/recovery/recovery.bin")) {
            em_print(5, "Image found!");
            delay(500);
            em_print(6, "Flashing...");
            bool ok = flash_recovery_image("/recovery/recovery.bin");
            if (ok) {
                em_print(7, "OK. Rebooting.");
                SPIFFS.remove("/recovery/recovery.bin");
                delay(1500);
                esp_restart();
            } else {
                em_print(7, "Flash FAILED.");
                delay(5000);
                esp_restart();
            }
        }
        delay(500);
    }

    em_print(5, "Timeout. Reboot.");
    delay(2000);
    esp_restart();
}

void loop() {
    delay(1000);
}
