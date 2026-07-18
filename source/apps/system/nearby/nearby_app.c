// nearby_app.c — PURR OS Nearby (.claw)
//
// Read-only "who's nearby right now" list, driven by proximity_module.c's
// ESP-NOW beacon table. App is named/directoried "nearby" (not "proximity")
// two reasons: purr_kernel_get_module() looks up by name across one unified
// registry (would collide with the kernel module's own "proximity" name),
// and ESP-IDF's component system keys by directory basename (a same-named
// sibling directory under source/modules/proximity/ silently ate this
// component's build entirely the first time both were named "proximity" —
// confirmed live, not theoretical). Mirrors meshdiag.c's auto-refresh-task
// + semaphore-guarded-deinit shape (not meshchat.c/msn.c's — there's no
// chat/messaging concept here, just a list), combined with services_app.c's
// purr_win_list_set_items() row-building.

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "purr_win.h"
#include "purr_kernel.h"
#include "purr_module.h"
#include "proximity.h"
#include "pairing.h"

#define REFRESH_MS 2000
#define MAX_ROWS   PROXIMITY_MAX_DEVICES
#define MAX_PAIRED_ROWS PAIRING_MAX_DEVICES

static purr_win_t s_win        = 0;
static purr_wid_t s_list       = 0;
static purr_wid_t s_status_lbl = 0;
// Trust-list display — was a single "Paired with: X" label back when only
// one device could be paired at a time; now a proper list (see pairing.h's
// multi-device trust list) so "Unpair" can act on whichever row is
// selected instead of always the one-and-only pairing.
static purr_wid_t s_paired_list      = 0;
static purr_wid_t s_paired_status_lbl = 0;

// Pairing confirm dialog (initiator side) — open while pairing_get_state()
// == PAIRING_STATE_PENDING_OUTGOING, mirrors msn.c's own backend-switch
// confirm-dialog shape (small window, a label, Cancel). refresh_task()'s
// existing REFRESH_MS poll also drives closing this automatically once the
// peer accepts/rejects/times out — no separate task needed.
static purr_win_t s_pair_win     = 0;
static purr_wid_t s_pair_win_lbl = 0;

static TaskHandle_t s_refresh_task = NULL;
static bool         s_running      = false;
// Given by refresh_task() right before it self-deletes, waited on by
// nearby_app_deinit() before it destroys s_win — same use-after-free
// fix meshdiag.c's/msn.c's own s_refresh_done closes (deinit() previously
// destroying widgets a still-running background task might be mid-refresh
// on).
static SemaphoreHandle_t s_refresh_done = NULL;

static char        s_row_bufs[MAX_ROWS][64];
static const char *s_row_ptrs[MAX_ROWS];

static char        s_paired_row_bufs[MAX_PAIRED_ROWS][32];
static const char *s_paired_row_ptrs[MAX_PAIRED_ROWS];

static void close_pair_dialog(void) {
    if (s_pair_win) {
        purr_win_destroy(s_pair_win);
        s_pair_win = 0; s_pair_win_lbl = 0;
    }
}

static void refresh(void) {
    int n = proximity_device_count();
    if (n > MAX_ROWS) n = MAX_ROWS;

    for (int i = 0; i < n; i++) {
        proximity_device_t dev;
        if (!proximity_device_at(i, &dev)) { n = i; break; }
        uint32_t age_s = ((uint32_t)purr_kernel_uptime_ms() - dev.last_seen_ms) / 1000UL;
        // "[radio]" flags PROXIMITY_CAP_RADIO_COMPANION devices — the ones
        // on_pair_click() below will actually accept a pairing request.
        snprintf(s_row_bufs[i], sizeof(s_row_bufs[i]), "%s%s  (%d dBm, %lus ago)",
                 dev.name, (dev.caps & PROXIMITY_CAP_RADIO_COMPANION) ? " [radio]" : "",
                 (int)dev.rssi, (unsigned long)age_s);
        s_row_ptrs[i] = s_row_bufs[i];
    }
    if (s_list) purr_win_list_set_items(s_list, s_row_ptrs, n);

    if (s_status_lbl) {
        if (!proximity_ready()) {
            purr_win_label_set(s_status_lbl, "Proximity: starting...");
        } else if (!proximity_is_alive()) {
            purr_win_label_set(s_status_lbl, "Proximity: not responding");
        } else {
            char buf[48];
            snprintf(buf, sizeof(buf), "Proximity: ready (%d nearby)", n);
            purr_win_label_set(s_status_lbl, buf);
        }
    }

    // Auto-close the confirm dialog once the peer accepts (PAIRED) or the
    // request ends any other way (rejected/timed out both surface as a
    // reset back to NONE — see pairing_module.c) — the dialog has nothing
    // further to wait for in either case.
    if (s_pair_win && pairing_get_state() != PAIRING_STATE_PENDING_OUTGOING) {
        close_pair_dialog();
    }

    int paired_n = pairing_device_count();
    if (paired_n > MAX_PAIRED_ROWS) paired_n = MAX_PAIRED_ROWS;
    for (int i = 0; i < paired_n; i++) {
        paired_device_t pd;
        if (!pairing_device_at(i, &pd)) { paired_n = i; break; }
        snprintf(s_paired_row_bufs[i], sizeof(s_paired_row_bufs[i]), "%s%s",
                 pd.name, pairing_is_home_base(pd.mac) ? "  [home]" : "");
        s_paired_row_ptrs[i] = s_paired_row_bufs[i];
    }
    if (s_paired_list) purr_win_list_set_items(s_paired_list, s_paired_row_ptrs, paired_n);

    if (s_paired_status_lbl) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Paired devices: %d", paired_n);
        purr_win_label_set(s_paired_status_lbl, buf);
    }
}

