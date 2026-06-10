#include "app_magidos.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "purr_apps_common.h"
#include "purr_taskbar.h"

#ifdef PURR_HAS_MAGIDOS
#include "drv_8086.h"
#include "magidos_cga.h"
#include "magidos_filepicker.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "app_magidos";

// Window state
static mw_handle_t s_handle = MW_INVALID_HANDLE;
static TaskHandle_t s_emulator_task = NULL;

// CGA VRAM snapshot (80x25 chars @ 2 bytes/char = 4000 bytes)
#define CGA_COLS 80
#define CGA_ROWS 25
#define CGA_BUFSIZE (CGA_COLS * CGA_ROWS * 2)
static uint8_t s_cga_vram[CGA_BUFSIZE];
static volatile bool s_vram_dirty = false;

// RGB888 framebuffer in PSRAM — allocated at window creation, freed on removal.
// Sized to the window client area; blitted with mw_gl_colour_bitmap each repaint.
static uint8_t *s_fb = NULL;
static int s_fb_w = 0, s_fb_h = 0;

// Refresh period: 3 MiniWin ticks = 60ms ≈ 16fps
#define REFRESH_TICKS 3

// Emulator task
static void _emulator_task(void *arg)
{
    (void)arg;

    // Init emulator
    drv_8086_config_t cfg = { .bios_path = NULL, .mem_size = 0 };
    if (drv_8086_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "8086 init failed");
        s_emulator_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Set frame callback to capture CGA output
    drv_8086_set_frame_callback([](const uint8_t *vram, int cols, int rows) {
        if (cols == CGA_COLS && rows == CGA_ROWS && vram) {
            memcpy(s_cga_vram, vram, CGA_BUFSIZE);
            s_vram_dirty = true;
        }
    });

    // Show file picker and load
    const char *path = magidos_filepicker_show();
    if (!path || drv_8086_load_file(path) != ESP_OK) {
        ESP_LOGW(TAG, "load failed or no file selected");
        s_emulator_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "loaded %s, starting emulation", path);

    // Emulation loop
    while (drv_8086_step()) {
        vTaskDelay(pdMS_TO_TICKS(1));  // yield to WM
    }

    ESP_LOGI(TAG, "emulation ended");
    s_emulator_task = NULL;
    vTaskDelete(NULL);
}

// Paint callback — single blit of the pre-rendered RGB888 framebuffer
static void _paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);

    if (!s_fb || s_emulator_task == NULL) {
        mw_gl_set_fill(MW_GL_FILL);
        mw_gl_set_border(MW_GL_BORDER_OFF);
        mw_gl_set_solid_fill_colour(MW_HAL_LCD_BLACK);
        mw_gl_rectangle(d, 0, 0, cr.width, cr.height);
        mw_gl_set_fg_colour(MW_HAL_LCD_WHITE);
        mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
        mw_gl_set_font(MW_GL_FONT_12);
        mw_gl_string(d, 8, 20, "MagiDOS");
        mw_gl_string(d, 8, 36, "Loading...");
        return;
    }

    magidos_cga_render(s_cga_vram, CGA_COLS, CGA_ROWS, s_fb, s_fb_w, s_fb_h);
    mw_gl_colour_bitmap(d, 0, 0, (uint16_t)s_fb_w, (uint16_t)s_fb_h, s_fb);
    s_vram_dirty = false;
}

// Message callback
static void _message(const mw_message_t *msg)
{
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE: {
        mw_util_rect_t cr = mw_get_window_client_rect(msg->recipient_handle);
        s_fb_w = cr.width;
        s_fb_h = cr.height;
        s_fb = (uint8_t*)heap_caps_malloc((size_t)(s_fb_w * s_fb_h * 3), MALLOC_CAP_SPIRAM);
        if (!s_fb) {
            ESP_LOGE(TAG, "framebuffer alloc failed (%dx%d)", s_fb_w, s_fb_h);
        }

        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        mw_paint_window_client(msg->recipient_handle);

        if (s_emulator_task == NULL) {
            BaseType_t rc = xTaskCreate(_emulator_task, "magidos", 8192, NULL, 3, &s_emulator_task);
            if (rc != pdPASS) ESP_LOGE(TAG, "failed to create emulator task");
        }

        mw_set_timer(mw_tick_counter + REFRESH_TICKS, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;
    }

    case MW_WINDOW_REMOVED_MESSAGE:
        taskbar_unregister(msg->recipient_handle);
        if (s_fb) { heap_caps_free(s_fb); s_fb = NULL; }
        s_handle = MW_INVALID_HANDLE;
        break;

    case MW_TIMER_MESSAGE:
        if (s_vram_dirty) {
            mw_paint_window_client(msg->recipient_handle);
        }
        mw_set_timer(mw_tick_counter + REFRESH_TICKS, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;

    case MW_TOUCH_DOWN_MESSAGE: {
        int16_t ty = (int16_t)(msg->message_data & 0xFFFF);
        int win_h = mw_get_window_client_rect(msg->recipient_handle).height;
        if (ty > win_h / 2)
            drv_8086_key(0x00, 0x50);  // down arrow
        else
            drv_8086_key(0x00, 0x48);  // up arrow
        break;
    }

    default:
        break;
    }
}

void app_magidos_launch(void)
{
    if (s_handle != MW_INVALID_HANDLE) {
        if (mw_get_window_flags(s_handle) & MW_WINDOW_FLAG_IS_MINIMISED)
            mw_set_window_minimised(s_handle, false);
        mw_bring_window_to_front(s_handle);
        return;
    }

    mw_util_rect_t r;
    mw_util_set_rect(&r, APP_WIN_X(210), 50, 210, 180);
    s_handle = mw_add_window(&r, "MagiDOS",
        _paint, _message, NULL, 0, APP_WIN_FLAGS, NULL);
    taskbar_register(s_handle, "MagiDOS");
}

#else  // !PURR_HAS_MAGIDOS

// Stub: MagiDOS not enabled
void app_magidos_launch(void)
{
    // No-op
}

#endif  // PURR_HAS_MAGIDOS
