cmake_minimum_required(VERSION 3.16)
project(purr_sim C)

set(CMAKE_C_STANDARD 11)

set(MW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../CoreOS/components/miniwinwm/MiniWin")
set(WIN_HAL "${MW_ROOT}/hal/windows")

# MiniWin core sources — same exclusions as the IDF component
file(GLOB_RECURSE MINIWIN_SRCS "${MW_ROOT}/*.c")
list(FILTER MINIWIN_SRCS EXCLUDE REGEX ".*/hal/.*")
list(FILTER MINIWIN_SRCS EXCLUDE REGEX ".*/dialog_file_chooser\\.c$")
list(FILTER MINIWIN_SRCS EXCLUDE REGEX ".*/fonts/truetype/[^/]+\\.c$")

# Windows HAL (delay, non_vol, timer, touch) + sim LCD (320x240 landscape)
set(HAL_SRCS
    "${WIN_HAL}/hal_delay.c"
    "${WIN_HAL}/hal_non_vol.c"
    "${WIN_HAL}/hal_timer.c"
    "${WIN_HAL}/hal_touch.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/hal_lcd_sim.c"
)

add_executable(purr_sim WIN32
    ${MINIWIN_SRCS}
    ${HAL_SRCS}
    "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mw_user.c"
)

target_include_directories(purr_sim PRIVATE
    "${MW_ROOT}"
    "${MW_ROOT}/hal"
    "${MW_ROOT}/gl/fonts/truetype/mcufont"
    "${CMAKE_CURRENT_SOURCE_DIR}"   # app.h + miniwin_config.h
)

# Suppress warnings from MiniWin upstream C sources
if(MSVC)
    target_compile_options(purr_sim PRIVATE /W0)
else()
    target_compile_options(purr_sim PRIVATE -w)
endif()

target_link_libraries(purr_sim PRIVATE gdi32 winmm)

message(STATUS "purr_sim: 320x240 landscape, Windows GDI renderer")
