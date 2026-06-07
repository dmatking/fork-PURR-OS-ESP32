idf_component_register(
    SRCS         "gps_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES     esp_timer driver
)
