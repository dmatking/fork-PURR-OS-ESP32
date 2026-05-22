import uasyncio as asyncio
import utime

_BEAT_INTERVAL = 2000
_TASKBAR_H     = 32   # pixels reserved at bottom for explorer taskbar


class DisplayModule:
    NAME = 'display'

    def __init__(self, core):
        self._core    = core
        self._display = None
        self._q       = core.subscribe('display')
        self._sys_q   = core.subscribe('core.crash')

    async def run(self):
        self._init_hw()
        self._splash()

        beat_task = asyncio.create_task(self._heartbeat())
        sys_task  = asyncio.create_task(self._watch_sys())

        try:
            await self._msg_loop()
        finally:
            beat_task.cancel()
            sys_task.cancel()

    # ------------------------------------------------------------------
    # Hardware init — delegates entirely to display_factory
    # ------------------------------------------------------------------

    def _init_hw(self):
        import json, sys
        sys.path.insert(0, '/lib')
        with open('/system/kernel.app/device.json') as f:
            cfg = json.load(f)
        from display_factory import make_display
        self._display = make_display(cfg)
        print("[display] init OK, driver:", cfg.get('display'))

    # ------------------------------------------------------------------
    # Layout helpers — all positions derived from display dimensions
    # ------------------------------------------------------------------

    def _metrics(self):
        d  = self._display
        sc = getattr(d, 'scale', 1)
        return d.width, d.height, 8 * sc, 8 * sc  # w, h, char_w, char_h

    def _content_h(self):
        """Height of the content area, leaving room for the taskbar on large displays."""
        _, h, _, _ = self._metrics()
        return (h - _TASKBAR_H) if h >= 200 else h

    def _splash(self):
        d = self._display
        if not d:
            return
        w, h, cw, ch = self._metrics()
        ch_ = self._content_h()

        d.fill(0)
        d.text("PURR  OS",   w // 2 - 4 * cw, ch_ // 8)
        d.hline(0,           ch_ // 4, w, 1)
        d.text("KITT v0.1",  w // 2 - 5 * cw, ch_ * 3 // 8)
        d.text("Modules OK", w // 2 - 5 * cw, ch_ * 7 // 8)
        d.show()

    def draw_text(self, lines):
        d = self._display
        if not d:
            return
        w, _, cw, ch = self._metrics()
        ch_  = self._content_h()
        max_lines      = ch_ // ch
        chars_per_line = w  // cw

        d.fill_rect(0, 0, w, ch_, 0)   # clear content area only
        for i, line in enumerate(lines[:max_lines]):
            d.text(line[:chars_per_line], 0, i * ch)
        d.show()

    def notify(self, msg):
        d = self._display
        if not d:
            return
        w, _, cw, ch = self._metrics()
        ch_  = self._content_h()
        chars_per_line = w // cw
        y    = ch_ - ch                 # bottom of content area

        d.fill_rect(0, y, w, ch, 0)
        d.text(msg[:chars_per_line], 0, y)
        d.show()

    # ------------------------------------------------------------------
    # Raw draw-op execution (used by explorer and other UI modules)
    # ------------------------------------------------------------------

    def _exec_raw(self, ops):
        d = self._display
        if not d:
            return
        for op in ops:
            cmd = op.get('cmd')
            if   cmd == 'fill':      d.fill(op['color'])
            elif cmd == 'fill_rect': d.fill_rect(op['x'], op['y'], op['w'], op['h'], op['color'])
            elif cmd == 'hline':     d.hline(op['x'], op['y'], op['w'], op['color'])
            elif cmd == 'vline':     d.vline(op['x'], op['y'], op['h'], op['color'])
            elif cmd == 'text':
                try:
                    d.text(op['s'], op['x'], op['y'], op.get('color', 1), op.get('bg', 0))
                except TypeError:
                    d.text(op['s'], op['x'], op['y'], op.get('color', 1))
            elif cmd == 'show':      d.show()

    # ------------------------------------------------------------------
    # Message loop
    # ------------------------------------------------------------------

    async def _msg_loop(self):
        while True:
            msg  = await self._q.get()
            kind = msg.get('type')
            if kind == 'text':
                self.draw_text(msg.get('lines', []))
            elif kind == 'notify':
                self.notify(msg.get('text', ''))
            elif kind == 'raw':
                self._exec_raw(msg.get('ops', []))
            elif kind == 'clear':
                if self._display:
                    w, _, _, _ = self._metrics()
                    ch_ = self._content_h()
                    self._display.fill_rect(0, 0, w, ch_, 0)
                    self._display.show()

    async def _watch_sys(self):
        while True:
            event   = await self._sys_q.get()
            name    = event.get('module', '?')
            restarts = event.get('restarts', 0)
            self.notify(f"{name} R:{restarts}")

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
