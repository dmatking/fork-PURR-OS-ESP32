"""
Explorer kernel stub.

Launch order:
  1. Try /apps/explorer.app  (full UI)
  2. On error → fall back to /apps/smol.app  (built-in text shell)

Goes quiet once a UI is confirmed running. Resumes polling only after
system.app.stopped fires (crash / quit).

To swap the UI: rename your .app folder to explorer.app.
"""

import uasyncio as asyncio

_BEAT_INTERVAL  = 2000
_RETRY_INTERVAL = 1000


class ExplorerModule:
    NAME = 'explorer'

    def __init__(self, core):
        self._core       = core
        self._stopped_q  = core.subscribe('system.app.stopped')
        self._launched_q = core.subscribe('system.app.launching')
        self._error_q    = core.subscribe('system.app.error')

        self._running    = False
        self._use_smol   = self._is_small_screen()

    @staticmethod
    def _is_small_screen():
        try:
            import json
            with open('/system/kernel.app/device.json') as f:
                cfg = json.load(f)
            return cfg.get('display_res', [0, 0])[1] < 100
        except Exception:
            return False

    async def run(self):
        beat_task = asyncio.create_task(self._heartbeat())
        try:
            while True:
                if not self._running:
                    target = 'smol' if self._use_smol else 'explorer'
                    self._core.publish('system.app.launch', {'app': target})

                await asyncio.sleep_ms(_RETRY_INTERVAL)

                # Confirmed launch → go quiet
                try:
                    msg = self._launched_q.get_nowait()
                    name = msg.get('name', '')
                    if name in ('explorer', 'smol'):
                        self._running = True
                except Exception:
                    pass

                # App stopped → retry same target
                try:
                    msg = self._stopped_q.get_nowait()
                    if msg.get('name') in ('explorer', 'smol'):
                        self._running = False
                except Exception:
                    pass

                # explorer.app failed → fall back to smol permanently
                try:
                    msg = self._error_q.get_nowait()
                    if msg.get('name') == 'explorer':
                        self._use_smol = True
                        self._running  = False
                except Exception:
                    pass

        finally:
            beat_task.cancel()

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
