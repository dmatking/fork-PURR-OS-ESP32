#pragma once

// PURR OS — MiniWin configuration dispatcher for CYD (ESP32-2432S024C)
// Pulls in the theme-specific config based on PURR_THEME_BLACKBERRY.

#ifdef PURR_THEME_BLACKBERRY
#  include "miniwin_config_blackberry.h"
#else
#  include "miniwin_config_wce.h"
#endif
