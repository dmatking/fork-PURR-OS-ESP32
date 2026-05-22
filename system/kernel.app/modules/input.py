import uasyncio as asyncio
import machine

_BEAT_INTERVAL = 2000
_POLL_INTERVAL = 50   # ms — button poll rate
_DEBOUNCE_MS   = 80

# Generic keycodes emitted on the 'input.key' channel
KEY_UP     = 'UP'
KEY_DOWN   = 'DOWN'
KEY_LEFT   = 'LEFT'
KEY_RIGHT  = 'RIGHT'
KEY_SELECT = 'SELECT'
KEY_BACK   = 'BACK'

# Emulator support (placeholder for future)
_emu = None


class InputModule:
    NAME = 'input'

    def __init__(self, core):
        self._core = core
        self._buttons = []
        self._last_state = {}
        self._last_change = {}

    async def run(self):
        self._init_hw()
        print("[input] init OK, buttons:", len(self._buttons))

        beat_task = asyncio.create_task(self._heartbeat())
        try:
            await self._poll_loop()
        finally:
            beat_task.cancel()

    def _init_hw(self):
        cfg = self._load_cfg()
        btn_cfg = cfg.get('buttons', {})

        # Map physical button names to generic keycodes
        keymap = {
            'user': KEY_SELECT,
            'prg':  KEY_BACK,
            'boot': KEY_SELECT,  # S3-Box-3 / generic single-button devices
        }

        import utime
        now = utime.ticks_ms()
        for btn_name, keycode in keymap.items():
            pin_num = btn_cfg.get(btn_name)
            if pin_num is None:
                continue
            pin = machine.Pin(pin_num, machine.Pin.IN, machine.Pin.PULL_UP)
            self._buttons.append((pin, keycode))
            self._last_state[keycode]  = 1   # pulled high = not pressed
            self._last_change[keycode] = now

    def _load_cfg(self):
        import json
        with open('/system/kernel.app/device.json') as f:
            return json.load(f)

    async def _poll_loop(self):
        import utime
        while True:
            now = utime.ticks_ms()
            for pin, keycode in self._buttons:
                state = pin.value()
                last  = self._last_state[keycode]
                age   = utime.ticks_diff(now, self._last_change[keycode])

                if state != last and age >= _DEBOUNCE_MS:
                    self._last_state[keycode]  = state
                    self._last_change[keycode] = now
                    if state == 0:  # active-low press
                        self._core.publish('input.key', {'key': keycode, 'event': 'press'})
                    else:
                        self._core.publish('input.key', {'key': keycode, 'event': 'release'})

            await asyncio.sleep_ms(_POLL_INTERVAL)

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
