// miniwin_win.c — catcall_ui_t backend for MiniWin
// Registers via purr_kernel_register_ui() during miniwin module init.
// MiniWin API: MW_WINDOW_HANDLE, MwCreateWindow, MwAddControl, etc.

#include <string.h>
#include <stdlib.h>
#include "../../kernel/catcalls/catcall_ui.h"
#include "../../kernel/core/purr_kernel.h"

// MiniWin headers — included from the MiniWin subdir
#include "MiniWin/miniwin.h"

// ── Handle pool ───────────────────────────────────────────────────────────────

#define MAX_WINS  16
#define MAX_WIDS  128

typedef struct {
    MW_WINDOW_HANDLE   mw_win;
    bool               used;
} win_slot_t;

typedef struct {
    MW_WINDOW_HANDLE   mw_win;
    MW_CONTROL_HANDLE  mw_ctrl;
    purr_win_cb_t      cb;
    void              *user;
    bool               used;
} wid_slot_t;

static win_slot_t s_wins[MAX_WINS];
static wid_slot_t s_wids[MAX_WIDS];

static purr_win_t alloc_win(MW_WINDOW_HANDLE h) {
    for (int i = 0; i < MAX_WINS; i++)
        if (!s_wins[i].used) { s_wins[i].mw_win = h; s_wins[i].used = true; return (purr_win_t)(i+1); }
    return 0;
}
static MW_WINDOW_HANDLE get_win(purr_win_t h) {
    if (h < 1 || h > MAX_WINS || !s_wins[h-1].used) return MW_INVALID_HANDLE_VALUE;
    return s_wins[h-1].mw_win;
}
static void free_win(purr_win_t h) {
    if (h >= 1 && h <= MAX_WINS) s_wins[h-1].used = false;
}

