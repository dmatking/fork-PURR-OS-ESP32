// purr_mesh.cpp — Meshtastic-compatible mesh stack for PURR OS
// Target: Heltec V3 (SX1262, ESP32-S3)
// Protocol: LONG_FAST — SF11, BW250, CR4/5, sync=0x2B

#ifdef PURR_HAS_MESH

#include "purr_mesh.h"
#include "lora_manager.h"
#include "../kitt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/aes.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <string.h>
#include <stdio.h>

#include <pb_encode.h>
#include <pb_decode.h>
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"

static const char *TAG = "mesh";

extern KITT kitt;

// ── Radio parameters ──────────────────────────────────────────────────────────
#define MESH_SF         11
#define MESH_BW_HZ      250000
#define MESH_CR         5           // 4/5
#define MESH_SYNC_WORD  0x2B
#define MESH_TX_DBM     20
#define MESH_HOP_LIMIT  3
#define MESH_BROADCAST  0xFFFFFFFFUL

// LONG_FAST channel 0 frequency by region
static uint32_t mesh_freq_for_region(const char* region) {
    if (region && strcmp(region, "EU") == 0)  return 869525000UL;
    if (region && strcmp(region, "JP") == 0)  return 923875000UL;
    if (region && strcmp(region, "CN") == 0)  return 470000000UL;
    if (region && strcmp(region, "AU") == 0)  return 916875000UL;
    return 906875000UL;  // US default (also ANZ, TW, KR approximate)
}

// ── Default channel PSK (Meshtastic "1" shorthand → 16-byte key) ──────────────
static const uint8_t DEFAULT_PSK16[16] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
};

// ── Module state ──────────────────────────────────────────────────────────────
static uint32_t          s_node_id    = 0;
static uint32_t          s_packet_seq = 1;
static uint32_t          s_freq_hz    = 906875000UL;
static bool              s_ready      = false;
static purr_mesh_rx_cb_t s_rx_cb      = nullptr;

// ── Dedup ring buffer ─────────────────────────────────────────────────────────
#define DEDUP_SLOTS 32
struct SeenPkt { uint32_t from; uint32_t id; };
static SeenPkt s_seen[DEDUP_SLOTS];
static int     s_seen_head = 0;

static bool dedup_seen(uint32_t from, uint32_t id) {
    for (int i = 0; i < DEDUP_SLOTS; i++)
        if (s_seen[i].from == from && s_seen[i].id == id) return true;
    return false;
}
static void dedup_add(uint32_t from, uint32_t id) {
    s_seen[s_seen_head] = {from, id};
    s_seen_head = (s_seen_head + 1) % DEDUP_SLOTS;
}

// ── Node table ────────────────────────────────────────────────────────────────
#define MAX_NODES 16
struct MeshNode { uint32_t id; int8_t rssi; uint32_t last_ms; };
static MeshNode s_nodes[MAX_NODES];
static int      s_nnode = 0;

static void node_touch(uint32_t id, int8_t rssi) {
    for (int i = 0; i < s_nnode; i++) {
        if (s_nodes[i].id == id) {
            s_nodes[i].rssi    = rssi;
            s_nodes[i].last_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
            return;
        }
    }
    if (s_nnode < MAX_NODES)
        s_nodes[s_nnode++] = {id, rssi, (uint32_t)(esp_timer_get_time() / 1000ULL)};
}

// ── Crypto ────────────────────────────────────────────────────────────────────
static void expand_psk(uint8_t key32[32]) {
    memcpy(key32,      DEFAULT_PSK16, 16);
    memcpy(key32 + 16, DEFAULT_PSK16, 16);
}

// IV layout: [packet_id as uint64_t (8 bytes)][from_node LE (4 bytes)][0x00 * 4]
static void build_iv(uint8_t iv[16], uint32_t pkt_id, uint32_t from_node) {
    memset(iv, 0, 16);
    uint64_t id64 = pkt_id;
    memcpy(iv, &id64, 8);
    memcpy(iv + 8, &from_node, 4);
}

