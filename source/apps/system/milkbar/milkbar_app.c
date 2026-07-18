// milkbar_app.c — PURR OS Milkbar (Remote Apps manager).
//
// Top list: this device's paired trust list (pairing.h — multi-device,
// see pairing_module.c). Selecting a row queries that device's app list
// over proximity_rpc_call() (REMOTEAPPS_ACTION_LIST, app_manager_remote.h)
// and shows it in the bottom list; Launch/Stop act on whichever app row is
// selected there. Same refresh_task + semaphore-guarded-deinit shape as
// nearby_app.c/msn.c/meshdiag.c in this codebase — the one difference is
// that proximity_rpc_call() is a real blocking network call (up to its own
// timeout), so it only ever runs on this app's own background task, never
// on cupcake_task (see proximity_rpc.h's own warning about that).

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "purr_win.h"
#include "purr_kernel.h"
#include "purr_module.h"
#include "pairing.h"
#include "proximity_rpc.h"
#include "app_manager_remote.h"

#ifdef CONFIG_PURR_UI_BACKEND_CUPCAKE
#include "cupcake.h"
#endif

#define REFRESH_MS        2000
#define RPC_TIMEOUT_MS    3000
#define MAX_DEVICE_ROWS   PAIRING_MAX_DEVICES
#define MAX_APP_ROWS      32   // PROXIMITY_RPC_MAX_MSG / sizeof(remote_app_entry_t) headroom, see app_manager_remote.h

static purr_win_t s_win         = 0;
static purr_wid_t s_device_list = 0;
static purr_wid_t s_app_list    = 0;
static purr_wid_t s_status_lbl  = 0;

static TaskHandle_t s_refresh_task = NULL;
static bool         s_running      = false;
// Same use-after-free fix as nearby_app.c/meshdiag.c/msn.c's own
// s_refresh_done — deinit() waits for the background task to actually
// exit before destroying widgets it might be mid-refresh on.
static SemaphoreHandle_t s_refresh_done = NULL;

static char        s_device_row_bufs[MAX_DEVICE_ROWS][32];
static const char *s_device_row_ptrs[MAX_DEVICE_ROWS];
static int          s_device_count = 0;

// Set from on_device_list_event() (cupcake_task, UI click) — read by
// refresh_task() (its own background task) to know which device's app
// list to fetch next poll. all-zero mac means "no device selected yet".
static uint8_t       s_selected_mac[6];
static volatile bool s_have_selection = false;

static char        s_app_row_bufs[MAX_APP_ROWS][64];
static const char *s_app_row_ptrs[MAX_APP_ROWS];
#ifdef CONFIG_PURR_UI_BACKEND_CUPCAKE
// Every row currently gets the same generic icon — remote app entries
// (remote_app_entry_t, app_manager_remote.h) carry no icon data at all
// (the LOCAL app_entry_t doesn't either; icons are a UI-layer-only lookup
// today, see cupcake's own app-drawer icon mapping), so the real per-row
// differentiator is the name text, not the glyph. Matches how this was
// explicitly scoped when asked for: "the name of the app... because
// they're all the same icon right now."
static const char *s_app_row_icons[MAX_APP_ROWS];
#endif
static int          s_app_count = 0;
static remote_app_entry_t s_last_apps[MAX_APP_ROWS];   // raw entries, for Launch/Stop to read state/name by row index

static void refresh_device_list(void) {
    int n = pairing_device_count();
    if (n > MAX_DEVICE_ROWS) n = MAX_DEVICE_ROWS;
    for (int i = 0; i < n; i++) {
        paired_device_t pd;
        if (!pairing_device_at(i, &pd)) { n = i; break; }
        snprintf(s_device_row_bufs[i], sizeof(s_device_row_bufs[i]), "%s", pd.name);
        s_device_row_ptrs[i] = s_device_row_bufs[i];
    }
    s_device_count = n;
    if (s_device_list) purr_win_list_set_items(s_device_list, s_device_row_ptrs, s_device_count);
}

// Runs on refresh_task() — see this file's top comment for why this can
// never be called from cupcake_task.
static void refresh_app_list_from_remote(void) {
    if (!s_have_selection) {
        s_app_count = 0;
        if (s_app_list) purr_win_list_set_items(s_app_list, s_app_row_ptrs, 0);
        if (s_status_lbl) purr_win_label_set(s_status_lbl, "Select a paired device");
        return;
    }

    uint8_t resp[PROXIMITY_RPC_MAX_MSG];
    size_t  resp_len = 0;
    bool ok = proximity_rpc_call(s_selected_mac, REMOTEAPPS_ACTION_LIST, NULL, 0,
                                  resp, sizeof(resp), &resp_len, RPC_TIMEOUT_MS);
    if (!ok) {
        s_app_count = 0;
        if (s_app_list) purr_win_list_set_items(s_app_list, s_app_row_ptrs, 0);
        if (s_status_lbl) purr_win_label_set(s_status_lbl, "Remote device not responding");
        return;
    }

    int n = (int)(resp_len / sizeof(remote_app_entry_t));
    if (n > MAX_APP_ROWS) n = MAX_APP_ROWS;
    memcpy(s_last_apps, resp, (size_t)n * sizeof(remote_app_entry_t));
    for (int i = 0; i < n; i++) {
        snprintf(s_app_row_bufs[i], sizeof(s_app_row_bufs[i]), "%s%s",
                 s_last_apps[i].name, s_last_apps[i].state == 1 /* APP_STATE_RUNNING */ ? "  (running)" : "");
        s_app_row_ptrs[i] = s_app_row_bufs[i];
#ifdef CONFIG_PURR_UI_BACKEND_CUPCAKE
        s_app_row_icons[i] = LV_SYMBOL_FILE;
#endif
    }
    s_app_count = n;

    if (s_app_list) {
#ifdef CONFIG_PURR_UI_BACKEND_CUPCAKE
        cupcake_win_list_set_items_icon(s_app_list, s_app_row_ptrs, s_app_row_icons, s_app_count);
#else
        purr_win_list_set_items(s_app_list, s_app_row_ptrs, s_app_count);
#endif
    }
    if (s_status_lbl) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%d remote app%s", s_app_count, s_app_count == 1 ? "" : "s");
        purr_win_label_set(s_status_lbl, buf);
    }
}

