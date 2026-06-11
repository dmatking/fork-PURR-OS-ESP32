# PURR OS — CYD S024C Flash Guide

**Hardware:** ESP32-2432S024C  
**Display:** ST7789 2.4" 320×240  
**Touch:** CST820 capacitive I2C  
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

## Flash via SDK

```bash
python3 SDK/sdk_core.py --target cyd_s024c --flash /dev/ttyUSB0
```

---

## Partition map

| Address    | File                | Description           |
|------------|---------------------|-----------------------|
| `0x1000`   | `bootloader.bin`    | IDF second-stage bootloader |
| `0x8000`   | `partition-table.bin` | Partition table      |
| `0x10000`  | `purr_os_core.bin`  | PURR OS (3 MB slot)  |
| `0x300000` | `spiffs.bin`        | SPIFFS filesystem (1 MB) |

---

## Notes

- CST820 capacitive touch — no calibration required; touch works immediately.
- Backlight GPIO 27 (differs from S028R which uses GPIO 21).
- No hardware RST pin on display — software reset in init sequence.
- SD card on SPI3 (VSPI): CS=5 MOSI=23 MISO=19 SCLK=18.