static bool aes_ctr(const uint8_t* in, uint8_t* out, size_t len,
                    const uint8_t key32[32], const uint8_t iv[16]) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (mbedtls_aes_setkey_enc(&ctx, key32, 256) != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }
    uint8_t counter[16], stream[16];
    memcpy(counter, iv, 16);
    size_t nc_off = 0;
    int rc = mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, counter, stream,
                                    const_cast<uint8_t*>(in), out);
    mbedtls_aes_free(&ctx);
    return rc == 0;
}

// ── Packet helpers ────────────────────────────────────────────────────────────

// Encrypt a plaintext Data payload and wrap it in a new MeshPacket.
// Returns wire byte count, or 0 on failure.
static size_t encode_data_packet(uint8_t* wire, size_t wire_max,
                                  uint32_t to, uint8_t hop_limit, uint8_t channel,
                                  const uint8_t* plain, size_t plain_len) {
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
    expand_psk(key32);
    build_iv(iv, pkt.id, pkt.from);
    if (!aes_ctr(plain, cipher, plain_len, key32, iv)) return 0;
    memcpy(pkt.encrypted.bytes, cipher, plain_len);
    pkt.encrypted.size = (pb_size_t)plain_len;

    pb_ostream_t ws = pb_ostream_from_buffer(wire, wire_max);
    return pb_encode(&ws, meshtastic_MeshPacket_fields, &pkt) ? ws.bytes_written : 0;
}

static size_t encode_text_packet(uint8_t* wire, size_t wire_max, const char* text) {
    meshtastic_Data d = meshtastic_Data_init_zero;
    d.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    size_t tlen = strlen(text);
    if (tlen > sizeof(d.payload.bytes)) tlen = sizeof(d.payload.bytes);
    memcpy(d.payload.bytes, text, tlen);
    d.payload.size = (pb_size_t)tlen;

    uint8_t plain[meshtastic_Data_size];
    pb_ostream_t os = pb_ostream_from_buffer(plain, sizeof(plain));
    if (!pb_encode(&os, meshtastic_Data_fields, &d)) return 0;

    return encode_data_packet(wire, wire_max, (uint32_t)MESH_BROADCAST,
                               MESH_HOP_LIMIT, 0, plain, os.bytes_written);
}

