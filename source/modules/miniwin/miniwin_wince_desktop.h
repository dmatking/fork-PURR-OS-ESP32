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

// Explicitly repaints every system layer window by handle (wallpaper,
// icons, taskbar+menu). Call whenever something that was covering part of
// the screen goes away and MiniWin's own mw_paint_all() can't be trusted
// to catch it on its own — it walks z-order 0..N assuming no gaps, and
// mw_bring_window_to_front() (called on every lock/unlock and every
// taskbar focus switch) is exactly the kind of z-order churn that creates
// those gaps, in practice leaving stale pixels behind until something else
// happens to repaint that region. Used by miniwin_win.c's mw_win_destroy()
// (an app window closing exposes whatever's underneath) and this file's
// own on_lock_transition() (unlocking exposes the layers the lock overlay
// was covering).
void wce_redraw_layers(void);

// The taskbar+Start Menu window's handle. This window resizes itself live
// (grows upward to cover the Start Menu popup when open, shrinks back to
// just the bottom strip when closed — see resize_taskbar_window() in the
// .c file), so its on-screen origin isn't fixed; miniwin_module.c's task
// loop uses this handle plus wce_status_rect() below (not a hardcoded
// rect) to correctly target the RAM/battery corner's periodic repaint
// regardless of the menu's current state.
mw_handle_t wce_taskbar_handle(void);

// The dedicated lock-overlay window's handle — invisible and inert until
// a lock fires (see miniwin_lock_set_transition_cb()'s callback in the .c
// file), at which point it's made visible, brought to the front of the
// z-order (outranking any open app window), and painted. Used by
// miniwin_module.c's task loop to target the lock footer's own slow
// periodic repaint.
mw_handle_t wce_lock_handle(void);

// Flips the taskbar corner between its free-RAM and battery-voltage
// readouts. Called by miniwin_module.c's task loop every few seconds —
// see that call site for the actual rotation cadence.
void wce_desktop_toggle_status(void);

// Bounding rect (window-local — see wce_taskbar_handle()'s comment on why
// this can't be a fixed constant) of the taskbar corner's RAM/battery
// readout — used by miniwin_module.c's task loop to scope that corner's
// periodic repaint to just itself.
void wce_status_rect(mw_util_rect_t *out);

// Bounding rect of the lock overlay's battery/RAM footer — used by
// miniwin_module.c's task loop to scope that footer's own slow periodic
// repaint (see LOCK_INFO_REFRESH_MS there) to just that strip. Window-
// local, but the lock window is always full-screen with a (0,0) origin,
// so this one happens to equal its screen-absolute position too.
void wce_lock_info_rect(mw_util_rect_t *out);

// Height in pixels of the taskbar strip along the bottom of the screen
// *at rest* (Start Menu closed) — miniwin_win.c's mw_win_create() still
// subtracts exactly this much from every app window's rect, same as
// before the taskbar became its own resizable window; app windows only
// ever need to avoid the permanent strip, never the transient popup.
int16_t wce_taskbar_height(void);

#ifdef __cplusplus
}
#endif
