#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>

void cmd_sys_info(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const esp_app_desc_t *app = esp_app_get_description();
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    printf("\n  App      : %s  v%s\n", app->project_name, app->version);
    printf("  IDF      : %s\n",        app->idf_ver);
    printf("  Chip     : ESP32 rev %d  cores=%d\n", chip.revision, chip.cores);
    printf("  Flash    : %u KB\n",     (unsigned)(flash_size / 1024));
    printf("  Heap int : %u KB free\n", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    printf("  Heap SPI : %u KB free\n", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)   / 1024));
    printf("  Uptime   : %u ms\n",     (unsigned)(esp_timer_get_time() / 1000));
    printf("\n");
}

void cmd_sys_tasks(int argc, char **argv)
{
    (void)argc; (void)argv;
#if configUSE_TRACE_FACILITY && configUSE_STATS_FORMATTING_FUNCTIONS
    char *buf = malloc(2048);
    if (!buf) { printf("out of memory\n"); return; }
    vTaskList(buf);
    printf("\nName             State  Pri  Stack  Num\n");
    printf("-----------------------------------------\n");
    printf("%s\n", buf);
    free(buf);
#else
    printf("Task count: %u\n", (unsigned)uxTaskGetNumberOfTasks());
    printf("(enable CONFIG_FREERTOS_USE_TRACE_FACILITY + CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS for full list)\n");
#endif
}

void cmd_sys_heap(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Internal : %u KB free  (low: %u KB)\n",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)        / 1024),
           (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
    printf("PSRAM    : %u KB free  (low: %u KB)\n",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)        / 1024),
           (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

void cmd_sys_nvs_erase(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Erasing NVS...\n");
    esp_err_t err = nvs_flash_erase();
    printf("%s\n", err == ESP_OK ? "Done. Reboot to reinitialise." : esp_err_to_name(err));
}

void cmd_sys_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Rebooting...\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
