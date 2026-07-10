#pragma once
// purr_kernel.h — PURR OS kernel spine API
//
// This is what modules call into the kernel for. The kernel owns these
// registries. Modules register their catcalls here at init time. Other
// modules call through here — never directly into another module.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "purr_module.h"
#include "../catcalls/catcalls.h"
#include "../catcalls/catcall_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Version ───────────────────────────────────────────────────────────────────

#define PURR_KERNEL_VERSION  "1.0.0-dp3"
#define KITT_VERSION         "1.0.0"

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

// Calls set_backlight() on whichever registered input driver implements it
// (e.g. bbq20) — keeps callers decoupled from which specific input driver
// actually has a backlight, mirroring how brightness goes through
// catcall_display rather than a specific display driver. No-op (returns
// ESP_ERR_NOT_SUPPORTED) if no registered input driver implements it.
esp_err_t purr_kernel_keyboard_set_backlight(uint8_t brightness);

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

// Call once per iteration from the active UI backend's own render/pump
// loop (right by its purr_kernel_ui_lock()/unlock() pair) — lets the
// kernel's health watchdog detect a fully hung UI task (one that's stopped
// pumping entirely, e.g. deadlocked on a lock it'll never release) and
// trigger purr_crash_guard's hang path. See purr_crash_guard.h.
void purr_kernel_ui_heartbeat(void);

// Optional: record which step of its own pump loop the active UI backend is
// currently inside (a string literal is fine — no copy is taken). Purely
// diagnostic: read back and logged by the health watchdog at the moment it
// detects a hang, so a stuck-forever iteration says *where* it's stuck
// instead of just "unresponsive" — the previous, first-time debugging pass
// on a live "miniwin hangs, cause unknown" report had nothing better to go
// on than that generic message. Cheap enough (one pointer store) to call at
// every step of a pump loop, not just once per iteration like the heartbeat
// above.
void purr_kernel_ui_breadcrumb(const char *step);

// ── Kernel log tail ───────────────────────────────────────────────────────────
// Captures every ESP_LOG* call system-wide into a small in-RAM scrollback
// buffer — call purr_kernel_klog_init() once, early in boot, then any
// module can pull recent kernel activity for on-screen display (e.g. a
// diagnostics screen) with no serial cable attached. Doesn't replace normal
// serial logging — the original vprintf is still called every time.
void   purr_kernel_klog_init(void);
// Copies up to out_size-1 of the most recent captured log text into out
// (null-terminated). Returns the number of bytes copied.
size_t purr_kernel_klog_tail(char *out, size_t out_size);

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

// Developer Mode — off by default, toggled from Settings (persisted under
// the "purr_settings" NVS namespace, synced here on settings' own init()).
// app_manager.c's launch_meow() checks this before running an unsigned
// .hiss script (a "-- purr-sig: unsigned" tag, or no tag at all) — signed
// .hiss scripts (dev-signed/trusted-signed/dev-approved) always run
// regardless of this flag; only "unsigned" is gated by it.
bool     purr_kernel_dev_mode_enabled(void);
void     purr_kernel_set_dev_mode(bool v);

// Screen idle timeout, minutes — off by default until Settings loads the
// persisted value (same "purr_settings" NVS namespace, synced on
// settings' own init()), defaulting to 1 minute in the meantime. Only
// Cupcake currently acts on this (cupcake_ui.c's idle check); other UI
// backends don't implement a lock screen yet, same as how
// keyboard_set_backlight only means something on drivers that implement it.
uint8_t  purr_kernel_screen_timeout_min(void);
void     purr_kernel_set_screen_timeout_min(uint8_t v);

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

// ── Service health registry ───────────────────────────────────────────────────
// Lets any module register a cheap "am I alive" check (e.g. meshtastic's
// heartbeat-staleness test) instead of each one spinning up its own watchdog
// task. A single shared kernel watchdog polls every registered check and
// calls purr_kernel_notify() itself the moment one transitions alive<->dead,
// so a hung/crashed service is surfaced without anything else having to be
// open or watching. The Services app enumerates this same registry to show
// live per-service status.

#define PURR_HEALTH_MAX  16

typedef bool (*purr_health_check_fn)(void);

// `name` is stored by pointer (not copied) and used verbatim as the
// notification source and the Services app's row label — pass a string
// literal or other static/permanent storage, not a stack buffer.
void purr_kernel_health_register(const char *name, purr_health_check_fn is_alive);

int  purr_kernel_health_count(void);

// Fetch registered check idx's name + current live status (calls its
// is_alive() right now — not a cached value). Returns false if idx is out
// of range.
bool purr_kernel_health_at(int idx, const char **name, bool *alive);

// ── App window tracking ───────────────────────────────────────────────────────
// Generic hook so purr_win.h can announce every window it creates without
// knowing app_manager exists — app_manager is the (single) consumer, opting
// in via purr_kernel_set_window_created_cb() at its own init, same pattern as
// the catcall registries above. Lets app_manager associate a running app with
// its purr_win_t so a "Running Apps" UI can show/restore it later, without
// every individual app needing to report its own window handle.

typedef void (*purr_window_created_cb_t)(purr_win_t win);

void purr_kernel_set_window_created_cb(purr_window_created_cb_t cb);

// Called by purr_win_create() — not meant to be called directly by apps.
void purr_kernel_notify_window_created(purr_win_t win);

// ── Boot readiness ────────────────────────────────────────────────────────────
// Set once by the specialized kernel boot, after every static module/app has
// loaded and app_manager's registry is final. UI modules that build their UI
// from the app registry (e.g. cardstack) must poll this from their own task
// before reading it — their task can run before boot.c finishes, since
// FreeRTOS may preempt into a freshly created higher-priority task.

bool purr_kernel_boot_ready(void);
void purr_kernel_set_boot_ready(bool v);

// ── Panic ─────────────────────────────────────────────────────────────────────

// Kernel panic — logs reason to serial, draws a red panic screen if display
// is up, holds 10s, then reboots. Thin wrapper over purr_kernel_panic_ex()
// with recoverable=false — this is the fatal path, used for Layer-0/P1
// REQUIRED failures. Never returns.
void __attribute__((noreturn)) purr_kernel_panic(const char *reason);

// Full panic screen. recoverable=false is identical to purr_kernel_panic()
// above (entity_name ignored). recoverable=true draws a blue "SUBSYSTEM
// DISABLED" screen instead — no auto-reboot, no dismiss, stays up until the
// user holds the on-screen reset target — used by purr_crash_guard.c once a
// P2/P3 entity (module or app) hits its strike threshold, or hangs outright.
// entity_name is shown on screen and included in the log-dump; may be NULL.
// Never returns.
void __attribute__((noreturn)) purr_kernel_panic_ex(const char *reason, bool recoverable, const char *entity_name);

// Optional hook a specialized kernel boot registers (e.g.
// kernel_tdeck_plus/kernel_tdp_boot.c, calling usb_msc_share_sd() —
// source/modules/usb_msc/usb_msc.h) so purr_kernel_panic_ex()'s recoverable
// branch can automatically expose the SD card over USB the moment the panic
// screen appears, without purr_kernel.c needing any direct dependency on
// USB/TinyUSB/SD specifics — same registration pattern as
// purr_kernel_set_window_created_cb() above. NULL (never registered) is a
// silent no-op — devices without USB MSC support just don't get this.
void purr_kernel_set_panic_usb_share_cb(void (*cb)(void));

#ifdef __cplusplus
}
#endif
