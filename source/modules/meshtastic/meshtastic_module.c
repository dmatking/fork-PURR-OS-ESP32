// meshtastic_module.c — PURR_MOD_SYSTEM registration + mesh task + public API.
// Ported from PURR-OS-0.11/CoreOS/system/kernel/modules/purr_mesh.cpp's
// purr_mesh_init()/mesh_task() — the key change is that this task waits on
// purr_kernel_radio() (whatever radio driver a device selects) rather than
// assuming a specific chip, and talks to it only through catcall_radio_t.

#include "meshtastic.h"
#include "sdkconfig.h"

// meshtastic_module.c is unconditionally compiled into every device's
// firmware (components under source/modules/ always are — confirmed live
// this session, it's why bt_mgr.c/mesh_ble.c broke 9/10 devices before
// getting the same guard). CONFIG_PURR_FEATURE_MESHTASTIC is off by
// default everywhere; the #else branch below gives every caller
// (meshchat.c's 10 call sites, mesh_ble.c) real, linkable no-op symbols
// instead of a build failure or a deleted app. See Kconfig.projbuild's
// help text.
#ifdef CONFIG_PURR_FEATURE_MESHTASTIC

#include "mesh_radio.h"
#include "mesh_router.h"
#include "mesh_ble.h"
#include "../../kernel/core/purr_kernel.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>
#include <pb_decode.h>
#include "meshtastic/portnums.pb.h"
#include "meshtastic/mesh.pb.h"

static const char *TAG = "meshtastic";

static uint32_t     s_node_id = 0;
static bool          s_ready   = false;
static mesh_rx_cb_t  s_rx_cbs[MESH_MAX_RX_CB];
static TaskHandle_t  s_task    = NULL;

// Outgoing packets are encoded synchronously (cheap: AES-CTR + a small
// memcpy, no radio access) but actually transmitted only on mesh_task()'s
// own task, via this queue — see mesh_manager_send_text()'s comment for
// why. Depth 4: a human sending messages faster than the radio can clear
// a ~10ms-polled queue is the actual backpressure signal, not a number
// tuned against a measurement.
typedef struct {
    uint8_t wire[256];
    size_t  len;
} mesh_tx_item_t;
#define MESH_TX_QUEUE_DEPTH 4
static QueueHandle_t s_tx_queue = NULL;

// NODEINFO_APP re-broadcast interval — matches the old code's actual timer
// (15 min), not the abbreviated "every 10 minutes" mentioned in the archived
// plan doc; port from the real, working code, not the doc summary.
#define MESH_ANNOUNCE_INTERVAL_MS (15UL * 60UL * 1000UL)

// ── Liveness heartbeat ────────────────────────────────────────────────────────
// mesh_task() stamps this every loop iteration; mesh_manager_is_alive()
// (registered with purr_kernel_health_register() in mesh_manager_init())
// reads it, so the kernel's shared health watchdog can detect a hung/
// crashed mesh task and the Services app can show its live status.
static volatile uint32_t s_last_heartbeat_ms = 0;
#define MESH_WATCHDOG_STALE_MS   5000UL

