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
static int         s_chat_scroll[MAX_CHATS];   // lines skipped from the top of the log — see render_chat_view()

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
static int          s_room_scroll[MESH_MAX_CHANNELS];   // lines skipped from the top of the log

// ── Add Room window ──────────────────────────────────────────────────────────

static purr_win_t  s_addroom_win      = 0;
static purr_wid_t  s_addroom_name_in  = 0;
static purr_wid_t  s_addroom_psk_in   = 0;

// ── Manage window (forget nodes/rooms) ───────────────────────────────────────

static purr_win_t  s_manage_win       = 0;
static purr_wid_t  s_manage_node_list = 0;
static purr_wid_t  s_manage_room_list = 0;

// ── SD persistence ────────────────────────────────────────────────────────────
// Real, unbounded history on /sdcard/meshchat/ — independent of the in-
// memory display buffers above (CHAT_LOG_LEN-capped scrollback, unchanged).
// Keyed by stable identity (node id / channel hash byte), not table index —
// indices shift when a node or room is forgotten, but the underlying id/
// hash never does. Everything here no-ops behind purr_kernel_sd_available()
// — no SD card means today's in-memory-only behavior, not a crash.

static void dm_path(uint32_t node_id, char *out, size_t out_max) {
    snprintf(out, out_max, "/sdcard/meshchat/dm_%08lX.txt", (unsigned long)node_id);
}

static void room_path(int channel_idx, char *out, size_t out_max) {
    uint8_t hash = 0;
    mesh_manager_channel_hash(channel_idx, &hash);
    snprintf(out, out_max, "/sdcard/meshchat/room_%02X.txt", hash);
}

static void append_to_sd(const char *path, const char *text) {
    if (!purr_kernel_sd_available()) return;
    FILE *f = fopen(path, "a");
    if (!f) return;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

// Seeds an in-memory log buffer from the tail of its SD file — called once
// per conversation, the first time it's opened this boot (buf still empty).
static void load_tail_from_sd(const char *path, char *buf, size_t *len, size_t buf_cap) {
    if (!purr_kernel_sd_available()) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize <= 0) { fclose(f); return; }
    size_t cap  = buf_cap - 1;
    size_t want = (size_t)fsize;
    if (want > cap) {
        fseek(f, (long)((size_t)fsize - cap), SEEK_SET);
        want = cap;
    } else {
        fseek(f, 0, SEEK_SET);
    }
    size_t n = fread(buf, 1, want, f);
    buf[n] = '\0';
    *len = n;
    fclose(f);
}

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

// ── Scrolling ────────────────────────────────────────────────────────────────
// No backend exposes real textarea scrolling through the purr_win_* catcall
// layer (MiniWin's own engine has the machinery — ui_text_box.c's
// lines_to_scroll — but miniwin_win.c never wires it up; Cupcake's LVGL
// textarea has no equivalent hook either). Rather than add new catcall/
// engine plumbing for one app, this renders a scrolled-down "window" into
// the log by re-calling the textarea_set() every backend already has,
// skipping the first N lines of the underlying buffer — works identically
// everywhere with zero engine changes. The textarea always draws from
// whatever text it's given starting at its own top, so without this the
// newest messages (appended at the END of the buffer) are exactly the ones
// that get clipped off the bottom once a conversation overflows the box.
#define CHAT_SCROLL_STEP 4   // lines revealed per "v" button press

static int count_lines(const char *s) {
    int n = 1;
    for (; *s; s++) if (*s == '\n') n++;
    return n;
}

static const char *skip_lines(const char *text, int n) {
    const char *p = text;
    while (n > 0 && *p) {
        if (*p == '\n') n--;
        p++;
    }
    return p;
}

static void render_chat_view(int idx) {
    if (!s_chat_win[idx] || !s_chat_out[idx]) return;
    int max_scroll = count_lines(s_chat_logs[idx]) - 1;
    if (max_scroll < 0) max_scroll = 0;
    if (s_chat_scroll[idx] > max_scroll) s_chat_scroll[idx] = max_scroll;
    purr_win_textarea_set(s_chat_out[idx], skip_lines(s_chat_logs[idx], s_chat_scroll[idx]));
}

