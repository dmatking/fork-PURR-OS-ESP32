idf_component_register(
    SRCS         "purr_wm.cpp"
                 "minwin_hal_adapter.cpp"
    INCLUDE_DIRS "."
    REQUIRES     drv_display drv_touch lib_miniwin lvgl__lvgl
)
