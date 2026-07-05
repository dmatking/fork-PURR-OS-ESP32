// mesh_router.c — packet encode/decode, flood routing, dedup, node table.
// Ported from PURR-OS-0.11/CoreOS/system/kernel/modules/purr_mesh.cpp — the
// wire format is nanopb-encoded meshtastic_MeshPacket directly (ciphertext
// in pkt.encrypted.bytes, IV derived from the packet's own plaintext id/from
// fields), NOT the [4B magic][16B IV][ciphertext] framing described in the
// old archived plan doc — that doc was an abbreviated/inaccurate summary of
// this actual, working code.
//
// relay_packet() is the one place this talks to the radio directly (a
// verbatim re-send of an already-encoded packet, not a new encode), via
// purr_kernel_radio() instead of the old lora_manager singleton.

#include "mesh_router.h"
#include "mesh_radio.h"
#include "../../kernel/core/purr_kernel.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "mesh_router";

static uint32_t s_node_id    = 0;
static uint32_t s_packet_seq = 1;

void mesh_router_init(uint32_t node_id)
{
    s_node_id = node_id;
    s_packet_seq = 1;
}

// ── Dedup ring buffer ─────────────────────────────────────────────────────────

#define DEDUP_SLOTS 32
typedef struct { uint32_t from; uint32_t id; } seen_pkt_t;
static seen_pkt_t s_seen[DEDUP_SLOTS];
static int        s_seen_head = 0;

bool mesh_router_dedup_seen(uint32_t from, uint32_t id)
{
    for (int i = 0; i < DEDUP_SLOTS; i++)
        if (s_seen[i].from == from && s_seen[i].id == id) return true;
    return false;
}

void mesh_router_dedup_add(uint32_t from, uint32_t id)
{
    s_seen[s_seen_head].from = from;
    s_seen[s_seen_head].id   = id;
    s_seen_head = (s_seen_head + 1) % DEDUP_SLOTS;
}

// ── Node table ────────────────────────────────────────────────────────────────

#define MAX_NODES 16
static mesh_node_t s_nodes[MAX_NODES];
static int         s_nnode = 0;

void mesh_router_node_touch(uint32_t id, int8_t rssi)
{
    for (int i = 0; i < s_nnode; i++) {
        if (s_nodes[i].id == id) {
            s_nodes[i].rssi    = rssi;
            s_nodes[i].last_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
            return;
        }
    }
    if (s_nnode < MAX_NODES) {
        // long_name/short_name start empty (static array, zero-initialized)
        // until a NodeInfo packet fills them in via node_set_name().
        s_nodes[s_nnode].id      = id;
        s_nodes[s_nnode].rssi    = rssi;
        s_nodes[s_nnode].last_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        s_nnode++;
    }
}

void mesh_router_node_set_name(uint32_t id, const char *long_name, const char *short_name)
{
    for (int i = 0; i < s_nnode; i++) {
        if (s_nodes[i].id == id) {
            if (long_name)  strncpy(s_nodes[i].long_name,  long_name,  sizeof(s_nodes[i].long_name) - 1);
            if (short_name) strncpy(s_nodes[i].short_name, short_name, sizeof(s_nodes[i].short_name) - 1);
            return;
        }
    }
    // mesh_task() always calls mesh_router_node_touch() first for every
    // decoded packet (including this NodeInfo one) — the node should already
    // exist by the time this runs, so there's nothing to do if not.
}

int mesh_router_node_count(void) { return s_nnode; }

const mesh_node_t *mesh_router_node_at(int idx)
{
    if (idx < 0 || idx >= s_nnode) return NULL;
    return &s_nodes[idx];
}

// ── Packet helpers ────────────────────────────────────────────────────────────

// Encrypt a plaintext Data payload and wrap it in a new MeshPacket.
// Returns wire byte count, or 0 on failure.
static size_t encode_data_packet(uint8_t *wire, size_t wire_max,
                                  uint32_t to, uint8_t hop_limit, uint8_t channel,
                                  const uint8_t *plain, size_t plain_len)
{
    if (plain_len > 233) return 0;  // Meshtastic max Data payload

    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.from                  = s_node_id;
    pkt.to                    = to;
    pkt.id                    = s_packet_seq++;
    pkt.hop_limit             = hop_limit;
    pkt.hop_start             = hop_limit;
    pkt.channel               = channel;
    pkt.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;

    uint8_t key32[32], iv[16], cipher[256];
    mesh_radio_expand_psk(key32);
    mesh_radio_build_iv(iv, pkt.id, pkt.from);
    if (!mesh_radio_aes_ctr(plain, cipher, plain_len, key32, iv)) return 0;
    memcpy(pkt.encrypted.bytes, cipher, plain_len);
    pkt.encrypted.size = (pb_size_t)plain_len;

    pb_ostream_t ws = pb_ostream_from_buffer(wire, wire_max);
    return pb_encode(&ws, meshtastic_MeshPacket_fields, &pkt) ? ws.bytes_written : 0;
}

