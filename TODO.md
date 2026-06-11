# PURR OS — Master TODO / Night List

> Last updated: 2026-06-10
> Status legend: [ ] todo  [~] in progress  [x] done  [!] blocked

---

## ⭐ 3. PRIORITY — Arduino Strip-Out + IDF Upgrade
> Strip Arduino fully → unlocks IDF 5.3/5.4+ upgrades. Arduino-ESP32 pins to IDF 5.1.x.
- [x] Fix ILI9341 white screen: missing vendor init cmds (0xEF,0xCF,0xED,0xE8,0xCB,0xF7,0xEA) required by CYD panels; s_ready bug; DMA disabled. Init now matches TFT_eSPI ILI9341_Init.h exactly.
- [x] Strip `espressif__arduino-esp32` from managed_components (deleted from disk)
- [x] Make `lib_tftespi` permanently empty (no-source stub)
- [x] Remove `CONFIG_AUTOSTART_ARDUINO`, `CONFIG_ARDUINO_SELECTIVE_COMPILATION` from all `.defaults`
- [x] Remove `PURR_ARDUINO_AUTOSTART` / `PURR_ARDUINO_COMPONENT` cmake vars + sdk_core.py flags
- [x] Remove `_arduino_patches()` from sdk_core.py
- [x] Always define `app_main` in main.cpp — no Arduino guard
- [x] Rewrite LoRa kernels (sx1262, sx1276) with RadioLib, no Arduino
- [x] Rewrite cc1101_manager.cpp with EspIdfHal, no SPIClass
- [x] Rebuild all 9 device releases after strip — all pass
- [ ] **Flash cyd_s028r and verify ILI9341 display works with IDF-only driver**
- [ ] Bump IDF minimum target to 5.3 in project CMakeLists / idf_component.yml

---

## 1. Hardware Pin Verification
- [x] Cross-reference every device schematic/datasheet online and verify all pin assignments
  - Results dumped to CLAUDE_NOTES.md § Device Pin Reference
  - heltec, cyd_s028r, cyd_s024c, tdeck_plus: verified correct
  - jc3248w535, waveshare169, tembed_cc1101: significant mismatches corrected
- [x] Fix pin mismatches:
  - tembed_cc1101: all display pins corrected (MOSI=9 SCLK=11 CS=41 DC=16 BL=21); CC1101 pins corrected; GPIO15 PowerEN added
  - waveshare169: all display pins corrected; touch CST816S SDA=11 SCL=10 INT=46 RST=-1
  - jc3248w535: GT911 SDA=4 SCL=8 INT=3 corrected; display is QSPI (driver TODO)
  - cyd_s028r: RST=-1 (GPIO4 is RGB LED); XPT2046 already correct (software SPI)
  - cyd_s024c: chip name CST820 noted in CLAUDE_NOTES; code is functionally compatible

---

## 2. Release Builds — All Devices
- [x] cyd_s028r — built + copied to releases/
- [x] cyd_s024c — built + copied to releases/
- [x] cyd_boot — built + copied (fixed: purr_panic.cpp missing, stub return type)
- [x] heltec — built + copied (fixed: PURR_ARDUINO_AUTOSTART for all autostart targets)
- [x] tdeck_plus — built + copied (fixed: GT911 touch branch in shell_cmds_miniwin)
- [x] jc3248w535 — built + copied
- [x] waveshare169 — built + copied (fixed: gpio_num_t cast in hal_touch.cpp)
- [x] tdeck — built + copied (created partitions_tdeck.csv; fixed misleading-indentation)
- [x] tembed_cc1101 — built + copied (fixed: drv_display ST7789 list; include paths)
- [ ] Re-run all 9 builds after Arduino strip (item 3)
- [ ] Write per-device flash README in each releases/ folder

---

## 4. Core OS Bug Audit
- [x] Read through kernel modules and system components for bugs — 3 fixed:
  - kitt.cpp get_touch_event(): NULL guard on out pointer
  - system/main.cpp: nvs_close called with uninitialized handle if nvs_open fails — fixed
  - display_st7789.cpp deinit(): NULL guard on s_panel
