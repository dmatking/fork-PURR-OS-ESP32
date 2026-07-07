// cupcake_hal.c — LVGL ↔ catcall_display / catcall_touch bridge
//
// Forked from cardstack_hal.c. Cupcake is a tap-driven grid launcher (no
// snap-scrolling card stack), so unlike Cardstack this HAL does not poll the
// trackball directly — trackball input, if present, only ever goes through
// LVGL's own indev abstraction here (not registered as one in v1, since
// tile navigation is by touch only).

#include "cupcake.h"
#include "../../kernel/core/purr_kernel.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

static const char *TAG = "cupcake_hal";

#ifndef CUPCAKE_BUF_LINES
#define CUPCAKE_BUF_LINES 20
#endif
#define CUPCAKE_BUF_WIDTH 480

static lv_color_t s_buf1[CUPCAKE_BUF_WIDTH * CUPCAKE_BUF_LINES];
static lv_color_t s_buf2[CUPCAKE_BUF_WIDTH * CUPCAKE_BUF_LINES];

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t      s_disp_drv;
static lv_indev_drv_t     s_touch_drv;
static lv_indev_drv_t     s_keypad_drv;
static lv_group_t        *s_kb_group = NULL;

static uint16_t s_disp_w = 320;
static uint16_t s_disp_h = 240;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    const catcall_display_t *d = purr_kernel_display();
    if (d && d->push_pixels) {
        int32_t w = area->x2 - area->x1 + 1;
        int32_t h = area->y2 - area->y1 + 1;
        d->push_pixels(area->x1, area->y1, (int)w, (int)h, (uint16_t *)color_p);
    }
    lv_disp_flush_ready(drv);
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    const catcall_touch_t *t = purr_kernel_touch();
    if (t && t->is_pressed()) {
        uint16_t x = 0, y = 0;
        t->read_point(&x, &y);
        data->point.x = (lv_coord_t)x;
        data->point.y = (lv_coord_t)y;
        data->state   = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// Physical keyboard bridge — no LV_INDEV_TYPE_KEYPAD was ever registered
// here before, so BBQ20 keystrokes never reached LVGL at all under Cupcake
// (only the touch pointer indev existed); the on-screen keyboard was the
// only way to type, and even that was separately broken (see
// cupcake_win.c's ck_win_show()/ck_kb_show() ordering note). purr_kernel_
// input() can't be used directly — it returns "first registered (legacy)",
// which on tdeck_plus is the trackball (registered before bbq20 in
// kernel_tdp_boot.c), not the keyboard — so this scans every registered
// input catcall and reacts only to KEY_DOWN events, ignoring POINTER
// (trackball) ones.
//
// bbq20.c only ever emits KEY_DOWN (the RP2040 bridge chip has no key-up
// event), so a single press is reported as one PR cycle followed by one REL
// cycle on the next read — LVGL's keypad indev fires the object's
// LV_EVENT_KEY on the PR edge, so this one-shot pattern is sufficient; it
// doesn't need a real "held" state to drive lv_textarea's normal
// backspace/char-insert handling.
static void keypad_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    static bool     s_pending_release = false;
    static uint32_t s_pending_key     = 0;

    if (s_pending_release) {
        data->key   = s_pending_key;
        data->state = LV_INDEV_STATE_REL;
        s_pending_release = false;
        return;
    }

    int n = purr_kernel_input_count();
    for (int i = 0; i < n; i++) {
        const catcall_input_t *in = purr_kernel_input_at(i);
        if (!in || !in->poll_event) continue;
        input_event_t ev;
        while (in->poll_event(&ev)) {
            if (ev.type != INPUT_EVENT_KEY_DOWN) continue;
            uint32_t key;
            switch (ev.keycode) {
                case 0x08: case 0x7F: key = LV_KEY_BACKSPACE; break;
                case 0x0D: case 0x0A: key = LV_KEY_ENTER;     break;
                case 0x1B:            key = LV_KEY_ESC;       break;
                default:              key = ev.keycode;       break;
            }
            s_pending_key     = key;
            s_pending_release = true;
            data->key   = key;
            data->state = LV_INDEV_STATE_PR;
            return;
        }
    }
    data->state = LV_INDEV_STATE_REL;
}

int cupcake_hal_init(void)
{
    const catcall_display_t *disp = purr_kernel_display();
    if (!disp) {
        ESP_LOGE(TAG, "no display catcall");
        return -1;
    }

    display_info_t info = {0};
    if (disp->get_info) {
        disp->get_info(&info);
        s_disp_w = info.width  ? info.width  : 320;
        s_disp_h = info.height ? info.height : 240;
    }
    ESP_LOGI(TAG, "display: %ux%u", s_disp_w, s_disp_h);

    lv_init();

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, s_disp_w * CUPCAKE_BUF_LINES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res      = (lv_coord_t)s_disp_w;
    s_disp_drv.ver_res      = (lv_coord_t)s_disp_h;
    s_disp_drv.flush_cb     = flush_cb;
    s_disp_drv.draw_buf     = &s_draw_buf;
    s_disp_drv.full_refresh = 0;
    lv_disp_drv_register(&s_disp_drv);

    const catcall_touch_t *touch = purr_kernel_touch();
    if (touch) {
        lv_indev_drv_init(&s_touch_drv);
        s_touch_drv.type    = LV_INDEV_TYPE_POINTER;
        s_touch_drv.read_cb = touch_read_cb;
        lv_indev_drv_register(&s_touch_drv);
        ESP_LOGI(TAG, "touch input registered");
    } else {
        ESP_LOGW(TAG, "no touch catcall — cupcake will run touchless");
    }

    if (purr_kernel_input_count() > 0) {
        s_kb_group = lv_group_create();
        lv_indev_drv_init(&s_keypad_drv);
        s_keypad_drv.type    = LV_INDEV_TYPE_KEYPAD;
        s_keypad_drv.read_cb = keypad_read_cb;
        lv_indev_t *kp_indev = lv_indev_drv_register(&s_keypad_drv);
        lv_indev_set_group(kp_indev, s_kb_group);
        ESP_LOGI(TAG, "keypad input registered (%d input driver(s))", purr_kernel_input_count());
    } else {
        ESP_LOGW(TAG, "no input catcall — physical keyboard unavailable");
    }

    // lv_tick_inc(5) is driven entirely by cupcake_task's own render loop
    // (cupcake_module.c) now, under purr_kernel_ui_lock() — this HAL used
    // to spawn a second, separate task ("cupcake_tick") calling
    // lv_tick_inc(5) on the same 5ms cadence with no lock at all, which
    // both double-counted LVGL's tick rate (running its animation/timer
    // clock at ~2x the intended speed the entire time) and raced
    // cupcake_task's own lv_tick_inc()/lv_timer_handler() calls — a real,
    // unsynchronized concurrent access to shared LVGL state, confirmed via
    // investigation into a live LoadProhibited crash (a looping .meow
    // script's Lua VM state got corrupted, most likely as a downstream
    // effect of this race). Removed rather than locked — cupcake_task
    // already does this correctly, so there is nothing left for a second
    // task to contribute.
    ESP_LOGI(TAG, "HAL init complete");
    return 0;
}

uint16_t cupcake_hal_width(void)  { return s_disp_w; }
uint16_t cupcake_hal_height(void) { return s_disp_h; }

lv_group_t *cupcake_hal_keypad_group(void) { return s_kb_group; }
