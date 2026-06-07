"""
PURR UI — grid launcher shell for PURR OS.
Replaces the Windows CE explorer as the primary user-facing shell.

Tiles are drag-and-drop rearrangeable. Order persists to /data/purr_ui_order.json.

IPC channels:
  - input.key:         subscribe — keyboard events (F1/F2/F3/BACK)
  - explorer.tray:     subscribe — status updates (wifi: bool)
  - system.app.launch: publish  — app launch requests
  - purr_ui.fn1-3:     publish  — function key presses
  - bridge.suspend:    subscribe — pause redraws during handoff
  - bridge.resume:     subscribe — resume after handoff
"""

import uasyncio as asyncio
import utime
import os

_BEAT_INTERVAL = 2000
_REFRESH_MS    = 100
_ORDER_PATH    = '/data/purr_ui_order.json'

# Must mirror purr_ui_layout.SLOT_POSITIONS / _TILE_W / _TILE_H exactly
_SLOT_POS = [(20, 32), (170, 32), (20, 224), (170, 224)]
_TILE_W   = 130
_TILE_H   = 170


class PurrUIModule:
    NAME = 'purr_ui'

    def __init__(self, core):
        self._core       = core
        self._keys       = core.subscribe('input.key')
        self._tray       = core.subscribe('explorer.tray')
        self._suspend_q  = core.subscribe('bridge.suspend')
        self._resume_q   = core.subscribe('bridge.resume')
        self._ui         = {}
        self._slots      = []
        self._handlers   = []   # Python-side refs prevent GC
        self._lvgl_mode  = False
        self._suspended  = False

    async def run(self):
        if self._check_lvgl():
            self._lvgl_mode = True
            print('[purr_ui] LVGL mode')
            await self._run_lvgl()
        else:
            print('[purr_ui] minimal mode')
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
            from modules.purr_ui_layout import build_purr_ui
        except ImportError:
            await self._run_minimal()
            return

        self._init_ui()

        beat_task   = asyncio.create_task(self._heartbeat())
        key_task    = asyncio.create_task(self._key_loop())
        clock_task  = asyncio.create_task(self._clock_loop())
        tray_task   = asyncio.create_task(self._tray_loop())
        draw_task   = asyncio.create_task(self._draw_loop())
        bridge_task = asyncio.create_task(self._bridge_loop())

        try:
            await asyncio.gather(key_task, clock_task, tray_task,
                                 draw_task, bridge_task)
        except asyncio.CancelledError:
            pass
        finally:
            for t in (beat_task, key_task, clock_task, tray_task,
                      draw_task, bridge_task):
                t.cancel()

    def _init_ui(self):
        try:
            import lvgl as lv
            from modules.purr_ui_layout import build_purr_ui
        except ImportError:
            self._lvgl_mode = False
            return

        apps = self._load_app_order()
        scr  = lv.screen_active()

        try:
            self._ui, self._slots = build_purr_ui(scr, apps)
            self._wire_tiles()
            self._wire_fn_btns()
            lv.refr_now()
            print('[purr_ui] initialized with {} apps'.format(len(apps)))
        except Exception as e:
            print('[purr_ui] init error:', e)
            self._lvgl_mode = False

    # ── Tile wiring ─────────────────────────────────────────────────────────

    def _wire_tiles(self):
        try:
            import lvgl as lv
        except ImportError:
            return

        for slot in self._slots:
            # Launch callback reads slot['app'] at call-time so it stays
            # correct after swaps without re-wiring.
            launch_h = self._make_launch_handler(slot['idx'])
            self._handlers.append(launch_h)
            slot['btn'].add_event_cb(launch_h, lv.EVENT.CLICKED, None)

            # Drag: RELEASED on the cell to detect drop position.
            drag_h = self._make_drag_handler(slot['idx'])
            self._handlers.append(drag_h)
            slot['cell'].add_event_cb(drag_h, lv.EVENT.RELEASED, None)

    def _make_launch_handler(self, slot_idx):
        def handler(evt):
            app = self._slots[slot_idx]['app']
            if app:
                print('[purr_ui] launch:', app)
                self._core.publish('system.app.launch', {'app': app})
        return handler

    def _make_drag_handler(self, slot_idx):
        def handler(evt):
            obj   = evt.get_target()
            cur_x = obj.get_x()
            cur_y = obj.get_y()
            sx, sy = _SLOT_POS[slot_idx]

            # Only treat as a drag if the cell moved significantly
            moved = abs(cur_x - sx) > 8 or abs(cur_y - sy) > 8
            if moved:
                nearest = self._nearest_slot(cur_x + _TILE_W // 2,
                                             cur_y + _TILE_H // 2)
                # Snap cell back to its home slot
                obj.set_pos(sx, sy)
                if nearest != slot_idx:
                    self._swap_tiles(slot_idx, nearest)
                    self._save_order()
            else:
                # Short tap with no movement — snap back cleanly
                obj.set_pos(sx, sy)
        return handler

    def _nearest_slot(self, cx, cy):
        nearest, best = 0, 999999
        for i, (sx, sy) in enumerate(_SLOT_POS):
            sc_x = sx + _TILE_W // 2
            sc_y = sy + _TILE_H // 2
            dist = abs(cx - sc_x) + abs(cy - sc_y)
            if dist < best:
                best    = dist
                nearest = i
        return nearest

    def _swap_tiles(self, i, j):
        si = self._slots[i]
        sj = self._slots[j]

        app_i = si['app']
        app_j = sj['app']

        si['app'] = app_j
        sj['app'] = app_i

        self._render_slot(si)
        self._render_slot(sj)
        print('[purr_ui] swapped slots {} ({}) <-> {} ({})'.format(
            i, app_j, j, app_i))

    def _render_slot(self, slot):
        try:
            import lvgl as lv
            from modules.purr_ui_layout import TILE_SYMBOLS, TILE_COLORS, COLOR_EMPTY_TILE, COLOR_WHITE
        except ImportError:
            return

        idx = slot['idx']
        app = slot['app']

        if app:
            slot['name_lbl'].set_text(app)
            slot['icon_lbl'].set_text(TILE_SYMBOLS[idx % 4])
            slot['btn'].set_style_bg_color(TILE_COLORS[idx % 4], 0)
        else:
            slot['name_lbl'].set_text("")
            slot['icon_lbl'].set_text(lv.SYMBOL.PLUS)
            slot['btn'].set_style_bg_color(COLOR_EMPTY_TILE, 0)

    # ── Function key wiring ─────────────────────────────────────────────────

    def _wire_fn_btns(self):
        try:
            import lvgl as lv
        except ImportError:
            return

        fn_btns  = self._ui.get('fn_btns', [])
        channels = ['purr_ui.fn1', 'purr_ui.fn2', 'purr_ui.fn3']

        for btn, ch, key_num in zip(fn_btns, channels, (1, 2, 3)):
            def make_fn(channel, n):
                def handler(evt):
                    self._core.publish(channel, {'key': 'F{}'.format(n)})
                return handler
            h = make_fn(ch, key_num)
            self._handlers.append(h)
            btn.add_event_cb(h, lv.EVENT.CLICKED, None)

    # ── App order persistence ───────────────────────────────────────────────

    def _load_app_order(self):
        apps = self._scan_apps()
        try:
            import ujson
            with open(_ORDER_PATH) as f:
                saved = ujson.load(f)
            # Merge: saved order first, then any new apps not in saved
            ordered = [a for a in saved if a in apps]
            ordered += [a for a in apps if a not in ordered]
            return ordered
        except Exception:
            return apps

    def _save_order(self):
        try:
            import ujson
            order = [s['app'] for s in self._slots if s['app']]
            try:
                os.mkdir('/data')
            except OSError:
                pass
            with open(_ORDER_PATH, 'w') as f:
                ujson.dump(order, f)
        except Exception as e:
            print('[purr_ui] order save failed:', e)

    def _scan_apps(self):
        apps = []
        try:
            for entry in os.listdir('/apps'):
                if entry.endswith('.app'):
                    try:
                        os.stat('/apps/{}/main.py'.format(entry))
                        apps.append(entry[:-4])
                    except OSError:
                        pass
        except OSError:
            pass
        return apps

    # ── Background loops ────────────────────────────────────────────────────

    async def _clock_loop(self):
        try:
            from modules.purr_ui_layout import update_clock
        except ImportError:
            return
        while True:
            now = utime.localtime()
            update_clock(self._ui, now[3], now[4])
            await asyncio.sleep_ms(60000)

    async def _tray_loop(self):
        try:
            from modules.purr_ui_layout import update_wifi
        except ImportError:
            while True:
                await self._tray.get()
            return
        while True:
            msg = await self._tray.get()
            if 'wifi' in msg:
                update_wifi(self._ui, msg['wifi'])

    async def _key_loop(self):
        fn_map = {'F1': 'purr_ui.fn1', 'F2': 'purr_ui.fn2', 'F3': 'purr_ui.fn3'}
        while True:
            msg   = await self._keys.get()
            key   = msg.get('key')
            event = msg.get('event')
            if key and event == 'press':
                if key in fn_map:
                    self._core.publish(fn_map[key], {'key': key})

    async def _draw_loop(self):
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

    async def _bridge_loop(self):
        while True:
            try:
                msg = self._suspend_q.get_nowait()
                self._suspended = True
                print('[purr_ui] suspended:', msg.get('target'))
            except Exception:
                pass
            try:
                msg = self._resume_q.get_nowait()
                if self._suspended:
                    self._suspended = False
                    print('[purr_ui] resumed:', msg.get('target'))
                    if self._lvgl_mode:
                        try:
                            import lvgl as lv
                            lv.refr_now()
                        except ImportError:
                            pass
            except Exception:
                pass
            await asyncio.sleep_ms(100)

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)

    # ── Fallback minimal mode ───────────────────────────────────────────────

    async def _run_minimal(self):
        beat_task = asyncio.create_task(self._heartbeat())
        try:
            while True:
                ops = [
                    {'cmd': 'fill',  'color': 0},
                    {'cmd': 'text',  's': 'PURR UI', 'x': 20, 'y': 20, 'color': 1},
                    {'cmd': 'text',  's': 'LVGL unavailable', 'x': 20, 'y': 50, 'color': 1},
                    {'cmd': 'show'},
                ]
                self._core.publish('display', {'type': 'raw', 'ops': ops})
                await asyncio.sleep_ms(5000)
        except asyncio.CancelledError:
            pass
        finally:
            beat_task.cancel()


def register(core, cfg):
    core.register('purr_ui', PurrUIModule)
