#pragma once
// cupcake.h — Cupcake UI public API
//
// Android 1.5 ("Cupcake")-style launcher: a home screen with a handful of
// pinned app shortcuts plus a bottom dock, and a separate full-screen app
// drawer (opened from the dock's center button) listing every registered
// app. Status bar + drag-down notification panel forked from Cardstack.

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Height of the persistent status-bar strip at the top of the screen —
// shared between cupcake_ui.c (which builds the strip) and cupcake_win.c
// (which must keep app windows, and their close buttons, entirely below it).
#define CUPCAKE_STATUS_PEEK_H 22

// Height of the Lollipop nav bar (Back/Apps/Recents) pinned to the bottom of
// the screen — lives on lv_layer_top() (see cupcake_ui.c's build_lp_navbar())
// so it renders above every app window regardless of z-order, same trick the
// status bar above already uses. Shared with cupcake_win.c so app windows
// (and anything they dock to their own bottom edge) stay clear of it.
#define CUPCAKE_NAVBAR_H 40

int      cupcake_hal_init(void);
uint16_t cupcake_hal_width(void);
uint16_t cupcake_hal_height(void);

// The lv_group_t physical-keyboard keypresses are dispatched through (see
// cupcake_hal.c's keypad_read_cb) — NULL if no input catcall was registered
// at HAL-init time. cupcake_win.c adds every textarea to this group and
// focuses one on purr_win_textarea_focus() so BBQ20 keystrokes land on the
// right widget.
lv_group_t *cupcake_hal_keypad_group(void);

// uptime_ms() of the last real input event (touch press, physical key,
// trackball nav step) — see cupcake_hal.c's mark_activity(). Used by
// cupcake_ui.c's idle-timeout check.
uint64_t cupcake_hal_last_activity_ms(void);

// Builds the home screen, dock, and (hidden) app drawer. Safe to call once,
// after the HAL and app_manager are both up.
void cupcake_ui_init(void);

// Per-tick housekeeping: refreshes the status bar/notification panel, and
// checks the idle timeout (purr_kernel_screen_timeout_min()) against
// cupcake_hal_last_activity_ms() to trigger the lock screen.
// Call periodically (every ~200ms is plenty).
void cupcake_ui_tick(void);

// True once the idle timeout has fired and the lock overlay is showing
// (or the screen is dark waiting to be woken) — cleared only by the
// overlay's own tap/swipe-to-dismiss handler.
bool cupcake_ui_is_locked(void);

// Called by cupcake_hal.c the moment new input arrives while locked: makes
// the (still-locked) lock screen visible again by restoring brightness.
// Does NOT clear the locked state — that's a separate, deliberate dismiss
// gesture on the overlay itself.
void cupcake_ui_wake(void);

#ifdef __cplusplus
}
#endif
