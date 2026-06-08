idf_component_register(
    SRCS
        "drv_shell.c"
        "shell_cmds_gpio.c"
        "shell_cmds_display.cpp"
        "shell_cmds_sys.c"
        "shell_cmds_miniwin.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_timer
        nvs_flash
        app_update
        drv_display
        drv_touch
        lib_miniwin
)

# IDF 5.x does not propagate symbols from REQUIRES into the final link for
# components that are static libs — force-link drv_display explicitly.
idf_component_get_property(drv_display_lib drv_display COMPONENT_LIB)
target_link_libraries(${COMPONENT_LIB} PRIVATE ${drv_display_lib})

# IDF 5.3 does not propagate include dirs through REQUIRES chains.
idf_component_get_property(miniwin_dir lib_miniwin COMPONENT_DIR)
if(miniwin_dir)
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "${miniwin_dir}/MiniWin"
        "${miniwin_dir}/MiniWin/hal"
        "${miniwin_dir}/MiniWin/hal/PURR_CYD"
    )
endif()
