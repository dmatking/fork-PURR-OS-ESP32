// theme_luna.c — KittenUI Luna theme
//
// Windows XP Luna aesthetic: blue title bars, parchment surfaces, rounded
// buttons, soft drop shadows. Lighter and friendlier than WCE Classic.
// The "XP blue" gradient is approximated as a flat #316AC5 since LVGL
// applies style uniformly — gradient support can be added via apply_fn.

#include "../kittenui_theme.h"
#include "lvgl.h"

static kittenui_theme_t s_theme_luna_rt;
static bool s_initialized = false;

const kittenui_theme_t *kittenui_theme_luna(void)
{
    if (!s_initialized) {
        kittenui_palette_t *p = &s_theme_luna_rt.palette;
        p->window_bg     = lv_color_make(0xEC, 0xE9, 0xD8); // XP parchment
        p->surface       = lv_color_make(0xEC, 0xE9, 0xD8);
        p->surface_alt   = lv_color_make(0xDE, 0xDB, 0xCC);
        p->titlebar      = lv_color_make(0x31, 0x6A, 0xC5); // XP blue
        p->titlebar_text = lv_color_make(0xFF, 0xFF, 0xFF);
        p->selected      = lv_color_make(0x31, 0x6A, 0xC5);
        p->selected_text = lv_color_make(0xFF, 0xFF, 0xFF);
        p->text          = lv_color_make(0x00, 0x00, 0x00);
        p->text_muted    = lv_color_make(0x7F, 0x7F, 0x7F);
        p->border        = lv_color_make(0xAC, 0xA8, 0x99);
        p->border_light  = lv_color_make(0xFF, 0xFF, 0xFF);
        p->border_dark   = lv_color_make(0xAC, 0xA8, 0x99);
        p->scrollbar     = lv_color_make(0xC8, 0xC5, 0xB8);
        p->header_bg     = lv_color_make(0x00, 0x33, 0x99); // deeper blue for header
        p->header_text   = lv_color_make(0xFF, 0xFF, 0xFF);
        p->accent        = lv_color_make(0x31, 0x6A, 0xC5);
        p->danger        = lv_color_make(0xCC, 0x00, 0x00);
        p->success       = lv_color_make(0x00, 0x80, 0x00);

        s_theme_luna_rt.name = "Luna";
        s_theme_luna_rt.id   = "luna";

        s_theme_luna_rt.fonts.small   = &lv_font_montserrat_10;
        s_theme_luna_rt.fonts.body    = &lv_font_montserrat_14;
        s_theme_luna_rt.fonts.heading = &lv_font_montserrat_16;
        s_theme_luna_rt.fonts.mono    = &lv_font_unscii_8;

        s_theme_luna_rt.flags.raised_buttons  = false;  // flat buttons in Luna
        s_theme_luna_rt.flags.rounded_corners = true;
        s_theme_luna_rt.flags.corner_radius   = 6;      // more rounded than WCE
        s_theme_luna_rt.flags.show_scrollbars = true;
        s_theme_luna_rt.flags.shadow_windows  = true;   // Luna had soft shadows
        s_theme_luna_rt.flags.padding         = 5;
        s_theme_luna_rt.flags.item_height     = 24;

        s_theme_luna_rt.apply_fn = NULL;

        s_initialized = true;
    }
    return &s_theme_luna_rt;
}
