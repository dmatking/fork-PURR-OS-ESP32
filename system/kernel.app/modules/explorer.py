import uasyncio as asyncio
import utime

_BEAT_INTERVAL = 2000
_REFRESH_MS    = 1000
_TASKBAR_H     = 32

# Classic Windows CE 2.x silver palette
_DESKTOP  = 0x0410  # WIN_TEAL desktop
_TASKBAR  = 0xC618  # LIGHT_GRAY silver taskbar
_BTN_FACE = 0xC618  # LIGHT_GRAY button face
_HI       = 0xFFFF  # WHITE   bevel top/left highlight
_SH       = 0x4208  # DARK_GRAY bevel bottom/right shadow
_START    = 0x0410  # WIN_TEAL Start button
_SEL      = 0x000F  # WIN_BLUE selected item
_FG_DARK  = 0x0000  # BLACK text on silver
_FG_LIGHT = 0xFFFF  # WHITE text on dark backgrounds


def _raised(ops, x, y, w, h, face):
    """Draw a raised 3D button rectangle."""
    ops.append({'cmd': 'fill_rect', 'x': x+1, 'y': y+1, 'w': w-2, 'h': h-2, 'color': face})
    ops.append({'cmd': 'hline', 'x': x,     'y': y,     'w': w,   'color': _HI})
    ops.append({'cmd': 'vline', 'x': x,     'y': y,     'h': h,   'color': _HI})
    ops.append({'cmd': 'hline', 'x': x,     'y': y+h-1, 'w': w,   'color': _SH})
    ops.append({'cmd': 'vline', 'x': x+w-1, 'y': y,     'h': h,   'color': _SH})


def _pressed(ops, x, y, w, h, face):
    """Draw a pressed (sunken) 3D button rectangle."""
    ops.append({'cmd': 'fill_rect', 'x': x+1, 'y': y+1, 'w': w-2, 'h': h-2, 'color': face})
    ops.append({'cmd': 'hline', 'x': x,     'y': y,     'w': w,   'color': _SH})
    ops.append({'cmd': 'vline', 'x': x,     'y': y,     'h': h,   'color': _SH})
    ops.append({'cmd': 'hline', 'x': x,     'y': y+h-1, 'w': w,   'color': _HI})
    ops.append({'cmd': 'vline', 'x': x+w-1, 'y': y,     'h': h,   'color': _HI})