- [ ] Verify PURR_DEFS propagation across all targets post-strip
- [ ] Audit drv_touch, drv_shell for correctness across all targets

---

## 5. Documentation Rewrite
- [ ] Rewrite README.md — accurate, show-ready (hardware support, build instructions, feature list)
- [ ] Per-device releases/<device>/README.md with flash instructions
- [ ] Update SDK/SDK_REFERENCE.md for current sdk_core.py interface

---

## 6. App SDK — Custom C Executable Format
- [ ] Design lightweight app binary format (position-independent C executables)
- [ ] SDK headers + linker script so apps compile independently
- [ ] Simple app loader in kernel (load from SPIFFS/SD, relocate, execute)
- [ ] Sample "hello world" app to validate SDK

---

## 7. WASM / WASI Runtime
- [ ] Evaluate WASM runtimes for ESP32 (wasm3, WAMR micro)
- [ ] Add as optional module (PURR_ENABLE_WASM)
- [ ] Expose PURR OS syscalls as WASI imports (display, touch, filesystem, serial)

---

## 8. Luna / Windows XP Theme
- [x] Add PURR_THEME_LUNA alongside wce and blackberry
  - miniwin_config_luna.h with Luna blue (#0049CD active, #7B96C2 inactive)
  - purr_miniwin_colors.h shared theme header across all devices
  - sdk_core.py UI_THEMES updated; CMakeLists PURR_THEME_LUNA=1 flag
- [ ] Title bar gradient (MiniWin has no gradient API yet — needs mw_gl extension)
- [ ] Start button green pill + rounded buttons

---

## 9. Hardware Input — Trackball + Keyboard
- [x] Trackball → arrow key mapping: fires NAV_UP/DOWN/LEFT/RIGHT on press edge always (was debug-only)
- [x] Keyboard ANSI escape sequences: \e[A/B/C/D translated to nav codes for arrow keys
- [x] Enter key wired (both trackball click and \r from keyboard)
- [x] Unified `purr_input_event_t` queue implemented (purr_input.h / purr_input.cpp)
  - KEY, TOUCH_DN/UP/MV, SCROLL event types; post from task or ISR
- [ ] Wire existing HAL drivers to post into purr_input queue (currently separate)

---

## 10. MiniWin Hardware Abstraction
- [ ] Define clean purr_input_t HAL: `{type: KEY/TOUCH/SCROLL, keycode, x, y, delta}`
- [ ] Each device HAL registers input source(s) at init
- [ ] Makes adding new hardware (rotary encoder, gamepad, BT keyboard) a one-file change

---

## 11. SD Card Fix
- [ ] Review SD card failure log (previous session JSONL)
- [ ] Likely SPI bus conflict or wrong CS/clock GPIO
- [ ] Fix and verify on hardware

---

## 12. Desktop Icons — Modular + Configurable
- [x] Registration-based icon system: desktop_icon_register(label, x, y, draw_fn, launch_fn)
- [x] Auto-position at right edge when x==0 && y==0
- [x] Per-icon custom draw callback; generic "P" glyph draw_fn available
- [x] desktop_icons_register_defaults() wired in mw_user_init (tdeck_plus)
- [ ] Drag/reposition support
- [ ] Icon config from JSON on SPIFFS

---

## 13. Version Bump
- [x] PURR OS → 0.10.1 (synced sdk_core.py + purr_version.h)
- [x] KITT → 0.6.9 (bumped from 0.6.8)

---

## 14. Unified Script Runtime (Lua / MicroPython / WASM)
- [ ] Single `purr_script_run(path)` dispatch — extension picks runtime (.lua / .py / .wasm)
- [ ] MicroPython: audit mpython_runtime.cpp, get basic scripts working
- [ ] Expose same KITT API to MicroPython as Lua
- [ ] WASM: wasm3/WAMR-micro as PURR_ENABLE_WASM; WASI imports = KITT APIs
- [ ] `kitt run <file>` shell command

---

## 15. More (TBD)
- [ ] *(additional items to be added)*
