set(_ipc_requires "")
if(PURR_ENABLE_LORA)
    list(APPEND _ipc_requires drv_lora)
endif()
if(PURR_ENABLE_WIFI)
    list(APPEND _ipc_requires drv_wifi)
endif()
if(PURR_ENABLE_BT)
    list(APPEND _ipc_requires drv_bt)
endif()

idf_component_register(
    SRCS
        "purr_dos_ipc.c"
    INCLUDE_DIRS "."
    REQUIRES     ${_ipc_requires}
)

# Propagate PURR feature flags into this component so the ifdefs in purr_dos_ipc.c work
if(PURR_ENABLE_LORA)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PURR_HAS_LORA=1)
    idf_component_get_property(drv_lora_inc drv_lora INCLUDE_DIRS)
    target_include_directories(${COMPONENT_LIB} PRIVATE ${drv_lora_inc})
endif()
if(PURR_ENABLE_WIFI)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PURR_HAS_WIFI=1)
    idf_component_get_property(drv_wifi_inc drv_wifi INCLUDE_DIRS)
    target_include_directories(${COMPONENT_LIB} PRIVATE ${drv_wifi_inc})
endif()
if(PURR_ENABLE_BT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PURR_HAS_BT=1)
    idf_component_get_property(drv_bt_inc drv_bt INCLUDE_DIRS)
    target_include_directories(${COMPONENT_LIB} PRIVATE ${drv_bt_inc})
endif()
