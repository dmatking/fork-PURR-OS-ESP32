#include "mesh_manager.h"

#ifdef PURR_HAS_MESH

#include "esp_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <string.h>

// ── Meshtastic includes ───────────────────────────────────────────────────────
// Requires CoreOS/components/meshtastic submodule.
// See HOWTO.md §Meshtastic for clone instructions.
//
// Key Meshtastic entry points used here:
//   MeshService::instance()        — singleton mesh orchestrator
//   MeshService::init()            — hardware init (calls radio init internally)
//   MeshService::sendText()        — send a text packet
//   MeshService::shutdown()        — stop all mesh tasks
//   nodeDB                         — global NodeDB instance
//   NodeDB::getNumMeshNodes()
//   NodeDB::getMeshNodeByIndex()
//   RadioInterface::activeReceive  — rx packet callback hook
//
// RadioInterface / SX1262Interface uses RadioLib internally. PURR OS's
// lora_manager also drives RadioLib — they CANNOT both hold the radio.
// mesh_manager_start() calls lora_manager_yield() before MeshService::init(),
// and mesh_manager_stop() calls lora_manager_reclaim() after shutdown().

#include "MeshService.h"        // from components/meshtastic/src/
#include "NodeDB.h"             // from components/meshtastic/src/
#include "configuration.h"      // Meshtastic config / channel structs
#include "PowerFSM.h"           // to suppress Meshtastic's sleep logic

// ── Forward declarations from lora_manager ───────────────────────────────────
extern "C" {
    void lora_manager_yield();    // release SX1262 to mesh stack
    void lora_manager_reclaim();  // re-init SX1262 for PURR use
}

static const char *TAG = "mesh";

// ── Internal state ────────────────────────────────────────────────────────────

static bool        s_running      = false;
static uint32_t    s_start_ms     = 0;
static int         s_packets_rx   = 0;
static int         s_packets_tx   = 0;
static int8_t      s_last_rssi    = 0;
static float       s_last_snr     = 0.0f;

static QueueHandle_t s_rx_queue   = nullptr;
static constexpr int RX_QUEUE_LEN = 16;

static char        s_channel_name[32] = "LongFast";
static uint8_t     s_channel_psk[32]  = {};   // all-zero = no encryption

// ── Meshtastic receive hook ───────────────────────────────────────────────────
// Called by Meshtastic's Router when a decoded text packet arrives.
// Runs in the Meshtastic receive task context.
static void on_mesh_receive(const meshtastic_MeshPacket* p) {
    if (!p || !s_rx_queue) return;
    if (p->which_payload_variant != meshtastic_MeshPacket_decoded_tag) return;
    if (p->decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP) return;

    mesh_packet_t pkt = {};
    pkt.from      = p->from;
    pkt.to        = p->to;
    pkt.rssi      = (int8_t)p->rx_rssi;
    pkt.snr       = p->rx_snr;
    pkt.timestamp = p->rx_time;
    strncpy(pkt.text, (const char*)p->decoded.payload.bytes,
            min((size_t)p->decoded.payload.size, sizeof(pkt.text) - 1));

    s_last_rssi = pkt.rssi;
    s_last_snr  = pkt.snr;
    s_packets_rx++;

    xQueueSendToBack(s_rx_queue, &pkt, 0);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

bool mesh_manager_start() {
    if (s_running) {
        ESP_LOGW(TAG, "already running");
        return false;
    }

    ESP_LOGI(TAG, "starting - yielding radio from lora_manager");
    lora_manager_yield();

    if (!s_rx_queue) {
        s_rx_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(mesh_packet_t));
        if (!s_rx_queue) {
            ESP_LOGE(TAG, "rx queue alloc failed");
            lora_manager_reclaim();
            return false;
        }
    }

    // Apply channel config before init
    // TODO: wire s_channel_name / s_channel_psk into Meshtastic's
    //       config.lora.channel_num and channel.settings.psk once
    //       the config API is confirmed against the submodule version.

    // Suppress Meshtastic's power-save / sleep FSM — PURR OS owns sleep.
    powerFSM.trigger(EVENT_BOOT);

    // Init the Meshtastic service. This starts its internal FreeRTOS tasks
    // (meshTask, radioTask, etc.) and opens the SX1262 via RadioLib.
    meshService.init();

    // Register our receive hook
    // TODO: Meshtastic exposes a callback registration API in later builds;
    //       earlier versions require patching Router.cpp to call on_mesh_receive.
    //       See HOWTO.md §Meshtastic for patch instructions.

    s_running   = true;
    s_start_ms  = (uint32_t)(esp_timer_get_time() / 1000ULL);
    s_packets_rx = s_packets_tx = 0;

    ESP_LOGI(TAG, "up - own ID 0x%08lX  channel '%s'",
             (unsigned long)mesh_manager_own_id(), s_channel_name);
    return true;
}

