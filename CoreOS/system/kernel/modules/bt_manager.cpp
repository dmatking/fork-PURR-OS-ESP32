#include "bt_manager.h"
#include <Arduino.h>
#include <Preferences.h>

// ESP32-S3 BT classic is not available; BT here means BLE via NimBLE or BlueDroid.
// This module provides the KITT API surface. Full BLE scan/pair implementation
// requires NimBLE-Arduino as an ESP-IDF component — stubs here are complete in
// interface but defer the BLE stack bring-up to that integration step.

static bool bt_on             = false;
static bool bt_yielded_flag   = false;
static bool discovery_running = false;
static uint32_t discovery_end = 0;

typedef struct { char name[64]; char addr[20]; bool connected; } bt_device_t;

static bt_device_t paired[BT_MAX_PAIRED];
static int paired_count = 0;

static bt_device_t discovered[BT_MAX_DISCOVERED];
static int discovered_count_val = 0;

static Preferences prefs;

static void load_paired_from_nvs() {
    prefs.begin("bt_paired", true);
    paired_count = prefs.getInt("count", 0);
    for (int i = 0; i < paired_count && i < BT_MAX_PAIRED; i++) {
        char key_n[12], key_a[12];
        snprintf(key_n, sizeof(key_n), "n%d", i);
        snprintf(key_a, sizeof(key_a), "a%d", i);
        String n = prefs.getString(key_n, "");
        String a = prefs.getString(key_a, "");
        strlcpy(paired[i].name, n.c_str(), sizeof(paired[i].name));
        strlcpy(paired[i].addr, a.c_str(), sizeof(paired[i].addr));
        paired[i].connected = false;
    }
    prefs.end();
}

static void save_paired_to_nvs() {
    prefs.begin("bt_paired", false);
    prefs.putInt("count", paired_count);
    for (int i = 0; i < paired_count; i++) {
        char key_n[12], key_a[12];
        snprintf(key_n, sizeof(key_n), "n%d", i);
        snprintf(key_a, sizeof(key_a), "a%d", i);
        prefs.putString(key_n, paired[i].name);
        prefs.putString(key_a, paired[i].addr);
    }
    prefs.end();
}

void bt_manager_init() {
    load_paired_from_nvs();
    // BLE stack init deferred — requires NimBLE component
    Serial.println("[bt] manager init (BLE stack pending NimBLE component)");
}

void bt_manager_update() {
    if (discovery_running && millis() > discovery_end) {
        discovery_running = false;
    }
}

void bt_manager_deinit() {
    bt_on = false;
}

bool bt_manager_enabled()    { return bt_on && !bt_yielded_flag; }
void bt_manager_enable()     { bt_on = true; }
void bt_manager_disable()    { bt_on = false; }

int  bt_manager_paired_count() { return paired_count; }

void bt_manager_get_paired_name(int i, char* buf, size_t len) {
    if (i < 0 || i >= paired_count) { buf[0] = '\0'; return; }
    strlcpy(buf, paired[i].name, len);
}

void bt_manager_get_paired_addr(int i, char* buf, size_t len) {
    if (i < 0 || i >= paired_count) { buf[0] = '\0'; return; }
    strlcpy(buf, paired[i].addr, len);
}

bool bt_manager_device_connected(int i) {
    return (i >= 0 && i < paired_count) && paired[i].connected;
}

void bt_manager_start_discovery(uint32_t timeout_ms) {
    if (!bt_on) return;
    discovered_count_val = 0;
    discovery_running    = true;
    discovery_end        = millis() + timeout_ms;
    Serial.printf("[bt] discovery started (%lu ms)\n", timeout_ms);
}

void bt_manager_stop_discovery()   { discovery_running = false; }
bool bt_manager_discovery_active() { return discovery_running; }
int  bt_manager_discovered_count() { return discovered_count_val; }

void bt_manager_get_discovered_name(int i, char* buf, size_t len) {
    if (i < 0 || i >= discovered_count_val) { buf[0] = '\0'; return; }
    strlcpy(buf, discovered[i].name, len);
}

void bt_manager_get_discovered_addr(int i, char* buf, size_t len) {
    if (i < 0 || i >= discovered_count_val) { buf[0] = '\0'; return; }
    strlcpy(buf, discovered[i].addr, len);
}

void bt_manager_pair(int discovered_index) {
    if (discovered_index < 0 || discovered_index >= discovered_count_val) return;
    if (paired_count >= BT_MAX_PAIRED) return;
    paired[paired_count] = discovered[discovered_index];
    paired[paired_count].connected = false;
    paired_count++;
    save_paired_to_nvs();
}

void bt_manager_unpair(int paired_index) {
    if (paired_index < 0 || paired_index >= paired_count) return;
    for (int i = paired_index; i < paired_count - 1; i++)
        paired[i] = paired[i + 1];
    paired_count--;
    save_paired_to_nvs();
}

void bt_manager_yield()    { bt_yielded_flag = true;  Serial.println("[bt] yielded"); }
void bt_manager_reclaim()  { bt_yielded_flag = false; Serial.println("[bt] reclaimed"); }
bool bt_manager_yielded()  { return bt_yielded_flag; }
