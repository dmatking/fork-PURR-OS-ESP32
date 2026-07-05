#pragma once
// mesh_radio.h — Meshtastic LONG_FAST radio preset + AES-256-CTR crypto.
// Internal to the meshtastic module — not a public API, see meshtastic.h.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// LONG_FAST preset — matches Meshtastic's default channel modulation exactly.
#define MESH_SF         11
#define MESH_BW_HZ      250000
#define MESH_CR         5           // 4/5
#define MESH_SYNC_WORD  0x2B
#define MESH_TX_DBM     20
#define MESH_HOP_LIMIT  3
#define MESH_BROADCAST  0xFFFFFFFFUL

// LONG_FAST channel 0 frequency by region ("US" default, "EU", "JP", "CN", "AU").
uint32_t mesh_radio_freq_for_region(const char *region);

// Retunes the already-registered radio catcall to the LONG_FAST preset at
// the given frequency. Does NOT call catcall_radio_t.init() itself — the
// radio driver's own module registration already did that with its default
// config; this only calls set_modulation/set_sync_word/set_frequency/
// set_power to retune it. Returns false if no radio catcall is registered.
bool mesh_radio_apply_preset(uint32_t freq_hz);

// Default channel PSK ("1" shorthand) expanded to a 32-byte AES-256 key.
void mesh_radio_expand_psk(uint8_t key32[32]);

// IV layout: [packet_id as uint64_t LE (8 bytes)][from_node LE (4 bytes)][0x00 * 4]
void mesh_radio_build_iv(uint8_t iv[16], uint32_t pkt_id, uint32_t from_node);

bool mesh_radio_aes_ctr(const uint8_t *in, uint8_t *out, size_t len,
                         const uint8_t key32[32], const uint8_t iv[16]);
