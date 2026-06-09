#include "purr_app_catalog.h"
#include "app_about.h"
#include "app_settings.h"
#include "app_files.h"
#include "app_launcher.h"
#include "app_magidos.h"
#include "app_magicmac.h"

const purr_catalog_entry_t purr_catalog[PURR_CATALOG_N] = {
    { "About",    app_about_launch     },
    { "Settings", app_settings_launch  },
    { "Files",    app_files_launch     },
    { "Apps",     app_launcher_launch  },
    { "MagiDOS",  app_magidos_launch   },
    { "MagicMac", app_magicmac_launch  },
};

const int purr_catalog_count = PURR_CATALOG_N;
