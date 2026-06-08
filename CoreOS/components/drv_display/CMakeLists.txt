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

# Propagate CYD_VARIANT_S024C into drv_display and lib_tftespi.
# main/CMakeLists.txt sets this in PURR_DEFS but that only reaches main's consumers,
# not its dependencies. We re-apply it here so User_Setup.h and display_ili9341.h
# both compile with the correct pin assignments.
if(CYD_DISPLAY_VARIANT STREQUAL "s024c" OR TARGET_DEVICE STREQUAL "cyd_s024c")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CYD_VARIANT_S024C=1)
    idf_component_get_property(tftespi_lib lib_tftespi COMPONENT_LIB)
    target_compile_definitions(${tftespi_lib} PUBLIC CYD_VARIANT_S024C=1)
endif()
