# ── Build knobs ───────────────────────────────────────────────────────────────
# Passed via SDK.ps1 / Build.ps1, or manually:
#   idf.py -DTARGET_DEVICE=cyd -DPURR_SHELL=blackberry ...
#
# Note: IDF runs cmake twice per set-target — a bare first pass for IDF setup
# (no user -D flags), then a full second pass with all user flags applied.
# The STATUS message below will appear twice; the second print is authoritative.
# set(... CACHE STRING) without FORCE is correct: command-line -D always wins.

set(TARGET_DEVICE  "heltec" CACHE STRING "Target: heltec | cyd | cyd_boot | tdeck")
set(BUILD_MINI     0        CACHE STRING "1 = strip MicroPython runtime")

# CYD display variant (cyd target only)
#   s028r  — ESP32-2432S028R (original, v0.4.0/v0.5.0) with XPT2046 touch
#   s024c  — ESP32-2432S024C (newer) with CST816S I2C touch
set(CYD_DISPLAY_VARIANT "s028r" CACHE STRING "CYD variant: s028r | s024c")

# UI shell selection (cyd and tdeck only).
#   both       — BlackberryUI preferred, Explorer as fallback  [default]
#   blackberry — BlackberryUI only
#   explorer   — Explorer Windows CE/PDA shell only
#   smol       — Smol OLED shell only
#   none       — headless, no UI shell compiled
set(PURR_SHELL "both" CACHE STRING "UI shell: both | blackberry | explorer | smol | none")

# ── Optional module toggles ───────────────────────────────────────────────────
set(PURR_ENABLE_BT      1 CACHE STRING "Include Bluetooth manager")
set(PURR_ENABLE_MTP     0 CACHE STRING "Include MTP USB manager")
set(PURR_ENABLE_FLASHER 0 CACHE STRING "Include OTA partition flasher")

# LoRa: auto-enabled for heltec/tdeck, off for cyd/cyd_boot. Override with -DPURR_ENABLE_LORA=0.
if(TARGET_DEVICE STREQUAL "heltec" OR TARGET_DEVICE STREQUAL "tdeck")
    set(_lora_default 1)
else()
    set(_lora_default 0)
endif()
set(PURR_ENABLE_LORA ${_lora_default} CACHE STRING "Include LoRa radio manager")

# Mesh: requires LoRa. Defaults off; enable with -DPURR_ENABLE_MESH=1.
set(PURR_ENABLE_MESH 0 CACHE STRING "Include Meshtastic mesh stack (requires LoRa)")

message(STATUS "PURR OS  target=${TARGET_DEVICE}  shell=${PURR_SHELL}  mini=${BUILD_MINI}  bt=${PURR_ENABLE_BT}  lora=${PURR_ENABLE_LORA}  mesh=${PURR_ENABLE_MESH}  mtp=${PURR_ENABLE_MTP}  flasher=${PURR_ENABLE_FLASHER}")

# ── Core sources (all targets) ────────────────────────────────────────────────
# Paths are relative to this file (CoreOS/main/), so prefix ../ to reach project root.
set(SRCS
    "../system/Arduino.cpp"
    "../system/kernel/main.cpp"
    "../system/kernel/kitt.cpp"
    "../system/kernel/device_config.cpp"
    "../system/kernel/idf_compat.c"
    "../system/kernel/modules/wifi_manager.cpp"
    "../system/kernel/modules/power_manager.cpp"
    "../system/bridge/main.cpp"
    "../system/system/main.cpp"
)

set(INCLUDE_DIRS
    "../system"
    "../system/kernel"
    "../system/kernel/modules"
    "../system/bridge"
    "../system/system"
    "../components/TFT_eSPI"
)

set(REQUIRES
    bblanchon__arduinojson
    esp_wifi
    nvs_flash
    spiffs
    esp_adc
    app_update
    esp_timer
    TFT_eSPI
    driver
)

# ── Bluetooth (optional) ──────────────────────────────────────────────────────
if(PURR_ENABLE_BT)
    list(APPEND SRCS "../system/kernel/modules/bt_manager.cpp")
    list(APPEND REQUIRES bt)
endif()

# ── LoRa (optional, auto from target) ────────────────────────────────────────
if(PURR_ENABLE_LORA)
    list(APPEND SRCS "../system/kernel/modules/lora_manager.cpp")
    list(APPEND REQUIRES radiolib)
endif()

# ── Mesh / Meshtastic (optional, requires LoRa) ───────────────────────────────
if(PURR_ENABLE_MESH)
    if(NOT PURR_ENABLE_LORA)
        message(FATAL_ERROR "PURR_ENABLE_MESH=1 requires PURR_ENABLE_LORA=1")
    endif()
    list(APPEND SRCS "../system/kernel/modules/purr_mesh.cpp")
    list(APPEND REQUIRES nanopb meshtastic_pb mbedtls)
    message(STATUS "purr_mesh: Meshtastic-compatible native mesh stack enabled.")
