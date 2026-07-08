// purr_kernel.c — PURR OS kernel spine implementation
//
// Module loading order:
//   1. Collect all .purr headers from flash_dir (read-only scan, no init)
//   2. Sort by load_priority ascending (1=REQUIRED first)
//   3. For each entry attempt load from flash path
//      - If fails AND priority == REQUIRED: try SD fallback, panic if still missing
//      - If fails AND priority == IMPORTANT: log warning, continue
//      - If fails AND priority == OPTIONAL: silent continue
//   4. SD card (/sdcard/modules, /sdcard/drivers) is scanned afterwards for
//      any additional optional modules not present on flash

#include "purr_kernel.h"
#include "purr_crash_guard.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>

static const char *TAG = "purr_kernel";

// ── Catcall registry ──────────────────────────────────────────────────────────

static const catcall_display_t *s_display = NULL;
static const catcall_touch_t   *s_touch   = NULL;
#define MAX_INPUTS 4
static const catcall_input_t   *s_inputs[MAX_INPUTS];
static int                      s_input_count = 0;
static const catcall_radio_t   *s_radio   = NULL;
static const catcall_gps_t     *s_gps     = NULL;
static const catcall_ui_t      *s_ui      = NULL;

void purr_kernel_register_display(const catcall_display_t *drv) {
    s_display = drv;
    ESP_LOGI(TAG, "display registered: %s", drv ? drv->name : "null");
}
void purr_kernel_register_touch(const catcall_touch_t *drv) {
    s_touch = drv;
    ESP_LOGI(TAG, "touch registered: %s", drv ? drv->name : "null");
}
void purr_kernel_register_input(const catcall_input_t *drv) {
    if (!drv) return;
    if (s_input_count < MAX_INPUTS) {
        s_inputs[s_input_count++] = drv;
    }
    ESP_LOGI(TAG, "input registered: %s (%d total)", drv->name, s_input_count);
}
void purr_kernel_register_radio(const catcall_radio_t *drv) {
    s_radio = drv;
    ESP_LOGI(TAG, "radio registered: %s", drv ? drv->name : "null");
}
void purr_kernel_register_gps(const catcall_gps_t *drv) {
    s_gps = drv;
    ESP_LOGI(TAG, "gps registered: %s", drv ? drv->name : "null");
}
void purr_kernel_register_ui(const catcall_ui_t *ui) {
    s_ui = ui;
    ESP_LOGI(TAG, "ui registered: %s", ui ? ui->name : "null");
}

const catcall_display_t *purr_kernel_display(void) { return s_display; }
const catcall_touch_t   *purr_kernel_touch(void)   { return s_touch; }
const catcall_input_t   *purr_kernel_input(void)    { return s_input_count > 0 ? s_inputs[0] : NULL; }
int                       purr_kernel_input_count(void) { return s_input_count; }
const catcall_input_t    *purr_kernel_input_at(int i)   { return (i >= 0 && i < s_input_count) ? s_inputs[i] : NULL; }
const catcall_radio_t   *purr_kernel_radio(void)   { return s_radio; }
const catcall_gps_t     *purr_kernel_gps(void)     { return s_gps; }
const catcall_ui_t      *purr_kernel_ui(void)      { return s_ui; }

esp_err_t purr_kernel_keyboard_set_backlight(uint8_t brightness) {
    for (int i = 0; i < s_input_count; i++) {
        if (s_inputs[i] && s_inputs[i]->set_backlight) {
            return s_inputs[i]->set_backlight(brightness);
        }
    }
    return ESP_ERR_NOT_SUPPORTED;
}

// ── App window tracking ───────────────────────────────────────────────────────

static purr_window_created_cb_t s_window_created_cb = NULL;

void purr_kernel_set_window_created_cb(purr_window_created_cb_t cb) {
    s_window_created_cb = cb;
}
void purr_kernel_notify_window_created(purr_win_t win) {
    if (s_window_created_cb) s_window_created_cb(win);
}

// ── UI thread safety ──────────────────────────────────────────────────────────
//
// LVGL (and any other catcall_ui_t backend) is not safe to call from two
// tasks at once. The UI backend's own task holds this for each render/
// message-pump call; purr_win.h's dispatch macros hold it around every call
// into the backend, so an app's background task (e.g. a periodic status
// updater) can't race the render loop. Created lazily on first use — the
// first call always happens single-threaded during a module's synchronous
// init() on the boot task, before any UI task is spun up, so there's no
// init race in practice.
//
// Recursive because a widget event callback (e.g. a button handler) runs
// synchronously from inside the backend's own pump call, on the same task
// that's already holding the lock — if that callback calls back into
// purr_win_* (settings.c's on_theme_wce does exactly this via
// purr_win_label_set()), a non-recursive mutex would deadlock against itself.
static SemaphoreHandle_t s_ui_mutex = NULL;

void purr_kernel_ui_lock(void) {
    if (!s_ui_mutex) s_ui_mutex = xSemaphoreCreateRecursiveMutex();
    if (s_ui_mutex) xSemaphoreTakeRecursive(s_ui_mutex, portMAX_DELAY);
}

void purr_kernel_ui_unlock(void) {
    if (s_ui_mutex) xSemaphoreGiveRecursive(s_ui_mutex);
}

// ── Module registry ───────────────────────────────────────────────────────────

#define MAX_MODULES 32

