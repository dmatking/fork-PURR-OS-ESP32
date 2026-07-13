idf_component_register(
    SRCS
        proximity_module.c
    INCLUDE_DIRS
        .
        ../../kernel/core
        ../../kernel/catcalls
    REQUIRES esp_common driver freertos esp_wifi esp_event esp_netif
)
