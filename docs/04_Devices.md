# PURR OS — Devices

## device.pcat Format

Each supported device has a `device.pcat` at `source/devices/<slug>/device.pcat`. This is a TOML-like manifest describing everything `purrstrap` needs to build for that device.

```ini
[device]
name      = "human readable name"
chip      = "esp32" | "esp32s3"
flash_mb  = 4 | 8 | 16
psram     = true | false
psram_mb  = 8
spiffs_kb = 256 | 512 | 1024   # SPIFFS partition size

[drivers]
display = "ili9341" | "st7789" | "axs15231b" | "ssd1306" | ""
touch   = "xpt2046" | "cst816s" | "gt911" | ""
input   = "trackball" | ""
radio   = "sx1262" | "sx1276" | ""
gps     = "generic_nmea" | ""

[radio]
wifi = true | false   # built-in silicon WiFi
bt   = true | false   # built-in silicon Bluetooth
lora = "sx1262" | "sx1276" | ""   # SPI LoRa module slug

[modules]
ui          = "kittenui" | "miniwin" | "oled_ui"
app_manager = "app_manager"

[ui]
theme = "wce" | "luna" | "dark"   # KittenUI/MiniWin only
style = "full" | "compact"        # oled_ui only

[apps]
# true = bake into SPIFFS flash image at build time
settings   = true | false
about      = true | false
terminal   = true | false
fileman    = true | false
calculator = true | false

[flash]
# Maps module/driver slug to load priority (1=required, 2=important, 3=optional)
display/ili9341 = 1
touch/xpt2046   = 1
kittenui        = 2
app_manager     = 2
apps/settings   = 3
apps/terminal   = 3

[pins]
# All GPIO assignments for this device.
# Use -1 for not connected / not applicable.
display_cs   = 15
display_bl   = 27
touch_sda    = 33
...
```

purrstrap reads this file to:
1. Determine which driver blobs to include in the SPIFFS image
2. Generate `purr_device_glue.c` with pin #defines and radio capability flags
3. Set `sdkconfig` chip, flash, PSRAM targets
4. Decide which system apps to bake in

---

## Supported Devices

### jc3248w535

```
Chip:    ESP32-S3
Flash:   16 MB
PSRAM:   8 MB
Screen:  3.5" AXS15231B 480x320 QSPI
Touch:   AXS15231B integrated capacitive (I2C)
Radio:   WiFi + BT (built-in)
SD:      no
UI:      MiniWin
Apps:    settings, about, terminal, fileman, calculator
```

The largest display in the lineup. 8 MB PSRAM makes it the best candidate for
MagicMac and MagiDOS once those rewrites are complete. Uses QSPI (4-bit parallel)
instead of SPI — the AXS15231B driver uses ESP-IDF's panel IO QSPI interface.

---

### tdeck_plus

```
Chip:    ESP32-S3
Flash:   16 MB
PSRAM:   8 MB
Screen:  3.2" ST7789 320x240 SPI
Touch:   GT911 capacitive (I2C)
Input:   Trackball (4-dir GPIO + click)
Radio:   WiFi + BT (built-in) + SX1276 LoRa (SPI)
GPS:     generic NMEA UART (TX=43, RX=44)
SD:      yes
UI:      KittenUI
Apps:    settings, about, terminal, fileman, calculator
```

Full-featured field device. LoRa, GPS, trackball, SD, keyboard. SX1276 shares
the SPI bus with the display via a separate CS pin. GT911 touch requires INT and
RST driven during init. GPS background task starts immediately on boot but valid
fix takes 20-60 seconds in open sky.

---

### tdeck

```
Chip:    ESP32-S3
Flash:   16 MB
PSRAM:   none
Screen:  3.2" ST7789 320x240 SPI
Touch:   none
Input:   Trackball (4-dir GPIO + click)
Radio:   WiFi + BT (built-in) + SX1262 LoRa (SPI)
SD:      yes
UI:      KittenUI
Apps:    settings, about, terminal, fileman, calculator
```

Same form factor as tdeck_plus but no touch and SX1262 instead of SX1276.
Navigation via trackball only — KittenUI keyboard and touch events are not
available; apps should handle trackball focus navigation via `catcall_input_t`.

---

### cyd

```
Chip:    ESP32
Flash:   4 MB
PSRAM:   none
Screen:  2.8" ILI9341 320x240 SPI
Touch:   XPT2046 resistive (SPI, shared bus)
Radio:   WiFi + BT (built-in)
SD:      yes (optional SPI)
UI:      KittenUI
Apps:    settings, about, terminal, fileman, calculator
```

The original "Cheap Yellow Display". XPT2046 resistive touch shares the SPI bus
with the display; separate CS pin. Resistive touch requires slightly more pressure
than capacitive — tap targets should be at least 24px.

---

### cyd_s024c

```
Chip:    ESP32
Flash:   4 MB
PSRAM:   none
Screen:  2.4" ILI9341-compat 240x320 SPI
Touch:   CST816S capacitive (I2C)
Radio:   WiFi + BT (built-in)
SD:      yes (optional SPI)
UI:      KittenUI
Apps:    settings, about, terminal, fileman, calculator
```

**Critical pin note:** Backlight is GPIO **27**, not GPIO 21. This was a common
source of confusion in v0.11. The device.pcat has the correct value baked in.

