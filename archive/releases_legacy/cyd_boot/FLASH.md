# PURR OS — CYD Boot Flash Guide

**Hardware:** ESP32-2432S028R (CYD) running in bootloader-only mode  
**Purpose:** Recovery / OTA host; no display driver active  
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
python3 SDK/sdk_core.py --target cyd_boot --flash /dev/ttyUSB0
```

---

## Partition map

| Address    | File                  | Description                   |
|------------|-----------------------|-------------------------------|
| `0x1000`   | `bootloader.bin`      | IDF second-stage bootloader   |
| `0x8000`   | `partition-table.bin` | Partition table               |
| `0x9000`   | *(internal NVS)*      | Key-value store — boot-try counter |
| `0x10000`  | `purr_os_core.bin`    | PURR Boot (3 MB slot)         |
| `0x300000` | `spiffs.bin`          | SPIFFS filesystem (1 MB)      |

---

## Notes

- Flash this to a CYD before flashing the full cyd_s028r image if you want recovery support.
- The boot-try counter in NVS triggers the recovery menu after 3 failed boots.
- Do not flash `ota_data_initial.bin` — OTA was removed in v0.10.1.
