#include "display_ili9341.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {

void cmd_display_init(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Running display_ili9341_init()...\n");
    display_ili9341_init();
    printf("Done.\n");
}

void cmd_display_reset(int argc, char **argv)
{
    if (argc < 2) { printf("usage: display-reset <rst_pin>  (S024C=4)\n"); return; }
    int pin = atoi(argv[1]);

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    gpio_num_t gpio = (gpio_num_t)pin;
    printf("GPIO%d (RST): HIGH -> LOW (100ms) -> HIGH\n", pin);
    gpio_set_level(gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    printf("Reset done — run display-init next\n");
}

void cmd_display_color(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: display-color <hex>  (0xF800=red  0x07E0=green  0x001F=blue  0x0000=black  0xFFFF=white)\n");
        return;
    }
    uint16_t color = (uint16_t)strtoul(argv[1], NULL, 0);
    printf("Filling display 0x%04X...\n", color);
    display_ili9341_fill_rect(0, 0, 320, 240, color);
    printf("Done.\n");
}

void cmd_display_text(int argc, char **argv)
{
    if (argc < 3) { printf("usage: display-text <row> <text>\n"); return; }
    int row = atoi(argv[1]);

    // Rejoin remaining tokens into one string
    char buf[128] = {0};
    for (int i = 2; i < argc; i++) {
        if (i > 2) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
    }
    display_ili9341_text((uint8_t)row, buf);
    printf("Row %d: %s\n", row, buf);
}

void cmd_display_bl(int argc, char **argv)
{
    if (argc < 2) { printf("usage: display-bl <0-255>\n"); return; }
    uint8_t level = (uint8_t)atoi(argv[1]);
    display_ili9341_set_brightness(level);
    printf("Backlight -> %d\n", level);
}

} // extern "C"
