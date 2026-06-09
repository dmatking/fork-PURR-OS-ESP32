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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "app_magidos";

// Window state
static mw_handle_t s_handle = MW_INVALID_HANDLE;
static TaskHandle_t s_emulator_task = NULL;

// CGA framebuffer (80x25 chars @ 2 bytes/char = 4000 bytes)
#define CGA_COLS 80
#define CGA_ROWS 25
#define CGA_BUFSIZE (CGA_COLS * CGA_ROWS * 2)
static uint8_t s_cga_vram[CGA_BUFSIZE];
static bool s_vram_dirty = false;

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

// Paint callback — render CGA text using MiniWin primitives
static void _paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);

    // If emulator not running, show status
    if (s_emulator_task == NULL || drv_8086_vram() == NULL) {
        mw_gl_set_fill(MW_GL_FILL);
        mw_gl_set_border(MW_GL_BORDER_OFF);
        mw_gl_set_solid_fill_colour(0x0000);  // black background
        mw_gl_rectangle(d, 0, 0, cr.width, cr.height);

        mw_gl_set_fg_colour(0xFFFF);  // white text
        mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
        mw_gl_set_font(MW_GL_FONT_9);
        mw_gl_string(d, 8, 20, "MagiDOS — 8086 emulator");
        mw_gl_string(d, 8, 40, "Loading...");
        return;
    }

    // Render CGA text mode using MiniWin drawing
    // For simplicity: just text output with CGA colors
    // CGA palette: 16 colors
    const uint16_t cga_pal[16] = {
        0x0000, 0x0015, 0x0540, 0x0555,  // black, blue, green, cyan
        0xA800, 0xA815, 0xAAA0, 0xAD55,  // red, magenta, brown, light grey
        0x5AAB, 0x555F, 0x57E0, 0x57FF,  // dark grey, bright blue, bright green, bright cyan
        0xF800, 0xF81F, 0xFFE0, 0xFFFF,  // bright red, bright magenta, yellow, white
    };

    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(0x0000);
    mw_gl_rectangle(d, 0, 0, cr.width, cr.height);

    // Render CGA text — simplified: just show the raw characters
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);

    for (int row = 0; row < CGA_ROWS && row * 12 < cr.height; row++) {
        for (int col = 0; col < CGA_COLS && col * 6 < cr.width; col++) {
            uint8_t ch = s_cga_vram[(row * CGA_COLS + col) * 2 + 0];
            uint8_t attr = s_cga_vram[(row * CGA_COLS + col) * 2 + 1];
            uint8_t fg = attr & 0x0F;
            // uint8_t bg = (attr >> 4) & 0x0F;  // could use for background

            if (ch >= 0x20 && ch < 0x7F) {
                char buf[2] = { (char)ch, 0 };
                mw_gl_set_fg_colour(cga_pal[fg]);
                mw_gl_string(d, col * 6 + 2, row * 12 + 4, buf);
            }
        }
    }

    s_vram_dirty = false;
}

// Message callback
static void _message(const mw_message_t *msg)
{
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        mw_paint_window_client(msg->recipient_handle);

        // Start emulator task
        if (s_emulator_task == NULL) {
            BaseType_t rc = xTaskCreate(_emulator_task, "magidos", 8192, NULL, 3, &s_emulator_task);
            if (rc != pdPASS) {
                ESP_LOGE(TAG, "failed to create emulator task");
            }
        }
        break;

    case MW_WINDOW_REMOVED_MESSAGE:
        taskbar_unregister(msg->recipient_handle);
        // Emulator task will self-delete when done
        break;

    case MW_PAINT_WINDOW_CLIENT_MESSAGE:
        // Refresh paint if VRAM changed
        if (s_vram_dirty) {
            mw_paint_window_client(msg->recipient_handle);
        }
        break;

    case MW_TOUCH_DOWN_MESSAGE: {
        // Simple touch handling: map Y coordinate to DOS keys
        int16_t ty = (int16_t)(msg->message_data & 0xFFFF);
        int win_h = mw_util_get_rect(msg->recipient_handle).height;
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
