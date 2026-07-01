# CattoBoardV1 — Handshake Protocols

## Overview

One handshake protocol on CattoBoardV1:

**CM5 ↔ RAK3172 LoRa (UART AT commands)**

The ESP32-S2 is a dumb HID keyboard — no protocol, no handshake, no data channel.
LoRa lives entirely on the CM5 side via the RAK3172 over UART2.

---

## CM5 ↔ RAK3172 LoRa UART Handshake

RAK3172 uses AT commands over UART2 at 115200 baud.

### Startup sequence

```
CM5 opens UART2 (/dev/ttyS2 or /dev/ttyAMA2)
      ↓
CM5 sends:     AT\r\n
RAK3172 responds: OK\r\n
      ↓
CM5 sends:     AT+VER=?\r\n
RAK3172 responds: +VER:RUI_4.x.x\r\nOK\r\n
      ↓
CM5 sends:     AT+BAND=?\r\n
RAK3172 responds: +BAND:8\r\nOK\r\n  (US915)
      ↓
LoRa ready for use
```

### Retry logic — 3 attempts, then log and continue without LoRa

### Python implementation (CM5 side)

```python
# purr_os/lora_daemon.py
import serial
import time

class LoRaDaemon:
    def __init__(self, port="/dev/ttyS2", baud=115200):
        self.ser = serial.Serial(port, baud, timeout=2)
        self.ready = False

    def init(self):
        for attempt in range(3):
            resp = self._cmd("AT")
            if "OK" in resp:
                ver = self._cmd("AT+VER=?")
                print(f"[LORA] RAK3172: {ver}")
                self.ready = True
                return True
            time.sleep(1)
        print("[LORA] RAK3172 not responding — continuing without LoRa")
        return False

    def _cmd(self, cmd, timeout=2):
        self.ser.write((cmd + "\r\n").encode())
        time.sleep(0.1)
        resp = ""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.ser.in_waiting:
                resp += self.ser.read(self.ser.in_waiting).decode()
                if "OK" in resp or "ERROR" in resp:
                    break
        return resp.strip()

    def send(self, data_hex):
        if not self.ready:
            return False
        resp = self._cmd(f"AT+SEND=3:{data_hex}")
        return "OK" in resp

    def receive(self):
        # RAK3172 pushes received packets as:
        # +EVT:RX_1:RSSI:-65:SNR:7:DLEN:5:DATA:48656C6C6F
        if self.ser.in_waiting:
            line = self.ser.readline().decode().strip()
            if "+EVT:RX" in line:
                return self._parse_rx(line)
        return None

    def _parse_rx(self, line):
        # Parse RAK3172 RX event string
        parts = line.split(":")
        try:
            return {
                "rssi": int(parts[2]),
                "snr":  float(parts[4]),
                "data": bytes.fromhex(parts[8])
            }
        except Exception:
            return None
```

---

## ESP32-S2 — No Protocol

The ESP32-S2 is USB HID only. The CM5 sees it as a standard USB keyboard.
No handshake, no data channel, no messages. It just sends keystrokes.
