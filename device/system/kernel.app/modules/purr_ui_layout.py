"""
PURR UI layout for PURR OS.
Grid launcher: status bar + 2x2 draggable app tiles + function key bar.

Screen: 320x480  (Ingenico Move 5000 / CattoPad portrait)
  Status bar : y=0,   h=24
  Main area  : y=24,  h=424
  Fn bar     : y=448, h=32
"""

try:
    import lvgl as lv
except ImportError:
    raise ImportError("LVGL library not found.")

# ── Palette ──────────────────────────────────────────────────────────────────

COLOR_BG         = lv.color_hex(0xEEEEEE)
COLOR_STATUS_BG  = lv.color_hex(0x1C1C2E)
COLOR_STATUS_TXT = lv.color_hex(0xFFFFFF)
COLOR_FN_BG      = lv.color_hex(0x2C2C2C)
COLOR_FN_BTN     = lv.color_hex(0x404040)
COLOR_FN_TXT     = lv.color_hex(0xFFFFFF)
COLOR_FN_BORDER  = lv.color_hex(0x666666)
COLOR_LABEL      = lv.color_hex(0x222222)
COLOR_INACTIVE   = lv.color_hex(0x888888)
COLOR_WHITE      = lv.color_hex(0xFFFFFF)
COLOR_EMPTY_TILE = lv.color_hex(0xAAAAAA)

# Tile background colors per slot position
TILE_COLORS = [
    lv.color_hex(0x0055BB),   # slot 0 — blue
    lv.color_hex(0x009944),   # slot 1 — green
    lv.color_hex(0xBB5500),   # slot 2 — orange
    lv.color_hex(0x991133),   # slot 3 — crimson
]

# Generic placeholder symbols per slot position
TILE_SYMBOLS = [
    lv.SYMBOL.PLUS,      # slot 0 — placeholder
    lv.SYMBOL.SETTINGS,  # slot 1
    lv.SYMBOL.FILE,      # slot 2 — placeholder
    lv.SYMBOL.WARNING,   # slot 3
]

# ── Geometry constants ───────────────────────────────────────────────────────

_SCREEN_W = 320
_SCREEN_H = 480
_STATUS_H = 24
_FN_H     = 32
_MAIN_H   = _SCREEN_H - _STATUS_H - _FN_H   # 424

_TILE_W   = 130
_TILE_H   = 170

# (x, y) within the main area container
# Two columns × two rows, evenly spaced in 320×424
SLOT_POSITIONS = [
    (20,  32),    # slot 0: top-left
    (170, 32),    # slot 1: top-right
    (20,  224),   # slot 2: bottom-left
    (170, 224),   # slot 3: bottom-right
]


# ── Status bar ───────────────────────────────────────────────────────────────

def _build_status_bar(scr):
    bar = lv.obj(scr)
    bar.set_size(_SCREEN_W, _STATUS_H)
    bar.set_pos(0, 0)
    bar.set_layout(lv.LAYOUT.FLEX)
    bar.set_flex_flow(lv.FLEX_FLOW.ROW)
    bar.set_flex_align(lv.FLEX_ALIGN.SPACE_BETWEEN, lv.FLEX_ALIGN.CENTER, 0)
    bar.set_style_bg_color(COLOR_STATUS_BG, 0)
    bar.set_style_bg_opa(lv.OPA.COVER, 0)
    bar.set_style_border_width(0, 0)
    bar.set_style_radius(0, 0)
    bar.set_style_pad_hor(6, 0)
    bar.set_style_pad_ver(0, 0)

    # Left: battery + network type
    left = lv.obj(bar)
    left.set_size(lv.SIZE.CONTENT, _STATUS_H)
    left.set_layout(lv.LAYOUT.FLEX)
    left.set_flex_flow(lv.FLEX_FLOW.ROW)
    left.set_flex_align(lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.CENTER, 0)
    left.set_style_bg_opa(lv.OPA.TRANSP, 0)
    left.set_style_border_width(0, 0)
    left.set_style_pad_all(0, 0)
    left.set_style_pad_column(4, 0)

    batt = lv.label(left)
    batt.set_text(lv.SYMBOL.BATTERY_FULL)
    batt.set_style_text_color(COLOR_STATUS_TXT, 0)

    net = lv.label(left)
    net.set_text("4G")
    net.set_style_text_color(COLOR_STATUS_TXT, 0)

    # Right: wifi + clock
    right = lv.obj(bar)
    right.set_size(lv.SIZE.CONTENT, _STATUS_H)
    right.set_layout(lv.LAYOUT.FLEX)
    right.set_flex_flow(lv.FLEX_FLOW.ROW)
    right.set_flex_align(lv.FLEX_ALIGN.END, lv.FLEX_ALIGN.CENTER, 0)
    right.set_style_bg_opa(lv.OPA.TRANSP, 0)
    right.set_style_border_width(0, 0)
    right.set_style_pad_all(0, 0)
    right.set_style_pad_column(6, 0)

    wifi = lv.label(right)
    wifi.set_text(lv.SYMBOL.WIFI)
    wifi.set_style_text_color(COLOR_INACTIVE, 0)

    clock = lv.label(right)
    clock.set_text("12:00")
    clock.set_style_text_color(COLOR_STATUS_TXT, 0)

    return {'bar': bar, 'batt': batt, 'net': net, 'wifi': wifi, 'clock': clock}


# ── Function key bar ─────────────────────────────────────────────────────────

