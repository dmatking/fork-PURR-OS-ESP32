// meshchat.c — PURR OS MeshChat: MSN-style buddy list + private 1:1 chat
// over the Meshtastic mesh (.claw). Talks only through meshtastic.h's public
// API (mesh_manager_send_text/set_rx_callback/node_at) — the mesh module
// itself is a real, wire-compatible Meshtastic client (verified default
// channel PSK + LongFast modem preset against real Meshtastic hardware
// while planning this), so this app is a standalone messaging client: no
// phone, no BLE companion, no official Meshtastic app required to chat.
//
// Buddy list is the app's main window; tapping a buddy opens a private chat
// window scoped to that node's ID. Message logs persist across closing and
// reopening a chat window (and across closing/relaunching the whole app) —
// only the window widgets are torn down and rebuilt, the log buffers below
// are static globals that outlive them.

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "purr_win.h"
#include "purr_kernel.h"
#include "purr_module.h"
#include "meshtastic.h"
#include "meshtastic/portnums.pb.h"

// Mirrors mesh_router.c's MAX_NODES — can't have more buddies than the mesh
// module itself tracks nodes.
#define MAX_CHATS      16
#define CHAT_LOG_LEN   1024
// A buddy is shown as "online" if heard from within this window; purely a
// UI-freshness cue, not synced to meshtastic_module.c's own (private)
// re-announce interval.
#define ONLINE_WINDOW_MS (15UL * 60UL * 1000UL)

// ── Buddy list state ──────────────────────────────────────────────────────────

static purr_win_t  s_buddy_win   = 0;
static purr_wid_t  s_buddy_list  = 0;
static TaskHandle_t s_refresh_task = NULL;
static bool         s_running      = false;

static uint32_t     s_buddy_ids[MAX_CHATS];
static char         s_buddy_labels[MAX_CHATS][64];
static const char   *s_buddy_label_ptrs[MAX_CHATS];
static int           s_buddy_count = 0;

// ── Per-buddy chat state (parallel arrays, indexed same as s_buddy_ids) ──────

static char        s_chat_logs[MAX_CHATS][CHAT_LOG_LEN];
static size_t      s_chat_loglen[MAX_CHATS];
static purr_win_t  s_chat_win[MAX_CHATS];
static purr_wid_t  s_chat_out[MAX_CHATS];
static purr_wid_t  s_chat_in[MAX_CHATS];

// ── Buddy list ────────────────────────────────────────────────────────────────

static void refresh_buddy_list(void) {
    int n = mesh_manager_node_count();
    if (n > MAX_CHATS) n = MAX_CHATS;
    s_buddy_count = n;

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    for (int i = 0; i < n; i++) {
        mesh_node_info_t info;
        if (mesh_manager_node_at(i, &info) != 0) { s_buddy_count = i; break; }
        s_buddy_ids[i] = info.id;
        bool online = (now_ms - info.last_ms) < ONLINE_WINDOW_MS;
        if (online) {
            snprintf(s_buddy_labels[i], sizeof(s_buddy_labels[i]), "%s (online)", info.long_name);
        } else {
            uint32_t age_min = (now_ms - info.last_ms) / 60000UL;
            snprintf(s_buddy_labels[i], sizeof(s_buddy_labels[i]), "%s (%lum ago)",
                     info.long_name, (unsigned long)age_min);
        }
        s_buddy_label_ptrs[i] = s_buddy_labels[i];
    }

    if (s_buddy_list) purr_win_list_set_items(s_buddy_list, s_buddy_label_ptrs, s_buddy_count);
}

// Finds idx for a node id already known to the buddy list, refreshing once
// if not found (covers a brand-new node's first message arriving before our
// next periodic refresh has picked it up).
static int find_buddy_idx(uint32_t node_id) {
    for (int i = 0; i < s_buddy_count; i++) if (s_buddy_ids[i] == node_id) return i;
    refresh_buddy_list();
    for (int i = 0; i < s_buddy_count; i++) if (s_buddy_ids[i] == node_id) return i;
    return -1;
}

