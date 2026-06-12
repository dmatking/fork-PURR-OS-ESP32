# Claude Working Notes
> Private scratchpad — plans, findings, decisions, open questions.
> Updated as work progresses. Not user-facing docs.
> These are the notes from Claude "Auto" Mode
---

## Session State (2026-06-11)

### Arduino strip-out — COMPLETE
- `managed_components/espressif__arduino-esp32` removed from disk permanently
- `lib_arduino` empty stub, `lib_tftespi` empty stub — both kept for CMake compat
- `lib_radiolib` uses new `EspIdfHal` (pure IDF SPI+GPIO, no Arduino HAL)
- LoRa kernels (sx1262, sx1276) rewritten with RadioLib — no Arduino
- `cc1101_manager.cpp` rewritten with EspIdfHal — no SPIClass
- `app_main` always defined (no Arduino autostart guard)
- All PURR_ARDUINO_* cmake vars and `_arduino_patches()` removed from sdk_core.py
- All 9 device targets build clean post-strip

### ILI9341 white screen — ROOT CAUSE FOUND AND FIXED
Three bugs fixed in `display_ili9341.cpp`:
1. **`s_ready` flag bug**: `fill_rect` guarded on `!s_ready`; moved `s_ready=true` BEFORE fill call
2. **DMA alignment risk**: Changed `SPI_DMA_CH_AUTO` → `SPI_DMA_DISABLED`; gamma arrays (15 bytes stack-allocated) could cause unaligned DMA. Disabled DMA to use FIFO for all transfers
3. **Missing vendor init commands**: TFT_eSPI sends 7 undocumented vendor commands (0xEF, 0xCF, 0xED, 0xE8, 0xCB, 0xF7, 0xEA) required by CYD panels. Our driver was missing them. Also: added 0xF2 (3Gamma disable), 0x26 (gamma curve select), fixed FRMCTR1 byte to 0x13 (100 Hz refresh)
Init sequence now matches TFT_eSPI's ILI9341_Init.h exactly.

### Needs hardware verification
- Flash `cyd_s028r` or `cyd_s024c` and confirm display works with new IDF-only driver
- If still white: check UART log for ILI9341 init errors (esp_log TAG "ili9341")

---

## Work Plan for This Run

### Order of operations
1. [x] Revert Arduino stack — confirmed working
2. [ ] Flash cyd_s028r + confirm display works (user away, pending)
3. [x] Release builds for all 9 devices — all binaries in releases/
4. [ ] Arduino strip-out round 2 (user confirmed: strip fully, newer IDF support)
5. [ ] Fix ILI9341 pure-IDF white screen root cause
6. [ ] Pin verification for all devices
7. [ ] Fix SD card bug
8. [ ] Fix keyboard + trackball input (T-Deck Plus)
9. [ ] MiniWin input HAL refactor
10. [ ] Luna/XP theme scaffold
11. [ ] Desktop icon modularisation
12. [ ] Docs pass
13. [ ] App SDK design sketch

### Break schedule
- BREAK NOW (after release builds chunk) — 30 min
- After Arduino strip-out: 30 min break
- After input HAL + Luna: 30 min break

### Bugs fixed during release builds
- cyd_boot: missing `purr_panic.cpp` in SRCS; `wifi_manager_scan_start` stub returned void vs bool
- heltec + all non-CYD autostart targets: PURR_ARDUINO_AUTOSTART not being set → duplicate app_main. Fixed in sdk_core.py
- tdeck_plus: shell_cmds_miniwin hardcoded to CST816S/XPT2046, no GT911 branch
- waveshare169: gpio_num_t cast missing in hal_touch.cpp
- tdeck: missing partitions_tdeck.csv; misleading-indentation in purr_app.cpp
- tembed_cc1101: not in drv_display ST7789 target list; include paths not propagated to main
- shell_cmds_miniwin: no-touch guard missing → fatal include error on touchless targets
- system/system/main.cpp: missing #include <string.h> for strcmp

### Rate limit strategy
- Do a chunk of work (~45-60 min equivalent)
- Set a 30 min schedule wakeup between major chunks
- Don't run builds back-to-back without breaks

---

## Device Pin Reference — Verified 2026-06-11

Pin verification agent ran against datasheets / community Schematic sources.
Critical mismatches marked ⚠️ WRONG — these caused or will cause silent hardware failure.

### CYD ESP32-2432S028R (ILI9341, 2.8")
- Display (HSPI/SPI2): MOSI=13 MISO=12 SCLK=14 CS=15 DC=2 RST=4¹ BL=21 ✓
- Touch XPT2046: **SEPARATE** SPI bus — NOT shared with display
  - ⚠️ WRONG in code: code shared SPI2 for touch, actual XPT2046 pins:
    CLK=25 MOSI=32 MISO=39 CS=33 IRQ=36
    (IRQ is `GPIO36 = ADC input only, no GPIO output — correct for IRQ`)
- SD card (VSPI/SPI3): CS=5 MOSI=23 MISO=19 SCLK=18 ✓
- RGB LED: R=4 G=16 B=17 (active low) ✓
  - ¹ GPIO4 is also the RGB RED pin — if RST=4 goes LOW it pulses the red LED.
    Display appears to lack a true hardware RST pin — RST should be -1 or tied HIGH.
