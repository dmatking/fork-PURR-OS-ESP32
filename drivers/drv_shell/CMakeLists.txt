set(_shell_requires
    driver
    esp_driver_usb_serial_jtag
    esp_timer
    nvs_flash
    app_update
    drv_display
    drv_touch
    lib_miniwin
    drv_wifi
)
if(PURR_ENABLE_BT)
    list(APPEND _shell_requires drv_bt)
endif()

idf_component_register(
    SRCS
        "drv_shell.c"
        "shell_cmds_gpio.c"
        "shell_cmds_display.cpp"
        "shell_cmds_sys.c"
        "shell_cmds_miniwin.cpp"
        "shell_cmds_conman.cpp"
        "shell_cmds_script.cpp"
        "shell_cmds_drv.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../CoreOS/system/kernel/modules/purr_script.cpp"
    INCLUDE_DIRS "."
    REQUIRES ${_shell_requires}
)

# Kernel headers (purr_panic.h etc.)
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../CoreOS/system/kernel"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../CoreOS/system/kernel/modules"
)

if(PURR_ENABLE_BT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SHELL_HAS_BT=1)
endif()

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
