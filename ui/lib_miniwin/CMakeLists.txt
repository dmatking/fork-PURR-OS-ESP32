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
elseif(TARGET_DEVICE STREQUAL "cyd_s024c")
    set(PURR_MW_DEVICE "cyd_s024c")
elseif(TARGET_DEVICE MATCHES "^cyd")   # cyd, cyd_boot
    set(PURR_MW_DEVICE "cyd")
elseif(TARGET_DEVICE STREQUAL "tdeck_plus")
    set(PURR_MW_DEVICE "tdeck_plus")
elseif(TARGET_DEVICE STREQUAL "jc3248w535")
    set(PURR_MW_DEVICE "jc3248w535")
elseif(TARGET_DEVICE STREQUAL "waveshare169")
    set(PURR_MW_DEVICE "waveshare169")
else()
    set(PURR_MW_DEVICE "generic")
endif()

set(PURR_MW_HAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../devices/${PURR_MW_DEVICE}")
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
# T-Deck Plus has an extra input HAL (keyboard + trackball)
if(EXISTS "${PURR_MW_HAL_DIR}/hal_input.cpp")
    list(APPEND MINIWIN_HAL_SRCS "${PURR_MW_HAL_DIR}/hal_input.cpp")
endif()

# ── Shared cross-UI app windows ───────────────────────────────────────────────
set(PURR_MW_APPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../devices/apps")
file(GLOB PURR_MW_APP_SRCS "${PURR_MW_APPS_DIR}/*.cpp")

set(_mw_requires nvs_flash esp_timer esp_lcd log driver drv_shell drv_display)
if(PURR_ENABLE_LUA)
    list(APPEND _mw_requires lib_lua)
endif()

# Touch driver: compile the source directly into lib_miniwin (same pattern as
# MagiDOS drv_8086) to avoid cross-archive link-order issues where drv_touch
# is resolved before lib_miniwin in the final executable link command.
set(_drv_touch_dir "${CMAKE_CURRENT_SOURCE_DIR}/../drv_touch")
set(_mw_touch_src "")
if(TARGET_DEVICE STREQUAL "cyd_boot" OR TARGET_DEVICE STREQUAL "waveshare169"
   OR TARGET_DEVICE STREQUAL "cyd_s024c")
    set(_mw_touch_src "${_drv_touch_dir}/touch_cst816s.cpp")
    set(_mw_has_cst816s ON)
elseif(TARGET_DEVICE MATCHES "^cyd")
    if(CYD_DISPLAY_VARIANT STREQUAL "s024c")
        set(_mw_touch_src "${_drv_touch_dir}/touch_cst816s.cpp")
        set(_mw_has_cst816s ON)
    else()
        set(_mw_touch_src "${_drv_touch_dir}/touch_xpt2046.cpp")
    endif()
elseif(TARGET_DEVICE STREQUAL "jc3248w535" OR TARGET_DEVICE STREQUAL "tdeck_plus")
    set(_mw_touch_src "${_drv_touch_dir}/touch_gt911.cpp")
endif()

# MagiDOS: compile drv_8086.c (which #includes 8086tiny.c) directly into
# lib_miniwin so app_magidos.cpp.obj and drv_8086.c.obj live in the same
# archive — no cross-archive link-order dependency.
set(_mw_magidos_srcs "")
if(PURR_ENABLE_MAGIDOS)
    set(_drv8086_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../magidos/CoreOS/components/drv_8086")
    set(_ipc_dir     "${CMAKE_CURRENT_SOURCE_DIR}/../../magidos/CoreOS/components/lib_purr_dos_ipc")
    list(APPEND _mw_magidos_srcs
        "${_drv8086_dir}/drv_8086.c"
        "${_ipc_dir}/purr_dos_ipc.c"
    )
endif()

idf_component_register(
    SRCS         ${MINIWIN_CORE_SRCS} ${MINIWIN_HAL_SRCS} ${PURR_MW_APP_SRCS} ${_mw_magidos_srcs} ${_mw_touch_src}
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin"
                 "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/hal"
                 "${PURR_MW_HAL_DIR}"
                 "${PURR_MW_APPS_DIR}"
                 "${CMAKE_CURRENT_SOURCE_DIR}/MiniWin/gl/fonts/truetype/mcufont"
                 "${CMAKE_CURRENT_SOURCE_DIR}/../drv_shell"
    REQUIRES     ${_mw_requires}
)

# Device HAL (hal_lcd.cpp) may call esp_lcd_* or display_st7796/st7789_* depending
# on the target. Force-link both after lib_miniwin to fix link-order issues:
# IDF topological sort puts dependencies before lib_miniwin, but GNU ld needs them after.
idf_component_get_property(esp_lcd_lib esp_lcd COMPONENT_LIB)
target_link_libraries(${COMPONENT_LIB} PUBLIC ${esp_lcd_lib})
idf_component_get_property(drv_display_lib drv_display COMPONENT_LIB)
target_link_libraries(${COMPONENT_LIB} PUBLIC ${drv_display_lib})

# Advertise MiniWin presence to consumers (drv_shell etc.)
target_compile_definitions(${COMPONENT_LIB} PUBLIC PURR_HAS_MINIWIN=1)

# Touch driver flag
if(_mw_has_cst816s)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PURR_HAS_CST816S_TOUCH=1)
endif()

# Feature flag propagation — lib_miniwin compiles devices/apps/ so it needs these defines
if(PURR_ENABLE_LUA)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PURR_HAS_LUA=1)
endif()
if(PURR_ENABLE_MAGIDOS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PURR_HAS_MAGIDOS=1)
    # 8086tiny (compiled via drv_8086.c) uses intentional operator tricks.
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wno-parentheses -Wno-unused-value -Wno-misleading-indentation
        -Wno-implicit-fallthrough -Wno-address
        -Wno-error=parentheses -Wno-error=unused-value
        -Wno-error=misleading-indentation -Wno-error=implicit-fallthrough
        -Wno-error=address -Wno-error=comment -Wno-comment)
endif()
if(PURR_ENABLE_MAGICMAC)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PURR_HAS_MAGICMAC=1)
endif()
if(PURR_UI_THEME STREQUAL "blackberry")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PURR_THEME_BLACKBERRY=1)
endif()
if(PURR_DEBUG_INPUT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PURR_DEBUG_INPUT=1)
    message(STATUS "[purr] PURR_DEBUG_INPUT enabled — HID serial logging active")
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
if(PURR_ENABLE_MAGIDOS)
    list(APPEND _mw_include_extra
        "${CMAKE_CURRENT_SOURCE_DIR}/../../magidos/CoreOS/components/drv_8086"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../magidos/CoreOS/components/lib_purr_dos_ipc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../magidos/purr_wm_app/magidos"
    )
endif()
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../system/kernel"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../system/kernel/modules"
    "${CMAKE_CURRENT_SOURCE_DIR}/../drv_display"
    "${CMAKE_CURRENT_SOURCE_DIR}/../drv_touch"
    ${_mw_include_extra}
    ${IDF_ALL_COMPONENT_INCLUDES}
    "${idf_path}/components/driver/deprecated"
    "${idf_path}/components/driver/touch_sensor/esp32/include"
)
