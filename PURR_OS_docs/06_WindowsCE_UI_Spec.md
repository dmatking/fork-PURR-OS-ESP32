# PURR OS — Windows CE UI Spec (explorer.paw)

## Overview

explorer.paw is the default UI shell for PURR OS on CattoPad (320×480 ILI9488). It mimics the Windows CE / Windows 95 aesthetic — grey beveled windows, navy taskbar, Start menu, system tray. It is built entirely on LVGL (Arduino port) and owns no hardware — all data comes from KITT APIs. It is fully swappable; any replacement that implements the contract calls works as a drop-in.

**Target size:** 800KB–1.2MB compiled

---

## Visual Style

### Color Palette

| Element | Color | Hex |
|---|---|---|
| Window background | Classic grey | `0xC0C0C0` |
| Taskbar | Windows navy | `0x000080` |
| Taskbar text | White | `0xFFFFFF` |
| Title bar active | Windows navy | `0x000080` |
| Title bar inactive | Mid grey | `0x808080` |
| Button face | Light grey | `0xD4D0C8` |
| Button highlight (bevel top/left) | White | `0xFFFFFF` |
| Button shadow (bevel bottom/right) | Dark grey | `0x808080` |
| Button dark shadow (outer) | Near black | `0x404040` |
| Desktop background | Teal | `0x008080` |
| Text (default) | Black | `0x000000` |
| Selected item | Windows blue | `0x000080` |
| Selected item text | White | `0xFFFFFF` |
| Menu background | Light grey | `0xD4D0C8` |
| Menu separator | Mid grey | `0x808080` |

### Bevel Drawing

LVGL does not natively render Win95-style 3D bevels. Draw them manually using `lv_canvas` or `lv_obj` border overrides:

```cpp
// Draw a raised bevel on any lv_obj — classic Win95 button look
void draw_raised_bevel(lv_obj_t* obj) {
  // Top and left edges — highlight (white)
  lv_obj_set_style_border_color(obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 2, LV_PART_MAIN);
  lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, LV_PART_MAIN);

  // Bottom and right edges — shadow (dark grey)
  // LVGL doesn't support per-side colors natively, so draw shadow manually:
  lv_obj_t* shadow_right = lv_obj_create(lv_obj_get_parent(obj));
  lv_obj_set_size(shadow_right, 2, lv_obj_get_height(obj));
  lv_obj_set_pos(shadow_right,
    lv_obj_get_x(obj) + lv_obj_get_width(obj),
    lv_obj_get_y(obj));
  lv_obj_set_style_bg_color(shadow_right, lv_color_hex(0x808080), 0);
  lv_obj_set_style_border_width(shadow_right, 0, 0);

  lv_obj_t* shadow_bottom = lv_obj_create(lv_obj_get_parent(obj));
  lv_obj_set_size(shadow_bottom, lv_obj_get_width(obj) + 2, 2);
  lv_obj_set_pos(shadow_bottom,
    lv_obj_get_x(obj),
    lv_obj_get_y(obj) + lv_obj_get_height(obj));
  lv_obj_set_style_bg_color(shadow_bottom, lv_color_hex(0x808080), 0);
  lv_obj_set_style_border_width(shadow_bottom, 0, 0);
}

// Draw a sunken bevel — used for pressed buttons and input fields
void draw_sunken_bevel(lv_obj_t* obj) {
  // Reverse of raised — shadow on top/left, highlight on bottom/right
  lv_obj_set_style_border_color(obj, lv_color_hex(0x808080), LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 2, LV_PART_MAIN);
  lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
  // Draw white lines on bottom/right same as above but color 0xFFFFFF
}
```

### Font

Win95/CE used bitmap fonts. Closest LVGL equivalent:

```cpp
// Use LVGL built-in monospace font at small sizes
// For best CE look, convert Tahoma 8pt or MS Sans Serif 8pt via LVGL font converter
// https://lvgl.io/tools/fontconverter

// Default fallback (built into LVGL, no conversion needed):
LV_FONT_DECLARE(lv_font_montserrat_12);  // Body text
LV_FONT_DECLARE(lv_font_montserrat_10);  // Small labels, status bar
LV_FONT_DECLARE(lv_font_montserrat_14);  // Title bars
```

---

## Screen Layout (320×480)

