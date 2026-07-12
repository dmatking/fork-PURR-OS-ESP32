// miniwin_wince_desktop.h — WinCE-style taskbar+start-menu desktop chrome
// for the generic MiniWin .purr module. Only compiled in when
// CONFIG_PURR_MINIWIN_DESKTOP_WINCE is set (see miniwin_module.c).
#pragma once
#include "MiniWin/miniwin.h"

#ifdef __cplusplus
extern "C" {
#endif

// Called by miniwin_win.c's mw_win_create()/mw_win_destroy() so every app
// window that goes through purr_win_create() automatically gets a taskbar
// button — no changes needed in settings.c/fileman.c/etc.
void wce_taskbar_register(mw_handle_t handle, const char *name);
void wce_taskbar_unregister(mw_handle_t handle);

// The desktop's own full-screen background window handle, used by
// miniwin_module.c's task loop to target the periodic RAM-readout repaint.
mw_handle_t wce_desktop_handle(void);

// Height in pixels of the taskbar strip along the bottom of the screen.
// miniwin_win.c's mw_win_create() subtracts this from every app window's
// rect so app windows structurally can never draw over the taskbar/Start
// button/status readout — those live in the desktop window's own paint
// callback, at the lowest z-order, and MiniWin has no compositing "always
// on top" layer (unlike Cupcake's lv_layer_top()), so the only way to keep
// them visible under a maximized app window is to never let anything else
// occupy that rect in the first place.
int16_t wce_taskbar_height(void);

#ifdef __cplusplus
}
#endif
