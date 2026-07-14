idf_component_register(
    SRCS
        m5tab5_bsp.cpp
    INCLUDE_DIRS
        .
        ../../../kernel/core
        ../../../kernel/catcalls
    REQUIRES esp_common driver freertos esp_timer espp__m5stack-tab5
)