size_t mesh_router_encode_text(uint8_t *wire, size_t wire_max, uint32_t to, const char *text)
{
    meshtastic_Data d = meshtastic_Data_init_zero;
    d.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    size_t tlen = strlen(text);
    if (tlen > sizeof(d.payload.bytes)) tlen = sizeof(d.payload.bytes);
    memcpy(d.payload.bytes, text, tlen);
    d.payload.size = (pb_size_t)tlen;

    uint8_t plain[meshtastic_Data_size];
    pb_ostream_t os = pb_ostream_from_buffer(plain, sizeof(plain));
    if (!pb_encode(&os, meshtastic_Data_fields, &d)) return 0;

    return encode_data_packet(wire, wire_max, to,
                               MESH_HOP_LIMIT, 0, plain, os.bytes_written);
}

size_t mesh_router_encode_nodeinfo(uint8_t *wire, size_t wire_max)
{
    meshtastic_User user = meshtastic_User_init_zero;
    snprintf(user.id,         sizeof(user.id),         "!%08lx",     (unsigned long)s_node_id);
    snprintf(user.long_name,  sizeof(user.long_name),  "PURR-%08lX", (unsigned long)s_node_id);
    snprintf(user.short_name, sizeof(user.short_name), "PRR");
    user.hw_model = meshtastic_HardwareModel_HELTEC_V3;

    uint8_t user_bytes[meshtastic_User_size];
    pb_ostream_t us = pb_ostream_from_buffer(user_bytes, sizeof(user_bytes));
    if (!pb_encode(&us, meshtastic_User_fields, &user)) return 0;

    meshtastic_Data d = meshtastic_Data_init_zero;
    d.portnum = meshtastic_PortNum_NODEINFO_APP;
    memcpy(d.payload.bytes, user_bytes, us.bytes_written);
    d.payload.size = (pb_size_t)us.bytes_written;

    uint8_t plain[meshtastic_Data_size];
    pb_ostream_t ds = pb_ostream_from_buffer(plain, sizeof(plain));
    if (!pb_encode(&ds, meshtastic_Data_fields, &d)) return 0;

    return encode_data_packet(wire, wire_max, (uint32_t)MESH_BROADCAST,
                               MESH_HOP_LIMIT, 0, plain, ds.bytes_written);
}

void mesh_router_relay(const uint8_t *raw, size_t raw_len)
{
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pb_istream_t is = pb_istream_from_buffer(raw, raw_len);
    if (!pb_decode(&is, meshtastic_MeshPacket_fields, &pkt)) return;
    if (pkt.to != (uint32_t)MESH_BROADCAST) return;
    if (pkt.hop_limit == 0) return;

    pkt.hop_limit--;

    uint8_t relay_buf[256];
    pb_ostream_t os = pb_ostream_from_buffer(relay_buf, sizeof(relay_buf));
    if (!pb_encode(&os, meshtastic_MeshPacket_fields, &pkt)) return;

    // Random back-off 0-500ms to reduce simultaneous relay collisions
    uint32_t delay_ms = esp_random() % 500;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    const catcall_radio_t *radio = purr_kernel_radio();
    if (radio && radio->send) radio->send(relay_buf, os.bytes_written);

    ESP_LOGI(TAG, "relay from=%08lX id=%lu hops_left=%u delay=%lums",
             (unsigned long)pkt.from, (unsigned long)pkt.id,
             (unsigned)pkt.hop_limit, (unsigned long)delay_ms);
}

bool mesh_router_decode(const uint8_t *raw, size_t raw_len,
                         uint32_t *from, uint32_t *to, uint32_t *pkt_id,
                         uint8_t *hop_limit, int *portnum,
                         uint8_t *payload, size_t *payload_len, size_t payload_max)
{
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pb_istream_t is = pb_istream_from_buffer(raw, raw_len);
    if (!pb_decode(&is, meshtastic_MeshPacket_fields, &pkt)) return false;

    *from      = pkt.from;
    *to        = pkt.to;
    *pkt_id    = pkt.id;
    *hop_limit = pkt.hop_limit;

    if (pkt.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        *portnum = (int)pkt.decoded.portnum;
        size_t n = pkt.decoded.payload.size < payload_max ? pkt.decoded.payload.size : payload_max;
        memcpy(payload, pkt.decoded.payload.bytes, n);
        *payload_len = n;
        return true;
    }
    if (pkt.which_payload_variant != meshtastic_MeshPacket_encrypted_tag) return false;

    uint8_t key32[32], iv[16], plain[256];
    size_t enc_len = pkt.encrypted.size;
    if (enc_len > sizeof(plain)) return false;
    mesh_radio_expand_psk(key32);
    mesh_radio_build_iv(iv, pkt.id, pkt.from);
    if (!mesh_radio_aes_ctr(pkt.encrypted.bytes, plain, enc_len, key32, iv)) return false;

    meshtastic_Data d = meshtastic_Data_init_zero;
    pb_istream_t ds = pb_istream_from_buffer(plain, enc_len);
    if (!pb_decode(&ds, meshtastic_Data_fields, &d)) return false;

    *portnum = (int)d.portnum;
    size_t n = d.payload.size < payload_max ? d.payload.size : payload_max;
    memcpy(payload, d.payload.bytes, n);
    *payload_len = n;
    return true;
}
