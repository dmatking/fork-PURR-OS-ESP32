# LoRa driver — kernel selected via PURR_LORA_KERNEL (sx1262 | sx1276 | rak3172)
# Defaults to sx1262 for heltec/tdeck. Override with -DPURR_LORA_KERNEL=rak3172 etc.

if(NOT PURR_ENABLE_LORA)
    idf_component_register()
    return()
endif()

set(PURR_LORA_KERNEL "sx1262" CACHE STRING "LoRa kernel: sx1262 | sx1276 | rak3172")

set(SRCS "lora_manager.cpp")

if(PURR_LORA_KERNEL STREQUAL "rak3172")
    list(APPEND SRCS "kernels/rak3172/lora_manager.cpp")
elseif(PURR_LORA_KERNEL STREQUAL "sx1276")
    list(APPEND SRCS "kernels/sx1276/lora_manager.cpp")
else()
    list(APPEND SRCS "kernels/sx1262/lora_manager.cpp")
endif()

idf_component_register(
    SRCS         ${SRCS}
    INCLUDE_DIRS "." "kernels/${PURR_LORA_KERNEL}"
    REQUIRES     lib_radiolib lib_arduino driver
)

message(STATUS "drv_lora: kernel=${PURR_LORA_KERNEL}")
