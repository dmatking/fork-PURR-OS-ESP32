cmake_minimum_required(VERSION 3.16)

# Shells/ lives next to CoreOS/ — register as extra component dirs so IDF finds them
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../Shells")

# MagiDOS emulator components (conditional)

# UI kernel: must be declared before component CMakeLists files are processed
# so lib_miniwin and other components can read it unconditionally.
# miniwin = MiniWin WM (CYD targets)   none = no UI framework
set(PURR_UI_KERNEL "none" CACHE STRING "UI framework: miniwin | none")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "0.10.0")
project(purr_os_core)

# Hardware support flags are injected by sdk_core.py via -D cmake args.
# Do not hardcode device-specific flags here — they would bleed into CYD builds.
