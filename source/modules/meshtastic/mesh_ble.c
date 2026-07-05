// mesh_ble.c — Meshtastic BLE phone-API companion service (see mesh_ble.h
// for the UUID-verification caveat before relying on this for real interop).
//
// Standard ESP-IDF Bluedroid GATTS attribute-table pattern (same shape as
// IDF's own gatt_server_service_table example): one service, three
// characteristics (toradio write, fromradio app-handled read, fromnum
// notify), advertised only when mesh_ble_set_advertising(true) is called
// (Settings' Bluetooth toggle, via bt_mgr).
//
// Known limitation: mesh_rx_cb_t (meshtastic.h) only carries
// (from, portnum, payload, len) — not the packet's original `to`/hop_limit/
// channel — so a forwarded FromRadio.packet always reports `to` as
// broadcast even if the original packet was a direct message. Good enough
// for the phone app to see message content; not a full packet mirror.

#include "mesh_ble.h"
#include "meshtastic.h"
#include "mesh_radio.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_log.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include <string.h>

static const char *TAG = "mesh_ble";

#define MESH_BLE_APP_ID     1   // distinct from bt_mgr's GATTC app_id (0)
#define MESH_BLE_FRAME_MAX  256
#define MESH_BLE_QUEUE_LEN  8

enum {
    IDX_SVC = 0,
    IDX_CHAR_TORADIO_DECL, IDX_CHAR_TORADIO_VAL,
    IDX_CHAR_FROMRADIO_DECL, IDX_CHAR_FROMRADIO_VAL,
    IDX_CHAR_FROMNUM_DECL, IDX_CHAR_FROMNUM_VAL, IDX_CHAR_FROMNUM_CFG,
    MESH_BLE_IDX_NB,
};

static uint16_t      s_handles[MESH_BLE_IDX_NB];
static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static uint16_t      s_conn_id  = 0;
static bool          s_connected = false;
static bool          s_want_advertising = false;

// See mesh_ble.h — reproduced from memory of Meshtastic's public BLE spec,
// byte-reversed (little-endian) from the human-readable UUID strings.
static const uint8_t SVC_UUID[16]       = {0xfd,0xea,0x73,0xe2,0xca,0x5d,0xa8,0x9f,0x1f,0x46,0xa8,0x15,0x18,0xb2,0xa1,0x6b};
static const uint8_t TORADIO_UUID[16]   = {0xe7,0x01,0x44,0x12,0x66,0x78,0xdd,0xa1,0xad,0x4d,0x9e,0x12,0xd2,0x76,0x5c,0xf7};
static const uint8_t FROMRADIO_UUID[16] = {0x02,0x00,0x12,0xac,0x42,0x02,0x78,0xb8,0xed,0x11,0x93,0x49,0x9e,0xe6,0x55,0x2c};
static const uint8_t FROMNUM_UUID[16]   = {0x53,0x44,0xe3,0x47,0x75,0xaa,0x70,0xa6,0x66,0x4f,0x00,0xa8,0x8c,0xa1,0x9d,0xed};

static const uint16_t s_pri_svc_uuid   = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t s_char_decl_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t s_char_cfg_uuid  = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t  s_prop_write  = ESP_GATT_CHAR_PROP_BIT_WRITE;
static const uint8_t  s_prop_read   = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t  s_prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;

static uint8_t s_fromnum_val[4]  = {0, 0, 0, 0};
static uint8_t s_fromnum_cccd[2] = {0, 0};

