# MiniWin window manager — PURR OS IDF component wrapper
#
# Device HAL lives in devices/<folder>/. Each folder provides:
#   hal_lcd.cpp  hal_touch.cpp  hal_delay.cpp  hal_timer.cpp
#   hal_non_vol.cpp  miniwin_config.h  purr_app.cpp
#
# To activate:
#   Fork https://github.com/miniwinwm/miniwinwm and clone your fork here so that
#   MiniWin/miniwin.c appears at CoreOS/components/lib_miniwin/MiniWin/miniwin.c
#   The device HAL in devices/<target>/ is checked in alongside the fork.

if(NOT PURR_UI_KERNEL STREQUAL "miniwin")
    idf_component_register()
    return()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/miniwin.c")
    message(WARNING "miniwinwm: MiniWin source not found — clone your fork into CoreOS/components/lib_miniwin/")
    idf_component_register()
    return()
endif()

# ── Map TARGET_DEVICE → devices/<folder> ──────────────────────────────────────
if(TARGET_DEVICE STREQUAL "cyd_s028r")
    set(PURR_MW_DEVICE "cyd_s028r")
elseif(TARGET_DEVICE MATCHES "^cyd")   # cyd, cyd_s024c, cyd_boot
    set(PURR_MW_DEVICE "cyd")
elseif(TARGET_DEVICE STREQUAL "tdeck_plus")
    set(PURR_MW_DEVICE "tdeck_plus")
elseif(TARGET_DEVICE STREQUAL "jc3248w535")
    set(PURR_MW_DEVICE "jc3248")
elseif(TARGET_DEVICE STREQUAL "waveshare169")
    set(PURR_MW_DEVICE "waveshare")
else()
    set(PURR_MW_DEVICE "generic")
endif()

set(PURR_MW_HAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../devices/${PURR_MW_DEVICE}")
message(STATUS "lib_miniwin: device=${PURR_MW_DEVICE}  hal=${PURR_MW_HAL_DIR}")

# ── Core platform-independent WM sources ──────────────────────────────────────
file(GLOB_RECURSE MINIWIN_CORE_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/*.c")
# Strip upstream platform HAL subdirs — only files directly in hal/ (hal.c etc.) are kept
list(FILTER MINIWIN_CORE_SRCS EXCLUDE REGEX ".*/hal/[^/]+/.+\\.c$")
# File chooser dialog requires a user-supplied app.h — not used in PURR OS
list(FILTER MINIWIN_CORE_SRCS EXCLUDE REGEX ".*/dialog_file_chooser\\.c$")
# Truetype font data files — excluded to save flash; PURR OS uses bitmap fonts only
list(FILTER MINIWIN_CORE_SRCS EXCLUDE REGEX ".*/fonts/truetype/[^/]+\\.c$")

# ── Device HAL sources ────────────────────────────────────────────────────────
set(MINIWIN_HAL_SRCS
    "${PURR_MW_HAL_DIR}/hal_lcd.cpp"
    "${PURR_MW_HAL_DIR}/hal_touch.cpp"
    "${PURR_MW_HAL_DIR}/hal_timer.cpp"
    "${PURR_MW_HAL_DIR}/hal_delay.cpp"
    "${PURR_MW_HAL_DIR}/hal_non_vol.cpp"
    "${PURR_MW_HAL_DIR}/purr_app.cpp"
)

# ── Shared cross-UI app windows ───────────────────────────────────────────────
set(PURR_MW_APPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../devices/apps")
file(GLOB PURR_MW_APP_SRCS "${PURR_MW_APPS_DIR}/*.cpp")

set(_mw_requires nvs_flash esp_timer esp_lcd log driver)
if(PURR_ENABLE_LUA)
    list(APPEND _mw_requires lib_lua)
endif()

idf_component_register(
    SRCS         ${MINIWIN_CORE_SRCS} ${MINIWIN_HAL_SRCS} ${PURR_MW_APP_SRCS}
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin"
                 "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/hal"
                 "${PURR_MW_HAL_DIR}"
                 "${PURR_MW_APPS_DIR}"
                 "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/gl/fonts/truetype/mcufont"
    REQUIRES     ${_mw_requires}
)

# Feature flag propagation — lib_miniwin compiles devices/apps/ so it needs these defines
if(PURR_ENABLE_LUA)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PURR_HAS_LUA=1)
endif()
if(PURR_ENABLE_MAGIDOS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PURR_HAS_MAGIDOS=1)
endif()
if(PURR_ENABLE_MAGICMAC)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PURR_HAS_MAGICMAC=1)
endif()

# IDF 5.3.x does not propagate include dirs through managed-component REQUIRES chains.
# Arduino.h transitively includes headers from many IDF components. Glob every
# IDF component's include/ dir in one shot rather than chasing them one by one.
idf_build_get_property(idf_path IDF_PATH)
file(GLOB IDF_ALL_COMPONENT_INCLUDES LIST_DIRECTORIES true
     "${idf_path}/components/*/include")
list(FILTER IDF_ALL_COMPONENT_INCLUDES EXCLUDE REGEX ".*/linux/include$")
list(FILTER IDF_ALL_COMPONENT_INCLUDES EXCLUDE REGEX ".*/riscv/include$")
set(_mw_include_extra "")
if(PURR_ENABLE_LUA)
    list(APPEND _mw_include_extra "${CMAKE_CURRENT_SOURCE_DIR}/../lib_lua/src")
endif()
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../system/kernel"
    "${CMAKE_CURRENT_SOURCE_DIR}/../drv_display"
    "${CMAKE_CURRENT_SOURCE_DIR}/../drv_touch"
    ${_mw_include_extra}
    ${IDF_ALL_COMPONENT_INCLUDES}
    "${idf_path}/components/driver/deprecated"
    "${idf_path}/components/driver/touch_sensor/esp32/include"
)
