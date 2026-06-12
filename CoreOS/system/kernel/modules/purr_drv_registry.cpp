// purr_drv_registry.cpp — compile-time driver registration
// Called once from kitt.cpp before sys_drv_init_all().
// Each driver's enabled flag is set from the device_config (cfg.*) passed in.
// Adding or removing a driver at compile time: add/remove the #ifdef block below.

#include "purr_drv_registry.h"
#include "purr_sys_drv.h"
#include "device_config.h"
#include <string.h>

// Device info
#include "device_info_drv.h"

// Subsystem managers
#include "wifi_manager.h"
#include "power_manager.h"
#ifdef PURR_HAS_BT
#  include "bt_manager.h"
#endif
#ifdef PURR_HAS_LORA
#  include "lora_manager.h"
#endif
#ifdef PURR_HAS_CC1101
#  include "cc1101_manager.h"
#endif
#ifdef PURR_HAS_GPS
#  include "gps_manager.h"
#endif
#ifdef PURR_HAS_MTP
#  include "mtp_manager.h"
#endif
#ifdef PURR_HAS_PI_SLOT
#  include "pi_manager.h"
#endif

// Display drivers
#ifdef PURR_DISPLAY_ILI9341
#  include "display_ili9341.h"
#endif
#ifdef PURR_DISPLAY_ST7789
#  include "display_st7789.h"
#endif
#ifdef PURR_DISPLAY_ST7796
#  include "display_st7796.h"
#endif
#ifdef PURR_DISPLAY_SSD1306
#  include "display_ssd1306.h"
#endif

// Touch drivers
#ifdef PURR_HAS_TOUCH_CST816S
#  include "touch_cst816s.h"
#endif
#ifdef PURR_HAS_TOUCH_GT911
#  include "touch_gt911.h"
#endif
#ifdef PURR_HAS_TOUCH_XPT2046
#  include "touch_xpt2046.h"
#endif
#ifdef PURR_HAS_TOUCH_MXT
#  include "touch_mxt336t.h"
#endif

void purr_drv_register_all(const device_config_t *cfg) {
    // ── Device info ──────────────────────────────────────────────────────────
    device_info_drv_register();

    // ── Subsystems ───────────────────────────────────────────────────────────
    wifi_manager_drv_register(cfg->wifi);
    power_manager_drv_register(true, (uint16_t)cfg->cpu_max_mhz);

#ifdef PURR_HAS_BT
    bt_manager_drv_register(cfg->bt);
#endif

#ifdef PURR_HAS_LORA
    {
        uint32_t freq = (strcmp(cfg->lora_region, "EU") == 0) ? 868000000UL : 915000000UL;
        lora_manager_drv_register(cfg->lora, freq, 14);
    }
#endif

#ifdef PURR_HAS_CC1101
    cc1101_manager_drv_register(cfg->cc1101, 433.92f, 4.8f, 5.0f, 58.0f, 10);
#endif

#ifdef PURR_HAS_GPS
    gps_manager_drv_register(true);
#endif

#ifdef PURR_HAS_MTP
    mtp_manager_drv_register(true);
#endif

#ifdef PURR_HAS_PI_SLOT
    pi_manager_drv_register(cfg->pi_slot);
#endif

    // ── Display ──────────────────────────────────────────────────────────────
#ifdef PURR_DISPLAY_ILI9341
    display_ili9341_drv_register(true);
#endif
#ifdef PURR_DISPLAY_ST7789
    display_st7789_drv_register(true);
#endif
#ifdef PURR_DISPLAY_ST7796
    display_st7796_drv_register(true);
#endif
#ifdef PURR_DISPLAY_SSD1306
    display_ssd1306_drv_register(true);
#endif

    // ── Touch ────────────────────────────────────────────────────────────────
#ifdef PURR_HAS_TOUCH_CST816S
    touch_cst816s_drv_register(true);
#endif
#if defined(PURR_HAS_TOUCH_GT911) && !defined(PURR_DEVICE_TDECK_PLUS)
    // tdeck_plus owns its GT911 init in hal_touch.cpp (shared I2C with keyboard)
    touch_gt911_drv_register(true);
#endif
#ifdef PURR_HAS_TOUCH_XPT2046
    touch_xpt2046_drv_register(true);
#endif
#ifdef PURR_HAS_TOUCH_MXT
    touch_mxt336t_drv_register(true);
#endif
}
