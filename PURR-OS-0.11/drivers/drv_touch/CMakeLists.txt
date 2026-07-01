set(SRCS)
set(PURR_TOUCH_CST816S OFF)

if(TARGET_DEVICE STREQUAL "cyd_boot" OR TARGET_DEVICE STREQUAL "waveshare169")
    list(APPEND SRCS "touch_cst816s.cpp")
    set(PURR_TOUCH_CST816S ON)
elseif(TARGET_DEVICE STREQUAL "cyd_s024c")
    list(APPEND SRCS "touch_cst816s.cpp")
    set(PURR_TOUCH_CST816S ON)
elseif(TARGET_DEVICE STREQUAL "cyd_s028r" OR TARGET_DEVICE STREQUAL "cyd")
    if(CYD_DISPLAY_VARIANT STREQUAL "s024c")
        list(APPEND SRCS "touch_cst816s.cpp")
        set(PURR_TOUCH_CST816S ON)
    else()
        list(APPEND SRCS "touch_xpt2046.cpp")
    endif()
elseif(TARGET_DEVICE STREQUAL "jc3248w535" OR TARGET_DEVICE STREQUAL "tdeck_plus")
    list(APPEND SRCS "touch_gt911.cpp")
endif()

if(NOT SRCS)
    idf_component_register()
    return()
endif()

idf_component_register(
    SRCS         ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES     driver
)

idf_component_get_property(gpio_lib   esp_driver_gpio COMPONENT_LIB)
idf_component_get_property(i2c_lib    esp_driver_i2c  COMPONENT_LIB)
idf_component_get_property(spi_lib    esp_driver_spi  COMPONENT_LIB)
target_link_libraries(${COMPONENT_LIB} PRIVATE ${gpio_lib} ${i2c_lib} ${spi_lib})

if(PURR_TOUCH_CST816S)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC PURR_HAS_CST816S_TOUCH=1 PURR_HAS_TOUCH_CST816S=1)
else()
    # XPT2046 or GT911 — advertise which one so consumers (drv_shell etc.) can branch
    if(TARGET_DEVICE STREQUAL "cyd_s028r" OR TARGET_DEVICE STREQUAL "cyd")
        target_compile_definitions(${COMPONENT_LIB} PUBLIC PURR_HAS_TOUCH_XPT2046=1)
    elseif(TARGET_DEVICE STREQUAL "jc3248w535" OR TARGET_DEVICE STREQUAL "tdeck_plus")
        target_compile_definitions(${COMPONENT_LIB} PUBLIC PURR_HAS_TOUCH_GT911=1)
    endif()
endif()

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../CoreOS/system/kernel/modules"
)
