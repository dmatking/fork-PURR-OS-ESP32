# PURR OS — T-Deck Flash Guide

**Hardware:** LilyGO T-Deck  
**Display:** ST7789 320×240 SPI  
**Input:** Physical keyboard (QWERTY), trackball  
**Flash:** 16 MB  

---

## Flash via esptool (Linux/macOS)

```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 \
  write_flash \
  0x0000  bootloader.bin \
  0x8000  partition-table.bin \
  0x10000 purr_os_core.bin \
  0xC00000 spiffs.bin
```

## Flash via esptool (Windows)

```powershell
esptool --chip esp32s3 --port COM8 --baud 921600 ^
  write_flash ^
  0x0000  bootloader.bin ^
  0x8000  partition-table.bin ^
  0x10000 purr_os_core.bin ^
  0xC00000 spiffs.bin
```

## Flash via SDK

```bash
python3 SDK/sdk_core.py --target tdeck --flash /dev/ttyUSB0
```

---

## Partition map

| Address    | File                  | Description                   |
|------------|-----------------------|-------------------------------|
| `0x0000`   | `bootloader.bin`      | IDF second-stage bootloader   |
| `0x8000`   | `partition-table.bin` | Partition table               |
| `0x9000`   | *(internal NVS)*      | Key-value store               |
| `0x10000`  | `purr_os_core.bin`    | PURR OS (11 MB slot)          |
| `0xC00000` | `spiffs.bin`          | SPIFFS filesystem (4 MB)      |

---

## Notes

- ESP32-S3 bootloader goes at `0x0000` (not `0x1000` like classic ESP32).
- Trackball directional inputs fire navigation events (UP/DOWN/LEFT/RIGHT/CLICK).
- ANSI arrow key sequences from the keyboard are translated to the same nav codes.
- Do not flash `ota_data_initial.bin` — OTA was removed in v0.10.1.
