#pragma once
// purr_kernel.h — PURR OS kernel spine API
//
// This is what modules call into the kernel for. The kernel owns these
// registries. Modules register their catcalls here at init time. Other
// modules call through here — never directly into another module.

#include <stdint.h>
#include <stdbool.h>
#include "purr_module.h"
#include "../catcalls/catcalls.h"
#include "../catcalls/catcall_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Version ───────────────────────────────────────────────────────────────────

#define PURR_KERNEL_VERSION  "0.13.0"
#define KITT_VERSION         "0.9.2"

// ── Module loader ─────────────────────────────────────────────────────────────

// Called automatically by PURR_MODULE_REGISTER() constructors before app_main.
// Safe to call very early (just appends to a static list).
void purr_kernel_register_module_static(const purr_module_header_t *hdr);

// Load all modules pre-registered via purr_kernel_register_module_static().
// Sorts by priority and calls each module's init().
// P1 modules that fail to init trigger purr_kernel_panic().
// Call from boot.c instead of purr_kernel_scan_modules for built-in modules.
int  purr_kernel_load_static_modules(void);

// Scan a directory for .purr files sorted by priority, with optional SD
// fallback. Intended for SD card extras only — built-in modules use
// purr_kernel_load_static_modules() instead.
int  purr_kernel_scan_modules(const char *flash_dir, const char *sd_fallback_dir);

// Load a single .purr by path (SD card / file-based modules).
int  purr_kernel_load_module(const char *path);

// Unload by name.
void purr_kernel_unload_module(const char *name);

// Get loaded module header by name (NULL if not loaded).
const purr_module_header_t *purr_kernel_get_module(const char *name);

// Enumerate all registered modules (drivers, system, UI, and pre-linked apps).
int purr_kernel_module_count(void);
const purr_module_header_t *purr_kernel_module_at(int idx);

// ── Catcall registry ──────────────────────────────────────────────────────────
// Modules call these at init to register what they provide.
// The kernel stores one pointer per catcall type. Last registered wins,
// which allows override (e.g. fallback display replaced by real driver).

void purr_kernel_register_display(const catcall_display_t *drv);
void purr_kernel_register_touch  (const catcall_touch_t   *drv);
void purr_kernel_register_input  (const catcall_input_t   *drv);
void purr_kernel_register_radio  (const catcall_radio_t   *drv);
void purr_kernel_register_gps    (const catcall_gps_t     *drv);
void purr_kernel_register_ui     (const catcall_ui_t      *ui);

// Retrieve registered catcall implementations (NULL if none registered).
const catcall_display_t *purr_kernel_display(void);
const catcall_touch_t   *purr_kernel_touch(void);
const catcall_input_t   *purr_kernel_input(void);       // first registered (legacy)
int                       purr_kernel_input_count(void); // total registered inputs
const catcall_input_t    *purr_kernel_input_at(int idx); // iterate all inputs
const catcall_radio_t   *purr_kernel_radio(void);
const catcall_gps_t     *purr_kernel_gps(void);
const catcall_ui_t      *purr_kernel_ui(void);

// ── System info ───────────────────────────────────────────────────────────────

uint32_t purr_kernel_free_ram(void);
uint64_t purr_kernel_uptime_ms(void);
bool     purr_kernel_sd_available(void);
bool     purr_kernel_wifi_connected(void);
void     purr_kernel_reboot(void);

// Called by SD / WiFi modules when their state changes
void     purr_kernel_set_sd_available(bool v);
void     purr_kernel_set_wifi_connected(bool v);

// ── Panic ─────────────────────────────────────────────────────────────────────

// Kernel panic — logs reason to serial, draws panic screen if display is up,
// then halts. Never returns.
void __attribute__((noreturn)) purr_kernel_panic(const char *reason);

#ifdef __cplusplus
}
#endif