typedef struct {
    purr_module_header_t header;
    bool                 loaded;
} module_slot_t;

static module_slot_t s_modules[MAX_MODULES];
static int           s_module_count = 0;

// ── Version comparison ────────────────────────────────────────────────────────

static int version_cmp(const char *a, const char *b) {
    unsigned ma=0,mi=0,pa=0, mb=0,mib=0,pb=0;
    sscanf(a, "%u.%u.%u", &ma, &mi, &pa);
    sscanf(b, "%u.%u.%u", &mb, &mib, &pb);
    if (ma != mb) return ma < mb ? -1 : 1;
    if (mi != mib) return mi < mib ? -1 : 1;
    if (pa != pb) return pa < pb ? -1 : 1;
    return 0;
}

// Returns the s_modules[] index already holding this name, or -1. Used to
// detect the same module present in both /flash and /sdcard (or an SD-card
// update of a module already loaded from flash) before appending a second
// copy — see purr_kernel_load_module().
static int find_module_slot_by_name(const char *name) {
    for (int i = 0; i < s_module_count; i++) {
        if (strncmp(s_modules[i].header.name, name, sizeof(s_modules[i].header.name)) == 0) {
            return i;
        }
    }
    return -1;
}

// ── Panic ─────────────────────────────────────────────────────────────────────
//
// Two variants, sharing one text-rendering core built ONLY on
// catcall_display_t::fill_rect() and catcall_touch_t (the only primitives
// those catcalls expose) — deliberately no LVGL/MiniWin/app_manager
// dependency, since the whole point of both screens is that they must
// still work when the thing that's broken is a P2/P3 module, including the
// active UI backend itself. See purr_crash_guard.h for the guard that
// decides when the recoverable (blue) variant fires.
//
// Font: a compact 3x5 dot-matrix, uppercase + digits + a minimal
// punctuation set only (not the full mixed-case ASCII range) — hand-
// authoring bitmap glyph data carries real risk of unnoticed bit errors
// with no way to visually proof each one before flashing, so the character
// set is deliberately kept small and every string is force-uppercased
// before rendering. Unsupported characters fall back to a blank glyph.

typedef struct { char c; uint8_t rows[5]; } panic_glyph_t;

// Each row's 3 bits = left,mid,right column (bit2=left). Every entry below
// was authored from an explicit 3x5 on/off grid, not transcribed from
// memory of an existing font table.
static const panic_glyph_t PANIC_FONT[] = {
    {' ', {0x0,0x0,0x0,0x0,0x0}},
    {'!', {0x2,0x2,0x2,0x0,0x2}},
    {'(', {0x3,0x4,0x4,0x4,0x3}},
    {')', {0x6,0x1,0x1,0x1,0x6}},
    {',', {0x0,0x0,0x0,0x2,0x4}},
    {'-', {0x0,0x0,0x7,0x0,0x0}},
    {'.', {0x0,0x0,0x0,0x0,0x2}},
    {'/', {0x1,0x1,0x2,0x4,0x4}},
    {':', {0x0,0x2,0x0,0x2,0x0}},
    {'_', {0x0,0x0,0x0,0x0,0x7}},
    {'0', {0x7,0x5,0x5,0x5,0x7}},
    {'1', {0x2,0x6,0x2,0x2,0x7}},
    {'2', {0x7,0x1,0x7,0x4,0x7}},
    {'3', {0x7,0x1,0x7,0x1,0x7}},
    {'4', {0x5,0x5,0x7,0x1,0x1}},
    {'5', {0x7,0x4,0x7,0x1,0x7}},
    {'6', {0x7,0x4,0x7,0x5,0x7}},
    {'7', {0x7,0x1,0x2,0x2,0x2}},
    {'8', {0x7,0x5,0x7,0x5,0x7}},
    {'9', {0x7,0x5,0x7,0x1,0x7}},
    {'A', {0x2,0x5,0x7,0x5,0x5}},
    {'B', {0x6,0x5,0x6,0x5,0x6}},
    {'C', {0x3,0x4,0x4,0x4,0x3}},
    {'D', {0x6,0x5,0x5,0x5,0x6}},
    {'E', {0x7,0x4,0x7,0x4,0x7}},
    {'F', {0x7,0x4,0x7,0x4,0x4}},
    {'G', {0x3,0x4,0x5,0x5,0x3}},
    {'H', {0x5,0x5,0x7,0x5,0x5}},
    {'I', {0x7,0x2,0x2,0x2,0x7}},
    {'J', {0x1,0x1,0x1,0x5,0x2}},
    {'K', {0x5,0x5,0x6,0x5,0x5}},
    {'L', {0x4,0x4,0x4,0x4,0x7}},
    {'M', {0x5,0x7,0x5,0x5,0x5}},
    {'N', {0x5,0x7,0x7,0x7,0x5}},
    {'O', {0x7,0x5,0x5,0x5,0x7}},
    {'P', {0x7,0x5,0x7,0x4,0x4}},
    {'Q', {0x7,0x5,0x5,0x7,0x1}},
    {'R', {0x7,0x5,0x6,0x5,0x5}},
    {'S', {0x3,0x4,0x7,0x1,0x6}},
    {'T', {0x7,0x2,0x2,0x2,0x2}},
    {'U', {0x5,0x5,0x5,0x5,0x7}},
    {'V', {0x5,0x5,0x5,0x5,0x2}},
    {'W', {0x5,0x5,0x7,0x7,0x5}},
    {'X', {0x5,0x5,0x2,0x5,0x5}},
    {'Y', {0x5,0x5,0x2,0x2,0x2}},
    {'Z', {0x7,0x1,0x2,0x4,0x7}},
};
#define PANIC_FONT_COUNT (sizeof(PANIC_FONT) / sizeof(PANIC_FONT[0]))

