// msn.c — PURR OS MSN (Mesh Social Network): rooms + buddy list, private 1:1
// chat and multi-channel group chat over whichever mesh backend (Meshtastic
// or MeshCore) is currently active. Renamed/refactored from meshchat.c,
// which talked to meshtastic.h directly — this file now talks only through
// msn_backend.h's protocol-agnostic vtable (msn_backend_meshtastic.c /
// msn_backend_meshcore.c own the actual meshtastic.h/meshcore_api.h calls).
//
// Launch screen is a backend chooser: two big buttons (Meshtastic/MeshCore)
// with an active-indicator next to whichever one purr_kernel_get_module()
// shows is actually running. Tapping the active one enters the normal
// Rooms/Buddies chat UI below. Tapping the inactive one live-switches to it
// via purr_kernel_mesh_backend_switch() — persists the choice and swaps
// which mesh module is loaded, no reboot (see that function's own comment
// in purr_kernel.c for how the one-physical-radio mutual exclusion still
// holds without one).
//
// Two lists on the chat window: Rooms (channels — group chat scoped to one
// channel's key) and Buddies (1:1 DMs). A buddy is DMed using whichever
// channel/key the active backend's own contact record implies (Meshtastic:
// the contact's last-heard channel; MeshCore: per-contact ECDH, no channel
// involved) — msn_backend_t's send_text() owns that distinction internally.
//
// Message logs (both rooms and DMs) persist across closing and reopening a
// chat window (and across closing/relaunching the whole app) — only the
// window widgets are torn down and rebuilt, the log buffers below are
// static/heap globals that outlive them. SD history paths are keyed by
// msn_contact_t.id_str / the channel's on-air hash byte — both stable
// across a contact/channel table re-sort, and (for Meshtastic) byte-for-
// byte identical to what meshchat.c already wrote, so existing users' chat
// history is picked up unchanged after this rename.

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
#include "msn_backend.h"
#include <mbedtls/sha256.h>

#define MAX_CHATS      16
#define CHAT_LOG_LEN   1024
#define MAX_ROOMS      8   // matches MESH_MAX_CHANNELS / MC_MAX_CHANNELS
// A buddy is shown as "online" if heard from within this window; purely a
// UI-freshness cue, computed from msn_contact_t.last_seen_ms_ago (already
// normalized by the active backend), not any protocol-specific clock.
#define ONLINE_WINDOW_MS (15UL * 60UL * 1000UL)

// ── Backend selection ────────────────────────────────────────────────────
static const msn_backend_t *s_backend = NULL;

// ── Chooser screen state ─────────────────────────────────────────────────
static purr_win_t s_chooser_win        = 0;
static purr_wid_t s_meshtastic_status  = 0;
static purr_wid_t s_meshcore_status    = 0;
static purr_win_t s_switch_confirm_win = 0;
static purr_wid_t s_switch_confirm_lbl = 0;
static purr_mesh_backend_t s_pending_switch_target;

// ── Main chat window state ───────────────────────────────────────────────

static purr_win_t  s_buddy_win   = 0;
static purr_wid_t  s_buddy_list  = 0;
static purr_wid_t  s_room_list   = 0;
static purr_wid_t  s_status_lbl  = 0;
static TaskHandle_t s_refresh_task = NULL;
static bool         s_running      = false;
// Given by buddy_refresh_task() right before it self-deletes, waited on by
// close_chat_ui() before it destroys any window — see that function's
// comment for the use-after-free this closes. Same shape as meshchat.c's
// own original fix for the "close_ctrl_body_start" hang.
static SemaphoreHandle_t s_refresh_done = NULL;

// ── Buddies (1:1 DMs) ────────────────────────────────────────────────────

static char         s_buddy_id_strs[MAX_CHATS][24];
static char         s_buddy_labels[MAX_CHATS][80];
static const char   *s_buddy_label_ptrs[MAX_CHATS];
static int           s_buddy_count = 0;