class ExplorerModule:
    NAME = 'explorer'

    def __init__(self, core):
        self._core  = core
        self._tray  = core.subscribe('explorer.tray')
        self._keys  = core.subscribe('input.key')
        self._wifi  = False
        self._menu_open = False
        self._menu_sel  = 0
        self._menu_items = ['Apps', 'Settings', 'Shutdown']
        self._need_desktop = True
        self._w     = 320
        self._h     = 480
        self._scale = 2
        self._load_cfg()

    def _load_cfg(self):
        try:
            import json
            with open('/system/kernel.app/device.json') as f:
                cfg = json.load(f)
            res = cfg.get('display_res', [128, 64])
            self._w, self._h = res[0], res[1]
            self._scale = 2 if self._w >= 200 else 1
        except Exception:
            pass

    async def run(self):
        beat_task = asyncio.create_task(self._heartbeat())
        tray_task = asyncio.create_task(self._tray_loop())
        key_task = asyncio.create_task(self._key_loop())
        try:
            await self._clock_loop()
        finally:
            beat_task.cancel()
            tray_task.cancel()
            key_task.cancel()

    # ------------------------------------------------------------------

    async def _clock_loop(self):
        while True:
            self._draw()
            await asyncio.sleep_ms(_REFRESH_MS)

    async def _tray_loop(self):
        while True:
            msg = await self._tray.get()
            if msg.get('wifi') is not None:
                self._wifi = bool(msg['wifi'])
                self._draw()

    async def _key_loop(self):
        while True:
            msg = await self._keys.get()
            if msg.get('event') != 'press':
                continue
            key = msg.get('key')
            if key == 'SELECT':
                self._menu_open = not self._menu_open
                self._menu_sel = 0
                self._need_desktop = True
                self._draw()
            elif self._menu_open:
                if key == 'UP':
                    self._menu_sel = (self._menu_sel - 1) % len(self._menu_items)
                    self._draw()
                elif key == 'DOWN':
                    self._menu_sel = (self._menu_sel + 1) % len(self._menu_items)
                    self._draw()
                elif key == 'BACK':
                    self._menu_open = False
                    self._need_desktop = True
                    self._draw()

    # ------------------------------------------------------------------

    def _draw(self):
        ops = []
        w   = self._w
        h   = self._h
        sc  = self._scale
        cw  = 8 * sc
        ch  = 8 * sc
        y_bar   = h - _TASKBAR_H
        btn_y   = y_bar + 3
        btn_h   = _TASKBAR_H - 6
        ty      = y_bar + (_TASKBAR_H - ch) // 2

        # Desktop fill (only when needed)
        if self._need_desktop:
            ops.append({'cmd': 'fill_rect', 'x': 0, 'y': 0,
                        'w': w, 'h': y_bar, 'color': _DESKTOP})
            self._need_desktop = False

        # Start menu overlay
        if self._menu_open:
            self._menu_ops(ops, cw, ch, y_bar)

        # Taskbar
        self._taskbar_ops(ops, w, sc, cw, ch, y_bar, btn_y, btn_h, ty)

        ops.append({'cmd': 'show'})
        self._core.publish('display', {'type': 'raw', 'ops': ops})

    # ------------------------------------------------------------------
    # Taskbar
    # ------------------------------------------------------------------

    def _taskbar_ops(self, ops, w, sc, cw, ch, y_bar, btn_y, btn_h, ty):
        # Silver background + top highlight
        ops.append({'cmd': 'fill_rect', 'x': 0, 'y': y_bar, 'w': w, 'h': _TASKBAR_H, 'color': _TASKBAR})
        ops.append({'cmd': 'hline',     'x': 0, 'y': y_bar, 'w': w, 'color': _HI})

        # --- Start button ---
        btn_w = 5*cw + 8
        txt_x = 2 + (btn_w - 5*cw) // 2
        if self._menu_open:
            _pressed(ops, 2, btn_y, btn_w, btn_h, _START)
            ops.append({'cmd': 'text', 's': 'Start', 'x': txt_x+1, 'y': ty+1,
                        'color': _FG_LIGHT, 'bg': _START})
        else:
            _raised(ops, 2, btn_y, btn_w, btn_h, _START)
            ops.append({'cmd': 'text', 's': 'Start', 'x': txt_x, 'y': ty,
                        'color': _FG_LIGHT, 'bg': _START})

        # Double-groove separator after Start
        sep1 = 2 + btn_w + 3
        ops.append({'cmd': 'vline', 'x': sep1,   'y': btn_y, 'h': btn_h, 'color': _SH})
        ops.append({'cmd': 'vline', 'x': sep1+1, 'y': btn_y, 'h': btn_h, 'color': _HI})

        # --- System tray (right-to-left) ---
        s   = utime.ticks_ms() // 1000
        clk = '{:02d}:{:02d}'.format((s // 60) % 60, s % 60)
        wifi_s = 'W+' if self._wifi else 'W-'

        clk_x  = w - 5*cw - 2
        wifi_x = clk_x - 2*cw - 6

        # Double-groove separator before tray
        tray_sep = wifi_x - 5
        ops.append({'cmd': 'vline', 'x': tray_sep,   'y': btn_y, 'h': btn_h, 'color': _SH})
        ops.append({'cmd': 'vline', 'x': tray_sep+1, 'y': btn_y, 'h': btn_h, 'color': _HI})

        # Tray contents
        ops.append({'cmd': 'text', 's': wifi_s, 'x': wifi_x, 'y': ty,
                    'color': _FG_DARK, 'bg': _TASKBAR})
        ops.append({'cmd': 'text', 's': clk,    'x': clk_x,  'y': ty,
                    'color': _FG_DARK, 'bg': _TASKBAR})

    def _menu_ops(self, ops, cw, ch, y_bar):
        menu_w   = 10*cw + 8
        item_h   = ch + 4
        header_h = ch + 6
        n        = len(self._menu_items)
        menu_h   = header_h + n*item_h + 4
        menu_y   = y_bar - menu_h

        # Outer raised panel
        _raised(ops, 0, menu_y, menu_w, menu_h, _BTN_FACE)

        # Title bar (teal strip)
        ops.append({'cmd': 'fill_rect', 'x': 1, 'y': menu_y+1,
                    'w': menu_w-2, 'h': header_h, 'color': _START})
        ops.append({'cmd': 'text', 's': 'PURR  OS', 'x': 8, 'y': menu_y+3,
                    'color': _FG_LIGHT, 'bg': _START})

        # Divider between title and items
        ops.append({'cmd': 'hline', 'x': 1, 'y': menu_y+header_h,
                    'w': menu_w-2, 'color': _SH})

        # Menu items
        items_top = menu_y + header_h + 2
        for i, item in enumerate(self._menu_items):
            iy   = items_top + i*item_h
            face = _SEL if i == self._menu_sel else _BTN_FACE
            fg   = _FG_LIGHT if i == self._menu_sel else _FG_DARK
            ops.append({'cmd': 'fill_rect', 'x': 2, 'y': iy,
                        'w': menu_w-4, 'h': item_h, 'color': face})
            ops.append({'cmd': 'text', 's': item, 'x': 8, 'y': iy+2,
                        'color': fg, 'bg': face})

    # ------------------------------------------------------------------

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
