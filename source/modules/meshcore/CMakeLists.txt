# modules/meshcore/CMakeLists.txt
#
# Same rationale as meshtastic/CMakeLists.txt for vendoring nanopb/protobufs
# directly as SRCS/INCLUDE_DIRS rather than separate components: modulestrap's
# discovery only scans source/modules/*/module.pcat and
# source/drivers/*/*/driver.pcat, so a bare source/lib/ folder wouldn't be
# picked up on its own.
#
# vendor/*.cpp are MeshCore's real upstream files, unmodified (see
# vendor/VENDORED.md) — their rweather/Crypto dependency (AES128/SHA256) is
# satisfied by the compat/ shim headers instead of fetching that library,
# and Arduino's Stream (which their headers declare against, even where
# unused by this integration) comes from espressif__arduino-esp32, already
# a project-wide managed dependency (CoreOS/main/idf_component.yml) — no
# change to CoreOS/main/CMakeLists.txt needed, this is an ordinary
# component-to-component REQUIRES.

idf_component_register(
    SRCS
        meshcore_module.cpp
        mc_radio_adapter.cpp
        mc_packet_pool.cpp
        mc_contacts.cpp
        mc_identity.cpp
        vendor/Packet.cpp
        vendor/Dispatcher.cpp
        vendor/Mesh.cpp
        vendor/Identity.cpp
        vendor/Utils.cpp
        ../../lib/lib_ed25519/add_scalar.c
        ../../lib/lib_ed25519/fe.c
        ../../lib/lib_ed25519/ge.c
        ../../lib/lib_ed25519/key_exchange.c
        ../../lib/lib_ed25519/keypair.c
        ../../lib/lib_ed25519/sc.c
        ../../lib/lib_ed25519/seed.c
        ../../lib/lib_ed25519/sha512.c
        ../../lib/lib_ed25519/sign.c
        ../../lib/lib_ed25519/verify.c
    INCLUDE_DIRS
        vendor
        compat
        .
        ../../kernel/core
        ../../kernel/catcalls
        ../../lib/lib_ed25519
    REQUIRES esp_common driver freertos nvs_flash esp_timer mbedtls espressif__arduino-esp32
)

# Two narrow suppressions for vendor/*.cpp's real (unmodified) upstream code
# — can't fix without deviating from byte-identical upstream, see
# vendor/VENDORED.md. Not -w: this component's own new code (mc_*.cpp)
# still gets full warnings.
#  - reorder: Dispatcher.h's ctor initializer list order doesn't match
#    member declaration order — harmless.
#  - maybe-uninitialized: Dispatcher.cpp's checkRecv() only reads score/
#    air_time when pkt is non-NULL, which is exactly when they were set —
#    GCC's flow analysis can't prove that through the nested scoping.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=reorder -Wno-error=maybe-uninitialized)