endif()

# ── MTP USB (optional) ────────────────────────────────────────────────────────
if(PURR_ENABLE_MTP)
    list(APPEND SRCS "../system/kernel/modules/mtp_manager.cpp")
endif()

# ── OTA Flasher (optional) ────────────────────────────────────────────────────
if(PURR_ENABLE_FLASHER)
    list(APPEND SRCS "../system/kernel/modules/flasher.cpp")
endif()

# ── Device-specific: Heltec ───────────────────────────────────────────────────
if(TARGET_DEVICE STREQUAL "heltec")
    list(APPEND SRCS
        "../system/kernel/modules/display_ssd1306.cpp"
        "../apps/smol/smol.cpp"
    )
    list(APPEND INCLUDE_DIRS "../apps/smol")

# ── Device-specific: CYD variants (S028R and S024C) ─────────────────────────
elseif(TARGET_DEVICE STREQUAL "cyd_s028r" OR TARGET_DEVICE STREQUAL "cyd_s024c")
    # Map variant targets to generic cyd with variant flag set
    set(CMAKE_TARGET "cyd")
    if(TARGET_DEVICE STREQUAL "cyd_s028r")
        set(CYD_DISPLAY_VARIANT "s028r")
    else()
        set(CYD_DISPLAY_VARIANT "s024c")
    endif()

    list(APPEND SRCS
        "../system/kernel/modules/display_ili9341.cpp"
        "../system/kernel/modules/partition_manager_stubs.cpp"
        "../system/kernel/modules/purr_wm.cpp"
        "../system/kernel/modules/blackberry_ui.cpp"
        "../system/kernel/modules/explorer.cpp"
        "../system/kernel/modules/classicmac.cpp"
        "../system/kernel/modules/ui_stubs.cpp"
    )

    if(CYD_DISPLAY_VARIANT STREQUAL "s024c")
        list(APPEND SRCS "../system/kernel/modules/touch_cst816s.cpp")
        message(STATUS "CYD variant: S024C (CST816S I2C touch)")
    else()
        list(APPEND SRCS "../system/kernel/modules/touch_xpt2046.cpp")
        message(STATUS "CYD variant: S028R (XPT2046 SPI touch)")
    endif()

    list(APPEND REQUIRES fatfs esp_partition esp_lcd driver esp_timer nvs_flash sdmmc miniwinwm lvgl/lvgl)
    list(APPEND PURR_DEFS PURR_HAS_LVGL=1)

# ── Device-specific: CYD (full OS — goes in ota_0) ───────────────────────────
elseif(TARGET_DEVICE STREQUAL "cyd")
    list(APPEND SRCS
        "../system/kernel/modules/display_ili9341.cpp"
        "../system/kernel/modules/partition_manager_stubs.cpp"
        "../system/kernel/modules/purr_wm.cpp"
        "../system/kernel/modules/blackberry_ui.cpp"
        "../system/kernel/modules/explorer.cpp"
        "../system/kernel/modules/classicmac.cpp"
        "../system/kernel/modules/ui_stubs.cpp"
        # Lua deferred — 32-bit preprocessor issue to resolve separately
        # "../system/kernel/modules/lua_runtime.cpp"
    )

    # Select touch driver based on variant (default s028r for original hardware)
    set(_CYD_VARIANT "${CYD_DISPLAY_VARIANT}")
    if(NOT _CYD_VARIANT)
        set(_CYD_VARIANT "s028r")
    endif()

    if(_CYD_VARIANT STREQUAL "s024c")
        list(APPEND SRCS "../system/kernel/modules/touch_cst816s.cpp")
        message(STATUS "CYD variant: S024C (CST816S I2C touch)")
    else()
        list(APPEND SRCS "../system/kernel/modules/touch_xpt2046.cpp")
        message(STATUS "CYD variant: S028R (XPT2046 SPI touch)")
    endif()

    list(APPEND REQUIRES fatfs esp_partition esp_lcd driver esp_timer nvs_flash sdmmc miniwinwm lvgl/lvgl)
    list(APPEND PURR_DEFS PURR_HAS_LVGL=1)