def _build_fn_bar(scr):
    bar = lv.obj(scr)
    bar.set_size(_SCREEN_W, _FN_H)
    bar.set_align(lv.ALIGN.BOTTOM_MID)
    bar.set_layout(lv.LAYOUT.FLEX)
    bar.set_flex_flow(lv.FLEX_FLOW.ROW)
    bar.set_flex_align(lv.FLEX_ALIGN.SPACE_EVENLY, lv.FLEX_ALIGN.CENTER, 0)
    bar.set_style_bg_color(COLOR_FN_BG, 0)
    bar.set_style_bg_opa(lv.OPA.COVER, 0)
    bar.set_style_radius(0, 0)
    bar.set_style_border_width(1, 0)
    bar.set_style_border_side(lv.BORDER.TOP, 0)
    bar.set_style_border_color(COLOR_FN_BORDER, 0)
    bar.set_style_pad_all(2, 0)

    fn_btns = []
    for text in ("F1", "F2", "F3"):
        btn = lv.button(bar)
        btn.set_size(90, 26)
        btn.set_style_bg_color(COLOR_FN_BTN, 0)
        btn.set_style_border_color(COLOR_FN_BORDER, 0)
        btn.set_style_border_width(1, 0)
        btn.set_style_radius(2, 0)
        btn.set_style_pad_all(0, 0)

        lbl = lv.label(btn)
        lbl.set_text(text)
        lbl.set_style_text_color(COLOR_FN_TXT, 0)
        lbl.center()

        fn_btns.append(btn)

    return bar, fn_btns


# ── App tile ─────────────────────────────────────────────────────────────────

def _build_tile(parent, x, y, app_name, slot_idx):
    """Build one tile at absolute position (x, y) within parent.

    Returns a slot dict with all widget refs for later updates and drag wiring.
    """
    cell = lv.obj(parent)
    cell.set_size(_TILE_W, _TILE_H)
    cell.set_pos(x, y)
    cell.set_layout(lv.LAYOUT.FLEX)
    cell.set_flex_flow(lv.FLEX_FLOW.COLUMN)
    cell.set_flex_align(lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER, 0)
    cell.set_style_bg_opa(lv.OPA.TRANSP, 0)
    cell.set_style_border_width(0, 0)
    cell.set_style_pad_all(0, 0)
    cell.set_style_pad_row(4, 0)
    cell.add_flag(lv.OBJ_FLAG.DRAGGABLE)

    has_app = app_name is not None

    btn = lv.button(cell)
    btn.set_size(110, 120)
    btn.set_style_bg_color(TILE_COLORS[slot_idx] if has_app else COLOR_EMPTY_TILE, 0)
    btn.set_style_border_width(0, 0)
    btn.set_style_radius(10, 0)
    btn.set_style_pad_all(0, 0)

    icon_lbl = lv.label(btn)
    icon_lbl.set_text(TILE_SYMBOLS[slot_idx] if has_app else lv.SYMBOL.PLUS)
    icon_lbl.set_style_text_color(COLOR_WHITE, 0)
    # Use a larger font for the icon if available
    try:
        icon_lbl.set_style_text_font(lv.font_montserrat_28, 0)
    except Exception:
        pass
    icon_lbl.center()

    name_lbl = lv.label(cell)
    name_lbl.set_text(app_name if has_app else "")
    name_lbl.set_style_text_color(COLOR_LABEL, 0)
    name_lbl.set_style_text_align(lv.TEXT_ALIGN.CENTER, 0)
    name_lbl.set_width(lv.pct(100))
    name_lbl.set_long_mode(lv.label.LONG.DOT)

    return {
        'idx':      slot_idx,
        'x':        x,
        'y':        y,
        'cell':     cell,
        'btn':      btn,
        'icon_lbl': icon_lbl,
        'name_lbl': name_lbl,
        'app':      app_name,
    }


# ── App grid ─────────────────────────────────────────────────────────────────

def _build_grid(scr, apps):
    """Build main area with 4 absolute-positioned tile slots.

    Always creates all 4 slots; unfilled slots show as empty.
    Returns (main_container, slots_list).
    """
    main = lv.obj(scr)
    main.set_size(_SCREEN_W, _MAIN_H)
    main.set_pos(0, _STATUS_H)
    main.set_style_bg_color(COLOR_BG, 0)
    main.set_style_bg_opa(lv.OPA.COVER, 0)
    main.set_style_border_width(0, 0)
    main.set_style_radius(0, 0)
    main.set_style_pad_all(0, 0)

    slots = []
    for idx, (sx, sy) in enumerate(SLOT_POSITIONS):
        app_name = apps[idx] if idx < len(apps) else None
        slot = _build_tile(main, sx, sy, app_name, idx)
        slots.append(slot)

    return main, slots


# ── Main entry ───────────────────────────────────────────────────────────────

def build_purr_ui(scr, apps):
    """Build the complete PURR UI.

    Returns:
        ui_elements : dict  — clock/wifi/fn_btns refs
        slots       : list  — slot dicts for drag wiring and launch callbacks
    """
    scr.set_style_bg_color(COLOR_BG, 0)
    scr.set_style_bg_opa(lv.OPA.COVER, 0)

    status      = _build_status_bar(scr)
    _, fn_btns  = _build_fn_bar(scr)
    _, slots    = _build_grid(scr, apps)

    ui = {
        'clock_label': status['clock'],
        'wifi_icon':   status['wifi'],
        'batt_icon':   status['batt'],
        'net_label':   status['net'],
        'fn_btns':     fn_btns,
    }
    return ui, slots


# ── Update helpers ───────────────────────────────────────────────────────────

def update_clock(ui, hour, minute):
    clock = ui.get('clock_label')
    if clock:
        clock.set_text("{:02d}:{:02d}".format(hour, minute))


def update_wifi(ui, connected):
    icon = ui.get('wifi_icon')
    if icon:
        icon.set_style_text_color(
            COLOR_STATUS_TXT if connected else COLOR_INACTIVE, 0
        )