void mesh_manager_stop() {
    if (!s_running) return;

    ESP_LOGI(TAG, "stopping");
    meshService.shutdown();

    // Give tasks a moment to exit before reclaiming the radio
    vTaskDelay(pdMS_TO_TICKS(500));

    lora_manager_reclaim();
    s_running = false;
    ESP_LOGI(TAG, "stopped - radio returned to lora_manager");
}

bool mesh_manager_running() {
    return s_running;
}

// ── Messaging ─────────────────────────────────────────────────────────────────

bool mesh_manager_send(const char* text, uint32_t dest) {
    if (!s_running || !text) return false;

    // meshService.sendText() broadcasts if dest == NODENUM_BROADCAST
    uint32_t mesh_dest = (dest == MESH_BROADCAST_ADDR)
                         ? NODENUM_BROADCAST
                         : dest;

    meshService.sendText(text, mesh_dest);
    s_packets_tx++;
    return true;
}

bool mesh_manager_recv(mesh_packet_t* out) {
    if (!s_rx_queue || !out) return false;
    return xQueueReceive(s_rx_queue, out, 0) == pdTRUE;
}

void mesh_manager_recv_flush() {
    if (!s_rx_queue) return;
    mesh_packet_t discard;
    while (xQueueReceive(s_rx_queue, &discard, 0) == pdTRUE) {}
}

// ── Node database ─────────────────────────────────────────────────────────────

int mesh_manager_node_count() {
    if (!s_running) return 0;
    return nodeDB->getNumMeshNodes();
}

bool mesh_manager_get_node(int idx, mesh_node_t* out) {
    if (!s_running || !out) return false;
    if (idx < 0 || idx >= nodeDB->getNumMeshNodes()) return false;

    meshtastic_NodeInfoLite* n = nodeDB->getMeshNodeByIndex(idx);
    if (!n) return false;

    out->id          = n->num;
    out->last_rssi   = (int8_t)n->snr;   // snr used as proxy; RSSI not stored per-node
    out->last_heard  = n->last_heard;
    out->battery_level = n->device_metrics.battery_level;

    strncpy(out->long_name,  n->user.long_name,  sizeof(out->long_name)  - 1);
    strncpy(out->short_name, n->user.short_name, sizeof(out->short_name) - 1);

    if (n->has_position) {
        out->has_position = true;
        out->latitude     = n->position.latitude_i  * 1e-7;
        out->longitude    = n->position.longitude_i * 1e-7;
        out->altitude_m   = (float)n->position.altitude;
    }

    return true;
}

uint32_t mesh_manager_own_id() {
    if (!s_running) return 0;
    return nodeDB->getNodeNum();
}

// ── Channel / config ──────────────────────────────────────────────────────────

void mesh_manager_set_channel(const char* name, const uint8_t* psk_32bytes) {
    if (s_running) {
        ESP_LOGW(TAG, "set_channel called while running - restart required");
        return;
    }
    if (name)        strncpy(s_channel_name, name, sizeof(s_channel_name) - 1);
    if (psk_32bytes) memcpy(s_channel_psk, psk_32bytes, 32);
}

const char* mesh_manager_channel_name() {
    return s_channel_name;
}

// ── Status ────────────────────────────────────────────────────────────────────

void mesh_manager_get_status(mesh_status_t* out) {
    if (!out) return;
    out->running    = s_running;
    out->own_id     = mesh_manager_own_id();
    out->node_count = mesh_manager_node_count();
    out->packets_rx = s_packets_rx;
    out->packets_tx = s_packets_tx;
    out->last_rssi  = s_last_rssi;
    out->last_snr   = s_last_snr;
    out->uptime_ms  = s_running ? ((uint32_t)(esp_timer_get_time() / 1000ULL) - s_start_ms) : 0;
}

#else  // !PURR_HAS_MESH — stub out everything so callers compile on all targets

bool         mesh_manager_start()                              { return false; }
void         mesh_manager_stop()                               {}
bool         mesh_manager_running()                            { return false; }
bool         mesh_manager_send(const char*, uint32_t)          { return false; }
bool         mesh_manager_recv(mesh_packet_t*)                 { return false; }
void         mesh_manager_recv_flush()                         {}
int          mesh_manager_node_count()                         { return 0; }
bool         mesh_manager_get_node(int, mesh_node_t*)          { return false; }
uint32_t     mesh_manager_own_id()                             { return 0; }
void         mesh_manager_set_channel(const char*, const uint8_t*) {}
const char*  mesh_manager_channel_name()                       { return ""; }
void         mesh_manager_get_status(mesh_status_t* o)         { if (o) *o = {}; }

#endif // PURR_HAS_MESH
