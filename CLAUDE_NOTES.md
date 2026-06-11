# Claude Working Notes
> Private scratchpad — plans, findings, decisions, open questions.
> Updated as work progresses. Not user-facing docs.

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

## Device Pin Reference (to be filled during verification)

### CYD ESP32-2432S028R
- Display (HSPI/SPI2): MOSI=13 MISO=12 SCLK=14 CS=15 DC=2 RST=4 BL=21
- Touch XPT2046 (same SPI bus): CS=33, IRQ=36
- SD card (VSPI/SPI3): CS=5 MOSI=23 MISO=19 SCLK=18
- RGB LED: R=4 G=16 B=17 (active low)
- ⚠️ Needs online verification

### CYD ESP32-2432S024C
- Display (HSPI): MOSI=13 MISO=12 SCLK=14 CS=15 DC=2 RST=-1 BL=27
- Touch CST816S (I2C): SDA=33 SCL=32 INT=21 RST=25
- SD card (VSPI): CS=5 SCK=18 MISO=19 MOSI=23
- RGB LED: R=4 G=16 B=17 (active low)
- ⚠️ Needs online verification

### T-Deck Plus
- Display ST7789: to verify
- Touch GT911: to verify
- Keyboard: to verify
- Trackball: to verify
- SD card: to verify

### JC3248W535
- Display ST7796: to verify
- Touch GT911: to verify

### Waveshare 1.69"
- Display ST7789: to verify
- Touch CST816S: to verify

---

## Open Questions
- Is `bblanchon__arduinojson` actually used in system code? (YES — device_config.cpp, kitt.cpp, bridge/main.cpp)
- Does `tinyusb` component exist in IDF 5.3 for esp32 (not s2/s3)? usb_hid is S2/S3 only anyway.
- SD card failure log — need to find in previous session transcript at `/home/Catto/.claude/projects/-home-Catto-Documents-Projects-PURR-OS-ESP32/f6ec2343-786d-4308-b7d6-4cf642c352d5.jsonl`
