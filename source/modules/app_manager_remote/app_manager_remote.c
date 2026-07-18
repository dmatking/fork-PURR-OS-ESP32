// app_manager_remote.c — responder side of the Remote Apps (Milkbar)
// protocol: answers proximity_rpc requests from a paired device wanting to
// list/launch/stop THIS device's apps. Pure wrapper around app_manager.h's
// existing public API — no new local behavior, just a new caller.
//
// A separate module (not folded into app_manager.c itself) specifically so
// devices without WiFi/proximity_rpc (most non-radio-companion targets)
// never pull this in — app_manager.c is universal across every device,
// this isn't. Auto-added by purrstrap.py's apply_radio_companion_
// defaults() alongside proximity/pairing/proximity_rpc, same condition
// (radio.wifi=true). Both this module and proximity_rpc register at
// P3/OPTIONAL — same-tier load order between them isn't guaranteed, which
// is why proximity_rpc_register() is deliberately safe to call before
// proximity_rpc_init() has actually run (see that function's own comment).

#include "../app_manager/app_manager.h"
#include "app_manager_remote.h"
#include "../proximity_rpc/proximity_rpc.h"
#include "../../kernel/core/purr_module.h"
#include <string.h>

// remote_app_entry_t is deliberately NOT app_entry_t itself (that struct
// carries local-only fields like purr_win_t window and a 256-byte path no
// remote caller needs) — see app_manager_remote.h for the wire shape both
// this responder and Milkbar's caller side share.

static bool handle_list(const uint8_t mac[6], uint16_t action_id,
                         const uint8_t *req, size_t req_len,
                         uint8_t *resp_out, size_t resp_cap, size_t *resp_len_out) {
    (void)mac; (void)action_id; (void)req; (void)req_len;
    int n = app_manager_count();
    size_t max_entries = resp_cap / sizeof(remote_app_entry_t);
    if ((size_t)n > max_entries) n = (int)max_entries;   // truncate rather than overflow the caller's buffer

    remote_app_entry_t *out = (remote_app_entry_t *)resp_out;
    int written = 0;
    for (int i = 0; i < n; i++) {
        const app_entry_t *app = app_manager_get(i);
        if (!app) continue;
        memset(&out[written], 0, sizeof(out[written]));
        strncpy(out[written].name, app->name, sizeof(out[written].name) - 1);
        out[written].tier  = (uint8_t)app->tier;
        out[written].state = (uint8_t)app->state;
        written++;
    }
    *resp_len_out = (size_t)written * sizeof(remote_app_entry_t);
    return true;
}

// Request payload for LAUNCH/STOP — a bare app name, matching
// remote_app_entry_t.name's size. Indices aren't stable across devices
// (see proximity_rpc.h's Phase C notes), so name is the only reliable key
// a remote caller has after a LIST response.
static int find_by_name(const char *name) {
    int n = app_manager_count();
    for (int i = 0; i < n; i++) {
        const app_entry_t *app = app_manager_get(i);
        if (app && strncmp(app->name, name, sizeof(app->name)) == 0) return i;
    }
    return -1;
}

static bool handle_launch(const uint8_t mac[6], uint16_t action_id,
                           const uint8_t *req, size_t req_len,
                           uint8_t *resp_out, size_t resp_cap, size_t *resp_len_out) {
    (void)mac; (void)action_id; (void)resp_out; (void)resp_cap;
    char name[48] = {0};
    size_t n = req_len < sizeof(name) - 1 ? req_len : sizeof(name) - 1;
    memcpy(name, req, n);

    bool ok = app_manager_launch_by_name(name) == 0;
    *resp_len_out = 0;
    return ok;
}

static bool handle_stop(const uint8_t mac[6], uint16_t action_id,
                         const uint8_t *req, size_t req_len,
                         uint8_t *resp_out, size_t resp_cap, size_t *resp_len_out) {
    (void)mac; (void)action_id; (void)resp_out; (void)resp_cap;
    char name[48] = {0};
    size_t n = req_len < sizeof(name) - 1 ? req_len : sizeof(name) - 1;
    memcpy(name, req, n);

    int idx = find_by_name(name);
    *resp_len_out = 0;
    if (idx < 0) return false;
    app_manager_stop(idx);
    return true;
}

void app_manager_remote_register(void) {
    proximity_rpc_register(REMOTEAPPS_ACTION_LIST,   handle_list);
    proximity_rpc_register(REMOTEAPPS_ACTION_LAUNCH, handle_launch);
    proximity_rpc_register(REMOTEAPPS_ACTION_STOP,   handle_stop);
}

// ── Module lifecycle ──────────────────────────────────────────────────────

static int module_init(void) {
    app_manager_remote_register();
    return 0;
}

// ── Module header ─────────────────────────────────────────────────────────

PURR_MODULE_REGISTER(app_manager_remote) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_SYSTEM,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "app_manager_remote",
    .version           = "1.0.0",
    .kernel_min        = "0.11.1",
    .kernel_max        = "",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = module_init,
    .deinit            = NULL,
};
