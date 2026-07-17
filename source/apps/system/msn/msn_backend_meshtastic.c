// msn_backend_meshtastic.c — msn_backend_t implementation wrapping
// meshtastic.h. Contacts are addressed by 32-bit node id upstream, but the
// unified msn_backend_t interface addresses everything by table index —
// this file owns the index<->node-id translation, replicating meshchat.c's
// original find_buddy_idx()-by-scan approach (Meshtastic's own API has no
// id-to-index lookup either, so this isn't a regression, just relocated).

#include "msn_backend.h"
#include "meshtastic.h"
#include "meshtastic/portnums.pb.h"
#include "purr_kernel.h"
#include <string.h>
#include <stdio.h>

static bool backend_ready(void)    { return mesh_manager_ready(); }
static bool backend_is_alive(void) { return mesh_manager_is_alive(); }

static int backend_contact_count(void) { return mesh_manager_node_count(); }

static bool backend_contact_at(int idx, msn_contact_t *out) {
    mesh_node_info_t info;
    if (mesh_manager_node_at(idx, &info) != 0) return false;

    snprintf(out->id_str, sizeof(out->id_str), "%08lX", (unsigned long)info.id);
    strncpy(out->name, info.long_name, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = 0;
    out->channel_idx = info.channel_idx;
    out->rssi_dbm    = info.rssi;
    out->hops_away   = info.hops_away;
    out->battery_pct = info.battery_pct;   // already -1-if-unknown, same convention as MSN_BATTERY_UNKNOWN

    if (info.last_ms == 0) {
        out->last_seen_ms_ago = MSN_LAST_SEEN_UNKNOWN;
    } else {
        uint32_t now_ms = (uint32_t)purr_kernel_uptime_ms();
        out->last_seen_ms_ago = now_ms - info.last_ms;
    }
    return true;
}

static void backend_contact_forget(int idx) {
    mesh_node_info_t info;
    if (mesh_manager_node_at(idx, &info) != 0) return;
    mesh_manager_node_forget(info.id);
}

static int backend_channel_count(void) { return mesh_manager_channel_count(); }

static bool backend_channel_name(int idx, char *out, size_t max) {
    return mesh_manager_channel_name(idx, out, max);
}

static int backend_channel_add(const char *name, const uint8_t *psk, size_t psk_len) {
    if (psk_len != 16) return -1;   // mesh_manager_add_channel() requires exactly 16 bytes
    return mesh_manager_add_channel(name, psk);
}

static void backend_channel_remove(int idx) {
    mesh_manager_remove_channel(idx);
}

static bool backend_channel_hash(int idx, uint8_t *hash_out) {
    return mesh_manager_channel_hash(idx, hash_out);
}

static bool backend_send_text(int to_contact_idx, int channel_idx, const char *text) {
    if (to_contact_idx >= 0) {
        // DM: channel_idx is ignored — the contact's own last-heard channel
        // picks which PSK to encode with, not whatever the caller passed.
        mesh_node_info_t info;
        if (mesh_manager_node_at(to_contact_idx, &info) != 0) return false;
        return mesh_manager_send_text(info.id, info.channel_idx, text);
    }
    if (channel_idx < 0) return false;
    return mesh_manager_send_text(MESH_BROADCAST, channel_idx, text);
}

// ── RX fan-out ───────────────────────────────────────────────────────────
// meshtastic.h's own callback delivers raw (from_node, to_node, channel_idx,
// portnum, payload, len) — this shim filters to text messages, resolves
// from_node to a contact index, and fans out to every locally-registered
// msn_rx_cb_t. Only one shim is ever registered with mesh_manager_add_rx_
// callback() regardless of how many msn_rx_cb_t subscribers MSN itself adds.

#define MAX_LOCAL_RX_CB 4
static msn_rx_cb_t s_local_cbs[MAX_LOCAL_RX_CB];
static bool        s_shim_registered = false;

static int find_contact_idx_by_node_id(uint32_t node_id) {
    int n = mesh_manager_node_count();
    for (int i = 0; i < n; i++) {
        mesh_node_info_t info;
        if (mesh_manager_node_at(i, &info) != 0) break;
        if (info.id == node_id) return i;
    }
    return -1;
}

static void rx_shim(uint32_t from_node, uint32_t to_node, int channel_idx, int portnum,
                     const uint8_t *payload, size_t len) {
    if (portnum != (int)meshtastic_PortNum_TEXT_MESSAGE_APP) return;

    char text[241];
    size_t n = len < sizeof(text) - 1 ? len : sizeof(text) - 1;
    memcpy(text, payload, n);
    text[n] = 0;

    int contact_idx = find_contact_idx_by_node_id(from_node);
    int out_channel_idx = (to_node == (uint32_t)MESH_BROADCAST) ? channel_idx : -1;

    for (int i = 0; i < MAX_LOCAL_RX_CB; i++) {
        if (s_local_cbs[i]) s_local_cbs[i](contact_idx, out_channel_idx, text);
    }
}

static void backend_add_rx_cb(msn_rx_cb_t cb) {
    if (!cb) return;
    if (!s_shim_registered) {
        mesh_manager_add_rx_callback(rx_shim);
        s_shim_registered = true;
    }
    for (int i = 0; i < MAX_LOCAL_RX_CB; i++) {
        if (s_local_cbs[i] == cb) return;
        if (!s_local_cbs[i]) { s_local_cbs[i] = cb; return; }
    }
}

static void backend_remove_rx_cb(msn_rx_cb_t cb) {
    for (int i = 0; i < MAX_LOCAL_RX_CB; i++) {
        if (s_local_cbs[i] == cb) { s_local_cbs[i] = NULL; return; }
    }
}

static const msn_backend_t s_backend = {
    .name           = "Meshtastic",
    .ready          = backend_ready,
    .is_alive       = backend_is_alive,
    .contact_count  = backend_contact_count,
    .contact_at     = backend_contact_at,
    .contact_forget = backend_contact_forget,
    .channel_count  = backend_channel_count,
    .channel_name   = backend_channel_name,
    .channel_add    = backend_channel_add,
    .channel_remove = backend_channel_remove,
    .channel_hash   = backend_channel_hash,
    .send_text      = backend_send_text,
    .add_rx_cb      = backend_add_rx_cb,
    .remove_rx_cb   = backend_remove_rx_cb,
};

const msn_backend_t *msn_backend_meshtastic(void) { return &s_backend; }
