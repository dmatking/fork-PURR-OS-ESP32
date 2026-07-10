// meshchat.c — PURR OS MeshChat: MSN-style rooms + buddy list, private 1:1
// chat and multi-channel group chat over the Meshtastic mesh (.claw). Talks
// only through meshtastic.h's public API (mesh_manager_send_text/
// add_rx_callback/node_at/channel_*) — the mesh module itself is a real,
// wire-compatible Meshtastic client (verified default channel PSK +
// LongFast modem preset against real Meshtastic hardware), so this app is a
// standalone messaging client: no phone, no BLE companion, no official
// Meshtastic app required to chat.
//
// Two lists on the main window: Rooms (channels — group chat scoped to one
// channel's PSK, channel 0 is always the "LongFast" default everyone
// speaks) and Buddies (1:1 DMs). A buddy is DMed on whichever channel it
// was last heard on (mesh_node_info_t.channel_idx) — a node met on a
// private room's channel can't be reached on the primary channel's key.
//
// Message logs (both rooms and DMs) persist across closing and reopening a
// chat window (and across closing/relaunching the whole app) — only the
// window widgets are torn down and rebuilt, the log buffers below are
// static/heap globals that outlive them.

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "purr_win.h"
#include "purr_kernel.h"
#include "purr_module.h"
#include "meshtastic.h"
#include "meshtastic/portnums.pb.h"
#include <mbedtls/sha256.h>

// Mirrors mesh_router.c's MAX_NODES — can't have more buddies than the mesh
// module itself tracks nodes.
#define MAX_CHATS      16
#define CHAT_LOG_LEN   1024
// A buddy is shown as "online" if heard from within this window; purely a
// UI-freshness cue, not synced to meshtastic_module.c's own (private)
// re-announce interval.
#define ONLINE_WINDOW_MS (15UL * 60UL * 1000UL)

// ── Main window state ────────────────────────────────────────────────────────

static purr_win_t  s_buddy_win   = 0;
static purr_wid_t  s_buddy_list  = 0;
static purr_wid_t  s_room_list   = 0;
static purr_wid_t  s_status_lbl  = 0;
static TaskHandle_t s_refresh_task = NULL;
static bool         s_running      = false;
// Given by buddy_refresh_task() right before it self-deletes, waited on by
// meshchat_deinit() before it destroys any window — see that function's
// comment for the use-after-free this closes (Task Manager's Kill button
// could tear down s_buddy_win/s_buddy_list/s_status_lbl while this task was
// still mid-refresh_buddy_list()/refresh_status_label(), touching those same
// now-freed handles; confirmed live as the "close_ctrl_body_start" hang).
static SemaphoreHandle_t s_refresh_done = NULL;

// ── Buddies (1:1 DMs) ────────────────────────────────────────────────────────

static uint32_t     s_buddy_ids[MAX_CHATS];
static int           s_buddy_channels[MAX_CHATS];   // which channel to DM each buddy on
static char         s_buddy_labels[MAX_CHATS][64];
static const char   *s_buddy_label_ptrs[MAX_CHATS];
static int           s_buddy_count = 0;

// Per-buddy chat state (parallel arrays, indexed same as s_buddy_ids).
static char        s_chat_logs[MAX_CHATS][CHAT_LOG_LEN];
static size_t      s_chat_loglen[MAX_CHATS];
static purr_win_t  s_chat_win[MAX_CHATS];
static purr_wid_t  s_chat_out[MAX_CHATS];
static purr_wid_t  s_chat_in[MAX_CHATS];

// ── Rooms (channel group chat) ───────────────────────────────────────────────

static char        s_room_names[MESH_MAX_CHANNELS][12];
static const char  *s_room_label_ptrs[MESH_MAX_CHANNELS];
static int          s_room_count = 0;

// Heap-allocated from PSRAM (not static arrays) — internal DRAM on this
// board is already razor-thin (see cupcake_module.c's static-stack-sizing
// comment); MESH_MAX_CHANNELS(8) * CHAT_LOG_LEN(1024) static buffers here
// would overflow dram0_0_seg the same way one 1024-byte static broadcast
// log previously did.
static char        *s_room_logs[MESH_MAX_CHANNELS];
static size_t        s_room_loglen[MESH_MAX_CHANNELS];
static purr_win_t   s_room_win[MESH_MAX_CHANNELS];
static purr_wid_t   s_room_out[MESH_MAX_CHANNELS];
static purr_wid_t   s_room_in[MESH_MAX_CHANNELS];

// ── Add Room window ──────────────────────────────────────────────────────────

static purr_win_t  s_addroom_win      = 0;
static purr_wid_t  s_addroom_name_in  = 0;
static purr_wid_t  s_addroom_psk_in   = 0;

