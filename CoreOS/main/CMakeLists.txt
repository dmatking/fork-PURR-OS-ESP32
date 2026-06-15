# purr_os main component — kernel spine
# Selects specialized kernel boot (kernel_<device>/) if present, falls back
# to core/boot.c for all other devices.
# Arduino kernels (kernel_arduino_*) glob .cpp in addition to .c and add arduino REQUIRES.

set(KERNEL_DIR  ${CMAKE_SOURCE_DIR}/../source/kernel/core)
set(CATCALL_DIR ${CMAKE_SOURCE_DIR}/../source/kernel/catcalls)

set(PURR_DEVICE $ENV{PURR_DEVICE})
if(NOT PURR_DEVICE)
    message(FATAL_ERROR "PURR_DEVICE env var not set — run via purrstrap, not idf.py directly")
endif()
set(GLUE_DIR ${CMAKE_SOURCE_DIR}/../cattobaked/${PURR_DEVICE}/glue)

# Check for a specialized kernel folder — kernel_<device>/
set(SPEC_KERNEL_DIR ${CMAKE_SOURCE_DIR}/../source/kernel/kernel_${PURR_DEVICE})
if(EXISTS ${SPEC_KERNEL_DIR})
    file(GLOB SPEC_SRCS ${SPEC_KERNEL_DIR}/*.c ${SPEC_KERNEL_DIR}/*.cpp)
    set(BOOT_SRCS ${SPEC_SRCS})
    message(STATUS "PURR: using specialized kernel: kernel_${PURR_DEVICE}")
    set(SPEC_INCLUDE_DIRS
        ${SPEC_KERNEL_DIR}
        ${CMAKE_SOURCE_DIR}/../source/kernel/kernel_arduino)
else()
    set(BOOT_SRCS ${KERNEL_DIR}/boot.c)
    set(SPEC_INCLUDE_DIRS "")
    message(STATUS "PURR: using generic kernel for ${PURR_DEVICE}")
endif()

# Arduino kernels need the arduino component; standard kernels need gt911/bbq20
string(FIND "${PURR_DEVICE}" "arduino" _IS_ARDUINO_KERNEL)
if(_IS_ARDUINO_KERNEL GREATER -1)
    set(EXTRA_REQUIRES espressif__arduino-esp32)
    set(DRIVER_REQUIRES st7789 trackball)
    message(STATUS "PURR: Arduino kernel — using Wire for I2C peripherals")
else()
    set(EXTRA_REQUIRES "")
    set(DRIVER_REQUIRES st7789 gt911 trackball bbq20)
endif()

idf_component_register(
    SRCS
        ${BOOT_SRCS}
        ${KERNEL_DIR}/purr_kernel.c
        ${GLUE_DIR}/purr_device_glue.c
    INCLUDE_DIRS
        ${KERNEL_DIR}
        ${CATCALL_DIR}
        ${SPEC_INCLUDE_DIRS}
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
        ${DRIVER_REQUIRES}
        ${EXTRA_REQUIRES}
)