// Per-buddy chat state (parallel arrays, indexed same as s_buddy_id_strs).
static char        s_chat_logs[MAX_CHATS][CHAT_LOG_LEN];
static size_t      s_chat_loglen[MAX_CHATS];
static purr_win_t  s_chat_win[MAX_CHATS];
static purr_wid_t  s_chat_out[MAX_CHATS];
static purr_wid_t  s_chat_in[MAX_CHATS];
static int         s_chat_scroll[MAX_CHATS];   // lines skipped from the top of the log — see render_chat_view()

// ── Rooms (channel group chat) ───────────────────────────────────────────

static char        s_room_names[MAX_ROOMS][12];
static const char  *s_room_label_ptrs[MAX_ROOMS];
static int          s_room_count = 0;

// Heap-allocated from PSRAM (not static arrays) — internal DRAM on this
// board is already razor-thin (see cupcake_module.c's static-stack-sizing
// comment); MAX_ROOMS(8) * CHAT_LOG_LEN(1024) static buffers here would
// overflow dram0_0_seg the same way one 1024-byte static broadcast log
// previously did.
static char        *s_room_logs[MAX_ROOMS];
static size_t        s_room_loglen[MAX_ROOMS];
static purr_win_t   s_room_win[MAX_ROOMS];
static purr_wid_t   s_room_out[MAX_ROOMS];
static purr_wid_t   s_room_in[MAX_ROOMS];
static int          s_room_scroll[MAX_ROOMS];   // lines skipped from the top of the log

// ── Add Room window ──────────────────────────────────────────────────────

static purr_win_t  s_addroom_win      = 0;
static purr_wid_t  s_addroom_name_in  = 0;
static purr_wid_t  s_addroom_psk_in   = 0;

// ── Manage window (forget nodes/rooms) ───────────────────────────────────

static purr_win_t  s_manage_win       = 0;
static purr_wid_t  s_manage_node_list = 0;
static purr_wid_t  s_manage_room_list = 0;

// ── SD persistence ────────────────────────────────────────────────────────
// Real, unbounded history on /sdcard/meshchat/ (directory name kept exactly
// as-is — not renamed to /sdcard/msn/ — specifically so existing users'
// Meshtastic chat history is picked up unchanged; kernel_tdp_boot.c already
// ensures this directory exists at boot). Independent of the in-memory
// display buffers above (CHAT_LOG_LEN-capped scrollback, unchanged). Keyed
// by msn_contact_t.id_str / a channel's on-air hash byte — stable identity,
// not table index (indices shift when a contact/room is forgotten).
// Everything here no-ops behind purr_kernel_sd_available() — no SD card
// means today's in-memory-only behavior, not a crash.

static void dm_path(const char *id_str, char *out, size_t out_max) {
    snprintf(out, out_max, "/sdcard/meshchat/dm_%s.txt", id_str);
}

