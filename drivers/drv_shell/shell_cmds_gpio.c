#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>

static void _gpio_output(int pin)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

static void _gpio_input(int pin)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

void cmd_gpio_get(int argc, char **argv)
{
    if (argc < 2) { printf("usage: gpio-get <pin>\n"); return; }
    int pin = atoi(argv[1]);
    _gpio_input(pin);
    printf("GPIO%d = %d\n", pin, gpio_get_level(pin));
}

void cmd_gpio_set(int argc, char **argv)
{
    if (argc < 3) { printf("usage: gpio-set <pin> <0|1>\n"); return; }
    int pin   = atoi(argv[1]);
    int level = atoi(argv[2]);
    _gpio_output(pin);
    gpio_set_level(pin, level ? 1 : 0);
    printf("GPIO%d <- %d\n", pin, level ? 1 : 0);
}

void cmd_gpio_pulse(int argc, char **argv)
{
    if (argc < 3) { printf("usage: gpio-pulse <pin> <ms>\n"); return; }
    int pin = atoi(argv[1]);
    int ms  = atoi(argv[2]);
    _gpio_output(pin);
    printf("GPIO%d: LOW for %d ms...\n", pin, ms);
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(pin, 1);
    printf("GPIO%d: HIGH\n", pin);
}
