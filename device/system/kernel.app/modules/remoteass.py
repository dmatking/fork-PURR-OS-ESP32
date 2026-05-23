"""
Remote access shell — executes kern commands received over LoRa.

Subscribes to lora.kern.rx (published by the lora module when a text
packet arrives addressed to the kern virtual node).

Replies are sent by publishing to the lora channel with type='kern_reply',
which the lora module transmits from the kern node back to the sender.

Commands:
  help    — list commands
  mem     — heap usage
  status  — running kernel modules
  reboot  — reset device
"""

import uasyncio as asyncio

_BEAT_INTERVAL = 2000


class RemoteShellModule:
    NAME = 'remoteshell'

    def __init__(self, core):
        self._core   = core
        self._kern_q = core.subscribe('lora.kern.rx')

    async def run(self):
        beat_task = asyncio.create_task(self._heartbeat())
        try:
            await self._shell_loop()
        finally:
            beat_task.cancel()

    async def _shell_loop(self):
        while True:
            msg       = await self._kern_q.get()
            cmd_line  = msg.get('cmd')
            from_node = msg.get('from_node')
            if not cmd_line or not from_node:
                continue
            result = self._exec(cmd_line)
            if result:
                self._core.publish('lora', {
                    'type': 'kern_reply',
                    'to':   from_node,
                    'text': result[:200],
                })

    def _exec(self, cmd_line):
        parts = cmd_line.split(None, 1)
        cmd   = parts[0].lower()
        arg   = parts[1] if len(parts) > 1 else ''

        if cmd == 'help':
            return 'cmds: help mem status reboot'

        elif cmd == 'mem':
            import gc
            gc.collect()
            return 'free:{}KB used:{}KB'.format(
                gc.mem_free() // 1024, gc.mem_alloc() // 1024)

        elif cmd == 'status':
            mods = list(self._core._modules.keys())
            return 'mods: ' + ' '.join(mods)

        elif cmd == 'reboot':
            self._core.reboot()
            return 'rebooting'

        return 'unknown:{} (help)'.format(cmd)

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
