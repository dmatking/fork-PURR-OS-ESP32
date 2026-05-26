#include "flasher.h"
#include "display_ssd1306.h"
#include "display_ili9488.h"
#include <Arduino.h>
#include <SPIFFS.h>
#include <Update.h>
#include <Preferences.h>

static Preferences prefs;

static void flasher_text(device_config_t* cfg, uint8_t row, const char* text) {
    if (strcmp(cfg->display, "ssd1306") == 0)
        display_ssd1306_text(row, text);
    else
        Serial.printf("[flasher] row%d: %s\n", row, text);
}

static void flasher_clear(device_config_t* cfg) {
    if (strcmp(cfg->display, "ssd1306") == 0)
        display_ssd1306_clear();
}

static bool nvs_clear_boot_flag() {
    prefs.begin("kitt_boot", false);
    prefs.remove("flash_flag");
    prefs.end();
    return true;
}

static bool ota_write_image(const char* path) {
    File f = SPIFFS.open(path, "r");
    if (!f) return false;

    size_t filesize = f.size();
    if (!Update.begin(filesize)) {
        f.close();
        return false;
    }

    uint8_t buf[512];
    while (f.available()) {
        size_t n = f.read(buf, sizeof(buf));
        if (Update.write(buf, n) != n) {
            f.close();
            Update.abort();
            return false;
        }
    }
    f.close();
    return Update.end(true);
}

void flasher_init()   {}
void flasher_update() {}
void flasher_deinit() {}

void flasher_run(device_config_t* cfg) {
    // Minimal init — display only, no LVGL, no radios
    if (strcmp(cfg->display, "ssd1306") == 0)
        display_ssd1306_init();

    flasher_clear(cfg);
    flasher_text(cfg, 0, "PURR OS Flasher");
    flasher_text(cfg, 1, "Looking...");

    bool has_bin  = SPIFFS.exists("/update/update_firmware.bin");
    bool has_purr = SPIFFS.exists("/update/update_firmware.purr");

    if (!has_bin && !has_purr) {
        flasher_text(cfg, 2, "ERR: No image");
        flasher_text(cfg, 3, "/update/*.bin");
        flasher_text(cfg, 4, "PWR to cancel");
        delay(10000);
        nvs_clear_boot_flag();
        esp_restart();
        return;
    }

    const char* img = has_bin ? "/update/update_firmware.bin" : "/update/update_firmware.purr";
    flasher_text(cfg, 2, "Writing...");

    bool ok = ota_write_image(img);

    if (ok) {
        flasher_text(cfg, 3, "Done. Rebooting");
        nvs_clear_boot_flag();
        SPIFFS.remove(img);
        delay(1500);
        esp_restart();
    } else {
        flasher_text(cfg, 3, "ERR: Flash fail");
        flasher_text(cfg, 4, "Image kept.");
        nvs_clear_boot_flag();
        delay(3000);
        esp_restart();
    }
}
