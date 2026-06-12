# PURR OS — Heltec WiFi LoRa 32 V3 Flash Guide

**Hardware:** Heltec WiFi LoRa 32 V3 (ESP32-S3)  
**Display:** SSD1306 OLED 128×64  
**Radio:** SX1262 LoRa  
**Flash:** 8 MB  

---

## Flash via esptool (Linux/macOS)

```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 \
  write_flash \
  0x0     bootloader.bin \
  0x8000  partition-table.bin \
  0x10000 purr_os_core.bin \
  0x700000 spiffs.bin
```

---

## Partition map

| Address     | File                | Description           |
|-------------|---------------------|-----------------------|
| `0x0`       | `bootloader.bin`    | IDF second-stage bootloader |
| `0x8000`    | `partition-table.bin` | Partition table      |
| `0x10000`   | `purr_os_core.bin`  | PURR OS (7 MB slot)  |
| `0x700000`  | `spiffs.bin`        | SPIFFS filesystem (1 MB) |

---

## Notes

- LoRa is enabled by default on Heltec. **Always connect an antenna before powering on** — transmitting without antenna can damage the SX1262 PA.
- SSD1306 OLED is I2C at address 0x3C on SDA=17, SCL=18.
- KittenUI text shell — no touchscreen, UART serial console for input.
- LoRa: SX1262 on MOSI=10 MISO=11 SCK=9 CS=8 DIO1=14 RST=12 BUSY=13.
