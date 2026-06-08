idf_component_register(
    SRCS         "purr_wm.cpp"
    INCLUDE_DIRS "."
    REQUIRES     drv_display drv_touch lib_miniwin
)

# IDF 5.3 does not propagate include dirs through REQUIRES chains.
idf_component_get_property(miniwin_dir lib_miniwin COMPONENT_DIR)
if(miniwin_dir)
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "${miniwin_dir}/MiniWin"
        "${miniwin_dir}/MiniWin/hal/PURR_CYD"
    )
endif()
