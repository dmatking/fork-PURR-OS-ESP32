import uasyncio as asyncio
import select as _select
import sys

_BEAT_INTERVAL = 2000
_POLL_MS       = 100
_PROMPT        = 'kitt> '

_HELP = """\
Commands:
  echo <text>      show text on display (use \\n for new lines)
  notify <text>    show text in the bottom notification bar
  clear            clear the display
  status           print running module names to terminal
  help             show this message"""


class ShellModule:
    NAME = 'shell'

    def __init__(self, core):
        self._core = core

    async def run(self):
        self._poll = _select.poll()
        self._poll.register(sys.stdin, _select.POLLIN)
        sys.stdout.write(_PROMPT)
        beat_task = asyncio.create_task(self._heartbeat())
        try:
            await self._input_loop()
        finally:
            beat_task.cancel()

    # ------------------------------------------------------------------
    # Input loop — polls stdin without blocking the event loop
    # ------------------------------------------------------------------

    async def _input_loop(self):
        buf = ''
        while True:
            if self._poll.poll(0):   # 0ms = non-blocking
                ch = sys.stdin.read(1)
                if ch in ('\r', '\n'):
                    sys.stdout.write('\n')
                    line = buf.strip()
                    buf = ''
                    if line:
                        self._dispatch(line)
                    sys.stdout.write(_PROMPT)
                elif ch == '\x7f':      # backspace
                    if buf:
                        buf = buf[:-1]
                        sys.stdout.write('\b \b')
                else:
                    buf += ch
                    sys.stdout.write(ch)
            await asyncio.sleep_ms(_POLL_MS)

    # ------------------------------------------------------------------
    # Command dispatch
    # ------------------------------------------------------------------

    def _dispatch(self, line):
        parts = line.split(None, 1)
        cmd = parts[0].lower()
        arg = parts[1] if len(parts) > 1 else ''

        if cmd == 'echo':
            # split on literal \n so multi-line is possible:  echo foo\nbar
            lines = arg.replace('\\n', '\n').split('\n')
            self._core.publish('display', {'type': 'text', 'lines': lines})

        elif cmd == 'notify':
            self._core.publish('display', {'type': 'notify', 'text': arg})

        elif cmd == 'clear':
            self._core.publish('display', {'type': 'clear'})

        elif cmd == 'status':
            mods = self._core._modules
            print('\nrunning modules:')
            for name, rec in mods.items():
                state = 'up' if rec.instance else 'down'
                print('  {:12s}  {}  restarts={}'.format(name, state, rec.restarts))

        elif cmd == 'help':
            print('\n' + _HELP)

        else:
            print('unknown command: {}  (type help)'.format(cmd))

    # ------------------------------------------------------------------

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
