#pragma once
// bt_mgr.h — PURR OS Bluetooth manager (public API)
//
// BLE only — despite being asked for "classic + BLE," the T-Deck Plus's
// ESP32-S3 has no classic Bluetooth (BR/EDR) radio hardware at all
// (SOC_BT_CLASSIC_SUPPORTED isn't defined for this chip in ESP-IDF's
// soc_caps.h, only SOC_BLE_SUPPORTED is) — Bluedroid is still the host
// stack (vs. NimBLE), matching the "want dual-mode-capable stack" intent,
// it just only ever runs in BLE mode on this board.

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char    name[32];
    uint8_t addr[6];
    int8_t  rssi;
} bt_scan_result_t;

int  bt_mgr_init(void);
void bt_mgr_deinit(void);

void bt_mgr_set_enabled(bool on);
bool bt_mgr_is_enabled(void);

// Blocking scan (duration_sec seconds) — fills the internal result list,
// read back via bt_mgr_scan_count()/bt_mgr_scan_at().
int  bt_mgr_scan(uint32_t duration_sec);
int  bt_mgr_scan_count(void);
bool bt_mgr_scan_at(int idx, bt_scan_result_t *out);

// Connects to a scanned device and requests bonding ("Just Works" — no PIN
// entry UI exists yet, so IO capability is fixed at NoInputNoOutput).
esp_err_t bt_mgr_pair(const uint8_t addr[6]);

#ifdef __cplusplus
}
#endif
