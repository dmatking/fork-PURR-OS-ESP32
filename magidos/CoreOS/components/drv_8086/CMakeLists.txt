idf_component_register(
    SRCS
        "8086tiny.c"
        "drv_8086.c"
    INCLUDE_DIRS "."
    REQUIRES lib_purr_dos_ipc
)