static void buddy_refresh_task(void *arg) {
    (void)arg;
    while (s_running) {
        refresh_buddy_list();
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    // Must match the WithCaps variant used to create this task (see its
    // xTaskCreateWithCaps() call site below).
    vTaskDeleteWithCaps(NULL);
}

// ── Chat log helper (same scroll-drop-half pattern as terminal.c) ───────────

static void chat_log_append(int idx, const char *text) {
    size_t tlen = strlen(text);
    size_t *len = &s_chat_loglen[idx];
    char   *buf = s_chat_logs[idx];
    if (*len + tlen >= CHAT_LOG_LEN - 1) {
        size_t half = CHAT_LOG_LEN / 2;
        memmove(buf, buf + half, *len - half);
        *len -= half;
        buf[*len] = '\0';
    }
    memcpy(buf + *len, text, tlen + 1);
    *len += tlen;
    if (s_chat_win[idx] && s_chat_out[idx]) purr_win_textarea_set(s_chat_out[idx], buf);
}

// ── Chat window ───────────────────────────────────────────────────────────────

static void on_chat_send(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e;
    int idx = (int)(intptr_t)user;
    if (idx < 0 || idx >= s_buddy_count) return;
    const char *text = purr_win_textarea_get(s_chat_in[idx]);
    if (!text || !*text) return;

    char line[160];
    snprintf(line, sizeof(line), "> %s\n", text);
    chat_log_append(idx, line);

    mesh_manager_send_text(s_buddy_ids[idx], text);
    purr_win_textarea_clear(s_chat_in[idx]);
}

static void open_chat(int idx) {
    if (idx < 0 || idx >= s_buddy_count) return;
    if (s_chat_win[idx]) {
        purr_win_show(s_chat_win[idx]);
        return;
    }

    s_chat_win[idx] = purr_win_create(s_buddy_labels[idx]);
    s_chat_out[idx] = purr_win_textarea(s_chat_win[idx], 100, 75);
    s_chat_in[idx]  = purr_win_textarea(s_chat_win[idx], 80, 10);
    purr_win_button(s_chat_win[idx], "Send", on_chat_send, (void *)(intptr_t)idx);

    purr_win_textarea_set(s_chat_out[idx], s_chat_logs[idx]);
    purr_win_textarea_focus(s_chat_in[idx]);
    purr_win_keyboard_show(s_chat_win[idx], s_chat_in[idx]);
    purr_win_show(s_chat_win[idx]);
}

static void on_buddy_list_event(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)user;
    if (e != PURR_EVENT_ACTIVATED) return;
    int idx = purr_win_list_get_selected(s_buddy_list);
    open_chat(idx);
}

// ── Incoming messages ─────────────────────────────────────────────────────────
// Registers the mesh module's single RX-callback slot — meshtastic_module.c
// already fires purr_kernel_notify() for every TEXT_MESSAGE_APP regardless
// (unchanged), so a message for a buddy whose chat window isn't open right
// now still surfaces as a notification banner.

static void on_mesh_rx(uint32_t from_node, int portnum, const uint8_t *payload, size_t len) {
    if (portnum != (int)meshtastic_PortNum_TEXT_MESSAGE_APP) return;
    int idx = find_buddy_idx(from_node);
    if (idx < 0) return;

    char text[241];
    size_t n = len < sizeof(text) - 1 ? len : sizeof(text) - 1;
    memcpy(text, payload, n);
    text[n] = '\0';

    char line[280];
    snprintf(line, sizeof(line), "%s: %s\n", s_buddy_labels[idx], text);
    chat_log_append(idx, line);
}

// ── App lifecycle ─────────────────────────────────────────────────────────────

static int meshchat_init(void) {
    s_buddy_win  = purr_win_create("MeshChat");
    s_buddy_list = purr_win_list(s_buddy_win, 100, 90);
    purr_win_list_on_select(s_buddy_list, on_buddy_list_event, NULL);

    refresh_buddy_list();
    purr_win_show(s_buddy_win);

    mesh_manager_add_rx_callback(on_mesh_rx);

    s_running = true;
    // No NVS/flash/SD access anywhere in this task's own body — safe on a
    // PSRAM-backed stack (see app_manager.c's launch_native()/launch_meow()
    // for the same pattern).
    xTaskCreateWithCaps(buddy_refresh_task, "meshchat_ref", 4096, NULL, 3, &s_refresh_task, MALLOC_CAP_SPIRAM);
    return 0;
}

static void meshchat_deinit(void) {
    s_running = false;
    mesh_manager_remove_rx_callback(on_mesh_rx);

    for (int i = 0; i < MAX_CHATS; i++) {
        if (s_chat_win[i]) {
            purr_win_destroy(s_chat_win[i]);
            s_chat_win[i] = 0; s_chat_out[i] = 0; s_chat_in[i] = 0;
        }
    }
    purr_win_destroy(s_buddy_win);
    s_buddy_win = 0; s_buddy_list = 0;
    // s_chat_logs/s_chat_loglen deliberately NOT cleared — chat history
    // persists across closing and relaunching the app.
}

// ── Module header ─────────────────────────────────────────────────────────────

PURR_MODULE_REGISTER(meshchat) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_APP,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "meshchat",
    .version           = "1.0.0",
    .kernel_min        = "0.9.0",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = meshchat_init,
    .deinit            = meshchat_deinit,
};