static void render_room_view(int idx) {
    if (!s_room_win[idx] || !s_room_out[idx] || !s_room_logs[idx]) return;
    int max_scroll = count_lines(s_room_logs[idx]) - 1;
    if (max_scroll < 0) max_scroll = 0;
    if (s_room_scroll[idx] > max_scroll) s_room_scroll[idx] = max_scroll;
    purr_win_textarea_set(s_room_out[idx], skip_lines(s_room_logs[idx], s_room_scroll[idx]));
}

static void on_chat_scroll_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e;
    int idx = (int)(intptr_t)user;
    if (idx < 0 || idx >= MAX_CHATS) return;
    s_chat_scroll[idx] += CHAT_SCROLL_STEP;
    render_chat_view(idx);
}

static void on_room_scroll_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e;
    int idx = (int)(intptr_t)user;
    if (idx < 0 || idx >= MESH_MAX_CHANNELS) return;
    s_room_scroll[idx] += CHAT_SCROLL_STEP;
    render_room_view(idx);
}

static void chat_log_append(int idx, const char *text) {
    log_append(s_chat_logs[idx], &s_chat_loglen[idx], text);
    render_chat_view(idx);
    char path[64];
    dm_path(s_buddy_ids[idx], path, sizeof(path));
    append_to_sd(path, text);
}