```
┌────────────────────────────────────┐  y=0
│           Desktop (teal)           │
│                                    │  y=32  ← taskbar height
│                                    │
│   [App window renders here]        │
│   320 × 408px usable area          │
│   (480 - 32px taskbar - 40px       │
│    tabview if controlpanel open)   │
│                                    │
│                                    │
│                                    │
│                                    │
│                                    │  y=448
├────────────────────────────────────┤
│ [Start] [App1] [App2]   [tray...] │  y=448–480 (taskbar, 32px)
└────────────────────────────────────┘  y=480
```

---

## Taskbar

Persistent. 320×32px. Always at the bottom. Never destroyed while explorer.paw is alive.

```cpp
// taskbar.cpp

static lv_obj_t* taskbar;
static lv_obj_t* start_btn;
static lv_obj_t* app_slot[3];   // Max 3 running app buttons
static lv_obj_t* tray;

// Tray items (right side, 32px each, icons)
static lv_obj_t* tray_battery;
static lv_obj_t* tray_wifi;
static lv_obj_t* tray_bt;
static lv_obj_t* tray_lora;
static lv_obj_t* tray_clock;

void taskbar_build(lv_obj_t* parent) {
  taskbar = lv_obj_create(parent);
  lv_obj_set_size(taskbar, 320, 32);
  lv_obj_align(taskbar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(taskbar, lv_color_hex(0x000080), 0);
  lv_obj_set_style_border_width(taskbar, 0, 0);
  lv_obj_set_style_radius(taskbar, 0, 0);
  lv_obj_clear_flag(taskbar, LV_OBJ_FLAG_SCROLLABLE);

  // Start button
  start_btn = lv_btn_create(taskbar);
  lv_obj_set_size(start_btn, 52, 24);
  lv_obj_align(start_btn, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_set_style_bg_color(start_btn, lv_color_hex(0xD4D0C8), 0);
  lv_obj_set_style_radius(start_btn, 0, 0);
  draw_raised_bevel(start_btn);

  lv_obj_t* start_lbl = lv_label_create(start_btn);
  lv_label_set_text(start_lbl, "Start");
  lv_obj_set_style_text_color(start_lbl, lv_color_hex(0x000000), 0);
  lv_obj_set_style_text_font(start_lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(start_lbl);
  lv_obj_add_event_cb(start_btn, start_btn_cb, LV_EVENT_CLICKED, NULL);

  // App slots (center) — hidden by default, shown when app launches
  for (int i = 0; i < 3; i++) {
    app_slot[i] = lv_btn_create(taskbar);
    lv_obj_set_size(app_slot[i], 70, 24);
    lv_obj_set_pos(app_slot[i], 58 + (i * 74), 4);
    lv_obj_set_style_bg_color(app_slot[i], lv_color_hex(0xD4D0C8), 0);
    lv_obj_set_style_radius(app_slot[i], 0, 0);
    draw_raised_bevel(app_slot[i]);
    lv_obj_add_flag(app_slot[i], LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* slot_lbl = lv_label_create(app_slot[i]);
    lv_label_set_text(slot_lbl, "");
    lv_obj_set_style_text_font(slot_lbl, &lv_font_montserrat_10, 0);
    lv_obj_center(slot_lbl);
  }

  // System tray (right side)
  tray = lv_obj_create(taskbar);
  lv_obj_set_size(tray, 130, 30);
  lv_obj_align(tray, LV_ALIGN_RIGHT_MID, -2, 0);
  lv_obj_set_style_bg_color(tray, lv_color_hex(0x000080), 0);
  lv_obj_set_style_border_width(tray, 1, 0);
  lv_obj_set_style_border_color(tray, lv_color_hex(0x808080), 0);
  lv_obj_set_style_border_side(tray, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_clear_flag(tray, LV_OBJ_FLAG_SCROLLABLE);

  // Tray icons (16×16 each, 4px gap)
  tray_lora    = tray_icon_create(tray, 0,   "L");
  tray_lora    = tray_icon_create(tray, 20,  "W");
  tray_bt      = tray_icon_create(tray, 40,  "B");
  tray_clock   = tray_clock_create(tray);
}

static lv_obj_t* tray_icon_create(lv_obj_t* parent, int x_offset, const char* label) {
  lv_obj_t* icon = lv_label_create(parent);
  lv_label_set_text(icon, label);
  lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_10, 0);
  lv_obj_set_pos(icon, x_offset, 8);
  return icon;
}

// Called by KITT tray_update_cb every 15–60s
void taskbar_update_tray(const kitt_tray_state_t* state) {
  // Update clock
  lv_label_set_text(tray_clock, state->time_str);

  // Update WiFi icon color
  lv_color_t wifi_color = state->wifi_connected ?
    lv_color_hex(0x00FF00) : lv_color_hex(0x808080);
  lv_obj_set_style_text_color(tray_wifi, wifi_color, 0);

  // Update BT icon
  lv_color_t bt_color = state->bt_enabled ?
    lv_color_hex(0x00FF00) : lv_color_hex(0x808080);
  lv_obj_set_style_text_color(tray_bt, bt_color, 0);

  // Update LoRa icon
  lv_color_t lora_color = state->lora_enabled ?
    lv_color_hex(0x00FF00) : lv_color_hex(0x808080);
  lv_obj_set_style_text_color(tray_lora, lora_color, 0);
}

// Called when an app launches — adds it to taskbar
void taskbar_add_app(int slot, const char* app_name) {
  if (slot < 0 || slot >= 3) return;
  lv_obj_clear_flag(app_slot[slot], LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* lbl = lv_obj_get_child(app_slot[slot], 0);
  lv_label_set_text(lbl, app_name);
}

// Called when an app closes
void taskbar_remove_app(int slot) {
  if (slot < 0 || slot >= 3) return;
  lv_obj_add_flag(app_slot[slot], LV_OBJ_FLAG_HIDDEN);
}
```

