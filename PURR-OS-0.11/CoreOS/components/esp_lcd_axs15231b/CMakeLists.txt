idf_component_register(SRCS "esp_lcd_axs15231b.c" "esp_lcd_axs15231b_mipi.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "priv_include"
                       REQUIRES "driver" "esp_lcd" "espressif__esp_lcd_touch")

# Version macros normally injected by cmake_utilities/package_manager (managed component).
# Define them manually since this component is local (no idf_component.yml processing).
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESP_LCD_AXS15231B_VER_MAJOR=2
    ESP_LCD_AXS15231B_VER_MINOR=1
    ESP_LCD_AXS15231B_VER_PATCH=0
)
