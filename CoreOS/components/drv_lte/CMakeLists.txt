# LTE driver — experimental, not yet implemented.
# Enable with -DPURR_ENABLE_LTE=1 when hardware support is added.

if(NOT PURR_ENABLE_LTE)
    idf_component_register()
    return()
endif()

idf_component_register(
    SRCS         "lte_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES     driver
)

message(WARNING "drv_lte: EXPERIMENTAL — LTE support is not yet implemented")
