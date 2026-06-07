// kitt_sim.cpp — KITT global instance for the simulator.
// Populates a few demo apps so the app drawer renders with content.

#include "compat/kitt_sim.h"

KITT kitt;

// Called from main_sim.cpp before mw_init() to populate demo content.
void kitt_sim_setup() {
    kitt._add_app("Terminal",  "/apps/terminal.meow");
    kitt._add_app("Files",     "/apps/files.meow");
    kitt._add_app("Settings",  "/apps/settings.meow");
    kitt._add_app("WiFi",      "/apps/wifi.meow");
    kitt._add_app("Radio",     "/apps/radio.meow");
    kitt._add_app("Flasher",   "/apps/flasher.meow");
}
