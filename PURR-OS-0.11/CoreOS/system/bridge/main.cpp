// bridge.meow — translates raw KITT key events to generic keycodes,
// and brokers radio handoff for /friends/ firmware.

#include "../kernel/kitt.h"
#include "esp_log.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bridge";

extern KITT kitt;

// Keymap: pin → generic_key string → KITT::generic_key_t
struct KeyMapEntry {
    uint8_t          pin;
    KITT::generic_key_t key;
};

static KeyMapEntry keymap[32];
static int         keymap_count = 0;

static KITT::generic_key_t string_to_key(const char* s) {
    if (strcmp(s, "UP")     == 0) return KITT::KEY_UP;
    if (strcmp(s, "DOWN")   == 0) return KITT::KEY_DOWN;
    if (strcmp(s, "LEFT")   == 0) return KITT::KEY_LEFT;
    if (strcmp(s, "RIGHT")  == 0) return KITT::KEY_RIGHT;
    if (strcmp(s, "SELECT") == 0) return KITT::KEY_SELECT;
    if (strcmp(s, "BACK")   == 0) return KITT::KEY_BACK;
    if (strcmp(s, "MENU")   == 0) return KITT::KEY_MENU;
    if (strcmp(s, "POWER")  == 0) return KITT::KEY_POWER;
    if (strcmp(s, "SOFT1")  == 0) return KITT::KEY_SOFT1;
    if (strcmp(s, "SOFT2")  == 0) return KITT::KEY_SOFT2;
    return KITT::KEY_NONE;
}

static void load_keymap(const char* path) {
    keymap_count = 0;
    FILE* f = fopen(path, "r");
    if (!f) {
        ESP_LOGW(TAG, "keymap not found: %s", path);
        return;
    }
    char buf[1024] = {};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    JsonDocument doc;
    if (deserializeJson(doc, buf) != DeserializationError::Ok) {
        ESP_LOGE(TAG, "keymap parse error");
        return;
    }

    // keymap JSON: { "pin_number_string": "KEY_NAME", ... }
    for (JsonPair kv : doc.as<JsonObject>()) {
        if (keymap_count >= 32) break;
        keymap[keymap_count].pin = (uint8_t)atoi(kv.key().c_str());
        keymap[keymap_count].key = string_to_key(kv.value().as<const char*>());
        keymap_count++;
    }
    ESP_LOGI(TAG, "keymap loaded: %d entries", keymap_count);
}

static KITT::generic_key_t pin_to_key(uint8_t pin) {
    for (int i = 0; i < keymap_count; i++)
        if (keymap[i].pin == pin) return keymap[i].key;
    return KITT::KEY_NONE;
}

// Radio handoff state
static bool yielding_wifi  = false;
static bool yielding_bt    = false;
static bool yielding_lora  = false;

static void on_reserved_combo() {
    // Power+Select pressed — reclaim all radios from firmware
    ESP_LOGI(TAG, "reserved combo: reclaiming radios");
    if (yielding_wifi)  { kitt.wifi_reclaim();  yielding_wifi  = false; }
    if (yielding_bt)    { kitt.bt_reclaim();    yielding_bt    = false; }
    if (yielding_lora)  { kitt.lora_reclaim();  yielding_lora  = false; }
}

// Called by system.meow when a /friends/ firmware requests radio ownership
void bridge_yield_radios(bool wifi, bool bt, bool lora) {
    if (wifi && !yielding_wifi)  { kitt.wifi_yield();  yielding_wifi  = true; }
    if (bt   && !yielding_bt)    { kitt.bt_yield();    yielding_bt    = true; }
    if (lora && !yielding_lora)  { kitt.lora_yield();  yielding_lora  = true; }
}

void bridge_reclaim_radios() {
    on_reserved_combo();
}

static void bridge_task(void*) {
    // Load keymap from device.json keymap path (stored in KITT)
    // KITT exposes device_name; keymap path is in config.
    // For now load the default cattopad map if present, heltec buttons otherwise.
    const char* kmap = "/system/bridge/keymaps/cattopad_4x5.json";
    struct stat st;
    if (stat(kmap, &st) != 0)
        kmap = "/system/bridge/keymaps/heltec.json";
    load_keymap(kmap);

    kitt.set_reserved_combo_callback(on_reserved_combo);

    while (true) {
        KITT::raw_key_event_t ev;
        while (kitt.get_raw_key_event(&ev)) {
            KITT::generic_key_t gkey = pin_to_key(ev.gpio_pin);
            if (gkey != KITT::KEY_NONE)
                kitt.inject_key(gkey, ev.pressed);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void bridge_start() {
    xTaskCreatePinnedToCore(bridge_task, "bridge", 4096, nullptr, 2, nullptr, 1);
    ESP_LOGI(TAG, "started");
}
