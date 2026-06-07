"""
Net-boot config module — fetches and executes a boot script over LoRa.

On startup, waits for LoRa ready, then broadcasts BOOTREQ from the kern
virtual node. If a BOOTSCRIPT response arrives within the timeout window,
each line is executed as a boot-time command. Then goes quiet.

Boot script format (newline-separated, max ~180 bytes total):
  BOOTSCRIPT
  wifi <ssid> [password]
  psk <base64-key>
  notify <message>
  reboot

Security: transport is encrypted by the existing LoRa PSK. If no PSK is
configured, only use netboot on trusted mesh segments.
"""

import uasyncio as asyncio
import utime

_BEAT_INTERVAL   = 2000
_BOOT_TIMEOUT_MS = 30_000
_BROADCAST       = 0xFFFFFFFF
_BOOT_MAGIC      = 'BOOTSCRIPT'


class NetBootModule:
    NAME = 'netboot'

    def __init__(self, core):
        self._core      = core
        self._status_q  = core.subscribe('lora.status')
        self._kern_q    = core.subscribe('lora.kern.rx')
        self._kern_node = None

    async def run(self):
        beat_task = asyncio.create_task(self._heartbeat())
        try:
            await self._boot_seq()
        finally:
            beat_task.cancel()

    # ── Boot sequence ─────────────────────────────────────────────────────────

    async def _boot_seq(self):
        print('[netboot] waiting for lora...')
        while True:
            msg = await self._status_q.get()
            if msg.get('ready'):
                self._kern_node = msg.get('kern_node')
                break

        print('[netboot] BOOTREQ {:08X}'.format(self._kern_node or 0))
        self._core.publish('lora', {
            'type': 'kern_reply',
            'to':   _BROADCAST,
            'text': 'BOOTREQ {:08X}'.format(self._kern_node or 0),
        })

        deadline = utime.ticks_add(utime.ticks_ms(), _BOOT_TIMEOUT_MS)
        script   = None
        while utime.ticks_diff(deadline, utime.ticks_ms()) > 0:
            try:
                msg = self._kern_q.get_nowait()
                cmd = msg.get('cmd', '')
                if cmd and cmd.startswith(_BOOT_MAGIC):
                    script = cmd
                    break
            except Exception:
                pass
            await asyncio.sleep_ms(200)

        if script is None:
            print('[netboot] timeout — no boot config')
            return

        print('[netboot] applying boot script')
        for line in script.split('\n')[1:]:   # skip BOOTSCRIPT header
            line = line.strip()
            if line:
                self._exec(line)

    # ── Command executor ──────────────────────────────────────────────────────

    def _exec(self, cmd_line):
        parts = cmd_line.split(None, 2)
        if not parts:
            return
        cmd = parts[0].lower()

        if cmd == 'wifi' and len(parts) >= 2:
            ssid = parts[1]
            pwd  = parts[2] if len(parts) > 2 else ''
            self._core.publish('wifi.connect', {'ssid': ssid, 'password': pwd})
            print('[netboot] wifi:', ssid)

        elif cmd == 'psk' and len(parts) >= 2:
            self._core.publish('lora', {'type': 'kern_setpsk', 'key': parts[1]})
            print('[netboot] psk updated')

        elif cmd == 'notify' and len(parts) >= 2:
            self._core.publish('explorer.notify', {
                'text': ' '.join(parts[1:]), 'ms': 5000,
            })
            print('[netboot] notify:', ' '.join(parts[1:]))

        elif cmd == 'reboot':
            print('[netboot] rebooting')
            self._core.reboot()

        else:
            print('[netboot] unknown cmd:', cmd)

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
