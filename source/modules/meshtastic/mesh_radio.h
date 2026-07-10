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

// LONG_FAST channel 0 frequency by region ("US" default, "EU", "JP", "CN", "AU").
uint32_t mesh_radio_freq_for_region(const char *region);

// Retunes the already-registered radio catcall to the LONG_FAST preset at
// the given frequency. Does NOT call catcall_radio_t.init() itself — the
// radio driver's own module registration already did that with its default
// config; this only calls set_modulation/set_sync_word/set_frequency/
// set_power to retune it. Also where the channel table (below) gets its
// one-time setup: channel 0 populated with the fixed LongFast default, then
// any saved custom channels loaded from NVS. Returns false if no radio
// catcall is registered.
bool mesh_radio_apply_preset(uint32_t freq_hz);

// IV layout: [packet_id as uint64_t LE (8 bytes)][from_node LE (4 bytes)][0x00 * 4]
// Matches Meshtastic's CryptoEngine::initNonce() byte-for-byte.
void mesh_radio_build_iv(uint8_t iv[16], uint32_t pkt_id, uint32_t from_node);

bool mesh_radio_aes_ctr(const uint8_t *in, uint8_t *out, size_t len,
                         const uint8_t key16[16], const uint8_t iv[16]);

// ── Channels ─────────────────────────────────────────────────────────────────
// Real Meshtastic supports up to 8 channels (index 0 = primary), each with
// its own name+PSK, selected on the wire by a single hash byte
// (PacketHeader.channel — see mesh_router.c's top comment). Channel 0 is
// always "LongFast" (name="LongFast", the real default 16-byte PSK) —
// fixed at mesh_radio_apply_preset()'s one-time setup, matching this
// project's whole prior single-channel behavior exactly, so every existing
// caller that never thinks about channels keeps working unchanged.
#define MESH_MAX_CHANNELS 8

typedef struct {
    char    name[12];
    uint8_t psk[16];
    uint8_t hash;
    bool    active;
} mesh_channel_t;

// xorHash(name) ^ xorHash(psk) — Channels::generateHash()'s exact algorithm
// (byte-XOR over each buffer, then XOR the two results together). Confirmed
// against real Meshtastic source; this is the same formula that was
// previously only ever hand-computed once for the LongFast default (0x08).
uint8_t mesh_radio_channel_hash(const char *name, const uint8_t psk[16]);

// Adds a channel (name copied/truncated to 11 chars + NUL, psk copied
// verbatim). Returns the new index, or -1 if all 8 slots are full or the
// computed hash collides with an existing active channel (two channels
// with the same on-air hash would be indistinguishable on decode).
// Persists to NVS immediately (namespace "purr_mesh") so it survives reboot.
int mesh_radio_add_channel(const char *name, const uint8_t psk[16]);

int                   mesh_radio_channel_count(void);
const mesh_channel_t *mesh_radio_channel_at(int idx);

// Finds the (first) active channel whose hash matches a received packet's
// channel byte, or -1 if none of our known channels match — used by
// mesh_router_decode() to pick which PSK to try.
int mesh_radio_channel_find_by_hash(uint8_t hash);

// Guards every catcall_radio_t call (send/receive/data_available/rssi/snr)
// against concurrent access. RadioLib has no locking of its own, and this
// module has two independent callers of the radio: mesh_task()'s own RX-
// poll loop (every 10ms, meshtastic_module.c) and anything that sends —
// mesh_manager_send_text() (called directly from a UI button press, i.e. a
// different task entirely) and mesh_router_relay()'s own send (called from
// mesh_task(), but still needs the same lock since it's racing the *other*
// caller). An interleaved send/receive corrupts RadioLib's internal mode-
// tracking state mid-sequence — confirmed live as the root cause of
// intermittent crashes specifically when sending a message while RX
// polling is concurrently running (which it always is). The mutex is
// created once inside mesh_radio_apply_preset() (called once at mesh_task
// startup, before s_ready unblocks any other caller) — every other radio
// catcall call site in this module must be wrapped in these.
void mesh_radio_lock(void);
void mesh_radio_unlock(void);
