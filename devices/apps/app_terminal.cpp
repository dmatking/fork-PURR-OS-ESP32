// PURR OS kernel terminal — runs shell commands, displays output in-app
// Input: keyboard (T-Deck physical keys or trackball click = Enter)
// Output: scrolling text display in window client area

#include "app_terminal.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "purr_apps_common.h"
#include "purr_taskbar.h"
#include "drv_shell.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "terminal";

// ── Layout ────────────────────────────────────────────────────────────────────
#define FONT_H      10   // MW_GL_FONT_9 line height approx
#define INPUT_H     14
#define PADDING      4
#define MAX_LINES   20
#define MAX_LINE_W  60   // chars per line

// ── State ─────────────────────────────────────────────────────────────────────
static mw_handle_t s_handle = MW_INVALID_HANDLE;

static char s_lines[MAX_LINES][MAX_LINE_W + 1];
static int  s_line_count = 0;

static char s_input[64];
static int  s_input_len = 0;

// ── Helpers ───────────────────────────────────────────────────────────────────
static void push_line(const char *text)
{
    if (s_line_count < MAX_LINES) {
        strncpy(s_lines[s_line_count], text, MAX_LINE_W);
        s_lines[s_line_count][MAX_LINE_W] = '\0';
        s_line_count++;
    } else {
        // Scroll up
        memmove(s_lines[0], s_lines[1], (MAX_LINES - 1) * (MAX_LINE_W + 1));
        strncpy(s_lines[MAX_LINES - 1], text, MAX_LINE_W);
        s_lines[MAX_LINES - 1][MAX_LINE_W] = '\0';
    }
}

static void push_text_block(const char *text)
{
    // Split text on newlines and push each line
    char buf[MAX_LINE_W + 1];
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        if (len > MAX_LINE_W) len = MAX_LINE_W;
        if (len > 0) {
            strncpy(buf, p, len);
            buf[len] = '\0';
            push_line(buf);
        }
        if (!nl) break;
        p = nl + 1;
    }
}

static void run_command(void)
{
    if (s_input_len == 0) return;

    // Echo command
    char echo[MAX_LINE_W + 1];
    snprintf(echo, sizeof(echo), "> %.57s", s_input);
    push_line(echo);

    // Execute
    static char out[1024];
    int n = purr_shell_run(s_input, out, sizeof(out));
    if (n > 0) {
        push_text_block(out);
    } else {
        push_line("(no output)");
    }

    s_input[0]  = '\0';
    s_input_len = 0;

    ESP_LOGD(TAG, "ran: %s", echo);
}

// ── Paint ─────────────────────────────────────────────────────────────────────
static void paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);

    // Background
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(0x000000);
    mw_gl_rectangle(d, 0, 0, cr.width, cr.height);

    // Lines of output
    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);

    int text_h = cr.height - INPUT_H - PADDING * 2;
    int max_visible = text_h / FONT_H;
    int start = s_line_count > max_visible ? s_line_count - max_visible : 0;

    for (int i = start; i < s_line_count; i++) {
        int row = i - start;
        int16_t y = (int16_t)(PADDING + row * FONT_H);
        mw_gl_set_fg_colour(0x00FF00);
        mw_gl_string(d, PADDING, y, s_lines[i]);
    }

    // Input bar
    int16_t iy = (int16_t)(cr.height - INPUT_H);
    mw_gl_set_solid_fill_colour(0x003300);
    mw_gl_rectangle(d, 0, iy, cr.width, INPUT_H);

    // Prompt + current input
    char prompt[68];
    snprintf(prompt, sizeof(prompt), "> %s_", s_input);
    mw_gl_set_fg_colour(0x00FF00);
    mw_gl_string(d, PADDING, (int16_t)(iy + 2), prompt);
}

// ── Message ───────────────────────────────────────────────────────────────────
static void message(const mw_message_t *msg)
{
    switch (msg->message_id) {

    case MW_WINDOW_CREATED_MESSAGE:
        push_line("PURR OS kernel terminal");
        push_line("Type command + Enter (trackball click)");
        push_line("Type 'help' for commands");
        push_line("---");
        mw_set_timer(MW_TICKS_PER_SECOND, s_handle, MW_WINDOW_MESSAGE);
        mw_paint_window_client(s_handle);
        break;

    case MW_TIMER_MESSAGE:
        mw_paint_window_client(s_handle);
        mw_set_timer(MW_TICKS_PER_SECOND, s_handle, MW_WINDOW_MESSAGE);
        break;

    case MW_KEY_PRESSED_MESSAGE: {
        uint8_t c = (uint8_t)msg->message_data;
        // Nav codes from trackball
        if (c == 0x0D || c == '\r' || c == '\n') {
            run_command();
        } else if (c == '\b' || c == 0x7F) {
            if (s_input_len > 0) { s_input_len--; s_input[s_input_len] = '\0'; }
        } else if (c >= 0x20 && c < 0x7F && s_input_len < (int)sizeof(s_input) - 1) {
            s_input[s_input_len++] = (char)c;
            s_input[s_input_len]   = '\0';
        }
        // Ignore other nav codes (arrow keys etc)
        mw_paint_window_client(s_handle);
        break;
    }

    case MW_WINDOW_REMOVED_MESSAGE:
        taskbar_unregister(s_handle);
        s_handle = MW_INVALID_HANDLE;
        break;

    default:
        break;
    }
}

// ── Launch ────────────────────────────────────────────────────────────────────
void app_terminal_launch(void)
{
    if (s_handle != MW_INVALID_HANDLE) {
        mw_bring_window_to_front(s_handle);
        return;
    }

    mw_util_rect_t r;
    mw_util_set_rect(&r, 10, 10, 300, 200);
    s_handle = mw_add_window(&r, "Terminal",
        paint, message, NULL, 0, APP_WIN_FLAGS_TOUCH, NULL);
    taskbar_register(s_handle, "Terminal");
}
