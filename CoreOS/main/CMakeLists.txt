# ── Build knobs ───────────────────────────────────────────────────────────────
# Passed via build.sh / Build.ps1 or manually: idf.py -DTARGET_DEVICE=heltec ...
set(TARGET_DEVICE  "heltec" CACHE STRING "Target device: heltec | cyd | tdeck")
set(BUILD_MINI     0        CACHE STRING "Mini build: 1 = strip MicroPython")

# ── Optional module toggles ───────────────────────────────────────────────────
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

# Mesh: Meshtastic co-resident stack. Requires PURR_ENABLE_LORA=1.
# Auto-disabled when LoRa is off. Override with -DPURR_ENABLE_MESH=1.
if(PURR_ENABLE_LORA)
    set(_mesh_default 0)
else()
    set(_mesh_default 0)
endif()
set(PURR_ENABLE_MESH ${_mesh_default} CACHE STRING "Include Meshtastic co-resident mesh stack (requires LoRa)")

message(STATUS "PURR OS  target=${TARGET_DEVICE}  mini=${BUILD_MINI}  bt=${PURR_ENABLE_BT}  lora=${PURR_ENABLE_LORA}  mesh=${PURR_ENABLE_MESH}  mtp=${PURR_ENABLE_MTP}  flasher=${PURR_ENABLE_FLASHER}")

# ── Core sources (all targets) ────────────────────────────────────────────────
# Paths are relative to this file (CoreOS/main/), so prefix ../ to reach project root.
set(SRCS
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
    "../system/kernel"
    "../system/kernel/modules"
    "../system/bridge"
    "../system/system"
)

set(REQUIRES
    espressif__arduino-esp32
    bblanchon__arduinojson
    esp_wifi
    nvs_flash
    spiffs
    esp_adc
    app_update
    esp_timer
)

# ── Bluetooth (optional) ──────────────────────────────────────────────────────
if(PURR_ENABLE_BT)
    list(APPEND SRCS "../system/kernel/modules/bt_manager.cpp")
    list(APPEND REQUIRES bt)
endif()

# ── LoRa (optional, auto from target) ────────────────────────────────────────
if(PURR_ENABLE_LORA)
    list(APPEND SRCS "../system/kernel/modules/lora_manager.cpp")
endif()

# ── Mesh / Meshtastic (optional, requires LoRa) ───────────────────────────────
if(PURR_ENABLE_MESH)
    if(NOT PURR_ENABLE_LORA)
        message(FATAL_ERROR "PURR_ENABLE_MESH=1 requires PURR_ENABLE_LORA=1")
    endif()
    set(_mesh_header "${CMAKE_CURRENT_SOURCE_DIR}/../components/meshtastic/src/MeshService.h")
    if(NOT EXISTS "${_mesh_header}")
        message(WARNING "Meshtastic submodule not present -- mesh disabled. Clone it (see HOWTO.md §Meshtastic).")
        set(PURR_ENABLE_MESH 0 CACHE STRING "" FORCE)
    else()
        list(APPEND SRCS "../system/kernel/modules/mesh_manager.cpp")
        list(APPEND INCLUDE_DIRS "../components/meshtastic/src")
        list(APPEND REQUIRES meshtastic)
        message(STATUS "Meshtastic co-resident mesh enabled.")
    endif()
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

# ── Device-specific: CYD ─────────────────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "cyd")
    list(APPEND SRCS
        "../system/kernel/modules/display_ili9341.cpp"
        "../system/kernel/modules/touch_cst816s.cpp"
        "../system/kernel/modules/partition_manager.cpp"
        "../system/kernel/modules/purr_bootloader.cpp"
        "../system/kernel/modules/explorer.cpp"
    )
    list(APPEND INCLUDE_DIRS "../components/TFT_eSPI")
    list(APPEND REQUIRES fatfs esp_partition TFT_eSPI)

# ── Device-specific: T-Deck ──────────────────────────────────────────────────
elseif(TARGET_DEVICE STREQUAL "tdeck")
    set(BUILD_TDECK_PLUS 0 CACHE STRING "T-Deck variant: 0 = original, 1 = Plus (GPS + larger battery)")
    list(APPEND SRCS
        "../system/kernel/modules/display_ssd1306.cpp"
        "../apps/smol/smol.cpp"
    )
    list(APPEND INCLUDE_DIRS "../apps/smol")
    if(BUILD_TDECK_PLUS)
        list(APPEND SRCS "../system/kernel/modules/gps_manager.cpp")
        message(STATUS "T-Deck Plus: MIA-M10Q GPS stub included.")
    endif()
    message(WARNING "T-Deck: placeholder build -- ST7789 driver and BB6 shell not yet wired.")

else()
    message(FATAL_ERROR "Unknown TARGET_DEVICE='${TARGET_DEVICE}'. Valid: heltec, cyd, tdeck")
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
elseif(TARGET_DEVICE STREQUAL "cyd")
    list(APPEND PURR_DEFS
        PURR_DISPLAY_ILI9341=1
        PURR_HAS_BOOTLOADER=1
        PURR_HAS_EXPLORER=1
        PURR_HAS_TOUCH_CST816S=1
        PURR_HAS_PARTITION_MGR=1
        CYD_VARIANT_S024C=1
    )
elseif(TARGET_DEVICE STREQUAL "tdeck")
    list(APPEND PURR_DEFS PURR_DISPLAY_SSD1306=1)
    if(BUILD_TDECK_PLUS)
        list(APPEND PURR_DEFS PURR_IS_TDECK_PLUS=1)
    endif()
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
