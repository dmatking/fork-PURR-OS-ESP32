// pairing_module.c — PURR_MOD_SYSTEM registration + pairing state machine
// + NVS persistence, riding on proximity_module.c's shared ESP-NOW frame
// dispatch (PROXIMITY_FRAME_PAIRING).
//
// State lives entirely in RAM except the durable "who am I paired with"
// fact, which follows the mesh_router.c/mc_contacts.cpp dirty-flag + small
// persist-task NVS pattern used elsewhere in this codebase. The frame
// handler (pairing_on_frame()) runs on proximity_task()'s own thread,
// which may have a PSRAM-backed stack — it only ever touches in-RAM state
// and sets a dirty flag, never NVS directly (same PSRAM-stack-vs-flash-
// cache-disable hazard documented in meshcore_module.cpp applies here);
// the actual NVS write happens on this module's own dedicated internal-
// RAM-stack pairing_task(), mirroring mc_persist_task().

#include "pairing.h"
#include "../proximity/proximity.h"
#include "../../kernel/core/purr_kernel.h"
#include "../../kernel/core/purr_module.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "pairing";

#define PAIRING_NVS_NS      "purr_pairing"
#define PAIRING_TIMEOUT_MS  60000UL   // a pending request/confirm with no action expires

typedef enum {
    PAIRING_MSG_REQUEST = 1,   // initiator -> responder: {code, sender's name}
    PAIRING_MSG_ACCEPT  = 2,   // responder -> initiator: {sender's name}
    PAIRING_MSG_REJECT  = 3,   // responder -> initiator
    PAIRING_MSG_UNPAIR  = 4,   // either -> other, best-effort notice
} pairing_msg_type_t;

typedef struct __attribute__((packed)) {
    uint8_t  msg_type;
    uint16_t code;
    char     name[20];
} pairing_wire_msg_t;

static pairing_state_t s_state = PAIRING_STATE_NONE;

static uint8_t  s_pending_mac[6];
static char     s_pending_name[20];
static uint16_t s_pending_code;
static uint32_t s_pending_started_ms;

// Trust list — see pairing.h's top comment. Index 0 is what the two
// existing single-device callers (nearby_app.c, oled_ui_module.c) see
// through pairing_is_paired()/pairing_get_paired_mac()/_name()/
// pairing_unpair(); new multi-device-aware code uses the indexed API.
static paired_device_t s_paired_devices[PAIRING_MAX_DEVICES];
static int      s_paired_count = 0;
static volatile bool s_dirty = false;

// Home base — see pairing.h's comment. Own NVS key, not a paired_device_t
// field (avoids the blob-versioning risk documented there). Persisted via
// the same s_dirty/pairing_task() path as the trust list itself, so
// PSRAM-stack callers (UI) never touch NVS directly.
static uint8_t s_home_base_mac[6];
static bool    s_home_base_set = false;