// ── Buddy list ────────────────────────────────────────────────────────────────

static void refresh_buddy_list(void) {
    int n = mesh_manager_node_count();
    if (n > MAX_CHATS) n = MAX_CHATS;
    s_buddy_count = n;

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    for (int i = 0; i < n; i++) {
        mesh_node_info_t info;
        if (mesh_manager_node_at(i, &info) != 0) { s_buddy_count = i; break; }
        s_buddy_ids[i]      = info.id;
        s_buddy_channels[i] = info.channel_idx;
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

// ── Room list ─────────────────────────────────────────────────────────────────

static void refresh_room_list(void) {
    int n = mesh_manager_channel_count();
    if (n > MESH_MAX_CHANNELS) n = MESH_MAX_CHANNELS;
    s_room_count = n;

    for (int i = 0; i < n; i++) {
        mesh_manager_channel_name(i, s_room_names[i], sizeof(s_room_names[i]));
        s_room_label_ptrs[i] = s_room_names[i];
    }

    if (s_room_list) purr_win_list_set_items(s_room_list, s_room_label_ptrs, s_room_count);
}

// Always-present status line — this and the Refresh/Add Room row below it
// are the fix for the "blank window" bug: previously the buddy list was the
// *only* widget in the window, so 0 discovered peers meant a fully empty
// box with nothing to look at or tap.
static void refresh_status_label(void) {
    if (!s_status_lbl) return;
    if (!mesh_manager_ready()) {
        purr_win_label_set(s_status_lbl, "Mesh: starting...");
    } else if (!mesh_manager_is_alive()) {
        purr_win_label_set(s_status_lbl, "Mesh: not responding");
    } else {
        char buf[48];
        snprintf(buf, sizeof(buf), "Mesh: ready (%d node%s)",
                 s_buddy_count, s_buddy_count == 1 ? "" : "s");
        purr_win_label_set(s_status_lbl, buf);
    }
}

static void on_refresh_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    refresh_buddy_list();
    refresh_room_list();
    refresh_status_label();
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
        refresh_room_list();
        refresh_status_label();
        // Slept in short steps (not one 10s vTaskDelay) so a stop request
        // (s_running = false) is noticed within ~200ms, not up to 10s late —
        // meshchat_deinit() blocks on s_refresh_done for this task to
        // actually exit before it destroys the windows/widgets this loop
        // touches, so a slow-to-notice stop directly extends how long Kill
        // stalls waiting, not just a cosmetic delay.
        for (int waited_ms = 0; waited_ms < 10000 && s_running; waited_ms += 200) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    if (s_refresh_done) xSemaphoreGive(s_refresh_done);
    // Must match the WithCaps variant used to create this task (see its
    // xTaskCreateWithCaps() call site below).
    vTaskDeleteWithCaps(NULL);
}

// ── Chat log helper (same scroll-drop-half pattern as terminal.c) ───────────

static void log_append(char *buf, size_t *len, const char *text) {
    size_t tlen = strlen(text);
    if (*len + tlen >= CHAT_LOG_LEN - 1) {
        size_t half = CHAT_LOG_LEN / 2;
        memmove(buf, buf + half, *len - half);
        *len -= half;
        buf[*len] = '\0';
    }
    memcpy(buf + *len, text, tlen + 1);
    *len += tlen;
}

static void chat_log_append(int idx, const char *text) {
    log_append(s_chat_logs[idx], &s_chat_loglen[idx], text);
    if (s_chat_win[idx] && s_chat_out[idx]) purr_win_textarea_set(s_chat_out[idx], s_chat_logs[idx]);
}

static void room_log_append(int idx, const char *text) {
    if (!s_room_logs[idx]) return;
    log_append(s_room_logs[idx], &s_room_loglen[idx], text);
    if (s_room_win[idx] && s_room_out[idx]) purr_win_textarea_set(s_room_out[idx], s_room_logs[idx]);
}

// ── DM chat window ───────────────────────────────────────────────────────────

static void on_chat_send(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e;
    int idx = (int)(intptr_t)user;
    if (idx < 0 || idx >= s_buddy_count) return;
    const char *text = purr_win_textarea_get(s_chat_in[idx]);
    if (!text || !*text) return;

    char line[160];
    snprintf(line, sizeof(line), "> %s\n", text);
    chat_log_append(idx, line);

    mesh_manager_send_text(s_buddy_ids[idx], s_buddy_channels[idx], text);
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
    // win_show() first — see terminal.c's terminal_init() for why (Cupcake's
    // win_show() raises the window above whatever kb_show() just showed).
    purr_win_show(s_chat_win[idx]);
    purr_win_keyboard_show(s_chat_win[idx], s_chat_in[idx]);
}

static void on_buddy_list_event(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)user;
    if (e != PURR_EVENT_ACTIVATED) return;
    int idx = purr_win_list_get_selected(s_buddy_list);
    open_chat(idx);
}

