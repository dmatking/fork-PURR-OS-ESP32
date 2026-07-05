// app_manager.c — PURR OS app manager

#include "app_manager.h"
#include "../../kernel/core/purr_kernel.h"
#include "../../kernel/core/purr_module.h"
#include "../../kernel/catcalls/purr_win.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "app_mgr";

#define MAX_APPS 64

static const char *s_scan_paths[] = {
    "/flash/apps",
    "/sdcard/apps",
    NULL
};

static app_entry_t s_apps[MAX_APPS];
static int         s_app_count = 0;

// app_manager_on_window_created() (see below) needs to know which app is
// currently inside its synchronous init() call, to attribute a
// purr_win_create() call to the right app_entry_t without every app needing
// to report its own window handle. This USED to be a single shared global
// (s_launching_app), set right before calling init() and cleared right
// after — but each app launches on its own FreeRTOS task, and with the
// ESP32-S3 being dual-core, two launches close together in time (e.g. two
// quick taps) can genuinely run their init() calls concurrently on the two
// cores. The second task's assignment stomped the first one mid-flight,
// silently attributing the first app's window to the second app's entry —
// the first app's ->window stayed 0 forever, and re-tapping its icon later
// hit the "already running, just re-show the tracked window" path on a
// handle that was never actually set. Nothing visible ever happened.
//
// Fixed by keying off the calling task's own handle instead of a shared
// global — see find_app_by_current_task() below. xTaskGetCurrentTaskHandle()
// is inherently race-free (it only ever returns the caller's own identity),
// and s_ctxs[idx].task is written by xTaskCreate() in the ORIGINAL launching
// context, before the new task can possibly start running — so by the time
// that task calls purr_win_create(), its own ctx->task entry is guaranteed
// to already be correct, regardless of how many other launches overlap.

// ── Tier detection ─────────────────────────────────────────────────────────────

static app_tier_t tier_from_ext(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (!ext) return APP_TIER_PAWS;
    if (strcmp(ext, ".meow") == 0) return APP_TIER_MEOW;
    if (strcmp(ext, ".claw") == 0) return APP_TIER_CLAW;
    return APP_TIER_PAWS;   // .paws and anything else
}

static const char *tier_name(app_tier_t t)
{
    switch (t) {
    case APP_TIER_MEOW: return "meow";
    case APP_TIER_PAWS: return "paws";
    case APP_TIER_CLAW: return "claw";
    default:            return "?";
    }
}

// Strip extension and path prefix to get a display name
static void make_display_name(const char *filename, char *out, size_t out_sz)
{
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    strncpy(out, base, out_sz - 1);
    out[out_sz - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

// ── Scan ──────────────────────────────────────────────────────────────────────

// Returns the s_apps[] index already holding this display name, or -1.
static int find_app_slot_by_name(const char *name) {
    for (int i = 0; i < s_app_count; i++) {
        if (strncmp(s_apps[i].name, name, sizeof(s_apps[i].name)) == 0) {
            return i;
        }
    }
    return -1;
}

static void scan_dir(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext) continue;
        if (strcmp(ext, ".meow") != 0 &&
            strcmp(ext, ".paws") != 0 &&
            strcmp(ext, ".claw") != 0) continue;

        char name[sizeof(((app_entry_t *)0)->name)];
        make_display_name(ent->d_name, name, sizeof(name));

        // Later scan paths shadow earlier ones — including a pre-linked app
        // of the same name — per docs/06_Apps.md ("SD apps with the same
        // name as a flash app shadow the flash version"). Previously every
        // path was appended unconditionally, so a same-named app on both
        // /flash/apps and /sdcard/apps produced two confusing entries in the
        // launcher instead of the SD copy taking over.
        int existing = find_app_slot_by_name(name);
        app_entry_t *app;
        if (existing >= 0) {
            app = &s_apps[existing];
        } else {
            if (s_app_count >= MAX_APPS) {
                ESP_LOGW(TAG, "app table full — skipping %s/%s", dir, ent->d_name);
                continue;
            }
            app = &s_apps[s_app_count++];
        }
        memset(app, 0, sizeof(*app));

        snprintf(app->path, sizeof(app->path), "%s/%.200s", dir, ent->d_name);
        strncpy(app->name, name, sizeof(app->name) - 1);
        app->tier  = tier_from_ext(ent->d_name);
        app->state = APP_STATE_IDLE;

        if (existing >= 0) {
            ESP_LOGI(TAG, "found [%s] %s (shadows earlier entry)", tier_name(app->tier), app->name);
        } else {
            ESP_LOGI(TAG, "found [%s] %s", tier_name(app->tier), app->name);
        }
    }
    closedir(d);
}

// ── Per-app task context ──────────────────────────────────────────────────────

