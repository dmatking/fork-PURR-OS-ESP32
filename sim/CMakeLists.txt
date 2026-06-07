cmake_minimum_required(VERSION 3.16)
project(purr_sim C CXX)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)

# ── Shell selection ───────────────────────────────────────────────────────────
set(PURR_SHELL "blackberry" CACHE STRING "UI shell to simulate (blackberry|explorer|classic)")
set_property(CACHE PURR_SHELL PROPERTY STRINGS blackberry explorer classic)
message(STATUS "purr_sim: PURR_SHELL=${PURR_SHELL}")

# ── Paths ─────────────────────────────────────────────────────────────────────
set(SIM_DIR     "${CMAKE_CURRENT_SOURCE_DIR}")
set(COREOS_DIR  "${SIM_DIR}/../CoreOS")
set(MW_ROOT     "${COREOS_DIR}/components/miniwinwm/MiniWin")
set(WIN_HAL     "${MW_ROOT}/hal/windows")
set(COMPAT_DIR  "${SIM_DIR}/compat")
set(GEN_DIR     "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(MAKE_DIRECTORY "${GEN_DIR}")

# ── Run the shell generator ───────────────────────────────────────────────────
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(GEN_SRC  "${GEN_DIR}/shell_sim.cpp")
set(GEN_INFO "${GEN_DIR}/shell_sim.cmake")

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${SIM_DIR}/gen_sim_shell.py"
            "--shell" "${PURR_SHELL}"
            "--out"   "${GEN_SRC}"
    WORKING_DIRECTORY "${SIM_DIR}"
    RESULT_VARIABLE GEN_RESULT
)
if(NOT GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "gen_sim_shell.py failed (exit ${GEN_RESULT})")
endif()

# Pull in width/height/title from the generated .cmake file
if(EXISTS "${GEN_INFO}")
    include("${GEN_INFO}")
endif()

# Defaults if generator didn't set them
if(NOT DEFINED SIM_SHELL_WIDTH)  set(SIM_SHELL_WIDTH  320) endif()
if(NOT DEFINED SIM_SHELL_HEIGHT) set(SIM_SHELL_HEIGHT 240) endif()
if(NOT DEFINED SIM_SHELL_TITLE)  set(SIM_SHELL_TITLE  "PURR OS Sim") endif()

message(STATUS "purr_sim: ${SIM_SHELL_WIDTH}x${SIM_SHELL_HEIGHT}  \"${SIM_SHELL_TITLE}\"")

# ── MiniWin core sources ──────────────────────────────────────────────────────
file(GLOB_RECURSE MINIWIN_SRCS "${MW_ROOT}/*.c")
list(FILTER MINIWIN_SRCS EXCLUDE REGEX ".*/hal/[^/]+/.+\\.c$")  # strip platform HALs
list(FILTER MINIWIN_SRCS EXCLUDE REGEX ".*/dialog_file_chooser\\.c$")
list(FILTER MINIWIN_SRCS EXCLUDE REGEX ".*/fonts/truetype/[^/]+\\.c$")

# ── Windows HAL ───────────────────────────────────────────────────────────────
set(HAL_SRCS
    "${WIN_HAL}/hal_delay.c"
    "${WIN_HAL}/hal_non_vol.c"
    "${WIN_HAL}/hal_timer.c"
    "${WIN_HAL}/hal_touch.c"
    "${SIM_DIR}/hal_lcd_sim.c"
)

# ── Shell sources ─────────────────────────────────────────────────────────────
if(PURR_SHELL STREQUAL "classic")
    set(SHELL_SRCS "${SIM_DIR}/mw_user.c")
else()
    set(SHELL_SRCS "${GEN_SRC}")
endif()

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(purr_sim WIN32
    ${MINIWIN_SRCS}
    ${HAL_SRCS}
    ${SHELL_SRCS}
    "${SIM_DIR}/main_sim.cpp"
    "${SIM_DIR}/kitt_sim.cpp"
)

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(purr_sim PRIVATE
    "${MW_ROOT}"
    "${MW_ROOT}/hal"
    "${MW_ROOT}/gl/fonts/truetype/mcufont"
    "${SIM_DIR}"                # app.h, miniwin_config.h (sim version)
    "${COMPAT_DIR}"             # Arduino_sim.h, kitt_sim.h, etc.
    "${COMPAT_DIR}/freertos"    # FreeRTOS_sim.h, task_sim.h
)

# ── Compile definitions ───────────────────────────────────────────────────────
target_compile_definitions(purr_sim PRIVATE
    PURR_SIMULATOR=1
    SIM_W=${SIM_SHELL_WIDTH}
    SIM_H=${SIM_SHELL_HEIGHT}
    "SIM_TITLE=L\"${SIM_SHELL_TITLE}\""
    SIM_SHELL_WIDTH=${SIM_SHELL_WIDTH}
    SIM_SHELL_HEIGHT=${SIM_SHELL_HEIGHT}
    _CRT_SECURE_NO_WARNINGS
    PURR_CYD=1
)

# ── Suppress upstream warnings ────────────────────────────────────────────────
if(MSVC)
    target_compile_options(purr_sim PRIVATE $<$<COMPILE_LANGUAGE:C>:/W0>)
    target_compile_options(purr_sim PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W1 /wd4996>)
else()
    target_compile_options(purr_sim PRIVATE -w)
endif()

target_link_libraries(purr_sim PRIVATE gdi32 winmm)

message(STATUS "purr_sim: ready — cmake --build . --target purr_sim")