# ── Device-specific: CYD bootloader (factory partition — minimal recovery image) ──
elseif(TARGET_DEVICE STREQUAL "cyd_boot")
    # Override base SRCS and REQUIRES — no WiFi, no BT, no OS modules.
    # This image lives in the factory partition and is never overwritten by OTA.
    set(SRCS
        "../system/kernel/main.cpp"
        "../system/kernel/kitt.cpp"
        "../system/kernel/device_config.cpp"
        "../system/kernel/idf_compat.c"
        "../system/bridge/main.cpp"
        "../system/system/main.cpp"
        "../system/kernel/modules/display_ili9341.cpp"
        "../system/kernel/modules/touch_cst816s.cpp"
        "../system/kernel/modules/partition_manager_stubs.cpp"
        "../system/kernel/modules/purr_bootloader.cpp"
        "../system/kernel/modules/stub_managers.cpp"
    )
    set(REQUIRES
        bblanchon__arduinojson
        nvs_flash
        spiffs
        esp_partition
        app_update
        esp_timer
        esp_lcd
        driver
    )
    # miniwinwm excluded: cyd_boot draws directly to the display.
    # Adding MiniWin would require mw_user_init/paint/message stubs and adds
    # significant code size to a minimal recovery image.
    # Always mini — no MicroPython in the bootloader image
    set(BUILD_MINI 1 CACHE STRING "" FORCE)
    message(STATUS "cyd_boot: minimal factory bootloader image (no WiFi/BT/OS modules)")

# ── Device-specific: T-Deck ──────────────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "tdeck")
    set(BUILD_TDECK_PLUS 0 CACHE STRING "T-Deck variant: 0=original, 1=Plus (GPS)")
    list(APPEND SRCS
        "../system/kernel/modules/display_ssd1306.cpp"
        "../apps/smol/smol.cpp"
        "../system/kernel/modules/blackberry_ui.cpp"
    )
    list(APPEND INCLUDE_DIRS "../apps/smol")
    if(BUILD_TDECK_PLUS)
        list(APPEND SRCS "../system/kernel/modules/gps_manager.cpp")
        message(STATUS "T-Deck Plus: MIA-M10Q GPS stub included.")
    endif()
    message(WARNING "T-Deck: placeholder build -- ST7789 driver and BB6 shell not yet wired.")

# ── Device-specific: JC3248W535 (WIP) ────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "jc3248w535")
    list(APPEND SRCS
        "../system/kernel/modules/display_st7796.cpp"
        "../system/kernel/modules/touch_gt911.cpp"
        # TEMP: disabled until esp_vfs_fat.h path is fixed
        # "../system/kernel/modules/partition_manager.cpp"
    )
    list(APPEND REQUIRES fatfs esp_partition esp_lcd driver esp_timer)
    set(BUILD_MINI 1 CACHE STRING "" FORCE)
    message(STATUS "jc3248w535: ✓ Bootloader boots confirmed. UI shell pending (use cyd_boot until app UI ready).")

# ── Device-specific: Waveshare 1.69" (WIP) ───────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "waveshare169")
    list(APPEND SRCS
        "../system/kernel/modules/display_st7789.cpp"
        "../system/kernel/modules/touch_cst816s.cpp"
        # TEMP: disabled until esp_vfs_fat.h path is fixed
        # "../system/kernel/modules/partition_manager.cpp"
    )
    list(APPEND REQUIRES fatfs esp_partition esp_lcd driver esp_timer)
    set(BUILD_MINI 1 CACHE STRING "" FORCE)
    message(WARNING "waveshare169: WIP target — ST7789 + CST816S, no shell yet. Verify pins before flashing.")

else()
    message(FATAL_ERROR "Unknown TARGET_DEVICE='${TARGET_DEVICE}'. Valid: heltec, cyd, cyd_boot, tdeck, jc3248w535, waveshare169")
endif()

# ── Full build: MicroPython runtime ───────────────────────────────────────────
if(NOT BUILD_MINI)
    set(_mp_header "${CMAKE_CURRENT_SOURCE_DIR}/../components/micropython/ports/embed/port/micropython_embed.h")
    if(NOT EXISTS "${_mp_header}")
        message(WARNING "MicroPython submodule not present -- building as mini. Clone it or pass -DBUILD_MINI=1.")
        set(BUILD_MINI 1 CACHE STRING "" FORCE)
    else()
        list(APPEND SRCS
            "../system/micropython/mpython_runtime.cpp"
            "../system/micropython/kitt_module.c"
        )
        list(APPEND INCLUDE_DIRS "../system/micropython")
        list(APPEND REQUIRES micropython)
    endif()
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
set(PURR_DEFS "")

if(TARGET_DEVICE STREQUAL "heltec")
    list(APPEND PURR_DEFS PURR_DISPLAY_SSD1306=1)
