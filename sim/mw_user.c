// mw_user.c — MiniWin root window for the PURR OS simulator.
// Implements mw_user_init / mw_user_root_paint_function / mw_user_root_message_function.
// Draws the BlackberryUI homescreen layout using mw_gl_* only — no ESP32/Arduino deps.
// Touch: left-click = tap, left-click + drag = swipe.

#include "miniwin.h"
#include "gl/gl.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// ── Colour helpers ────────────────────────────────────────────────────────────
// Palette stored as RGB565, converted to mw_hal_lcd_colour_t (0x00RRGGBB) at draw time.
static mw_hal_lcd_colour_t c565(uint16_t c) {
    uint8_t r5 = (c >> 11) & 0x1F;
    uint8_t g6 = (c >> 5)  & 0x3F;
    uint8_t b5 =  c        & 0x1F;
    return (uint32_t)(((r5 << 3) | (r5 >> 2)) << 16)
         | (uint32_t)(((g6 << 2) | (g6 >> 4)) << 8)
         | (uint32_t)((b5 << 3) | (b5 >> 2));
}

static const uint16_t C_STATUS_BG  = 0x0000;
static const uint16_t C_STATUS_TXT = 0xFFFF;
static const uint16_t C_TIME_BG    = 0x18C5;
static const uint16_t C_TIME_TXT   = 0xFFFF;
static const uint16_t C_TIME_SUB   = 0x8410;
static const uint16_t C_NOTIF_BG   = 0x1082;
static const uint16_t C_WALL_BG    = 0x18C5;
static const uint16_t C_WALL_PLATE = 0x10A2;
static const uint16_t C_WATERMARK  = 0x294A;
static const uint16_t C_HINT_TXT   = 0x2104;
static const uint16_t C_TAB_BG     = 0x0841;
static const uint16_t C_TAB_ACTIVE = 0xFFFF;
static const uint16_t C_TAB_INACT  = 0x528A;
static const uint16_t C_DOCK_BG    = 0x0842;
static const uint16_t C_ICON_BG    = 0x18E5;
static const uint16_t C_ICON_BORDER= 0x2947;
static const uint16_t C_ICON_TXT   = 0xFFFF;
static const uint16_t C_DRAWER_BG  = 0x0841;
static const uint16_t C_DIV        = 0x2104;

static const uint16_t APP_COLORS[] = {
    0x4E1E, 0xFD20, 0xFFE0, 0x07E0, 0xF228, 0x4C9F,
    0xF7BE, 0x07FF, 0x3CDF, 0x801F, 0xFC00, 0x0481,
};
static const int N_APP_COLORS = 12;

// ── Layout ───────────────────────────────────────────────────────────────────
#define SCR_W       320
#define SCR_H       240
#define STATUS_H    20
#define TIME_Y      20
#define TIME_H      30
#define NOTIF_Y     50
#define NOTIF_H     20
#define CONTENT_Y   70
#define CONTENT_H   134
#define TAB_Y       204
#define TAB_H       16
#define DOCK_Y      220
#define DOCK_H      20
#define GRID_COLS   4
#define CELL_W      (SCR_W / GRID_COLS)
#define CELL_H      50
#define ICON_TW     60
#define ICON_TH     36
#define ICON_TX     ((CELL_W - ICON_TW) / 2)
#define ICON_TY     7
#define DR_NOTIF_Y  STATUS_H
#define DR_TAB_Y    (DR_NOTIF_Y + NOTIF_H)
#define DR_GRID_Y   (DR_TAB_Y + TAB_H)
#define DR_GRID_H   (SCR_H - DR_GRID_Y)
#define TAB_COUNT   3
#define DOCK_COLS   4
#define DOCK_SW     (SCR_W / DOCK_COLS)

static const char *TAB_NAMES[TAB_COUNT] = { "Frequent", "All", "Favorites" };
static const char *DOCK_LABELS[DOCK_COLS] = { "WIFI", "MSGS", "FLASH", "LOGS" };
static const uint16_t DOCK_COLORS[DOCK_COLS] = { 0x4E1E, 0x07E0, 0xFD20, 0xF228 };

// ── Stub app list ─────────────────────────────────────────────────────────────
#define MAX_APPS 12
static const char *SIM_APPS[MAX_APPS] = {
    "Terminal", "WiFi", "Logs", "Flash",
    "BLE",      "Mesh", "MTP", "Python",
    "Settings", "Games","Music","Files",
};
static int s_napp = MAX_APPS;

