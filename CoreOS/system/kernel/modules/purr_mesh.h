#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Callback fired on every successfully decoded + decrypted incoming packet.
// portnum matches meshtastic_PortNum values (1=TEXT, 3=POSITION, 4=NODEINFO, ...).
typedef void (*purr_mesh_rx_cb_t)(uint32_t from_node, int portnum,
                                   const uint8_t* payload, size_t len);

// region: "US" (default), "EU", "JP", "CN", "AU" — selects LONG_FAST channel 0 frequency
void     purr_mesh_init(const char* region = nullptr);
bool     purr_mesh_send_text(const char* text);
void     purr_mesh_set_rx_callback(purr_mesh_rx_cb_t cb);
uint32_t purr_mesh_node_id();
int      purr_mesh_node_count();
bool     purr_mesh_ready();
