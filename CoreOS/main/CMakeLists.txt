# ── Build knobs ───────────────────────────────────────────────────────────────
# Passed via SDK/sdk.sh / sdk_core.py, or manually:
#   idf.py -DTARGET_DEVICE=cyd ...
#
# Note: IDF runs cmake twice per set-target — a bare first pass for IDF setup
# (no user -D flags), then a full second pass with all user flags applied.
# The STATUS message below will appear twice; the second print is authoritative.

set(TARGET_DEVICE  "heltec" CACHE STRING "Target: heltec | tembed_cc1101 | cyd | cyd_s028r | cyd_s024c | cyd_boot | tdeck | tdeck_plus | jc3248w535 | waveshare169")
set(BUILD_MINI     0        CACHE STRING "1 = strip MicroPython runtime")
# CYD display variant (cyd target only)
#   s028r  — ESP32-2432S028R (original) with XPT2046 touch
#   s024c  — ESP32-2432S024C (newer) with CST816S I2C touch
set(CYD_DISPLAY_VARIANT "s028r" CACHE STRING "CYD variant: s028r | s024c")

# ── Optional module toggles ───────────────────────────────────────────────────
set(PURR_ENABLE_BT       1 CACHE STRING "Include Bluetooth manager")
set(PURR_ENABLE_MTP      0 CACHE STRING "Include MTP USB manager")
set(PURR_ENABLE_FLASHER  0 CACHE STRING "Include OTA partition flasher")
set(PURR_ENABLE_LTE      0 CACHE STRING "Include LTE manager (experimental)")
set(PURR_ENABLE_SHELL    1 CACHE STRING "Include USB serial debug shell (esp_console)")
set(PURR_ENABLE_LUA      1 CACHE STRING "Include Lua 5.4 runtime (.paws/.claw scripts)")
set(BUILD_TDECK_PLUS     0 CACHE STRING "T-Deck variant: 0=original, 1=Plus (GPS/keyboard)")
set(PURR_ENABLE_MAGIDOS  0 CACHE STRING "Include MagiDOS 8086 emulator app")
set(PURR_ENABLE_MAGICMAC 0 CACHE STRING "Include MagicMac Plus emulator app")
set(PURR_ENABLE_GPS      0 CACHE STRING "Include GPS manager (T-Deck Plus)")
set(PURR_UI_THEME        "wce" CACHE STRING "MiniWin shell theme: wce | blackberry")

# LoRa: auto-enabled for heltec/tdeck, off for cyd/cyd_boot.
if(TARGET_DEVICE STREQUAL "heltec" OR TARGET_DEVICE STREQUAL "tdeck")
    set(_lora_default 1)
else()
    set(_lora_default 0)
endif()
set(PURR_ENABLE_LORA ${_lora_default} CACHE STRING "Include LoRa radio manager")

# CC1101: auto-enabled for T-Embed CC1101
if(TARGET_DEVICE STREQUAL "tembed_cc1101")
    set(_cc1101_default 1)
else()
    set(_cc1101_default 0)
endif()
set(PURR_ENABLE_CC1101 ${_cc1101_default} CACHE STRING "Include CC1101 sub-GHz radio manager")

# Mesh: requires LoRa. Defaults off.
set(PURR_ENABLE_MESH 0 CACHE STRING "Include Meshtastic mesh stack (requires LoRa)")

message(STATUS "PURR OS  target=${TARGET_DEVICE}  ui=${PURR_UI_KERNEL}  mini=${BUILD_MINI}  bt=${PURR_ENABLE_BT}  lora=${PURR_ENABLE_LORA}  mesh=${PURR_ENABLE_MESH}  mtp=${PURR_ENABLE_MTP}  flasher=${PURR_ENABLE_FLASHER}  lte=${PURR_ENABLE_LTE}  shell=${PURR_ENABLE_SHELL}  lua=${PURR_ENABLE_LUA}  magidos=${PURR_ENABLE_MAGIDOS}  magicmac=${PURR_ENABLE_MAGICMAC}")

