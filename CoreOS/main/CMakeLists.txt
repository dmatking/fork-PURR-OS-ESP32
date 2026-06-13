# purr_os main component — kernel spine
# Sources: source/kernel/core/boot.c + purr_kernel.c
# Headers exposed to all extra components via INCLUDE_DIRS.

set(KERNEL_DIR  ${CMAKE_SOURCE_DIR}/../source/kernel/core)
set(CATCALL_DIR ${CMAKE_SOURCE_DIR}/../source/kernel/catcalls)

idf_component_register(
    SRCS
        ${KERNEL_DIR}/boot.c
        ${KERNEL_DIR}/purr_kernel.c
    INCLUDE_DIRS
        ${KERNEL_DIR}
        ${CATCALL_DIR}
    REQUIRES
        esp_common
        esp_system
        esp_timer
        driver
        freertos
        nvs_flash
        spiffs
        fatfs
        sdmmc
)
