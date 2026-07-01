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

#define PURR_KERNEL_VERSION  "1.0.0-dp1"
#define KITT_VERSION         "0.10.0"

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

// ── UI thread safety ──────────────────────────────────────────────────────────
// LVGL (and other catcall_ui_t backends) are not safe to call from more than
// one task at a time. The registered UI backend's own render/message-pump
// task must hold this lock for the duration of each pump call; purr_win.h's
// dispatch macros take it automatically around every call into the backend,
// so an app updating its UI from a background task (e.g. a periodic status
// refresh) is safe without doing anything extra. Recursive — safe to
// re-acquire from a widget callback invoked synchronously during the
// backend's own pump call (e.g. a button handler that itself calls
// purr_win_label_set() while the UI task is mid-render).
void purr_kernel_ui_lock(void);
void purr_kernel_ui_unlock(void);

// ── System info ───────────────────────────────────────────────────────────────

uint32_t purr_kernel_free_ram(void);
uint64_t purr_kernel_uptime_ms(void);
bool     purr_kernel_sd_available(void);
bool     purr_kernel_wifi_connected(void);
int      purr_kernel_battery_percent(void);  // -1 = unknown (no PMIC/fuel gauge)
bool     purr_kernel_lora_available(void);
void     purr_kernel_reboot(void);

// Called by SD / WiFi / PMIC / LoRa drivers when their state changes
void     purr_kernel_set_sd_available(bool v);
void     purr_kernel_set_wifi_connected(bool v);
void     purr_kernel_set_battery_percent(int v);
void     purr_kernel_set_lora_available(bool v);

// ── Notifications ─────────────────────────────────────────────────────────────
// In-memory ring buffer, open to any module/driver/app. Not persisted across
// reboot — cleared on boot. Newest entry is index 0.

#define PURR_NOTIFY_MAX        32
#define PURR_NOTIFY_TITLE_LEN  32
#define PURR_NOTIFY_BODY_LEN   64
#define PURR_NOTIFY_SOURCE_LEN 16

typedef struct {
    char     title[PURR_NOTIFY_TITLE_LEN];
    char     body[PURR_NOTIFY_BODY_LEN];
    char     source[PURR_NOTIFY_SOURCE_LEN];
    uint64_t timestamp_ms;
} purr_notification_t;

// Post a notification. Safe to call from any module, driver, or app.
void purr_kernel_notify(const char *title, const char *body, const char *source);

// Number of notifications currently held (<= PURR_NOTIFY_MAX).
int  purr_kernel_notify_count(void);

// Fetch notification at idx, newest-first (0 = most recent). Returns false
// if idx is out of range.
bool purr_kernel_notify_at(int idx, purr_notification_t *out);

void purr_kernel_notify_clear(void);

// ── Boot readiness ────────────────────────────────────────────────────────────
// Set once by the specialized kernel boot, after every static module/app has
// loaded and app_manager's registry is final. UI modules that build their UI
// from the app registry (e.g. cardstack) must poll this from their own task
// before reading it — their task can run before boot.c finishes, since
// FreeRTOS may preempt into a freshly created higher-priority task.

bool purr_kernel_boot_ready(void);
void purr_kernel_set_boot_ready(bool v);

// ── Panic ─────────────────────────────────────────────────────────────────────

// Kernel panic — logs reason to serial, draws panic screen if display is up,
// then halts. Never returns.
void __attribute__((noreturn)) purr_kernel_panic(const char *reason);

#ifdef __cplusplus
}
#endif