static void room_path(int channel_idx, char *out, size_t out_max) {
    uint8_t hash = 0;
    s_backend->channel_hash(channel_idx, &hash);
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

// ── Buddy list ────────────────────────────────────────────────────────────

static void refresh_buddy_list(void) {
    purr_kernel_ui_breadcrumb("msn:refresh_buddy");
    int n = s_backend->contact_count();
    if (n > MAX_CHATS) n = MAX_CHATS;
    s_buddy_count = n;

    for (int i = 0; i < n; i++) {
        msn_contact_t info;
        if (!s_backend->contact_at(i, &info)) { s_buddy_count = i; break; }
        strncpy(s_buddy_id_strs[i], info.id_str, sizeof(s_buddy_id_strs[i]) - 1);
        if (info.last_seen_ms_ago == MSN_LAST_SEEN_UNKNOWN) {
            snprintf(s_buddy_labels[i], sizeof(s_buddy_labels[i]), "%s (never heard)", info.name);
        } else if (info.last_seen_ms_ago < ONLINE_WINDOW_MS) {
            snprintf(s_buddy_labels[i], sizeof(s_buddy_labels[i]), "%s (online)", info.name);
        } else {
            uint32_t age_min = info.last_seen_ms_ago / 60000UL;
            snprintf(s_buddy_labels[i], sizeof(s_buddy_labels[i]), "%s (%lum ago)",
                     info.name, (unsigned long)age_min);
        }
        s_buddy_label_ptrs[i] = s_buddy_labels[i];
    }

    if (s_buddy_list) {
        purr_kernel_ui_breadcrumb("msn:buddy_list_set_items");
        purr_win_list_set_items(s_buddy_list, s_buddy_label_ptrs, s_buddy_count);
    }
}

// ── Room list ─────────────────────────────────────────────────────────────

static void refresh_room_list(void) {
    purr_kernel_ui_breadcrumb("msn:refresh_room");
    int n = s_backend->channel_count();
    if (n > MAX_ROOMS) n = MAX_ROOMS;
    s_room_count = n;

    for (int i = 0; i < n; i++) {
        s_backend->channel_name(i, s_room_names[i], sizeof(s_room_names[i]));
        s_room_label_ptrs[i] = s_room_names[i];
    }

    if (s_room_list) {
        purr_kernel_ui_breadcrumb("msn:room_list_set_items");
        purr_win_list_set_items(s_room_list, s_room_label_ptrs, s_room_count);
    }
}

// Always-present status line — this and the Refresh/Add Room row below it
// are the fix for the "blank window" bug: previously the buddy list was the
// *only* widget in the window, so 0 discovered peers meant a fully empty
// box with nothing to look at or tap.
static void refresh_status_label(void) {
    purr_kernel_ui_breadcrumb("msn:refresh_status");
    if (!s_status_lbl) return;
    if (!s_backend->ready()) {
        purr_win_label_set(s_status_lbl, "Mesh: starting...");
    } else if (!s_backend->is_alive()) {
        purr_win_label_set(s_status_lbl, "Mesh: not responding");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s: ready (%d node%s)",
                 s_backend->name, s_buddy_count, s_buddy_count == 1 ? "" : "s");
        purr_win_label_set(s_status_lbl, buf);
    }
}

static void on_refresh_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    refresh_buddy_list();
    refresh_room_list();
    refresh_status_label();
}

static void buddy_refresh_task(void *arg) {
    (void)arg;
    while (s_running) {
        refresh_buddy_list();
        refresh_room_list();
        refresh_status_label();
        // Slept in short steps (not one 10s vTaskDelay) so a stop request
        // (s_running = false) is noticed within ~200ms, not up to 10s late.
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

// ── Scrolling ────────────────────────────────────────────────────────────
// No backend exposes real textarea scrolling through the purr_win_* catcall
// layer — see the original meshchat.c's comment for the full rationale.
// Renders a scrolled-down "window" into the log by re-calling textarea_set()
// with the first N lines skipped, rather than adding new engine plumbing.
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
    if (idx < 0 || idx >= MAX_ROOMS) return;
    s_room_scroll[idx] += CHAT_SCROLL_STEP;
    render_room_view(idx);
}

static void chat_log_append(int idx, const char *text) {
    log_append(s_chat_logs[idx], &s_chat_loglen[idx], text);
    render_chat_view(idx);
    char path[64];
    dm_path(s_buddy_id_strs[idx], path, sizeof(path));
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

    s_backend->send_text(idx, -1, text);
    purr_win_textarea_clear(s_chat_in[idx]);
}

static void open_chat(int idx) {
    if (idx < 0 || idx >= s_buddy_count) return;
    purr_kernel_ui_breadcrumb("msn:open_chat");
    if (s_chat_win[idx]) {
        purr_win_show(s_chat_win[idx]);
        return;
    }

    // First time this conversation's been opened this boot — resume its
    // history from SD before building the window.
    if (s_chat_loglen[idx] == 0) {
        char path[64];
        dm_path(s_buddy_id_strs[idx], path, sizeof(path));
        load_tail_from_sd(path, s_chat_logs[idx], &s_chat_loglen[idx], CHAT_LOG_LEN);
    }

    purr_kernel_ui_breadcrumb("msn:open_chat:win_create");
    s_chat_win[idx] = purr_win_create(s_buddy_labels[idx]);
    s_chat_out[idx] = purr_win_textarea(s_chat_win[idx], 100, 75);
    purr_wid_t row = purr_win_row(s_chat_win[idx], 4);
    s_chat_in[idx]  = purr_win_textarea(s_chat_win[idx], 60, 10);
    purr_win_button(s_chat_win[idx], "Send", on_chat_send, (void *)(intptr_t)idx);
    purr_win_button(s_chat_win[idx], "v", on_chat_scroll_click, (void *)(intptr_t)idx);
    purr_win_layout_end(row);

    purr_kernel_ui_breadcrumb("msn:open_chat:render");
    render_chat_view(idx);
    purr_win_textarea_focus(s_chat_in[idx]);
    // win_show() first — see terminal.c's terminal_init() for why (Cupcake's
    // win_show() raises the window above whatever kb_show() just showed).
    purr_kernel_ui_breadcrumb("msn:open_chat:show");
    purr_win_show(s_chat_win[idx]);
    purr_kernel_ui_breadcrumb("msn:open_chat:kb_show");
    purr_win_keyboard_show(s_chat_win[idx], s_chat_in[idx]);
    purr_kernel_ui_breadcrumb("msn:open_chat:done");
}

static void on_buddy_list_event(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)user;
    purr_kernel_ui_breadcrumb("msn:on_buddy_list_event");
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

    s_backend->send_text(-1, idx, text);
    purr_win_textarea_clear(s_room_in[idx]);
}

