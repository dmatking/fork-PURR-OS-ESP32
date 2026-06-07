// main_sim.cpp — Win32 host for the PURR OS simulator.
// Shell (blackberry / explorer / classic) is selected at cmake time via PURR_SHELL.
// Creates a window sized to the shell's display dimensions, routes mouse to
// MiniWin touch events, and runs the MiniWin main loop.

#ifdef _WIN32

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "app.h"      // hwnd, mx, my, mouse_down, app_main_loop_process
#include "compat/kitt_sim.h"

extern "C" {
#include "miniwin.h"
}

#ifndef SIM_W
#  define SIM_W 320
#endif
#ifndef SIM_H
#  define SIM_H 240
#endif
#ifndef SIM_TITLE
#  define SIM_TITLE L"PURR OS Simulator"
#endif

// Declare kitt_sim_setup (defined in kitt_sim.cpp)
void kitt_sim_setup();

// ── Globals (declared extern in app.h) ───────────────────────────────────────
HWND hwnd       = NULL;
int  mx         = 0;
int  my         = 0;
bool mouse_down = false;

// ── Win32 message pump ────────────────────────────────────────────────────────
void app_main_loop_process(void) {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) ExitProcess(0);
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

// ── Window procedure ──────────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEMOVE:
        mx = (int)(short)LOWORD(lp);
        my = (int)(short)HIWORD(lp);
        return 0;
    case WM_LBUTTONDOWN:
        mouse_down = true;
        mx = (int)(short)LOWORD(lp);
        my = (int)(short)HIWORD(lp);
        SetCapture(hWnd);
        return 0;
    case WM_LBUTTONUP:
        mouse_down = false;
        ReleaseCapture();
        return 0;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hWnd, msg, wp, lp);
    }
}

// ── WinMain ───────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    WNDCLASSW wc    = {};
    wc.style        = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc  = WndProc;
    wc.hInstance    = hInst;
    wc.hCursor      = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"PurrOSSim";
    RegisterClassW(&wc);

    RECT r = {0, 0, SIM_W, SIM_H};
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    hwnd = CreateWindowW(L"PurrOSSim", SIM_TITLE,
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                         CW_USEDEFAULT, CW_USEDEFAULT,
                         r.right - r.left, r.bottom - r.top,
                         NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    kitt_sim_setup();

    mw_init();

    while (true) {
        app_main_loop_process();
        mw_process_message();
        mw_paint_all();
        Sleep(MW_TICK_PERIOD_MS);
    }
    return 0;
}

#endif // _WIN32
