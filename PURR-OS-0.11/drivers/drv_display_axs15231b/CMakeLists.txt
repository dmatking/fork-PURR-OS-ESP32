idf_component_register(
    SRCS         "display_axs15231b.cpp"
    INCLUDE_DIRS "."
    REQUIRES     driver esp_lcd esp_timer esp_driver_ledc esp_lcd_axs15231b esp_driver_i2c
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC PURR_DISPLAY_AXS15231B=1)

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../CoreOS/system/kernel/modules"
    "${CMAKE_CURRENT_SOURCE_DIR}/../drv_display"
)
