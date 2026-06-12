idf_component_register(
    SRCS         "gps_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES     esp_timer driver
)

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../CoreOS/system/kernel/modules"
)
