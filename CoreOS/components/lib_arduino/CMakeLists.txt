# lib_arduino — Arduino compatibility shim for ESP-IDF
# Provides Arduino.h, Print.h, SPI.h, String.h and the Arduino.cpp runtime
# (millis, delay, digitalWrite, pinMode, analogRead, etc. via IDF primitives).
#
# Any driver component that #includes <Arduino.h> should add lib_arduino
# to its REQUIRES list. This avoids the circular-dep that would arise from
# requiring `main` (which also REQUIRES those driver components).
#
# Sources live in CoreOS/system/ — this component registers them without
# moving files, so git history is preserved.

set(ARDUINO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../system")

idf_component_register(
    SRCS         "${ARDUINO_SRC}/Arduino.cpp"
    INCLUDE_DIRS "${ARDUINO_SRC}"
    REQUIRES     driver esp_timer freertos esp_adc
)
