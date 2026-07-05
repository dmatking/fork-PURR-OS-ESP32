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

static void lv_tick_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5));
        lv_tick_inc(5);
    }
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

    // No storage access anywhere in this task (just lv_tick_inc + sleep) —
    // safe on a PSRAM-backed stack, unlike cupcake_task itself (see
    // cupcake_module.c's comment on why that one stays internal).
    static TaskHandle_t s_tick_task = NULL;
    BaseType_t tr = xTaskCreateWithCaps(lv_tick_task, "cupcake_tick", 4096, NULL, 1, &s_tick_task, MALLOC_CAP_SPIRAM);
    if (tr != pdPASS) {
        ESP_LOGE(TAG, "failed to create tick task");
        return -1;
    }

    ESP_LOGI(TAG, "HAL init complete");
    return 0;
}

uint16_t cupcake_hal_width(void)  { return s_disp_w; }
uint16_t cupcake_hal_height(void) { return s_disp_h; }
