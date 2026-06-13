// miniwin_win.c — catcall_ui_t backend for MiniWin
// Registers via purr_kernel_register_ui() during miniwin module init.

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "../../kernel/catcalls/catcall_ui.h"
#include "../../kernel/core/purr_kernel.h"
#include "MiniWin/miniwin.h"
#include "MiniWin/ui/ui_label.h"
#include "MiniWin/ui/ui_button.h"
#include "MiniWin/ui/ui_text_box.h"

// ── Handle pool ───────────────────────────────────────────────────────────────

#define MAX_WINS  16
#define MAX_WIDS  128

typedef struct {
    mw_handle_t    mw_win;
    bool           used;
} win_slot_t;

typedef struct {
    mw_handle_t    mw_win;
    mw_handle_t    mw_ctrl;
    purr_win_cb_t  cb;
    void          *user;
    bool           used;
} wid_slot_t;

static win_slot_t s_wins[MAX_WINS];
static wid_slot_t s_wids[MAX_WIDS];

static purr_win_t alloc_win(mw_handle_t h) {
    for (int i = 0; i < MAX_WINS; i++)
        if (!s_wins[i].used) { s_wins[i].mw_win = h; s_wins[i].used = true; return (purr_win_t)(i+1); }
    return 0;
}
static mw_handle_t get_win(purr_win_t h) {
    if (h < 1 || h > MAX_WINS || !s_wins[h-1].used) return MW_INVALID_HANDLE;
    return s_wins[h-1].mw_win;
}
static void free_win(purr_win_t h) {
    if (h >= 1 && h <= MAX_WINS) s_wins[h-1].used = false;
}

static purr_wid_t alloc_wid(mw_handle_t win, mw_handle_t ctrl,
                              purr_win_cb_t cb, void *user) {
    for (int i = 0; i < MAX_WIDS; i++) {
        if (!s_wids[i].used) {
            s_wids[i].mw_win  = win;
            s_wids[i].mw_ctrl = ctrl;
            s_wids[i].cb      = cb;
            s_wids[i].user    = user;
            s_wids[i].used    = true;
            return (purr_wid_t)(i+1);
        }
    }
    return 0;
}
static wid_slot_t *get_wid(purr_wid_t h) {
    if (h < 1 || h > MAX_WIDS || !s_wids[h-1].used) return NULL;
    return &s_wids[h-1];
}

// ── Per-window paint / message stubs ─────────────────────────────────────────

static void win_paint_func(mw_handle_t window_handle, const mw_gl_draw_info_t *draw_info)
{
    (void)window_handle; (void)draw_info;
}

static void win_message_func(const mw_message_t *msg)
{
    if (!msg) return;
    if (msg->message_id == MW_BUTTON_PRESSED_MESSAGE) {
        mw_handle_t ctrl = msg->sender_handle;
        mw_handle_t win  = msg->recipient_handle;
        for (int i = 0; i < MAX_WIDS; i++) {
            if (s_wids[i].used && s_wids[i].mw_win == win && s_wids[i].mw_ctrl == ctrl) {
                if (s_wids[i].cb)
                    s_wids[i].cb((purr_wid_t)(i+1), PURR_EVENT_CLICKED, s_wids[i].user);
                break;
            }
        }
    }
}

// ── Window ────────────────────────────────────────────────────────────────────

static purr_win_t mw_win_create(const char *title) {
    mw_util_rect_t r = { 0, 0, 320, 240 };
    mw_handle_t h = mw_add_window(&r, title,
        win_paint_func, win_message_func,
        NULL, 0,
        MW_WINDOW_FLAG_HAS_BORDER | MW_WINDOW_FLAG_HAS_TITLE_BAR,
        NULL);
    if (h == MW_INVALID_HANDLE) return 0;
    mw_set_window_visible(h, false);
    return alloc_win(h);
}

static void mw_win_destroy(purr_win_t h) {
    mw_handle_t wh = get_win(h);
    if (wh != MW_INVALID_HANDLE) mw_remove_window(wh);
    free_win(h);
}

static void mw_win_show(purr_win_t h) {
    mw_handle_t wh = get_win(h);
    if (wh != MW_INVALID_HANDLE) mw_set_window_visible(wh, true);
}

