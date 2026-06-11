# PURR OS — CYD S028R Flash Guide

**Hardware:** ESP32-2432S028R  
**Display:** ILI9341 2.4" 320×240  
**Touch:** XPT2046 resistive SPI  
**Flash:** 4 MB  

---

## Flash via esptool (Linux/macOS)

```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
  write_flash \
  0x1000  bootloader.bin \
  0x8000  partition-table.bin \
  0x10000 purr_os_core.bin \
  0x300000 spiffs.bin
```

## Flash via esptool (Windows)

```powershell
esptool --chip esp32 --port COM8 --baud 921600 ^
  write_flash ^
  0x1000  bootloader.bin ^
  0x8000  partition-table.bin ^
  0x10000 purr_os_core.bin ^
  0x300000 spiffs.bin
```

## Flash via SDK

```bash
python3 SDK/sdk_core.py --target cyd_s028r --flash /dev/ttyUSB0
```

---

## Partition map

| Address    | File                | Description           |
|------------|---------------------|-----------------------|
| `0x1000`   | `bootloader.bin`    | IDF second-stage bootloader |
| `0x8000`   | `partition-table.bin` | Partition table      |
| `0x9000`   | *(internal NVS)*    | Key-value store — auto-formatted |
| `0x10000`  | `purr_os_core.bin`  | PURR OS (3 MB slot)  |
| `0x300000` | `spiffs.bin`        | SPIFFS filesystem (1 MB) |

---

## Notes

- **Do not flash** `ota_data_initial.bin` — OTA slots were removed in v0.10.1.
- Touch requires resistive calibration on first boot — tap the crosshairs shown.
- GPIO 0 held at power-on forces the SDK recovery menu.
