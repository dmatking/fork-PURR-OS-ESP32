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

int      cupcake_hal_init(void);
uint16_t cupcake_hal_width(void);
uint16_t cupcake_hal_height(void);

// Builds the home screen, dock, and (hidden) app drawer. Safe to call once,
// after the HAL and app_manager are both up.
void cupcake_ui_init(void);

// Per-tick housekeeping: refreshes the status bar/notification panel.
// Call periodically (every ~200ms is plenty).
void cupcake_ui_tick(void);

#ifdef __cplusplus
}
#endif