static size_t encode_nodeinfo_packet(uint8_t* wire, size_t wire_max) {
    meshtastic_User user = meshtastic_User_init_zero;
    snprintf(user.id,         sizeof(user.id),         "!%08lx",       (unsigned long)s_node_id);
    snprintf(user.long_name,  sizeof(user.long_name),  "PURR-%08lX",   (unsigned long)s_node_id);
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

// Re-broadcast a received packet with hop_limit decremented.
// Does NOT decrypt/re-encrypt — preserves the encrypted payload verbatim.
static void relay_packet(const uint8_t* raw, size_t raw_len) {
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pb_istream_t is = pb_istream_from_buffer(raw, raw_len);
    if (!pb_decode(&is, meshtastic_MeshPacket_fields, &pkt)) return;
    if (pkt.to != (uint32_t)MESH_BROADCAST) return;
    if (pkt.hop_limit == 0) return;

    pkt.hop_limit--;

    uint8_t relay_buf[256];
    pb_ostream_t os = pb_ostream_from_buffer(relay_buf, sizeof(relay_buf));
    if (!pb_encode(&os, meshtastic_MeshPacket_fields, &pkt)) return;

    // Random back-off 0–500 ms to reduce simultaneous relay collisions
    uint32_t delay_ms = esp_random() % 500;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    lora_manager_send(relay_buf, os.bytes_written);
    ESP_LOGI(TAG, "relay from=%08lX id=%lu hops_left=%u delay=%lums",
             (unsigned long)pkt.from, (unsigned long)pkt.id,
             (unsigned)pkt.hop_limit, (unsigned long)delay_ms);
}

// Decode + decrypt raw LoRa bytes.  Returns false on parse/crypto failure.
static bool decode_packet(const uint8_t* raw, size_t raw_len,
                           uint32_t* from, uint32_t* to, uint32_t* pkt_id,
                           uint8_t* hop_limit,
                           int* portnum,
                           uint8_t* payload, size_t* payload_len, size_t payload_max) {
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pb_istream_t is = pb_istream_from_buffer(raw, raw_len);
    if (!pb_decode(&is, meshtastic_MeshPacket_fields, &pkt)) return false;

    *from      = pkt.from;
    *to        = pkt.to;
    *pkt_id    = pkt.id;
    *hop_limit = pkt.hop_limit;

    if (pkt.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        *portnum     = (int)pkt.decoded.portnum;
        size_t n     = pkt.decoded.payload.size < payload_max ? pkt.decoded.payload.size : payload_max;
        memcpy(payload, pkt.decoded.payload.bytes, n);
        *payload_len = n;
        return true;
    }
    if (pkt.which_payload_variant != meshtastic_MeshPacket_encrypted_tag) return false;

    uint8_t key32[32], iv[16], plain[256];
    size_t enc_len = pkt.encrypted.size;
    if (enc_len > sizeof(plain)) return false;
    expand_psk(key32);
    build_iv(iv, pkt.id, pkt.from);
    if (!aes_ctr(pkt.encrypted.bytes, plain, enc_len, key32, iv)) return false;

    meshtastic_Data d = meshtastic_Data_init_zero;
    pb_istream_t ds = pb_istream_from_buffer(plain, enc_len);
    if (!pb_decode(&ds, meshtastic_Data_fields, &d)) return false;

    *portnum     = (int)d.portnum;
    size_t n     = d.payload.size < payload_max ? d.payload.size : payload_max;
    memcpy(payload, d.payload.bytes, n);
    *payload_len = n;
    return true;
}

// ── OLED status ───────────────────────────────────────────────────────────────
static char s_last_from[12] = {0};
static char s_last_text[48] = {0};

static void refresh_display() {
    char l0[32], l1[32];
    snprintf(l0, sizeof(l0), "MESH  %d nodes", s_nnode);
    snprintf(l1, sizeof(l1), "ID:%08lX", (unsigned long)s_node_id);
    kitt.text_print(0, l0);
    kitt.text_print(1, l1);
    if (s_last_from[0]) {
        char l2[32];
        snprintf(l2, sizeof(l2), "<%s", s_last_from);
        kitt.text_print(2, l2);
        kitt.text_print(3, s_last_text);
    } else {
        kitt.text_print(2, "Listening...");
        kitt.text_print(3, "");
    }
}

// ── Mesh task ─────────────────────────────────────────────────────────────────
static void mesh_task(void*) {
    lora_manager_set_spreading_factor(MESH_SF);
    lora_manager_set_bandwidth(MESH_BW_HZ);
    lora_manager_set_coding_rate(MESH_CR);
    lora_manager_set_sync_word(MESH_SYNC_WORD);
    lora_manager_set_frequency(s_freq_hz);
    lora_manager_set_power(MESH_TX_DBM);

    s_ready = true;
    ESP_LOGI(TAG, "ready id=%08lX  %luHz  SF%d BW%dkHz",
             (unsigned long)s_node_id, (unsigned long)s_freq_hz,
             MESH_SF, MESH_BW_HZ / 1000);
    refresh_display();

    // Announce ourselves on the mesh immediately, then every 15 minutes
    uint32_t last_announce = (uint32_t)(esp_timer_get_time() / 1000ULL) - 14 * 60 * 1000UL;

    while (true) {
        // ── RX ────────────────────────────────────────────────────────────────
        if (lora_manager_data_available()) {
            uint8_t raw[256];
            size_t  raw_len = lora_manager_read(raw, sizeof(raw));
            int8_t  rssi    = (int8_t)lora_manager_rssi();
            float   snr     = lora_manager_snr();

            if (raw_len > 0) {
                uint32_t from = 0, to = 0, pkt_id = 0;
                uint8_t  hop_limit = 0;
                int      portnum   = 0;
                uint8_t  payload[240];
                size_t   payload_len = 0;

                if (decode_packet(raw, raw_len, &from, &to, &pkt_id, &hop_limit,
                                  &portnum, payload, &payload_len, sizeof(payload) - 1)) {
                    if (!dedup_seen(from, pkt_id)) {
                        dedup_add(from, pkt_id);
                        node_touch(from, rssi);

                        ESP_LOGI(TAG, "rx from=%08lX to=%08lX port=%d len=%u rssi=%d snr=%.1f",
                                 (unsigned long)from, (unsigned long)to,
                                 portnum, (unsigned)payload_len, rssi, snr);

                        switch (portnum) {
                        case (int)meshtastic_PortNum_TEXT_MESSAGE_APP:
                            payload[payload_len] = '\0';
                            snprintf(s_last_from, sizeof(s_last_from), "%08lX", (unsigned long)from);
                            strncpy(s_last_text, (const char*)payload, sizeof(s_last_text) - 1);
                            s_last_text[sizeof(s_last_text) - 1] = '\0';
                            ESP_LOGI(TAG, "text: %s", s_last_text);
                            break;
                        case (int)meshtastic_PortNum_NODEINFO_APP: {
                            meshtastic_User user = meshtastic_User_init_zero;
                            pb_istream_t us = pb_istream_from_buffer(payload, payload_len);
                            if (pb_decode(&us, meshtastic_User_fields, &user))
                                ESP_LOGI(TAG, "nodeinfo: %s (%s)",
                                         user.long_name, user.short_name);
                            break;
                        }
                        default:
                            break;
                        }

                        if (s_rx_cb) s_rx_cb(from, portnum, payload, payload_len);
                        refresh_display();

                        // Relay broadcast packets that still have hops remaining
                        if (to == (uint32_t)MESH_BROADCAST && hop_limit > 0)
                            relay_packet(raw, raw_len);
                    }
                }
            }
        }

        // ── Periodic NodeInfo announcement ────────────────────────────────────
        if ((uint32_t)(esp_timer_get_time() / 1000ULL) - last_announce >= 15UL * 60UL * 1000UL) {
            last_announce = (uint32_t)(esp_timer_get_time() / 1000ULL);
            uint8_t wire[256];
            size_t  len = encode_nodeinfo_packet(wire, sizeof(wire));
            if (len > 0) {
                lora_manager_send(wire, len);
                ESP_LOGI(TAG, "nodeinfo broadcast id=%08lX", (unsigned long)s_node_id);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── Public API ────────────────────────────────────────────────────────────────
void purr_mesh_init(const char* region) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    s_node_id = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16)
              | ((uint32_t)mac[4] <<  8) |  (uint32_t)mac[5];
    s_freq_hz = mesh_freq_for_region(region);

    memset(s_seen,  0, sizeof(s_seen));
    memset(s_nodes, 0, sizeof(s_nodes));

    xTaskCreatePinnedToCore(mesh_task, "purr_mesh", 8192, nullptr, 4, nullptr, 1);
}

bool purr_mesh_send_text(const char* text) {
    if (!s_ready) return false;
    uint8_t wire[256];
    size_t  len = encode_text_packet(wire, sizeof(wire), text);
    if (len == 0) { ESP_LOGE(TAG, "encode failed"); return false; }
    bool ok = lora_manager_send(wire, len);
    ESP_LOGI(TAG, "tx text len=%u ok=%d", (unsigned)len, ok);
    return ok;
}

void     purr_mesh_set_rx_callback(purr_mesh_rx_cb_t cb) { s_rx_cb = cb; }
uint32_t purr_mesh_node_id()    { return s_node_id; }
int      purr_mesh_node_count() { return s_nnode; }
bool     purr_mesh_ready()      { return s_ready; }

#endif  // PURR_HAS_MESH
