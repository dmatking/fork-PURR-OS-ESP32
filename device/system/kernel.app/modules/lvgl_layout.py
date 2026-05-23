"""
Windows CE inspired LVGL layout for PURR OS.
Pure MicroPython/LVGL v8 syntax — no hardcoded coordinates.

Usage:
    import lvgl_layout as layout
    layout.build_desktop(scr)  # scr = lv.screen_active()
"""

try:
    import lvgl as lv
except ImportError:
    raise ImportError("LVGL library not found. Install micropython-lvgl.")


# ── Screen geometry ──────────────────────────────────────────────────────────

_SCREEN_W  = 320
_SCREEN_H  = 480
_TASKBAR_H = 32


# ── Windows CE Color Palette ─────────────────────────────────────────────────

COLOR_DESKTOP    = lv.color_hex(0x008080)    # Teal desktop background
COLOR_TASKBAR    = lv.color_hex(0xC0C0C0)    # Light gray taskbar
COLOR_TITLEBAR   = lv.color_hex(0x000080)    # Dark blue window titles
COLOR_HIGHLIGHT  = lv.color_hex(0xFFFFFF)    # White highlights
COLOR_SHADOW     = lv.color_hex(0x808080)    # Dark gray shadows
COLOR_TEXT       = lv.color_hex(0x000000)    # Black text
COLOR_BUTTON_BG  = lv.color_hex(0xC0C0C0)   # Button background
COLOR_INACTIVE   = lv.color_hex(0xAAAAAA)   # Grayed-out tray icons (disconnected)


# ── Desktop Background ───────────────────────────────────────────────────────

def _build_desktop(scr):
    scr.set_style_bg_color(COLOR_DESKTOP, 0)
    scr.set_style_bg_opa(lv.OPA.COVER, 0)

    icon_cont = lv.obj(scr)
    icon_cont.set_size(100, _SCREEN_H - _TASKBAR_H)
    icon_cont.set_pos(0, 0)
    icon_cont.set_layout(lv.LAYOUT.FLEX)
    icon_cont.set_flex_flow(lv.FLEX_FLOW.COLUMN_WRAP)
    icon_cont.set_style_pad_all(4, 0)
    icon_cont.set_style_bg_opa(lv.OPA.TRANSP, 0)

    icon_label = lv.label(icon_cont)
    icon_label.set_text(lv.SYMBOL.DIRECTORY + "\n" + "Files")
    icon_label.set_style_text_align(lv.TEXT_ALIGN.CENTER, 0)
    icon_label.set_style_text_color(COLOR_HIGHLIGHT, 0)


# ── Taskbar (Bottom) ─────────────────────────────────────────────────────────

def _build_taskbar(scr):
    taskbar = lv.obj(scr)
    taskbar.set_size(_SCREEN_W, _TASKBAR_H)
    taskbar.set_align(lv.ALIGN.BOTTOM_MID)
    taskbar.set_layout(lv.LAYOUT.FLEX)
    taskbar.set_flex_flow(lv.FLEX_FLOW.ROW)
    taskbar.set_flex_align(lv.FLEX_ALIGN.SPACE_BETWEEN, lv.FLEX_ALIGN.CENTER, 0)
    taskbar.set_style_bg_color(COLOR_TASKBAR, 0)
    taskbar.set_style_pad_all(2, 0)
    taskbar.set_style_border_side(lv.BORDER.TOP, 0)
    taskbar.set_style_border_color(COLOR_SHADOW, 0)
    taskbar.set_style_border_width(2, 0)

    # ── Start Button (Left) ──────────────────────────────────────
    # No CHECKABLE flag — we manually control STATE.CHECKED so the button
    # stays visually pressed for exactly as long as the Start menu is open.
    start_btn = lv.button(taskbar)
    start_btn.set_size(60, 28)
    start_btn.set_style_bg_color(COLOR_BUTTON_BG, 0)
    start_btn.set_style_border_width(2, 0)
    start_btn.set_style_border_color(COLOR_HIGHLIGHT, 0)
    start_btn.set_style_border_side(lv.BORDER.LEFT | lv.BORDER.TOP, 0)
    # Pressed / checked: invert border — shadow on top-left looks depressed
    start_btn.set_style_border_color(COLOR_SHADOW, lv.PART.MAIN | lv.STATE.PRESSED)
    start_btn.set_style_border_color(COLOR_SHADOW, lv.PART.MAIN | lv.STATE.CHECKED)

    start_label = lv.label(start_btn)
    start_label.set_text(":) Start")
    start_label.set_style_text_color(COLOR_TEXT, 0)
    start_label.center()

    # ── System Tray (Right) ──────────────────────────────────────
    tray = lv.obj(taskbar)
    tray.set_layout(lv.LAYOUT.FLEX)
    tray.set_flex_flow(lv.FLEX_FLOW.ROW)
    tray.set_flex_align(lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER, 0)
    tray.set_style_bg_opa(lv.OPA.TRANSP, 0)
    tray.set_style_pad_all(2, 0)
    tray.set_size(lv.SIZE.CONTENT, 28)

    wifi_icon = lv.label(tray)
    wifi_icon.set_text(lv.SYMBOL.WIFI)
    wifi_icon.set_style_text_color(COLOR_INACTIVE, 0)

    bt_icon = lv.label(tray)
    bt_icon.set_text(lv.SYMBOL.BLUETOOTH)
    bt_icon.set_style_text_color(COLOR_TEXT, 0)

    clock_label = lv.label(tray)
    clock_label.set_text("12:34")
    clock_label.set_style_text_color(COLOR_TEXT, 0)

    return taskbar, start_btn, start_label, clock_label, wifi_icon