static void open_room(int idx) {
    if (idx < 0 || idx >= s_room_count) return;
    purr_kernel_ui_breadcrumb("msn:open_room");
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

    purr_kernel_ui_breadcrumb("msn:open_room:win_create");
    s_room_win[idx] = purr_win_create(s_room_names[idx]);
    s_room_out[idx] = purr_win_textarea(s_room_win[idx], 100, 75);
    purr_wid_t row = purr_win_row(s_room_win[idx], 4);
    s_room_in[idx]  = purr_win_textarea(s_room_win[idx], 60, 10);
    purr_win_button(s_room_win[idx], "Send", on_room_send, (void *)(intptr_t)idx);
    purr_win_button(s_room_win[idx], "v", on_room_scroll_click, (void *)(intptr_t)idx);
    purr_win_layout_end(row);

    purr_kernel_ui_breadcrumb("msn:open_room:render");
    render_room_view(idx);
    purr_win_textarea_focus(s_room_in[idx]);
    purr_kernel_ui_breadcrumb("msn:open_room:show");
    purr_win_show(s_room_win[idx]);
    purr_kernel_ui_breadcrumb("msn:open_room:kb_show");
    purr_win_keyboard_show(s_room_win[idx], s_room_in[idx]);
    purr_kernel_ui_breadcrumb("msn:open_room:done");
}

static void on_room_list_event(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)user;
    purr_kernel_ui_breadcrumb("msn:on_room_list_event");
    if (e != PURR_EVENT_ACTIVATED) return;
    int idx = purr_win_list_get_selected(s_room_list);
    open_room(idx);
}

