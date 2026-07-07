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


void __attribute__((noreturn)) purr_kernel_panic(const char *reason)
{
    // Always log to serial first — display may not be up
    ESP_LOGE(TAG, "=== KERNEL PANIC ===");
    ESP_LOGE(TAG, "%s", reason);
    ESP_LOGE(TAG, "System halted.");

    // Best-effort panic screen if display catcall is registered
    const catcall_display_t *disp = s_display;
    if (disp) {
        display_info_t info = {0};
        if (disp->get_info) disp->get_info(&info);
        uint16_t w = info.width  ? info.width  : 320;
        uint16_t h = info.height ? info.height : 240;

        // Fill red background
        if (disp->fill_rect) {
            disp->fill_rect(0, 0, w, h, 0xD800u); // red

            // White header bar
            disp->fill_rect(0, 0, w, 24, 0xFFFFu);

            // Draw "PURR OS — KERNEL PANIC" text is best-effort; without a
            // fully populated font table we rely on the serial log above.
            // A production build would have the full font here.
            //
            // For now: draw a distinctive striped pattern so the user knows
            // the device has halted and it isn't just a blank screen.
            for (int y = 32; y < h - 8; y += 16) {
                disp->fill_rect(0, y, w, 8, 0xFFFFu); // white stripe
            }

            // Bottom bar with reason truncated to ~40 chars
            disp->fill_rect(0, h - 20, w, 20, 0x0000u);
        }
    }

    // Halt
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
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

    if (call_init && hdr->init) {
        int rc = hdr->init();
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

#define HEALTH_WATCHDOG_POLL_MS 2000UL

static void health_watchdog_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(HEALTH_WATCHDOG_POLL_MS));

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

    // Lazily started on first registration rather than unconditionally at
    // kernel boot — most builds/tests never register anything, and this
    // avoids spending a task+stack on devices/configs with nothing to watch.
    // PSRAM-backed: this task only calls is_alive() callbacks + notify(),
    // never touches NVS/flash, so it's exempt from the cache-disable-crash
    // constraint documented at app_manager.c's static-stack exception.
    if (!s_health_watchdog_task) {
        xTaskCreateWithCaps(health_watchdog_task, "health_wd", 3072, NULL, 2,
                             &s_health_watchdog_task, MALLOC_CAP_SPIRAM);
    }
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
