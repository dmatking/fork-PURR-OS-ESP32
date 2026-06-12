idf_component_register(
    SRCS         "bt_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES     esp_timer
    PRIV_REQUIRES bt nvs_flash
)

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../system/kernel/modules"
)