#define PANIC_SCALE   4
#define PANIC_CHAR_W  (3 * PANIC_SCALE + PANIC_SCALE)   // glyph + 1-cell gap
#define PANIC_CHAR_H  (5 * PANIC_SCALE + PANIC_SCALE)   // glyph + 1-line gap

static const panic_glyph_t *panic_glyph_for(char c)
{
    for (size_t i = 0; i < PANIC_FONT_COUNT; i++)
        if (PANIC_FONT[i].c == c) return &PANIC_FONT[i];
    return &PANIC_FONT[0];   // blank/unsupported
}

static void panic_draw_char(const catcall_display_t *disp, int x, int y, char c, uint16_t color)
{
    char up = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    const panic_glyph_t *g = panic_glyph_for(up);
    for (int row = 0; row < 5; row++) {
        uint8_t bits = g->rows[row];
        for (int col = 0; col < 3; col++) {
            if (bits & (0x4u >> col)) {
                disp->fill_rect(x + col * PANIC_SCALE, y + row * PANIC_SCALE,
                                 PANIC_SCALE, PANIC_SCALE, color);
            }
        }
    }
}

// Left-aligned at (x,y), hard-wraps every max_chars_per_line characters
// (character-count wrap, not word-aware — fine for short diagnostic
// strings) and on '\n'. Returns the y just below the last line, so callers
// can stack further text underneath.
static int panic_draw_string(const catcall_display_t *disp, int x, int y,
                              const char *s, uint16_t color, int max_chars_per_line)
{
    if (max_chars_per_line < 1) max_chars_per_line = 1;
    int col = 0, cx = x, cy = y;
    for (const char *p = s; s && *p; p++) {
        if (*p == '\n' || col >= max_chars_per_line) {
            cx = x; cy += PANIC_CHAR_H; col = 0;
            if (*p == '\n') continue;
        }
        panic_draw_char(disp, cx, cy, *p, color);
        cx += PANIC_CHAR_W;
        col++;
    }
    return cy + PANIC_CHAR_H;
}

static void panic_dump_logs(const char *entity_name, const char *reason)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
        "PURR OS crash dump\n"
        "entity: %s\n"
        "reason: %s\n"
        "uptime_ms: %llu\n"
        "free_internal: %u\n"
        "free_psram: %u\n"
        "reset_reason: %d\n",
        entity_name ? entity_name : "(unknown)",
        reason ? reason : "(unknown)",
        (unsigned long long)purr_kernel_uptime_ms(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (int)esp_reset_reason());

    ESP_LOGE(TAG, "%s", buf);

    // SD if present (matches "both, and serial only if no SD card"), always
    // also to serial above regardless.
    if (purr_kernel_sd_available()) {
        char path[64];
        snprintf(path, sizeof(path), "/sdcard/crashlog_%llu.txt",
                 (unsigned long long)purr_kernel_uptime_ms());
        FILE *f = fopen(path, "w");
        if (f) {
            fwrite(buf, 1, strlen(buf), f);
            fclose(f);
            ESP_LOGI(TAG, "crash dump written to %s", path);
        } else {
            ESP_LOGW(TAG, "crash dump: failed to open %s for write", path);
        }
    }
}