---

## Start Menu

Modal overlay. Appears above the desktop when Start button is tapped. Windows 95 style — vertical list with icons, divider, firmware section below.

```cpp
// start_menu.cpp

static lv_obj_t* menu_container = NULL;
static bool menu_open = false;

void start_menu_open(lv_obj_t* parent) {
  if (menu_open) {
    start_menu_close();
    return;
  }

  menu_open = true;

  // Container — appears just above taskbar, left-aligned
  menu_container = lv_obj_create(parent);
  lv_obj_set_size(menu_container, 180, 320);
  lv_obj_align(menu_container, LV_ALIGN_BOTTOM_LEFT, 2, -34);  // Above taskbar
  lv_obj_set_style_bg_color(menu_container, lv_color_hex(0xD4D0C8), 0);
  lv_obj_set_style_radius(menu_container, 0, 0);
  lv_obj_set_style_border_width(menu_container, 2, 0);
  lv_obj_set_style_border_color(menu_container, lv_color_hex(0x000000), 0);
  draw_raised_bevel(menu_container);

  // Windows CE side banner (left strip, navy + white text rotated)
  lv_obj_t* banner = lv_obj_create(menu_container);
  lv_obj_set_size(banner, 20, 320);
  lv_obj_set_pos(banner, 0, 0);
  lv_obj_set_style_bg_color(banner, lv_color_hex(0x000080), 0);
  lv_obj_set_style_border_width(banner, 0, 0);
  // "PURR OS" vertical text — use a rotated label
  lv_obj_t* banner_lbl = lv_label_create(banner);
  lv_label_set_text(banner_lbl, "PURR");
  lv_obj_set_style_text_color(banner_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_transform_angle(banner_lbl, 900, 0);  // 90 degrees
  lv_obj_align(banner_lbl, LV_ALIGN_CENTER, 0, 0);

  // App list (scrollable, right of banner)
  lv_obj_t* app_list = lv_list_create(menu_container);
  lv_obj_set_size(app_list, 158, 220);
  lv_obj_set_pos(app_list, 22, 0);
  lv_obj_set_style_bg_color(app_list, lv_color_hex(0xD4D0C8), 0);
  lv_obj_set_style_border_width(app_list, 0, 0);
  lv_obj_set_style_radius(app_list, 0, 0);

  // Populate with apps from KITT
  int app_count = kitt_app_list_count();
  for (int i = 0; i < app_count; i++) {
    kitt_app_entry_t app;
    kitt_app_get_entry(i, &app);

    lv_obj_t* btn = lv_list_add_btn(app_list, NULL, app.name);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xD4D0C8), 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, app_launch_cb, LV_EVENT_CLICKED,
                        (void*)strdup(app.path));
  }

  // Divider
  lv_obj_t* divider = lv_obj_create(menu_container);
  lv_obj_set_size(divider, 158, 1);
  lv_obj_set_pos(divider, 22, 222);
  lv_obj_set_style_bg_color(divider, lv_color_hex(0x808080), 0);
  lv_obj_set_style_border_width(divider, 0, 0);

  // Firmware section label
  lv_obj_t* fw_lbl = lv_label_create(menu_container);
  lv_label_set_text(fw_lbl, "Firmware");
  lv_obj_set_style_text_font(fw_lbl, &lv_font_montserrat_10, 0);
  lv_obj_set_pos(fw_lbl, 26, 228);

  // Firmware list
  lv_obj_t* fw_list = lv_list_create(menu_container);
  lv_obj_set_size(fw_list, 158, 88);
  lv_obj_set_pos(fw_list, 22, 242);
  lv_obj_set_style_bg_color(fw_list, lv_color_hex(0xD4D0C8), 0);
  lv_obj_set_style_border_width(fw_list, 0, 0);

  int fw_count = kitt_firmware_list_count();
  for (int i = 0; i < fw_count; i++) {
    kitt_firmware_entry_t fw;
    kitt_firmware_get_entry(i, &fw);

    lv_obj_t* btn = lv_list_add_btn(fw_list, NULL, fw.name);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xD4D0C8), 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, firmware_launch_cb, LV_EVENT_CLICKED,
                        (void*)strdup(fw.path));
  }

  // Close menu on outside tap
  lv_obj_add_event_cb(lv_scr_act(), start_menu_outside_tap_cb,
                      LV_EVENT_CLICKED, NULL);
}

void start_menu_close(void) {
  if (menu_container) {
    lv_obj_del(menu_container);
    menu_container = NULL;
  }
  menu_open = false;
}

static void app_launch_cb(lv_event_t* e) {
  const char* path = (const char*)lv_event_get_user_data(e);
  start_menu_close();

  // Pre-launch check
  char reason[128];
  kitt_app_entry_t app;
  // Find entry by path
  for (int i = 0; i < kitt_app_list_count(); i++) {
    kitt_app_get_entry(i, &app);
    if (strcmp(app.path, path) == 0) break;
  }

  if (!kitt_can_launch_app(&app, reason, sizeof(reason))) {
    explorer_show_popup("Cannot Launch", reason, "OK");
    return;
  }

  system_app_launch(path);
}
```

