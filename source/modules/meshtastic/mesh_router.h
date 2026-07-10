#pragma once
// mesh_router.h — packet encode/decode, flood routing, dedup, node table.
// Internal to the meshtastic module — not a public API, see meshtastic.h.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Must be called once (with this node's ID) before any other function here.
void mesh_router_init(uint32_t node_id);

// Encodes a TEXT_MESSAGE_APP packet, encrypted, ready to hand to
// catcall_radio_t.send(). `to` is a destination node ID, or MESH_BROADCAST
// (mesh_radio.h) for the shared channel. `channel_idx` selects which
// channel's PSK/hash to encode with (mesh_radio.h's channel table).
// Returns wire byte count, or 0 on failure.
size_t mesh_router_encode_text(uint8_t *wire, size_t wire_max, uint32_t to, int channel_idx, const char *text);

// Encodes a broadcast NODEINFO_APP packet (this node's User info).
size_t mesh_router_encode_nodeinfo(uint8_t *wire, size_t wire_max);

// Encodes an implicit-ACK ROUTING_APP packet (error_reason=NONE) addressed
// back to `to` on `channel_idx` (must be the same channel the original
// packet arrived on — the sender only knows how to decode with that
// channel's PSK), with `request_id` set to the original packet's id —
// this is what real Meshtastic firmware auto-sends whenever it receives a
// unicast packet with want_ack set, and what every real client (the phone
// app, another node's OLED) is waiting on before it'll show a message as
// delivered instead of erroring out. Returns wire byte count, or 0 on
// failure.
size_t mesh_router_encode_ack(uint8_t *wire, size_t wire_max, uint32_t to, int channel_idx, uint32_t request_id);

// Re-broadcasts a received raw packet with hop_limit decremented, after a
// random 0-500ms backoff. Does NOT decrypt/re-encrypt — the encrypted
// payload is preserved verbatim, only the plaintext header changes.
void mesh_router_relay(const uint8_t *raw, size_t raw_len);

// Decodes + decrypts raw LoRa bytes. Returns false on parse/crypto failure,
// including "the packet's channel-hash byte doesn't match any channel we
// know" — that's an expected, silent case (someone else's private room),
// not logged as a warning the way real decode failures are.
// *want_ack reflects the header's WANT_ACK flag bit — mesh_task() uses this
// to decide whether to send an implicit ACK back (see
// mesh_router_encode_ack()) once the packet is confirmed addressed to us.
// *channel_idx is which of our configured channels matched.
bool mesh_router_decode(const uint8_t *raw, size_t raw_len,
                         uint32_t *from, uint32_t *to, uint32_t *pkt_id,
                         uint8_t *hop_limit, bool *want_ack, int *channel_idx, int *portnum,
                         uint8_t *payload, size_t *payload_len, size_t payload_max);

// Dedup ring buffer (32 slots, keyed by from+packet_id).
bool mesh_router_dedup_seen(uint32_t from, uint32_t id);
void mesh_router_dedup_add(uint32_t from, uint32_t id);

// Node table (16 nodes, tracks RSSI, last-seen time, and — once a NodeInfo
// packet has been heard from that node — its display name).
typedef struct {
    uint32_t id;
    char     long_name[40];
    char     short_name[8];
    int8_t   rssi;
    uint32_t last_ms;
    int      channel_idx;   // channel this node was last heard on — DMs to
                             // this node are sent back on this same channel
} mesh_node_t;

// channel_idx: which channel this packet was heard on (from
// mesh_router_decode()'s output) — updates the node's "home channel" for
// DMing, since a node heard on a private room's channel can't be reached
// on the primary channel's PSK.
void mesh_router_node_touch(uint32_t id, int8_t rssi, int channel_idx);

// Called when a NODEINFO_APP packet is decoded (see mesh_task()'s RX loop in
// meshtastic_module.c) — fills in the display name for a node already
// present via mesh_router_node_touch(). No-op if the node isn't tracked yet.
void mesh_router_node_set_name(uint32_t id, const char *long_name, const char *short_name);

int  mesh_router_node_count(void);

// Enumerate the node table by index (0..mesh_router_node_count()-1). Returns
// NULL out of range. The returned pointer is only valid until the next
// mesh_router_node_touch()/mesh_router_node_set_name() call.
const mesh_node_t *mesh_router_node_at(int idx);
