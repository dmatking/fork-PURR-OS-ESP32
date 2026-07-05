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

# Guard: the WinCE shell is baked directly into a kernel's boot.c/.cpp (no
# catcall_ui_t wrapper) and only steps aside correctly when MiniWin is the
# selected UI backend (see miniwin_module.c's own CONFIG_PURR_UI_WINCE_SHELL
# check). Enabling WinCE without MiniWin would produce a kernel where no UI
# system ever starts — fail the build instead of shipping a blank screen.
if(CONFIG_PURR_UI_WINCE_SHELL AND NOT CONFIG_PURR_UI_BACKEND_MINIWIN)
    message(FATAL_ERROR "CONFIG_PURR_UI_WINCE_SHELL requires CONFIG_PURR_UI_BACKEND_MINIWIN — check sdkconfig_${PURR_DEVICE} (generated) and sdkconfig_${PURR_DEVICE}.overrides")
endif()

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

# Arduino kernels use Wire for I2C; native kernels use IDF drivers. Which one
# a device is comes from device.pcat's [device] kernel_type field, resolved
# by purrstrap into PURR_KERNEL_TYPE — not guessed from the device name.
set(PURR_KERNEL_TYPE $ENV{PURR_KERNEL_TYPE})
if(NOT PURR_KERNEL_TYPE)
    message(FATAL_ERROR "PURR_KERNEL_TYPE env var not set — run via purrstrap, not idf.py directly")
endif()

if(PURR_KERNEL_TYPE STREQUAL "arduino")
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
        esp_netif
        esp_event
        esp_wifi
        driver
        freertos
        nvs_flash
        spiffs
        fatfs
        sdmmc
        ${DRIVER_REQUIRES}
        ${EXTRA_REQUIRES}
        miniwin
)
