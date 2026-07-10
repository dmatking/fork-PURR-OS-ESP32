#pragma once
// mesh_radio.h — Meshtastic LONG_FAST radio preset + AES-128-CTR crypto.
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

// Default channel's hash byte, as transmitted in PacketHeader.channel (see
// mesh_router.c's top comment) — Channels::generateHash() computes this as
// xorHash(channel_name) ^ xorHash(psk_bytes). Precomputed by hand for the
// fixed inputs this project supports (name="LongFast", psk=DEFAULT_PSK16 in
// mesh_radio.c): xorHash("LongFast") ^ xorHash(defaultpsk) == 0x08. Confirmed
// against real captured over-the-air packets — every strong-signal capture's
// byte 13 was 0x08. Revisit if multi-channel support is ever added.
#define MESH_CHANNEL_HASH 0x08

// LONG_FAST channel 0 frequency by region ("US" default, "EU", "JP", "CN", "AU").
uint32_t mesh_radio_freq_for_region(const char *region);

// Retunes the already-registered radio catcall to the LONG_FAST preset at
// the given frequency. Does NOT call catcall_radio_t.init() itself — the
// radio driver's own module registration already did that with its default
// config; this only calls set_modulation/set_sync_word/set_frequency/
// set_power to retune it. Returns false if no radio catcall is registered.
bool mesh_radio_apply_preset(uint32_t freq_hz);

// Default channel PSK ("1" shorthand, i.e. channel 0 / LongFast's actual
// out-of-the-box key) — the real 16-byte key used as-is. Meshtastic's own
// Channels.h calls this out explicitly: "16 bytes of random PSK for our
// _public_ default channel that all devices power up on (AES128)" — this
// was previously doubled into a fake 32-byte key and run through AES-256,
// which silently produced a completely different keystream from every real
// Meshtastic node (same bug on both encode and decode, so two PURR nodes
// could still talk to each other — just never to anything real).
void mesh_radio_default_psk(uint8_t key16[16]);

// IV layout: [packet_id as uint64_t LE (8 bytes)][from_node LE (4 bytes)][0x00 * 4]
// Matches Meshtastic's CryptoEngine::initNonce() byte-for-byte.
void mesh_radio_build_iv(uint8_t iv[16], uint32_t pkt_id, uint32_t from_node);

bool mesh_radio_aes_ctr(const uint8_t *in, uint8_t *out, size_t len,
                         const uint8_t key16[16], const uint8_t iv[16]);
