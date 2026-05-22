import uasyncio as asyncio
import utime

_BEAT_INTERVAL = 2000
_REFRESH_MS    = 100


class ExplorerModule:
    NAME = 'explorer'

    def __init__(self, core):
        self._core  = core
        self._keys  = core.subscribe('input.key')
        self._last_key = None
        self._last_key_time = 0
        self._w = 320
        self._h = 240
        self._load_cfg()

    def _load_cfg(self):
        try:
            import json
            with open('/system/kernel.app/device.json') as f:
                cfg = json.load(f)
            res = cfg.get('display_res', [128, 64])
            self._w, self._h = res[0], res[1]
        except Exception:
            pass

    async def run(self):
        beat_task = asyncio.create_task(self._heartbeat())
        key_task = asyncio.create_task(self._key_loop())
        try:
            await self._draw_loop()
        finally:
            beat_task.cancel()
            key_task.cancel()

    async def _key_loop(self):
        while True:
            msg = await self._keys.get()
            key = msg.get('key')
            event = msg.get('event')
            if key and event == 'press':
                self._last_key = key
                self._last_key_time = utime.ticks_ms()
                print('[key tester] pressed: {}'.format(key))

    async def _draw_loop(self):
        while True:
            self._draw()
            await asyncio.sleep_ms(_REFRESH_MS)

    def _draw(self):
        w = self._w
        h = self._h
        ops = []

        # Clear screen
        ops.append({'cmd': 'fill', 'color': 0})

        # Title
        ops.append({'cmd': 'text', 's': 'KEY TESTER', 'x': w//2 - 50, 'y': 10, 'color': 1})

        # Current key display
        if self._last_key:
            age_ms = utime.ticks_diff(utime.ticks_ms(), self._last_key_time)
            if age_ms < 1000:
                ops.append({'cmd': 'text', 's': 'Last key:', 'x': 20, 'y': 60, 'color': 1})
                ops.append({'cmd': 'text', 's': self._last_key, 'x': 20, 'y': 80, 'color': 1})
            else:
                self._last_key = None

        # Instructions
        ops.append({'cmd': 'text', 's': 'Press keys on emulator:', 'x': 20, 'y': 130, 'color': 1})
        ops.append({'cmd': 'text', 's': 'Enter, Esc, Arrows', 'x': 20, 'y': 150, 'color': 1})

        ops.append({'cmd': 'show'})
        self._core.publish('display', {'type': 'raw', 'ops': ops})

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