static void on_device_list_event(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)user;
    if (e != PURR_EVENT_ACTIVATED) return;
    int idx = purr_win_list_get_selected(s_device_list);
    if (idx < 0) return;
    paired_device_t pd;
    if (!pairing_device_at(idx, &pd)) return;
    memcpy(s_selected_mac, pd.mac, 6);
    s_have_selection = true;
    if (s_status_lbl) purr_win_label_set(s_status_lbl, "Loading...");
    // Actual fetch happens on refresh_task()'s own next pass, not here —
    // this callback runs on cupcake_task.
}

static void on_launch_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    if (!s_have_selection) return;
    int idx = purr_win_list_get_selected(s_app_list);
    if (idx < 0 || idx >= s_app_count) return;

    uint8_t resp[16]; size_t resp_len = 0;
    bool ok = proximity_rpc_call(s_selected_mac, REMOTEAPPS_ACTION_LAUNCH,
                                  (const uint8_t *)s_last_apps[idx].name, strlen(s_last_apps[idx].name),
                                  resp, sizeof(resp), &resp_len, RPC_TIMEOUT_MS);
    if (s_status_lbl) purr_win_label_set(s_status_lbl, ok ? "Launched" : "Launch failed");
}

static void on_stop_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    if (!s_have_selection) return;
    int idx = purr_win_list_get_selected(s_app_list);
    if (idx < 0 || idx >= s_app_count) return;

    uint8_t resp[16]; size_t resp_len = 0;
    bool ok = proximity_rpc_call(s_selected_mac, REMOTEAPPS_ACTION_STOP,
                                  (const uint8_t *)s_last_apps[idx].name, strlen(s_last_apps[idx].name),
                                  resp, sizeof(resp), &resp_len, RPC_TIMEOUT_MS);
    if (s_status_lbl) purr_win_label_set(s_status_lbl, ok ? "Stopped" : "Stop failed");
}

static void on_refresh_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    refresh_device_list();
}

static void refresh_task(void *arg) {
    (void)arg;
    while (s_running) {
        refresh_device_list();
        refresh_app_list_from_remote();   // no-op fast path if nothing selected yet
        // Short steps, not one REFRESH_MS vTaskDelay — same reasoning as
        // nearby_app.c's own refresh_task(): milkbar_app_deinit() blocks on
        // this task actually exiting, so how quickly it notices
        // s_running == false directly bounds how long a close/Kill stalls.
        // A live proximity_rpc_call() in flight when s_running flips to
        // false still has to finish or time out first either way — up to
        // RPC_TIMEOUT_MS, not bounded by this loop's own step size.
        for (int waited_ms = 0; waited_ms < REFRESH_MS && s_running; waited_ms += 200) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    if (s_refresh_done) xSemaphoreGive(s_refresh_done);
    vTaskDeleteWithCaps(NULL);
}

static int milkbar_app_init(void) {
    if (!s_refresh_done) s_refresh_done = xSemaphoreCreateBinary();

    s_win = purr_win_create("Milkbar");
    purr_win_label(s_win, "Paired devices:");
    s_device_list = purr_win_list(s_win, 100, 30);
    purr_win_list_on_select(s_device_list, on_device_list_event, NULL);

    purr_wid_t row = purr_win_row(s_win, 4);
    purr_win_button(s_win, "Refresh", on_refresh_click, NULL);
    purr_win_button(s_win, "Launch", on_launch_click, NULL);
    purr_win_button(s_win, "Stop", on_stop_click, NULL);
    purr_win_layout_end(row);

    s_status_lbl = purr_win_label(s_win, "Select a paired device");
    s_app_list = purr_win_list(s_win, 100, 30);

    s_have_selection = false;
    refresh_device_list();
    purr_win_show(s_win);

    s_running = true;
    // Background task does the (potentially slow, blocking) proximity_rpc_
    // call() work — see this file's top comment. PSRAM-backed stack: no
    // NVS/flash access anywhere in this task's own body, same rationale as
    // nearby_app.c's identical refresh_task() pattern.
    xTaskCreateWithCaps(refresh_task, "milkbar_ref", 4096, NULL, 3, &s_refresh_task, MALLOC_CAP_SPIRAM);
    return 0;
}

static void milkbar_app_deinit(void) {
    s_running = false;
    if (s_refresh_done) xSemaphoreTake(s_refresh_done, pdMS_TO_TICKS(RPC_TIMEOUT_MS + 500));
    s_refresh_task = NULL;

    purr_win_destroy(s_win);
    s_win = 0; s_device_list = 0; s_app_list = 0; s_status_lbl = 0;
    s_have_selection = false;
}

// ── Module header ─────────────────────────────────────────────────────────

PURR_MODULE_REGISTER(milkbar) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_APP,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "milkbar",
    .version           = "1.0.0",
    .kernel_min        = "0.11.1",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = milkbar_app_init,
    .deinit            = milkbar_app_deinit,
};
