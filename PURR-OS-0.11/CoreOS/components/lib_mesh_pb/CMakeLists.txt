# Meshtastic protobuf definitions — only compiled when PURR_ENABLE_MESH=1.
# All other targets (cyd, cyd_boot, heltec without mesh) register empty.
if(NOT PURR_ENABLE_MESH)
    idf_component_register()
    return()
endif()

idf_component_register(
    SRCS
        "meshtastic/channel.pb.c"
        "meshtastic/config.pb.c"
        "meshtastic/device_ui.pb.c"
        "meshtastic/mesh.pb.c"
        "meshtastic/module_config.pb.c"
        "meshtastic/portnums.pb.c"
        "meshtastic/telemetry.pb.c"
        "meshtastic/xmodem.pb.c"
    INCLUDE_DIRS "."
    REQUIRES     lib_nanopb
)
