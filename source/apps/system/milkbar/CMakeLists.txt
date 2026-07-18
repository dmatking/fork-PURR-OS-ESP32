idf_component_register(
    SRCS
        milkbar_app.c
    INCLUDE_DIRS
        .
        ../../../kernel/core
        ../../../kernel/catcalls
        ../../../modules/pairing
        ../../../modules/proximity_rpc
        ../../../modules/app_manager_remote
    REQUIRES esp_common driver freertos miniwin cupcake app_manager pairing proximity_rpc app_manager_remote
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)
