// Desktop icons — SD card, Apps folder, etc.
// Click to open files app or launch app

#include "desktop_icons.h"
#include "gl/gl.h"
#include "miniwin.h"
#include "purr_app_catalog.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "desktop";

#define ICON_SIZE       48
#define ICON_PADDING    4
#define LABEL_H         14
#define DISPLAY_WIDTH   320

// Icon registry - positions calculated at runtime in desktop_icons_paint
static desktop_icon_entry_t icons[] = {
    { 0, 0, ICON_SIZE, ICON_SIZE + LABEL_H, "SD Card", ICON_SD_CARD },
    { 0, 0, ICON_SIZE, ICON_SIZE + LABEL_H, "Apps", ICON_APPS },
};

// Draw a single icon (box + label)
static void draw_icon(const mw_gl_draw_info_t *d, const desktop_icon_entry_t *icon)
{
    int16_t ix = icon->x;
    int16_t iy = icon->y;

    // Draw icon box (white background, gray border)
    mw_gl_set_fg_colour(0x999999);
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_rectangle(d, ix, iy, ix + ICON_SIZE, iy + ICON_SIZE);

    mw_gl_set_fg_colour(0xFFFFFF);
    mw_gl_rectangle(d, ix + 1, iy + 1, ix + ICON_SIZE - 1, iy + ICON_SIZE - 1);

    // Draw icon content (simple text or symbol)
    mw_gl_set_fg_colour(0x000000);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_16);

    switch (icon->icon_id) {
    case ICON_SD_CARD:
        // Draw "SD" text in center
        mw_gl_string(d, ix + 12, iy + 18, "SD");
        break;
    case ICON_APPS:
        // Draw grid pattern (2x2 dots)
        mw_gl_set_fg_colour(0x0066FF);
        mw_gl_set_fill(MW_GL_FILL);
        mw_gl_rectangle(d, ix + 10, iy + 10, ix + 16, iy + 16);
        mw_gl_rectangle(d, ix + 24, iy + 10, ix + 30, iy + 16);
        mw_gl_rectangle(d, ix + 10, iy + 24, ix + 16, iy + 30);
        mw_gl_rectangle(d, ix + 24, iy + 24, ix + 30, iy + 30);
        break;
    default:
        break;
    }

    // Draw label below icon
    mw_gl_set_fg_colour(0xFFFFFF);
    mw_gl_set_font(MW_GL_FONT_9);
    int16_t label_width = 0;
    for (const char *p = icon->label; *p; p++) label_width += 6;  // Approximate
    int16_t label_x = ix + (ICON_SIZE - label_width) / 2;
    mw_gl_string(d, label_x, iy + ICON_SIZE + 2, icon->label);
}

void desktop_icons_paint(const mw_gl_draw_info_t *d)
{
    if (!d) return;

    // Update icon positions based on display width
    int16_t right_x = mw_hal_lcd_get_display_width() - ICON_SIZE - ICON_PADDING;
    icons[0].x = right_x;
    icons[0].y = ICON_PADDING;
    icons[1].x = right_x;
    icons[1].y = ICON_SIZE + LABEL_H + ICON_PADDING * 3;

    for (int i = 0; i < ICON_COUNT; i++) {
        draw_icon(d, &icons[i]);
    }
}

int desktop_icons_touch(int16_t x, int16_t y)
{
    for (int i = 0; i < ICON_COUNT; i++) {
        const desktop_icon_entry_t *icon = &icons[i];
        if (x >= icon->x && x < icon->x + icon->w &&
            y >= icon->y && y < icon->y + icon->h) {
            ESP_LOGI(TAG, "clicked icon: %s", icon->label);
            return icon->icon_id;
        }
    }
    return -1;
}

void desktop_icon_launch(int icon_type)
{
    switch (icon_type) {
    case ICON_SD_CARD:
        // Launch files app
        for (int i = 0; i < purr_catalog_count; i++) {
            if (purr_catalog[i].name && strstr(purr_catalog[i].name, "Files")) {
                purr_catalog[i].launch();
                return;
            }
        }
        break;

    case ICON_APPS:
        // Launch launcher or app manager
        for (int i = 0; i < purr_catalog_count; i++) {
            if (purr_catalog[i].name && (strstr(purr_catalog[i].name, "Launcher") ||
                                          strstr(purr_catalog[i].name, "Programs"))) {
                purr_catalog[i].launch();
                return;
            }
        }
        break;

    default:
        break;
    }
}
