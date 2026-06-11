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
- [ ] Cross-reference every device schematic/datasheet online and verify all pin assignments in `SDK/targets/*.defaults` and device HAL files
  - heltec, tembed_cc1101, cyd / cyd_s028r / cyd_s024c / cyd_boot, tdeck / tdeck_plus, jc3248w535, waveshare169
- [ ] Fix any mismatches found (display, touch, SD, RGB LED, buttons, backlight)

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
- [ ] Read through kernel modules and system components for bugs, dead guards, bad flags
- [ ] Verify PURR_DEFS propagation across all targets post-strip
- [ ] Audit drv_touch, drv_display, drv_shell for correctness across all targets

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
- [ ] Add PURR_THEME_LUNA alongside wce and blackberry
- [ ] Title bar: blue/silver gradient + rounded close/min/max buttons
- [ ] Start button: green pill, taskbar at bottom
- [ ] Extend MiniWin theme hooks to support gradient fills

---

## 9. Hardware Input — Trackball + Keyboard
- [ ] Trackball → arrow key mapping (UP/DOWN/LEFT/RIGHT fed into MiniWin input queue)
- [ ] Keyboard: full audit of T-Deck keyboard driver — keys not registering
- [ ] Enter key: wire to MiniWin button/dialog confirm
- [ ] Unified `purr_input_event_t` queue — all hardware feeds one queue MiniWin consumes

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
- [ ] Make desktop icons moveable/repositionable (drag or config-driven layout)
- [ ] Support per-app custom icon bitmaps
- [ ] Change default app icon from blank to "P", "pp", or "mew" glyph
- [ ] Icon config: load layout + icon paths from JSON on SPIFFS
- [ ] New app icon registration: one-line, not hardcoded coordinates

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
