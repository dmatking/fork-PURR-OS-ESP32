# Devices

---

## Target table

| `TARGET_DEVICE` | Chip | Display | Resolution | Touch | Flash | PSRAM | Status |
|-----------------|------|---------|------------|-------|-------|-------|--------|
| `cyd_s024c` | ESP32 | ILI9341 | 320×240 | CST816S I2C | 4MB | — | ✅ Primary |
| `cyd_s028r` | ESP32 | ILI9341 | 320×240 | XPT2046 SPI | 4MB | — | ✅ Supported |
| `cyd_boot` | ESP32 | ILI9341 | 320×240 | CST816S | 4MB | — | ✅ Factory only |
| `tdeck_plus` | ESP32-S3 | ST7789 | 320×240 | GT911 cap | 16MB | — | ✅ Supported |
| `jc3248w535` | ESP32-S3 | ST7796 | 480×320 | GT911 I2C | 16MB | 8MB | 🚧 WIP |
| `waveshare169` | ESP32-S3 | ST7789 | 240×280 | CST816S | 4MB | — | 🚧 WIP |
| `heltec` | ESP32-S3 | SSD1306 | 128×64 | — | 8MB | — | ✅ LoRa / KittenUI |
| `tembed_cc1101` | ESP32-S3 | ST7789 | 170×320 | — | 16MB | 8MB | ✅ CC1101 / KittenUI |
| `tdeck` | ESP32-S3 | ST7789 | 320×240 | trackball | 16MB | — | 🚧 No touch |

---

## CYD pin assignments

### CYD S024C (ESP32-2432S024C) — primary target

| Signal | GPIO |
|--------|------|
| LCD SPI (HSPI/SPI2) MOSI | 13 |
| LCD SPI CLK | 14 |
| LCD SPI CS | 15 |
| LCD DC | 2 |
| LCD BL (backlight) | 27 |
| LCD RST | 12 |
| Touch I2C SDA (CST816S) | 33 |
| Touch I2C SCL | 32 |
| Touch INT | 21 |
| SD SPI (VSPI/SPI3) MOSI | 23 |
| SD SPI MISO | 19 |
| SD SPI CLK | 18 |
| SD SPI CS | 5 |
| RGB LED R | 4 |
| RGB LED G | 16 |
| RGB LED B | 17 |

### CYD S028R (ESP32-2432S028R)

| Signal | GPIO |
|--------|------|
| LCD (same as S024C) | — |
| Touch SPI MOSI (XPT2046) | 32 |
| Touch SPI MISO | 39 |
| Touch SPI CLK | 25 |
| Touch SPI CS | 33 |
| LCD BL | 21 |

---

## Device JSON format

Each device has a JSON config in `CoreOS/system/kernel/devices/<target>.json`, mounted on SPIFFS and loaded at boot by `device_config_load()`.

```json
{
  "device":       "CYD-S024C",
  "display":      "ILI9341",
  "display_w":    320,
  "display_h":    240,
  "touch":        "CST816S",
  "psram":        false,
  "flash_mb":     4,
  "ram_kb":       320,
  "psram_mb":     0,
  "wifi":         true,
  "bt":           true,
  "lora":         false,
  "sd":           true,
  "pi_slot":      false,
  "cpu_max_mhz":  240,
  "lora_region":  "",
  "verbose_boot": false,
  "boot_splash":  "",
  "keymap":       ""
}
```

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `device` | string | Human-readable device label |
| `display` | string | Controller chip (ILI9341, ST7789, ST7796, SSD1306) |
| `display_w/h` | int | Native resolution in pixels |
| `touch` | string | Touch controller (CST816S, XPT2046, GT911, none) |
| `psram` | bool | Has external PSRAM |
| `flash_mb` | int | Flash size |
| `ram_kb` | int | Internal SRAM in KB |
| `psram_mb` | int | PSRAM size (0 if none) |
| `wifi` | bool | WiFi enabled at runtime |
| `bt` | bool | Bluetooth enabled at runtime |
| `lora` | bool | LoRa radio present |
| `sd` | bool | SD card present and should be mounted |
| `pi_slot` | bool | Has Raspberry Pi expansion slot |
| `cpu_max_mhz` | int | Maximum CPU frequency |
| `lora_region` | string | ISM band string (e.g. "EU868") |
| `verbose_boot` | bool | Print all boot steps to serial |

---

## Adding a new device

1. Create `devices/<yourdevice>/` with: `hal_lcd.cpp`, `hal_touch.cpp`, `hal_timer.cpp`, `hal_delay.cpp`, `hal_non_vol.cpp`, `miniwin_config.h`, `purr_app.cpp`
2. Add mapping in `CoreOS/components/lib_miniwin/CMakeLists.txt`:
   ```cmake
   elseif(TARGET_DEVICE STREQUAL "yourdevice")
       set(PURR_MW_DEVICE "yourdevice")
   ```
3. Add target entry in `SDK/sdk_core.py` `TARGETS` dict
4. Add display/touch feature defines in `main/CMakeLists.txt` PURR_DEFS block
5. Create `CoreOS/system/kernel/devices/yourdevice.json`
6. Add `TARGET_DEVICE` to `else()` error message in `main/CMakeLists.txt`
