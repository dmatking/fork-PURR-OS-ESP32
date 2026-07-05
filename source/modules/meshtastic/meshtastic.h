#pragma once
// meshtastic.h — public API for PURR OS's Meshtastic-compatible mesh module.
//
// Self-contained PURR_MOD_SYSTEM module: owns radio preset config, AES
// encryption, packet encode/decode, flood routing, dedup, and the node
// table internally. Talks to the radio exclusively through
// purr_kernel_radio()'s catcall_radio_t — never touches a specific radio
// chip directly, so it works with any registered radio driver.
//
// Ported from PURR-OS-0.11/CoreOS/system/kernel/modules/purr_mesh.{cpp,h}.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Callback fired on every successfully decoded + decrypted incoming packet.
// portnum matches meshtastic_PortNum values (1=TEXT, 3=POSITION, 4=NODEINFO, ...).
typedef void (*mesh_rx_cb_t)(uint32_t from_node, int portnum,
                              const uint8_t *payload, size_t len);

// PURR_MOD_SYSTEM init()/deinit() — called by the kernel module loader.
// init() just creates the mesh task and returns immediately; the task
// itself waits for the radio catcall to be registered before doing anything.
int  mesh_manager_init(void);
void mesh_manager_deinit(void);

// Encrypts and sends a TEXT_MESSAGE_APP packet. `to` is a destination node
// ID for a private direct message, or MESH_BROADCAST (mesh_radio.h) for the
// shared channel everyone hears. Returns false if the radio isn't ready yet
// or encoding/send failed.
bool mesh_manager_send_text(uint32_t to, const char *text);

// Register/unregister a callback for every incoming decoded packet (any
// portnum) — up to MESH_MAX_RX_CB subscribers at once (e.g. MeshChat and a
// Bluetooth companion-app bridge can both observe RX independently).
#define MESH_MAX_RX_CB 4
void mesh_manager_add_rx_callback(mesh_rx_cb_t cb);
void mesh_manager_remove_rx_callback(mesh_rx_cb_t cb);

uint32_t mesh_manager_node_id(void);
int      mesh_manager_node_count(void);
bool     mesh_manager_ready(void);

// True if the mesh task's own loop has stamped its heartbeat within the
// last few seconds — registered with purr_kernel_health_register() so the
// kernel's shared watchdog can catch a hung/crashed mesh task, and read
// directly by the Services app to show Meshtastic's live status.
bool     mesh_manager_is_alive(void);

// A known mesh node's display name (once heard via NodeInfo) + signal info.
typedef struct {
    uint32_t id;
    char     long_name[40];   // "!%08lX"-formatted node ID if never heard
    char     short_name[8];
    int8_t   rssi;
    uint32_t last_ms;         // esp_timer_get_time()/1000 at last packet heard
} mesh_node_info_t;

// Enumerate known nodes by index (0..mesh_manager_node_count()-1). Returns
// 0 and fills *out on success, -1 if idx is out of range.
int mesh_manager_node_at(int idx, mesh_node_info_t *out);

#ifdef __cplusplus
}
#endif
