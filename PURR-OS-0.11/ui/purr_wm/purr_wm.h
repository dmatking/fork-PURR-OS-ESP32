#pragma once
// purr_wm.h — PURR OS Window Manager
// Thin shell management layer on top of LVGL.
// Handles screen stack, shell switching, app lifecycle, notifications.

#include <stdint.h>
#include <stdbool.h>

#ifdef PURR_HAS_LVGL
#include <lvgl.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ── Shell IDs ─────────────────────────────────────────────────────────────────
typedef enum {
    PURR_SHELL_BLACKBERRY = 0,
    PURR_SHELL_EXPLORER,
    PURR_SHELL_CLASSICMAC,
    PURR_SHELL_COUNT
} purr_shell_t;

// ── App types ─────────────────────────────────────────────────────────────────
typedef enum {
    PURR_APP_PAWS = 0,   // user app, sandboxed
    PURR_APP_CLAW,       // system app, full access
} purr_app_type_t;

// ── WM Lifecycle ──────────────────────────────────────────────────────────────

// Initialize WM — call after LVGL init and KITT init
void purr_wm_init();

// Start the active shell — call from system_start()
void purr_wm_start();

// ── Shell Management ──────────────────────────────────────────────────────────

// Switch to a different shell (animated transition)
void purr_wm_switch_shell(purr_shell_t shell);

// Get currently active shell
purr_shell_t purr_wm_active_shell();

// ── App Lifecycle ─────────────────────────────────────────────────────────────

// Launch an app by path (.paws or .claw)
// Auto-detects runtime (Lua or WASM) from file magic bytes
bool purr_wm_launch(const char* path);

// Close top-most app and return to shell
void purr_wm_back();

// ── Notifications ─────────────────────────────────────────────────────────────

// Show a brief toast notification
void purr_wm_notify(const char* message, uint32_t duration_ms);

// ── Shell registration (called by shell implementations) ──────────────────────
#ifdef PURR_HAS_LVGL
typedef lv_obj_t* (*purr_shell_create_fn)(lv_obj_t* parent);
void purr_wm_register_shell(purr_shell_t id, purr_shell_create_fn create_fn);
#endif

#ifdef __cplusplus
}
#endif
