// device_config.cpp — parse device.json from SPIFFS (pure ESP-IDF)

#include "device_config.h"
#include "purr_idf_compat.h"
#include <ArduinoJson.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "cfg";

bool device_config_load(const char* path, device_config_t* out) {
    // Build full VFS path — device.json is stored under /spiffs/
    char fpath[128];
    if (strncmp(path, "/spiffs", 7) == 0)
        snprintf(fpath, sizeof(fpath), "%s", path);
    else
        snprintf(fpath, sizeof(fpath), "/spiffs%s", path);

    FILE* f = fopen(fpath, "r");
    if (!f) {
        ESP_LOGE(TAG, "cannot open %s", fpath);
        return false;
    }

    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, buf);
    if (err) {
        ESP_LOGE(TAG, "JSON parse error: %s", err.c_str());
        return false;
    }

    strlcpy(out->device,       doc["device"]      | "unknown", sizeof(out->device));
    strlcpy(out->display,      doc["display"]     | "none",    sizeof(out->display));
    strlcpy(out->touch,        doc["touch"]       | "none",    sizeof(out->touch));
    strlcpy(out->lora_region,  doc["lora_region"] | "US",      sizeof(out->lora_region));
    strlcpy(out->boot_splash,  doc["boot_splash"] | "",        sizeof(out->boot_splash));
    strlcpy(out->keymap,       doc["keymap"]      | "",        sizeof(out->keymap));

    JsonArray res = doc["display_res"];
    out->display_w = (res && res.size() >= 2) ? (uint16_t)res[0] : 128;
    out->display_h = (res && res.size() >= 2) ? (uint16_t)res[1] : 64;

    out->psram                    = doc["psram"]    | false;
    out->pi_slot                  = doc["pi_slot"]  | false;
    out->verbose_boot             = doc["verbose"]  | false;
    out->cpu_max_mhz              = doc["cpu_max_mhz"] | 240;
    out->friends_ram_threshold_kb = doc["friends_ram_threshold_kb"] | 64;

    const char* flash_str = doc["flash"] | "4mb";
    out->flash_mb = (uint8_t)atoi(flash_str);
    if (out->flash_mb == 0) out->flash_mb = 4;

    out->ram_kb   = doc["ram_kb"]   | (uint16_t)(esp_get_free_heap_size() / 1024);
    out->psram_mb = doc["psram_mb"] | 0;

    out->wifi = false; out->bt = false; out->lora = false;
    JsonArray radios = doc["radios"];
    if (radios) {
        for (JsonVariant v : radios) {
            const char* r = v.as<const char*>();
            if (r && strcmp(r, "wifi") == 0) out->wifi = true;
            if (r && strcmp(r, "bt")   == 0) out->bt   = true;
            if (r && strcmp(r, "lora") == 0) out->lora = true;
        }
    }

    ESP_LOGI(TAG, "loaded: device=%s display=%s %dx%d psram=%d",
             out->device, out->display, out->display_w, out->display_h, out->psram);
    return true;
}
