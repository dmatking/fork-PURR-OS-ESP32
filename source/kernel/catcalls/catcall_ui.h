#pragma once
// catcall_ui.h — unified UI catcall contract
//
// UI modules (kittenui, miniwin) register an implementation of this struct.
// Apps call purr_win_*() helpers (purr_win.h) which dispatch through the
// registered implementation — apps never reference LVGL or MiniWin directly.
//
// Handle types are opaque uint32_t tokens. 0 = invalid/NULL handle.
// The backend maps tokens to its internal widget pointers.

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define CATCALL_UI_VERSION 4

typedef uint32_t purr_win_t;   // window handle
typedef uint32_t purr_wid_t;   // widget handle (label, button, textarea, etc.)

// Widget event types passed to callbacks
typedef enum {
    PURR_EVENT_CLICKED   = 0,
    PURR_EVENT_CHANGED   = 1,   // textarea text changed
    PURR_EVENT_FOCUSED   = 2,
    PURR_EVENT_DEFOCUS   = 3,
    PURR_EVENT_SELECTED  = 4,   // list: highlight moved (no confirm)
    PURR_EVENT_ACTIVATED = 5,   // list: entry confirmed/entered
} purr_event_t;

typedef void (*purr_win_cb_t)(purr_wid_t wid, purr_event_t event, void *user);

// Text alignment
typedef enum {
    PURR_ALIGN_LEFT   = 0,
    PURR_ALIGN_CENTER = 1,
    PURR_ALIGN_RIGHT  = 2,
} purr_align_t;

// Layout direction for purr_win_begin_row / purr_win_begin_col
typedef enum {
    PURR_LAYOUT_ROW = 0,
    PURR_LAYOUT_COL = 1,
} purr_layout_t;

typedef struct {
    const char  *name;
    uint8_t      catcall_version;   // must equal CATCALL_UI_VERSION

    // ── Window lifecycle ───────────────────────────────────────────────────
    purr_win_t (*win_create)  (const char *title);
    void       (*win_destroy) (purr_win_t win);
    void       (*win_show)    (purr_win_t win);
    void       (*win_hide)    (purr_win_t win);
    void       (*win_clear)   (purr_win_t win);   // remove all child widgets

    // Fires cb when the window's native title-bar close button is clicked,
    // in addition to the backend's own default hide-on-close behavior — lets
    // something outside the app (app_manager) know the user asked to fully
    // close this window, without the backend needing to know what that
    // means. Optional: backends that don't implement it can leave this NULL.
    void       (*win_on_close) (purr_win_t win, purr_win_cb_t cb, void *user);

    // ── Labels ─────────────────────────────────────────────────────────────
    purr_wid_t (*label_create) (purr_win_t win, const char *text);
    void       (*label_set)    (purr_wid_t wid, const char *text);
    void       (*label_align)  (purr_wid_t wid, purr_align_t align);

    // ── Buttons ────────────────────────────────────────────────────────────
    purr_wid_t (*btn_create)   (purr_win_t win, const char *label,
                                purr_win_cb_t cb, void *user);
    void       (*btn_enable)   (purr_wid_t wid, bool enabled);

    // ── Text area (scrollable multi-line display + optional input) ─────────
    purr_wid_t (*textarea_create) (purr_win_t win, uint16_t w_pct, uint16_t h_pct);
    void       (*textarea_append)  (purr_wid_t wid, const char *text);
    void       (*textarea_set)     (purr_wid_t wid, const char *text);
    void       (*textarea_clear)   (purr_wid_t wid);
    const char *(*textarea_get)    (purr_wid_t wid);    // current text (backend-owned)
    void       (*textarea_focus)   (purr_wid_t wid);    // show keyboard / cursor
    void       (*textarea_cb)      (purr_wid_t wid, purr_win_cb_t cb, void *user);

    // ── List (flat, non-nested selectable list) ─────────────────────────────
    purr_wid_t (*list_create)       (purr_win_t win, uint16_t w_pct, uint16_t h_pct);
    void       (*list_set_items)    (purr_wid_t wid, const char **items, int count);
    void       (*list_clear)        (purr_wid_t wid);
    int        (*list_get_selected) (purr_wid_t wid);   // -1 if none
    void       (*list_set_selected) (purr_wid_t wid, int index);
    void       (*list_cb)           (purr_wid_t wid, purr_win_cb_t cb, void *user);

    // ── Layout helpers ─────────────────────────────────────────────────────
    // Begin a row or column container inside win. Returns container widget.
    // grow: when true, the container expands to fill the remaining space in
    // its parent's flex layout instead of hugging its own content — needed
    // for any row/col holding percentage-sized children (e.g. a list+preview
    // split), which otherwise collapse to 0 size inside a content-sized
    // parent. false preserves the original hug-content behavior.
    purr_wid_t (*layout_begin) (purr_win_t win, purr_layout_t dir, uint8_t pad, bool grow);
    void       (*layout_end)   (purr_wid_t container);

    // ── Keyboard ───────────────────────────────────────────────────────────
    void (*kb_show) (purr_win_t win, purr_wid_t target_textarea);
    void (*kb_hide) (purr_win_t win);

} catcall_ui_t;