// -1 if not found. Internal only — callers use pairing_is_trusted()/
// pairing_device_at().
static int find_paired_idx(const uint8_t mac[6]) {
    for (int i = 0; i < s_paired_count; i++) {
        if (memcmp(s_paired_devices[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

// Adds mac/name to the trust list, or updates the name in place if mac is
// already present (re-pairing with a device whose display name changed).
// Silently drops the add if the list is full — same "cap and warn" shape
// as mesh_router.c's MAX_NODES handling, not a hard error the caller needs
// to check for (a full trust list is a real but rare edge case, not
// something worth threading a bool return through every ACCEPT/CONFIRM
// call site for).
static void add_or_update_paired(const uint8_t mac[6], const char *name) {
    int idx = find_paired_idx(mac);
    if (idx < 0) {
        if (s_paired_count >= PAIRING_MAX_DEVICES) {
            ESP_LOGW(TAG, "trust list full (%d devices) — not adding new pairing", PAIRING_MAX_DEVICES);
            return;
        }
        idx = s_paired_count++;
        memcpy(s_paired_devices[idx].mac, mac, 6);
    }
    strncpy(s_paired_devices[idx].name, name ? name : "", sizeof(s_paired_devices[idx].name) - 1);
    s_paired_devices[idx].name[sizeof(s_paired_devices[idx].name) - 1] = 0;
    s_dirty = true;
}

// Removes mac from the trust list if present, compacting the array (order
// doesn't matter — nothing depends on trust-list index stability across a
// forget(), only within a single enumeration pass).
static bool remove_paired(const uint8_t mac[6]) {
    int idx = find_paired_idx(mac);
    if (idx < 0) return false;
    s_paired_devices[idx] = s_paired_devices[s_paired_count - 1];
    s_paired_count--;
    // A forgotten device can't stay the home base — leaving it set would
    // point homebase.c's presence watcher at a MAC that's no longer trusted.
    if (s_home_base_set && memcmp(s_home_base_mac, mac, 6) == 0) {
        s_home_base_set = false;
    }
    s_dirty = true;
    return true;
}

static TaskHandle_t s_task = NULL;

// ── NVS ──────────────────────────────────────────────────────────────────

// Whole trust list as one blob, same shape as mesh_router.c's own
// "nodes" blob — a bounded fixed-layout array is simpler than per-device
// NVS keys and the list is small (PAIRING_MAX_DEVICES=8, ~26 bytes each).
static void load_paired(void) {
    nvs_handle_t h;
    if (nvs_open(PAIRING_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    uint8_t count = 0;
    if (nvs_get_u8(h, "count", &count) == ESP_OK && count > 0) {
        if (count > PAIRING_MAX_DEVICES) count = PAIRING_MAX_DEVICES;
        size_t blob_len = sizeof(paired_device_t) * (size_t)count;
        if (nvs_get_blob(h, "devices", s_paired_devices, &blob_len) == ESP_OK) {
            s_paired_count = count;
        }
    }

    size_t hb_len = sizeof(s_home_base_mac);
    if (nvs_get_blob(h, "home_base", s_home_base_mac, &hb_len) == ESP_OK && hb_len == sizeof(s_home_base_mac)) {
        s_home_base_set = true;
    }
    nvs_close(h);
}

static void save_paired(void) {
    nvs_handle_t h;
    if (nvs_open(PAIRING_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "count", (uint8_t)s_paired_count);
    if (s_paired_count > 0) {
        nvs_set_blob(h, "devices", s_paired_devices, sizeof(paired_device_t) * (size_t)s_paired_count);
    }
    if (s_home_base_set) {
        nvs_set_blob(h, "home_base", s_home_base_mac, sizeof(s_home_base_mac));
    } else {
        nvs_erase_key(h, "home_base");   // best-effort; ESP_ERR_NVS_NOT_FOUND if already absent
    }
    nvs_commit(h);
    nvs_close(h);
}

// ── Frame handling ───────────────────────────────────────────────────────
// Runs on proximity_task() — see this file's header comment. No NVS here.

static void send_msg(const uint8_t *mac, pairing_msg_type_t type, const char *name) {
    pairing_wire_msg_t msg = {0};
    msg.msg_type = (uint8_t)type;
    msg.code = (type == PAIRING_MSG_REQUEST) ? s_pending_code : 0;
    if (name) {
        strncpy(msg.name, name, sizeof(msg.name) - 1);
    }
    // Diagnostic: current WiFi channel at send time — compare against the
    // peer's own logged channel to confirm/rule out a channel mismatch
    // (see on_espnow_send()'s comment in proximity_module.c).
    uint8_t chan = 0; wifi_second_chan_t second;
    esp_wifi_get_channel(&chan, &second);
    ESP_LOGI(TAG, "sending msg_type=%d to %02X:%02X:%02X:%02X:%02X:%02X on channel %d",
             (int)type, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (int)chan);
    bool ok = proximity_send_unicast(mac, PROXIMITY_FRAME_PAIRING, (const uint8_t *)&msg, sizeof(msg));
    if (!ok) {
        ESP_LOGW(TAG, "proximity_send_unicast() returned false for msg_type=%d", (int)type);
    }
}

static void pairing_on_frame(const uint8_t *mac, int8_t rssi, const uint8_t *payload, size_t len) {
    (void)rssi;
    if (len != sizeof(pairing_wire_msg_t)) {
        ESP_LOGW(TAG, "pairing frame from %02X:%02X:%02X:%02X:%02X:%02X wrong size (%u, expected %u)",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                 (unsigned)len, (unsigned)sizeof(pairing_wire_msg_t));
        return;
    }

    pairing_wire_msg_t msg;
    memcpy(&msg, payload, sizeof(msg));
    char name[sizeof(msg.name) + 1];
    memcpy(name, msg.name, sizeof(msg.name));
    name[sizeof(msg.name)] = 0;

    uint8_t chan = 0; wifi_second_chan_t second;
    esp_wifi_get_channel(&chan, &second);
    ESP_LOGI(TAG, "recv msg_type=%d from %02X:%02X:%02X:%02X:%02X:%02X (\"%s\") on channel %d, our state=%d",
             (int)msg.msg_type, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], name, (int)chan, (int)s_state);

    switch ((pairing_msg_type_t)msg.msg_type) {
    case PAIRING_MSG_REQUEST:
        // Drop only if we're mid-negotiation with someone else — an
        // already-PAIRED state (with one or more OTHER devices) doesn't
        // block accepting a new request; see pairing.h's multi-device
        // trust-list comment. No request queueing/stealing an
        // in-progress negotiation, though.
        if (s_state != PAIRING_STATE_NONE && s_state != PAIRING_STATE_PAIRED) return;
        memcpy(s_pending_mac, mac, 6);
        strncpy(s_pending_name, name, sizeof(s_pending_name) - 1);
        s_pending_name[sizeof(s_pending_name) - 1] = 0;
        s_pending_code = msg.code;
        s_pending_started_ms = (uint32_t)purr_kernel_uptime_ms();
        s_state = PAIRING_STATE_PENDING_INCOMING;

        char notify_body[64];
        snprintf(notify_body, sizeof(notify_body), "Pairing request from %s", name);
        purr_kernel_notify("Nearby device", notify_body, "pairing");
        break;

    case PAIRING_MSG_ACCEPT:
        if (s_state != PAIRING_STATE_PENDING_OUTGOING) return;
        if (memcmp(mac, s_pending_mac, 6) != 0) return;
        add_or_update_paired(mac, name);
        s_state = PAIRING_STATE_PAIRED;
        break;

    case PAIRING_MSG_REJECT:
        if (s_state != PAIRING_STATE_PENDING_OUTGOING) return;
        if (memcmp(mac, s_pending_mac, 6) != 0) return;
        s_state = PAIRING_STATE_NONE;
        break;

    case PAIRING_MSG_UNPAIR:
        if (!remove_paired(mac)) return;
        if (s_state == PAIRING_STATE_PAIRED) s_state = PAIRING_STATE_NONE;
        break;

    default:
        break;
    }
}

// ── Task ─────────────────────────────────────────────────────────────────
// Internal-RAM stack (plain xTaskCreate, no WithCaps) — the only place in
// this module allowed to touch NVS.

static void pairing_task(void *arg) {
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if ((s_state == PAIRING_STATE_PENDING_OUTGOING || s_state == PAIRING_STATE_PENDING_INCOMING) &&
            (uint32_t)purr_kernel_uptime_ms() - s_pending_started_ms >= PAIRING_TIMEOUT_MS) {
            ESP_LOGI(TAG, "pending pairing timed out");
            s_state = PAIRING_STATE_NONE;
        }

        if (s_dirty) {
            s_dirty = false;
            save_paired();
        }
    }
}

// ── Public API ───────────────────────────────────────────────────────────

pairing_state_t pairing_get_state(void) { return s_state; }

bool pairing_start(const uint8_t mac[6], const char *peer_name) {
    // PAIRED doesn't block starting a new negotiation with a DIFFERENT
    // device — only being mid-negotiation already does. See pairing.h's
    // multi-device trust-list comment.
    if (!mac || (s_state != PAIRING_STATE_NONE && s_state != PAIRING_STATE_PAIRED)) return false;

    memcpy(s_pending_mac, mac, 6);
    s_pending_name[0] = 0;
    if (peer_name) {
        strncpy(s_pending_name, peer_name, sizeof(s_pending_name) - 1);
    }
    s_pending_code = (uint16_t)(esp_random() % 10000);
    s_pending_started_ms = (uint32_t)purr_kernel_uptime_ms();
    s_state = PAIRING_STATE_PENDING_OUTGOING;

    char own_name[20];
    proximity_get_own_name(own_name, sizeof(own_name));
    send_msg(mac, PAIRING_MSG_REQUEST, own_name);
    return true;
}

void pairing_cancel(void) {
    if (s_state == PAIRING_STATE_PENDING_OUTGOING) s_state = PAIRING_STATE_NONE;
}

bool pairing_get_pending_code(char *out, size_t out_len) {
    if (s_state != PAIRING_STATE_PENDING_INCOMING && s_state != PAIRING_STATE_PENDING_OUTGOING) return false;
    if (!out || out_len == 0) return false;
    snprintf(out, out_len, "%04u", (unsigned)s_pending_code);
    return true;
}

bool pairing_get_pending_peer_name(char *out, size_t out_len) {
    if (s_state != PAIRING_STATE_PENDING_INCOMING && s_state != PAIRING_STATE_PENDING_OUTGOING) return false;
    if (!out || out_len == 0) return false;
    snprintf(out, out_len, "%s", s_pending_name);
    return true;
}

void pairing_confirm(void) {
    if (s_state != PAIRING_STATE_PENDING_INCOMING) return;

    add_or_update_paired(s_pending_mac, s_pending_name);
    s_state = PAIRING_STATE_PAIRED;

    char own_name[20];
    proximity_get_own_name(own_name, sizeof(own_name));
    send_msg(s_pending_mac, PAIRING_MSG_ACCEPT, own_name);
}

void pairing_reject(void) {
    if (s_state != PAIRING_STATE_PENDING_INCOMING) return;
    send_msg(s_pending_mac, PAIRING_MSG_REJECT, NULL);
    s_state = PAIRING_STATE_NONE;
}

bool pairing_is_paired(void) { return s_paired_count > 0; }

bool pairing_get_paired_mac(uint8_t out_mac[6]) {
    if (s_paired_count == 0 || !out_mac) return false;
    memcpy(out_mac, s_paired_devices[0].mac, 6);
    return true;
}

bool pairing_get_paired_name(char *out, size_t out_len) {
    if (s_paired_count == 0 || !out || out_len == 0) return false;
    snprintf(out, out_len, "%s", s_paired_devices[0].name);
    return true;
}

void pairing_unpair(void) {
    if (s_paired_count == 0) return;
    pairing_forget(s_paired_devices[0].mac);
}

int pairing_device_count(void) { return s_paired_count; }

bool pairing_device_at(int idx, paired_device_t *out) {
    if (idx < 0 || idx >= s_paired_count || !out) return false;
    *out = s_paired_devices[idx];
    return true;
}

bool pairing_is_trusted(const uint8_t mac[6]) {
    if (!mac) return false;
    return find_paired_idx(mac) >= 0;
}

void pairing_forget(const uint8_t mac[6]) {
    if (!mac || find_paired_idx(mac) < 0) return;
    send_msg(mac, PAIRING_MSG_UNPAIR, NULL);
    remove_paired(mac);
    if (s_state == PAIRING_STATE_PAIRED) s_state = PAIRING_STATE_NONE;
}

bool pairing_set_home_base(const uint8_t mac[6]) {
    if (!mac || find_paired_idx(mac) < 0) return false;
    memcpy(s_home_base_mac, mac, 6);
    s_home_base_set = true;
    s_dirty = true;
    return true;
}

bool pairing_get_home_base(uint8_t out_mac[6]) {
    if (!s_home_base_set || !out_mac) return false;
    memcpy(out_mac, s_home_base_mac, 6);
    return true;
}

bool pairing_is_home_base(const uint8_t mac[6]) {
    if (!mac || !s_home_base_set) return false;
    return memcmp(mac, s_home_base_mac, 6) == 0;
}

void pairing_clear_home_base(void) {
    if (!s_home_base_set) return;
    s_home_base_set = false;
    s_dirty = true;
}

// ── Lifecycle ────────────────────────────────────────────────────────────

int pairing_init(void) {
    // Safe here: pairing_init() runs on the kernel's module-loader task
    // (internal RAM stack), same reasoning meshcore_module.cpp documents
    // for why its own identity load moved out of mc_task() and into
    // mc_manager_init() — NVS access here, not on pairing_task() the first
    // time, is fine, and the load only happens once at boot anyway.
    load_paired();

    proximity_register_handler(PROXIMITY_FRAME_PAIRING, pairing_on_frame);

    // Small/no PSRAM hazard here either way, but internal RAM keeps this
    // consistent with "only pairing_task() touches NVS" — plain
    // xTaskCreatePinnedToCore (not WithCaps), core 1 alongside the mesh/
    // radio-companion tasks it's conceptually part of (see mc_persist_task).
    //
    // Tried un-pinning this (leaving it tskNO_AFFINITY) on the theory that
    // it was adding to core 1 contention against cupcake's UI task — live
    // hardware testing showed the opposite: the "UI TASK UNRESPONSIVE"
    // crash-guard hang tripped FASTER unpinned (~33s) than pinned (~89s).
    // An unpinned task can still land on core 1 (and migrate there
    // unpredictably), so pinning it deterministically seems to help, not
    // hurt. The underlying stall is still unexplained — see the "Remote
    // radio companion" plan notes / meshtastic's own mesh_task pinning
    // comment; leading suspect is CPU monopolization somewhere in the
    // sx1262_rl/RadioLib radio-polling path (a documented precedent exists
    // in sx1262.c's wait_busy() for the old hand-rolled driver — same
    // "UI TASK UNRESPONSIVE" signature, fixed there already; sx1262_rl's
    // RadioLib-vendored busy-wait hasn't been audited the same way yet).
    BaseType_t ok = xTaskCreatePinnedToCore(pairing_task, "pairing", 3072, NULL, 2, &s_task, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create pairing task — persistence disabled");
    }

    ESP_LOGI(TAG, "ready (%d paired device%s)", s_paired_count, s_paired_count == 1 ? "" : "s");
    return 0;
}

void pairing_deinit(void) {
    if (s_task) {
        if (s_dirty) {
            s_dirty = false;
            save_paired();
        }
        vTaskDelete(s_task);
        s_task = NULL;
    }
    proximity_register_handler(PROXIMITY_FRAME_PAIRING, NULL);
    s_state = PAIRING_STATE_NONE;
}

// ── Module header ─────────────────────────────────────────────────────────

PURR_MODULE_REGISTER(pairing) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_SYSTEM,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "pairing",
    .version           = "1.0.0",
    .kernel_min        = "0.11.1",
    .kernel_max        = "",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = pairing_init,
    .deinit            = pairing_deinit,
};
