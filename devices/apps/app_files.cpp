#include "app_files.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "kitt.h"
#include "purr_apps_common.h"
#include "purr_taskbar.h"
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

extern KITT kitt;

static mw_handle_t s_handle = MW_INVALID_HANDLE;

#define WIN_W    240
#define WIN_H    190
#define TAB_H    14
#define PATH_H   13
#define ENTRY_H  13
#define SCROLL_W 16

#define MAX_ENTRIES 48
#define MAX_NAME    38

#define SD_ROOT     "/sdcard"
#define SPIFFS_ROOT "/spiffs"

typedef struct {
    char     name[MAX_NAME];
    bool     is_dir;
    uint32_t size;
} fentry_t;

static int      s_tab    = 0;
static char     s_path[2][192] = { SD_ROOT, SPIFFS_ROOT };
static fentry_t s_entries[MAX_ENTRIES];
static int      s_count  = 0;
static int      s_scroll = 0;

static const char *cur_root(void) { return s_tab == 0 ? SD_ROOT : SPIFFS_ROOT; }
static bool        at_root(void)  { return strcmp(s_path[s_tab], cur_root()) == 0; }

static void scan_dir(void)
{
    s_count  = 0;
    s_scroll = 0;
    if (s_tab == 0 && !kitt.sd_available()) return;

    DIR *d = opendir(s_path[s_tab]);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && s_count < MAX_ENTRIES) {
        if (ent->d_name[0] == '.') continue;
        fentry_t *e = &s_entries[s_count];
        strncpy(e->name, ent->d_name, MAX_NAME - 1);
        e->name[MAX_NAME - 1] = '\0';
        e->size = 0;

        char fp[512];
        snprintf(fp, sizeof(fp), "%s/%s", s_path[s_tab], ent->d_name);

        e->is_dir = (ent->d_type == DT_DIR);
        if (ent->d_type == DT_UNKNOWN) {
            struct stat st;
            if (stat(fp, &st) == 0) e->is_dir = (bool)S_ISDIR(st.st_mode);
        }
        if (!e->is_dir) {
            struct stat st;
            if (stat(fp, &st) == 0) e->size = (uint32_t)st.st_size;
        }
        s_count++;
    }
    closedir(d);
}

static void draw_tabs(const mw_gl_draw_info_t *d, int16_t cw)
{
    const char *const names[2] = { "SD", "SPIFFS" };
    int16_t tw = (int16_t)(cw / 2);
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_font(MW_GL_FONT_12);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    for (int i = 0; i < 2; i++) {
        int16_t tx = (int16_t)(i * tw);
        mw_gl_set_solid_fill_colour(i == s_tab ? WCE_HI : WCE_BAR);
        mw_gl_rectangle(d, tx, 0, tw, TAB_H);
        mw_gl_set_fg_colour(WCE_TXT);
        mw_gl_string(d, (int16_t)(tx + 4), 3, names[i]);
        if (i == s_tab) {
            mw_gl_set_fg_colour(WCE_DARK);
            mw_gl_hline(d, tx, (int16_t)(tx + tw - 1), 0);
            mw_gl_vline(d, tx, 0, (int16_t)(TAB_H - 1));
        } else {
            mw_gl_set_fg_colour(WCE_SHD);
            mw_gl_vline(d, (int16_t)(tx + tw - 1), 0, (int16_t)(TAB_H - 1));
        }
    }
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 0, (int16_t)(cw - 1), TAB_H);
}

static void draw_path_bar(const mw_gl_draw_info_t *d, int16_t cw)
{
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_DARK);
    mw_gl_rectangle(d, 0, TAB_H, cw, PATH_H);
    const char *p = s_path[s_tab];
    int plen = (int)strlen(p);
    int maxc = (cw - 6) / 6;
    if (plen > maxc) p += plen - maxc;
    mw_gl_set_fg_colour(WCE_HI);
    mw_gl_set_font(MW_GL_FONT_12);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_string(d, 3, (int16_t)(TAB_H + 2), p);
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 0, (int16_t)(cw - 1), (int16_t)(TAB_H + PATH_H));
}

static void draw_entry_list(const mw_gl_draw_info_t *d,
                            int16_t list_y, int16_t list_h, int16_t list_w)
{
    bool has_back = !at_root();
    int  total    = (int)has_back + s_count;
    int  visible  = list_h / ENTRY_H;

    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_font(MW_GL_FONT_12);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);

    for (int i = 0; i < visible; i++) {
        int idx = s_scroll + i;
        if (idx >= total) break;
        int16_t ey = (int16_t)(list_y + i * ENTRY_H);

        if (i % 2 == 1) {
            mw_gl_set_solid_fill_colour(0xD8D8D8u);
            mw_gl_rectangle(d, 0, ey, list_w, ENTRY_H);
        }

        if (has_back && idx == 0) {
            mw_gl_set_fg_colour(0x000080u);
            mw_gl_string(d, 3, (int16_t)(ey + 2), "[..]");
        } else {
            int fi = idx - (int)has_back;
            if (fi < 0 || fi >= s_count) continue;
            fentry_t *e = &s_entries[fi];
            char line[44];
            if (e->is_dir) {
                snprintf(line, sizeof(line), "[%.36s]", e->name);
                mw_gl_set_fg_colour(0x000080u);
            } else {
                char sz[10];
                if (e->size >= 1048576u)
                    snprintf(sz, sizeof(sz), "%uMB", (unsigned)(e->size >> 20));
                else if (e->size >= 1024u)
                    snprintf(sz, sizeof(sz), "%ukB", (unsigned)(e->size >> 10));
                else
                    snprintf(sz, sizeof(sz), "%uB", (unsigned)e->size);
                snprintf(line, sizeof(line), "%-26.26s %7s", e->name, sz);
                mw_gl_set_fg_colour(WCE_TXT);
            }
            mw_gl_string(d, 3, (int16_t)(ey + 2), line);
        }

        mw_gl_set_fg_colour(WCE_SHD);
        mw_gl_hline(d, 0, (int16_t)(list_w - 1), (int16_t)(ey + ENTRY_H - 1));
    }
}

