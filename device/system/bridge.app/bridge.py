"""
bridge.app — input translation + radio handoff broker.

IPC channels:
  - input.raw:          subscribe — {button, event} raw GPIO events
  - input.key:          publish   — {key, event} translated keycodes
  - bridge.handoff:     subscribe — {target} request handoff to a friend
  - bridge.handoff.ack: publish   — {target, status} result of handoff
  - bridge.suspend:     publish   — {target} UI should pause before handoff
  - bridge.resume:      publish   — {target} UI should restore after handoff

Handoff model:
  Python friends (/friends/{name}/main.py or /friends/{name}.py) are exec'd
  directly — exec blocks the event loop for the duration, which is intentional.
  Compiled firmware (.bin/.fw) requires a machine-level reset; bridge prints a
  clear message and sends handoff.ack with status='needs_reset' in that case.
"""

import uasyncio as asyncio
import os

_BEAT_INTERVAL = 2000
_DEVICE_JSON   = '/system/kernel.app/device.json'

_PYTHON_EXTS  = ('.py',)
_BINARY_EXTS  = ('.bin', '.fw')


class BridgeModule:
    NAME = 'bridge'

    def __init__(self, core):
        self._core   = core
        self._keymap = {}

    async def run(self):
        self._load_keymap()
        print("[bridge] keymap:", self._keymap)

        raw_q     = self._core.subscribe('input.raw')
        handoff_q = self._core.subscribe('bridge.handoff')

        beat_task    = asyncio.create_task(self._heartbeat())
        key_task     = asyncio.create_task(self._key_loop(raw_q))
        handoff_task = asyncio.create_task(self._handoff_loop(handoff_q))

        try:
            while True:
                await asyncio.sleep_ms(1000)
        finally:
            beat_task.cancel()
            key_task.cancel()
            handoff_task.cancel()

    # ── Key translation ────────────────────────────────────────────────────────

    async def _key_loop(self, q):
        while True:
            msg     = await q.get()
            btn     = msg.get('button')
            event   = msg.get('event', 'press')
            keycode = self._keymap.get(btn)
            if keycode:
                self._core.publish('input.key', {'key': keycode, 'event': event})
            else:
                print("[bridge] unmapped button:", btn)

    # ── Radio handoff ──────────────────────────────────────────────────────────

    async def _handoff_loop(self, q):
        while True:
            msg    = await q.get()
            target = msg.get('target', 'unknown')
            print("[bridge] handoff request:", target)
            await self._do_handoff(target)

    async def _do_handoff(self, target):
        self._core.publish('bridge.suspend', {'target': target})

        entry, kind = self._find_entry(target)

        if entry is None:
            print('[bridge] no entry found for:', target)
            self._core.publish('bridge.handoff.ack', {'target': target, 'status': 'not_found'})
            self._core.publish('bridge.resume', {'target': target})
            return

        if kind == 'binary':
            print('[bridge] {} is compiled firmware — needs machine.reset() handoff'.format(target))
            self._core.publish('bridge.handoff.ack', {'target': target, 'status': 'needs_reset'})
            self._core.publish('bridge.resume', {'target': target})
            return

        # Python friend — exec blocks the event loop intentionally
        try:
            with open(entry) as f:
                code = f.read()
            print('[bridge] exec:', entry)
            exec(code, {'__name__': '__main__', '_core': self._core})
            print('[bridge] handoff returned:', target)
            self._core.publish('bridge.handoff.ack', {'target': target, 'status': 'returned'})
        except Exception as e:
            print('[bridge] handoff error:', e)
            self._core.publish('bridge.handoff.ack', {'target': target, 'status': 'error', 'error': str(e)})
        finally:
            self._core.publish('bridge.resume', {'target': target})

    def _find_entry(self, target):
        """Return (path, kind) where kind is 'python' or 'binary', or (None, None)."""
        candidates = [
            ('/friends/{}/main.py'.format(target),  'python'),
            ('/friends/{}.py'.format(target),        'python'),
            ('/friends/{}.bin'.format(target),       'binary'),
            ('/friends/{}.fw'.format(target),        'binary'),
        ]
        for path, kind in candidates:
            try:
                os.stat(path)
                return path, kind
            except OSError:
                pass
        return None, None

    # ── Helpers ────────────────────────────────────────────────────────────────

    def _load_keymap(self):
        import json
        with open(_DEVICE_JSON) as f:
            cfg = json.load(f)
        self._keymap = cfg.get('keymap', {})

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)


def register(core, cfg):
    """Registered by KITT auto-discovery. Bridge runs whenever buttons are present."""
    if cfg.get('buttons'):
        core.register('bridge', BridgeModule)
