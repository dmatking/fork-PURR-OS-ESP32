idf_component_register(
    SRCS
        "src/lapi.c"
        "src/lcode.c"
        "src/lctype.c"
        "src/ldebug.c"
        "src/ldo.c"
        "src/ldump.c"
        "src/lfunc.c"
        "src/lgc.c"
        "src/linit.c"
        "src/llex.c"
        "src/lmem.c"
        "src/lobject.c"
        "src/lopcodes.c"
        "src/lparser.c"
        "src/lstate.c"
        "src/lstring.c"
        "src/ltable.c"
        "src/ltm.c"
        "src/lundump.c"
        "src/lvm.c"
        "src/lzio.c"
        "src/lauxlib.c"
        "src/lbaselib.c"
        "src/lcorolib.c"
        "src/ldblib.c"
        "src/liolib.c"
        "src/lmathlib.c"
        "src/loadlib.c"
        "src/loslib.c"
        "src/lstrlib.c"
        "src/ltablib.c"
        "src/lutf8lib.c"
    INCLUDE_DIRS
        "src"
)

# ESP32 Xtensa toolchain doesn't expose long long the way Lua 5.4 expects.
# LUA_32BITS uses int32 for lua_Integer and float for lua_Number — correct for ESP32.
target_compile_definitions(__idf_lua PUBLIC LUA_32BITS=1)

# Suppress warnings from Lua upstream C sources
target_compile_options(__idf_lua PRIVATE -w)
