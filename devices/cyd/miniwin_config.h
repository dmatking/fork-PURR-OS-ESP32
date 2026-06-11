#pragma once

// PURR OS — MiniWin configuration dispatcher for CYD (ESP32-2432S024C)
// Theme selected at build time via -DPURR_THEME_* compile flag.

#if defined(PURR_THEME_BLACKBERRY)
#  include "miniwin_config_blackberry.h"
#elif defined(PURR_THEME_LUNA)
#  include "miniwin_config_luna.h"
#else
#  include "miniwin_config_wce.h"
#endif
