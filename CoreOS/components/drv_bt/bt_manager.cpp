// bt_manager.cpp — Bluetooth manager (pure ESP-IDF, NVS persistence)
// BLE stack deferred — stubs complete in interface, BLE init pending NimBLE component.

#include "bt_manager.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "bt";

static bool bt_on             = false;
static bool bt_yielded_flag   = false;
static bool discovery_running = false;
static uint32_t discovery_end = 0;

typedef struct { char name[64]; char addr[20]; bool connected; } bt_device_t;

static bt_device_t paired[BT_MAX_PAIRED];
static int paired_count = 0;

static bt_device_t discovered[BT_MAX_DISCOVERED];
static int discovered_count_val = 0;

static void load_paired_from_nvs() {
    nvs_handle_t h;
    if (nvs_open("bt_paired", NVS_READONLY, &h) != ESP_OK) return;

    int32_t count = 0;
    nvs_get_i32(h, "count", &count);
    paired_count = (count < BT_MAX_PAIRED) ? (int)count : BT_MAX_PAIRED;

    for (int i = 0; i < paired_count; i++) {
        char key_n[12], key_a[12];
        snprintf(key_n, sizeof(key_n), "n%d", i);
        snprintf(key_a, sizeof(key_a), "a%d", i);
        size_t nl = sizeof(paired[i].name), al = sizeof(paired[i].addr);
        nvs_get_str(h, key_n, paired[i].name, &nl);
        nvs_get_str(h, key_a, paired[i].addr, &al);
        paired[i].connected = false;
    }
    nvs_close(h);
}

static void save_paired_to_nvs() {
    nvs_handle_t h;
    if (nvs_open("bt_paired", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "count", paired_count);
    for (int i = 0; i < paired_count; i++) {
        char key_n[12], key_a[12];
        snprintf(key_n, sizeof(key_n), "n%d", i);
        snprintf(key_a, sizeof(key_a), "a%d", i);
        nvs_set_str(h, key_n, paired[i].name);
        nvs_set_str(h, key_a, paired[i].addr);
    }
    nvs_commit(h);
    nvs_close(h);
}

void bt_manager_init() {
    load_paired_from_nvs();
    ESP_LOGI(TAG, "manager init (BLE stack pending NimBLE component)");
}

void bt_manager_update() {
    if (discovery_running && (uint32_t)(esp_timer_get_time() / 1000) > discovery_end)
        discovery_running = false;
}

void bt_manager_deinit()     { bt_on = false; }
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
    discovery_end        = (uint32_t)(esp_timer_get_time() / 1000) + timeout_ms;
    ESP_LOGI(TAG, "discovery started (%lu ms)", (unsigned long)timeout_ms);
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
    for (int i = paired_index; i < paired_count - 1; i++) paired[i] = paired[i + 1];
    paired_count--;
    save_paired_to_nvs();
}

void bt_manager_yield()   { bt_yielded_flag = true;  ESP_LOGI(TAG, "yielded"); }
void bt_manager_reclaim() { bt_yielded_flag = false; ESP_LOGI(TAG, "reclaimed"); }
bool bt_manager_yielded() { return bt_yielded_flag; }
