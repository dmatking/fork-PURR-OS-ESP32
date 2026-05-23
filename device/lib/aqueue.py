import uasyncio as asyncio


class Queue:
    """
    Async FIFO queue built on asyncio.Event.
    Drop-in replacement for the asyncio.Queue that was removed in MicroPython 1.20+.
    """

    def __init__(self, maxsize=0):
        self._items   = []
        self._ready   = asyncio.Event()
        self._maxsize = maxsize

    def put_nowait(self, item):
        if self._maxsize and len(self._items) >= self._maxsize:
            return  # drop oldest? for now just drop new — never blocks publisher
        self._items.append(item)
        self._ready.set()

    async def get(self):
        while not self._items:
            self._ready.clear()
            await self._ready.wait()
        return self._items.pop(0)

    def qsize(self):
        return len(self._items)

    def get_nowait(self):
        if not self._items:
            raise IndexError('queue empty')
        return self._items.pop(0)

    def empty(self):
        return not self._items
