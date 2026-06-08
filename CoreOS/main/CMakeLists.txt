# ── Build knobs ───────────────────────────────────────────────────────────────
# Passed via SDK/sdk.sh / sdk_core.py, or manually:
#   idf.py -DTARGET_DEVICE=cyd ...
#
# Note: IDF runs cmake twice per set-target — a bare first pass for IDF setup
# (no user -D flags), then a full second pass with all user flags applied.
# The STATUS message below will appear twice; the second print is authoritative.

set(TARGET_DEVICE  "heltec" CACHE STRING "Target: heltec | cyd | cyd_boot | tdeck | jc3248w535 | waveshare169")
set(BUILD_MINI     0        CACHE STRING "1 = strip MicroPython runtime")

# CYD display variant (cyd target only)
#   s028r  — ESP32-2432S028R (original) with XPT2046 touch
#   s024c  — ESP32-2432S024C (newer) with CST816S I2C touch
set(CYD_DISPLAY_VARIANT "s028r" CACHE STRING "CYD variant: s028r | s024c")

# ── Optional module toggles ───────────────────────────────────────────────────
set(PURR_ENABLE_BT      1 CACHE STRING "Include Bluetooth manager")
set(PURR_ENABLE_MTP     0 CACHE STRING "Include MTP USB manager")
set(PURR_ENABLE_FLASHER 0 CACHE STRING "Include OTA partition flasher")
set(PURR_ENABLE_LTE     0 CACHE STRING "Include LTE manager (experimental)")
set(PURR_ENABLE_SHELL   1 CACHE STRING "Include USB serial debug shell (esp_console)")

# LoRa: auto-enabled for heltec/tdeck, off for cyd/cyd_boot.
if(TARGET_DEVICE STREQUAL "heltec" OR TARGET_DEVICE STREQUAL "tdeck")
    set(_lora_default 1)
else()
    set(_lora_default 0)
endif()
set(PURR_ENABLE_LORA ${_lora_default} CACHE STRING "Include LoRa radio manager")

# Mesh: requires LoRa. Defaults off.
set(PURR_ENABLE_MESH 0 CACHE STRING "Include Meshtastic mesh stack (requires LoRa)")

message(STATUS "PURR OS  target=${TARGET_DEVICE}  ui=${PURR_UI_KERNEL}  mini=${BUILD_MINI}  bt=${PURR_ENABLE_BT}  lora=${PURR_ENABLE_LORA}  mesh=${PURR_ENABLE_MESH}  mtp=${PURR_ENABLE_MTP}  flasher=${PURR_ENABLE_FLASHER}  lte=${PURR_ENABLE_LTE}  shell=${PURR_ENABLE_SHELL}")

# ── Core sources (all targets) ────────────────────────────────────────────────
set(SRCS
    "../system/kernel/main.cpp"
    "../system/kernel/kitt.cpp"
    "../system/kernel/device_config.cpp"
    "../system/kernel/idf_compat.c"
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
)