# ── Core sources (all targets) ────────────────────────────────────────────────
set(SRCS
    "../system/kernel/main.cpp"
    "../system/kernel/kitt.cpp"
    "../system/kernel/purr_panic.cpp"
    "../system/kernel/device_config.cpp"
    "../system/kernel/idf_compat.c"
    "../system/kernel/modules/power_manager.cpp"
    "../system/kernel/modules/purr_sys_drv.cpp"
    "../system/kernel/modules/device_info_drv.cpp"
    "../system/kernel/modules/purr_drv_registry.cpp"
    "../system/kernel/modules/purr_drv.cpp"
    "../system/kernel/modules/purr_drv_interp.cpp"
    "../system/kernel/modules/purr_input.cpp"
    "../system/bridge/main.cpp"
    "../system/system/main.cpp"
)

set(INCLUDE_DIRS
    "../system"
    "../system/kernel"
    "../system/kernel/modules"
    "../system/bridge"
    "../system/system"
)

set(REQUIRES
    bblanchon__arduinojson
    esp_wifi
    nvs_flash
    spiffs
    fatfs
    sdmmc
    esp_adc
    app_update
    esp_timer
    driver
    drv_display
    drv_touch
    drv_wifi
)

# ── Bluetooth ─────────────────────────────────────────────────────────────────
if(PURR_ENABLE_BT)
    list(APPEND REQUIRES drv_bt bt)
endif()

# ── LoRa ──────────────────────────────────────────────────────────────────────
if(PURR_ENABLE_LORA)
    list(APPEND REQUIRES drv_lora lib_radiolib)
endif()

# ── CC1101 ────────────────────────────────────────────────────────────────────
if(PURR_ENABLE_CC1101)
    list(APPEND REQUIRES drv_cc1101 lib_radiolib)
endif()

# ── Mesh / Meshtastic ─────────────────────────────────────────────────────────
if(PURR_ENABLE_MESH)
    if(NOT PURR_ENABLE_LORA)
        message(FATAL_ERROR "PURR_ENABLE_MESH=1 requires PURR_ENABLE_LORA=1")
    endif()
    list(APPEND SRCS "../system/kernel/modules/purr_mesh.cpp")
    list(APPEND REQUIRES lib_nanopb lib_mesh_pb mbedtls)
    message(STATUS "purr_mesh: Meshtastic-compatible native mesh stack enabled.")
endif()

# ── MTP USB ───────────────────────────────────────────────────────────────────
if(PURR_ENABLE_MTP)
    list(APPEND SRCS "../system/kernel/modules/mtp_manager.cpp")
endif()

# ── OTA Flasher ───────────────────────────────────────────────────────────────
if(PURR_ENABLE_FLASHER)
    list(APPEND SRCS "../system/kernel/modules/flasher.cpp")
endif()

# ── LTE (experimental) ────────────────────────────────────────────────────────
if(PURR_ENABLE_LTE)
    list(APPEND REQUIRES drv_lte)
endif()

# ── Debug shell ───────────────────────────────────────────────────────────────
if(PURR_ENABLE_SHELL)
    list(APPEND REQUIRES drv_shell)
endif()


# ── Device-specific: Heltec ───────────────────────────────────────────────────
if(TARGET_DEVICE STREQUAL "heltec")
    list(APPEND SRCS "../apps/kitten_ui/kitten_ui.cpp")
    list(APPEND INCLUDE_DIRS "../apps/kitten_ui")

# ── Device-specific: LilyGo T-Embed CC1101 ───────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "tembed_cc1101")
    list(APPEND SRCS "../apps/kitten_ui/kitten_ui.cpp")
    list(APPEND INCLUDE_DIRS "../apps/kitten_ui")
    set(PURR_HAS_PSRAM 1 CACHE STRING "" FORCE)
    message(STATUS "T-Embed CC1101: ESP32-S3R8  ST7789 170x320  CC1101 433/868/915 MHz  rotary encoder")

# ── Device-specific: CYD variants ────────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "cyd_s028r" OR TARGET_DEVICE STREQUAL "cyd_s024c")
    if(TARGET_DEVICE STREQUAL "cyd_s028r")
        set(CYD_DISPLAY_VARIANT "s028r")
    else()
        set(CYD_DISPLAY_VARIANT "s024c")
    endif()

    list(APPEND SRCS
        "../system/kernel/modules/partition_manager.cpp"
        "../system/kernel/modules/ui_stubs.cpp"
    )
    list(APPEND REQUIRES esp_partition esp_lcd esp_timer nvs_flash)
    if(PURR_UI_KERNEL STREQUAL "miniwin")
        list(APPEND REQUIRES lib_miniwin)
    endif()