# ── Explorer Window (Floating) ───────────────────────────────────────────────

def _build_explorer_window(scr):
    win = lv.obj(scr)
    win.set_size(260, 340)
    win.set_align(lv.ALIGN.CENTER)
    win.set_layout(lv.LAYOUT.FLEX)
    win.set_flex_flow(lv.FLEX_FLOW.COLUMN)
    win.set_style_bg_color(COLOR_TASKBAR, 0)
    win.set_style_border_width(2, 0)
    win.set_style_border_color(COLOR_HIGHLIGHT, 0)
    win.set_style_border_side(lv.BORDER.LEFT | lv.BORDER.TOP, 0)
    win.set_style_border_color(COLOR_SHADOW, lv.PART.MAIN)
    win.set_style_border_side(lv.BORDER.RIGHT | lv.BORDER.BOTTOM, 0)
    win.set_style_pad_all(0, 0)

    # ── Title Bar ────────────────────────────────────────────────
    title_bar = lv.obj(win)
    title_bar.set_size(lv.pct(100), 20)
    title_bar.set_style_bg_color(COLOR_TITLEBAR, 0)
    title_bar.set_style_pad_all(2, 0)
    title_bar.set_layout(lv.LAYOUT.FLEX)
    title_bar.set_flex_flow(lv.FLEX_FLOW.ROW)
    title_bar.set_flex_align(lv.FLEX_ALIGN.SPACE_BETWEEN, lv.FLEX_ALIGN.CENTER, 0)

    title_text = lv.label(title_bar)
    title_text.set_text("Explorer")
    title_text.set_style_text_color(COLOR_HIGHLIGHT, 0)

    close_btn = lv.button(title_bar)
    close_btn.set_size(18, 18)
    close_btn.set_style_bg_color(COLOR_TITLEBAR, 0)
    close_btn.set_style_border_width(0, 0)
    close_btn.set_style_pad_all(0, 0)

    close_label = lv.label(close_btn)
    close_label.set_text("x")
    close_label.set_style_text_color(COLOR_HIGHLIGHT, 0)
    close_label.center()

    # ── Content Area ─────────────────────────────────────────────
    content = lv.obj(win)
    content.set_size(lv.pct(100), lv.pct(100) - 20)
    content.set_style_bg_color(lv.color_hex(0xFFFFFF), 0)
    content.set_layout(lv.LAYOUT.FLEX)
    content.set_flex_flow(lv.FLEX_FLOW.COLUMN)
    content.set_style_pad_all(4, 0)
    content.set_style_border_width(0, 0)

    for fname in ["boot.py", "main.py", "device.json"]:
        item = lv.button(content)
        item.set_size(lv.pct(100), 24)
        item.set_style_bg_color(lv.color_hex(0xFFFFFF), 0)
        item.set_style_border_width(1, 0)
        item.set_style_border_color(COLOR_SHADOW, 0)

        label = lv.label(item)
        label.set_text(lv.SYMBOL.FILE + " " + fname)
        label.set_style_text_color(COLOR_TEXT, 0)
        label.align(lv.ALIGN.LEFT_MID, 4, 0)

    return win, close_btn