---

## App Window

Each app gets a window container — title bar + content area. Fullscreen apps fill 320×448 (480 minus 32px taskbar). Lightweight overlay apps get a resizable window, smaller by default.

```cpp
// app_window.cpp

typedef struct {
  lv_obj_t* window;
  lv_obj_t* titlebar;
  lv_obj_t* title_label;
  lv_obj_t* close_btn;
  lv_obj_t* content;
  char app_name[64];
  int taskbar_slot;
} app_window_t;

app_window_t* app_window_create(lv_obj_t* parent,
                                 const char* title,
                                 bool fullscreen,
                                 int taskbar_slot) {
  app_window_t* win = (app_window_t*)malloc(sizeof(app_window_t));
  memset(win, 0, sizeof(app_window_t));
  strlcpy(win->app_name, title, sizeof(win->app_name));
  win->taskbar_slot = taskbar_slot;

  // Outer window frame
  win->window = lv_obj_create(parent);
  lv_obj_set_style_bg_color(win->window, lv_color_hex(0xC0C0C0), 0);
  lv_obj_set_style_radius(win->window, 0, 0);
  lv_obj_set_style_border_width(win->window, 2, 0);
  lv_obj_set_style_border_color(win->window, lv_color_hex(0x000000), 0);
  draw_raised_bevel(win->window);

  if (fullscreen) {
    lv_obj_set_pos(win->window, 0, 0);
    lv_obj_set_size(win->window, 320, 448);
  } else {
    // Lightweight overlay — centered, smaller
    lv_obj_set_size(win->window, 280, 360);
    lv_obj_align(win->window, LV_ALIGN_CENTER, 0, -16);
  }

  // Title bar
  win->titlebar = lv_obj_create(win->window);
  lv_obj_set_size(win->titlebar, lv_obj_get_width(win->window) - 4, 22);
  lv_obj_set_pos(win->titlebar, 2, 2);
  lv_obj_set_style_bg_color(win->titlebar, lv_color_hex(0x000080), 0);  // Active = navy
  lv_obj_set_style_border_width(win->titlebar, 0, 0);
  lv_obj_set_style_radius(win->titlebar, 0, 0);
  lv_obj_clear_flag(win->titlebar, LV_OBJ_FLAG_SCROLLABLE);

  // Title label
  win->title_label = lv_label_create(win->titlebar);
  lv_label_set_text(win->title_label, title);
  lv_obj_set_style_text_color(win->title_label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(win->title_label, &lv_font_montserrat_12, 0);
  lv_obj_align(win->title_label, LV_ALIGN_LEFT_MID, 4, 0);

  // Close button (X)
  win->close_btn = lv_btn_create(win->titlebar);
  lv_obj_set_size(win->close_btn, 18, 16);
  lv_obj_align(win->close_btn, LV_ALIGN_RIGHT_MID, -2, 0);
  lv_obj_set_style_bg_color(win->close_btn, lv_color_hex(0xD4D0C8), 0);
  lv_obj_set_style_radius(win->close_btn, 0, 0);
  draw_raised_bevel(win->close_btn);

  lv_obj_t* x = lv_label_create(win->close_btn);
  lv_label_set_text(x, "X");
  lv_obj_set_style_text_font(x, &lv_font_montserrat_10, 0);
  lv_obj_center(x);
  lv_obj_add_event_cb(win->close_btn, app_window_close_cb,
                      LV_EVENT_CLICKED, (void*)win);

  // Content area (below titlebar)
  win->content = lv_obj_create(win->window);
  int content_y = 26;
  lv_obj_set_pos(win->content, 2, content_y);
  lv_obj_set_size(win->content,
    lv_obj_get_width(win->window) - 4,
    lv_obj_get_height(win->window) - content_y - 2);
  lv_obj_set_style_bg_color(win->content, lv_color_hex(0xC0C0C0), 0);
  lv_obj_set_style_border_width(win->content, 0, 0);
  lv_obj_set_style_radius(win->content, 0, 0);
  draw_sunken_bevel(win->content);

  // Add to taskbar
  taskbar_add_app(taskbar_slot, title);

  return win;
}

static void app_window_close_cb(lv_event_t* e) {
  app_window_t* win = (app_window_t*)lv_event_get_user_data(e);
  taskbar_remove_app(win->taskbar_slot);
  system_app_close(win->app_name);
  lv_obj_del(win->window);
  free(win);
}
```

