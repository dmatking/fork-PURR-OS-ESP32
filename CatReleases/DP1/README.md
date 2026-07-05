# PURR OS — DP1 (Developer Preview 1)

Full-stack build of all 10 supported devices, produced after the UI list-widget /
MiniWin-fixes / sdkconfig-generator pass (see `CHANGELOG.md` v1.0.0-dp).

Every device folder contains **both** forms of the image:

- **Split images** — flash each at its own offset: `bootloader.bin` (0x0 / 0x1000
  depending on chip), `partition-table.bin` (0x8000), `firmware.bin` (per
  partition table), `flash.bin` (SPIFFS, at the `spiffs_offset` listed in
  `manifest.json` for that device).
- **One image** — `PURR_OS_<device>.bin`, pre-merged with `esptool merge_bin`,
  flash it alone at offset `0x0`.

`manifest.json` records chip, kernel_type, UI backend, flash size, SPIFFS
offset, and file sizes for all 10 devices in one place.

## Flashing the merged image (recommended)

```bash
esptool.py -p <PORT> write_flash 0x0 PURR_OS_<device>.bin
```

## Flashing split images

```bash
esptool.py -p <PORT> write_flash \
  0x0     bootloader.bin \
  0x8000  partition-table.bin \
  0x10000 firmware.bin \
  <spiffs_offset from manifest.json>  flash.bin
```

(Bootloader offset is 0x0 for ESP32-S3 devices and 0x1000 for plain ESP32
devices in this project's partition layout — check `CoreOS/partitions_*mb.csv`
if unsure.)

## Devices in this release

| Device | Chip | UI backend | Flash |
|---|---|---|---|
| cyd | esp32 | kittenui | 4 MB |
| cyd_s024c | esp32 | kittenui | 4 MB |
| cyd_s028r | esp32 | kittenui | 4 MB |
| heltec | esp32s3 | oled_ui | 8 MB |
| jc3248w535 | esp32s3 | miniwin | 16 MB |
| tdeck | esp32s3 | miniwin | 16 MB |
| tdeck_plus | esp32s3 | miniwin | 16 MB |
| tdeck_plus_arduino | esp32s3 (arduino kernel) | miniwin | 16 MB |
| tdeck_plus_test | esp32s3 (arduino kernel) | (input test mode, no UI) | 16 MB |
| waveshare169 | esp32s3 | kittenui | 4 MB |

## Verification status

All 10 devices completed a full `idf.py build` + `esptool merge_bin` against
ESP-IDF v5.3.5. `cyd` and `tdeck_plus` were spot-checked against expected
`purrstrap generate --check` output beforehand. No hardware-in-the-loop testing
has been done for this batch except where separately noted for `tdeck_plus`.
