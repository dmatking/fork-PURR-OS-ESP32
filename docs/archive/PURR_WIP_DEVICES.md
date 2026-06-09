# PURR OS WIP Device Support — Arduino Removal

## Devices from fork (https://github.com/PastorCatto/PURR-OS-ESP32)

### Touch Drivers Migrated to Pure IDF

- [x] **GT911** (JC3248W535 — ESP32-S3, 3.5" 480x320)
  - Migrated from Arduino Wire to IDF i2c_master
  - GPIO reset/interrupt now use IDF gpio driver
  - File: `touch_gt911.cpp`

- [ ] **MXT336T** (Atmel I2C touch controller)
  - Status: Copied from fork, awaiting IDF migration
  - File: `touch_mxt336t.cpp` (Arduino version)

- [ ] **XPT2046** (CYD SPI touch)
  - Status: Copied from fork, awaiting IDF migration
  - File: `touch_xpt2046.cpp` (Arduino version)

## Next Steps

1. Finish current build (CST816S, display fixes complete)
2. Migrate MXT336T to IDF SPI master
3. Migrate XPT2046 to IDF SPI master
4. Test each touch driver on hardware
5. Add CMakeLists guards for WIP targets (jc3248w535, etc.)