void __attribute__((noreturn)) purr_kernel_panic_ex(const char *reason, bool recoverable, const char *entity_name)
{
    // Always log to serial first — display may not be up.
    ESP_LOGE(TAG, "=== KERNEL PANIC%s ===", recoverable ? " (recoverable)" : "");
    if (entity_name && entity_name[0]) ESP_LOGE(TAG, "entity: %s", entity_name);
    ESP_LOGE(TAG, "%s", reason ? reason : "(no reason given)");
    ESP_LOGE(TAG, "System halted.");

    const catcall_display_t *disp = s_display;
    uint16_t w = 320, h = 240;
    if (disp) {
        display_info_t info = {0};
        if (disp->get_info) disp->get_info(&info);
        w = info.width  ? info.width  : 320;
        h = info.height ? info.height : 240;

        uint16_t bg = recoverable ? 0x001Fu /* blue */ : 0xD800u /* red */;
        disp->fill_rect(0, 0, w, h, bg);
        disp->fill_rect(0, 0, w, 24, 0xFFFFu);   // white header bar

        int max_cols = (int)((w - 8) / PANIC_CHAR_W);
        int y = 4;
        y = panic_draw_string(disp, 4, y,
                               recoverable ? "SUBSYSTEM DISABLED" : "PURR OS - KERNEL PANIC",
                               0x0000u, max_cols);
        y += PANIC_SCALE;
        if (entity_name && entity_name[0]) {
            y = panic_draw_string(disp, 4, y, entity_name, 0xFFFFu, max_cols);
        }
        y = panic_draw_string(disp, 4, y, reason ? reason : "UNKNOWN REASON", 0xFFFFu, max_cols);
    }

    if (!recoverable) {
        // Fatal — same shape the historic KITT 0.6.0 panic screen used:
        // hold long enough to actually read, then reboot. purr_kernel_reboot()
        // (below) already exists and is reused, not reimplemented.
        for (int s = 10; s > 0; s--) {
            if (disp) {
                char line[24];
                snprintf(line, sizeof(line), "RESTARTING IN %d...", s);
                disp->fill_rect(4, h - 28, w - 8, 24, 0xD800u);
                panic_draw_string(disp, 4, h - 28, line, 0xFFFFu, (int)((w - 8) / PANIC_CHAR_W));
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        purr_kernel_reboot();
    }

    // Recoverable (blue): no auto-reboot, no dismiss — sits here until the
    // user forces a reset, per design. Raw touch polling only (no
    // LVGL/MiniWin dependency — must work even when the disabled entity IS
    // the active UI backend).
    int btn_h   = 48;
    int btn_y   = h - btn_h - 8;
    int dump_x  = 4;
    int dump_w  = (w - 12) / 2;
    int reset_x = dump_x + dump_w + 4;
    int reset_w = dump_w;

    if (disp) {
        disp->fill_rect(dump_x, btn_y, dump_w, btn_h, 0x0000u);
        panic_draw_string(disp, dump_x + 4, btn_y + 6, "TAP:DUMP LOGS", 0xFFFFu,
                           (int)((dump_w - 8) / PANIC_CHAR_W));
        disp->fill_rect(reset_x, btn_y, reset_w, btn_h, 0x0000u);
        panic_draw_string(disp, reset_x + 4, btn_y + 6, "HOLD:RESET", 0xFFFFu,
                           (int)((reset_w - 8) / PANIC_CHAR_W));
    }

    const catcall_touch_t *touch = s_touch;
    uint32_t hold_start_ms = 0;
    bool holding_reset = false;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (!touch || !touch->is_pressed || !touch->is_pressed()) {
            holding_reset = false;
            continue;
        }
        uint16_t tx = 0, ty = 0;
        if (touch->read_point) touch->read_point(&tx, &ty);

        bool in_reset = (int)tx >= reset_x && (int)tx < reset_x + reset_w &&
                         (int)ty >= btn_y  && (int)ty < btn_y + btn_h;
        bool in_dump  = (int)tx >= dump_x  && (int)tx < dump_x + dump_w &&
                         (int)ty >= btn_y  && (int)ty < btn_y + btn_h;

        if (in_reset) {
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            if (!holding_reset) { holding_reset = true; hold_start_ms = now_ms; }
            else if (now_ms - hold_start_ms >= 2000) {
                purr_kernel_reboot();
            }
        } else {
            holding_reset = false;
            if (in_dump) {
                panic_dump_logs(entity_name, reason);
                // Debounce: wait for release so one tap doesn't dump repeatedly.
                while (touch->is_pressed && touch->is_pressed()) vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
    }
}

void __attribute__((noreturn)) purr_kernel_panic(const char *reason)
{
    purr_kernel_panic_ex(reason, /*recoverable=*/false, NULL);
}

// ── Module header peek (no init, no registration) ─────────────────────────────

static bool peek_module_header(const char *path, purr_module_header_t *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(out, 1, sizeof(*out), f);
    fclose(f);
    return n >= sizeof(*out) && out->magic == PURR_MODULE_MAGIC;
}

// ── Static (constructor) module registration ──────────────────────────────────
//
// PURR_MODULE_REGISTER() emits a __attribute__((constructor)) function for each
// module. These run before app_main, filling s_static_reg[]. When boot.c calls
// purr_kernel_load_static_modules(), we sort by priority and init each one.

#define MAX_STATIC_REG 64
static const purr_module_header_t *s_static_reg[MAX_STATIC_REG];
static int s_static_reg_count = 0;

void purr_kernel_register_module_static(const purr_module_header_t *hdr)
{
    if (s_static_reg_count < MAX_STATIC_REG) {
        s_static_reg[s_static_reg_count++] = hdr;
    } else {
        // Can't call ESP_LOG this early — just silently drop (shouldn't happen)
    }
}

static int load_one_static(const purr_module_header_t *hdr)
{
    if (s_module_count >= MAX_MODULES) {
        ESP_LOGE(TAG, "module table full, cannot load %s", hdr->name);
        return -1;
    }
    if (hdr->magic != PURR_MODULE_MAGIC) {
        ESP_LOGE(TAG, "bad magic in static module '%s'", hdr->name);
        return -1;
    }
    if (hdr->abi_version != PURR_MODULE_ABI_VERSION) {
        ESP_LOGE(TAG, "ABI mismatch: '%s' (module=%d kernel=%d)",
                 hdr->name, hdr->abi_version, PURR_MODULE_ABI_VERSION);
        return -1;
    }

    // Apps are registered in the module table for the app_manager to discover,
    // but their init() is NOT called at boot — the app_manager launches them.
    bool call_init = (hdr->module_type != PURR_MOD_APP);

    // Crash-loop guard applies to P2/P3 only — P1 REQUIRED already panics
    // immediately on the very first failure (below, in the caller), so
    // there's no "loop" to catch there; a P1 module that keeps failing
    // stops the boot outright every time regardless of this guard.
    bool guarded = call_init && hdr->load_priority != PURR_PRIORITY_REQUIRED;
    if (guarded && purr_crash_guard_is_disabled(hdr->name)) {
        ESP_LOGW(TAG, "static module '%s' disabled after repeated failures — skipping", hdr->name);
        return -1;
    }

    if (call_init && hdr->init) {
        if (guarded) purr_crash_guard_mark_start(hdr->name);
        int rc = hdr->init();
        if (guarded) purr_crash_guard_mark_stop(hdr->name, rc == 0, "init() failed");
        if (rc != 0) {
            ESP_LOGE(TAG, "static module '%s' init() returned %d", hdr->name, rc);
            return -1;
        }
    }
    module_slot_t *slot = &s_modules[s_module_count++];
    memcpy(&slot->header, hdr, sizeof(*hdr));
    slot->loaded = call_init;
    const char *prio = hdr->load_priority == PURR_PRIORITY_REQUIRED  ? "P1" :
                       hdr->load_priority == PURR_PRIORITY_IMPORTANT ? "P2" : "P3";
    const char *badge = call_init ? "[static]" : "[app/deferred]";
    ESP_LOGI(TAG, "loaded %s: %s v%s %s", prio, hdr->name, hdr->version, badge);
    return 0;
}

static int cmp_reg_priority(const void *a, const void *b)
{
    const purr_module_header_t *ha = *(const purr_module_header_t **)a;
    const purr_module_header_t *hb = *(const purr_module_header_t **)b;
    // Primary: load_priority (1=REQUIRED first). Secondary: module_type (DRIVER < SYSTEM < UI < APP).
    int ka = (int)ha->load_priority * 10 + (int)ha->module_type;
    int kb = (int)hb->load_priority * 10 + (int)hb->module_type;
    return ka - kb;
}

int purr_kernel_load_static_modules(void)
{
    qsort(s_static_reg, s_static_reg_count,
          sizeof(s_static_reg[0]), cmp_reg_priority);

    int n = s_static_reg_count;
    for (int i = 0; i < n; i++) {
        const purr_module_header_t *hdr = s_static_reg[i];
        int rc = load_one_static(hdr);
        if (rc != 0 && hdr->load_priority == PURR_PRIORITY_REQUIRED) {
            char reason[96];
            snprintf(reason, sizeof(reason),
                     "Required module '%.32s' failed to init.", hdr->name);
            purr_kernel_panic(reason);
        }
    }
    ESP_LOGI(TAG, "static module load: %d/%d initialised", s_module_count, n);
    return s_module_count;
}

// ── File-based loader (SD card extras) ───────────────────────────────────────

int purr_kernel_load_module(const char *path)
{
    purr_module_header_t hdr;
    if (!peek_module_header(path, &hdr)) {
        ESP_LOGE(TAG, "cannot read/validate header: %s", path);
        return -1;
    }

    if (hdr.abi_version != PURR_MODULE_ABI_VERSION) {
        ESP_LOGE(TAG, "ABI mismatch in %s (module=%d kernel=%d)",
                 path, hdr.abi_version, PURR_MODULE_ABI_VERSION);
        return -1;
    }

    if (hdr.kernel_min[0] && version_cmp(KITT_VERSION, hdr.kernel_min) < 0) {
        ESP_LOGE(TAG, "module %s requires kernel >= %s (running %s)",
                 hdr.name, hdr.kernel_min, KITT_VERSION);
        return -1;
    }

    bool compat_mode = false;
    if (hdr.kernel_max[0] && version_cmp(KITT_VERSION, hdr.kernel_max) > 0) {
        ESP_LOGW(TAG, "module %s: kernel %s > kernel_max %s [COMPAT]",
                 hdr.name, KITT_VERSION, hdr.kernel_max);
        compat_mode = true;
    }

    // Same name may already be loaded — e.g. present in both /flash and
    // /sdcard, or an SD-card update of a module loaded earlier from flash.
    // Highest version wins; loading the same module twice would double-
    // register its catcall and run init() twice.
    module_slot_t *slot;
    int existing = find_module_slot_by_name(hdr.name);
    if (existing >= 0) {
        if (version_cmp(hdr.version, s_modules[existing].header.version) <= 0) {
            ESP_LOGI(TAG, "module '%s' v%s already loaded (have v%s) — skipping %s",
                     hdr.name, hdr.version, s_modules[existing].header.version, path);
            return 0;
        }
        ESP_LOGI(TAG, "module '%s': v%s supersedes loaded v%s — reloading from %s",
                 hdr.name, hdr.version, s_modules[existing].header.version, path);
        if (s_modules[existing].header.deinit) s_modules[existing].header.deinit();
        slot = &s_modules[existing];
    } else {
        if (s_module_count >= MAX_MODULES) {
            ESP_LOGE(TAG, "module table full, cannot load %s", path);
            return -1;
        }
        slot = &s_modules[s_module_count++];
    }

    if (hdr.init) {
        int rc = hdr.init();
        if (rc != 0) {
            ESP_LOGE(TAG, "module %s init() returned %d", hdr.name, rc);
            return -1;
        }
    }

    memcpy(&slot->header, &hdr, sizeof(hdr));
    slot->loaded = true;

    const char *badge = compat_mode ? " [COMPAT]" : " [OK]";
    const char *prio  = hdr.load_priority == PURR_PRIORITY_REQUIRED  ? "P1" :
                        hdr.load_priority == PURR_PRIORITY_IMPORTANT ? "P2" : "P3";
    ESP_LOGI(TAG, "loaded %s: %s v%s%s", prio, hdr.name, hdr.version, badge);
    return 0;
}

// ── Priority-sorted scan entry ────────────────────────────────────────────────

#define MAX_SCAN_ENTRIES 64

typedef struct {
    char                 path[512];
    uint8_t              priority;      // from peeked header
    uint8_t              module_type;   // PURR_MOD_* from peeked header
    char                 name[32];
} scan_entry_t;

static int scan_entry_cmp(const void *a, const void *b)
{
    const scan_entry_t *sa = (const scan_entry_t *)a;
    const scan_entry_t *sb = (const scan_entry_t *)b;
    int ka = (int)sa->priority * 10 + (int)sa->module_type;
    int kb = (int)sb->priority * 10 + (int)sb->module_type;
    return ka - kb;
}

// Collect all .purr files in dir (non-recursive for this level).
static int collect_dir(const char *dir, scan_entry_t *entries, int max_entries, int count)
{
    DIR *d = opendir(dir);
    if (!d) return count;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < max_entries) {
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext || strcmp(ext, ".purr") != 0) continue;
        scan_entry_t *e = &entries[count];
        snprintf(e->path, sizeof(e->path), "%s/%s", dir, ent->d_name);
        purr_module_header_t hdr = {0};
        if (peek_module_header(e->path, &hdr)) {
            e->priority    = hdr.load_priority ? hdr.load_priority : PURR_PRIORITY_OPTIONAL;
            e->module_type = hdr.module_type   ? hdr.module_type   : PURR_MOD_SYSTEM;
            strncpy(e->name, hdr.name, sizeof(e->name) - 1);
        } else {
            e->priority    = PURR_PRIORITY_OPTIONAL;
            e->module_type = PURR_MOD_SYSTEM;
            strncpy(e->name, ent->d_name, sizeof(e->name) - 1);
        }
        count++;
    }
    closedir(d);
    return count;
}

// Also recurse one level into subdirectories (for drivers/<type>/)
static int collect_dir_recursive(const char *dir, scan_entry_t *entries, int max, int count)
{
    count = collect_dir(dir, entries, max, count);
    DIR *d = opendir(dir);
    if (!d) return count;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < max) {
        if (ent->d_type != DT_DIR || ent->d_name[0] == '.') continue;
        char sub[512];
        snprintf(sub, sizeof(sub), "%s/%s", dir, ent->d_name);
        count = collect_dir(sub, entries, max, count);
    }
    closedir(d);
    return count;
}