# ── Device-specific: CYD full OS ─────────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "cyd")
    list(APPEND SRCS
        "../system/kernel/modules/partition_manager.cpp"
        "../system/kernel/modules/ui_stubs.cpp"
    )
    list(APPEND REQUIRES esp_partition esp_lcd esp_timer nvs_flash)
    if(PURR_UI_KERNEL STREQUAL "miniwin")
        list(APPEND REQUIRES lib_miniwin)
    endif()

# ── Device-specific: CYD bootloader ──────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "cyd_boot")
    set(SRCS
        "../system/kernel/main.cpp"
        "../system/kernel/kitt.cpp"
        "../system/kernel/purr_panic.cpp"
        "../system/kernel/device_config.cpp"
        "../system/kernel/idf_compat.c"
        "../system/bridge/main.cpp"
        "../system/system/main.cpp"
        "../system/kernel/modules/partition_manager.cpp"
        "../system/kernel/modules/purr_bootloader.cpp"
        "../system/kernel/modules/stub_managers.cpp"
        "../system/kernel/modules/purr_input.cpp"
    )
    set(REQUIRES
        bblanchon__arduinojson
        nvs_flash
        spiffs
        esp_partition
        app_update
        esp_timer
        esp_lcd
        lib_tftespi
        driver
        drv_display
        drv_touch
    )
    set(BUILD_MINI 1 CACHE STRING "" FORCE)
    message(STATUS "cyd_boot: minimal factory bootloader image")

# ── Device-specific: T-Deck ───────────────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "tdeck")
    list(APPEND SRCS "../apps/kitten_ui/kitten_ui.cpp")
    list(APPEND INCLUDE_DIRS "../apps/kitten_ui")
    if(BUILD_TDECK_PLUS)
        list(APPEND REQUIRES drv_gps)
        message(STATUS "T-Deck Plus: GPS driver included.")
    endif()
    if(PURR_UI_KERNEL STREQUAL "miniwin")
        list(APPEND REQUIRES lib_miniwin)
    endif()
    message(WARNING "T-Deck: placeholder build -- ST7789 driver and BB6 shell not yet wired.")

# ── Device-specific: T-Deck Plus (touch screen, GT911) ───────────────────────
elseif(TARGET_DEVICE STREQUAL "tdeck_plus")
    list(APPEND SRCS
        "../apps/kitten_ui/kitten_ui.cpp"
        "../system/kernel/modules/partition_manager.cpp"
    )
    list(APPEND INCLUDE_DIRS "../apps/kitten_ui")
    if(PURR_UI_KERNEL STREQUAL "miniwin")
        list(APPEND REQUIRES lib_miniwin)
    endif()
    set(PURR_HAS_PSRAM 1 CACHE STRING "" FORCE)
    message(STATUS "T-Deck Plus: ST7789 320x240 + GT911 touch + SD card (SPI3).")

# ── Device-specific: JC3248W535 ───────────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "jc3248w535")
    list(APPEND REQUIRES fatfs esp_partition esp_lcd esp_timer)
    if(PURR_UI_KERNEL STREQUAL "miniwin")
        list(APPEND REQUIRES lib_miniwin)
    endif()
    set(BUILD_MINI 1 CACHE STRING "" FORCE)
    set(PURR_HAS_PSRAM 1 CACHE STRING "" FORCE)
    message(STATUS "jc3248w535: Bootloader boots confirmed. UI shell pending.")

# ── Device-specific: Waveshare 1.69" ─────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "waveshare169")
    list(APPEND REQUIRES fatfs esp_partition esp_lcd esp_timer)
    if(PURR_UI_KERNEL STREQUAL "miniwin")
        list(APPEND REQUIRES lib_miniwin)
    endif()
    set(BUILD_MINI 1 CACHE STRING "" FORCE)
    message(WARNING "waveshare169: WIP target — ST7789 + CST816S, no shell yet.")

else()
    message(FATAL_ERROR "Unknown TARGET_DEVICE='${TARGET_DEVICE}'. Valid: heltec, tembed_cc1101, cyd, cyd_s028r, cyd_s024c, cyd_boot, tdeck, tdeck_plus, jc3248w535, waveshare169")
endif()

# ── Lua runtime ──────────────────────────────────────────────────────────────
if(PURR_ENABLE_LUA)
    list(APPEND SRCS "../system/kernel/modules/lua_runtime.cpp")
    list(APPEND REQUIRES lib_lua)
    message(STATUS "Lua runtime: enabled")
