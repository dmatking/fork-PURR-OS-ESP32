#include "app_restart.h"
#include "app_restart_menu.h"

void app_restart_launch(void)
{
    // Show restart menu with boot mode selection instead of immediate restart
    app_restart_menu_launch();
}