typedef struct {
    app_entry_t            *app;
    const purr_module_header_t *mod;      // non-null for pre-linked native apps
    TaskHandle_t            task;
    SemaphoreHandle_t       done;
} app_task_ctx_t;

static app_task_ctx_t s_ctxs[MAX_APPS];

// ── Lua VM dispatch ───────────────────────────────────────────────────────────
//
// .meow apps run via the lua_runtime module. That module must be loaded first
// and register itself under the name "lua_runtime". We find it in the kernel
// module registry and call its init() which bootstraps the VM for one script.
// The path of the script is passed via a thin task-arg struct written into NVS
// (or a global, since only one Lua VM runs at a time on these boards).

static char s_meow_pending_path[128];

static void meow_task(void *arg) {
    app_task_ctx_t *ctx = (app_task_ctx_t *)arg;
    const purr_module_header_t *lua_rt = purr_kernel_get_module("lua_runtime");
    if (!lua_rt || !lua_rt->init) {
        ESP_LOGE(TAG, "lua_runtime module not loaded — cannot run .meow");
        ctx->app->state = APP_STATE_ERROR;
        snprintf(ctx->app->error, sizeof(ctx->app->error), "lua_runtime not loaded");
        xSemaphoreGive(ctx->done);
        vTaskDelete(NULL);
        return;
    }
    // lua_runtime.init() picks up the pending path via s_meow_pending_path
    int rc = lua_rt->init();
    // Only mark ERROR on failure — a successful init() means the script is up
    // and running, not finished. Previously this immediately overwrote the
    // RUNNING state set at launch with STOPPED the instant init() returned,
    // which for every app here is almost instantly — so the Running Apps
    // list and the re-launch guard below never actually worked. state now
    // only becomes STOPPED via the explicit app_manager_stop().
    if (rc != 0) {
        ctx->app->state = APP_STATE_ERROR;
        snprintf(ctx->app->error, sizeof(ctx->app->error), "lua_runtime init failed (%d)", rc);
    }
    xSemaphoreGive(ctx->done);
    vTaskDelete(NULL);
}

static int launch_meow(app_entry_t *app, int idx)
{
    const purr_module_header_t *lua_rt = purr_kernel_get_module("lua_runtime");
    if (!lua_rt) {
        app->state = APP_STATE_ERROR;
        snprintf(app->error, sizeof(app->error), "lua_runtime module not loaded");
        ESP_LOGW(TAG, "cannot run .meow '%s': lua_runtime not in module registry", app->name);
        return -1;
    }

    strncpy(s_meow_pending_path, app->path, sizeof(s_meow_pending_path) - 1);

    app_task_ctx_t *ctx = &s_ctxs[idx];
    ctx->app  = app;
    ctx->mod  = lua_rt;
    ctx->done = xSemaphoreCreateBinary();

    ESP_LOGI(TAG, "launch .meow: %s", app->path);

    BaseType_t ok = xTaskCreate(meow_task, app->name, 8192, ctx, 5, &ctx->task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed for '%s' — likely out of internal heap", app->name);
        app->state = APP_STATE_ERROR;
        snprintf(app->error, sizeof(app->error), "xTaskCreate failed (out of memory?)");
        vSemaphoreDelete(ctx->done);
        ctx->done = NULL;
        return -1;
    }
    app->state = APP_STATE_RUNNING;
    return 0;
}

// ── Native app dispatch (.paws / .claw) ───────────────────────────────────────
//
// .paws and .claw apps are compiled C that must be pre-linked into the firmware
// as .purr system modules. The app_manager looks them up in the kernel module
// registry by name (filename without extension). If the module is registered and
// running, we call init() directly on a new FreeRTOS task.
//
// True hot-loading from SD blobs requires position-independent compilation and
// an ELF relocator — tracked as future work.

static void native_task(void *arg) {
    app_task_ctx_t *ctx = (app_task_ctx_t *)arg;
    ESP_LOGI(TAG, "native app task start: %s (core=%d prio=%u)",
             ctx->app->name, xPortGetCoreID(), (unsigned)uxTaskPriorityGet(NULL));

    int rc = ctx->mod->init();
    ESP_LOGI(TAG, "native app task init() returned: %s rc=%d window=%u",
             ctx->app->name, rc, (unsigned)ctx->app->window);

    // Only mark ERROR on failure — see meow_task()'s comment on why this no
    // longer overwrites RUNNING with STOPPED just because the (non-blocking)
    // init() call returned.
    if (rc != 0) {
        ctx->app->state = APP_STATE_ERROR;
        snprintf(ctx->app->error, sizeof(ctx->app->error), "init() returned %d", rc);
    }
    xSemaphoreGive(ctx->done);
    vTaskDelete(NULL);
}

