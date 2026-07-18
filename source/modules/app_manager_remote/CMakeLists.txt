# esp32p4 (tab5): same as proximity/CMakeLists.txt — this rides on
# proximity_rpc, which cannot exist on the radio-less P4.
idf_build_get_property(purr_idf_target IDF_TARGET)
if("${purr_idf_target}" STREQUAL "esp32p4")
    idf_component_register()
    return()
endif()

idf_component_register(
    SRCS
        app_manager_remote.c
    INCLUDE_DIRS
        .
        ../../kernel/core
        ../app_manager
        ../proximity_rpc
    REQUIRES esp_common driver freertos app_manager proximity_rpc
)
