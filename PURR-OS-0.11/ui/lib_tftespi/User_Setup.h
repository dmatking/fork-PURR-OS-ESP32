// User_Setup.h — TFT_eSPI config for PURR OS CYD targets
// Covers ESP32-2432S028R (S028R) and ESP32-2432S024C (S024C) variants.
// Selected at compile time via -DCYD_VARIANT_S024C.

#define ILI9341_DRIVER

// ── Display dimensions ────────────────────────────────────────────────────────
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// ── Pin assignments ───────────────────────────────────────────────────────────
// Both S028R and S024C use HSPI (GPIO 13/12/14/15) for the display.
// S024C RST is tied to board reset — use -1 (no GPIO reset).
// S024C BL=27; S028R BL=21.
#ifdef CYD_VARIANT_S024C
// ESP32-2432S024C (CST820 I2C touch, RST tied to board reset)
#  define TFT_BL   27
#  define TFT_MOSI 13
#  define TFT_MISO 12
#  define TFT_SCLK 14
#  define TFT_CS   15
#  define TFT_DC    2
#  define TFT_RST  -1
#else
// ESP32-2432S028R (XPT2046 SPI touch, RST=4)
#  define TFT_BL   21
#  define TFT_MOSI 13
#  define TFT_MISO 12
#  define TFT_SCLK 14
#  define TFT_CS   15
#  define TFT_DC    2
#  define TFT_RST   4
#endif

// SPI port selection is controlled by CONFIG_TFT_HSPI_PORT / CONFIG_TFT_VSPI_PORT
// in the target's sdkconfig.defaults (TFT_config.h sets USE_HSPI_PORT from Kconfig).
// Do NOT define USE_HSPI_PORT here — TFT_config.h sets USER_SETUP_LOADED before
// this file is reached, so this file is only included as a fallback (no IDF build).

// ── SPI speed ─────────────────────────────────────────────────────────────────
#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY   6000000

// ── Backlight (active HIGH) ───────────────────────────────────────────────────
#define TFT_BACKLIGHT_ON HIGH

// ── Fonts ─────────────────────────────────────────────────────────────────────
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4

// ── ESP-IDF compatibility ─────────────────────────────────────────────────────
#define SUPPORT_TRANSACTIONS
