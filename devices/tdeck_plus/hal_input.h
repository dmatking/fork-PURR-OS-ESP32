#pragma once
#include "miniwin.h"
#include "gl/gl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void    hal_input_init();
void    hal_input_tick();                                  // call every MiniWin timer tick
void    hal_input_draw_cursor(const mw_gl_draw_info_t *d); // call from shell root paint

bool    hal_input_cursor_visible();
void    hal_input_get_cursor(int16_t *x, int16_t *y);

bool    hal_input_key_available();
uint8_t hal_input_key_read();

bool    hal_input_click_pending();  // consume pending trackball click

// Call when the touch IC reports an actual finger touch — hides trackball cursor
// so touch takes over. Cursor reappears automatically on next trackball movement.
void    hal_input_notify_touch();

#ifdef __cplusplus
}
#endif