---

## Popup / Dialog

Blocking modal dialog — Win95 style message box with title bar and OK/Cancel buttons.

```cpp
// popup.cpp

typedef struct {
  lv_obj_t* overlay;
  lv_obj_t* dialog;
  void (*ok_cb)(void);
  void (*cancel_cb)(void);
} popup_t;

static popup_t active_popup = {0};

void explorer_show_popup(const char* title, const char* message, const char* btn_label) {
  // Dim overlay
  active_popup.overlay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(active_popup.overlay, 320, 480);
  lv_obj_set_pos(active_popup.overlay, 0, 0);
  lv_obj_set_style_bg_color(active_popup.overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(active_popup.overlay, LV_OPA_50, 0);
  lv_obj_set_style_border_width(active_popup.overlay, 0, 0);

  // Dialog box
  active_popup.dialog = lv_obj_create(active_popup.overlay);
  lv_obj_set_size(active_popup.dialog, 240, 160);
  lv_obj_align(active_popup.dialog, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(active_popup.dialog, lv_color_hex(0xD4D0C8), 0);
  lv_obj_set_style_radius(active_popup.dialog, 0, 0);
  lv_obj_set_style_border_width(active_popup.dialog, 2, 0);
  draw_raised_bevel(active_popup.dialog);

  // Title bar
  lv_obj_t* dlg_title = lv_obj_create(active_popup.dialog);
  lv_obj_set_size(dlg_title, 236, 22);
  lv_obj_set_pos(dlg_title, 2, 2);
  lv_obj_set_style_bg_color(dlg_title, lv_color_hex(0x000080), 0);
  lv_obj_set_style_border_width(dlg_title, 0, 0);

  lv_obj_t* dlg_title_lbl = lv_label_create(dlg_title);
  lv_label_set_text(dlg_title_lbl, title);
  lv_obj_set_style_text_color(dlg_title_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(dlg_title_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(dlg_title_lbl, LV_ALIGN_LEFT_MID, 4, 0);

  // Message label
  lv_obj_t* msg = lv_label_create(active_popup.dialog);
  lv_label_set_text(msg, message);
  lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
  lv_obj_set_width(msg, 220);
  lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(msg, 10, 32);

  // OK button
  lv_obj_t* ok_btn = lv_btn_create(active_popup.dialog);
  lv_obj_set_size(ok_btn, 80, 28);
  lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0xD4D0C8), 0);
  lv_obj_set_style_radius(ok_btn, 0, 0);
  draw_raised_bevel(ok_btn);

  lv_obj_t* ok_lbl = lv_label_create(ok_btn);
  lv_label_set_text(ok_lbl, btn_label ? btn_label : "OK");
  lv_obj_set_style_text_font(ok_lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(ok_lbl);
  lv_obj_add_event_cb(ok_btn, popup_close_cb, LV_EVENT_CLICKED, NULL);
}

static void popup_close_cb(lv_event_t* e) {
  if (active_popup.overlay) {
    lv_obj_del(active_popup.overlay);
    active_popup.overlay = NULL;
    active_popup.dialog  = NULL;
  }
  if (active_popup.ok_cb) active_popup.ok_cb();
}
```

