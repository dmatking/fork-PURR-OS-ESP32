#pragma once
#include "gl/gl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Desktop icon system — dynamic registration, custom draw + launch callbacks.
// Usage (one-liner per icon):
//   desktop_icon_register("SD Card", x, y, draw_sdcard, launch_sdcard);
//
// The desktop module owns layout when x==0 && y==0 (auto-position at right edge).

#define DESKTOP_ICON_MAX   16
#define ICON_SIZE          48
#define ICON_LABEL_H       14

typedef void (*icon_draw_fn_t)(const mw_gl_draw_info_t *d, int16_t x, int16_t y);
typedef void (*icon_launch_fn_t)(void);

typedef struct {
    const char     *label;
    int16_t         x, y;      // 0,0 = auto-positioned at right edge
    int16_t         w, h;      // set by register; w=ICON_SIZE h=ICON_SIZE+ICON_LABEL_H
    icon_draw_fn_t  draw;
    icon_launch_fn_t launch;
} desktop_icon_entry_t;

// Register an icon. Returns index (>=0) or -1 if table is full.
int  desktop_icon_register(const char *label, int16_t x, int16_t y,
                            icon_draw_fn_t draw, icon_launch_fn_t launch);

// Helpers: built-in draw routines for standard icons
void desktop_icon_draw_sdcard(const mw_gl_draw_info_t *d, int16_t x, int16_t y);
void desktop_icon_draw_apps(const mw_gl_draw_info_t *d, int16_t x, int16_t y);
void desktop_icon_draw_generic(const mw_gl_draw_info_t *d, int16_t x, int16_t y);

// Draw all registered icons
void desktop_icons_paint(const mw_gl_draw_info_t *d);

// Hit-test: returns icon index or -1
int  desktop_icons_touch(int16_t x, int16_t y);

// Launch: fire the launch callback for a registered icon
void desktop_icon_launch(int icon_index);

// Register the default built-in icons (SD Card + Apps).
// Call this at startup if you want the standard layout.
void desktop_icons_register_defaults(void);

#ifdef __cplusplus
}
#endif
