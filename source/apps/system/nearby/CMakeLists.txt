idf_component_register(
    SRCS
        nearby_app.c
    INCLUDE_DIRS
        .
        ../../../kernel/core
        ../../../kernel/catcalls
        ../../../modules/proximity
        ../../../modules/pairing
    REQUIRES esp_common driver freertos miniwin app_manager proximity pairing
)
