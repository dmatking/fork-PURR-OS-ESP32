file(GLOB_RECURSE RADIOLIB_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

idf_component_register(
    SRCS         ${RADIOLIB_SRCS}
    INCLUDE_DIRS "src"
    REQUIRES     driver
)

# RadioLib pure-IDF mode — no Arduino dependency
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    RADIOLIB_GODMODE=1
    RADIOLIB_STATIC_ONLY=1
)

# Suppress warnings from the library so they don't pollute PURR OS build output
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-missing-field-initializers
    -Wno-implicit-fallthrough
)