// ── State ────────────────────────────────────────────────────────────────────
typedef enum { STATE_HOME = 0, STATE_DRAWER } ScreenState;
static ScreenState s_state = STATE_HOME;
static int  s_tab = 1;
static int16_t s_touch_start_x = 0, s_touch_start_y = 0;
static int16_t s_last_x = 0, s_last_y = 0;

// ── Hit zones ────────────────────────────────────────────────────────────────
#define MAX_HITS    64
#define HIT_TAB(i)  (100 + (i))
#define HIT_DOCK(i) (300 + (i))
typedef struct { int16_t x, y, w, h; int id; } HitZone;
static HitZone s_hits[MAX_HITS];
static int s_nhit = 0;
static void hit_clear(void) { s_nhit = 0; }
static void hit_reg(int16_t x, int16_t y, int16_t w, int16_t h, int id) {
    if (s_nhit < MAX_HITS) { s_hits[s_nhit].x=x; s_hits[s_nhit].y=y;
                              s_hits[s_nhit].w=w; s_hits[s_nhit].h=h;
                              s_hits[s_nhit].id=id; s_nhit++; }
}
static int hit_test(int16_t tx, int16_t ty) {
    for (int i = s_nhit-1; i >= 0; i--)
        if (tx >= s_hits[i].x && tx < s_hits[i].x+s_hits[i].w &&
            ty >= s_hits[i].y && ty < s_hits[i].y+s_hits[i].h)
            return s_hits[i].id;
    return -1;
}

// ── Draw helpers ─────────────────────────────────────────────────────────────
static void fill(const mw_gl_draw_info_t *d,
                 int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    mw_gl_set_solid_fill_colour(c565(c));
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_rectangle(d, x, y, w, h);
}

static void hline_sim(const mw_gl_draw_info_t *d,
                      int16_t x, int16_t y, int16_t w, uint16_t c) {
    mw_gl_set_fg_colour(c565(c));
    mw_gl_set_line(MW_GL_SOLID_LINE);
    mw_gl_hline(d, x, x + w - 1, y);
}

static mw_gl_font_t font_for_sz(int sz) {
    if (sz >= 3) return MW_GL_FONT_20;
    if (sz >= 2) return MW_GL_FONT_16;
    return MW_GL_FONT_9;
}

static void str_sim(const mw_gl_draw_info_t *d,
                    int16_t x, int16_t y, const char *s,
                    uint16_t fg, uint16_t bg, int sz) {
    mw_gl_set_font(font_for_sz(sz));
    mw_gl_set_fg_colour(c565(fg));
    mw_gl_set_bg_colour(c565(bg));
    mw_gl_set_bg_transparency(MW_GL_BG_NOT_TRANSPARENT);
    mw_gl_string(d, x, y, s);
}

static void str_cx_sim(const mw_gl_draw_info_t *d,
                       int16_t bx, int16_t by, int16_t bw, int16_t bh,
                       const char *s, uint16_t fg, uint16_t bg, int sz) {
    mw_gl_set_font(font_for_sz(sz));
    int16_t tw = mw_gl_get_string_width_pixels(s);
    int16_t fh = mw_gl_get_font_height();
    int16_t tx = bx + (bw - tw) / 2;
    int16_t ty = by + (bh - fh) / 2;
    mw_gl_set_fg_colour(c565(fg));
    mw_gl_set_bg_colour(c565(bg));
    mw_gl_set_bg_transparency(MW_GL_BG_NOT_TRANSPARENT);
    mw_gl_string(d, tx, ty, s);
}

// ── Draw sections ─────────────────────────────────────────────────────────────
static void draw_status(const mw_gl_draw_info_t *d, bool show_time) {
    fill(d, 0, 0, SCR_W, STATUS_H, C_STATUS_BG);
    str_sim(d, 4, (STATUS_H-8)/2, "PURR OS", C_STATUS_TXT, C_STATUS_BG, 1);
    str_sim(d, SCR_W-40, (STATUS_H-8)/2, "100%", C_STATUS_TXT, C_STATUS_BG, 1);
    if (show_time) {
        char tbuf[10];
        snprintf(tbuf, sizeof(tbuf), "12:00");
        mw_gl_set_font(MW_GL_FONT_16);
        int16_t tw = mw_gl_get_string_width_pixels(tbuf);
        str_sim(d, SCR_W/2 - tw/2, (STATUS_H-16)/2, tbuf, C_STATUS_TXT, C_STATUS_BG, 2);
    }
}

