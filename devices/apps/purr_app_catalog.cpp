#include "purr_app_catalog.h"
#include "app_about.h"
#include "app_settings.h"
#include "app_files.h"
#include "app_launcher.h"
#include "app_terminal.h"
#ifdef PURR_HAS_MAGIDOS
#  include "app_magidos.h"
#endif
#ifdef PURR_HAS_MAGICMAC
#  include "app_magicmac.h"
#endif

const purr_catalog_entry_t purr_catalog[] = {
    { "About",    app_about_launch     },
    { "Settings", app_settings_launch  },
    { "Files",    app_files_launch     },
    { "Apps",     app_launcher_launch  },
    { "Terminal", app_terminal_launch  },
#ifdef PURR_HAS_MAGIDOS
    { "MagiDOS",  app_magidos_launch   },
#endif
#ifdef PURR_HAS_MAGICMAC
    { "MagicMac", app_magicmac_launch  },
#endif
};

const int purr_catalog_count = (int)(sizeof(purr_catalog) / sizeof(purr_catalog[0]));
