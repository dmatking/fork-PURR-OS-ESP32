"""
smol — minimal text-mode shell for 128×64 displays (Heltec / SSD1306).

Row layout (8 rows × 16 chars):
  0  PURR OS
  1  ──────────────
  2  > app name
  3    app name
  4    app name
  5    app name
  6    app name
  7  SEL BCK:menu

Keys:
  SELECT / DOWN  — move cursor; second SELECT launches
  UP             — move cursor up
  BACK           — open PURR menu
"""

import uasyncio as asyncio
import os
import utime

_BEAT_INTERVAL  = 2000
_REFRESH_ACTIVE = 150    # ms while recently active
_REFRESH_IDLE   = 3000   # ms after 3s of no input
_IDLE_AFTER_MS  = 3000

_LIST_ROWS = 5   # rows 2–6

# Apps that require a large display — hidden on small screens
_SMALL_BLACKLIST = {
    'explorer1',   # Win95 CE — gfx only
    'classicmac',  # Mac UI — gfx only
    'ClassicMac',
}


class Module:
    NAME = 'smol'

    def __init__(self, core):
        self._core      = core
        self._keys      = core.subscribe('input.key')
        self._stopped_q = core.subscribe('system.app.stopped')

        self._apps    = []
        self._cursor  = 0
        self._scroll  = 0
        self._confirm = False

        self._menu_open = False
        self._menu_sel  = 0

        self._child = None   # name of app we launched; None = we own the display
        self._PURR_ITEMS = ['About', 'System Info', '---', 'Quit PURR OS']

        self._about_open   = False
        self._sysinfo_open = False

        self._device_name = 'purr_os'
        self._flash       = '?'
        self._w = 128
        self._h = 64

        self._last_input = utime.ticks_ms()

        self._load_cfg()
        self._rescan()

    def _load_cfg(self):
        try:
            import json
            with open('/system/kernel.app/device.json') as f:
                cfg = json.load(f)
            res = cfg.get('display_res', [128, 64])
            self._w, self._h  = res[0], res[1]
            self._device_name = cfg.get('device', 'purr_os')
            self._flash       = cfg.get('flash', '?')
        except Exception:
            pass

    def _rescan(self):
        apps = []
        try:
            for entry in sorted(os.listdir('/apps')):
                if not entry.endswith('.app'):
                    continue
                name = entry[:-4]
                if name in ('explorer', 'smol') or name in _SMALL_BLACKLIST:
                    continue
                try:
                    os.stat('/apps/{}/main.py'.format(entry))
                    apps.append(name)
                except OSError:
                    pass
        except OSError:
            pass
        self._apps = apps
        self._cursor = min(self._cursor, max(0, len(apps) - 1))

    # ── Lifecycle ─────────────────────────────────────────────────────────────

    async def run(self):
        beat_task  = asyncio.create_task(self._heartbeat())
        key_task   = asyncio.create_task(self._key_loop())
        child_task = asyncio.create_task(self._child_watch())
        try:
            await self._draw_loop()
        finally:
            beat_task.cancel()
            key_task.cancel()
            child_task.cancel()

    async def _child_watch(self):
        while True:
            msg = await self._stopped_q.get()
            if msg.get('name') == self._child:
                self._child = None   # child exited — reclaim display
                self._last_input = utime.ticks_ms()

    async def _key_loop(self):
        while True:
            msg   = await self._keys.get()
            if self._child:
                continue   # child owns the display and keys
            key   = msg.get('key')
            event = msg.get('event')
            if not (key and event == 'press'):
                continue
            if self._about_open or self._sysinfo_open:
                self._about_open   = False
                self._sysinfo_open = False
            elif self._menu_open:
                self._handle_menu(key)
            else:
                self._handle_desktop(key)
            self._last_input = utime.ticks_ms()

    def _handle_desktop(self, key):
        n = len(self._apps)
        if key == 'SELECT':
            if self._confirm:
                self._confirm = False
                self._launch(self._apps[self._cursor] if self._apps else None)
            else:
                if n:
                    self._cursor = (self._cursor + 1) % n
                self._confirm = True
        elif key == 'DOWN':
            if n:
                self._cursor = (self._cursor + 1) % n
            self._confirm = True
        elif key == 'UP':
            if n:
                self._cursor = (self._cursor - 1) % n
            self._confirm = True
        elif key == 'BACK':
            items = self._PURR_ITEMS
            self._menu_sel  = next(
                (i for i, x in enumerate(items) if not x.startswith('---')), 0
            )
            self._menu_open = True

    def _handle_menu(self, key):
        visible = [x for x in self._PURR_ITEMS if not x.startswith('---')]
        n = len(visible)
        if key == 'DOWN':
            self._menu_sel = (self._menu_sel + 1) % n
        elif key == 'UP':
            self._menu_sel = (self._menu_sel - 1) % n
        elif key == 'SELECT':
            item            = visible[self._menu_sel] if visible else None
            self._menu_open = False
            self._menu_sel  = 0
            if item == 'Quit PURR OS':
                try:
                    import machine
                    machine.reset()
                except Exception:
                    pass
            elif item == 'About':
                self._about_open = True
            elif item == 'System Info':
                self._sysinfo_open = True
        elif key == 'BACK':
            self._menu_open = False
            self._menu_sel  = 0

    def _launch(self, name):
        if name:
            self._child = name
            self._core.publish('system.app.launch', {'app': name})

    # ── Draw loop ─────────────────────────────────────────────────────────────

    async def _draw_loop(self):
        while True:
            if not self._child:
                self._draw()
            age = utime.ticks_diff(utime.ticks_ms(), self._last_input)
            interval = _REFRESH_ACTIVE if age < _IDLE_AFTER_MS else _REFRESH_IDLE
            await asyncio.sleep_ms(interval)

    def _draw(self):
        if self._about_open:
            try:
                import gc
                free = gc.mem_free() // 1024
            except Exception:
                free = 0
            lines = [
                'PURR OS 1.0',
                self._device_name[:16],
                '{}x{} {}'.format(self._w, self._h, self._flash)[:16],
                '{}K free'.format(free),
                '',
                '',
                '',
                'any key: back',
            ]
            self._core.publish('display', {'type': 'text', 'lines': lines})
            return

        if self._sysinfo_open:
            try:
                import gc
                free  = gc.mem_free()
                total = free + gc.mem_alloc()
                mem   = '{}K/{}K'.format(free // 1024, total // 1024)
            except Exception:
                mem = '?'
            try:
                import machine
                freq = '{}MHz'.format(machine.freq() // 1_000_000)
            except Exception:
                freq = '?'
            up    = utime.ticks_ms() // 1000
            h, r  = divmod(up, 3600)
            m, s  = divmod(r, 60)
            up_s  = '{}:{:02d}:{:02d}'.format(h, m, s)
            lines = [
                'System Info',
                'RAM ' + mem,
                'CPU ' + freq,
                'Up  ' + up_s,
                '',
                '',
                '',
                'any key: back',
            ]
            self._core.publish('display', {'type': 'text', 'lines': lines})
            return

        if self._menu_open:
            visible = [x for x in self._PURR_ITEMS if not x.startswith('---')]
            lines   = ['- PURR -', '']
            for i, item in enumerate(visible):
                lines.append(('>' if i == self._menu_sel else ' ') + item[:14])
            while len(lines) < 7:
                lines.append('')
            lines.append('SEL:pick BCK:close')
            self._core.publish('display', {'type': 'text', 'lines': lines[:8]})
            return

        # Desktop: app list
        n      = len(self._apps)
        c      = self._cursor
        s      = self._scroll
        if c < s:
            self._scroll = c;   s = c
        elif c >= s + _LIST_ROWS:
            self._scroll = c - _LIST_ROWS + 1;  s = self._scroll

        hint = 'SEL:open' if self._confirm else 'SEL:next'

        lines = ['PURR OS', '']
        for i in range(_LIST_ROWS):
            idx = s + i
            if idx < n:
                name   = self._apps[idx]
                sel    = idx == c
                prefix = '>' if sel else ' '
                lines.append(prefix + name[:14])
            else:
                lines.append('')
        lines.append(hint + ' BCK:menu')

        self._core.publish('display', {'type': 'text', 'lines': lines[:8]})

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
