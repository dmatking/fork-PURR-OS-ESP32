// main.c — Win32 host for the PURR OS MiniWin simulator.
// Creates a 320x240 window, routes mouse to MiniWin touch, runs the WM loop.

#ifdef _WIN32

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "app.h"
#include "miniwin.h"

#define SIM_W  320
#define SIM_H  240
#define SIM_TITLE L"PURR OS — MiniWin Sim (320x240)"

// ── Globals (declared extern in app.h) ───────────────────────────────────────
HWND  hwnd       = NULL;
int   mx         = 0;
int   my         = 0;
bool  mouse_down = false;

// ── Win32 message pump (called from hal_touch.c) ──────────────────────────────
void app_main_loop_process(void) {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) ExitProcess(0);
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

// ── Window procedure ─────────────────────────────────────────────────────────
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

// ── WinMain ──────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow) {
    (void)hPrev; (void)cmdLine;

    WNDCLASSW wc = {0};
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"PurrOSSim";
    RegisterClassW(&wc);

    // Size the window so the CLIENT area is exactly SIM_W x SIM_H
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

    // MiniWin init — calls mw_user_init(), then schedules first root paint
    mw_init();

    // Main loop: drain messages, process one MiniWin message, paint
    while (true) {
        app_main_loop_process();
        mw_process_message();
        mw_paint_all();
        Sleep(MW_TICK_PERIOD_MS);
    }

    return 0;
}

#endif // _WIN32
