cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(purr_os_core)

idf_component_register(
    SRCS
        "system/kernel/main.cpp"
        "system/kernel/kitt.cpp"
        "system/kernel/device_config.cpp"
        "system/kernel/modules/display_ili9488.cpp"
        "system/kernel/modules/display_ssd1306.cpp"
        "system/kernel/modules/touch_mxt336t.cpp"
        "system/kernel/modules/wifi_manager.cpp"
        "system/kernel/modules/bt_manager.cpp"
        "system/kernel/modules/lora_manager.cpp"
        "system/kernel/modules/pi_manager.cpp"
        "system/kernel/modules/mtp_manager.cpp"
        "system/kernel/modules/flasher.cpp"
        "system/kernel/modules/power_manager.cpp"
        "system/bridge/main.cpp"
        "system/system/main.cpp"
    INCLUDE_DIRS
        "system/kernel"
        "system/kernel/modules"
        "system/bridge"
        "system/system"
    REQUIRES
        arduino
        lvgl
        ArduinoJson
        esp_wifi
        bt
        nvs_flash
        spiffs
        fatfs
        esp_adc
        app_update
)

# Watchdog and emergency are separate Arduino sketches / partitions.
# They share the modules headers but are built independently.
