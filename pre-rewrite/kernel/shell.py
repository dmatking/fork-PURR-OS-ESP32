import uasyncio as asyncio
import select as _select
import sys
import utime

_BEAT_INTERVAL = 2000
_POLL_MS       = 100
_PROMPT        = 'kitt> '

_HELP = """\
System:
  mem              free / used RAM
  uptime           kernel uptime
  status           running modules + restart counts
  reboot           soft reboot
  pub <topic> <j>  publish raw IPC message (j = JSON object)

Display:
  echo <text>      show text on display  (\\n for newlines)
  notify <text>    bottom notification bar
  clear            clear the display

Radio:
  lora send <txt>  broadcast Meshtastic text from user node
  lora status      LoRa module status
  lora kern        show kern virtual node ID
  lora kern share <node_id_hex>   send PSK to that node
  lora sim <cmd>   inject kern shell command (spoof/emulator only)
  lora sim broadcast <text>       inject a mesh broadcast
  lora sim nodeinfo               inject NodeInfo packet to kern node
  meshme <n>       send N test broadcasts from kern node

WiFi:
  wifi status      connection status + IP
  wifi connect <ssid> [pass]  connect to network
  wifi scan        scan nearby APs
  wifi off         disconnect

  help             show this message"""


class ShellModule:
    NAME = 'shell'

    def __init__(self, core):
        self._core    = core
        self._boot_ms = utime.ticks_ms()

    async def run(self):
        self._poll = _select.poll()
        self._poll.register(sys.stdin, _select.POLLIN)
        sys.stdout.write(_PROMPT)
        beat_task = asyncio.create_task(self._heartbeat())
        try:
            await self._input_loop()
        finally:
            beat_task.cancel()

    # ── Input loop ────────────────────────────────────────────────────────────

    async def _input_loop(self):
        buf = ''
        while True:
            if self._poll.poll(0):
                ch = sys.stdin.read(1)
                if ch in ('\r', '\n'):
                    sys.stdout.write('\n')
                    line = buf.strip()
                    buf  = ''
                    if line:
                        self._dispatch(line)
                    sys.stdout.write(_PROMPT)
                elif ch == '\x7f':
                    if buf:
                        buf = buf[:-1]
                        sys.stdout.write('\b \b')
                else:
                    buf += ch
                    sys.stdout.write(ch)
            await asyncio.sleep_ms(_POLL_MS)

    # ── Command dispatch ──────────────────────────────────────────────────────

    def _dispatch(self, line):
        parts = line.split(None, 1)
        cmd   = parts[0].lower()
        arg   = parts[1] if len(parts) > 1 else ''

        if cmd == 'help':
            print('\n' + _HELP)

        elif cmd == 'mem':
            self._cmd_mem()

        elif cmd == 'uptime':
            self._cmd_uptime()

        elif cmd == 'status':
            self._cmd_status()

        elif cmd == 'reboot':
            print('rebooting...')
            self._core.reboot()

        elif cmd == 'pub':
            self._cmd_pub(arg)

        elif cmd == 'echo':
            lines = arg.replace('\\n', '\n').split('\n')
            self._core.publish('display', {'type': 'text', 'lines': lines})

        elif cmd == 'notify':
            self._core.publish('display', {'type': 'notify', 'text': arg})

        elif cmd == 'clear':
            self._core.publish('display', {'type': 'clear'})

        elif cmd == 'lora':
            self._cmd_lora(arg)

        elif cmd == 'meshme':
            self._cmd_meshme(arg)

        elif cmd == 'wifi':
            self._cmd_wifi(arg)

        else:
            print('unknown: {}  (help)'.format(cmd))

    # ── System commands ───────────────────────────────────────────────────────

    def _cmd_mem(self):
        import gc
        gc.collect()
        free  = gc.mem_free()
        used  = gc.mem_alloc()
        total = free + used
        pct   = 100 * used // total if total else 0
        print('RAM: {}KB free  {}KB used  {}% ({}KB total)'.format(
            free // 1024, used // 1024, pct, total // 1024))

    def _cmd_uptime(self):
        ms  = utime.ticks_diff(utime.ticks_ms(), self._boot_ms)
        s   = ms // 1000
        m   = s  // 60
        h   = m  // 60
        print('uptime: {:02d}:{:02d}:{:02d}'.format(h, m % 60, s % 60))

    def _cmd_status(self):
        mods = self._core._modules
        print('\nmodule           state   restarts')
        for name, rec in mods.items():
            state = 'up' if rec.instance else 'down'
            print('  {:14s}  {:6s}  {}'.format(name, state, rec.restarts))

    def _cmd_pub(self, arg):
        import json
        parts = arg.split(None, 1)
        if not parts:
            print('usage: pub <topic> <json>')
            return
        topic = parts[0]
        try:
            payload = json.loads(parts[1]) if len(parts) > 1 else {}
        except Exception as e:
            print('pub: bad JSON:', e)
            return
        self._core.publish(topic, payload)
        print('→', topic)

    # ── LoRa commands ─────────────────────────────────────────────────────────

    def _cmd_lora(self, arg):
        parts = arg.split(None, 1)
        if not parts:
            print('usage: lora send <text> | lora status | lora kern [share <node>]')
            return
        sub  = parts[0].lower()
        rest = parts[1] if len(parts) > 1 else ''

        if sub == 'send':
            if not rest:
                print('lora send: need text')
                return
            self._core.publish('lora', {'type': 'send', 'text': rest})
            print('→ broadcast: {}'.format(rest))

        elif sub == 'status':
            self._core.publish('lora', {'type': 'status'})
            print('status queued (watch lora.status IPC)')

        elif sub == 'kern':
            self._cmd_lora_kern(rest)

        elif sub == 'sim':
            self._cmd_lora_sim(rest)

        else:
            print('lora: unknown sub:', sub)

    def _cmd_lora_kern(self, arg):
        parts = arg.split(None, 1)
        if not parts:
            # Just print kern node info from module record
            rec = self._core._modules.get('lora')
            if rec and rec.instance:
                inst = rec.instance
                print('kern node:  {:08X}'.format(inst._kern_node or 0))
                print('main node:  {:08X}'.format(inst._node or 0))
                print('encryption: {}'.format('ON' if inst._psk else 'OFF'))
            else:
                print('lora module not running')
            return

        sub  = parts[0].lower()
        rest = parts[1] if len(parts) > 1 else ''

        if sub == 'share':
            if not rest:
                print('usage: lora kern share <node_id_hex>')
                return
            try:
                node_id = int(rest.lstrip('!'), 16)
            except Exception:
                print('kern share: invalid node id (hex expected, e.g. deadbeef)')
                return
            self._core.publish('lora', {'type': 'kern_sendpsk', 'to_node': node_id})
            print('→ sending PSK to {:08X}'.format(node_id))

        else:
            print('lora kern: unknown sub:', sub)

    # ── lora sim command ──────────────────────────────────────────────────────

    def _cmd_lora_sim(self, arg):
        """Inject a simulated Meshtastic packet through the full _handle_rx path."""
        rec = self._core._modules.get('lora')
        if not rec or not rec.instance:
            print('lora module not running')
            return
        inst = rec.instance
        if not inst._spoof:
            print('sim only available in spoof mode (no real hardware attached)')
            return

        parts = arg.split(None, 1)
        if not parts:
            print('usage: lora sim <cmd>            → kern shell command')
            print('       lora sim broadcast <text> → mesh broadcast')
            print('       lora sim nodeinfo         → NodeInfo packet to kern node')
            return

        sub  = parts[0].lower()
        rest = parts[1] if len(parts) > 1 else ''

        if sub == 'broadcast':
            if not rest:
                print('lora sim broadcast: need text')
                return
            inst._inject_pkt(0xFFFFFFFF, 0xDEADBEEF, 1, rest)

        elif sub == 'nodeinfo':
            inst._inject_pkt(inst._kern_node, 0xDEADBEEF, 4, '')

        else:
            # Treat the whole arg as a kern shell command
            cmd_text = arg.strip()
            inst._inject_pkt(inst._kern_node, 0xDEADBEEF, 1, cmd_text)
            print('→ kern: {!r}'.format(cmd_text))

    # ── meshme command ────────────────────────────────────────────────────────

    def _cmd_meshme(self, arg):
        try:
            count = int(arg) if arg.strip() else 1
        except Exception:
            print('usage: meshme <count>')
            return
        if count < 1:
            print('meshme: count must be >= 1')
            return
        self._core.publish('lora', {'type': 'meshme', 'count': count})
        print('queued {} test broadcast{}'.format(count, 's' if count != 1 else ''))

    # ── WiFi commands ─────────────────────────────────────────────────────────

    def _cmd_wifi(self, arg):
        parts = arg.split(None, 2)
        if not parts:
            print('usage: wifi status | wifi connect <ssid> [pass] | wifi scan | wifi off')
            return
        sub = parts[0].lower()

        if sub == 'status':
            self._core.publish('wifi', {'type': 'status'})
            # Print directly if we can reach the wifi module
            rec = self._core._modules.get('wifi')
            if rec and rec.instance:
                inst = rec.instance
                if inst._sta.isconnected():
                    cfg = inst._sta.ifconfig()
                    print('connected  ip={}  gw={}  dns={}'.format(cfg[0], cfg[2], cfg[3]))
                else:
                    print('not connected')
            else:
                print('wifi module not running')

        elif sub == 'connect':
            ssid = parts[1] if len(parts) > 1 else ''
            pw   = parts[2] if len(parts) > 2 else ''
            if not ssid:
                print('usage: wifi connect <ssid> [password]')
                return
            self._core.publish('wifi', {'type': 'connect', 'ssid': ssid, 'password': pw})
            print('connecting to {}...'.format(ssid))

        elif sub == 'scan':
            rec = self._core._modules.get('wifi')
            if rec and rec.instance:
                nets = rec.instance._sta.scan()
                if not nets:
                    print('no networks found')
                else:
                    print('{} networks:'.format(len(nets)))
                    for n in nets:
                        ssid = n[0].decode() if n[0] else '(hidden)'
                        ch   = n[2]
                        rssi = n[3]
                        print('  {:32s}  ch {:2d}  {}dBm'.format(ssid, ch, rssi))
            else:
                print('wifi module not running')

        elif sub == 'off':
            self._core.publish('wifi', {'type': 'disconnect'})
            print('disconnecting')

        else:
            print('wifi: unknown sub:', sub)

    # ── Heartbeat ─────────────────────────────────────────────────────────────

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
