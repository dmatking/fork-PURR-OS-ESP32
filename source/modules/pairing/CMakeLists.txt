idf_component_register(
    SRCS
        pairing_module.c
    INCLUDE_DIRS
        .
        ../../kernel/core
        ../../kernel/catcalls
        ../proximity
    REQUIRES esp_common driver freertos nvs_flash esp_hw_support proximity
)
