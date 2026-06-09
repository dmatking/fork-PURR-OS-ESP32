#include "app_restart.h"
#include "esp_system.h"

void app_restart_launch(void)
{
    esp_restart();
}