static void mw_win_hide(purr_win_t h) {
    mw_handle_t wh = get_win(h);
    if (wh != MW_INVALID_HANDLE) mw_set_window_visible(wh, false);
}

static void mw_win_clear(purr_win_t h) {
    mw_handle_t wh = get_win(h);
    if (wh == MW_INVALID_HANDLE) return;
    for (int i = 0; i < MAX_WIDS; i++) {
        if (s_wids[i].used && s_wids[i].mw_win == wh) {
            mw_remove_control(s_wids[i].mw_ctrl);
            s_wids[i].used = false;
        }
    }
}

// ── Labels ────────────────────────────────────────────────────────────────────

static int16_t s_label_y[MAX_WINS];

static mw_ui_label_data_t s_label_data[MAX_WIDS];

static purr_wid_t mw_label_create(purr_win_t h, const char *text) {
    mw_handle_t wh = get_win(h);
    if (wh == MW_INVALID_HANDLE) return 0;

    // Find a free label data slot
    int slot = -1;
    for (int i = 0; i < MAX_WIDS; i++) {
        if (!s_wids[i].used) { slot = i; break; }
    }
    if (slot < 0) return 0;

    memset(&s_label_data[slot], 0, sizeof(s_label_data[slot]));
    strncpy(s_label_data[slot].label, text, MW_UI_LABEL_MAX_CHARS);

    int16_t y = s_label_y[h-1];
    mw_handle_t ctrl = mw_ui_label_add_new(4, y, 312,
        wh,
        MW_CONTROL_FLAG_IS_VISIBLE | MW_CONTROL_FLAG_IS_ENABLED,
        &s_label_data[slot]);
    s_label_y[h-1] = (int16_t)(y + MW_UI_LABEL_HEIGHT + 2);

    return alloc_wid(wh, ctrl, NULL, NULL);
}

static void mw_label_set(purr_wid_t wid, const char *text) {
    wid_slot_t *s = get_wid(wid);
    if (!s) return;
    // Update instance data and repaint
    int idx = (int)(wid - 1);
    strncpy(s_label_data[idx].label, text, MW_UI_LABEL_MAX_CHARS);
    mw_paint_control(s->mw_ctrl);
}

static void mw_label_align(purr_wid_t wid, purr_align_t align) {
    (void)wid; (void)align;
}

// ── Buttons ───────────────────────────────────────────────────────────────────

static int16_t s_btn_y[MAX_WINS];
static mw_ui_button_data_t s_btn_data[MAX_WIDS];

static purr_wid_t mw_btn_create(purr_win_t h, const char *label,
                                  purr_win_cb_t cb, void *user) {
    mw_handle_t wh = get_win(h);
    if (wh == MW_INVALID_HANDLE) return 0;

    int slot = -1;
    for (int i = 0; i < MAX_WIDS; i++) {
        if (!s_wids[i].used) { slot = i; break; }
    }
    if (slot < 0) return 0;

    memset(&s_btn_data[slot], 0, sizeof(s_btn_data[slot]));
    strncpy(s_btn_data[slot].button_label, label, MW_UI_BUTTON_LABEL_MAX_CHARS);

    int16_t y = s_btn_y[h-1];
    mw_handle_t ctrl = mw_ui_button_add_new(4, y,
        wh,
        MW_CONTROL_FLAG_IS_VISIBLE | MW_CONTROL_FLAG_IS_ENABLED,
        &s_btn_data[slot]);
    s_btn_y[h-1] = (int16_t)(y + MW_UI_BUTTON_HEIGHT + 4);

    return alloc_wid(wh, ctrl, cb, user);
}

static void mw_btn_enable(purr_wid_t wid, bool enabled) {
    wid_slot_t *s = get_wid(wid);
    if (s) mw_set_control_enabled(s->mw_ctrl, enabled);
}

// ── Textarea ──────────────────────────────────────────────────────────────────

static mw_ui_text_box_data_t s_ta_data[MAX_WIDS];
static char s_ta_buf[MAX_WIDS][512];

