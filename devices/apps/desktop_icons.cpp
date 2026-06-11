// Desktop icons — dynamic registry, custom draw/launch callbacks.

#include "desktop_icons.h"
#include "gl/gl.h"
#include "miniwin.h"
#include "purr_app_catalog.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "desktop";

#define ICON_PADDING   4

static desktop_icon_entry_t s_icons[DESKTOP_ICON_MAX];
static int s_count = 0;

// ── Registration ──────────────────────────────────────────────────────────────

int desktop_icon_register(const char *label, int16_t x, int16_t y,
                           icon_draw_fn_t draw, icon_launch_fn_t launch)
{
    if (s_count >= DESKTOP_ICON_MAX) {
        ESP_LOGW(TAG, "icon table full — skipping '%s'", label ? label : "?");
        return -1;
    }
    desktop_icon_entry_t *e = &s_icons[s_count];
    e->label  = label;
    e->x      = x;
    e->y      = y;
    e->w      = ICON_SIZE;
    e->h      = ICON_SIZE + ICON_LABEL_H;
    e->draw   = draw;
    e->launch = launch;
    return s_count++;
}

// ── Built-in draw helpers ─────────────────────────────────────────────────────

static void _draw_frame(const mw_gl_draw_info_t *d, int16_t x, int16_t y)
{
    mw_gl_set_fg_colour(0x999999);
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_rectangle(d, x, y, x + ICON_SIZE, y + ICON_SIZE);
    mw_gl_set_fg_colour(0xFFFFFF);
    mw_gl_rectangle(d, x + 1, y + 1, x + ICON_SIZE - 1, y + ICON_SIZE - 1);
}

void desktop_icon_draw_sdcard(const mw_gl_draw_info_t *d, int16_t x, int16_t y)
{
    _draw_frame(d, x, y);
    mw_gl_set_fg_colour(0x000000);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_16);
    mw_gl_string(d, x + 12, y + 18, "SD");
}

void desktop_icon_draw_apps(const mw_gl_draw_info_t *d, int16_t x, int16_t y)
{
    _draw_frame(d, x, y);
    mw_gl_set_fg_colour(0x0066FF);
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_rectangle(d, x + 10, y + 10, x + 16, y + 16);
    mw_gl_rectangle(d, x + 24, y + 10, x + 30, y + 16);
    mw_gl_rectangle(d, x + 10, y + 24, x + 16, y + 30);
    mw_gl_rectangle(d, x + 24, y + 24, x + 30, y + 30);
}

void desktop_icon_draw_generic(const mw_gl_draw_info_t *d, int16_t x, int16_t y)
{
    _draw_frame(d, x, y);
    mw_gl_set_fg_colour(0x444444);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_string(d, x + 16, y + 20, "P");
}

// ── Default icon launch actions ───────────────────────────────────────────────

static void _launch_by_name(const char *needle1, const char *needle2)
{
    for (int i = 0; i < purr_catalog_count; i++) {
        const char *n = purr_catalog[i].name;
        if (!n) continue;
        if (strstr(n, needle1) || (needle2 && strstr(n, needle2))) {
            purr_catalog[i].launch();
            return;
        }
    }
    ESP_LOGW(TAG, "no app matching '%s'", needle1);
}

static void _launch_sdcard(void)  { _launch_by_name("Files", "SD"); }
static void _launch_apps(void)    { _launch_by_name("Launcher", "Programs"); }

// ── Default icons ─────────────────────────────────────────────────────────────

void desktop_icons_register_defaults(void)
{
    // x=0,y=0 means auto-position at right edge (see desktop_icons_paint)
    desktop_icon_register("SD Card", 0, 0, desktop_icon_draw_sdcard, _launch_sdcard);
    desktop_icon_register("Apps",    0, 0, desktop_icon_draw_apps,    _launch_apps);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void desktop_icons_paint(const mw_gl_draw_info_t *d)
{
    if (!d) return;

    int16_t right_x = mw_hal_lcd_get_display_width() - ICON_SIZE - ICON_PADDING;
    int16_t auto_y  = ICON_PADDING;
    int16_t step    = ICON_SIZE + ICON_LABEL_H + ICON_PADDING * 2;

    for (int i = 0; i < s_count; i++) {
        desktop_icon_entry_t *e = &s_icons[i];

        // Auto-position icons at top-right (x==0 && y==0 sentinel)
        if (e->x == 0 && e->y == 0) {
            e->x = right_x;
            e->y = auto_y;
        }
        auto_y = e->y + step;

        // Draw frame + icon glyph
        if (e->draw) e->draw(d, e->x, e->y);

        // Label
        mw_gl_set_fg_colour(0xFFFFFF);
        mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
        mw_gl_set_font(MW_GL_FONT_9);
        int16_t lw = (int16_t)(strlen(e->label) * 6);
        mw_gl_string(d, e->x + (ICON_SIZE - lw) / 2, e->y + ICON_SIZE + 2, e->label);
    }
}

// ── Hit-test ──────────────────────────────────────────────────────────────────

int desktop_icons_touch(int16_t x, int16_t y)
{
    for (int i = 0; i < s_count; i++) {
        const desktop_icon_entry_t *e = &s_icons[i];
        if (x >= e->x && x < e->x + e->w &&
            y >= e->y && y < e->y + e->h) {
            ESP_LOGI(TAG, "hit icon[%d]: %s", i, e->label);
            return i;
        }
    }
    return -1;
}

void desktop_icon_launch(int idx)
{
    if (idx < 0 || idx >= s_count) return;
    if (s_icons[idx].launch) s_icons[idx].launch();
}
