#pragma once

// PURR OS — shared MiniWin colour theme definitions
// Include this from every device's miniwin_config.h to get theme support.
// Theme selected at build time: -DPURR_THEME_BLACKBERRY or -DPURR_THEME_LUNA
// Default (no flag): WCE / Windows CE classic.

#if defined(PURR_THEME_BLACKBERRY)

#define MW_TITLE_BAR_COLOUR_FOCUS       0x008818u
#define MW_TITLE_BAR_COLOUR_NO_FOCUS    0x002200u
#define MW_TITLE_BAR_COLOUR_MODAL       0x220000u
#define MW_TITLE_TEXT_COLOUR_FOCUS      0x00FF44u
#define MW_TITLE_TEXT_COLOUR_NO_FOCUS   0x004410u
#define MW_TITLE_TEXT_COLOUR_MODAL      0xFF4444u
#define MW_CONTROL_UP_COLOUR            0x002200u
#define MW_CONTROL_SEPARATOR_COLOUR     0x003000u
#define MW_CONTROL_DOWN_COLOUR          0x001800u
#define MW_CONTROL_DISABLED_COLOUR      0x000800u

#elif defined(PURR_THEME_LUNA)

// Windows XP Luna — flat approximations (no gradient support in MiniWin yet)
#define MW_TITLE_BAR_COLOUR_FOCUS       0x0049CDu   // Luna blue active
#define MW_TITLE_BAR_COLOUR_NO_FOCUS    0x7B96C2u   // muted inactive blue
#define MW_TITLE_BAR_COLOUR_MODAL       0xCC0000u
#define MW_TITLE_TEXT_COLOUR_FOCUS      0xFFFFFFu
#define MW_TITLE_TEXT_COLOUR_NO_FOCUS   0xE8E8E8u
#define MW_TITLE_TEXT_COLOUR_MODAL      0xFFFFFFu
#define MW_CONTROL_UP_COLOUR            0xECE9D8u   // XP button face
#define MW_CONTROL_SEPARATOR_COLOUR     0xACA899u
#define MW_CONTROL_DOWN_COLOUR          0xD4D0C8u   // XP control bg
#define MW_CONTROL_DISABLED_COLOUR      0xBDB9ABu

#else  // WCE default

#define MW_TITLE_BAR_COLOUR_FOCUS       MW_HAL_LCD_BLUE
#define MW_TITLE_BAR_COLOUR_NO_FOCUS    MW_HAL_LCD_GREY5
#define MW_TITLE_BAR_COLOUR_MODAL       MW_HAL_LCD_RED
#define MW_TITLE_TEXT_COLOUR_FOCUS      MW_HAL_LCD_WHITE
#define MW_TITLE_TEXT_COLOUR_NO_FOCUS   MW_HAL_LCD_WHITE
#define MW_TITLE_TEXT_COLOUR_MODAL      MW_HAL_LCD_WHITE
#define MW_CONTROL_UP_COLOUR            MW_HAL_LCD_GREY2
#define MW_CONTROL_SEPARATOR_COLOUR     MW_HAL_LCD_GREY3
#define MW_CONTROL_DOWN_COLOUR          MW_HAL_LCD_GREY4
#define MW_CONTROL_DISABLED_COLOUR      MW_HAL_LCD_GREY5

#endif
