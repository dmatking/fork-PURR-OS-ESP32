# PURR OS — T-Embed CC1101 Flash Guide

**Hardware:** LilyGO T-Embed CC1101  
**Display:** ST7789 170×320 SPI  
**Radio:** CC1101 sub-GHz (shares SPI bus with display)  
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
python3 SDK/sdk_core.py --target tembed_cc1101 --flash /dev/ttyUSB0
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
- **GPIO15 (POWER_EN)** must be driven HIGH before display or CC1101 can initialise — the firmware does this automatically on boot.
- Display SPI pins: MOSI=9 SCLK=11 CS=41 DC=16 BL=21.
- CC1101 shares SPI bus (MOSI=9 MISO=10 SCK=11) with CS=12.
- Do not flash `ota_data_initial.bin` — OTA was removed in v0.10.1.