static const esp_gatts_attr_db_t s_attr_db[MESH_BLE_IDX_NB] = {
    [IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&s_pri_svc_uuid, ESP_GATT_PERM_READ,
         sizeof(SVC_UUID), sizeof(SVC_UUID), (uint8_t *)SVC_UUID}
    },
    [IDX_CHAR_TORADIO_DECL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&s_char_decl_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&s_prop_write}
    },
    [IDX_CHAR_TORADIO_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)TORADIO_UUID, ESP_GATT_PERM_WRITE,
         MESH_BLE_FRAME_MAX, 0, NULL}
    },
    [IDX_CHAR_FROMRADIO_DECL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&s_char_decl_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&s_prop_read}
    },
    // App-handled reads: each read drains one queued FromRadio frame (or
    // 0 bytes if the queue is empty), matching Meshtastic's real protocol.
    [IDX_CHAR_FROMRADIO_VAL] = {
        {ESP_GATT_RSP_BY_APP},
        {ESP_UUID_LEN_128, (uint8_t *)FROMRADIO_UUID, ESP_GATT_PERM_READ,
         MESH_BLE_FRAME_MAX, 0, NULL}
    },
    [IDX_CHAR_FROMNUM_DECL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&s_char_decl_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&s_prop_notify}
    },
    [IDX_CHAR_FROMNUM_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)FROMNUM_UUID, ESP_GATT_PERM_READ,
         sizeof(s_fromnum_val), sizeof(s_fromnum_val), s_fromnum_val}
    },
    [IDX_CHAR_FROMNUM_CFG] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&s_char_cfg_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(s_fromnum_cccd), sizeof(s_fromnum_cccd), s_fromnum_cccd}
    },
};

// ── Outgoing frame queue (mesh RX → phone) ───────────────────────────────────

static uint8_t  s_out_queue[MESH_BLE_QUEUE_LEN][MESH_BLE_FRAME_MAX];
static size_t   s_out_len[MESH_BLE_QUEUE_LEN];
static int      s_out_head = 0, s_out_tail = 0, s_out_count = 0;
static uint32_t s_fromnum = 0;

static void enqueue_frame(const uint8_t *data, size_t len) {
    if (len > MESH_BLE_FRAME_MAX) return;
    if (s_out_count >= MESH_BLE_QUEUE_LEN) {
        // drop oldest — a phone that's fallen behind will re-sync via fromnum
        s_out_head = (s_out_head + 1) % MESH_BLE_QUEUE_LEN;
        s_out_count--;
    }
    memcpy(s_out_queue[s_out_tail], data, len);
    s_out_len[s_out_tail] = len;
    s_out_tail = (s_out_tail + 1) % MESH_BLE_QUEUE_LEN;
    s_out_count++;

    s_fromnum++;
    memcpy(s_fromnum_val, &s_fromnum, sizeof(s_fromnum));
    if (s_connected && s_gatts_if != ESP_GATT_IF_NONE) {
        esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, s_handles[IDX_CHAR_FROMNUM_VAL],
                                     sizeof(s_fromnum_val), s_fromnum_val, false);
    }
}

static bool dequeue_frame(uint8_t *out, size_t *out_len) {
    if (s_out_count == 0) { *out_len = 0; return false; }
    size_t n = s_out_len[s_out_head];
    memcpy(out, s_out_queue[s_out_head], n);
    *out_len = n;
    s_out_head = (s_out_head + 1) % MESH_BLE_QUEUE_LEN;
    s_out_count--;
    return true;
}

// ── Mesh RX → FromRadio ───────────────────────────────────────────────────────

static void on_mesh_rx(uint32_t from_node, int portnum, const uint8_t *payload, size_t len) {
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_packet_tag;
    fr.packet.from = from_node;
    fr.packet.to   = (uint32_t)MESH_BROADCAST;   // see file header note — original `to` isn't available here
    fr.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    fr.packet.decoded.portnum = (meshtastic_PortNum)portnum;
    size_t n = len < sizeof(fr.packet.decoded.payload.bytes) ? len : sizeof(fr.packet.decoded.payload.bytes);
    memcpy(fr.packet.decoded.payload.bytes, payload, n);
    fr.packet.decoded.payload.size = (pb_size_t)n;

    uint8_t wire[MESH_BLE_FRAME_MAX];
    pb_ostream_t os = pb_ostream_from_buffer(wire, sizeof(wire));
    if (pb_encode(&os, meshtastic_FromRadio_fields, &fr)) {
        enqueue_frame(wire, os.bytes_written);
    }
}

// ── ToRadio write → mesh TX ───────────────────────────────────────────────────

