"""
Windows CE explorer shell for PURR OS.
Provides taskbar, file explorer, Start menu, and system tray (IPC-driven).

Falls back to minimal keyboard display if LVGL is unavailable.
IPC channels:
  - input.key:         subscribe — keyboard events (UP, DOWN, SELECT, BACK)
  - explorer.tray:     subscribe — status updates (wifi: bool, ...)
  - display:           publish  — raw draw ops (minimal mode only)
  - system.app.launch: publish  — app launch requests
  - bridge.handoff:    publish  — radio handoff requests
"""

import uasyncio as asyncio
import utime

_BEAT_INTERVAL = 2000
_REFRESH_MS    = 100


class ExplorerModule:
    NAME = 'explorer'

    def __init__(self, core):
        self._core        = core
        self._keys        = core.subscribe('input.key')
        self._tray        = core.subscribe('explorer.tray')
        self._suspend_q   = core.subscribe('bridge.suspend')
        self._resume_q    = core.subscribe('bridge.resume')
        self._ui_elements = {}
        self._lvgl_mode   = False
        self._suspended   = False
        self._last_key    = None
        self._last_key_time = 0
        self._start_menu  = None   # lv.obj panel when open, None when closed

    async def run(self):
        if self._check_lvgl():
            print('[explorer] LVGL available, starting GUI mode')
            self._lvgl_mode = True
            await self._run_lvgl()
        else:
            print('[explorer] LVGL unavailable, using minimal mode')
            await self._run_minimal()

    def _check_lvgl(self):
        try:
            import lvgl as lv
            return True
        except ImportError:
            return False

    # ── LVGL mode ──────────────────────────────────────────────────────────

    async def _run_lvgl(self):
        try:
            import lvgl as lv
            from modules.lvgl_layout import build_desktop
        except ImportError:
            print('[explorer] failed to import lvgl modules, falling back')
            await self._run_minimal()
            return

        self._init_lvgl()
        beat_task   = asyncio.create_task(self._heartbeat())
        key_task    = asyncio.create_task(self._key_loop())
        clock_task  = asyncio.create_task(self._clock_loop())
        tray_task   = asyncio.create_task(self._tray_loop())
        draw_task   = asyncio.create_task(self._draw_loop_lvgl())
        bridge_task = asyncio.create_task(self._bridge_loop())

        try:
            await asyncio.sleep_ms(0)
            await asyncio.gather(key_task, clock_task, tray_task,
                                 draw_task, bridge_task)
        except asyncio.CancelledError:
            pass
        finally:
            beat_task.cancel()
            key_task.cancel()
            clock_task.cancel()
            tray_task.cancel()
            draw_task.cancel()
            bridge_task.cancel()

    def _init_lvgl(self):
        try:
            import lvgl as lv
            from modules.lvgl_layout import build_desktop

            scr = lv.screen_active()
            self._ui_elements = build_desktop(scr)

            self._ui_elements['start_btn'].add_event_cb(
                self._on_start_clicked, lv.EVENT.CLICKED, None
            )
            self._ui_elements['close_btn'].add_event_cb(
                self._on_close_clicked, lv.EVENT.CLICKED, None
            )

            lv.refr_now()
            print('[explorer] LVGL desktop initialized')

        except Exception as e:
            print('[explorer] LVGL init failed: {}'.format(e))
            self._lvgl_mode = False

    # ── Start menu ─────────────────────────────────────────────────────────

    def _on_start_clicked(self, evt):
        if self._start_menu is not None:
            self._close_start_menu()
        else:
            self._open_start_menu()

    def _open_start_menu(self):
        try:
            import lvgl as lv
            from modules.lvgl_layout import build_start_menu
        except ImportError:
            return

        apps    = self._scan_apps()
        friends = self._scan_friends()
        scr     = lv.screen_active()
        self._start_menu = build_start_menu(scr, apps, friends, self._on_app_launch)

        start_btn = self._ui_elements.get('start_btn')
        if start_btn:
            start_btn.add_state(lv.STATE.CHECKED)
        start_label = self._ui_elements.get('start_label')
        if start_label:
            start_label.set_text(":0 Start")

    def _close_start_menu(self):
        if self._start_menu is not None:
            self._start_menu.delete()
            self._start_menu = None

        try:
            import lvgl as lv
            start_btn = self._ui_elements.get('start_btn')
            if start_btn:
                start_btn.clear_state(lv.STATE.CHECKED)
        except ImportError:
            pass
        start_label = self._ui_elements.get('start_label')
        if start_label:
            start_label.set_text(":) Start")

    def _on_app_launch(self, kind, name):
        self._close_start_menu()
        if kind == 'app':
            print('[explorer] launch request:', name)
            self._core.publish('system.app.launch', {'app': name})
        elif kind == 'friend':
            print('[explorer] handoff request:', name)
            self._core.publish('bridge.handoff', {'target': name})

    # ── Explorer window close ───────────────────────────────────────────────

    def _on_close_clicked(self, evt):
        win = self._ui_elements.get('window')
        if win:
            try:
                import lvgl as lv
                win.add_flag(lv.OBJ_FLAG.HIDDEN)
            except ImportError:
                pass

    # ── Key handling ────────────────────────────────────────────────────────

    async def _key_loop(self):
        while True:
            msg   = await self._keys.get()
            key   = msg.get('key')
            event = msg.get('event')
            if key and event == 'press':
                self._last_key      = key
                self._last_key_time = utime.ticks_ms()
                print('[explorer] key: {}'.format(key))
                if self._lvgl_mode:
                    self._handle_key_lvgl(key)

    def _handle_key_lvgl(self, key):
        if key == 'BACK':
            if self._start_menu is not None:
                self._close_start_menu()
            else:
                win = self._ui_elements.get('window')
                if win:
                    try:
                        import lvgl as lv
                        win.add_flag(lv.OBJ_FLAG.HIDDEN)
                    except ImportError:
                        pass

    # ── Clock ───────────────────────────────────────────────────────────────

    async def _clock_loop(self):
        if not self._lvgl_mode:
            return
        try:
            from modules.lvgl_layout import update_clock
        except ImportError:
            return

        while True:
            now    = utime.localtime()
            update_clock(self._ui_elements, now[3], now[4])
            await asyncio.sleep_ms(60000)

    # ── Tray ────────────────────────────────────────────────────────────────

    async def _tray_loop(self):
        try:
            from modules.lvgl_layout import update_wifi
        except ImportError:
            while True:
                await self._tray.get()   # drain to avoid queue buildup
            return

        while True:
            msg = await self._tray.get()
            if not self._lvgl_mode:
                continue
            if 'wifi' in msg:
                update_wifi(self._ui_elements, msg['wifi'])

    # ── LVGL draw loop ──────────────────────────────────────────────────────

    async def _draw_loop_lvgl(self):
        try:
            import lvgl as lv
        except ImportError:
            return

        while True:
            if not self._suspended:
                try:
                    lv.refr_now()
                except Exception:
                    pass
            await asyncio.sleep_ms(_REFRESH_MS)

    # ── Filesystem scanning ─────────────────────────────────────────────────

    def _scan_apps(self):
        import os
        apps = []
        try:
            for entry in os.listdir('/apps'):
                if entry.endswith('.app'):
                    try:
                        os.stat('/apps/{}/main.py'.format(entry))
                        apps.append(entry[:-4])   # strip .app suffix
                    except OSError:
                        pass
        except OSError:
            pass
        return apps

    def _scan_friends(self):
        import os
        friends = []
        try:
            for entry in os.listdir('/friends'):
                if entry.endswith('.bin') or entry.endswith('.fw') or entry.endswith('.py'):
                    friends.append(entry)
                    continue
                # Directory with main.py — Python friend bundle
                try:
                    os.stat('/friends/{}/main.py'.format(entry))
                    friends.append(entry)
                except OSError:
                    pass
        except OSError:
            pass
        return friends

    # ── Bridge suspend / resume ─────────────────────────────────────────────

    async def _bridge_loop(self):
        while True:
            try:
                msg = self._suspend_q.get_nowait()
                self._suspended = True
                print('[explorer] suspended for handoff:', msg.get('target'))
            except Exception:
                pass
            try:
                msg = self._resume_q.get_nowait()
                if self._suspended:
                    self._suspended = False
                    print('[explorer] resumed from handoff:', msg.get('target'))
                    if self._lvgl_mode:
                        try:
                            import lvgl as lv
                            lv.refr_now()
                        except ImportError:
                            pass
            except Exception:
                pass
            await asyncio.sleep_ms(100)

    # ── Heartbeat ───────────────────────────────────────────────────────────

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)

    # ── Fallback: Minimal display mode (no LVGL) ────────────────────────────

    async def _run_minimal(self):
        self._show_fallback_splash()

        beat_task = asyncio.create_task(self._heartbeat())
        key_task  = asyncio.create_task(self._key_loop())
        draw_task = asyncio.create_task(self._draw_loop_minimal())

        try:
            await asyncio.gather(key_task, draw_task)
        except asyncio.CancelledError:
            pass
        finally:
            beat_task.cancel()
            key_task.cancel()
            draw_task.cancel()

    def _show_fallback_splash(self):
        ops = [
            {'cmd': 'fill',  'color': 0},
            {'cmd': 'text',  's': 'FALLBACK MODE',     'x': 20, 'y': 30,  'color': 1},
            {'cmd': 'text',  's': 'LVGL not available', 'x': 20, 'y': 60,  'color': 1},
            {'cmd': 'text',  's': 'Using text mode',    'x': 20, 'y': 90,  'color': 1},
            {'cmd': 'text',  's': '---',                'x': 20, 'y': 130, 'color': 1},
            {'cmd': 'text',  's': 'Press keys to test', 'x': 20, 'y': 160, 'color': 1},
            {'cmd': 'show'},
        ]
        self._core.publish('display', {'type': 'raw', 'ops': ops})

    async def _draw_loop_minimal(self):
        splash_time     = utime.ticks_ms()
        splash_done     = False

        while True:
            now = utime.ticks_ms()
            if not splash_done and utime.ticks_diff(now, splash_time) < 2000:
                await asyncio.sleep_ms(_REFRESH_MS)
                continue
            splash_done = True

            ops = [
                {'cmd': 'fill',  'color': 0},
                {'cmd': 'text',  's': '[FALLBACK MODE]', 'x': 20, 'y': 10, 'color': 1},
            ]

            if self._last_key:
                age = utime.ticks_diff(utime.ticks_ms(), self._last_key_time)
                if age < 1000:
                    ops.append({'cmd': 'text', 's': 'Key: {}'.format(self._last_key),
                                'x': 20, 'y': 50, 'color': 1})
                else:
                    self._last_key = None

            ops.append({'cmd': 'show'})
            self._core.publish('display', {'type': 'raw', 'ops': ops})
            await asyncio.sleep_ms(_REFRESH_MS)
