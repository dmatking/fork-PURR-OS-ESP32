# PURR OS — T-Deck Plus Flash Guide

**Hardware:** LilyGo T-Deck Plus (ESP32-S3)  
**Display:** ST7789 3.5" 320×240  
**Touch:** GT911 capacitive  
**Input:** Physical keyboard (I2C 0x55) + trackball  
**Flash:** 16 MB  

---

## Flash via esptool (Linux/macOS)

```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 \
  write_flash \
  0x0     bootloader.bin \
  0x8000  partition-table.bin \
  0x10000 purr_os_core.bin \
  0xE00000 spiffs.bin
```

## Flash via SDK

```bash
python3 SDK/sdk_core.py --target tdeck_plus --flash /dev/ttyUSB0
```

---

## Partition map

| Address     | File                | Description           |
|-------------|---------------------|-----------------------|
| `0x0`       | `bootloader.bin`    | IDF second-stage bootloader |
| `0x8000`    | `partition-table.bin` | Partition table      |
| `0x10000`   | `purr_os_core.bin`  | PURR OS (14 MB slot) |
| `0xE00000`  | `spiffs.bin`        | SPIFFS filesystem (2 MB) |

---

## Notes

- ESP32-S3 bootloader flashes at `0x0`, not `0x1000` (unlike original ESP32).
- Trackball fires UP/DOWN/LEFT/RIGHT/Enter nav events into MiniWin.
- Physical keyboard: printable chars + ANSI arrow key escape sequences supported.
- SD card slot shares SPI3 bus with display; mount point `/sdcard`.
- GPIO 10 is peripheral power enable — must be driven HIGH before SPI init.
  PURR OS handles this automatically; do not pull GPIO 10 LOW externally.
