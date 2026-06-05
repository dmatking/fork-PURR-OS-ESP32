# MiniWin window manager — PURR OS IDF component wrapper
# Only active for CYD targets (ILI9341 + CST816S). All other targets compile empty.
#
# To activate:
#   Fork https://github.com/miniwinwm/miniwinwm and clone your fork here so that
#   MiniWin/miniwin.c appears at CoreOS/components/miniwinwm/MiniWin/miniwin.c
#   The PURR_CYD HAL in MiniWin/hal/PURR_CYD/ is checked in alongside the fork.

if(NOT TARGET_DEVICE STREQUAL "cyd")
    # cyd_boot draws directly to the display (no WM) — register empty to skip compilation.
    # All other non-CYD targets also skip.
    idf_component_register()
    return()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/miniwin.c")
    message(WARNING "miniwinwm: MiniWin source not found — clone your fork into CoreOS/components/miniwinwm/")
    idf_component_register()
    return()
endif()

# Core platform-independent WM sources
file(GLOB_RECURSE MINIWIN_CORE_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/*.c")
# Strip platform-specific HAL subdirectories — only PURR_CYD is used.
# hal/hal.c (the common HAL dispatcher that defines mw_hal_init) lives directly
# in hal/ and must be kept; only files in named subdirs (hal/Windows/, hal/Linux/,
# hal/STM32F4/, etc.) are excluded.
list(FILTER MINIWIN_CORE_SRCS EXCLUDE REGEX ".*/hal/[^/]+/.+\\.c$")
# File chooser dialog requires a user-supplied app.h and is not used in PURR OS
list(FILTER MINIWIN_CORE_SRCS EXCLUDE REGEX ".*/dialog_file_chooser\\.c$")
# Truetype font DATA files (not the mcufont renderer) — excluded to save flash;
# PURR OS uses bitmap fonts only (MW_GL_FONT_9 … MW_GL_FONT_24)
list(FILTER MINIWIN_CORE_SRCS EXCLUDE REGEX ".*/fonts/truetype/[^/]+\\.c$")

# PURR CYD HAL — C++ to bridge into Arduino/IDF display and touch drivers
set(MINIWIN_HAL_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/hal/PURR_CYD/hal_lcd.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/hal/PURR_CYD/hal_touch.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/hal/PURR_CYD/hal_timer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/hal/PURR_CYD/hal_delay.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/hal/PURR_CYD/hal_non_vol.cpp"
)

idf_component_register(
    SRCS         ${MINIWIN_CORE_SRCS} ${MINIWIN_HAL_SRCS}
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin"
                 "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/hal"
                 "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/hal/PURR_CYD"
                 "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/gl/fonts/truetype/mcufont"
    REQUIRES     espressif__arduino-esp32
                 nvs_flash
                 esp_timer
                 TFT_eSPI
)

# Tell MiniWin HAL files which platform they're compiling for
target_compile_definitions(${COMPONENT_LIB} PUBLIC PURR_CYD=1)

# IDF 5.3.x does not propagate include dirs through managed-component REQUIRES chains.
# Arduino.h transitively includes headers from many IDF components. Glob every
# IDF component's include/ dir in one shot rather than chasing them one by one.
idf_build_get_property(idf_path IDF_PATH)
file(GLOB IDF_ALL_COMPONENT_INCLUDES LIST_DIRECTORIES true
     "${idf_path}/components/*/include")
list(FILTER IDF_ALL_COMPONENT_INCLUDES EXCLUDE REGEX ".*/linux/include$")
list(FILTER IDF_ALL_COMPONENT_INCLUDES EXCLUDE REGEX ".*/riscv/include$")
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../managed_components/espressif__arduino-esp32/cores/esp32"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../managed_components/espressif__arduino-esp32/variants/esp32"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../system/kernel/modules"
    ${IDF_ALL_COMPONENT_INCLUDES}
    "${idf_path}/components/driver/deprecated"
    "${idf_path}/components/driver/touch_sensor/esp32/include"
)

message(STATUS "miniwinwm: PURR_CYD HAL registered (ILI9341 + CST816S)")
