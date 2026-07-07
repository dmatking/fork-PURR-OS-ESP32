// driver_manager_app.c — PURR OS Driver Manager UI (.claw)
//
// Thin, read-only UI over the existing driver_manager.c system module's
// backend (driver_manager_get_count()/get_entry()) — that module already
// scans /flash/drivers + /sdcard/drivers, version-compares, checks
// kernel_min/max + required_catcalls, and tracks an OK/COMPAT/FAIL/SKIP
// status per driver. This app just lists it.

#include <stdio.h>
#include <string.h>
#include "purr_win.h"
#include "purr_module.h"
#include "driver_manager.h"

#define MAX_ROWS 32

static purr_win_t s_win        = 0;
static purr_wid_t s_list       = 0;
static purr_wid_t s_status_lbl = 0;

static char        s_row_bufs[MAX_ROWS][96];
static const char *s_row_ptrs[MAX_ROWS];

static void refresh(void) {
    int n = driver_manager_get_count();
    if (n > MAX_ROWS) n = MAX_ROWS;

    for (int i = 0; i < n; i++) {
        const drv_entry_t *d = driver_manager_get_entry(i);
        if (!d) { n = i; break; }
        if (d->status == DRV_STATUS_FAIL || d->status == DRV_STATUS_SKIP) {
            snprintf(s_row_bufs[i], sizeof(s_row_bufs[i]), "%s %-16s v%s (%s)",
                     drv_status_badge(d->status), d->name, d->version, d->fail_reason);
        } else {
            snprintf(s_row_bufs[i], sizeof(s_row_bufs[i]), "%s %-16s v%s",
                     drv_status_badge(d->status), d->name, d->version);
        }
        s_row_ptrs[i] = s_row_bufs[i];
    }
    purr_win_list_set_items(s_list, s_row_ptrs, n);

    char status[48];
    snprintf(status, sizeof(status), "%d driver(s) scanned", n);
    purr_win_label_set(s_status_lbl, status);
}

static void on_refresh(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    refresh();
}

static int driver_manager_app_init(void) {
    s_win = purr_win_create("Driver Manager");

    purr_win_button(s_win, "Refresh", on_refresh, NULL);
    s_list = purr_win_list(s_win, 100, 80);
    s_status_lbl = purr_win_label(s_win, "Ready.");

    purr_win_show(s_win);
    refresh();
    return 0;
}

static void driver_manager_app_deinit(void) {
    purr_win_destroy(s_win);
    s_win = 0; s_list = 0; s_status_lbl = 0;
}

// ── Module header ─────────────────────────────────────────────────────────────
// Registered name is "drivermgr" (not "driver_manager") to avoid colliding
// with the backend system module of that name in the kernel's module
// registry — app_manager looks apps up by this .name field.

PURR_MODULE_REGISTER(drivermgr) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_APP,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "drivermgr",
    .version           = "1.0.0",
    .kernel_min        = "0.11.1",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = driver_manager_app_init,
    .deinit            = driver_manager_app_deinit,
};
