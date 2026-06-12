if(NOT PURR_ENABLE_WIFI AND TARGET_DEVICE STREQUAL "heltec")
    idf_component_register(
        SRCS         "wifi_manager_stub.cpp"
        INCLUDE_DIRS "."
    )
    return()
endif()

idf_component_register(
    SRCS         "wifi_manager.cpp"
    INCLUDE_DIRS "."
)

# Use cmake's target_link_libraries so that interface properties (include dirs)
# properly propagate — IDF's PRIV_REQUIRES uses set_property which bypasses this.
idf_component_get_property(esp_wifi_lib esp_wifi COMPONENT_LIB)
idf_component_get_property(nvs_flash_lib nvs_flash COMPONENT_LIB)
idf_component_get_property(esp_event_lib esp_event COMPONENT_LIB)
idf_component_get_property(esp_netif_lib esp_netif COMPONENT_LIB)
target_link_libraries(${COMPONENT_LIB} PRIVATE
    ${esp_wifi_lib} ${nvs_flash_lib} ${esp_event_lib} ${esp_netif_lib}
)

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../system/kernel/modules"
)
