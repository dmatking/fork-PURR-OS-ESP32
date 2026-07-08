// purr_crash_guard.c — see purr_crash_guard.h for the full design rationale.

#include "purr_crash_guard.h"
#include "purr_kernel.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "crash_guard";

#define NVS_NS            "purr_crash"
#define NVS_KEY_BREADCRUMB "cur"
#define STRIKE_THRESHOLD  5

// Entity names come from purr_module_header_t.name (char[32]) or
// app_entry_t.name (char[48]) — both well over NVS's 15-char key limit, so
// per-entity records are keyed by a hash instead of the name itself. The
// name is also stored inside the record (for the panic screen / logging),
// so nothing is lost by not using it as the key directly.
typedef struct {
    char    name[32];
    uint8_t count;
    uint8_t disabled;
} entry_t;

static uint32_t hash_name(const char *s)
{
    uint32_t h = 2166136261u;   // FNV-1a
    while (s && *s) { h ^= (uint8_t)(*s++); h *= 16777619u; }
    return h;
}

static void entity_key(const char *entity_name, char *out, size_t out_sz)
{
    snprintf(out, out_sz, "e%08lx", (unsigned long)hash_name(entity_name));
}

static bool load_entry(const char *entity_name, entry_t *out)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    char key[16];
    entity_key(entity_name, key, sizeof(key));
    size_t sz = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, key, out, &sz);
    nvs_close(h);
    return err == ESP_OK && sz == sizeof(*out);
}

static void save_entry(const char *entity_name, const entry_t *in)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    char key[16];
    entity_key(entity_name, key, sizeof(key));
    nvs_set_blob(h, key, in, sizeof(*in));
    nvs_commit(h);
    nvs_close(h);
}

static void clear_breadcrumb(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_BREADCRUMB, "");
    nvs_commit(h);
    nvs_close(h);
}

// Increments entity_name's strike count, persists it, and — once the
// threshold is hit (or force_panic is set, for the hang case) — shows the
// blue panic screen. purr_kernel_panic_ex() is noreturn when it actually
// renders/loops, so callers below never return past this in that case.
static void record_strike_and_maybe_panic(const char *entity_name, const char *reason, bool force_panic)
{
    if (!entity_name || !entity_name[0]) return;

    entry_t e;
    if (!load_entry(entity_name, &e)) {
        memset(&e, 0, sizeof(e));
        strncpy(e.name, entity_name, sizeof(e.name) - 1);
    }
    if (e.count < 255) e.count++;
    if (e.count >= STRIKE_THRESHOLD) e.disabled = 1;
    save_entry(entity_name, &e);

    ESP_LOGW(TAG, "strike %u/%d for '%s': %s", e.count, STRIKE_THRESHOLD,
             entity_name, reason ? reason : "");

    if (force_panic || e.count >= STRIKE_THRESHOLD) {
        char panic_reason[128];
        snprintf(panic_reason, sizeof(panic_reason), "%.32s: %.48s (strike %u/%d)",
                 entity_name, reason ? reason : "unknown", e.count, STRIKE_THRESHOLD);
        purr_kernel_panic_ex(panic_reason, /*recoverable=*/true, entity_name);
        // noreturn in practice — purr_kernel_panic_ex() loops until the
        // user forces a reset.
    }
}

void purr_crash_guard_mark_start(const char *entity_name)
{
    if (!entity_name) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_BREADCRUMB, entity_name);
    nvs_commit(h);
    nvs_close(h);
}

void purr_crash_guard_mark_stop(const char *entity_name, bool ok, const char *reason)
{
    clear_breadcrumb();
    if (ok) return;
    record_strike_and_maybe_panic(entity_name, reason, /*force_panic=*/false);
}

void purr_crash_guard_mark_hang(const char *entity_name, const char *reason)
{
    clear_breadcrumb();
    record_strike_and_maybe_panic(entity_name, reason, /*force_panic=*/true);
}

bool purr_crash_guard_is_disabled(const char *entity_name)
{
    if (!entity_name) return false;
    entry_t e;
    if (!load_entry(entity_name, &e)) return false;
    return e.disabled != 0;
}

void purr_crash_guard_check_reset_reason(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    bool unclean = (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT ||
                    r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
                    r == ESP_RST_BROWNOUT || r == ESP_RST_CPU_LOCKUP ||
                    r == ESP_RST_PWR_GLITCH);
    if (!unclean) {
        // Clean boot (power-on / intentional esp_restart / deep sleep
        // wake) — whatever breadcrumb is left over, if any, is stale
        // (e.g. the device was power-cycled mid-launch) and shouldn't be
        // blamed on anything.
        clear_breadcrumb();
        return;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    char name[48] = {0};
    size_t sz = sizeof(name);
    esp_err_t err = nvs_get_str(h, NVS_KEY_BREADCRUMB, name, &sz);
    nvs_close(h);
    if (err != ESP_OK || name[0] == '\0') return;

    ESP_LOGW(TAG, "unclean reset (reason=%d) while '%s' was active", (int)r, name);
    record_strike_and_maybe_panic(name, "unclean reset / hard crash", /*force_panic=*/false);
    clear_breadcrumb();
}