static int launch_native(app_entry_t *app, int idx)
{
    const purr_module_header_t *mod = purr_kernel_get_module(app->name);
    if (!mod) {
        // App not pre-linked — report clearly so the user knows what to do
        app->state = APP_STATE_ERROR;
        snprintf(app->error, sizeof(app->error), "not pre-linked: %.48s", app->name);
        ESP_LOGW(TAG, "app '%s' not found in kernel module registry — it must be pre-linked into firmware", app->name);
        return -1;
    }
    if (!mod->init) {
        app->state = APP_STATE_ERROR;
        snprintf(app->error, sizeof(app->error), "module has no init()");
        return -1;
    }

    app_task_ctx_t *ctx = &s_ctxs[idx];
    ctx->app  = app;
    ctx->mod  = mod;
    ctx->done = xSemaphoreCreateBinary();

    ESP_LOGI(TAG, "launch .%s: %s (pre-linked module) — free heap: internal=%u largest_internal_block=%u",
             tier_name(app->tier), app->name,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    // Stack size: .claw apps get more stack (kernel access)
    uint32_t stack = (app->tier == APP_TIER_CLAW) ? 16384 : 8192;
    BaseType_t ok = xTaskCreate(native_task, app->name, stack, ctx, 5, &ctx->task);
    if (ok != pdPASS) {
        // xTaskCreate's return value was never checked before — a silent
        // failure here (task stacks come out of internal DRAM regardless of
        // PSRAM being present, and that pool is small and shared with
        // WiFi/BT/LoRa/LVGL buffers) left `state` stuck at RUNNING forever
        // with no task ever created, so every later tap on ANY app silently
        // no-op'd via the `state == APP_STATE_RUNNING` guard below — exactly
        // matching "apps just don't open" with zero error or crash.
        ESP_LOGE(TAG, "xTaskCreate failed for '%s' (stack=%u) — likely out of internal heap", app->name, (unsigned)stack);
        app->state = APP_STATE_ERROR;
        snprintf(app->error, sizeof(app->error), "xTaskCreate failed (out of memory?)");
        vSemaphoreDelete(ctx->done);
        ctx->done = NULL;
        return -1;
    }
    app->state = APP_STATE_RUNNING;
    return 0;
}

// ── Public API ────────────────────────────────────────────────────────────────

int app_manager_scan(void)
{
    s_app_count = 0;

    // Discover pre-linked apps registered in the kernel module table
    int n = purr_kernel_module_count();
    for (int i = 0; i < n && s_app_count < MAX_APPS; i++) {
        const purr_module_header_t *hdr = purr_kernel_module_at(i);
        if (!hdr || hdr->module_type != PURR_MOD_APP) continue;

        app_entry_t *app = &s_apps[s_app_count];
        memset(app, 0, sizeof(*app));
        strncpy(app->name, hdr->name, sizeof(app->name) - 1);
        snprintf(app->path, sizeof(app->path), "prelinked:/%s", hdr->name);
        app->tier  = APP_TIER_CLAW;
        app->state = APP_STATE_IDLE;

        ESP_LOGI(TAG, "found [claw/pre-linked] %s", app->name);
        s_app_count++;
    }

    // Also scan filesystem paths for .meow / .paws / .claw files (SD extras)
    for (int i = 0; s_scan_paths[i]; i++) {
        scan_dir(s_scan_paths[i]);
    }

    ESP_LOGI(TAG, "scan complete: %d apps found", s_app_count);
    return s_app_count;
}

int app_manager_launch_idx(int idx)
{
    if (idx < 0 || idx >= s_app_count) return -1;
    return app_manager_launch_path(s_apps[idx].path);
}

int app_manager_launch_path(const char *path)
{
    for (int i = 0; i < s_app_count; i++) {
        if (strcmp(s_apps[i].path, path) == 0) {
            app_entry_t *app = &s_apps[i];
            if (app->state == APP_STATE_RUNNING) return 0;
            if (app->tier == APP_TIER_MEOW) return launch_meow(app, i);
            return launch_native(app, i);
        }
    }
    return -1;
}

void app_manager_stop(int idx)
{
    if (idx < 0 || idx >= s_app_count) return;
    app_entry_t *app = &s_apps[idx];
    if (app->state != APP_STATE_RUNNING) return;

    app_task_ctx_t *ctx = &s_ctxs[idx];

    // Signal the module to clean up (deinit) then wait for task to finish
    if (ctx->mod && ctx->mod->deinit) {
        ctx->mod->deinit();
    }

    // A successful semaphore take proves native_task()/meow_task() already
    // ran to completion and self-deleted (vTaskDelete(NULL)) — ctx->task is
    // then a stale handle whose TCB may already be reclaimed/reused by an
    // unrelated task, so it must not be touched. Only a genuine timeout means
    // the task is still alive and actually needs force-deleting.
    if (ctx->done) {
        if (xSemaphoreTake(ctx->done, pdMS_TO_TICKS(2000)) == pdTRUE) {
            ctx->task = NULL;
        } else if (ctx->task) {
            ESP_LOGW(TAG, "force-deleting task for '%s'", app->name);
            vTaskDelete(ctx->task);
            ctx->task = NULL;
        }
        vSemaphoreDelete(ctx->done);
        ctx->done = NULL;
    }

    app->state = APP_STATE_STOPPED;
    ESP_LOGI(TAG, "stopped: %s", app->name);
}

const char *app_manager_get_pending_meow_path(void) { return s_meow_pending_path; }

int app_manager_count(void)             { return s_app_count; }
const app_entry_t *app_manager_get(int idx)
{
    if (idx < 0 || idx >= s_app_count) return NULL;
    return &s_apps[idx];
}

// ── Launcher button callback ──────────────────────────────────────────────────

static void launcher_btn_cb(purr_wid_t wid, purr_event_t event, void *user)
{
    if (event != PURR_EVENT_CLICKED) return;
    int idx = (int)(intptr_t)user;
    if (idx >= 0 && idx < s_app_count) {
        ESP_LOGI(TAG, "launcher: launching %s", s_apps[idx].name);
        app_manager_launch_idx(idx);
    }
}

void app_manager_open_launcher(void)
{
    ESP_LOGI(TAG, "cat apps launcher: %d app(s)", s_app_count);

    if (!purr_kernel_ui()) {
        ESP_LOGW(TAG, "no UI registered — launcher running in serial-only mode");
        for (int i = 0; i < s_app_count; i++) {
            ESP_LOGI(TAG, "  [%d] %s (%s)", i, s_apps[i].name, tier_name(s_apps[i].tier));
        }
        return;
    }

    purr_win_t win = purr_win_create("Cat Apps");
    if (!win) {
        ESP_LOGE(TAG, "launcher: failed to create window");
        return;
    }

    if (s_app_count == 0) {
        purr_win_label(win, "No apps installed.\nCopy .meow or .paws files to /sdcard/apps");
    } else {
        for (int i = 0; i < s_app_count; i++) {
            purr_win_button(win, s_apps[i].name, launcher_btn_cb, (void *)(intptr_t)i);
        }
    }

    purr_win_show(win);
    ESP_LOGI(TAG, "launcher window open");
}

// ── Window tracking ───────────────────────────────────────────────────────────
// Wired up via purr_kernel_set_window_created_cb() below — see the comment
// above s_ctxs' old s_launching_app field for why this doesn't need every
// app to report its own window, and why it's keyed by task handle now.

static void app_manager_on_win_close(purr_wid_t win, purr_event_t event, void *user) {
    (void)win; (void)event;
    app_entry_t *app = (app_entry_t *)user;
    int idx = (int)(app - s_apps);
    app_manager_stop(idx);
}

// Finds the app_entry_t whose launch task is the one calling right now —
// race-free (xTaskGetCurrentTaskHandle() only ever returns the caller's own
// identity), unlike the single shared s_launching_app global this replaced.
// Returns NULL for calls from any other task (a widget callback invoked from
// the UI's own render task, a follow-up dialog/window an app opens later,
// etc.) — correctly leaving app->window as the app's *original* window from
// its init() call, not overwritten by a later one.
static app_entry_t *find_app_by_current_task(void) {
    TaskHandle_t me = xTaskGetCurrentTaskHandle();
    for (int i = 0; i < s_app_count; i++) {
        if (s_ctxs[i].task == me) return s_ctxs[i].app;
    }
    return NULL;
}

static void app_manager_on_window_created(purr_win_t win) {
    app_entry_t *app = find_app_by_current_task();
    if (!app) return;
    app->window = win;
    purr_win_on_close(win, app_manager_on_win_close, (void *)app);
}

int app_manager_init(void)
{
    memset(s_ctxs, 0, sizeof(s_ctxs));
    purr_kernel_set_window_created_cb(app_manager_on_window_created);
    app_manager_scan();
    ESP_LOGI(TAG, "init complete");
    return 0;
}

void app_manager_deinit(void)
{
    for (int i = 0; i < s_app_count; i++) {
        if (s_apps[i].state == APP_STATE_RUNNING) {
            app_manager_stop(i);
        }
    }
    s_app_count = 0;
}

// ── .purr module header ───────────────────────────────────────────────────────

PURR_MODULE_REGISTER(app_manager) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_SYSTEM,
    .name              = "app_manager",
    .version           = "0.1.0",
    .kernel_min        = "0.9.0",
    .kernel_max        = "",
    .provided_catcalls = 0,
    .required_catcalls = 0,   // display/touch used at runtime via catcall, not hard-required
    .init              = app_manager_init,
    .deinit            = app_manager_deinit,
};