---

### cyd_s028r

```
Chip:    ESP32
Flash:   4 MB
PSRAM:   none
Screen:  2.8" ILI9341 320x240 SPI
Touch:   XPT2046 resistive (SPI, shared bus)
Radio:   WiFi + BT (built-in)
SD:      yes (optional SPI)
UI:      KittenUI
Apps:    settings, about, terminal, fileman, calculator
```

Same panel as cyd but with a portrait-flip MADCTL issue. The driver applies
`display_madctl = 0x40` automatically from device.pcat.

---

### heltec

```
Chip:    ESP32-S3
Flash:   8 MB
PSRAM:   none
Screen:  128x64 SSD1306 OLED (I2C)
Touch:   none
Radio:   WiFi + BT (built-in) + SX1262 LoRa (SPI)
SD:      no
UI:      oled_ui (text-mode only)
Apps:    none (screen too small)
```

LoRa-first device. No GUI apps — the oled_ui module provides a text-mode status
display with a 6x8 bitmap font. Text-only terminal access and radio tools work
via SD card. The oled_ui module exposes `oled_ui_log(const char *line)` for other
modules to print status lines.

---

### waveshare169

```
Chip:    ESP32-S3
Flash:   4 MB
PSRAM:   none
Screen:  1.69" ST7789 240x280 SPI
Touch:   CST816S capacitive (I2C)
Radio:   WiFi + BT (built-in)
SD:      no
UI:      KittenUI
Apps:    none (screen below medium threshold)
```

Small wearable/badge device. KittenUI loads so custom .meow/.paws scripts work,
but the built-in system apps are excluded because 240x280 is too small to lay
them out usably. Use .meow scripts on the SD card for small-screen UIs.

---

## Screen Size Classification

purrstrap uses this classification to determine the default app bundle:

| Class | Resolution | Example devices | Apps bundled |
|-------|-----------|-----------------|--------------|
| Small | < 240x240 | heltec (128x64) | none |
| Small-medium | 240x280 | waveshare169 | none |
| Medium | 240x320 – 320x240 | cyd*, cyd_s024c, cyd_s028r | all 5 |
| Large | 320x240+ | tdeck, tdeck_plus, jc3248w535 | all 5 |

You can override this by setting `apps.* = true | false` in device.pcat.

---

## Adding a New Device

1. Create `source/devices/<slug>/device.pcat`
2. Fill in `[device]`, `[drivers]`, `[radio]`, `[modules]`, `[ui]`, `[apps]`, `[flash]`, `[pins]`
3. Run `purrstrap list` — your device should appear
4. Run `purrstrap build <slug>` — purrstrap generates glue and assembles the image

If your display or touch driver does not exist yet, see [05_Drivers.md](05_Drivers.md).

---

## Pin Assignment Reference

### Display — SPI

| Key | Description |
|-----|-------------|
| `display_cs` | Chip select |
| `display_dc` | Data/command |
| `display_mosi` | SPI MOSI |
| `display_sclk` | SPI clock |
| `display_rst` | Reset (-1 if not wired) |
| `display_bl` | Backlight PWM/GPIO |
| `display_madctl` | MADCTL override byte (optional, for panel orientation fixes) |

### Display — QSPI (AXS15231B)

| Key | Description |
|-----|-------------|
| `display_cs` | Chip select |
| `display_pclk` | Pixel clock |
| `display_d0`-`d3` | 4-bit data lines |
| `display_bl` | Backlight |
| `display_rst` | Reset (-1 if not wired) |

### Touch — I2C

| Key | Description |
|-----|-------------|
| `touch_sda` | I2C data |
| `touch_scl` | I2C clock |
| `touch_int` | Interrupt (-1 if unused) |
| `touch_rst` | Reset (-1 if not wired) |

### Touch — SPI (XPT2046)

| Key | Description |
|-----|-------------|
| `touch_cs` | Chip select (shares SPI bus with display) |
| `touch_miso` | SPI MISO |
| `touch_mosi` | SPI MOSI (shared) |
| `touch_sclk` | SPI clock (shared) |
| `touch_irq` | Touch interrupt |

### Input — Trackball

| Key | Description |
|-----|-------------|
| `trackball_up` | Up direction GPIO |
| `trackball_down` | Down direction GPIO |
| `trackball_left` | Left direction GPIO |
| `trackball_right` | Right direction GPIO |
| `trackball_click` | Click button GPIO |

### Radio — LoRa (SX1262/SX1276)

| Key | Description |
|-----|-------------|
| `lora_mosi` | SPI MOSI |
| `lora_miso` | SPI MISO |
| `lora_sclk` | SPI clock |
| `lora_cs` | Chip select |
| `lora_rst` | Reset |
| `lora_irq` | DIO0/IRQ interrupt |
| `lora_busy` | BUSY pin (SX1262 only) |

### GPS — UART

| Key | Description |
|-----|-------------|
| `gps_tx` | GPS TX -> ESP RX |
| `gps_rx` | ESP TX -> GPS RX |

### SD Card — SPI

| Key | Description |
|-----|-------------|
| `sd_cs` | Chip select |
| `sd_mosi` | SPI MOSI |
| `sd_miso` | SPI MISO |
| `sd_sclk` | SPI clock |
| `sd_enabled` | `true` to mount SD at boot |
