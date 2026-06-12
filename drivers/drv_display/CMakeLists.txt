set(SRCS)

if(TARGET_DEVICE STREQUAL "cyd" OR TARGET_DEVICE STREQUAL "cyd_boot"
   OR TARGET_DEVICE STREQUAL "cyd_s028r" OR TARGET_DEVICE STREQUAL "cyd_s024c")
    list(APPEND SRCS "display_ili9341.cpp")
elseif(TARGET_DEVICE STREQUAL "jc3248w535")
    list(APPEND SRCS "display_st7796.cpp")
elseif(TARGET_DEVICE STREQUAL "waveshare169" OR TARGET_DEVICE STREQUAL "tdeck_plus"
       OR TARGET_DEVICE STREQUAL "tdeck" OR TARGET_DEVICE STREQUAL "tembed_cc1101")
    list(APPEND SRCS "display_st7789.cpp")
elseif(TARGET_DEVICE STREQUAL "heltec")
    list(APPEND SRCS "display_ssd1306.cpp")
endif()

if(NOT SRCS)
    idf_component_register()
    return()
endif()

idf_component_register(
    SRCS         ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES     driver esp_lcd esp_timer esp_driver_ledc
)

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../system/kernel/modules"
)

# Propagate display-type defines as PUBLIC so consumers (drv_shell etc.) know
# which display driver is active — main's PURR_DEFS only reaches main's own
# compilation unit, not its dependencies.
if(TARGET_DEVICE STREQUAL "cyd" OR TARGET_DEVICE STREQUAL "cyd_boot"
   OR TARGET_DEVICE STREQUAL "cyd_s028r" OR TARGET_DEVICE STREQUAL "cyd_s024c")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC PURR_DISPLAY_ILI9341=1)
endif()
if(TARGET_DEVICE STREQUAL "jc3248w535")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC PURR_DISPLAY_ST7796=1)
endif()
if(TARGET_DEVICE STREQUAL "waveshare169" OR TARGET_DEVICE STREQUAL "tdeck_plus"
   OR TARGET_DEVICE STREQUAL "tdeck" OR TARGET_DEVICE STREQUAL "tembed_cc1101")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC PURR_DISPLAY_ST7789=1)
endif()
if(TARGET_DEVICE STREQUAL "heltec")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC PURR_DISPLAY_SSD1306=1)
endif()

# CYD S024C variant pin select — re-applied here so User_Setup.h and
# display_ili9341.h compile with the correct BL/RST assignments.
if(CYD_DISPLAY_VARIANT STREQUAL "s024c" OR TARGET_DEVICE STREQUAL "cyd_s024c")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CYD_VARIANT_S024C=1)
endif()