static void paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);
    int16_t cw = cr.width;
    int16_t ch = cr.height;

    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, 0, cw, ch);

    draw_tabs(d, cw);
    draw_path_bar(d, cw);

    int16_t list_y = (int16_t)(TAB_H + PATH_H);
    int16_t list_h = (int16_t)(ch - list_y);
    int16_t list_w = (int16_t)(cw - SCROLL_W);

    mw_gl_set_font(MW_GL_FONT_12);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);

    if (s_tab == 0 && !kitt.sd_available()) {
        mw_gl_set_fg_colour(WCE_SHD);
        mw_gl_string(d, 4, (int16_t)(list_y + 6), "SD card not mounted");
    } else if (s_count == 0 && at_root()) {
        mw_gl_set_fg_colour(WCE_SHD);
        mw_gl_string(d, 4, (int16_t)(list_y + 6), "(empty)");
    } else {
        draw_entry_list(d, list_y, list_h, list_w);
    }

    // Scroll strip
    int16_t sx = list_w;
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, sx, list_y, SCROLL_W, list_h);
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_vline(d, sx, list_y, (int16_t)(ch - 1));
    mw_gl_set_fg_colour(WCE_DARK);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_12);
    mw_gl_string(d, (int16_t)(sx + 4), (int16_t)(list_y + 3),               "^");
    mw_gl_string(d, (int16_t)(sx + 4), (int16_t)(list_y + list_h / 2 + 3),  "v");
}

static void message(const mw_message_t *msg)
{
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        scan_dir();
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        mw_paint_window_client(msg->recipient_handle);
        break;

    case MW_TOUCH_DOWN_MESSAGE: {
        mw_util_rect_t cr = mw_get_window_client_rect(msg->recipient_handle);
        int16_t cx = (int16_t)(msg->message_data >> 16);
        int16_t cy = (int16_t)(msg->message_data & 0xFFFF);

        if (cy >= 0 && cy < TAB_H) {
            int nt = cx / (cr.width / 2);
            if (nt >= 0 && nt < 2 && nt != s_tab) {
                s_tab = nt;
                scan_dir();
            }
            mw_paint_window_client(msg->recipient_handle);
            break;
        }

        int16_t list_y = (int16_t)(TAB_H + PATH_H);
        if (cy < list_y) break;

        int16_t list_h = (int16_t)(cr.height - list_y);
        int16_t list_w = (int16_t)(cr.width  - SCROLL_W);

        if (cx >= list_w) {
            // Scroll strip: upper half = up, lower half = down
            bool has_back = !at_root();
            int  total    = (int)has_back + s_count;
            int  visible  = list_h / ENTRY_H;
            if (cy - list_y < list_h / 2) {
                if (s_scroll > 0) s_scroll--;
            } else {
                if (s_scroll + visible < total) s_scroll++;
            }
            mw_paint_window_client(msg->recipient_handle);
            break;
        }

        if (cx >= 0 && cx < list_w) {
            int  item     = (cy - list_y) / ENTRY_H + s_scroll;
            bool has_back = !at_root();

            if (has_back && item == 0) {
                // Go up one level, but not above root
                char *sep = strrchr(s_path[s_tab], '/');
                if (sep && sep != s_path[s_tab])
                    *sep = '\0';
                if (strlen(s_path[s_tab]) < strlen(cur_root()))
                    strncpy(s_path[s_tab], cur_root(), sizeof(s_path[0]) - 1);
                scan_dir();
            } else {
                int fi = item - (int)has_back;
                if (fi >= 0 && fi < s_count && s_entries[fi].is_dir) {
                    size_t cur_len = strlen(s_path[s_tab]);
                    size_t name_len = strlen(s_entries[fi].name);
                    if (cur_len + name_len + 2 < sizeof(s_path[0])) {
                        s_path[s_tab][cur_len] = '/';
                        strcpy(s_path[s_tab] + cur_len + 1, s_entries[fi].name);
                        scan_dir();
                    }
                }
            }
            mw_paint_window_client(msg->recipient_handle);
        }
        break;
    }

    case MW_WINDOW_REMOVED_MESSAGE:
        taskbar_unregister(s_handle);
        s_handle = MW_INVALID_HANDLE;
        strncpy(s_path[0], SD_ROOT,     sizeof(s_path[0]) - 1);
        strncpy(s_path[1], SPIFFS_ROOT, sizeof(s_path[1]) - 1);
        break;

    default:
        break;
    }
}

void app_files_launch(void)
{
    if (s_handle != MW_INVALID_HANDLE) {
        if (mw_get_window_flags(s_handle) & MW_WINDOW_FLAG_IS_MINIMISED)
            mw_set_window_minimised(s_handle, false);
        mw_bring_window_to_front(s_handle);
        taskbar_set_focus(s_handle);
        mw_paint_all();
        return;
    }
    s_tab = 0;
    strncpy(s_path[0], SD_ROOT,     sizeof(s_path[0]) - 1);
    strncpy(s_path[1], SPIFFS_ROOT, sizeof(s_path[1]) - 1);
    mw_util_rect_t r;
    mw_util_set_rect(&r, APP_WIN_X(WIN_W), 12, WIN_W, WIN_H);
    s_handle = mw_add_window(&r, "Files",
        paint, message, NULL, 0, APP_WIN_FLAGS_TOUCH, NULL);
    taskbar_register(s_handle, "Files");
}
