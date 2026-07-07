// hwtest.c — PURR OS hardware tester (.claw)
// Live log of trackball motion/click and keyboard keypresses, for verifying
// those two input devices work without digging through serial logs.

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "purr_win.h"
#include "purr_kernel.h"
#include "purr_module.h"
#include "../../../kernel/catcalls/catcall_input.h"

#define HW_LOG_LINES 14
#define HW_LINE_LEN  40

static purr_win_t s_win = 0;
static purr_wid_t s_out = 0;
static TaskHandle_t s_poller = NULL;
static bool s_running = false;

static char    s_log[HW_LOG_LINES][HW_LINE_LEN];
static int     s_log_head = 0;
static int     s_log_count = 0;

static void log_line(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_log[s_log_head], HW_LINE_LEN, fmt, ap);
    va_end(ap);
    s_log_head = (s_log_head + 1) % HW_LOG_LINES;
    if (s_log_count < HW_LOG_LINES) s_log_count++;
}

static void render_log(char *buf, size_t sz) {
    size_t pos = 0;
    pos += snprintf(buf + pos, sz - pos,
        "Trackball + keyboard tester\n"
        "Roll the ball / click it / type keys.\n"
        "----------------------------------------\n");
    int start = (s_log_head - s_log_count + HW_LOG_LINES) % HW_LOG_LINES;
    for (int i = 0; i < s_log_count && pos < sz; i++) {
        int idx = (start + i) % HW_LOG_LINES;
        pos += snprintf(buf + pos, sz - pos, "%s\n", s_log[idx]);
    }
}

// ── Poller task — drains every registered input device ─────────────────────

static void poller_task(void *arg) {
    (void)arg;
    char buf[HW_LOG_LINES * HW_LINE_LEN + 128];
    bool dirty = true;

    while (s_running) {
        int n = purr_kernel_input_count();
        for (int i = 0; i < n; i++) {
            const catcall_input_t *inp = purr_kernel_input_at(i);
            if (!inp || !inp->poll_event) continue;

            input_event_t ev;
            int drained = 0;
            while (drained++ < 16 && inp->poll_event(&ev)) {
                dirty = true;
                switch (ev.type) {
                    case INPUT_EVENT_POINTER:
                        log_line("[%s] move dx=%d dy=%d", inp->name, ev.delta_x, ev.delta_y);
                        break;
                    case INPUT_EVENT_KEY_DOWN:
                        if (ev.keycode >= 0x20 && ev.keycode <= 0x7E)
                            log_line("[%s] key DOWN '%c' (0x%02X)", inp->name, (char)ev.keycode, ev.keycode);
                        else
                            log_line("[%s] key DOWN 0x%02X", inp->name, ev.keycode);
                        break;
                    case INPUT_EVENT_KEY_UP:
                        log_line("[%s] key UP   0x%02X", inp->name, ev.keycode);
                        break;
                    default:
                        break;
                }
            }
        }

        if (dirty) {
            render_log(buf, sizeof(buf));
            purr_win_textarea_set(s_out, buf);
            dirty = false;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    // Must match the WithCaps variant used to create this task.
    vTaskDeleteWithCaps(NULL);
}

// ── Close button ─────────────────────────────────────────────────────────────

static void on_close(purr_wid_t w, purr_event_t e, void *u) {
    (void)w; (void)e; (void)u;
    purr_win_hide(s_win);
}

static void on_clear(purr_wid_t w, purr_event_t e, void *u) {
    (void)w; (void)e; (void)u;
    s_log_head = 0;
    s_log_count = 0;
    char buf[256];
    render_log(buf, sizeof(buf));
    purr_win_textarea_set(s_out, buf);
}

// ── App lifecycle ─────────────────────────────────────────────────────────────

static int hwtest_init(void) {
    s_win = purr_win_create("HW Tester");

    s_out = purr_win_textarea(s_win, 100, 80);

    purr_wid_t row = purr_win_row(s_win, 4);
    purr_win_button(s_win, "Clear", on_clear, NULL);
    purr_win_button(s_win, "Close", on_close, NULL);
    purr_win_layout_end(row);

    char buf[256];
    render_log(buf, sizeof(buf));
    purr_win_textarea_set(s_out, buf);

    purr_win_show(s_win);

    s_running = true;
    // No NVS/flash/SD access in this task's body — safe on a PSRAM-backed
    // stack (see app_manager.c's launch_native()/launch_meow() pattern).
    xTaskCreateWithCaps(poller_task, "hwtest_poll", 3072, NULL, 4, &s_poller, MALLOC_CAP_SPIRAM);
    return 0;
}

static void hwtest_deinit(void) {
    s_running = false;
    if (s_poller) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_poller = NULL;
    }
    purr_win_destroy(s_win);
    s_win = 0;
}

// ── Module header ─────────────────────────────────────────────────────────────

PURR_MODULE_REGISTER(hwtest) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_APP,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "hwtest",
    .version           = "1.0.0",
    .kernel_min        = "0.11.1",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = hwtest_init,
    .deinit            = hwtest_deinit,
};