static void mesh_task(void *arg)
{
    (void)arg;

    // Wait for a radio catcall to be registered — don't assume a specific
    // chip or load-priority ordering against it (same reasoning as
    // cardstack_module.c waiting on purr_kernel_boot_ready(), just waiting
    // on this module's actual dependency instead).
    while (!purr_kernel_radio()) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    uint32_t freq_hz = mesh_radio_freq_for_region(NULL);  // US default for now
    mesh_radio_apply_preset(freq_hz);
    mesh_router_init(s_node_id);
    purr_kernel_set_lora_available(true);

    s_ready = true;
    ESP_LOGI(TAG, "ready id=%08lX  %luHz  SF%d BW%dkHz",
             (unsigned long)s_node_id, (unsigned long)freq_hz,
             MESH_SF, MESH_BW_HZ / 1000);
    purr_kernel_notify("Mesh ready", "Meshtastic mesh online", "meshtastic");

    // Announce ourselves on the mesh immediately, then on the interval above.
    uint32_t last_announce = (uint32_t)(esp_timer_get_time() / 1000ULL) - MESH_ANNOUNCE_INTERVAL_MS + 1000UL;

    for (;;) {
        const catcall_radio_t *radio = purr_kernel_radio();

        // ── RX ────────────────────────────────────────────────────────────
        // Locked for the whole data_available()/receive()/rssi()/snr()
        // sequence — see mesh_radio_lock()'s doc comment. Everything past
        // this point (decode, dedup, notify, callbacks) doesn't touch the
        // radio and doesn't need the lock held.
        mesh_radio_lock();
        bool have_data = radio && radio->data_available && radio->data_available();
        uint8_t raw[256];
        int raw_len = 0;
        int rssi = 0;
        float snr = 0.0f;
        if (have_data) {
            raw_len = radio->receive ? radio->receive(raw, sizeof(raw)) : 0;
            rssi = radio->rssi ? radio->rssi() : 0;
            snr  = radio->snr  ? radio->snr()  : 0.0f;
        }
        mesh_radio_unlock();
        if (have_data) {

            if (raw_len > 0) {
                uint32_t from = 0, to = 0, pkt_id = 0;
                uint8_t  hop_limit  = 0;
                bool     want_ack   = false;
                int      channel_idx = -1;
                int      portnum   = 0;
                uint8_t  payload[240];
                size_t   payload_len = 0;

                // Diagnostic — previously a failed decode (bad CRC, wrong
                // channel key, malformed header) was completely silent, the
                // same as nothing arriving at all. With a live "0 nodes"
                // report and no other visible errors, that ambiguity is the
                // actual blocker: the else branch below is what tells apart
                // "no RF ever lands" (never fires) from "RF lands but won't
                // decode" (fires with a real raw_len/rssi) — a wrong channel
                // PSK or sync word looks identical to dead silence otherwise.
                // A channel-hash-doesn't-match-anything-we-know packet also
                // returns false here, but that's expected/normal (someone
                // else's private room) — mesh_router_decode() itself only
                // logs a warning for genuine decode failures, not that case.
                bool decoded = mesh_router_decode(raw, (size_t)raw_len, &from, &to, &pkt_id, &hop_limit,
                                        &want_ack, &channel_idx, &portnum, payload, &payload_len, sizeof(payload) - 1);
                if (!decoded) {
                    ESP_LOGD(TAG, "rx: %d bytes received but not decoded (rssi=%d snr=%.1f)",
                             raw_len, rssi, snr);
                }
                // Half-duplex radios can hear their own transmission echoed
                // back (antenna reflection, or the RX re-arm landing before
                // the TX's own tail has fully cleared) — without this check
                // that showed up as "messaging myself" in MeshChat and a
                // phantom self-entry in the node list. Real Meshtastic
                // filters this the same way (NodeDB never adds the local
                // node as a remote peer).
                if (decoded && from == s_node_id) decoded = false;

                if (decoded) {
                    if (!mesh_router_dedup_seen(from, pkt_id)) {
                        mesh_router_dedup_add(from, pkt_id);
                        mesh_router_node_touch(from, (int8_t)rssi, channel_idx);

                        ESP_LOGI(TAG, "rx from=%08lX to=%08lX ch=%d port=%d len=%u rssi=%d snr=%.1f",
                                 (unsigned long)from, (unsigned long)to, channel_idx,
                                 portnum, (unsigned)payload_len, rssi, snr);

                        // Implicit ACK — real Meshtastic firmware always
                        // sends this back for a unicast packet with
                        // want_ack set; without it, every real client
                        // (phone app, another node's own screen) sits
                        // waiting and eventually shows a delivery error,
                        // even though we received and processed the
                        // message fine. Never for broadcasts — only a
                        // packet addressed specifically to us.
                        if (to == s_node_id && want_ack) {
                            mesh_tx_item_t ack;
                            ack.len = mesh_router_encode_ack(ack.wire, sizeof(ack.wire), from, channel_idx, pkt_id);
                            if (ack.len > 0) xQueueSend(s_tx_queue, &ack, 0);
                        }

                        if (portnum == (int)meshtastic_PortNum_TEXT_MESSAGE_APP) {
                            payload[payload_len] = '\0';
                            char title[PURR_NOTIFY_TITLE_LEN];
                            snprintf(title, sizeof(title), "Mesh: %08lX", (unsigned long)from);
                            purr_kernel_notify(title, (const char *)payload, "meshtastic");
                        }

                        // NodeInfo carries the node's real display name — was
                        // previously decoded and thrown away (only rssi/
                        // last-seen were tracked via node_touch() above).
                        if (portnum == (int)meshtastic_PortNum_NODEINFO_APP) {
                            meshtastic_User user = meshtastic_User_init_zero;
                            pb_istream_t us = pb_istream_from_buffer(payload, payload_len);
                            if (pb_decode(&us, meshtastic_User_fields, &user)) {
                                mesh_router_node_set_name(from, user.long_name, user.short_name);
                            }
                        }

                        for (int rcb = 0; rcb < MESH_MAX_RX_CB; rcb++) {
                            if (s_rx_cbs[rcb]) s_rx_cbs[rcb](from, to, channel_idx, portnum, payload, payload_len);
                        }

                        // Relay broadcast packets that still have hops remaining
                        if (to == (uint32_t)MESH_BROADCAST && hop_limit > 0) {
                            mesh_router_relay(raw, (size_t)raw_len);
                        }
                    }
                }
            }
        }

        // ── Periodic NodeInfo announcement ─────────────────────────────────
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        s_last_heartbeat_ms = now_ms;
        if (now_ms - last_announce >= MESH_ANNOUNCE_INTERVAL_MS) {
            last_announce = now_ms;
            uint8_t wire[256];
            size_t  len = mesh_router_encode_nodeinfo(wire, sizeof(wire));
            if (len > 0 && radio && radio->send) {
                mesh_radio_lock();
                radio->send(wire, len);
                mesh_radio_unlock();
                ESP_LOGI(TAG, "nodeinfo broadcast id=%08lX", (unsigned long)s_node_id);
            }
        }

        // ── Outgoing message queue ───────────────────────────────────────
        // The actual transmit happens here, on this task, never on
        // whatever caller queued it (mesh_manager_send_text() is called
        // directly from a UI button press) — RadioLib's transmit() is a
        // blocking call, and at this preset's SF11/BW250kHz it can easily
        // take hundreds of ms of real airtime. Doing that synchronously on
        // cupcake_task used to happen inside its own purr_kernel_ui_lock()-
        // held lv_timer_handler() call, freezing the entire UI for the
        // duration — confirmed live via the frame-time watchdog: a single
        // send produced a 515ms "lv_timer_handler() took 515ms" warning.
        // Draining the whole queue each pass (not just one item) keeps a
        // burst of messages from backing up behind mesh_task()'s own 10ms
        // poll cadence.
        mesh_tx_item_t tx_item;
        while (xQueueReceive(s_tx_queue, &tx_item, 0) == pdTRUE) {
            if (radio && radio->send) {
                mesh_radio_lock();
                esp_err_t ret = radio->send(tx_item.wire, tx_item.len);
                mesh_radio_unlock();
                ESP_LOGI(TAG, "tx len=%u ok=%d", (unsigned)tx_item.len, ret == ESP_OK);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

// Registered with purr_kernel_health_register() below — the kernel's own
// shared watchdog polls this and pushes a purr_kernel_notify() the moment
// it transitions alive<->stale, so a hung/crashed mesh task is surfaced
// instead of silently going dark. Also what the Services app reads for
// Meshtastic's row.
bool mesh_manager_is_alive(void)
{
    if (!s_ready) return false;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return (now_ms - s_last_heartbeat_ms) < MESH_WATCHDOG_STALE_MS;
}

bool mesh_manager_send_text(uint32_t to, int channel_idx, const char *text)
{
    if (!s_ready) return false;
    // Encoding (AES-CTR + memcpy) is cheap and doesn't touch the radio —
    // safe to do right here on the caller's own task (a UI button press).
    // The actual radio->send() does not happen here — it's queued for
    // mesh_task() to perform, since RadioLib's transmit() blocks for real
    // LoRa airtime (hundreds of ms at this preset) and doing that inline
    // on a UI callback froze the whole screen for the duration. Returning
    // true here means "queued for transmission", not "confirmed sent" —
    // see meshtastic.h's doc comment.
    mesh_tx_item_t item;
    item.len = mesh_router_encode_text(item.wire, sizeof(item.wire), to, channel_idx, text);
    if (item.len == 0) { ESP_LOGE(TAG, "encode failed"); return false; }

    if (xQueueSend(s_tx_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "tx queue full — message dropped");
        return false;
    }
    return true;
}

int mesh_manager_channel_count(void) { return mesh_radio_channel_count(); }

bool mesh_manager_channel_name(int idx, char *name_out, size_t name_max)
{
    const mesh_channel_t *ch = mesh_radio_channel_at(idx);
    if (!ch || !name_out) return false;
    strncpy(name_out, ch->name, name_max - 1);
    name_out[name_max - 1] = '\0';
    return true;
}

int mesh_manager_add_channel(const char *name, const uint8_t psk16[16])
{
    return mesh_radio_add_channel(name, psk16);
}

void mesh_manager_add_rx_callback(mesh_rx_cb_t cb) {
    if (!cb) return;
    for (int i = 0; i < MESH_MAX_RX_CB; i++) {
        if (s_rx_cbs[i] == cb) return;         // already registered
        if (!s_rx_cbs[i]) { s_rx_cbs[i] = cb; return; }
    }
    ESP_LOGW(TAG, "rx callback table full (%d) — not registered", MESH_MAX_RX_CB);
}

void mesh_manager_remove_rx_callback(mesh_rx_cb_t cb) {
    for (int i = 0; i < MESH_MAX_RX_CB; i++) {
        if (s_rx_cbs[i] == cb) { s_rx_cbs[i] = NULL; return; }
    }
}
uint32_t mesh_manager_node_id(void)    { return s_node_id; }
int      mesh_manager_node_count(void) { return mesh_router_node_count(); }
bool     mesh_manager_ready(void)      { return s_ready; }

int mesh_manager_node_at(int idx, mesh_node_info_t *out)
{
    const mesh_node_t *n = mesh_router_node_at(idx);
    if (!n || !out) return -1;
    out->id          = n->id;
    out->rssi        = n->rssi;
    out->last_ms     = n->last_ms;
    out->channel_idx = n->channel_idx;
    if (n->long_name[0]) {
        strncpy(out->long_name, n->long_name, sizeof(out->long_name) - 1);
        out->long_name[sizeof(out->long_name) - 1] = '\0';
    } else {
        snprintf(out->long_name, sizeof(out->long_name), "!%08lX", (unsigned long)n->id);
    }
    strncpy(out->short_name, n->short_name, sizeof(out->short_name) - 1);
    out->short_name[sizeof(out->short_name) - 1] = '\0';
    return 0;
}

// ── Module lifecycle ──────────────────────────────────────────────────────────

int mesh_manager_init(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    s_node_id = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16)
              | ((uint32_t)mac[4] <<  8) |  (uint32_t)mac[5];

    s_tx_queue = xQueueCreate(MESH_TX_QUEUE_DEPTH, sizeof(mesh_tx_item_t));
    if (!s_tx_queue) {
        ESP_LOGE(TAG, "failed to create tx queue");
        return -1;
    }

    // No NVS/flash/SD access anywhere in mesh_task()'s own call graph
    // (mesh_radio.c/mesh_router.c audited) — safe on a PSRAM-backed stack,
    // matching app_manager.c's launch_native()/launch_meow() pattern. Must
    // be paired with vTaskDeleteWithCaps() in mesh_manager_deinit() below.
    BaseType_t ret = xTaskCreateWithCaps(mesh_task, "meshtastic", 8192, NULL, 4, &s_task, MALLOC_CAP_SPIRAM);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create mesh task");
        return -1;
    }

    // bt_mgr (device.pcat [flash] priority 2) has already brought up
    // Bluedroid by the time this module (priority 3) loads — safe to
    // register the GATTS companion service here.
    mesh_ble_init();

    purr_kernel_health_register("meshtastic", mesh_manager_is_alive);
    return 0;
}

void mesh_manager_deinit(void)
{
    mesh_ble_deinit();
    // Must match the WithCaps variant used to create this task (PSRAM-backed
    // stack, see mesh_manager_init() above).
    if (s_task) { vTaskDeleteWithCaps(s_task); s_task = NULL; }
    s_ready = false;
}

#else  // !CONFIG_PURR_FEATURE_MESHTASTIC — see this file's top-of-file comment

bool     mesh_manager_is_alive(void)                            { return false; }
bool     mesh_manager_send_text(uint32_t to, int channel_idx, const char *text)
                                                                 { (void)to; (void)channel_idx; (void)text; return false; }
void     mesh_manager_add_rx_callback(mesh_rx_cb_t cb)          { (void)cb; }
void     mesh_manager_remove_rx_callback(mesh_rx_cb_t cb)       { (void)cb; }
uint32_t mesh_manager_node_id(void)                             { return 0; }
int      mesh_manager_node_count(void)                          { return 0; }
bool     mesh_manager_ready(void)                                { return false; }
int      mesh_manager_node_at(int idx, mesh_node_info_t *out)   { (void)idx; (void)out; return -1; }
int      mesh_manager_channel_count(void)                       { return 0; }
bool     mesh_manager_channel_name(int idx, char *name_out, size_t name_max)
                                                                 { (void)idx; (void)name_out; (void)name_max; return false; }
int      mesh_manager_add_channel(const char *name, const uint8_t psk16[16])
                                                                 { (void)name; (void)psk16; return -1; }
int      mesh_manager_init(void)                                { return 0; }
void     mesh_manager_deinit(void)                               {}

#endif  // CONFIG_PURR_FEATURE_MESHTASTIC

// ── Module header ─────────────────────────────────────────────────────────────
#include "../../kernel/core/purr_module.h"

PURR_MODULE_REGISTER(meshtastic) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_SYSTEM,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "meshtastic",
    .version           = "1.0.0",
    .kernel_min        = "0.11.1",
    .kernel_max        = "",
    .provided_catcalls = 0,
    .required_catcalls = 0,   // checked at runtime via purr_kernel_radio(), not this field (see app_manager.c's own comment on the same pattern)
    .init              = mesh_manager_init,
    .deinit            = mesh_manager_deinit,
};
