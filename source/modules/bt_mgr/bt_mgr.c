// bt_mgr.c — PURR OS Bluetooth manager (BLE only, see bt_mgr.h)
//
// Bluedroid BLE-only bring-up: controller + host init, a GAP scan
// (blocking, semaphore-gated like wifi_mgr_scan()), and pairing via a GATTC
// client connection with bonding requested ("Just Works" — IO capability
// NoInputNoOutput, no PIN entry UI exists yet).

#include "bt_mgr.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "bt_mgr";
#define MAX_SCAN_RESULTS 24
#define GATTC_APP_ID 0

static bool s_enabled = false;
static bt_scan_result_t s_scan[MAX_SCAN_RESULTS];
static int  s_scan_count = 0;
static SemaphoreHandle_t s_scan_done_sem = NULL;

static esp_gatt_if_t s_gattc_if = ESP_GATT_IF_NONE;
static uint8_t       s_pair_addr[6];

// ── GAP callback ──────────────────────────────────────────────────────────────

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        break;   // handled synchronously by bt_mgr_scan() below (set then start)
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        struct ble_scan_result_evt_param *r = &param->scan_rst;
        if (r->search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;
        if (s_scan_count >= MAX_SCAN_RESULTS) break;

        bt_scan_result_t *out = &s_scan[s_scan_count];
        memcpy(out->addr, r->bda, 6);
        out->rssi = (int8_t)r->rssi;

        uint8_t name_len = 0;
        uint8_t *name = esp_ble_resolve_adv_data_by_type(
            r->ble_adv, r->adv_data_len + r->scan_rsp_len, ESP_BLE_AD_TYPE_NAME_CMPL, &name_len);
        if (name && name_len) {
            if (name_len > sizeof(out->name) - 1) name_len = sizeof(out->name) - 1;
            memcpy(out->name, name, name_len);
            out->name[name_len] = '\0';
        } else {
            snprintf(out->name, sizeof(out->name), "%02X:%02X:%02X:%02X:%02X:%02X",
                     out->addr[0], out->addr[1], out->addr[2], out->addr[3], out->addr[4], out->addr[5]);
        }
        s_scan_count++;
        break;
    }
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (s_scan_done_sem) xSemaphoreGive(s_scan_done_sem);
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        // Peer is requesting security (they're connecting to us) — accept.
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "auth complete, success=%d", param->ble_security.auth_cmpl.success);
        break;
    default:
        break;
    }
}

// ── GATTC callback (used only to open a connection + trigger bonding) ───────

static void gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) {
    switch (event) {
    case ESP_GATTC_REG_EVT:
        s_gattc_if = gattc_if;
        break;
    case ESP_GATTC_CONNECT_EVT:
        ESP_LOGI(TAG, "gattc connected — requesting encryption/bonding");
        esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
        break;
    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGI(TAG, "gattc disconnected, reason=0x%x", param->disconnect.reason);
        break;
    default:
        break;
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

// The Bluedroid host itself stays initialized+enabled for the module's
// whole lifetime (matching wifi_mgr's "driver always started" approach —
// cycling esp_bluedroid_enable()/disable() repeatedly at runtime is fragile
// and not how either stack is normally used). This flag just gates whether
// Settings/the Meshtastic companion service are allowed to actually scan or
// advertise — disabling stops any in-progress scan immediately.
void bt_mgr_set_enabled(bool on) {
    s_enabled = on;
    if (!on) esp_ble_gap_stop_scanning();
}

bool bt_mgr_is_enabled(void) { return s_enabled; }

int bt_mgr_scan(uint32_t duration_sec) {
    if (!s_enabled) return -1;
    s_scan_count = 0;

    static esp_ble_scan_params_t scan_params = {
        .scan_type          = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval      = 0x50,
        .scan_window        = 0x30,
        .scan_duplicate     = BLE_SCAN_DUPLICATE_ENABLE,
    };
    esp_ble_gap_set_scan_params(&scan_params);

    if (!s_scan_done_sem) s_scan_done_sem = xSemaphoreCreateBinary();
    esp_ble_gap_start_scanning(duration_sec);

    xSemaphoreTake(s_scan_done_sem, pdMS_TO_TICKS((duration_sec + 2) * 1000));
    return s_scan_count;
}

int  bt_mgr_scan_count(void) { return s_scan_count; }

bool bt_mgr_scan_at(int idx, bt_scan_result_t *out) {
    if (idx < 0 || idx >= s_scan_count || !out) return false;
    *out = s_scan[idx];
    return true;
}

esp_err_t bt_mgr_pair(const uint8_t addr[6]) {
    if (!s_enabled || s_gattc_if == ESP_GATT_IF_NONE) return ESP_ERR_INVALID_STATE;
    memcpy(s_pair_addr, addr, 6);
    return esp_ble_gattc_open(s_gattc_if, (uint8_t *)s_pair_addr, BLE_ADDR_TYPE_PUBLIC, true);
}

// ── Module lifecycle ──────────────────────────────────────────────────────────

int bt_mgr_init(void) {
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "controller init failed: %s", esp_err_to_name(ret)); return -1; }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "controller enable failed: %s", esp_err_to_name(ret)); return -1; }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK) { ESP_LOGE(TAG, "bluedroid init failed: %s", esp_err_to_name(ret)); return -1; }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) { ESP_LOGE(TAG, "bluedroid enable failed: %s", esp_err_to_name(ret)); return -1; }
    // s_enabled starts false — Bluedroid itself is up (see bt_mgr_set_enabled's
    // comment), but scanning/advertising stay off until the user opts in.

    esp_ble_gap_register_callback(gap_cb);
    esp_ble_gattc_register_callback(gattc_cb);
    esp_ble_gattc_app_register(GATTC_APP_ID);

    // "Just Works" bonding: no PIN entry UI exists yet, so IO capability is
    // fixed at NoInputNoOutput. Good enough for simple accessories; devices
    // that require a displayed/typed passkey aren't supported.
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t auth_req  = ESP_LE_AUTH_REQ_SC_BOND;
    uint8_t key_size  = 16;
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));

    ESP_LOGI(TAG, "init complete (BLE only)");
    return 0;
}

void bt_mgr_deinit(void) {
    bt_mgr_set_enabled(false);
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    if (s_scan_done_sem) { vSemaphoreDelete(s_scan_done_sem); s_scan_done_sem = NULL; }
}

// ── .purr module header ───────────────────────────────────────────────────────
#include "purr_module.h"

PURR_MODULE_REGISTER(bt_mgr) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_SYSTEM,
    .name              = "bt_mgr",
    .version           = "0.1.0",
    .kernel_min        = "0.9.0",
    .kernel_max        = "",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = bt_mgr_init,
    .deinit            = bt_mgr_deinit,
};
