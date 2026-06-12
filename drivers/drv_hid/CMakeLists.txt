set(SRCS "keyboard_matrix.cpp")
set(_HAS_USB FALSE)
if(PURR_HAS_USB_HID AND (IDF_TARGET STREQUAL "esp32s2" OR IDF_TARGET STREQUAL "esp32s3"))
    list(APPEND SRCS "usb_hid.cpp")
    set(_HAS_USB TRUE)
endif()

if(_HAS_USB)
    idf_component_register(
        SRCS         ${SRCS}
        INCLUDE_DIRS "."
        REQUIRES     driver tinyusb
    )
else()
    idf_component_register(
        SRCS         ${SRCS}
        INCLUDE_DIRS "."
        REQUIRES     driver
    )
endif()
