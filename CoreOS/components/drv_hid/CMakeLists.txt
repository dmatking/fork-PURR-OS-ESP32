set(SRCS "keyboard_matrix.cpp")
set(_HAS_USB FALSE)
if(PURR_HAS_USB_HID AND (IDF_TARGET STREQUAL "esp32s2" OR IDF_TARGET STREQUAL "esp32s3"))
    list(APPEND SRCS "usb_hid.cpp")
    set(_HAS_USB TRUE)
endif()

idf_component_register(
    SRCS         ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES     driver
)

if(_HAS_USB)
    idf_component_get_property(arduino_dir espressif__arduino-esp32 COMPONENT_DIR)
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "${arduino_dir}/cores/esp32"
        "${arduino_dir}/libraries/USB/src"
    )
endif()