// ── Room chat window ──────────────────────────────────────────────────────────

static void on_room_send(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e;
    int idx = (int)(intptr_t)user;
    if (idx < 0 || idx >= s_room_count) return;
    const char *text = purr_win_textarea_get(s_room_in[idx]);
    if (!text || !*text) return;

    char line[160];
    snprintf(line, sizeof(line), "> %s\n", text);
    room_log_append(idx, line);

    mesh_manager_send_text(MESH_BROADCAST, idx, text);
    purr_win_textarea_clear(s_room_in[idx]);
}

static void open_room(int idx) {
    if (idx < 0 || idx >= s_room_count) return;
    if (s_room_win[idx]) {
        purr_win_show(s_room_win[idx]);
        return;
    }
    if (!s_room_logs[idx]) {
        s_room_logs[idx] = heap_caps_malloc(CHAT_LOG_LEN, MALLOC_CAP_SPIRAM);
        if (s_room_logs[idx]) s_room_logs[idx][0] = '\0';
        else return;
    }

    s_room_win[idx] = purr_win_create(s_room_names[idx]);
    s_room_out[idx] = purr_win_textarea(s_room_win[idx], 100, 75);
    s_room_in[idx]  = purr_win_textarea(s_room_win[idx], 80, 10);
    purr_win_button(s_room_win[idx], "Send", on_room_send, (void *)(intptr_t)idx);

    purr_win_textarea_set(s_room_out[idx], s_room_logs[idx]);
    purr_win_textarea_focus(s_room_in[idx]);
    // win_show() first — see terminal.c's terminal_init() for why (Cupcake's
    // win_show() raises the window above whatever kb_show() just showed).
    purr_win_show(s_room_win[idx]);
    purr_win_keyboard_show(s_room_win[idx], s_room_in[idx]);
}

static void on_room_list_event(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)user;
    if (e != PURR_EVENT_ACTIVATED) return;
    int idx = purr_win_list_get_selected(s_room_list);
    open_room(idx);
}

// ── Add Room ──────────────────────────────────────────────────────────────────
// No QR/URL channel-sharing (out of scope) — the PSK is derived from
// whatever passphrase text the user types, via SHA-256 truncated to 16
// bytes, so two PURR devices typing the same passphrase land on the same
// key deterministically. This won't match a real Meshtastic node's own
// custom-channel PSK (those are raw random bytes shared via QR/URL, not a
// passphrase) — interop there would need the exact same 16 raw bytes,
// which this flow doesn't support yet.

static void derive_psk_from_passphrase(const char *passphrase, uint8_t psk16[16]) {
    uint8_t hash[32];
    mbedtls_sha256((const unsigned char *)passphrase, strlen(passphrase), hash, 0);
    memcpy(psk16, hash, 16);
}

static void on_addroom_create(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    const char *name = purr_win_textarea_get(s_addroom_name_in);
    const char *pass = purr_win_textarea_get(s_addroom_psk_in);
    if (!name || !*name || !pass || !*pass) return;

    uint8_t psk16[16];
    derive_psk_from_passphrase(pass, psk16);
    int idx = mesh_manager_add_channel(name, psk16);
    if (idx < 0) return;   // full, or hash collision — nothing to show for it yet

    refresh_room_list();
    purr_win_destroy(s_addroom_win);
    s_addroom_win = 0; s_addroom_name_in = 0; s_addroom_psk_in = 0;
    open_room(idx);
}

static void open_addroom(void) {
    if (s_addroom_win) {
        purr_win_show(s_addroom_win);
        return;
    }
    if (mesh_manager_channel_count() >= MESH_MAX_CHANNELS) return;   // full

    s_addroom_win     = purr_win_create("Add Room");
    purr_win_label(s_addroom_win, "Room name:");
    s_addroom_name_in = purr_win_textarea(s_addroom_win, 90, 15);
    purr_win_label(s_addroom_win, "Passphrase (shared with whoever else joins):");
    s_addroom_psk_in  = purr_win_textarea(s_addroom_win, 90, 15);
    purr_win_button(s_addroom_win, "Create", on_addroom_create, NULL);

    purr_win_textarea_focus(s_addroom_name_in);
    purr_win_show(s_addroom_win);
    purr_win_keyboard_show(s_addroom_win, s_addroom_name_in);
}

static void on_addroom_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    open_addroom();
}

