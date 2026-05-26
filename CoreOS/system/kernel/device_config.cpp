#include "device_config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

bool device_config_load(const char* path, device_config_t* out) {
    if (!SPIFFS.begin(true)) {
        Serial.println("[cfg] SPIFFS mount failed");
        return false;
    }

    File f = SPIFFS.open(path, "r");
    if (!f) {
        Serial.printf("[cfg] cannot open %s\n", path);
        return false;
    }

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[cfg] JSON parse error: %s\n", err.c_str());
        return false;
    }

    strlcpy(out->device,       doc["device"]  | "unknown",  sizeof(out->device));
    strlcpy(out->display,      doc["display"] | "none",     sizeof(out->display));
    strlcpy(out->touch,        doc["touch"]   | "none",     sizeof(out->touch));
    strlcpy(out->lora_region,  doc["lora_region"] | "US",   sizeof(out->lora_region));
    strlcpy(out->boot_splash,  doc["boot_splash"] | "",     sizeof(out->boot_splash));
    strlcpy(out->keymap,       doc["keymap"]  | "",         sizeof(out->keymap));

    JsonArray res = doc["display_res"];
    out->display_w = (res && res.size() >= 2) ? (uint16_t)res[0] : 128;
    out->display_h = (res && res.size() >= 2) ? (uint16_t)res[1] : 64;

    out->psram                   = doc["psram"]    | false;
    out->pi_slot                 = doc["pi_slot"]  | false;
    out->verbose_boot            = doc["verbose"]  | false;
    out->cpu_max_mhz             = doc["cpu_max_mhz"] | 240;
    out->friends_ram_threshold_kb = doc["friends_ram_threshold_kb"] | 64;

    // Flash size string e.g. "8mb" → uint8_t
    const char* flash_str = doc["flash"] | "4mb";
    out->flash_mb = (uint8_t)atoi(flash_str);
    if (out->flash_mb == 0) out->flash_mb = 4;

    // RAM / PSRAM — device.json may omit these; fill from ESP-IDF if absent
    out->ram_kb   = doc["ram_kb"]   | (uint16_t)(esp_get_free_heap_size() / 1024);
    out->psram_mb = doc["psram_mb"] | 0;

    // Radios array: ["wifi", "lora", "bt"]
    out->wifi = false;
    out->bt   = false;
    out->lora = false;
    JsonArray radios = doc["radios"];
    if (radios) {
        for (JsonVariant v : radios) {
            const char* r = v.as<const char*>();
            if (strcmp(r, "wifi") == 0) out->wifi = true;
            if (strcmp(r, "bt")   == 0) out->bt   = true;
            if (strcmp(r, "lora") == 0) out->lora = true;
        }
    }

    Serial.printf("[cfg] loaded: device=%s display=%s %dx%d psram=%d\n",
                  out->device, out->display, out->display_w, out->display_h, out->psram);
    return true;
}