// ── Public scan + load ────────────────────────────────────────────────────────

int purr_kernel_scan_modules(const char *flash_dir, const char *sd_fallback_dir)
{
    static scan_entry_t entries[MAX_SCAN_ENTRIES];
    int count = 0;

    count = collect_dir_recursive(flash_dir, entries, MAX_SCAN_ENTRIES, 0);

    if (count == 0) {
        ESP_LOGW(TAG, "scan: no .purr files found in %s", flash_dir);
    }

    // Sort by priority — REQUIRED (1) loaded before IMPORTANT (2) before OPTIONAL (3)
    qsort(entries, count, sizeof(scan_entry_t), scan_entry_cmp);

    int loaded = 0;
    for (int i = 0; i < count; i++) {
        scan_entry_t *e = &entries[i];
        const char *prio_str = e->priority == PURR_PRIORITY_REQUIRED  ? "P1:REQUIRED " :
                               e->priority == PURR_PRIORITY_IMPORTANT ? "P2:IMPORTANT" : "P3:OPTIONAL ";
        ESP_LOGI(TAG, "  %s  %s", prio_str, e->name[0] ? e->name : e->path);

        int rc = purr_kernel_load_module(e->path);
        if (rc == 0) {
            loaded++;
            continue;
        }

        // Load from flash failed
        if (e->priority == PURR_PRIORITY_REQUIRED) {
            // Try SD fallback
            bool recovered = false;
            if (sd_fallback_dir) {
                char fallback[512];
                // Try flat: /sdcard/modules/<name>.purr
                snprintf(fallback, sizeof(fallback), "%s/%s.purr", sd_fallback_dir, e->name);
                if (purr_kernel_load_module(fallback) == 0) {
                    ESP_LOGW(TAG, "P1 module '%s' recovered from SD card", e->name);
                    recovered = true;
                    loaded++;
                }
                // Try the same relative subpath as on flash
                if (!recovered) {
                    // Extract filename from path and try sd_fallback_dir/<filename>
                    const char *fname = strrchr(e->path, '/');
                    if (fname) fname++;
                    if (fname) {
                        snprintf(fallback, sizeof(fallback), "%s/%s", sd_fallback_dir, fname);
                        if (purr_kernel_load_module(fallback) == 0) {
                            ESP_LOGW(TAG, "P1 module '%s' recovered from SD card (by filename)", e->name);
                            recovered = true;
                            loaded++;
                        }
                    }
                }
            }

            if (!recovered) {
                char reason[96];
                snprintf(reason, sizeof(reason),
                         "Required module '%.32s' missing.",
                         e->name[0] ? e->name : "unknown");
                purr_kernel_panic(reason);
                // never returns
            }

        } else if (e->priority == PURR_PRIORITY_IMPORTANT) {
            ESP_LOGW(TAG, "P2 module '%s' failed to load — continuing without it",
                     e->name[0] ? e->name : e->path);
        }
        // OPTIONAL: silent
    }

    ESP_LOGI(TAG, "scan %s: %d/%d modules loaded", flash_dir, loaded, count);
    return loaded;
}

