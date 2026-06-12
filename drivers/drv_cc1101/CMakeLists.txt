if(NOT PURR_ENABLE_CC1101)
    idf_component_register()
    return()
endif()

idf_component_register(
    SRCS         "cc1101_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES     driver
    PRIV_REQUIRES lib_radiolib
)

# IDF 5.3 doesn't always propagate REQUIRES include paths for local components.
idf_component_get_property(radiolib_dir lib_radiolib COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${radiolib_dir}/src")

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../CoreOS/system/kernel/modules"
)
