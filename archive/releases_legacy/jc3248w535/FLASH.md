# PURR OS — JC3248W535 Flash Guide

**Hardware:** JC3248W535  
**Display:** ST7796 3.5" 480×320 (QSPI)  
**Touch:** GT911 capacitive I2C (SDA=4 SCL=8 INT=3)  
**Flash:** 16 MB | **PSRAM:** 8 MB  

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
python3 SDK/sdk_core.py --target jc3248w535 --flash /dev/ttyUSB0
```

---

## Partition map

| Address      | File                  | Description                   |
|--------------|-----------------------|-------------------------------|
| `0x0000`     | `bootloader.bin`      | IDF second-stage bootloader   |
| `0x8000`     | `partition-table.bin` | Partition table               |
| `0x9000`     | *(internal NVS)*      | Key-value store               |
| `0x10000`    | `purr_os_core.bin`    | PURR OS (11 MB slot)          |
| `0xC00000`   | `spiffs.bin`          | SPIFFS filesystem (4 MB)      |

---

## Notes

- ESP32-S3 bootloader goes at `0x0000` (not `0x1000` like classic ESP32).
- MagiDOS is included — requires the 8 MB PSRAM to run x86 emulation.
- GPIO 0 held at power-on forces download mode.
- Do not flash `ota_data_initial.bin` — OTA was removed in v0.10.1.
