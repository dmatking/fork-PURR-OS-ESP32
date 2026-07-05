# Vendors nanopb + the Meshtastic protobufs directly into this component
# (source/lib/lib_nanopb, source/lib/lib_mesh_pb) rather than registering
# them as separate ESP-IDF components — modulestrap's component discovery
# only scans source/modules/*/module.pcat and source/drivers/*/*/driver.pcat,
# so a bare source/lib/ folder wouldn't be picked up on its own. Keeping the
# vendored sources private to this module's SRCS/INCLUDE_DIRS avoids adding
# a new "lib" concept to the build system for one module's dependency.
idf_component_register(
    SRCS
        meshtastic_module.c
        mesh_radio.c
        mesh_router.c
        mesh_ble.c
        ../../lib/lib_nanopb/pb_common.c
        ../../lib/lib_nanopb/pb_encode.c
        ../../lib/lib_nanopb/pb_decode.c
        ../../lib/lib_mesh_pb/meshtastic/mesh.pb.c
        ../../lib/lib_mesh_pb/meshtastic/portnums.pb.c
        ../../lib/lib_mesh_pb/meshtastic/channel.pb.c
        ../../lib/lib_mesh_pb/meshtastic/config.pb.c
        ../../lib/lib_mesh_pb/meshtastic/device_ui.pb.c
        ../../lib/lib_mesh_pb/meshtastic/module_config.pb.c
        ../../lib/lib_mesh_pb/meshtastic/telemetry.pb.c
        ../../lib/lib_mesh_pb/meshtastic/xmodem.pb.c
    INCLUDE_DIRS
        .
        ../../kernel/core
        ../../kernel/catcalls
        ../../lib/lib_nanopb
        ../../lib/lib_mesh_pb
    REQUIRES esp_common driver freertos nvs_flash esp_timer mbedtls bt
)