void purr_kernel_unload_module(const char *name)
{
    for (int i = 0; i < s_module_count; i++) {
        if (strcmp(s_modules[i].header.name, name) == 0) {
            if (s_modules[i].header.deinit) s_modules[i].header.deinit();
            s_modules[i] = s_modules[--s_module_count];
            ESP_LOGI(TAG, "unloaded: %s", name);
            return;
        }
    }
}

const purr_module_header_t *purr_kernel_get_module(const char *name)
{
    for (int i = 0; i < s_module_count; i++)
        if (strcmp(s_modules[i].header.name, name) == 0)
            return &s_modules[i].header;
    return NULL;
}

int purr_kernel_module_count(void)
{
    return s_module_count;
}

const purr_module_header_t *purr_kernel_module_at(int idx)
{
    if (idx < 0 || idx >= s_module_count) return NULL;
    return &s_modules[idx].header;
}

// ── System info ───────────────────────────────────────────────────────────────

uint32_t purr_kernel_free_ram(void) {
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
}

uint64_t purr_kernel_uptime_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

static bool s_sd_available    = false;
static bool s_wifi_connected  = false;
static int  s_battery_percent = -1;   // -1 = unknown (no PMIC/fuel gauge found)
static bool s_lora_available  = false;
static bool s_dev_mode        = false;   // off by default — see purr_kernel.h's doc comment