static purr_wid_t alloc_wid(MW_WINDOW_HANDLE win, MW_CONTROL_HANDLE ctrl,
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
static void free_wid(purr_wid_t h) {
    if (h >= 1 && h <= MAX_WIDS) s_wids[h-1].used = false;
}

// ── Window ────────────────────────────────────────────────────────────────────

static purr_win_t mw_win_create(const char *title) {
    MW_WINDOW_HANDLE h = MwCreateWindow(0, 0, 320, 240,
        MW_WINDOW_FLAG_HAS_TITLE_BAR | MW_WINDOW_FLAG_IS_VISIBLE,
        title, NULL);
    if (h == MW_INVALID_HANDLE_VALUE) return 0;
    MwSetVisible(h, false);
    return alloc_win(h);
}

static void mw_win_destroy(purr_win_t h) {
    MW_WINDOW_HANDLE wh = get_win(h);
    if (wh != MW_INVALID_HANDLE_VALUE) MwDestroyWindow(wh);
    free_win(h);
}

static void mw_win_show(purr_win_t h) {
    MW_WINDOW_HANDLE wh = get_win(h);
    if (wh != MW_INVALID_HANDLE_VALUE) MwSetVisible(wh, true);
}

static void mw_win_hide(purr_win_t h) {
    MW_WINDOW_HANDLE wh = get_win(h);
    if (wh != MW_INVALID_HANDLE_VALUE) MwSetVisible(wh, false);
}

static void mw_win_clear(purr_win_t h) {
    MW_WINDOW_HANDLE wh = get_win(h);
    if (wh == MW_INVALID_HANDLE_VALUE) return;
    // Remove all controls registered to this window from our pool
    for (int i = 0; i < MAX_WIDS; i++) {
        if (s_wids[i].used && s_wids[i].mw_win == wh) {
            MwDestroyControl(wh, s_wids[i].mw_ctrl);
            s_wids[i].used = false;
        }
    }
}

// ── Labels ────────────────────────────────────────────────────────────────────

static purr_win_t s_label_y_cursor[MAX_WINS];  // simple vertical stacking

static purr_wid_t mw_label_create(purr_win_t h, const char *text) {
    MW_WINDOW_HANDLE wh = get_win(h);
    if (wh == MW_INVALID_HANDLE_VALUE) return 0;
    int y = (int)s_label_y_cursor[h-1];
    MW_CONTROL_HANDLE ctrl = MwAddLabel(wh, 4, y, 312, 18, text, 0);
    s_label_y_cursor[h-1] += 20;
    return alloc_wid(wh, ctrl, NULL, NULL);
}

static void mw_label_set(purr_wid_t wid, const char *text) {
    wid_slot_t *s = get_wid(wid);
    if (s) MwSetLabelText(s->mw_win, s->mw_ctrl, text);
}

static void mw_label_align(purr_wid_t wid, purr_align_t align) {
    (void)wid; (void)align;
    // MiniWin label alignment set at create time; no-op for now
}

// ── Buttons ───────────────────────────────────────────────────────────────────

static void mw_message_handler(MW_WINDOW_HANDLE win, MW_CONTROL_HANDLE ctrl,
                                MW_MESSAGE_T message, MW_MESSAGE_PARAM_T param1,
                                MW_MESSAGE_PARAM_T param2)
{
    if (message != MW_CONTROL_PUSHED_MESSAGE) return;
    // Find matching wid slot
    for (int i = 0; i < MAX_WIDS; i++) {
        if (s_wids[i].used && s_wids[i].mw_win == win && s_wids[i].mw_ctrl == ctrl) {
            if (s_wids[i].cb)
                s_wids[i].cb((purr_wid_t)(i+1), PURR_EVENT_CLICKED, s_wids[i].user);
            break;
        }
    }
}

static int s_btn_y_cursor[MAX_WINS];

static purr_wid_t mw_btn_create(purr_win_t h, const char *label,
                                  purr_win_cb_t cb, void *user) {
    MW_WINDOW_HANDLE wh = get_win(h);
    if (wh == MW_INVALID_HANDLE_VALUE) return 0;
    int y = s_btn_y_cursor[h-1];
    MW_CONTROL_HANDLE ctrl = MwAddButton(wh, 4, y, 80, 26, label,
                                          mw_message_handler, 0);
    s_btn_y_cursor[h-1] += 30;
    return alloc_wid(wh, ctrl, cb, user);
}

static void mw_btn_enable(purr_wid_t wid, bool enabled) {
    wid_slot_t *s = get_wid(wid);
    if (s) MwSetEnabled(s->mw_win, s->mw_ctrl, enabled);
}

// ── Textarea ──────────────────────────────────────────────────────────────────
// MiniWin uses a scrolling text box (arrow list or text edit box).
// We use MW_TEXT_BOX if available, otherwise a label with scrolling.

static purr_wid_t mw_ta_create(purr_win_t h, uint16_t w_pct, uint16_t h_pct) {
    MW_WINDOW_HANDLE wh = get_win(h);
    if (wh == MW_INVALID_HANDLE_VALUE) return 0;
    int w = (320 * w_pct) / 100;
    int ht = (200 * h_pct) / 100;
    MW_CONTROL_HANDLE ctrl = MwAddTextBox(wh, 4, 28, w - 8, ht, NULL, 0);
    return alloc_wid(wh, ctrl, NULL, NULL);
}

static void mw_ta_append(purr_wid_t wid, const char *text) {
    wid_slot_t *s = get_wid(wid);
    if (s) MwAppendTextBox(s->mw_win, s->mw_ctrl, text);
}

static void mw_ta_set(purr_wid_t wid, const char *text) {
    wid_slot_t *s = get_wid(wid);
    if (s) MwSetTextBoxText(s->mw_win, s->mw_ctrl, text);
}

static void mw_ta_clear(purr_wid_t wid) {
    wid_slot_t *s = get_wid(wid);
    if (s) MwSetTextBoxText(s->mw_win, s->mw_ctrl, "");
}

static const char *mw_ta_get(purr_wid_t wid) {
    wid_slot_t *s = get_wid(wid);
    if (!s) return NULL;
    return MwGetTextBoxText(s->mw_win, s->mw_ctrl);
}

static void mw_ta_focus(purr_wid_t wid) {
    wid_slot_t *s = get_wid(wid);
    if (s) MwSetFocus(s->mw_win, s->mw_ctrl);
}

static void mw_ta_cb(purr_wid_t wid, purr_win_cb_t cb, void *user) {
    wid_slot_t *s = get_wid(wid);
    if (s) { s->cb = cb; s->user = user; }
}

// ── Layout (simple, MiniWin doesn't have flex) ────────────────────────────────

static purr_wid_t mw_layout_begin(purr_win_t h, purr_layout_t dir, uint8_t pad) {
    // MiniWin has no container concept; return the window itself as a pseudo-container
    (void)dir; (void)pad;
    return (purr_wid_t)h;  // reuse win handle as layout token
}

static void mw_layout_end(purr_wid_t wid) { (void)wid; }

// ── Keyboard (MiniWin on-screen keyboard not standard — use input catcall) ───

static void mw_kb_show(purr_win_t h, purr_wid_t target) {
    (void)h; (void)target;
    // MiniWin doesn't have a built-in OSK; physical keyboard via catcall_input
}

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
