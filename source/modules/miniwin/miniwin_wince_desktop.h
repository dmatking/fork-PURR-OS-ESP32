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

#ifdef __cplusplus
}
#endif
