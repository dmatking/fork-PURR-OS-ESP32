cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(catto_hid)

# Target: ESP32-S2 (pure USB HID keyboard controller)
# Board: ESP32-S2-WROVER as on Page 3 of board.md
# USB: native hardware GPIO19/20 (schematic labels IO22/IO23 on module connector)

idf_component_register(
    SRCS
        "main.cpp"
        "keyboard_matrix.cpp"
        "usb_hid.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        arduino
        usb
)