- ⚠️ TODO: Fix XPT2046 touch driver to init its own separate SPI bus

### CYD ESP32-2432S024C (ST7789, 2.4")
- Display (HSPI): MOSI=13 MISO=12 SCLK=14 CS=15 DC=2 RST=-1 BL=27 ✓
- Touch chip: **CST820** (not CST816S — same protocol, different chip rev)
  - SDA=33 SCL=32 INT=21 RST=25 ✓
- SD card (VSPI): CS=5 SCK=18 MISO=19 MOSI=23 ✓
- RGB LED: R=4 G=16 B=17 (active low) ✓

### T-Deck Plus (ESP32-S3, 16MB)
- Display ST7789 (SPI3): MOSI=41 MISO=38 SCLK=40 CS=12 DC=11 RST=10¹ BL=42 ✓
  - ¹ GPIO10 is also POWER ENABLE for display + peripherals (HIGH = on)
- Touch GT911 (I2C shared bus): SDA=18 SCL=8 INT=46 RST=-1 ✓
- Keyboard ESP32-C3 (I2C 0x55): SDA=18 SCL=8 INT=46 ✓
- Trackball (active LOW): UP=3 DOWN=15 LEFT=1 RIGHT=2 CLICK=0 ✓
  (These are CORRECT in hal_input.cpp — no pin fix needed)
- SD card (SPI3 shared): CS=13 MOSI=41 MISO=38 SCLK=40 ✓

### JC3248W535 (ESP32-S3, 8MB+8MB PSRAM)
- Display ST7796: **QUAD-SPI** (not standard SPI!)
  - ⚠️ WRONG: code uses regular 4-wire SPI driver; quad-SPI needs different init
  - Actual: D0=13 D1=10 D2=11 D3=12 SCLK=40 CS=6 DC=7 RST=17 BL=9
  - GT911 SDA/SCL: ⚠️ WRONG order in code; actual: SDA=4 SCL=8 INT=3 RST=38
- SD card: CS=46 MOSI=13 MISO=11 SCLK=12 (shares quad-SPI GPIOs — SD is SPI mode)

### Waveshare 1.69" (ESP32-S3, 4MB) — NEARLY ALL PINS WRONG
- Display ST7789:
  - ⚠️ Code had: MOSI=35 SCLK=36 CS=34 DC=37 RST=38 BL=45
  - ⚠️ Actual:   MOSI=9  SCLK=18 CS=16 DC=2  RST=3  BL=17
- Touch CST816S (I2C):
  - ⚠️ Code had: SDA=33 SCL=32 INT=21
  - ⚠️ Actual:   SDA=11 SCL=10 INT=46 RST=-1 (no reset pin exposed)
- No SD card slot on this device

### T-Embed CC1101 (ESP32-S3, 8MB) — ALL DISPLAY + CC1101 PINS WRONG
- Display ST7789 (SPI2):
  - ⚠️ Code had: MOSI=3 SCLK=5 CS=4 DC=6 BL=15
  - ⚠️ Actual:   MOSI=9 SCLK=11 CS=41 DC=16 RST=-1 BL=21
- CC1101 (shares SPI2 with display):
  - ⚠️ Code had: MOSI=23 MISO=19 SCK=18 CS=5 GDO0=22 GDO2=21
  - ⚠️ Actual:   MOSI=9 MISO=10 SCK=11 CS=12 GDO0=38 GDO2=39
- PowerEN: GPIO15 HIGH to power both display and CC1101 (must be set in init)

### Heltec WiFi LoRa 32 V3 (ESP32-S3, 8MB)
- Display SSD1306 OLED (I2C): SDA=17 SCL=18 (onboard OLED, addr 0x3C) ✓
- LoRa SX1262: MOSI=10 MISO=11 SCK=9 CS=8 DIO1=14 RST=12 BUSY=13 ✓

### T-Embed (without CC1101) — Not in target list; CC1101 variant used instead

### Summary of status
| Device          | Pins correct? | Fix needed |
|-----------------|---------------|------------|
| cyd_s028r       | Partial        | XPT2046 own SPI bus, RST=-1 |
| cyd_s024c       | ✓ Yes          | Rename CST816S→CST820 in comments |
| tdeck_plus      | ✓ Yes          | None (trackball already correct) |
| jc3248w535      | NO             | Quad-SPI display driver needed; GT911 I2C pin swap |
| waveshare169    | NO             | All pins wrong |
| tembed_cc1101   | NO             | All display+CC1101 pins wrong; PowerEN missing |
| heltec          | ✓ Yes          | None |

---

## Open Questions
- Is `bblanchon__arduinojson` actually used in system code? (YES — device_config.cpp, kitt.cpp, bridge/main.cpp)
- Does `tinyusb` component exist in IDF 5.3 for esp32 (not s2/s3)? usb_hid is S2/S3 only anyway.
- SD card failure log — need to find in previous session transcript at `/home/Catto/.claude/projects/-home-Catto-Documents-Projects-PURR-OS-ESP32/f6ec2343-786d-4308-b7d6-4cf642c352d5.jsonl`
