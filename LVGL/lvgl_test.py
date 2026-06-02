#!/usr/bin/env python3
"""
Standalone Windows CE LVGL layout tester for local development.
Tests the layout without needing the full PURR OS kernel.

Usage:
  python3 lvgl_test.py

This uses SDL2 backend for LVGL to render on your Mac.
Install requirements:
  pip3 install lv_bindings pygame
"""

import sys
import os

# Set display buffer to SDL (for local testing)
os.environ['SDL_VIDEODRIVER'] = 'windowed'

try:
    import lvgl as lv
    from lvgl.SDL import SDL
except ImportError:
    print("LVGL not installed. Install with:")
    print("  pip3 install lv_bindings pygame")
    print("  pip3 install lvgl-micropython")
    sys.exit(1)


def init_display():
    """Initialize LVGL with SDL backend."""
    SDL = lv.SDL
    disp = SDL(320, 240)
    return disp


def build_windows_ce_ui(scr):
    """Build the Windows CE inspired UI."""

    # Colors
    COLOR_DESKTOP = lv.color_hex(0x008080)      # Teal
    COLOR_TASKBAR = lv.color_hex(0xC0C0C0)      # Light gray
    COLOR_TITLEBAR = lv.color_hex(0x000080)     # Dark blue
    COLOR_HIGHLIGHT = lv.color_hex(0xFFFFFF)    # White
    COLOR_SHADOW = lv.color_hex(0x808080)       # Gray
    COLOR_TEXT = lv.color_hex(0x000000)         # Black

    # Desktop background
    scr.set_style_bg_color(COLOR_DESKTOP, 0)
    scr.set_style_bg_opa(lv.OPA.COVER, 0)

    # ── TASKBAR (BOTTOM) ───────────────────────────────────────────────────

    taskbar = lv.obj(scr)
    taskbar.set_size(320, 32)
    taskbar.set_align(lv.ALIGN.BOTTOM_MID)
    taskbar.set_layout(lv.LAYOUT.FLEX)
    taskbar.set_flex_flow(lv.FLEX_FLOW.ROW)
    taskbar.set_flex_align(lv.FLEX_ALIGN.SPACE_BETWEEN, lv.FLEX_ALIGN.CENTER, 0)
    taskbar.set_style_bg_color(COLOR_TASKBAR, 0)
    taskbar.set_style_pad_all(2, 0)
    taskbar.set_style_border_width(2, 0)
    taskbar.set_style_border_side(lv.BORDER.TOP, 0)
    taskbar.set_style_border_color(COLOR_SHADOW, 0)

    # Start Button
    start_btn = lv.button(taskbar)
    start_btn.set_width(60)
    start_btn.set_style_bg_color(COLOR_TASKBAR, 0)
    start_btn.set_style_border_width(2, 0)
    start_btn.set_style_border_color(COLOR_HIGHLIGHT, 0)
    start_btn.set_style_border_side(lv.BORDER.LEFT | lv.BORDER.TOP, 0)

    start_label = lv.label(start_btn)
    start_label.set_text("Start")
    start_label.set_style_text_color(COLOR_TEXT, 0)

    # System Tray (Right side)
    tray = lv.obj(taskbar)
    tray.set_layout(lv.LAYOUT.FLEX)
    tray.set_flex_flow(lv.FLEX_FLOW.ROW)
    tray.set_flex_align(lv.FLEX_ALIGN.END, lv.FLEX_ALIGN.CENTER, 0)
    tray.set_style_bg_opa(lv.OPA.TRANSP, 0)
    tray.set_style_pad_all(4, 0)
    tray.set_size(lv.SIZE.CONTENT, lv.SIZE.CONTENT)

    # WiFi icon
    wifi = lv.label(tray)
    wifi.set_text("W")
    wifi.set_style_text_color(COLOR_TEXT, 0)

    # Bluetooth icon
    bt = lv.label(tray)
    bt.set_text("B")
    bt.set_style_text_color(COLOR_TEXT, 0)

    # Clock
    clock = lv.label(tray)
    clock.set_text("12:34")
    clock.set_style_text_color(COLOR_TEXT, 0)

    # ── EXPLORER WINDOW (CENTER) ───────────────────────────────────────────

    win = lv.obj(scr)
    win.set_size(200, 160)
    win.set_align(lv.ALIGN.CENTER)
    win.set_layout(lv.LAYOUT.FLEX)
    win.set_flex_flow(lv.FLEX_FLOW.COLUMN)
    win.set_style_bg_color(COLOR_TASKBAR, 0)
    win.set_style_border_width(2, 0)
    win.set_style_border_color(COLOR_HIGHLIGHT, 0)
    win.set_style_border_side(lv.BORDER.LEFT | lv.BORDER.TOP, 0)
    win.set_style_pad_all(0, 0)

    # Title bar
    title_bar = lv.obj(win)
    title_bar.set_size(lv.pct(100), 20)
    title_bar.set_style_bg_color(COLOR_TITLEBAR, 0)
    title_bar.set_style_pad_all(2, 0)
    title_bar.set_style_border_width(0, 0)
    title_bar.set_layout(lv.LAYOUT.FLEX)
    title_bar.set_flex_flow(lv.FLEX_FLOW.ROW)
    title_bar.set_flex_align(lv.FLEX_ALIGN.SPACE_BETWEEN, lv.FLEX_ALIGN.CENTER, 0)

    title_text = lv.label(title_bar)
    title_text.set_text("Explorer")
    title_text.set_style_text_color(COLOR_HIGHLIGHT, 0)

    # Close button
    close_btn = lv.button(title_bar)
    close_btn.set_size(18, 18)
    close_btn.set_style_bg_color(COLOR_TITLEBAR, 0)
    close_btn.set_style_border_width(0, 0)

    close_lbl = lv.label(close_btn)
    close_lbl.set_text("x")
    close_lbl.set_style_text_color(COLOR_HIGHLIGHT, 0)

    # Content area
    content = lv.obj(win)
    content.set_size(lv.pct(100), lv.pct(100) - 20)
    content.set_style_bg_color(lv.color_hex(0xFFFFFF), 0)
    content.set_layout(lv.LAYOUT.FLEX)
    content.set_flex_flow(lv.FLEX_FLOW.COLUMN)
    content.set_style_pad_all(4, 0)
    content.set_style_border_width(0, 0)

    # File list
    for fname in ["boot.py", "main.py", "config.json"]:
        btn = lv.button(content)
        btn.set_width(lv.pct(100))
        btn.set_height(24)
        btn.set_style_bg_color(lv.color_hex(0xFFFFFF), 0)
        btn.set_style_border_width(1, 0)
        btn.set_style_border_color(COLOR_SHADOW, 0)

        lbl = lv.label(btn)
        lbl.set_text("[F] " + fname)
        lbl.set_style_text_color(COLOR_TEXT, 0)
        lbl.align(lv.ALIGN.LEFT_MID, 4, 0)

    # ── DESKTOP ICONS (TOP LEFT) ───────────────────────────────────────────

    icons = lv.obj(scr)
    icons.set_size(80, 240)
    icons.set_pos(0, 0)
    icons.set_layout(lv.LAYOUT.FLEX)
    icons.set_flex_flow(lv.FLEX_FLOW.COLUMN)
    icons.set_style_pad_all(4, 0)
    icons.set_style_bg_opa(lv.OPA.TRANSP, 0)

    icon_label = lv.label(icons)
    icon_label.set_text("[*]\nFiles")
    icon_label.set_style_text_align(lv.TEXT_ALIGN.CENTER, 0)
    icon_label.set_style_text_color(COLOR_HIGHLIGHT, 0)

    return {
        'taskbar': taskbar,
        'start': start_btn,
        'clock': clock,
        'window': win,
        'close': close_btn,
    }


def main():
    """Run the LVGL test display."""
    print("Initializing LVGL with SDL backend...")
    try:
        disp = init_display()
    except Exception as e:
        print(f"Failed to initialize display: {e}")
        return

    scr = lv.screen_active()
    print("Building Windows CE UI...")
    ui = build_windows_ce_ui(scr)

    print("\nUI initialized!")
    print("Keyboard shortcuts:")
    print("  Q - Quit")
    print("  Click Start button to test interactivity")
    print("\nClick window elements to test input handling.")

    # Run LVGL event loop
    try:
        while True:
            lv.tick_inc(10)
            lv.task_handler()
    except KeyboardInterrupt:
        print("\nExiting...")


if __name__ == '__main__':
    main()
