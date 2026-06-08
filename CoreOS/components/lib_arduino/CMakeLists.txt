# lib_arduino — passthrough to real Arduino-ESP32
# The hand-written shim in CoreOS/system/ is superseded by espressif/arduino-esp32.
# Components that REQUIRE lib_arduino now get the full Arduino API.

idf_component_register(
    REQUIRES espressif__arduino-esp32
)