// ── Add Room ──────────────────────────────────────────────────────────────────
// No QR/URL channel-sharing (out of scope) — the key is derived from
// whatever passphrase text the user types, via SHA-256 truncated to 16
// bytes, so two PURR devices typing the same passphrase land on the same
// key deterministically. For Meshtastic this won't match a real node's own
// custom-channel PSK (those are raw random bytes shared via QR/URL, not a
// passphrase) — interop there would need the exact same 16 raw bytes.

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
    int idx = s_backend->channel_add(name, psk16, sizeof(psk16));
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
    if (s_backend->channel_count() >= MAX_ROOMS) return;   // full

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
// A forget action changes which buddy/room ends up at which table index —
// rather than try to shift the parallel s_chat_*/s_room_* window/log arrays
// to match, every open chat/room window just gets closed and its in-memory
// log cleared. Re-opening any of them reloads correctly (dm_path()/
// room_path() are keyed by id_str / channel hash, not index) — a small UX
// cost for a deliberate, infrequent action, in exchange for zero risk of a
// stale log silently appearing under the wrong buddy's name.

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
    for (int i = 0; i < MAX_ROOMS; i++) {
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
    dm_path(s_buddy_id_strs[idx], path, sizeof(path));
    remove(path);
    s_backend->contact_forget(idx);
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
    s_backend->channel_remove(idx);
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
// Registers with the active backend's single unified RX-callback slot —
// both mesh modules already fire purr_kernel_notify() for every text
// message regardless (unchanged), so a message for a buddy/room whose
// window isn't open right now still surfaces as a notification banner.

static void on_mesh_rx(int contact_idx, int channel_idx, const char *text) {
    if (channel_idx >= 0) {
        if (channel_idx >= s_room_count) return;
        char line[280];
        snprintf(line, sizeof(line), "%s: %s\n", s_room_names[channel_idx], text);
        room_log_append(channel_idx, line);
        return;
    }

    if (contact_idx < 0 || contact_idx >= s_buddy_count) return;
    char line[280];
    snprintf(line, sizeof(line), "%s: %s\n", s_buddy_labels[contact_idx], text);
    chat_log_append(contact_idx, line);
}

// ── Chat UI lifecycle (Rooms/Buddies window + its background task) ─────────

static void open_chat_ui(void) {
    // Reused across relaunches like s_room_logs below — starts "empty"
    // (taken), which is exactly the state close_chat_ui()'s xSemaphoreTake()
    // below needs at the start of every run.
    if (!s_refresh_done) s_refresh_done = xSemaphoreCreateBinary();

    s_buddy_win  = purr_win_create("MSN");
    s_status_lbl = purr_win_label(s_buddy_win, "Mesh: starting...");

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

    s_backend->add_rx_cb(on_mesh_rx);

    s_running = true;
    // No NVS/flash/SD access anywhere in this task's own body — safe on a
    // PSRAM-backed stack (see app_manager.c's launch_native()/launch_meow()
    // for the same pattern).
    xTaskCreateWithCaps(buddy_refresh_task, "msn_ref", 4096, NULL, 3, &s_refresh_task, MALLOC_CAP_SPIRAM);
}

static void close_chat_ui(void) {
    s_running = false;
    // Wait for buddy_refresh_task() to actually notice s_running == false
    // and exit before touching any window/widget below — see
    // s_refresh_done's declaration comment for the use-after-free this
    // closes.
    if (s_refresh_done) xSemaphoreTake(s_refresh_done, pdMS_TO_TICKS(2000));
    s_refresh_task = NULL;

    if (s_backend) s_backend->remove_rx_cb(on_mesh_rx);

    for (int i = 0; i < MAX_CHATS; i++) {
        if (s_chat_win[i]) {
            purr_win_destroy(s_chat_win[i]);
            s_chat_win[i] = 0; s_chat_out[i] = 0; s_chat_in[i] = 0;
        }
    }
    for (int i = 0; i < MAX_ROOMS; i++) {
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
    if (s_buddy_win) purr_win_destroy(s_buddy_win);
    s_buddy_win = 0; s_buddy_list = 0; s_room_list = 0; s_status_lbl = 0;
    // s_chat_logs/s_chat_loglen/s_room_logs/s_room_loglen deliberately NOT
    // cleared — chat history persists across closing and relaunching the app.
}

// ── Chooser screen ───────────────────────────────────────────────────────

static void update_chooser_status(void) {
    bool mt_active = purr_kernel_get_module("meshtastic") != NULL;
    bool mc_active = purr_kernel_get_module("meshcore") != NULL;
    if (s_meshtastic_status) purr_win_label_set(s_meshtastic_status, mt_active ? "* active" : "");
    if (s_meshcore_status)   purr_win_label_set(s_meshcore_status,   mc_active ? "* active" : "");
}

static void close_chooser(void) {
    if (s_chooser_win) {
        purr_win_destroy(s_chooser_win);
        s_chooser_win = 0; s_meshtastic_status = 0; s_meshcore_status = 0;
    }
}

static void enter_chat_ui(const msn_backend_t *backend) {
    s_backend = backend;
    close_chooser();
    open_chat_ui();
}

static void do_switch(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    purr_mesh_backend_t target = s_pending_switch_target;
    int rc = purr_kernel_mesh_backend_switch(target);

    if (s_switch_confirm_win) {
        purr_win_destroy(s_switch_confirm_win);
        s_switch_confirm_win = 0; s_switch_confirm_lbl = 0;
    }

    if (rc != PURR_MODCTL_OK && rc != PURR_MODCTL_ERR_ALREADY) {
        update_chooser_status();   // switch failed — stay on the chooser, reflect real state
        return;
    }
    enter_chat_ui(target == PURR_MESH_BACKEND_MESHCORE ? msn_backend_meshcore() : msn_backend_meshtastic());
}

static void on_switch_cancel(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    if (s_switch_confirm_win) {
        purr_win_destroy(s_switch_confirm_win);
        s_switch_confirm_win = 0; s_switch_confirm_lbl = 0;
    }
}

static void open_switch_confirm(purr_mesh_backend_t target, const char *name) {
    s_pending_switch_target = target;
    if (s_switch_confirm_win) {
        purr_win_show(s_switch_confirm_win);
        return;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Switch to %s?", name);

    s_switch_confirm_win = purr_win_create("Switch Backend");
    s_switch_confirm_lbl = purr_win_label(s_switch_confirm_win, msg);
    purr_wid_t row = purr_win_row(s_switch_confirm_win, 4);
    purr_win_button(s_switch_confirm_win, "Switch", do_switch, NULL);
    purr_win_button(s_switch_confirm_win, "Cancel", on_switch_cancel, NULL);
    purr_win_layout_end(row);

    purr_win_show(s_switch_confirm_win);
}

static void on_meshtastic_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    if (purr_kernel_get_module("meshtastic")) {
        enter_chat_ui(msn_backend_meshtastic());
    } else {
        open_switch_confirm(PURR_MESH_BACKEND_MESHTASTIC, "Meshtastic");
    }
}

static void on_meshcore_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    if (purr_kernel_get_module("meshcore")) {
        enter_chat_ui(msn_backend_meshcore());
    } else {
        open_switch_confirm(PURR_MESH_BACKEND_MESHCORE, "MeshCore");
    }
}

static void open_chooser(void) {
    s_chooser_win = purr_win_create("MSN");
    purr_win_label(s_chooser_win, "Choose a mesh backend:");

    purr_wid_t mt_row = purr_win_row(s_chooser_win, 4);
    purr_win_button(s_chooser_win, "Meshtastic", on_meshtastic_click, NULL);
    s_meshtastic_status = purr_win_label(s_chooser_win, "");
    purr_win_layout_end(mt_row);

    purr_wid_t mc_row = purr_win_row(s_chooser_win, 4);
    purr_win_button(s_chooser_win, "MeshCore", on_meshcore_click, NULL);
    s_meshcore_status = purr_win_label(s_chooser_win, "");
    purr_win_layout_end(mc_row);

    update_chooser_status();
    purr_win_show(s_chooser_win);
}

// ── App lifecycle ─────────────────────────────────────────────────────────────

static int msn_init(void) {
    s_backend = NULL;
    open_chooser();
    return 0;
}

static void msn_deinit(void) {
    close_chat_ui();
    close_chooser();
    if (s_switch_confirm_win) {
        purr_win_destroy(s_switch_confirm_win);
        s_switch_confirm_win = 0; s_switch_confirm_lbl = 0;
    }
    s_backend = NULL;
}

// ── Module header ─────────────────────────────────────────────────────────────

PURR_MODULE_REGISTER(msn) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_APP,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "msn",
    .version           = "1.0.0",
    .kernel_min        = "0.11.1",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = msn_init,
    .deinit            = msn_deinit,
};
