idf_component_register(
    SRCS         "bt_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES     esp_timer
    PRIV_REQUIRES bt nvs_flash
)