void purr_kernel_set_sd_available(bool v)    { s_sd_available    = v; }
void purr_kernel_set_wifi_connected(bool v)  { s_wifi_connected  = v; }
void purr_kernel_set_battery_percent(int v)  { s_battery_percent = v; }
void purr_kernel_set_lora_available(bool v)  { s_lora_available  = v; }
void purr_kernel_set_dev_mode(bool v)        { s_dev_mode        = v; }

bool purr_kernel_sd_available(void)    { return s_sd_available; }
bool purr_kernel_wifi_connected(void)  { return s_wifi_connected; }
int  purr_kernel_battery_percent(void) { return s_battery_percent; }
bool purr_kernel_lora_available(void)  { return s_lora_available; }
bool purr_kernel_dev_mode_enabled(void) { return s_dev_mode; }

void purr_kernel_reboot(void) {
    ESP_LOGW(TAG, "kernel reboot requested");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

// ── Notifications ─────────────────────────────────────────────────────────────
// Ring buffer indexed by insertion order; s_notify_head points at the slot
// the *next* notification will be written to, so the most recent entry is
// always at (s_notify_head - 1).

static purr_notification_t s_notify_buf[PURR_NOTIFY_MAX];
static int                  s_notify_head  = 0;
static int                  s_notify_count = 0;

void purr_kernel_notify(const char *title, const char *body, const char *source)
{
    purr_notification_t *n = &s_notify_buf[s_notify_head];
    memset(n, 0, sizeof(*n));
    if (title)  strncpy(n->title,  title,  sizeof(n->title)  - 1);
    if (body)   strncpy(n->body,   body,   sizeof(n->body)   - 1);
    if (source) strncpy(n->source, source, sizeof(n->source) - 1);
    n->timestamp_ms = purr_kernel_uptime_ms();

    s_notify_head = (s_notify_head + 1) % PURR_NOTIFY_MAX;
    if (s_notify_count < PURR_NOTIFY_MAX) s_notify_count++;

    ESP_LOGI(TAG, "notify [%s] %s: %s", source ? source : "?", title ? title : "", body ? body : "");
}

int purr_kernel_notify_count(void) { return s_notify_count; }

bool purr_kernel_notify_at(int idx, purr_notification_t *out)
{
    if (idx < 0 || idx >= s_notify_count || !out) return false;
    int slot = (s_notify_head - 1 - idx + PURR_NOTIFY_MAX) % PURR_NOTIFY_MAX;
    *out = s_notify_buf[slot];
    return true;
}

void purr_kernel_notify_clear(void)
{
    s_notify_head  = 0;
    s_notify_count = 0;
}

// ── Service health registry ───────────────────────────────────────────────────
// See purr_kernel_health_register()'s comment in purr_kernel.h — a single
// shared watchdog task polls every registered check so individual modules
// (meshtastic, wifi_mgr, bt_mgr, ...) don't each need their own.

typedef struct {
    const char           *name;
    purr_health_check_fn  is_alive;
    bool                  was_alive;
} health_entry_t;

static health_entry_t   s_health[PURR_HEALTH_MAX];
static int               s_health_count = 0;
static TaskHandle_t      s_health_watchdog_task = NULL;

// Last uptime_ms() a UI backend's own pump loop called purr_kernel_ui_heartbeat()
// — 0 until the first call ever happens. See the staleness check in
// health_watchdog_task() below and purr_crash_guard.h for the full design.
static uint64_t s_ui_last_heartbeat_ms = 0;

#define HEALTH_WATCHDOG_POLL_MS 2000UL
#define UI_HANG_THRESHOLD_MS    6000UL   // ~3 missed polls

static void health_watchdog_task(void *arg);

static void ensure_health_watchdog_started(void)
{
    // PSRAM-backed: this task only calls is_alive()/heartbeat-staleness
    // checks + notify()/panic, never touches NVS/flash directly, so it's
    // exempt from the cache-disable-crash constraint documented at
    // app_manager.c's static-stack exception. Lazily started (by whichever
    // of purr_kernel_health_register()/purr_kernel_ui_heartbeat() runs
    // first) rather than unconditionally at kernel boot — some
    // builds/configs never need either.
    if (!s_health_watchdog_task) {
        xTaskCreateWithCaps(health_watchdog_task, "health_wd", 3072, NULL, 2,
                             &s_health_watchdog_task, MALLOC_CAP_SPIRAM);
    }
}

void purr_kernel_ui_heartbeat(void)
{
    s_ui_last_heartbeat_ms = purr_kernel_uptime_ms();
    ensure_health_watchdog_started();
}

static void health_watchdog_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(HEALTH_WATCHDOG_POLL_MS));

        // UI-hang detection — feeds purr_crash_guard's hang path. The
        // render/pump loops (cupcake_task, MiniWin's pump task) have no
        // natural per-iteration timeout wrapper the way app tasks get
        // from app_manager_stop()'s semaphore-wait — they're designed to
        // loop forever, so a genuine deadlock (confirmed live: MiniWin's
        // own close-icon handling could freeze the whole UI this way
        // before Part A's fix) has nothing else to catch it. Only checked
        // once a UI backend is registered AND has heartbeated at least
        // once — a headless/serial-only build, or the brief window before
        // the UI task's very first loop iteration, must not trip this.
        const catcall_ui_t *ui = purr_kernel_ui();
        if (ui && s_ui_last_heartbeat_ms != 0) {
            uint64_t now = purr_kernel_uptime_ms();
            if (now - s_ui_last_heartbeat_ms > UI_HANG_THRESHOLD_MS) {
                purr_crash_guard_mark_hang(ui->name, "UI TASK UNRESPONSIVE");
                // Loops forever inside purr_kernel_panic_ex() (blue,
                // recoverable) — nothing after this point runs again
                // this boot.
            }
        }

        // TEMPORARY diagnostic — tracking a reported fast internal-DRAM
        // drain ("23 bytes free... high memory pressure out of the gate").
        // Logs every 2s regardless of app activity so a continuous background
        // leak (vs. a one-time boot-time consumption) shows up as a steady
        // downward slope even with nothing else touched. Remove once found.
        //
        // dma_free/largest_dma added while chasing a follow-on symptom:
        // live boot captures show "dma_utils: esp_dma_capable_malloc(172):
        // Not enough heap memory" -> "diskio_sdmmc: sdmmc_read_blocks
        // failed" during phase-2 SD scanning, right after bt_mgr/wifi_mgr/
        // meshtastic have all loaded — and the same SD-read path
        // (fopen()/fread()) is what launch_meow() and lua_sd_read()/
        // lua_sd_write() use, so this is the likely cause of "loaded
        // scripts are broken". esp_dma_capable_malloc() requests
        // MALLOC_CAP_DMA specifically, which is internal-RAM-only on this
        // chip (esp_dma_utils.c explicitly can't combine MALLOC_CAP_DMA
        // with MALLOC_CAP_SPIRAM) — so unlike the BLE/LVGL fixes, this
        // can't just be routed to PSRAM. CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL
        // already carves out a dedicated 32KB DMA-capable pool at boot
        // (esp_psram_extram_reserve_dma_pool(), confirmed live: "esp_psram:
        // Reserving pool of 32K of internal memory for DMA/internal
        // allocations"). MALLOC_CAP_INTERNAL alone (already logged above)
        // doesn't tell us whether that specific 32KB reserve is what's
        // collapsing, or whether it's healthy and something else is going
        // on — these two extra fields answer that question directly.
        ESP_LOGW(TAG, "heapwatch: internal_free=%u largest_internal=%u dma_free=%u largest_dma=%u psram_free=%u uptime_ms=%llu",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned long long)purr_kernel_uptime_ms());

        for (int i = 0; i < s_health_count; i++) {
            health_entry_t *h = &s_health[i];
            bool alive = h->is_alive();
            if (h->was_alive && !alive) {
                char body[PURR_NOTIFY_BODY_LEN];
                snprintf(body, sizeof(body), "%s appears unresponsive", h->name);
                purr_kernel_notify("Service down", body, h->name);
            } else if (!h->was_alive && alive) {
                char body[PURR_NOTIFY_BODY_LEN];
                snprintf(body, sizeof(body), "%s recovered", h->name);
                purr_kernel_notify("Service recovered", body, h->name);
            }
            h->was_alive = alive;
        }
    }
}

void purr_kernel_health_register(const char *name, purr_health_check_fn is_alive)
{
    if (!name || !is_alive || s_health_count >= PURR_HEALTH_MAX) return;

    health_entry_t *h = &s_health[s_health_count++];
    h->name      = name;
    h->is_alive  = is_alive;
    h->was_alive = is_alive();

    ensure_health_watchdog_started();
}

int purr_kernel_health_count(void) { return s_health_count; }

bool purr_kernel_health_at(int idx, const char **name, bool *alive)
{
    if (idx < 0 || idx >= s_health_count) return false;
    if (name)  *name  = s_health[idx].name;
    if (alive) *alive = s_health[idx].is_alive();
    return true;
}

// ── Boot readiness ───────────────────────────────────────────────────────────

static volatile bool s_boot_ready = false;

bool purr_kernel_boot_ready(void)        { return s_boot_ready; }
void purr_kernel_set_boot_ready(bool v)  { s_boot_ready = v; }
