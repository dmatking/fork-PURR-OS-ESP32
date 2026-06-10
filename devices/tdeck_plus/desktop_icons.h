#pragma once
#include "gl/gl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Desktop icon system — manage icons on desktop area
// Icons drawn above taskbar, handle clicks

typedef enum {
    ICON_SD_CARD,
    ICON_APPS,
    ICON_COUNT
} desktop_icon_t;

typedef struct {
    int16_t x, y;           // Top-left position
    int16_t w, h;           // Dimensions
    const char *label;      // Icon label
    desktop_icon_t icon_id; // Icon type
} desktop_icon_entry_t;

// Draw all desktop icons
void desktop_icons_paint(const mw_gl_draw_info_t *d);

// Check if click hit an icon, return icon type or -1
int desktop_icons_touch(int16_t x, int16_t y);

// Launch action for icon (opens folder or app)
void desktop_icon_launch(int icon_type);

#ifdef __cplusplus
}
#endif
