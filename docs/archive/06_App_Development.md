# App Development

Writing a built-in MiniWin app in C++.

---

## File structure

Apps live in `devices/apps/`. Each app is a `.cpp` + `.h` pair. The glob in `lib_miniwin/CMakeLists.txt` picks up all `.cpp` files automatically.

```
devices/apps/
  app_myapp.h        ← declare: void app_myapp_launch(void)
  app_myapp.cpp      ← implement: paint callback, message callback, launch fn
```

Add the app to the catalog in `purr_app_catalog.cpp`:

```cpp
#include "app_myapp.h"

const purr_catalog_entry_t purr_catalog[] = {
    ...
    { "MyApp", app_myapp_launch },
};
```

---

## Minimal app skeleton

```cpp
// app_myapp.cpp
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "purr_apps_common.h"
#include "purr_taskbar.h"
#include "app_myapp.h"

static mw_handle_t s_handle;

static void myapp_paint(mw_handle_t handle, const mw_gl_draw_info_t *d)
{
    (void)handle;

    // Fill background
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, 0, 300, 200);

    // Draw text
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_string(d, 10, 10, "Hello from MyApp!");
}

static void myapp_message(const mw_message_t *msg)
{
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        mw_paint_window_client(msg->recipient_handle);
        taskbar_register(msg->recipient_handle, "MyApp");
        // Optional: start 1Hz repaint timer
        mw_set_timer(MW_TICKS_PER_SECOND, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;

    case MW_TIMER_MESSAGE:
        mw_paint_window_client(msg->recipient_handle);
        mw_set_timer(MW_TICKS_PER_SECOND, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;

    case MW_TOUCH_DOWN_MESSAGE: {
        int16_t cx = (int16_t)(msg->message_data >> 16);
        int16_t cy = (int16_t)(msg->message_data & 0xFFFF);
        // handle tap at cx, cy (client-relative)
        (void)cx; (void)cy;
        break;
    }

    case MW_WINDOW_REMOVED_MESSAGE:
        taskbar_unregister(msg->recipient_handle);
        break;

    default:
        break;
    }
}

void app_myapp_launch(void)
{
    // Don't open a second copy
    if (s_handle != MW_INVALID_HANDLE &&
        !(mw_get_window_flags(s_handle) & MW_WINDOW_FLAG_IS_MINIMISED)) {
        mw_bring_window_to_front(s_handle);
        mw_paint_all();
        return;
    }

    mw_util_rect_t r;
    mw_util_set_rect(&r, APP_WIN_X(300), 14, 300, 200);
    s_handle = mw_add_window(&r, "MyApp",
        myapp_paint, myapp_message,
        NULL, 0, APP_WIN_FLAGS_TOUCH, NULL);
}
```

```cpp
// app_myapp.h
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void app_myapp_launch(void);
#ifdef __cplusplus
}
#endif
```

---

## Drawing reference

All drawing happens inside the paint callback with a `const mw_gl_draw_info_t *d` handle.

```cpp
// Filled rectangle
mw_gl_set_fill(MW_GL_FILL);
mw_gl_set_border(MW_GL_BORDER_OFF);
mw_gl_set_solid_fill_colour(0x003000u);     // RGB888
mw_gl_rectangle(d, x, y, w, h);

// Outlined rectangle (no fill)
mw_gl_set_fill(MW_GL_NO_FILL);
mw_gl_set_border(MW_GL_BORDER_ON);
mw_gl_set_fg_colour(0x00FF00u);
mw_gl_rectangle(d, x, y, w, h);

// Text (font 9px)
mw_gl_set_fg_colour(0xFFFFFFu);
mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
mw_gl_set_font(MW_GL_FONT_9);
mw_gl_string(d, x, y, "text");

// Lines
mw_gl_set_fg_colour(color);
mw_gl_hline(d, x1, x2, y);
mw_gl_vline(d, x, y1, y2);
```

Colors are RGB888 `uint32_t` — MiniWin converts to native 16-bit for the display.

---

## WCE raised/sunken button helper

The WCE shell and all system apps use this 3D border style:

```cpp
static void draw_raised(const mw_gl_draw_info_t *d,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        mw_hal_lcd_colour_t fill)
{
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(fill); mw_gl_rectangle(d, x, y, w, h);
    mw_gl_set_fg_colour(WCE_HI);
    mw_gl_hline(d, x, x+w-1, y); mw_gl_vline(d, x, y, y+h-1);
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, x+1, x+w-2, y+h-2); mw_gl_vline(d, x+w-2, y+1, y+h-2);
    mw_gl_set_fg_colour(WCE_DARK);
    mw_gl_hline(d, x, x+w-1, y+h-1); mw_gl_vline(d, x+w-1, y, y+h-1);
}
```

---

## Optional-feature guards

If your app uses Lua or other optional modules, guard it:

```cpp
// In purr_app_catalog.cpp:
#ifdef PURR_HAS_MYAPP
#  include "app_myapp.h"
#endif

// In catalog array:
#ifdef PURR_HAS_MYAPP
    { "MyApp", app_myapp_launch },
#endif
```

Add `PURR_ENABLE_MYAPP` cmake flag following the same pattern as `PURR_ENABLE_MAGIDOS` in `main/CMakeLists.txt` and `lib_miniwin/CMakeLists.txt`.

---

## Multi-tab apps (Settings / Files pattern)

Use a `tab` state variable. In the paint callback, render the active tab's content. In the message callback, hit-test the tab bar row:

```cpp
static int s_tab = 0;  // 0 = first tab

// In paint:
static const char *TABS[] = { "WiFi", "System", "About" };
int16_t tw = client_w / 3;
for (int i = 0; i < 3; i++) {
    mw_hal_lcd_colour_t fg = (i == s_tab) ? WCE_TXT : WCE_SHD;
    mw_gl_string(d, i * tw + 4, 2, TABS[i]);   // fg already set
}

// In touch handler (touch coords are already client-relative):
int16_t cx = (int16_t)(msg->message_data >> 16);
int16_t cy = (int16_t)(msg->message_data & 0xFFFF);
if (cy < TAB_H) {
    s_tab = cx / tab_width;
    mw_paint_window_client(msg->recipient_handle);
}
```