static void draw_time_zone(const mw_gl_draw_info_t *d) {
    fill(d, 0, TIME_Y, SCR_W, TIME_H, C_TIME_BG);
    mw_gl_set_font(MW_GL_FONT_16);
    int16_t tw = mw_gl_get_string_width_pixels("12:00");
    str_sim(d, SCR_W/2 - tw/2, TIME_Y+2, "12:00", C_TIME_TXT, C_TIME_BG, 2);
    mw_gl_set_font(MW_GL_FONT_9);
    int16_t dw = mw_gl_get_string_width_pixels("ESP32-2432S024C");
    str_sim(d, SCR_W/2 - dw/2, TIME_Y+20, "ESP32-2432S024C", C_TIME_SUB, C_TIME_BG, 1);
}

static void draw_notif(const mw_gl_draw_info_t *d, int16_t at_y) {
    fill(d, 0, at_y, SCR_W, NOTIF_H, C_NOTIF_BG);
    hline_sim(d, 0, at_y, SCR_W, C_DIV);
    hline_sim(d, 0, at_y+NOTIF_H-1, SCR_W, C_DIV);
    str_sim(d, 4, at_y+(NOTIF_H-8)/2, "WIFI  MSGS  LOGS", C_STATUS_TXT, C_NOTIF_BG, 1);
}

static void draw_tabs(const mw_gl_draw_info_t *d, int16_t at_y, bool reg) {
    fill(d, 0, at_y, SCR_W, TAB_H, C_TAB_BG);
    hline_sim(d, 0, at_y, SCR_W, C_DIV);
    int tab_w = SCR_W / TAB_COUNT;
    for (int i = 0; i < TAB_COUNT; i++) {
        int16_t tx = (int16_t)(i * tab_w);
        uint16_t fg = (i == s_tab) ? C_TAB_ACTIVE : C_TAB_INACT;
        str_cx_sim(d, tx, at_y, tab_w, TAB_H, TAB_NAMES[i], fg, C_TAB_BG, 1);
        if (i == s_tab) hline_sim(d, tx+2, at_y+TAB_H-2, tab_w-4, C_TAB_ACTIVE);
        if (i < TAB_COUNT-1) fill(d, tx+tab_w-1, at_y, 1, TAB_H, C_DIV);
        if (reg) hit_reg(tx, at_y, tab_w, TAB_H, HIT_TAB(i));
    }
}

static void draw_wallpaper(const mw_gl_draw_info_t *d) {
    fill(d, 0, CONTENT_Y, SCR_W, CONTENT_H, C_WALL_BG);
    int16_t px=80, py=CONTENT_Y+32, pw=160, ph=60;
    fill(d, px, py, pw, ph, C_WALL_PLATE);
    str_cx_sim(d, px, py+8, pw, 24, "PURR OS", C_WATERMARK, C_WALL_PLATE, 3);
    mw_gl_set_font(MW_GL_FONT_9);
    int16_t vw = mw_gl_get_string_width_pixels("v0.6.0");
    str_sim(d, px+(pw-vw)/2, py+42, "v0.6.0", C_TIME_SUB, C_WALL_PLATE, 1);
    mw_gl_set_font(MW_GL_FONT_9);
    int16_t hw = mw_gl_get_string_width_pixels("swipe up for apps");
    str_sim(d, SCR_W/2-hw/2, CONTENT_Y+CONTENT_H-14,
            "swipe up for apps", C_HINT_TXT, C_WALL_BG, 1);
}

static void draw_dock(const mw_gl_draw_info_t *d, bool reg) {
    fill(d, 0, DOCK_Y, SCR_W, DOCK_H, C_DOCK_BG);
    hline_sim(d, 0, DOCK_Y, SCR_W, C_DIV);
    for (int i = 0; i < DOCK_COLS; i++) {
        int16_t dx = (int16_t)(i * DOCK_SW);
        fill(d, dx+2, DOCK_Y+2, DOCK_SW-4, DOCK_H-4, DOCK_COLORS[i]);
        str_cx_sim(d, dx+2, DOCK_Y+2, DOCK_SW-4, DOCK_H-4,
                   DOCK_LABELS[i], C_ICON_TXT, DOCK_COLORS[i], 1);
        if (i < DOCK_COLS-1) fill(d, dx+DOCK_SW-1, DOCK_Y, 1, DOCK_H, C_DIV);
        if (reg) hit_reg(dx, DOCK_Y, DOCK_SW, DOCK_H, HIT_DOCK(i));
    }
}

