// miniwin_keyboard.c — unified input pump for MiniWin
//
// Drains all registered input catcalls each tick. Each event is offered to
// the cursor layer first (pointer + trackball click). Events not consumed by
// the cursor are treated as keyboard events and posted to the focused window.
//
// Call miniwin_keyboard_poll() once per tick, after mw_process_message() and
// before miniwin_cursor_poll() (which only redraws — it no longer drains).

#include "miniwin_keyboard.h"
#include "miniwin_cursor.h"
#include "../../kernel/core/purr_kernel.h"
#include "../../kernel/catcalls/catcall_input.h"
#include "MiniWin/miniwin.h"
#include "esp_log.h"

static const char *TAG = "mw_input";

void miniwin_keyboard_poll(void)
{
    int n = purr_kernel_input_count();
    for (int i = 0; i < n; i++) {
        const catcall_input_t *inp = purr_kernel_input_at(i);
        if (!inp || !inp->poll_event) continue;

        input_event_t ev;
        while (inp->poll_event(&ev)) {
            // Offer to cursor first
            if (miniwin_cursor_handle_event(&ev)) continue;

            // Remaining key-down events → focused window
            if (ev.type != INPUT_EVENT_KEY_DOWN) continue;

            mw_handle_t focused = mw_find_window_with_focus();
            if (focused == MW_INVALID_HANDLE) continue;

            ESP_LOGD(TAG, "key 0x%04x → window %u", ev.keycode, (unsigned)focused);
            mw_post_message(MW_KEY_PRESSED_MESSAGE,
                            MW_UNUSED_MESSAGE_PARAMETER,
                            focused,
                            (uint32_t)(ev.keycode & 0xFF),
                            NULL,
                            MW_WINDOW_MESSAGE);
        }
    }
}
