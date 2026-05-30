cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(purr_os_core)

# ── Build knobs ───────────────────────────────────────────────────────────────
# Passed via build.sh / Build.ps1, or manually: idf.py -DTARGET_DEVICE=heltec ...
set(TARGET_DEVICE  "heltec" CACHE STRING "Target device: heltec | cyd | tdeck")
set(BUILD_MINI     0        CACHE STRING "Mini build: 1 = strip MicroPython")

# ── Optional module toggles ───────────────────────────────────────────────────
# 1 = compile in, 0 = strip entirely. Build scripts set these; override with -D flags.
set(PURR_ENABLE_BT      1 CACHE STRING "Include Bluetooth manager")
set(PURR_ENABLE_MTP     0 CACHE STRING "Include MTP USB manager")
set(PURR_ENABLE_FLASHER 0 CACHE STRING "Include OTA partition flasher")

# LoRa: auto-enabled for heltec/tdeck, off for cyd. Override with -DPURR_ENABLE_LORA=0.
if(TARGET_DEVICE STREQUAL "heltec" OR TARGET_DEVICE STREQUAL "tdeck")
    set(_lora_default 1)
else()
    set(_lora_default 0)
endif()
set(PURR_ENABLE_LORA ${_lora_default} CACHE STRING "Include LoRa radio manager")

message(STATUS "PURR OS  target=${TARGET_DEVICE}  mini=${BUILD_MINI}  bt=${PURR_ENABLE_BT}  lora=${PURR_ENABLE_LORA}  mtp=${PURR_ENABLE_MTP}  flasher=${PURR_ENABLE_FLASHER}")

# ── Core sources (all targets) ────────────────────────────────────────────────
set(SRCS
    "system/kernel/main.cpp"
    "system/kernel/kitt.cpp"
    "system/kernel/device_config.cpp"
    "system/kernel/modules/wifi_manager.cpp"
    "system/kernel/modules/power_manager.cpp"
    "system/bridge/main.cpp"
    "system/system/main.cpp"
)

set(INCLUDE_DIRS
    "system/kernel"
    "system/kernel/modules"
    "system/bridge"
    "system/system"
)

set(REQUIRES
    arduino
    ArduinoJson
    esp_wifi
    nvs_flash
    spiffs
    esp_adc
    app_update
)

# ── Bluetooth (optional) ──────────────────────────────────────────────────────
if(PURR_ENABLE_BT)
    list(APPEND SRCS "system/kernel/modules/bt_manager.cpp")
    list(APPEND REQUIRES bt)
endif()

# ── LoRa (optional, auto from target) ────────────────────────────────────────
if(PURR_ENABLE_LORA)
    list(APPEND SRCS "system/kernel/modules/lora_manager.cpp")
endif()

# ── MTP USB (optional) ────────────────────────────────────────────────────────
if(PURR_ENABLE_MTP)
    list(APPEND SRCS "system/kernel/modules/mtp_manager.cpp")
endif()

# ── OTA Flasher (optional) ────────────────────────────────────────────────────
if(PURR_ENABLE_FLASHER)
    list(APPEND SRCS "system/kernel/modules/flasher.cpp")
endif()

# ── Device-specific: Heltec ───────────────────────────────────────────────────
if(TARGET_DEVICE STREQUAL "heltec")
    list(APPEND SRCS
        "system/kernel/modules/display_ssd1306.cpp"
        "apps/smol/smol.cpp"
    )
    list(APPEND INCLUDE_DIRS "apps/smol")

# ── Device-specific: CYD ─────────────────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "cyd")
    list(APPEND SRCS
        "system/kernel/modules/display_ili9341.cpp"
        "system/kernel/modules/touch_xpt2046.cpp"
        "system/kernel/modules/partition_manager.cpp"
        "apps/launcher/launcher.cpp"
    )
    list(APPEND INCLUDE_DIRS "apps/launcher")
    list(APPEND REQUIRES lvgl fatfs esp_partition)

# ── Device-specific: T-Deck ──────────────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "tdeck")
    list(APPEND SRCS
        "system/kernel/modules/display_ssd1306.cpp"
        "apps/smol/smol.cpp"
    )
    list(APPEND INCLUDE_DIRS "apps/smol")
    message(WARNING "T-Deck: placeholder build — ST7789 driver and BB6 shell not yet wired.")

else()
    message(FATAL_ERROR "Unknown TARGET_DEVICE='${TARGET_DEVICE}'. Valid: heltec, cyd, tdeck")
endif()

# ── Full build: MicroPython runtime ───────────────────────────────────────────
if(NOT BUILD_MINI)
    list(APPEND SRCS
        "system/micropython/mpython_runtime.cpp"
        "system/micropython/kitt_module.c"
    )
    list(APPEND INCLUDE_DIRS "system/micropython")
    list(APPEND REQUIRES micropython)
else()
    message(STATUS "Mini build: MicroPython excluded.")
endif()

# ── Register component ────────────────────────────────────────────────────────
idf_component_register(
    SRCS         ${SRCS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES     ${REQUIRES}
)

# ── Compile-time feature flags ────────────────────────────────────────────────
# kitt.cpp gates all hardware calls on these — must mirror SRCS above exactly.
set(PURR_DEFS "")

# Device display + touch
if(TARGET_DEVICE STREQUAL "heltec")
    list(APPEND PURR_DEFS PURR_DISPLAY_SSD1306=1)
elseif(TARGET_DEVICE STREQUAL "cyd")
    list(APPEND PURR_DEFS
        PURR_DISPLAY_ILI9341=1
        PURR_HAS_LVGL=1
        PURR_HAS_TOUCH_XPT2046=1
        PURR_HAS_PARTITION_MGR=1
    )
elseif(TARGET_DEVICE STREQUAL "tdeck")
    list(APPEND PURR_DEFS PURR_DISPLAY_SSD1306=1)
endif()

# Optional modules
if(PURR_ENABLE_BT)
    list(APPEND PURR_DEFS PURR_HAS_BT=1)
endif()
if(PURR_ENABLE_LORA)
    list(APPEND PURR_DEFS PURR_HAS_LORA=1)
endif()
if(PURR_ENABLE_MTP)
    list(APPEND PURR_DEFS PURR_HAS_MTP=1)
endif()
if(PURR_ENABLE_FLASHER)
    list(APPEND PURR_DEFS PURR_HAS_FLASHER=1)
endif()
if(NOT BUILD_MINI)
    list(APPEND PURR_DEFS PURR_HAS_MICROPYTHON=1)
endif()

target_compile_definitions(${COMPONENT_LIB} PUBLIC ${PURR_DEFS})