static void draw_grid(const mw_gl_draw_info_t *d,
                      int16_t at_y, int16_t height) {
    fill(d, 0, at_y, SCR_W, height, C_DRAWER_BG);
    int max_rows = height / CELL_H;
    for (int row = 0; row < max_rows; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            int    idx = row * GRID_COLS + col;
            int16_t cx = (int16_t)(col * CELL_W);
            int16_t cy = at_y + (int16_t)(row * CELL_H);
            if (idx < s_napp) {
                uint16_t tc = APP_COLORS[idx % N_APP_COLORS];
                fill(d, cx+ICON_TX-1, cy+ICON_TY-1, ICON_TW+2, ICON_TH+2, C_ICON_BORDER);
                fill(d, cx+ICON_TX, cy+ICON_TY, ICON_TW, ICON_TH, tc);
                char abbr[3] = { SIM_APPS[idx][0], SIM_APPS[idx][1], '\0' };
                str_cx_sim(d, cx+ICON_TX, cy+ICON_TY, ICON_TW, ICON_TH,
                            abbr, 0xFFFF, tc, 1);
                mw_gl_set_font(MW_GL_FONT_9);
                int16_t lw = mw_gl_get_string_width_pixels(SIM_APPS[idx]);
                str_sim(d, cx+CELL_W/2-lw/2, cy+ICON_TY+ICON_TH+3,
                        SIM_APPS[idx], C_ICON_TXT, C_DRAWER_BG, 1);
            } else {
                fill(d, cx+ICON_TX, cy+ICON_TY, ICON_TW, ICON_TH, C_ICON_BG);
            }
            if (col < GRID_COLS-1) fill(d, cx+CELL_W-1, cy, 1, CELL_H, C_DIV);
        }
        hline_sim(d, 0, at_y+(int16_t)((row+1)*CELL_H), SCR_W, C_DIV);
    }
}

static void draw_home(const mw_gl_draw_info_t *d) {
    hit_clear();
    draw_status(d, false);
    draw_time_zone(d);
    draw_notif(d, NOTIF_Y);
    draw_wallpaper(d);
    draw_tabs(d, TAB_Y, true);
    draw_dock(d, true);
}

static void draw_drawer(const mw_gl_draw_info_t *d) {
    hit_clear();
    draw_status(d, true);
    draw_notif(d, DR_NOTIF_Y);
    draw_tabs(d, DR_TAB_Y, true);
    draw_grid(d, DR_GRID_Y, DR_GRID_H);
}

// ── MiniWin root window callbacks ─────────────────────────────────────────────

void mw_user_root_paint_function(const mw_gl_draw_info_t *draw_info) {
    if (s_state == STATE_HOME) draw_home(draw_info);
    else                       draw_drawer(draw_info);
}

void mw_user_root_message_function(const mw_message_t *message) {
    if (message->message_id == MW_TOUCH_DOWN_MESSAGE) {
        s_touch_start_x = (int16_t)(message->message_data >> 16);
        s_touch_start_y = (int16_t)(message->message_data & 0xFFFF);
        s_last_x = s_touch_start_x;
        s_last_y = s_touch_start_y;

    } else if (message->message_id == MW_TOUCH_DRAG_MESSAGE) {
        s_last_x = (int16_t)(message->message_data >> 16);
        s_last_y = (int16_t)(message->message_data & 0xFFFF);

    } else if (message->message_id == MW_TOUCH_UP_MESSAGE) {
        int16_t dx = s_last_x - s_touch_start_x;
        int16_t dy = s_last_y - s_touch_start_y;

        if (dy < 0 && abs(dy) >= 40 && abs(dy) > abs(dx) && s_state == STATE_HOME) {
            s_state = STATE_DRAWER;
            mw_paint_all();
        } else if (dy > 0 && abs(dy) >= 40 && abs(dy) > abs(dx) && s_state == STATE_DRAWER) {
            s_state = STATE_HOME;
            mw_paint_all();
        } else if (abs(dx) <= 10 && abs(dy) <= 10) {
            int id = hit_test(s_touch_start_x, s_touch_start_y);
            if (id >= HIT_TAB(0) && id <= HIT_TAB(TAB_COUNT-1)) {
                s_tab = id - HIT_TAB(0);
                mw_paint_all();
            }
        }
    }
}

void mw_user_init(void) {
    // Nothing to scan in the sim — SIM_APPS is static
}
