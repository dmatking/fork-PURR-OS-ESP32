// meshdiag.c — PURR OS Meshtastic hardware/debugging diagnostics screen.
//
// Not a chat client (see meshchat.c for that) — this is purely for bringing
// up and debugging the radio itself: raw RSSI/SNR, node count, kernel log
// tail (no serial cable needed), and a one-shot test broadcast. Built for
// the T-Deck Plus Pounce build, which boots straight into this instead of
// the launcher (see kernel_tdp_boot.c) — keyboard-only, no trackball
// required: Tab/Shift+Tab cycles focus, Enter activates, same as every
// other Pounce window.

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "purr_win.h"
#include "purr_kernel.h"
#include "purr_module.h"
#include "meshtastic.h"

#define KLOG_TAIL_SIZE 2048
#define REFRESH_MS     2000

static purr_win_t s_win        = 0;
static purr_wid_t s_stats_out  = 0;
static purr_wid_t s_log_out    = 0;
static purr_wid_t s_send_in    = 0;

static TaskHandle_t s_refresh_task = NULL;
static bool         s_running      = false;
static char        *s_klog_buf     = NULL;   // heap/PSRAM — see meshchat.c's identical rationale
// Given by diag_refresh_task() right before it self-deletes, waited on by
// meshdiag_deinit() before it destroys s_win — closes the exact same
// use-after-free meshchat.c's s_refresh_done fixes (deinit() previously
// destroyed s_win/s_stats_out/s_log_out with zero synchronization at all
// against this task, which could be mid refresh_stats()/refresh_klog() on
// those same handles at any point in its REFRESH_MS=2000ms cycle).
static SemaphoreHandle_t s_refresh_done = NULL;

static void refresh_stats(void) {
    if (!s_stats_out) return;

    const catcall_radio_t *radio = purr_kernel_radio();
    char buf[512];
    int n = 0;

    n += snprintf(buf + n, sizeof(buf) - n,
        "uptime: %llums   free ram: %u\n",
        (unsigned long long)purr_kernel_uptime_ms(), (unsigned)purr_kernel_free_ram());

    int batt = purr_kernel_battery_percent();
    n += snprintf(buf + n, sizeof(buf) - n,
        "wifi: %s   sd: %s   battery: %s\n",
        purr_kernel_wifi_connected() ? "connected" : "down",
        purr_kernel_sd_available() ? "ok" : "none",
        batt < 0 ? "unknown" : "");
    if (batt >= 0) {
        n += snprintf(buf + n, sizeof(buf) - n, "  (%d%%)\n", batt);
    }

    n += snprintf(buf + n, sizeof(buf) - n,
        "lora hw: %s\n", purr_kernel_lora_available() ? "present" : "not detected");

    if (radio) {
        n += snprintf(buf + n, sizeof(buf) - n,
            "radio rssi: %d dBm   snr: %.1f dB\n",
            radio->rssi ? radio->rssi() : 0,
            radio->snr ? radio->snr() : 0.0f);
    } else {
        n += snprintf(buf + n, sizeof(buf) - n, "radio: no catcall registered\n");
    }

    n += snprintf(buf + n, sizeof(buf) - n,
        "mesh: %s / %s   nodes: %d   my id: !%08lX\n",
        mesh_manager_ready() ? "ready" : "starting",
        mesh_manager_is_alive() ? "alive" : "not responding",
        mesh_manager_node_count(), (unsigned long)mesh_manager_node_id());

    purr_win_textarea_set(s_stats_out, buf);
}

static void refresh_klog(void) {
    if (!s_log_out || !s_klog_buf) return;
    purr_kernel_klog_tail(s_klog_buf, KLOG_TAIL_SIZE);
    purr_win_textarea_set(s_log_out, s_klog_buf);
}

static void diag_refresh_task(void *arg) {
    (void)arg;
    while (s_running) {
        refresh_stats();
        refresh_klog();
        // Short steps, not one REFRESH_MS vTaskDelay — see s_refresh_done's
        // declaration comment; meshdiag_deinit() blocks on this task
        // actually exiting, so how quickly it notices s_running == false
        // directly bounds how long a Kill/close stalls.
        for (int waited_ms = 0; waited_ms < REFRESH_MS && s_running; waited_ms += 200) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    if (s_refresh_done) xSemaphoreGive(s_refresh_done);
    // Must match the WithCaps variant used to create this task (see its
    // xTaskCreateWithCaps() call site below) — same pattern as
    // meshchat.c's buddy_refresh_task().
    vTaskDeleteWithCaps(NULL);
}

static void on_send_test(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    const char *text = purr_win_textarea_get(s_send_in);
    if (!text || !*text) text = "PURR OS meshdiag test";
    bool ok = mesh_manager_send_text(MESH_BROADCAST, text);
    ESP_LOGI("meshdiag", "test send %s: \"%s\"", ok ? "OK" : "FAILED", text);
    refresh_stats();
}

static void on_refresh_click(purr_wid_t w, purr_event_t e, void *user) {
    (void)w; (void)e; (void)user;
    refresh_stats();
    refresh_klog();
}

static int meshdiag_init(void) {
    if (!s_klog_buf) s_klog_buf = heap_caps_malloc(KLOG_TAIL_SIZE, MALLOC_CAP_SPIRAM);
    // Reused across relaunches — starts "empty" (taken), which is exactly
    // the state meshdiag_deinit()'s xSemaphoreTake() below needs at the
    // start of every run.
    if (!s_refresh_done) s_refresh_done = xSemaphoreCreateBinary();

    s_win = purr_win_create("Mesh Diagnostics");

    s_stats_out = purr_win_textarea(s_win, 100, 35);

    purr_wid_t row = purr_win_row(s_win, 4);
    purr_win_button(s_win, "Refresh", on_refresh_click, NULL);
    purr_win_layout_end(row);

    s_send_in = purr_win_textarea(s_win, 70, 12);
    purr_win_button(s_win, "Send Test", on_send_test, NULL);

    s_log_out = purr_win_textarea(s_win, 100, 40);

    refresh_stats();
    refresh_klog();
    purr_win_show(s_win);
    purr_win_keyboard_show(s_win, s_send_in);

    s_running = true;
    // No NVS/flash/SD access anywhere in this task's own body — safe on a
    // PSRAM-backed stack (see app_manager.c's launch_native()/launch_meow()
    // for the same pattern this mirrors).
    xTaskCreateWithCaps(diag_refresh_task, "meshdiag_ref", 4096, NULL, 3, &s_refresh_task, MALLOC_CAP_SPIRAM);
    return 0;
}

static void meshdiag_deinit(void) {
    s_running = false;
    // Wait for diag_refresh_task() to actually exit before touching s_win
    // below — see s_refresh_done's declaration comment.
    if (s_refresh_done) xSemaphoreTake(s_refresh_done, pdMS_TO_TICKS(2000));
    s_refresh_task = NULL;

    purr_win_destroy(s_win);
    s_win = 0; s_stats_out = 0; s_log_out = 0; s_send_in = 0;
}

// ── Module header ─────────────────────────────────────────────────────────────

PURR_MODULE_REGISTER(meshdiag) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_APP,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "meshdiag",
    .version           = "1.0.0",
    .kernel_min        = "0.11.1",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = meshdiag_init,
    .deinit            = meshdiag_deinit,
};