static void room_log_append(int idx, const char *text) {
    if (!s_room_logs[idx]) return;
    log_append(s_room_logs[idx], &s_room_loglen[idx], text);
    render_room_view(idx);
    char path[64];
    room_path(idx, path, sizeof(path));
    append_to_sd(path, text);
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

    // First time this conversation's been opened this boot — resume its
    // history from SD before building the window.
    if (s_chat_loglen[idx] == 0) {
        char path[64];
        dm_path(s_buddy_ids[idx], path, sizeof(path));
        load_tail_from_sd(path, s_chat_logs[idx], &s_chat_loglen[idx], CHAT_LOG_LEN);
    }

    s_chat_win[idx] = purr_win_create(s_buddy_labels[idx]);
    s_chat_out[idx] = purr_win_textarea(s_chat_win[idx], 100, 75);
    purr_wid_t row = purr_win_row(s_chat_win[idx], 4);
    s_chat_in[idx]  = purr_win_textarea(s_chat_win[idx], 60, 10);
    purr_win_button(s_chat_win[idx], "Send", on_chat_send, (void *)(intptr_t)idx);
    purr_win_button(s_chat_win[idx], "v", on_chat_scroll_click, (void *)(intptr_t)idx);
    purr_win_layout_end(row);

    render_chat_view(idx);
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
    // First time this room's been opened this boot — resume its history
    // from SD before building the window.
    if (s_room_loglen[idx] == 0) {
        char path[64];
        room_path(idx, path, sizeof(path));
        load_tail_from_sd(path, s_room_logs[idx], &s_room_loglen[idx], CHAT_LOG_LEN);
    }

    s_room_win[idx] = purr_win_create(s_room_names[idx]);
    s_room_out[idx] = purr_win_textarea(s_room_win[idx], 100, 75);
    purr_wid_t row = purr_win_row(s_room_win[idx], 4);
    s_room_in[idx]  = purr_win_textarea(s_room_win[idx], 60, 10);
    purr_win_button(s_room_win[idx], "Send", on_room_send, (void *)(intptr_t)idx);
    purr_win_button(s_room_win[idx], "v", on_room_scroll_click, (void *)(intptr_t)idx);
    purr_win_layout_end(row);

    render_room_view(idx);
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

// ── Manage (forget nodes/rooms) ───────────────────────────────────────────────
// A forget action changes which buddy/room ends up at which table index
// (mesh_router_node_forget()/mesh_radio_remove_channel() both shift the
// underlying table down) — rather than try to shift the parallel
// s_chat_*/s_room_* window/log arrays to match, every open chat/room window
// just gets closed and its in-memory log cleared. Re-opening any of them
// reloads correctly (dm_path()/room_path() are keyed by node id / channel
// hash, not index, so this is always safe) — a small UX cost for a
// deliberate, infrequent action, in exchange for zero risk of a stale log
// silently appearing under the wrong buddy's name.

static void reset_all_buddy_windows(void) {
    for (int i = 0; i < MAX_CHATS; i++) {
        if (s_chat_win[i]) {
            purr_win_destroy(s_chat_win[i]);
            s_chat_win[i] = 0; s_chat_out[i] = 0; s_chat_in[i] = 0;
        }
        s_chat_logs[i][0] = '\0';
        s_chat_loglen[i]  = 0;
    }
}

static void reset_all_room_windows(void) {
    for (int i = 0; i < MESH_MAX_CHANNELS; i++) {
        if (s_room_win[i]) {
            purr_win_destroy(s_room_win[i]);
            s_room_win[i] = 0; s_room_out[i] = 0; s_room_in[i] = 0;
        }
        if (s_room_logs[i]) s_room_logs[i][0] = '\0';
        s_room_loglen[i] = 0;
    }
}

static void refresh_manage_lists(void) {
    if (s_manage_node_list) purr_win_list_set_items(s_manage_node_list, s_buddy_label_ptrs, s_buddy_count);
    if (s_manage_room_list) purr_win_list_set_items(s_manage_room_list, s_room_label_ptrs, s_room_count);
}

static void on_forget_node(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    int idx = purr_win_list_get_selected(s_manage_node_list);
    if (idx < 0 || idx >= s_buddy_count) return;

    char path[64];
    dm_path(s_buddy_ids[idx], path, sizeof(path));
    remove(path);
    mesh_manager_node_forget(s_buddy_ids[idx]);
    reset_all_buddy_windows();
    refresh_buddy_list();
    refresh_manage_lists();
}

static void on_forget_room(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    int idx = purr_win_list_get_selected(s_manage_room_list);
    if (idx < 0 || idx >= s_room_count) return;

    char path[64];
    room_path(idx, path, sizeof(path));
    remove(path);
    mesh_manager_remove_channel(idx);
    reset_all_room_windows();
    refresh_room_list();
    refresh_manage_lists();
}

static void open_manage(void) {
    if (s_manage_win) {
        purr_win_show(s_manage_win);
        refresh_manage_lists();
        return;
    }

    s_manage_win = purr_win_create("Manage");
    purr_win_label(s_manage_win, "Known Nodes");
    s_manage_node_list = purr_win_list(s_manage_win, 100, 35);
    purr_win_button(s_manage_win, "Forget Node", on_forget_node, NULL);

    purr_win_label(s_manage_win, "Rooms");
    s_manage_room_list = purr_win_list(s_manage_win, 100, 35);
    purr_win_button(s_manage_win, "Forget Room", on_forget_room, NULL);

    refresh_manage_lists();
    purr_win_show(s_manage_win);
}

static void on_manage_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    open_manage();
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

    // Display name only — the app/module id stays "meshchat" (file names,
    // device.pcat's apps.meshchat entry, icon lookup key all unchanged).
    s_buddy_win  = purr_win_create("MSN");
    s_status_lbl = purr_win_label(s_buddy_win, "Mesh: starting...");

    // Static control row — always present regardless of how many peers
    // (zero, at worst) have been discovered, unlike the lists below it.
    purr_wid_t row = purr_win_row(s_buddy_win, 4);
    purr_win_button(s_buddy_win, "Refresh",  on_refresh_click,  NULL);
    purr_win_button(s_buddy_win, "Add Room", on_addroom_click,  NULL);
    purr_win_button(s_buddy_win, "Manage",   on_manage_click,   NULL);
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
    if (s_manage_win) {
        purr_win_destroy(s_manage_win);
        s_manage_win = 0; s_manage_node_list = 0; s_manage_room_list = 0;
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
