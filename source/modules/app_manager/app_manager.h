#pragma once
// app_manager.h — PURR OS app manager public API
//
// The app manager is a .purr system module that:
//   - Scans /flash/apps and /sdcard/apps for .meow, .paws, .claw files
//   - Maintains a registry of available and running apps
//   - Provides the Cat Apps launcher UI (app grid over MiniWin)
//   - Enforces tier boundaries: .paws gets no kernel calls, .claw gets all

#include <stdint.h>
#include <stdbool.h>
#include "../../kernel/catcalls/catcall_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── App tiers ─────────────────────────────────────────────────────────────────

typedef enum {
    APP_TIER_MEOW = 0,   // Lua script — sandboxed VM, win.*/sd.*/kitt.* API
    APP_TIER_PAWS = 1,   // Compiled userland — win.*/sd.* only
    APP_TIER_CLAW = 2,   // Compiled kernel-access — full kernel API
} app_tier_t;

// ── App entry ─────────────────────────────────────────────────────────────────

typedef enum {
    APP_STATE_IDLE    = 0,
    APP_STATE_RUNNING = 1,
    APP_STATE_STOPPED = 2,
    APP_STATE_ERROR   = 3,
} app_state_t;

typedef struct {
    char        name[48];        // display name (from filename or embedded manifest)
    char        path[256];       // full path to the app file
    app_tier_t  tier;
    app_state_t state;
    char        error[96];       // populated on APP_STATE_ERROR
    purr_win_t  window;          // set automatically when the app calls purr_win_create();
                                  // 0 if it hasn't (yet), or never will
} app_entry_t;

// ── Public API ────────────────────────────────────────────────────────────────

// Called at boot by the kernel module loader
int  app_manager_init(void);
void app_manager_deinit(void);

// Re-scan app paths and rebuild the registry (hot-reload from SD)
int  app_manager_scan(void);

// Launch an app by index or by path. Returns 0 on success.
int  app_manager_launch_idx(int idx);
int  app_manager_launch_path(const char *path);

// Stop a running app by index
void app_manager_stop(int idx);

// Registry access (for Cat Apps UI)
int              app_manager_count(void);
const app_entry_t *app_manager_get(int idx);

// Open the Cat Apps launcher UI over the current MiniWin context
void app_manager_open_launcher(void);

// The path of the .meow script currently being launched — set right before
// launch_meow() creates its task, valid for lua_runtime's init() to read
// during that same launch. Only one Lua VM runs at a time on these boards.
const char *app_manager_get_pending_meow_path(void);

#ifdef __cplusplus
}
#endif