static purr_wid_t mw_ta_create(purr_win_t h, uint16_t w_pct, uint16_t h_pct) {
    mw_handle_t wh = get_win(h);
    if (wh == MW_INVALID_HANDLE) return 0;

    int slot = -1;
    for (int i = 0; i < MAX_WIDS; i++) {
        if (!s_wids[i].used) { slot = i; break; }
    }
    if (slot < 0) return 0;

    memset(&s_ta_data[slot], 0, sizeof(s_ta_data[slot]));
    s_ta_buf[slot][0] = '\0';
    s_ta_data[slot].text = s_ta_buf[slot];

    mw_util_rect_t r = {
        4, 28,
        (int16_t)((320 * w_pct) / 100 - 8),
        (int16_t)((200 * h_pct) / 100)
    };
    mw_handle_t ctrl = mw_ui_text_box_add_new(&r, wh,
        MW_CONTROL_FLAG_IS_VISIBLE | MW_CONTROL_FLAG_IS_ENABLED,
        &s_ta_data[slot]);
    return alloc_wid(wh, ctrl, NULL, NULL);
}

static void mw_ta_append(purr_wid_t wid, const char *text) {
    wid_slot_t *s = get_wid(wid);
    if (!s) return;
    int idx = (int)(wid - 1);
    strncat(s_ta_buf[idx], text, sizeof(s_ta_buf[idx]) - strlen(s_ta_buf[idx]) - 1);
    mw_paint_control(s->mw_ctrl);
}

static void mw_ta_set(purr_wid_t wid, const char *text) {
    wid_slot_t *s = get_wid(wid);
    if (!s) return;
    int idx = (int)(wid - 1);
    strncpy(s_ta_buf[idx], text, sizeof(s_ta_buf[idx]) - 1);
    mw_paint_control(s->mw_ctrl);
}

static void mw_ta_clear(purr_wid_t wid) {
    mw_ta_set(wid, "");
}

static const char *mw_ta_get(purr_wid_t wid) {
    int idx = (int)(wid - 1);
    if (idx < 0 || idx >= MAX_WIDS || !s_wids[idx].used) return NULL;
    return s_ta_buf[idx];
}

static void mw_ta_focus(purr_wid_t wid) { (void)wid; }

static void mw_ta_cb(purr_wid_t wid, purr_win_cb_t cb, void *user) {
    wid_slot_t *s = get_wid(wid);
    if (s) { s->cb = cb; s->user = user; }
}

// ── Layout ────────────────────────────────────────────────────────────────────

static purr_wid_t mw_layout_begin(purr_win_t h, purr_layout_t dir, uint8_t pad) {
    (void)dir; (void)pad;
    return (purr_wid_t)h;
}

static void mw_layout_end(purr_wid_t wid) { (void)wid; }

// ── Keyboard ──────────────────────────────────────────────────────────────────

static void mw_kb_show(purr_win_t h, purr_wid_t target) { (void)h; (void)target; }
static void mw_kb_hide(purr_win_t h) { (void)h; }

// ── Registration ──────────────────────────────────────────────────────────────

static const catcall_ui_t s_miniwin_ui = {
    .name            = "miniwin",
    .catcall_version = CATCALL_UI_VERSION,
    .win_create      = mw_win_create,
    .win_destroy     = mw_win_destroy,
    .win_show        = mw_win_show,
    .win_hide        = mw_win_hide,
    .win_clear       = mw_win_clear,
    .label_create    = mw_label_create,
    .label_set       = mw_label_set,
    .label_align     = mw_label_align,
    .btn_create      = mw_btn_create,
    .btn_enable      = mw_btn_enable,
    .textarea_create = mw_ta_create,
    .textarea_append = mw_ta_append,
    .textarea_set    = mw_ta_set,
    .textarea_clear  = mw_ta_clear,
    .textarea_get    = mw_ta_get,
    .textarea_focus  = mw_ta_focus,
    .textarea_cb     = mw_ta_cb,
    .layout_begin    = mw_layout_begin,
    .layout_end      = mw_layout_end,
    .kb_show         = mw_kb_show,
    .kb_hide         = mw_kb_hide,
};

void miniwin_win_register(void) {
    purr_kernel_register_ui(&s_miniwin_ui);
}
