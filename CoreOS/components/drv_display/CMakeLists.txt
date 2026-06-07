set(SRCS)

if(TARGET_DEVICE STREQUAL "cyd" OR TARGET_DEVICE STREQUAL "cyd_boot"
   OR TARGET_DEVICE STREQUAL "cyd_s028r" OR TARGET_DEVICE STREQUAL "cyd_s024c")
    list(APPEND SRCS "display_ili9341.cpp")
elseif(TARGET_DEVICE STREQUAL "jc3248w535")
    list(APPEND SRCS "display_st7796.cpp")
elseif(TARGET_DEVICE STREQUAL "waveshare169")
    list(APPEND SRCS "display_st7789.cpp")
elseif(TARGET_DEVICE STREQUAL "heltec" OR TARGET_DEVICE STREQUAL "tdeck")
    list(APPEND SRCS "display_ssd1306.cpp")
endif()

if(NOT SRCS)
    idf_component_register()
    return()
endif()

idf_component_register(
    SRCS         ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES     driver esp_lcd esp_timer esp_driver_ledc lib_tftespi
)
