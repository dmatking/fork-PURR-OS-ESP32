set(SRCS "keyboard_matrix.cpp")
if(IDF_TARGET STREQUAL "esp32s2" OR IDF_TARGET STREQUAL "esp32s3")
    list(APPEND SRCS "usb_hid.cpp")
endif()

idf_component_register(
    SRCS         ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES     driver
)
