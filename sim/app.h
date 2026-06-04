#pragma once
// Simulator glue header — referenced by MiniWin's Windows HAL files.
// Provides the Win32 window handle, mouse state, and main-loop pump.

#ifdef __cplusplus
extern "C" {
#endif

#include <windows.h>
#include <stdbool.h>

// Set by main.c after CreateWindow(); read by hal_lcd.c / hal_touch.c
extern HWND hwnd;

// Updated by WM_MOUSEMOVE in main.c; read by hal_touch.c
extern int mx;
extern int my;

// Updated by WM_LBUTTONDOWN/UP in main.c; read by hal_touch.c
extern bool mouse_down;

// Called from hal_touch.c inside mw_hal_touch_get_state() — drains the Win32 message queue
void app_main_loop_process(void);

#ifdef __cplusplus
}
#endif
