// miniwin_wince_icons.h — desktop icon bitmaps for the WinCE-style MiniWin
// desktop (miniwin_wince_desktop.c). Only compiled in when
// CONFIG_PURR_MINIWIN_DESKTOP_WINCE is set.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 24x24, 1bpp monochrome, row-major/MSB-first — same format as the vendored
// MiniWin/bitmaps/*_icon_*.c files, drawn with mw_gl_monochrome_bitmap().
#define WCE_ICON_SIZE 24

extern const uint8_t wce_icon_settings[];
extern const uint8_t wce_icon_terminal[];
extern const uint8_t wce_icon_calculator[];
extern const uint8_t wce_icon_msn[];

#ifdef __cplusplus
}
#endif
