# PURR OS — Waveshare 1.69" Flash Guide

**Hardware:** Waveshare ESP32-S3 1.69"  
**Display:** ST7789 240×280 portrait  
**Touch:** CST816S capacitive I2C (SDA=11 SCL=10 INT=46)  
**Flash:** 4 MB  

---

## Flash via esptool (Linux/macOS)

```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 \
  write_flash \
  0x0000  bootloader.bin \
  0x8000  partition-table.bin \
  0x10000 purr_os_core.bin \
  0x300000 spiffs.bin
```

## Flash via esptool (Windows)

```powershell
esptool --chip esp32s3 --port COM8 --baud 921600 ^
  write_flash ^
  0x0000  bootloader.bin ^
  0x8000  partition-table.bin ^
  0x10000 purr_os_core.bin ^
  0x300000 spiffs.bin
```

## Flash via SDK

```bash
python3 SDK/sdk_core.py --target waveshare169 --flash /dev/ttyUSB0
```

---

## Partition map

| Address    | File                  | Description                   |
|------------|-----------------------|-------------------------------|
| `0x0000`   | `bootloader.bin`      | IDF second-stage bootloader   |
| `0x8000`   | `partition-table.bin` | Partition table               |
| `0x9000`   | *(internal NVS)*      | Key-value store               |
| `0x10000`  | `purr_os_core.bin`    | PURR OS (3 MB slot)           |
| `0x300000` | `spiffs.bin`          | SPIFFS filesystem (1 MB)      |

---

## Notes

- ESP32-S3 bootloader goes at `0x0000` (not `0x1000` like classic ESP32).
- Display is portrait-native (240×280) — UI is optimised for this orientation.
- Touch has no hardware reset pin; driver uses a 50 ms power-on delay instead.
- Do not flash `ota_data_initial.bin` — OTA was removed in v0.10.1.