endif()

# ── MicroPython runtime ───────────────────────────────────────────────────────
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

# ── IDF include propagation workarounds ───────────────────────────────────────
# IDF 5.3 does not propagate include dirs through REQUIRES chains to main.
# drv_display and drv_cc1101 headers are included by kernel files — expose them here.
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/drv_display"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/drv_cc1101"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/drv_lora"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/drv_touch"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/drv_shell"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/drv_wifi"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/drv_bt"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/drv_gps"
)

if(PURR_UI_KERNEL STREQUAL "miniwin")
    if(TARGET_DEVICE STREQUAL "cyd_s028r")
        set(_mw_dev "cyd_s028r")
    elseif(TARGET_DEVICE STREQUAL "cyd_s024c")
        set(_mw_dev "cyd_s024c")
    elseif(TARGET_DEVICE MATCHES "^cyd")
        set(_mw_dev "cyd")
    elseif(TARGET_DEVICE STREQUAL "tdeck_plus")
        set(_mw_dev "tdeck_plus")
    elseif(TARGET_DEVICE STREQUAL "jc3248w535")
        set(_mw_dev "jc3248w535")
    elseif(TARGET_DEVICE STREQUAL "waveshare169")
        set(_mw_dev "waveshare169")
    else()
        set(_mw_dev "generic")
    endif()
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../ui/lib_miniwin/MiniWin"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../devices/${_mw_dev}"
    )
endif()
if(TARGET_DEVICE STREQUAL "cyd_boot")
    idf_component_get_property(fatfs_lib  fatfs  COMPONENT_LIB)
    idf_component_get_property(sdmmc_lib  sdmmc  COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} PRIVATE ${fatfs_lib} ${sdmmc_lib})
endif()

# ── Compile-time feature flags ────────────────────────────────────────────────
set(PURR_DEFS "")

if(TARGET_DEVICE STREQUAL "heltec")
    list(APPEND PURR_DEFS PURR_DISPLAY_SSD1306=1 PURR_TARGET_HELTEC=1)
elseif(TARGET_DEVICE STREQUAL "tembed_cc1101")
    list(APPEND PURR_DEFS
        PURR_DISPLAY_ST7789=1
        PURR_TARGET_TEMBED_CC1101=1
        PURR_FORCE_KITTEN_UI=1
        PURR_HAS_PSRAM=1
        PURR_HAS_ENCODER=1
        PURR_ENCODER_CLK=1
        PURR_ENCODER_DT=2
        PURR_ENCODER_SW=0
        # Display ST7789 — verified pins (LilyGo T-Embed CC1101 schematic)
        PURR_ST7789_MOSI=9
        PURR_ST7789_SCLK=11
        PURR_ST7789_CS=41
        PURR_ST7789_DC=16
        PURR_ST7789_RST=-1
        PURR_ST7789_BL=21
        PURR_ST7789_WIDTH=170
        PURR_ST7789_HEIGHT=320
        PURR_ST7789_SPI_HOST=SPI2_HOST
        # CC1101 verified pins
        PURR_CC1101_MOSI=9
        PURR_CC1101_MISO=10
        PURR_CC1101_SCK=11
        PURR_CC1101_CS=12
        PURR_CC1101_GDO0=38
        PURR_CC1101_GDO2=39
        # Peripheral power enable — must be driven HIGH before SPI init
        PURR_TEMBED_POWER_EN=15
    )
elseif(TARGET_DEVICE STREQUAL "cyd_s028r" OR TARGET_DEVICE STREQUAL "cyd_s024c" OR TARGET_DEVICE STREQUAL "cyd")
    list(APPEND PURR_DEFS PURR_DISPLAY_ILI9341=1 PURR_HAS_PARTITION_MGR=1)
    if(CYD_DISPLAY_VARIANT STREQUAL "s024c" OR TARGET_DEVICE STREQUAL "cyd_s024c")
        list(APPEND PURR_DEFS PURR_HAS_TOUCH_CST816S=1 CYD_VARIANT_S024C=1 PURR_TARGET_CYD_S024C=1)
    elseif(TARGET_DEVICE STREQUAL "cyd_s028r")
        list(APPEND PURR_DEFS PURR_HAS_TOUCH_XPT2046=1 PURR_TARGET_CYD_S028R=1)
    else()
        list(APPEND PURR_DEFS PURR_HAS_TOUCH_XPT2046=1 PURR_TARGET_CYD=1)
    endif()