set(REQUIRES
    bblanchon__arduinojson
    esp_wifi
    nvs_flash
    spiffs
    esp_adc
    app_update
    esp_timer
    lib_tftespi
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
    list(APPEND SRCS "../apps/smol/smol.cpp")
    list(APPEND INCLUDE_DIRS "../apps/smol")

# ── Device-specific: CYD variants ────────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "cyd_s028r" OR TARGET_DEVICE STREQUAL "cyd_s024c")
    if(TARGET_DEVICE STREQUAL "cyd_s028r")
        set(CYD_DISPLAY_VARIANT "s028r")
    else()
        set(CYD_DISPLAY_VARIANT "s024c")
    endif()

    list(APPEND SRCS
        "../system/kernel/modules/partition_manager_stubs.cpp"
        "../system/kernel/modules/ui_stubs.cpp"
    )
    list(APPEND REQUIRES fatfs esp_partition esp_lcd esp_timer nvs_flash sdmmc)
    if(PURR_UI_KERNEL STREQUAL "miniwin")
        list(APPEND REQUIRES lib_miniwin)
    endif()

# ── Device-specific: CYD full OS ─────────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "cyd")
    list(APPEND SRCS
        "../system/kernel/modules/partition_manager_stubs.cpp"
        "../system/kernel/modules/ui_stubs.cpp"
    )
    list(APPEND REQUIRES fatfs esp_partition esp_lcd esp_timer nvs_flash sdmmc)
    if(PURR_UI_KERNEL STREQUAL "miniwin")
        list(APPEND REQUIRES lib_miniwin)
    endif()

# ── Device-specific: CYD bootloader ──────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "cyd_boot")
    set(SRCS
        "../system/kernel/main.cpp"
        "../system/kernel/kitt.cpp"
        "../system/kernel/device_config.cpp"
        "../system/kernel/idf_compat.c"
        "../system/bridge/main.cpp"
        "../system/system/main.cpp"
        "../system/kernel/modules/partition_manager.cpp"
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
        lib_tftespi
        driver
        fatfs
        sdmmc
        drv_display
        drv_touch
    )
    set(BUILD_MINI 1 CACHE STRING "" FORCE)
    message(STATUS "cyd_boot: minimal factory bootloader image")

# ── Device-specific: T-Deck ───────────────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "tdeck")
    set(BUILD_TDECK_PLUS 0 CACHE STRING "T-Deck variant: 0=original, 1=Plus (GPS)")
    list(APPEND SRCS "../apps/smol/smol.cpp")
    list(APPEND INCLUDE_DIRS "../apps/smol")
    if(BUILD_TDECK_PLUS)
        list(APPEND REQUIRES drv_gps)
        message(STATUS "T-Deck Plus: GPS driver included.")
    endif()
    if(PURR_UI_KERNEL STREQUAL "miniwin")
        list(APPEND REQUIRES lib_miniwin)
    endif()
    message(WARNING "T-Deck: placeholder build -- ST7789 driver and BB6 shell not yet wired.")

# ── Device-specific: JC3248W535 ───────────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "jc3248w535")
    list(APPEND REQUIRES fatfs esp_partition esp_lcd esp_timer)
    if(PURR_UI_KERNEL STREQUAL "miniwin")
        list(APPEND REQUIRES lib_miniwin)
    endif()
    set(BUILD_MINI 1 CACHE STRING "" FORCE)
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
    message(FATAL_ERROR "Unknown TARGET_DEVICE='${TARGET_DEVICE}'. Valid: heltec, cyd, cyd_boot, tdeck, jc3248w535, waveshare169")
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
# IDF 5.3 does not propagate include dirs through REQUIRES chains.
if(PURR_UI_KERNEL STREQUAL "miniwin")
    if(TARGET_DEVICE MATCHES "^cyd")
        set(_mw_dev "cyd")
    else()
        set(_mw_dev "generic")
    endif()
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../components/lib_miniwin/MiniWin"
        "${CMAKE_CURRENT_SOURCE_DIR}/../components/lib_miniwin/devices/${_mw_dev}"
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
    list(APPEND PURR_DEFS PURR_DISPLAY_SSD1306=1)
elseif(TARGET_DEVICE STREQUAL "cyd_s028r" OR TARGET_DEVICE STREQUAL "cyd_s024c" OR TARGET_DEVICE STREQUAL "cyd")
    list(APPEND PURR_DEFS PURR_DISPLAY_ILI9341=1 PURR_HAS_PARTITION_MGR=1)
    if(CYD_DISPLAY_VARIANT STREQUAL "s024c" OR TARGET_DEVICE STREQUAL "cyd_s024c")
        list(APPEND PURR_DEFS PURR_HAS_TOUCH_CST816S=1 CYD_VARIANT_S024C=1)
    else()
        list(APPEND PURR_DEFS PURR_HAS_TOUCH_XPT2046=1)
    endif()
elseif(TARGET_DEVICE STREQUAL "cyd_boot")
    list(APPEND PURR_DEFS
        PURR_DISPLAY_ILI9341=1 PURR_HAS_BOOTLOADER=1 PURR_IS_BOOTLOADER_IMG=1
        PURR_HAS_TOUCH_CST816S=1 PURR_HAS_PARTITION_MGR=1 CYD_VARIANT_S024C=1
    )
elseif(TARGET_DEVICE STREQUAL "tdeck")
    list(APPEND PURR_DEFS PURR_DISPLAY_SSD1306=1)
    if(BUILD_TDECK_PLUS)
        list(APPEND PURR_DEFS PURR_IS_TDECK_PLUS=1)
    endif()
elseif(TARGET_DEVICE STREQUAL "jc3248w535")
    list(APPEND PURR_DEFS PURR_DISPLAY_ST7796=1 PURR_HAS_TOUCH_GT911=1 PURR_HAS_PARTITION_MGR=1 PURR_TARGET_WIP=1)
elseif(TARGET_DEVICE STREQUAL "waveshare169")
    list(APPEND PURR_DEFS PURR_DISPLAY_ST7789=1 PURR_HAS_TOUCH_CST816S=1 PURR_HAS_PARTITION_MGR=1 PURR_TARGET_WIP=1)
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

target_compile_definitions(${COMPONENT_LIB} PUBLIC ${PURR_DEFS})