---

## Toast Notification

Non-blocking — slides up from bottom, auto-dismisses after 3s.

```cpp
void explorer_show_toast(const char* message) {
  lv_obj_t* toast = lv_obj_create(lv_scr_act());
  lv_obj_set_size(toast, 280, 36);
  lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -36);  // Above taskbar
  lv_obj_set_style_bg_color(toast, lv_color_hex(0x000080), 0);
  lv_obj_set_style_border_width(toast, 1, 0);
  lv_obj_set_style_border_color(toast, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(toast, 2, 0);

  lv_obj_t* lbl = lv_label_create(toast);
  lv_label_set_text(lbl, message);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);

  // Auto-delete after 3 seconds
  lv_obj_t* toast_ptr = toast;
  lv_timer_t* t = lv_timer_create([](lv_timer_t* timer) {
    lv_obj_t* obj = (lv_obj_t*)timer->user_data;
    if (obj) lv_obj_del(obj);
    lv_timer_del(timer);
  }, 3000, toast_ptr);
  (void)t;
}
```

---

## File Explorer / File Picker

Windows CE style file manager. Used as a standalone view and as a picker called by other apps.

```cpp
// file_explorer.cpp

// Standalone launch: opens as a fullscreen app window
// Picker mode: opens as a modal, returns selected path via callback

typedef struct {
  lv_obj_t* container;
  lv_obj_t* path_label;
  lv_obj_t* file_list;
  char current_path[256];
  bool picker_mode;
  void (*result_cb)(const char* path);
} file_explorer_t;

static file_explorer_t fe = {0};

void file_explorer_open(lv_obj_t* parent,
                        const char* start_path,
                        bool picker_mode,
                        void (*result_cb)(const char* path)) {
  fe.picker_mode = picker_mode;
  fe.result_cb   = result_cb;
  strlcpy(fe.current_path, start_path, sizeof(fe.current_path));

  fe.container = lv_obj_create(parent);
  lv_obj_set_size(fe.container, picker_mode ? 280 : 320, picker_mode ? 360 : 448);
  if (picker_mode) lv_obj_align(fe.container, LV_ALIGN_CENTER, 0, -16);
  else lv_obj_set_pos(fe.container, 0, 0);
  lv_obj_set_style_bg_color(fe.container, lv_color_hex(0xC0C0C0), 0);
  lv_obj_set_style_border_width(fe.container, 0, 0);
  lv_obj_set_style_radius(fe.container, 0, 0);

  // Address bar
  lv_obj_t* addr_bar = lv_obj_create(fe.container);
  lv_obj_set_size(addr_bar, lv_obj_get_width(fe.container), 28);
  lv_obj_set_pos(addr_bar, 0, 0);
  lv_obj_set_style_bg_color(addr_bar, lv_color_hex(0xD4D0C8), 0);
  lv_obj_set_style_border_width(addr_bar, 1, 0);
  draw_sunken_bevel(addr_bar);

  fe.path_label = lv_label_create(addr_bar);
  lv_label_set_text(fe.path_label, start_path);
  lv_obj_set_style_text_font(fe.path_label, &lv_font_montserrat_10, 0);
  lv_obj_align(fe.path_label, LV_ALIGN_LEFT_MID, 4, 0);

  // File list
  fe.file_list = lv_list_create(fe.container);
  lv_obj_set_size(fe.file_list,
    lv_obj_get_width(fe.container),
    lv_obj_get_height(fe.container) - 28);
  lv_obj_set_pos(fe.file_list, 0, 28);
  lv_obj_set_style_bg_color(fe.file_list, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(fe.file_list, 0, 0);
  lv_obj_set_style_border_width(fe.file_list, 0, 0);

  file_explorer_populate(start_path);
}

static void file_explorer_populate(const char* path) {
  lv_obj_clean(fe.file_list);

  // Add ".." back entry if not at root
  if (strcmp(path, "/") != 0) {
    lv_obj_t* back = lv_list_add_btn(fe.file_list, NULL, ".. (Back)");
    lv_obj_add_event_cb(back, file_back_cb, LV_EVENT_CLICKED, NULL);
  }

  // Iterate SPIFFS directory
  File root = SPIFFS.open(path);
  File f = root.openNextFile();
  while (f) {
    char name[128];
    strlcpy(name, f.name(), sizeof(name));
    bool is_dir = f.isDirectory();

    lv_obj_t* btn = lv_list_add_btn(fe.file_list, NULL, name);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, LV_PART_MAIN);

    if (fe.picker_mode && !is_dir) {
      // In picker mode, file tap returns the path
      lv_obj_add_event_cb(btn, file_pick_cb, LV_EVENT_CLICKED,
                          (void*)strdup(f.path()));
    } else if (is_dir) {
      lv_obj_add_event_cb(btn, file_dir_cb, LV_EVENT_CLICKED,
                          (void*)strdup(f.path()));
    }
    f = root.openNextFile();
  }
}
```

