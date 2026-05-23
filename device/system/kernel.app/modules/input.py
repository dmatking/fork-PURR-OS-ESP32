import uasyncio as asyncio
import machine

_BEAT_INTERVAL = 2000
_POLL_INTERVAL = 50   # ms — button poll rate
_DEBOUNCE_MS   = 80

try:
    import _purr_emulator as _emu
except ImportError:
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

        import utime
        now = utime.ticks_ms()
        for btn_name, pin_num in btn_cfg.items():
            pin = machine.Pin(pin_num, machine.Pin.IN, machine.Pin.PULL_UP)
            self._buttons.append((pin, btn_name))
            self._last_state[btn_name]  = 1   # pulled high = not pressed
            self._last_change[btn_name] = now

    def _load_cfg(self):
        import json
        with open('/system/kernel.app/device.json') as f:
            return json.load(f)

    async def _poll_loop(self):
        import utime
        while True:
            # Drain emulator-injected keys — emulator speaks generic keycodes directly
            if _emu is not None and _emu.injected_keys:
                ev = _emu.injected_keys.pop(0)
                if ev.get('key'):
                    print('[input] publishing key: {}'.format(ev['key']))
                    self._core.publish('input.key', {'key': ev['key'], 'event': 'press'})

            now = utime.ticks_ms()
            for pin, btn_name in self._buttons:
                state = pin.value()
                last  = self._last_state[btn_name]
                age   = utime.ticks_diff(now, self._last_change[btn_name])

                if state != last and age >= _DEBOUNCE_MS:
                    self._last_state[btn_name]  = state
                    self._last_change[btn_name] = now
                    if state == 0:  # active-low press
                        self._core.publish('input.raw', {'button': btn_name, 'event': 'press'})
                    else:
                        self._core.publish('input.raw', {'button': btn_name, 'event': 'release'})

            await asyncio.sleep_ms(_POLL_INTERVAL)

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