static void handle_toradio_write(const uint8_t *data, size_t len) {
    meshtastic_ToRadio tr = meshtastic_ToRadio_init_zero;
    pb_istream_t is = pb_istream_from_buffer(data, len);
    if (!pb_decode(&is, meshtastic_ToRadio_fields, &tr)) {
        ESP_LOGW(TAG, "toradio decode failed");
        return;
    }
    if (tr.which_payload_variant != meshtastic_ToRadio_packet_tag) return;
    if (tr.packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag) return;
    if (tr.packet.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP) return;

    char text[241];
    size_t n = tr.packet.decoded.payload.size < sizeof(text) - 1 ? tr.packet.decoded.payload.size : sizeof(text) - 1;
    memcpy(text, tr.packet.decoded.payload.bytes, n);
    text[n] = '\0';

    uint32_t to = tr.packet.to ? tr.packet.to : (uint32_t)MESH_BROADCAST;
    mesh_manager_send_text(to, text);
}

// ── GATTS callback ────────────────────────────────────────────────────────────

static void gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        s_gatts_if = gatts_if;
        esp_ble_gap_set_device_name("PURR-Mesh");
        esp_ble_gatts_create_attr_tab(s_attr_db, gatts_if, MESH_BLE_IDX_NB, 0);
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status != ESP_GATT_OK || param->add_attr_tab.num_handle != MESH_BLE_IDX_NB) {
            ESP_LOGE(TAG, "attr table creation failed, status=%d num=%d",
                      param->add_attr_tab.status, param->add_attr_tab.num_handle);
            break;
        }
        memcpy(s_handles, param->add_attr_tab.handles, sizeof(s_handles));
        esp_ble_gatts_start_service(s_handles[IDX_SVC]);
        break;

    case ESP_GATTS_CONNECT_EVT:
        s_conn_id   = param->connect.conn_id;
        s_connected = true;
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        s_connected = false;
        if (s_want_advertising) {
            static esp_ble_adv_params_t adv_params = {
                .adv_int_min = 0x20, .adv_int_max = 0x40,
                .adv_type = ADV_TYPE_IND, .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
                .channel_map = ADV_CHNL_ALL, .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
            };
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;

    case ESP_GATTS_READ_EVT: {
        if (param->read.handle != s_handles[IDX_CHAR_FROMRADIO_VAL]) break;
        esp_gatt_rsp_t rsp = { 0 };
        rsp.attr_value.handle = param->read.handle;
        size_t out_len = 0;
        dequeue_frame(rsp.attr_value.value, &out_len);
        rsp.attr_value.len = (uint16_t)out_len;
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        break;
    }

    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle == s_handles[IDX_CHAR_TORADIO_VAL]) {
            handle_toradio_write(param->write.value, param->write.len);
        }
        break;

    default:
        break;
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void mesh_ble_set_advertising(bool on) {
    s_want_advertising = on;
    if (on) {
        static esp_ble_adv_data_t adv_data = {
            .set_scan_rsp = false, .include_name = true, .include_txpower = false,
            .min_interval = 0x20, .max_interval = 0x40, .appearance = 0,
            .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
        };
        adv_data.service_uuid_len = sizeof(SVC_UUID);
        adv_data.p_service_uuid   = (uint8_t *)SVC_UUID;
        // Not waiting for ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT before
        // starting advertising (the stricter/more correct pattern) — bt_mgr.c
        // already owns the one GAP callback registration Bluedroid allows,
        // so this file doesn't have its own event hook to wait on. Both
        // calls are serialized through the same HCI command queue in
        // practice, which is why this simplification is common in simpler
        // BLE example code, but it is a known race vs. the fully-correct flow.
        esp_ble_gap_config_adv_data(&adv_data);
        static esp_ble_adv_params_t adv_params = {
            .adv_int_min = 0x20, .adv_int_max = 0x40,
            .adv_type = ADV_TYPE_IND, .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .channel_map = ADV_CHNL_ALL, .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        };
        esp_ble_gap_start_advertising(&adv_params);
    } else {
        esp_ble_gap_stop_advertising();
    }
}

int mesh_ble_init(void) {
    esp_ble_gatts_register_callback(gatts_cb);
    esp_ble_gatts_app_register(MESH_BLE_APP_ID);
    mesh_manager_add_rx_callback(on_mesh_rx);
    ESP_LOGI(TAG, "init complete");
    return 0;
}

void mesh_ble_deinit(void) {
    mesh_manager_remove_rx_callback(on_mesh_rx);
    mesh_ble_set_advertising(false);
}