# ── Start Menu ───────────────────────────────────────────────────────────────

def build_start_menu(scr, apps, friends, on_select):
    """Create the Start menu panel anchored above the Start button.

    apps    — list of app name strings (from /apps/ scan)
    friends — list of firmware filenames (from /friends/ scan)
    on_select(kind, name) — called when an item is clicked

    Returns the panel object. Caller calls panel.delete() to close it.
    """
    panel = lv.obj(scr)
    panel.set_size(200, lv.SIZE.CONTENT)
    panel.align(lv.ALIGN.BOTTOM_LEFT, 0, -_TASKBAR_H)   # sit flush above taskbar
    panel.set_layout(lv.LAYOUT.FLEX)
    panel.set_flex_flow(lv.FLEX_FLOW.COLUMN)
    panel.set_style_bg_color(COLOR_BUTTON_BG, 0)
    panel.set_style_border_width(2, 0)
    panel.set_style_border_color(COLOR_HIGHLIGHT, 0)
    panel.set_style_border_side(lv.BORDER.LEFT | lv.BORDER.TOP, 0)
    panel.set_style_pad_all(4, 0)
    panel.set_style_pad_row(2, 0)

    def _section_hdr(text):
        lbl = lv.label(panel)
        lbl.set_text(text)
        lbl.set_width(lv.pct(100))
        lbl.set_style_text_color(COLOR_SHADOW, 0)

    def _item(icon, text, kind, name):
        btn = lv.button(panel)
        btn.set_size(lv.pct(100), 26)
        btn.set_style_bg_color(COLOR_BUTTON_BG, 0)
        btn.set_style_border_width(0, 0)
        btn.set_style_pad_all(2, 0)
        lbl = lv.label(btn)
        lbl.set_text(icon + " " + text)
        lbl.set_style_text_color(COLOR_TEXT, 0)
        lbl.align(lv.ALIGN.LEFT_MID, 4, 0)
        # Capture loop values via default args
        btn.add_event_cb(
            lambda e, k=kind, n=name: on_select(k, n),
            lv.EVENT.CLICKED, None
        )

    if apps:
        _section_hdr("Apps")
        for app in apps:
            _item(lv.SYMBOL.DIRECTORY, app, 'app', app)

    if friends:
        _section_hdr("Friends")
        for fw in friends:
            display_name = fw.rsplit('.', 1)[0]
            _item(lv.SYMBOL.DOWNLOAD, display_name, 'friend', fw)

    if not apps and not friends:
        lbl = lv.label(panel)
        lbl.set_text("No apps installed.\n\nDrop .app bundles\ninto /apps/")
        lbl.set_width(lv.pct(100))
        lbl.set_style_text_color(COLOR_SHADOW, 0)

    return panel


# ── Update helpers ───────────────────────────────────────────────────────────

def update_clock(ui_elements, hour, minute):
    clock = ui_elements.get('clock_label')
    if clock:
        clock.set_text("{:02d}:{:02d}".format(hour, minute))


def update_wifi(ui_elements, connected):
    icon = ui_elements.get('wifi_icon')
    if icon:
        icon.set_style_text_color(
            COLOR_TEXT if connected else COLOR_INACTIVE, 0
        )


# ── Main Build Function ──────────────────────────────────────────────────────

def build_desktop(scr):
    """Build the complete Windows CE inspired desktop layout."""
    _build_desktop(scr)
    taskbar, start_btn, start_label, clock_label, wifi_icon = _build_taskbar(scr)
    win, close_btn = _build_explorer_window(scr)

    return {
        'taskbar':     taskbar,
        'start_btn':   start_btn,
        'start_label': start_label,
        'clock_label': clock_label,
        'wifi_icon':   wifi_icon,
        'window':      win,
        'close_btn':   close_btn,
    }