elseif(TARGET_DEVICE STREQUAL "cyd_s028r" OR TARGET_DEVICE STREQUAL "cyd_s024c")
    # Map variant targets to generic cyd PURR_DEFS
    list(APPEND PURR_DEFS
        PURR_DISPLAY_ILI9341=1
        PURR_HAS_PARTITION_MGR=1
    )

    if(TARGET_DEVICE STREQUAL "cyd_s024c")
        list(APPEND PURR_DEFS
            PURR_HAS_TOUCH_CST816S=1
            CYD_VARIANT_S024C=1
        )
    else()
        list(APPEND PURR_DEFS
            PURR_HAS_TOUCH_XPT2046=1
        )
    endif()

    # UI shells — rendered via PURR WM + LVGL (no MiniWin dependency)
    if(PURR_SHELL STREQUAL "blackberry" OR PURR_SHELL STREQUAL "both")
        list(APPEND PURR_DEFS PURR_HAS_BLACKBERRY_UI=1 PURR_BBUI_TARGET_TOUCH=1)
    endif()
    if(PURR_SHELL STREQUAL "explorer" OR PURR_SHELL STREQUAL "both")
        list(APPEND PURR_DEFS PURR_HAS_EXPLORER=1)
    endif()
    if(PURR_SHELL STREQUAL "classicmac")
        list(APPEND PURR_DEFS PURR_HAS_CLASSICMAC=1)
    endif()
elseif(TARGET_DEVICE STREQUAL "cyd")
    # Full OS image (ota_0) — no bootloader module; recovery is in the factory partition
    set(_CYD_VARIANT "${CYD_DISPLAY_VARIANT}")
    if(NOT _CYD_VARIANT)
        set(_CYD_VARIANT "s028r")
    endif()

    list(APPEND PURR_DEFS
        PURR_DISPLAY_ILI9341=1
        PURR_HAS_PARTITION_MGR=1
    )

    if(_CYD_VARIANT STREQUAL "s024c")
        list(APPEND PURR_DEFS
            PURR_HAS_TOUCH_CST816S=1
            CYD_VARIANT_S024C=1
        )
    else()
        list(APPEND PURR_DEFS
            PURR_HAS_TOUCH_XPT2046=1
        )
    endif()

    # UI shells — rendered via PURR WM + LVGL (no MiniWin dependency)
    if(PURR_SHELL STREQUAL "blackberry" OR PURR_SHELL STREQUAL "both")
        list(APPEND PURR_DEFS PURR_HAS_BLACKBERRY_UI=1 PURR_BBUI_TARGET_TOUCH=1)
    endif()
    if(PURR_SHELL STREQUAL "explorer" OR PURR_SHELL STREQUAL "both")
        list(APPEND PURR_DEFS PURR_HAS_EXPLORER=1)
    endif()
    if(PURR_SHELL STREQUAL "classicmac")
        list(APPEND PURR_DEFS PURR_HAS_CLASSICMAC=1)
    endif()
elseif(TARGET_DEVICE STREQUAL "cyd_boot")
    # Factory bootloader image — minimal, always shows recovery UI
    list(APPEND PURR_DEFS
        PURR_DISPLAY_ILI9341=1
        PURR_HAS_BOOTLOADER=1
        PURR_IS_BOOTLOADER_IMG=1
        PURR_HAS_TOUCH_CST816S=1
        PURR_HAS_PARTITION_MGR=1
        CYD_VARIANT_S024C=1
    )
elseif(TARGET_DEVICE STREQUAL "tdeck")
    list(APPEND PURR_DEFS PURR_DISPLAY_SSD1306=1)
    if(PURR_SHELL STREQUAL "blackberry" OR PURR_SHELL STREQUAL "both")
        list(APPEND PURR_DEFS PURR_HAS_BLACKBERRY_UI=1 PURR_BBUI_TARGET_KEYS=1)
    endif()
    if(BUILD_TDECK_PLUS)
        list(APPEND PURR_DEFS PURR_IS_TDECK_PLUS=1)
    endif()
elseif(TARGET_DEVICE STREQUAL "jc3248w535")
    list(APPEND PURR_DEFS
        PURR_DISPLAY_ST7796=1
        PURR_HAS_TOUCH_GT911=1
        PURR_HAS_PARTITION_MGR=1
        PURR_TARGET_WIP=1
    )
elseif(TARGET_DEVICE STREQUAL "waveshare169")
    list(APPEND PURR_DEFS
        PURR_DISPLAY_ST7789=1
        PURR_HAS_TOUCH_CST816S=1
        PURR_HAS_PARTITION_MGR=1
        PURR_TARGET_WIP=1
    )
endif()

if(PURR_ENABLE_BT)
    list(APPEND PURR_DEFS PURR_HAS_BT=1)
endif()
if(PURR_ENABLE_LORA)
    list(APPEND PURR_DEFS PURR_HAS_LORA=1)
endif()
if(PURR_ENABLE_MESH)
    list(APPEND PURR_DEFS PURR_HAS_MESH=1)
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

# Ensure LUA_32BITS is defined for all files including Lua headers
if(TARGET_DEVICE STREQUAL "cyd" OR TARGET_DEVICE STREQUAL "cyd_boot")
    target_compile_options(${COMPONENT_LIB} PRIVATE -DLUA_32BITS=1)
endif()