static void on_refresh_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    refresh();
}

static void on_pair_cancel_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    pairing_cancel();
    close_pair_dialog();
}

static void open_pair_dialog(const char *peer_name) {
    char code[8];
    pairing_get_pending_code(code, sizeof(code));

    char msg[80];
    snprintf(msg, sizeof(msg), "Pairing with %s\nCode: %s\nWaiting for confirmation...",
             peer_name, code);

    s_pair_win = purr_win_create("Pairing");
    s_pair_win_lbl = purr_win_label(s_pair_win, msg);
    purr_win_button(s_pair_win, "Cancel", on_pair_cancel_click, NULL);
    purr_win_show(s_pair_win);
}

// Acts on whichever row is currently selected in the list — purr_win's
// list widget only offers click/select, not a press-and-hold gesture (no
// long-press concept anywhere in purr_win.h/catcall_ui.h), so pairing is a
// select-then-click-a-button flow rather than the hold gesture used
// elsewhere in this codebase for other confirm actions (e.g. oled_ui's
// button-timing idiom, which is a raw catcall_input_t consumer with no
// such constraint).
static void on_pair_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    // Only mid-negotiation blocks starting a new one — already having
    // other paired devices doesn't (pairing_start() enforces the same
    // rule; this is just an early UI-side bail so on_pair_click() doesn't
    // even try).
    pairing_state_t st = pairing_get_state();
    if (st != PAIRING_STATE_NONE && st != PAIRING_STATE_PAIRED) return;

    int idx = purr_win_list_get_selected(s_list);
    if (idx < 0) return;

    proximity_device_t dev;
    if (!proximity_device_at(idx, &dev)) return;
    if (!(dev.caps & PROXIMITY_CAP_RADIO_COMPANION)) return;   // not a pairable device

    if (pairing_start(dev.mac, dev.name)) {
        open_pair_dialog(dev.name);
    }
}

static void on_unpair_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    int idx = purr_win_list_get_selected(s_paired_list);
    if (idx < 0) return;
    paired_device_t pd;
    if (!pairing_device_at(idx, &pd)) return;
    pairing_forget(pd.mac);
    refresh();
}

// Toggles: selecting the current home base clears it, selecting any other
// paired row makes it the new one (pairing_set_home_base() only allows one
// at a time — see pairing.h).
static void on_set_home_base_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    int idx = purr_win_list_get_selected(s_paired_list);
    if (idx < 0) return;
    paired_device_t pd;
    if (!pairing_device_at(idx, &pd)) return;
    if (pairing_is_home_base(pd.mac)) {
        pairing_clear_home_base();
    } else {
        pairing_set_home_base(pd.mac);
    }
    refresh();
}

static void refresh_task(void *arg) {
    (void)arg;
    while (s_running) {
        refresh();
        // Short steps, not one REFRESH_MS vTaskDelay — nearby_app_deinit()
        // blocks on this task actually exiting, so how quickly it notices
        // s_running == false directly bounds how long a close/Kill stalls.
        for (int waited_ms = 0; waited_ms < REFRESH_MS && s_running; waited_ms += 200) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    if (s_refresh_done) xSemaphoreGive(s_refresh_done);
    // Must match the WithCaps variant used to create this task below.
    vTaskDeleteWithCaps(NULL);
}

static int nearby_app_init(void) {
    // Reused across relaunches — starts "empty" (taken), which is exactly
    // the state nearby_app_deinit()'s xSemaphoreTake() below needs at
    // the start of every run.
    if (!s_refresh_done) s_refresh_done = xSemaphoreCreateBinary();

    s_win = purr_win_create("Nearby");
    s_status_lbl = purr_win_label(s_win, "Proximity: starting...");

    purr_wid_t row = purr_win_row(s_win, 4);
    purr_win_button(s_win, "Refresh", on_refresh_click, NULL);
    purr_win_button(s_win, "Pair", on_pair_click, NULL);
    purr_win_button(s_win, "Unpair", on_unpair_click, NULL);
    purr_win_button(s_win, "Set Home", on_set_home_base_click, NULL);
    purr_win_layout_end(row);

    s_list = purr_win_list(s_win, 100, 40);
    s_paired_status_lbl = purr_win_label(s_win, "Paired devices: 0");
    s_paired_list = purr_win_list(s_win, 100, 20);

    refresh();
    purr_win_show(s_win);

    s_running = true;
    // No NVS/flash/SD access anywhere in this task's own body — safe on a
    // PSRAM-backed stack (proximity_module.c's own device table is
    // deliberately in-RAM only, same rationale as app_manager.c's launch_
    // native()/launch_meow() PSRAM-stack pattern this mirrors).
    xTaskCreateWithCaps(refresh_task, "proximity_ref", 4096, NULL, 3, &s_refresh_task, MALLOC_CAP_SPIRAM);
    return 0;
}

static void nearby_app_deinit(void) {
    s_running = false;
    // Wait for refresh_task() to actually exit before touching s_win below
    // — see s_refresh_done's declaration comment.
    if (s_refresh_done) xSemaphoreTake(s_refresh_done, pdMS_TO_TICKS(2000));
    s_refresh_task = NULL;

    close_pair_dialog();
    purr_win_destroy(s_win);
    s_win = 0; s_list = 0; s_status_lbl = 0;
    s_paired_list = 0; s_paired_status_lbl = 0;
}

// ── Module header ─────────────────────────────────────────────────────────

PURR_MODULE_REGISTER(nearby) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_APP,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "nearby",
    .version           = "1.0.0",
    .kernel_min        = "0.11.1",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = nearby_app_init,
    .deinit            = nearby_app_deinit,
};
