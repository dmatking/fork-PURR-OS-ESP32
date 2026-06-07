cmake_minimum_required(VERSION 3.16)

# Shells/ lives next to CoreOS/ — register as extra component dirs so IDF finds them
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../Shells")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "0.9.0")
project(purr_os_core)
