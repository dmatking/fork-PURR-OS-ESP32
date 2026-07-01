#pragma once
// Meshtastic co-resident process — LoRa targets only (PURR_HAS_MESH)
//
// Runs the Meshtastic mesh stack as a set of FreeRTOS tasks alongside PURR OS.
// Requires PURR_ENABLE_LORA=1. Auto-disabled on CYD (no LoRa hardware).
//
// Radio ownership: KITT's lora_manager yields the SX1262 to mesh_manager on
// start, and reclaims it on stop. Do not call KITT LoRa APIs while mesh is
// running — use the mesh send/recv API instead.
//
// Submodule: CoreOS/components/meshtastic  (see HOWTO.md §Meshtastic)

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ── Lifecycle ─────────────────────────────────────────────────────────────────

// Start the Meshtastic stack. Yields the SX1262 from lora_manager.
// Safe to call at boot (if PURR_MESH_AUTOSTART=1) or from app manager.
// Returns false if already running or radio init fails.
bool mesh_manager_start();

// Stop the Meshtastic stack. Releases SX1262 back to lora_manager.
// Blocks until all mesh tasks have exited (max 3 s).
void mesh_manager_stop();

// True while mesh tasks are running.
bool mesh_manager_running();

// ── Messaging ─────────────────────────────────────────────────────────────────

#define MESH_BROADCAST_ADDR  0xFFFFFFFF

typedef struct {
    uint32_t    from;               // sender node ID
    uint32_t    to;                 // dest node ID (MESH_BROADCAST_ADDR = broadcast)
    char        text[256];          // decoded text payload (null-terminated)
    int8_t      rssi;               // received signal strength
    float       snr;                // signal-to-noise ratio
    uint32_t    timestamp;          // unix epoch from node (0 if unknown)
} mesh_packet_t;

// Send a text message. dest = MESH_BROADCAST_ADDR for broadcast.
// Returns false if mesh not running or send queue full.
bool mesh_manager_send(const char* text, uint32_t dest);

// Poll for a received packet. Returns false if queue empty.
bool mesh_manager_recv(mesh_packet_t* out);

// Flush all queued received packets.
void mesh_manager_recv_flush();

// ── Node database ─────────────────────────────────────────────────────────────

typedef struct {
    uint32_t    id;
    char        long_name[40];
    char        short_name[5];
    int8_t      last_rssi;
    uint32_t    last_heard;         // millis() timestamp
    bool        has_position;
    double      latitude;
    double      longitude;
    float       altitude_m;
    uint8_t     battery_level;      // 0-100, 101 = plugged in
} mesh_node_t;

// Number of nodes currently in the node DB (including self).
int mesh_manager_node_count();

// Get node by index (0-based). Returns false if idx out of range.
bool mesh_manager_get_node(int idx, mesh_node_t* out);

// Get this device's own node ID.
uint32_t mesh_manager_own_id();

// ── Channel / config ──────────────────────────────────────────────────────────

// Set channel name and PSK before calling start(). Persisted to NVS.
// If not called, uses Meshtastic defaults (LongFast / no encryption).
void mesh_manager_set_channel(const char* name, const uint8_t* psk_32bytes);

// Current channel name (null-terminated).
const char* mesh_manager_channel_name();

// ── Status ────────────────────────────────────────────────────────────────────

typedef struct {
    bool        running;
    uint32_t    own_id;
    int         node_count;
    int         packets_rx;
    int         packets_tx;
    int8_t      last_rssi;
    float       last_snr;
    uint32_t    uptime_ms;          // ms since mesh_manager_start()
} mesh_status_t;

void mesh_manager_get_status(mesh_status_t* out);
