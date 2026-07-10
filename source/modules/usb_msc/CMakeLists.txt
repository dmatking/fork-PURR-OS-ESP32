idf_component_register(
    SRCS
        usb_msc.c
    INCLUDE_DIRS
        .
    REQUIRES esp_common driver freertos esp_tinyusb fatfs sdmmc
)