// ── Incoming messages ─────────────────────────────────────────────────────────
// Registers the mesh module's single RX-callback slot — meshtastic_module.c
// already fires purr_kernel_notify() for every TEXT_MESSAGE_APP regardless
// (unchanged), so a message for a buddy/room whose window isn't open right
// now still surfaces as a notification banner.

static void on_mesh_rx(uint32_t from_node, uint32_t to_node, int channel_idx, int portnum,
                        const uint8_t *payload, size_t len) {
    if (portnum != (int)meshtastic_PortNum_TEXT_MESSAGE_APP) return;

    char text[241];
    size_t n = len < sizeof(text) - 1 ? len : sizeof(text) - 1;
    memcpy(text, payload, n);
    text[n] = '\0';

    if (to_node == (uint32_t)MESH_BROADCAST) {
        if (channel_idx < 0 || channel_idx >= s_room_count) return;
        char line[280];
        snprintf(line, sizeof(line), "%08lX: %s\n", (unsigned long)from_node, text);
        room_log_append(channel_idx, line);
        return;
    }

    int idx = find_buddy_idx(from_node);
    if (idx < 0) return;
    char line[280];
    snprintf(line, sizeof(line), "%s: %s\n", s_buddy_labels[idx], text);
    chat_log_append(idx, line);
}

// ── App lifecycle ─────────────────────────────────────────────────────────────

static int meshchat_init(void) {
    // Reused across relaunches like s_room_logs below — starts "empty"
    // (taken), which is exactly the state meshchat_deinit()'s
    // xSemaphoreTake() below needs at the start of every run.
    if (!s_refresh_done) s_refresh_done = xSemaphoreCreateBinary();

    s_buddy_win  = purr_win_create("MeshChat");
    s_status_lbl = purr_win_label(s_buddy_win, "Mesh: starting...");

    // Static control row — always present regardless of how many peers
    // (zero, at worst) have been discovered, unlike the lists below it.
    purr_wid_t row = purr_win_row(s_buddy_win, 4);
    purr_win_button(s_buddy_win, "Refresh",  on_refresh_click,  NULL);
    purr_win_button(s_buddy_win, "Add Room", on_addroom_click,  NULL);
    purr_win_layout_end(row);

    purr_win_label(s_buddy_win, "Rooms");
    s_room_list = purr_win_list(s_buddy_win, 100, 40);
    purr_win_list_on_select(s_room_list, on_room_list_event, NULL);

    purr_win_label(s_buddy_win, "Buddies");
    s_buddy_list = purr_win_list(s_buddy_win, 100, 40);
    purr_win_list_on_select(s_buddy_list, on_buddy_list_event, NULL);

    refresh_buddy_list();
    refresh_room_list();
    refresh_status_label();
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
    // Wait for buddy_refresh_task() to actually notice s_running == false
    // and exit before touching any window/widget below — see
    // s_refresh_done's declaration comment for the use-after-free this
    // closes. Bounded timeout matches app_manager_stop()'s own
    // wait-then-proceed pattern; the task loop has no blocking calls beyond
    // its own short vTaskDelay steps, so it should never actually take
    // anywhere near this long to respond.
    if (s_refresh_done) xSemaphoreTake(s_refresh_done, pdMS_TO_TICKS(2000));
    s_refresh_task = NULL;

    mesh_manager_remove_rx_callback(on_mesh_rx);

    for (int i = 0; i < MAX_CHATS; i++) {
        if (s_chat_win[i]) {
            purr_win_destroy(s_chat_win[i]);
            s_chat_win[i] = 0; s_chat_out[i] = 0; s_chat_in[i] = 0;
        }
    }
    for (int i = 0; i < MESH_MAX_CHANNELS; i++) {
        if (s_room_win[i]) {
            purr_win_destroy(s_room_win[i]);
            s_room_win[i] = 0; s_room_out[i] = 0; s_room_in[i] = 0;
        }
    }
    if (s_addroom_win) {
        purr_win_destroy(s_addroom_win);
        s_addroom_win = 0; s_addroom_name_in = 0; s_addroom_psk_in = 0;
    }
    purr_win_destroy(s_buddy_win);
    s_buddy_win = 0; s_buddy_list = 0; s_room_list = 0; s_status_lbl = 0;
    // s_chat_logs/s_chat_loglen/s_room_logs/s_room_loglen deliberately NOT
    // cleared — chat history persists across closing and relaunching the app.
}

// ── Module header ─────────────────────────────────────────────────────────────

PURR_MODULE_REGISTER(meshchat) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_APP,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "meshchat",
    .version           = "1.0.0",
    .kernel_min        = "0.11.1",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = meshchat_init,
    .deinit            = meshchat_deinit,
};
