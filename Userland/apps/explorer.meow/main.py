"""
PURR OS — Windows CE / Win95 desktop shell (explorer1.app).
Rename to explorer.app to activate.

Keys:
  SELECT/DOWN/UP — cycle desktop icons; second SELECT launches
  BACK           — open Start menu
  ENTER          — launch immediately

File browsing: launch Finder from the Start menu or desktop icon.
"Quit PURR OS" → machine.reset()
"""

import uasyncio as asyncio
import os
import utime

_BEAT_INTERVAL = 2000
_REFRESH_MS    = 100

# Windows 95/CE palette (RGB565)
_FACE        = 0xC618
_SHADOW      = 0x8410
_HIGHLIGHT   = 0xFFFF
_DARK_SHADOW = 0x0000
_TITLEBAR    = 0x000080
_TITLETEXT   = 0xFFFF
_TEXT        = 0x0000
_DESKTOP     = 0x008080
_SEL_BG      = 0x000080
_SEL_FG      = 0xFFFF


class Module:
    NAME = 'win95'

    def __init__(self, core):
        self._core = core
        self._keys = core.subscribe('input.key')

        self._w = 320
        self._h = 240

        self._menu_open      = False
        self._menu_selection = 0
        self._menu_items     = []
        self._start_pressed  = False

        self._desk_cursor  = 0
        self._desk_confirm = False

        self._load_cfg()
        self._calc_layout()

    def _load_cfg(self):
        try:
            import json
            with open('/system/kernel.app/device.json') as f:
                cfg = json.load(f)
            res = cfg.get('display_res', [128, 64])
            self._w, self._h = res[0], res[1]
        except Exception:
            pass

    def _calc_layout(self):
        self._taskbar_h = max(20, int(self._h * 0.09))
        self._start_w   = max(72, int(self._taskbar_h * 2.0))
        self._start_h   = self._taskbar_h - 4
        self._menu_w    = max(140, int(self._w * 0.40))
        self._title_h   = 20

    # ── App scanning ──────────────────────────────────────────────────────────

    def _scan_apps(self):
        apps = []
        try:
            for entry in sorted(os.listdir('/apps')):
                if not entry.endswith('.app'):
                    continue
                name = entry[:-4]
                if name in ('explorer', 'explorer1'):
                    continue
                try:
                    os.stat('/apps/{}/main.py'.format(entry))
                    apps.append(name)
                except OSError:
                    pass
        except OSError:
            pass
        return apps

    # ── Lifecycle ─────────────────────────────────────────────────────────────

    async def run(self):
        beat_task = asyncio.create_task(self._heartbeat())
        key_task  = asyncio.create_task(self._key_loop())
        try:
            await self._draw_loop()
        finally:
            beat_task.cancel()
            key_task.cancel()

    async def _key_loop(self):
        while True:
            msg   = await self._keys.get()
            key   = msg.get('key')
            event = msg.get('event')
            if not (key and event == 'press'):
                continue
            if self._menu_open:
                self._handle_key_menu(key)
            else:
                self._handle_key_desktop(key)

    # ── Key handlers ─────────────────────────────────────────────────────────

    def _handle_key_desktop(self, key):
        apps = self._scan_apps()
        n    = len(apps)
        if key in ('SELECT', 'DOWN'):
            if self._desk_confirm and key == 'SELECT':
                self._desk_confirm = False
                self._launch(apps[self._desk_cursor] if apps else None)
            else:
                if n:
                    self._desk_cursor = (self._desk_cursor + 1) % n
                self._desk_confirm = True
        elif key == 'UP':
            if n:
                self._desk_cursor = (self._desk_cursor - 1) % n
            self._desk_confirm = True
        elif key == 'ENTER':
            self._desk_confirm = False
            self._launch(apps[self._desk_cursor] if apps else None)
        elif key == 'BACK':
            menu_apps            = self._scan_apps()
            self._menu_items     = menu_apps + ['---', 'Quit PURR OS']
            self._menu_open      = True
            self._menu_selection = 0
            self._start_pressed  = True

    def _handle_key_menu(self, key):
        display = [i for i in self._menu_items if i != '---']
        n = len(display)
        if key == 'SELECT':
            selected            = display[self._menu_selection] if display else None
            self._menu_open     = False
            self._start_pressed = False
            if selected == 'Quit PURR OS':
                try:
                    import machine
                    machine.reset()
                except Exception:
                    pass
            else:
                self._launch(selected)
        elif key == 'DOWN':
            self._menu_selection = (self._menu_selection + 1) % n
        elif key == 'UP':
            self._menu_selection = (self._menu_selection - 1) % n
        elif key == 'BACK':
            self._menu_open     = False
            self._start_pressed = False

    def _launch(self, name):
        if name:
            self._core.publish('system.app.launch', {'app': name})

    # ── Draw loop ─────────────────────────────────────────────────────────────

    async def _draw_loop(self):
        while True:
            self._draw_desktop()
            await asyncio.sleep_ms(_REFRESH_MS)

    def _draw_desktop(self):
        ops       = []
        taskbar_y = self._h - self._taskbar_h

        ops.append({'cmd': 'fill', 'color': _DESKTOP})

        apps   = self._scan_apps()
        icon_x = 8
        icon_y = 4
        for i, name in enumerate(apps):
            iy     = icon_y + i * 18
            is_sel = (i == self._desk_cursor)
            if is_sel:
                ops.append({'cmd': 'fill_rect', 'x': icon_x - 2, 'y': iy,
                            'w': min(self._w - 12, len(name) * 8 + 20), 'h': 14,
                            'color': _SEL_BG})
                fg = _SEL_FG
            else:
                fg = _HIGHLIGHT
            ops.append({'cmd': 'text', 's': '[+] ' + name,
                        'x': icon_x, 'y': iy + 1, 'color': fg})

        if self._menu_open:
            display_items = [i for i in self._menu_items if i != '---']
            n_items = max(1, len(display_items))
            menu_x  = 4
            menu_h  = 14 + n_items * 16
            menu_y  = taskbar_y - menu_h

            ops.append({'cmd': 'fill_rect', 'x': menu_x, 'y': menu_y,
                        'w': self._menu_w, 'h': menu_h, 'color': _FACE})
            ops.append({'cmd': 'hline', 'x': menu_x, 'y': menu_y,
                        'w': self._menu_w, 'color': _HIGHLIGHT})
            ops.append({'cmd': 'vline', 'x': menu_x, 'y': menu_y,
                        'h': menu_h, 'color': _HIGHLIGHT})
            ops.append({'cmd': 'hline', 'x': menu_x, 'y': menu_y + menu_h - 1,
                        'w': self._menu_w, 'color': _DARK_SHADOW})
            ops.append({'cmd': 'vline', 'x': menu_x + self._menu_w - 1, 'y': menu_y,
                        'h': menu_h, 'color': _DARK_SHADOW})
            ops.append({'cmd': 'fill_rect', 'x': menu_x + 2, 'y': menu_y + 2,
                        'w': self._menu_w - 4, 'h': 10, 'color': _TITLEBAR})
            ops.append({'cmd': 'text', 's': 'PURR OS',
                        'x': menu_x + 6, 'y': menu_y + 3, 'color': _TITLETEXT})

            for i, item in enumerate(display_items):
                iy = menu_y + 14 + i * 16
                if i == self._menu_selection:
                    ops.append({'cmd': 'fill_rect', 'x': menu_x + 2, 'y': iy,
                                'w': self._menu_w - 4, 'h': 14, 'color': _SEL_BG})
                    ops.append({'cmd': 'text', 's': item,
                                'x': menu_x + 8, 'y': iy + 3, 'color': _SEL_FG})
                else:
                    ops.append({'cmd': 'text', 's': item,
                                'x': menu_x + 8, 'y': iy + 3, 'color': _TEXT})

        # Taskbar
        ops.append({'cmd': 'fill_rect', 'x': 0, 'y': taskbar_y,
                    'w': self._w, 'h': self._taskbar_h, 'color': _FACE})
        ops.append({'cmd': 'hline', 'x': 0, 'y': taskbar_y,
                    'w': self._w, 'color': _HIGHLIGHT})
        ops.append({'cmd': 'hline', 'x': 0, 'y': taskbar_y + 1,
                    'w': self._w, 'color': _HIGHLIGHT})
        ops.append({'cmd': 'hline', 'x': 0, 'y': taskbar_y + self._taskbar_h - 1,
                    'w': self._w, 'color': _DARK_SHADOW})

        start_x = 3
        start_y = taskbar_y + 2
        fill    = _TITLEBAR if self._start_pressed else None
        tc      = _TITLETEXT if self._start_pressed else _TEXT
        label   = ':0 Start' if self._menu_open else ':) Start'
        self._draw_raised_button(ops, start_x, start_y,
                                 self._start_w, self._start_h,
                                 self._start_pressed, fill=fill)
        ops.append({'cmd': 'text', 's': label,
                    'x': start_x + 4, 'y': start_y + 2, 'color': tc})

        tray_x = self._w - 40
        tray_y = taskbar_y + 3
        try:
            now = utime.localtime()
            clk = '{:02d}:{:02d}'.format(now[3], now[4])
        except Exception:
            clk = '--:--'
        ops.append({'cmd': 'text', 's': clk, 'x': tray_x, 'y': tray_y, 'color': _TEXT})

        ops.append({'cmd': 'show'})
        self._core.publish('display', {'type': 'raw', 'ops': ops})

    def _draw_raised_button(self, ops, x, y, w, h, pressed=False, fill=None):
        bg = fill if fill is not None else _FACE
        ops.append({'cmd': 'fill_rect', 'x': x, 'y': y, 'w': w, 'h': h, 'color': bg})
        tl = _SHADOW if pressed else _HIGHLIGHT
        br = _HIGHLIGHT if pressed else _SHADOW
        ops.append({'cmd': 'hline', 'x': x,     'y': y,     'w': w, 'color': tl})
        ops.append({'cmd': 'vline', 'x': x,     'y': y,     'h': h, 'color': tl})
        ops.append({'cmd': 'hline', 'x': x,     'y': y+h-1, 'w': w, 'color': br})
        ops.append({'cmd': 'vline', 'x': x+w-1, 'y': y,     'h': h, 'color': br})

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