elseif(TARGET_DEVICE STREQUAL "cyd_boot")
    list(APPEND PURR_DEFS
        PURR_DISPLAY_ILI9341=1 PURR_HAS_BOOTLOADER=1 PURR_IS_BOOTLOADER_IMG=1
        PURR_HAS_TOUCH_CST816S=1 PURR_HAS_PARTITION_MGR=1 CYD_VARIANT_S024C=1
        PURR_TARGET_CYD_BOOT=1
    )
elseif(TARGET_DEVICE STREQUAL "tdeck")
    list(APPEND PURR_DEFS PURR_DISPLAY_ST7789=1 PURR_TARGET_TDECK=1)
elseif(TARGET_DEVICE STREQUAL "tdeck_plus")
    list(APPEND PURR_DEFS
        PURR_DISPLAY_ST7789=1
        PURR_HAS_TOUCH_GT911=1
        PURR_HAS_PSRAM=1
        PURR_TARGET_TDECK_PLUS=1
        PURR_HAS_PARTITION_MGR=1
        PURR_DEVICE_TDECK_PLUS=1
    )
elseif(TARGET_DEVICE STREQUAL "jc3248w535")
    list(APPEND PURR_DEFS PURR_DISPLAY_ST7796=1 PURR_HAS_TOUCH_GT911=1 PURR_TARGET_WIP=1 PURR_HAS_PSRAM=1 PURR_TARGET_JC3248W535=1)
elseif(TARGET_DEVICE STREQUAL "waveshare169")
    list(APPEND PURR_DEFS
        PURR_DISPLAY_ST7789=1 PURR_HAS_TOUCH_CST816S=1 PURR_TARGET_WIP=1 PURR_TARGET_WAVESHARE169=1
        # Display ST7789 — verified pins (Waveshare ESP32-S3 1.69" schematic)
        PURR_ST7789_MOSI=9
        PURR_ST7789_SCLK=18
        PURR_ST7789_CS=16
        PURR_ST7789_DC=2
        PURR_ST7789_RST=3
        PURR_ST7789_BL=17
        PURR_ST7789_SPI_HOST=SPI2_HOST
    )
endif()

# MiniWin available on all targets that have a display and are not heltec
if(PURR_UI_KERNEL STREQUAL "miniwin" AND NOT TARGET_DEVICE STREQUAL "heltec")
    list(APPEND PURR_DEFS PURR_HAS_MINIWIN=1)
endif()

if(PURR_ENABLE_BT)
    list(APPEND PURR_DEFS PURR_HAS_BT=1)
endif()
if(PURR_ENABLE_LORA)
    list(APPEND PURR_DEFS PURR_HAS_LORA=1)
endif()
if(PURR_ENABLE_CC1101)
    list(APPEND PURR_DEFS PURR_HAS_CC1101=1)
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
if(PURR_ENABLE_LTE)
    list(APPEND PURR_DEFS PURR_HAS_LTE=1)
endif()
if(PURR_ENABLE_SHELL)
    list(APPEND PURR_DEFS PURR_HAS_SHELL=1)
endif()
if(NOT BUILD_MINI)
    list(APPEND PURR_DEFS PURR_HAS_MICROPYTHON=1)
endif()
if(PURR_ENABLE_LUA)
    list(APPEND PURR_DEFS PURR_HAS_LUA=1)
endif()
if(PURR_ENABLE_MAGIDOS)
    list(APPEND PURR_DEFS PURR_HAS_MAGIDOS=1)
endif()
if(PURR_ENABLE_MAGICMAC)
    list(APPEND PURR_DEFS PURR_HAS_MAGICMAC=1)
endif()
if(PURR_ENABLE_GPS)
    list(APPEND PURR_DEFS PURR_HAS_GPS=1)
endif()
if(PURR_UI_THEME STREQUAL "blackberry")
    list(APPEND PURR_DEFS PURR_THEME_BLACKBERRY=1)
elseif(PURR_UI_THEME STREQUAL "luna")
    list(APPEND PURR_DEFS PURR_THEME_LUNA=1)
endif()

target_compile_definitions(${COMPONENT_LIB} PUBLIC ${PURR_DEFS})