---

## Desktop Background

Simple teal fill. Optional: scanline pattern for authenticity.

```cpp
void explorer_build_desktop(lv_obj_t* parent) {
  lv_obj_t* desktop = lv_obj_create(parent);
  lv_obj_set_size(desktop, 320, 448);
  lv_obj_set_pos(desktop, 0, 0);
  lv_obj_set_style_bg_color(desktop, lv_color_hex(0x008080), 0);  // Classic teal
  lv_obj_set_style_border_width(desktop, 0, 0);
  lv_obj_set_style_radius(desktop, 0, 0);
  lv_obj_clear_flag(desktop, LV_OBJ_FLAG_SCROLLABLE);

  // Desktop is the parent for all app windows and overlays
  // explorer stores a reference to pass to app_window_create()
}
```

---

## Explorer Contract Calls (Required for Replacement UIs)

Any replacement for explorer.paw must implement these:

```cpp
void explorer_update_tray(const kitt_tray_state_t* state);  // KITT push — radio/battery/clock
void explorer_show_popup(const char* title,                 // Blocking modal dialog
                         const char* message,
                         const char* btn_label);
void explorer_show_toast(const char* message);              // Non-blocking toast, 3s auto-dismiss
void explorer_show_crash_report(const char* app_name,      // App crash info on relaunch
                                 const char* reason);
void explorer_memory_warning(int percent_used);             // 90 / 95 / 98 thresholds
void explorer_list_refresh(void);                           // Re-query KITT for app+firmware lists
```

---

## File Structure

```
system/explorer.paw/
├── main.cpp               # Entry point — app_create, app_update, app_destroy
├── manifest.json
├── taskbar.h/.cpp         # Taskbar + system tray + app slots
├── start_menu.h/.cpp      # Start menu modal
├── app_window.h/.cpp      # Window frame + titlebar + content area
├── popup.h/.cpp           # Blocking modal dialog
├── toast.h/.cpp           # Non-blocking toast notification
├── file_explorer.h/.cpp   # File manager + file picker
├── desktop.h/.cpp         # Desktop background + app container
└── assets/
    ├── icons/             # 16x16 and 32x32 BMP icons for built-in apps
    ├── splash_logo.bmp    # Optional boot logo (used if KITT passes through)
    └── wallpaper.bmp      # Optional desktop wallpaper (320x448 BMP)
```

