# PURR OS — Device Reference

See [04_Flashing.md](04_Flashing.md) for flash offsets, esptool commands, and web flasher instructions for each device.

## T-Deck Plus (`tdeck_plus`)

**Chip:** ESP32-S3  **Flash:** 16MB  **PSRAM:** 8MB

| Peripheral | Interface | Pins |
|-----------|-----------|------|
| Display ST7789 320×240 | SPI3 | MOSI=41 MISO=38 SCLK=40 CS=12 DC=11 RST=13 BL=42 |
| Touch GT911 | I2C | SDA=18 SCL=8 INT=16 RST=15 |
| Keyboard (BB6) | I2C | SDA=18 SCL=8 (shared with GT911) |
| Trackball | GPIO | UP=3 DOWN=7 LEFT=6 RIGHT=5 CLICK=0 |
| SD card | SPI3 | CS=39 (shared bus with display) |
| LoRa SX1262 | SPI2 | — |
| GPS u-blox MIA-M10Q | UART | TX=48 RX=18 (opt) |
| Power enable | GPIO | GPIO 10 — HIGH before SPI init |

**Partition table:** `sdkconfig` — 16MB: bootloader@0x0, ptable@0x8000, ota_data@0xe000, factory@0x10000, spiffs@0xe00000

**First boot:** MiniWin 3-point touch calibration runs, stored to NVS.

**Known hardware notes:**
- I2C bus is shared between GT911 (addr 0x5D) and keyboard (addr 0x55). The GT911 sys_drv is disabled on this target; `hal_touch.cpp` owns the bus.
- GPIO 10 must be HIGH before any SPI peripheral is accessed.
- SD card in SPI mode; IDF patch applied to `sdmmc_io.c` to allow `ESP_ERR_INVALID_SIZE` from non-SDIO card CMD52.

---

## JC3248W535 (`jc3248w535`)

**Chip:** ESP32-S3  **Flash:** 16MB  **PSRAM:** 8MB  
**Display:** ST7796 480×320  **Touch:** GT911 (SDA=19 SCL=20)

MagiDOS and MagicMac enabled in release builds (8MB PSRAM required).

---

## CYD S028R (`cyd_s028r`)

**Chip:** ESP32  **Flash:** 4MB  
**Display:** ILI9341 2.4" 320×240 landscape  **Touch:** XPT2046 SPI resistive  
**Backlight:** GPIO 21  **SD card:** VSPI (MOSI=23 MISO=19 SCLK=18 CS=5)

| Peripheral | Pins |
|-----------|------|
| Display SPI (HSPI) | MOSI=13 MISO=12 SCLK=14 CS=15 DC=2 |
| Touch XPT2046 | T_CS=33 T_MOSI=32 T_MISO=39 T_SCLK=25 T_IRQ=36 |
| SD card (VSPI) | MOSI=23 MISO=19 SCLK=18 CS=5 |

**Display driver notes:** MADCTL=0x40 (MX=1). The PCB routes ILI9341 column 0 to the physical right edge, so MX=1 is required to flip column addressing. `SPI_TRANS_USE_TXDATA` must be used for all ≤4-byte SPI transfers — stack-allocated buffers are inaccessible to DMA and silently corrupt short transfers including MADCTL.

3-point resistive touch calibration runs on first boot, stored in NVS. Erase flash to recalibrate.

---

## CYD S024C (`cyd_s024c`)

**Chip:** ESP32  **Flash:** 4MB  
**Display:** ILI9341 2.4" 240×320 portrait  **Touch:** CST816S I2C (SDA=33 SCL=32)  
**Backlight:** GPIO 27 (not 21 — different from S028R)

---

## CYD Boot (`cyd_boot`)

Factory kernel. Chainloads `ota_0`. Flash this first, then install PURR OS to `ota_0`. Fixed build — no optional modules.

---

## Heltec WiFi LoRa 32 V3 (`heltec`)

**Chip:** ESP32-S3  **Flash:** 8MB  
**Display:** SSD1306 OLED 128×64  **Radio:** SX1262 LoRa  
**Shell:** KittenUI (text-mode, no MiniWin)

LoRa enabled by default. Antenna required before transmitting.

---

## T-Deck (`tdeck`)

**Chip:** ESP32-S3  **Flash:** 16MB  
**Display:** ST7789 320×240  **Input:** trackball + keyboard  **Radio:** SX1262

WIP — KittenUI shell, touch not yet wired.

---

## Waveshare 1.69" (`waveshare169`)

**Chip:** ESP32-S3  **Flash:** 4MB  
**Display:** ST7789 240×280  **Touch:** CST816S I2C (SDA=6 SCL=7)

WIP — verify pin assignments before flashing.

---

## T-Embed CC1101 (`tembed_cc1101`)

**Chip:** ESP32-S3R8  **Flash:** 16MB  **PSRAM:** 8MB  
**Display:** ST7789 170×320  **Input:** rotary encoder (CLK=1 DT=2 SW=0)  
**Radio:** CC1101 sub-GHz (CS=12 MOSI=9 MISO=10 SCK=11 GDO0=38 GDO2=39)  
**Power enable:** GPIO 15

KittenUI shell.

---

## Adding a new device

1. Create `devices/<newdevice>/` with HAL files (copy from `devices/generic/` as template)
2. Add device entry to `CoreOS/main/CMakeLists.txt`:
   - Device-specific SRCS/REQUIRES block
   - `PURR_DEFS` compile flags
3. Add to `ui/lib_miniwin/CMakeLists.txt` device mapping (if MiniWin)
4. Add device entry to `purrstrap.py` `DEVICES` dict
5. Add baked-in config to `CoreOS/system/kernel/device_config.cpp`
6. Add `SDK/targets/<device>.defaults` for sdkconfig defaults
