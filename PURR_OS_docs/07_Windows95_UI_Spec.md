# PURR OS — Windows 95 UI Spec (explorer.paw alternate theme)

## Overview

This is an alternate theme for explorer.paw that mimics Windows 95 more closely than
the CE variant. The key differences from CE: slightly chunkier chrome, the iconic
Start button with the Windows logo text, the classic grey desktop, a thicker taskbar
(32px like Win95), and modal dialogs that match the Win95 look exactly. Everything
still runs on LVGL on the ESP32-S3. Swap this in place of the CE explorer.paw — same
contract calls, different visuals.

**Target size:** 900KB–1.3MB compiled (slightly larger due to richer chrome assets)

---

## Visual Differences from CE Theme

| Element | Windows CE Theme | Windows 95 Theme |
|---|---|---|
| Desktop color | Teal `0x008080` | Dark teal `0x008080` (same, Win95 default) |
| Taskbar height | 32px | 32px |
| Taskbar color | Navy `0x000080` | Grey `0xC0C0C0` with raised bevel |
| Start button | Plain grey + "Start" text | Raised bevel + "Start" + Win logo |
| Title bar gradient | Flat navy | Flat navy (Win95 had flat, not gradient) |
| Title bar buttons | X only | Minimize + Maximize + Close |
| Window chrome | Thin bevel | Thicker raised bevel (2px highlight + 2px shadow) |
| Dialog buttons | Single centered | Multiple, right-aligned |
| Icon size | 16px tray | 32px desktop icons + 16px tray |
| Font | Montserrat 12 | Montserrat 12 (closest to MS Sans Serif 8pt) |
| Scrollbar | LVGL default | Raised bevel scrollbar with arrows |

---

## Color Palette

| Element | Hex | Notes |
|---|---|---|
| Desktop | `0x008080` | Classic Win95 teal |
| Window face | `0xD4D0C8` | Slightly warmer than pure `0xC0C0C0` |
| Window frame | `0x000000` | 1px outer border |
| Button face | `0xD4D0C8` | Same as window face |
| Button highlight | `0xFFFFFF` | Top + left bevel edge |
| Button light shadow | `0xD4D0C8` | Inner bottom + right |
| Button shadow | `0x808080` | Outer bottom + right |
| Button dark shadow | `0x404040` | Far outer edge |
| Title bar active | `0x000080` | Navy — active window |
| Title bar inactive | `0x808080` | Grey — inactive / background window |
| Title bar text active | `0xFFFFFF` | |
| Title bar text inactive | `0xC0C0C0` | |
| Menu bar | `0xD4D0C8` | |
| Menu highlight | `0x000080` | Selected item bg |
| Menu highlight text | `0xFFFFFF` | |
| Menu separator | `0x808080` / `0xFFFFFF` | Double line — grey then white |
| Desktop icon text | `0xFFFFFF` | White with dark shadow |
| Desktop icon text shadow | `0x000000` | 1px offset shadow |
| Scrollbar track | `0xD4D0C8` | Hatched pattern if possible |
| Scrollbar thumb | `0xD4D0C8` | Raised bevel |

---

## Bevel System — Win95 Style

Win95 uses a 4-layer bevel (not 2 like CE). From outer to inner, going clockwise
from top-left:

```
Layer 1 (outermost): dark shadow  0x404040 — bottom + right
Layer 2:             shadow       0x808080 — bottom + right (inside layer 1)
Layer 3:             highlight    0xFFFFFF — top + left
Layer 4 (innermost): light shadow 0xD4D0C8 — top + left (inside layer 3)
```

For pressed (sunken) state, layers 3 and 4 swap with 1 and 2.

```cpp
// win95_bevel.h

// Draw full 4-layer raised Win95 bevel around an lv_obj
// Uses canvas drawing since LVGL can't do asymmetric per-side colors natively
void win95_draw_raised(lv_obj_t* obj) {
  int x  = lv_obj_get_x(obj);
  int y  = lv_obj_get_y(obj);
  int w  = lv_obj_get_width(obj);
  int h  = lv_obj_get_height(obj);
  lv_obj_t* parent = lv_obj_get_parent(obj);

  // Helper: draw 1px horizontal or vertical line
  auto hline = [&](lv_obj_t* p, int lx, int ly, int len, uint32_t color) {
    lv_obj_t* line = lv_obj_create(p);
    lv_obj_set_size(line, len, 1);
    lv_obj_set_pos(line, lx, ly);
    lv_obj_set_style_bg_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
  };
  auto vline = [&](lv_obj_t* p, int lx, int ly, int len, uint32_t color) {
    lv_obj_t* line = lv_obj_create(p);
    lv_obj_set_size(line, 1, len);
    lv_obj_set_pos(line, lx, ly);
    lv_obj_set_style_bg_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
  };

  // Layer 3: highlight (top + left, white)
  hline(parent, x,     y,     w,   0xFFFFFF);
  vline(parent, x,     y,     h,   0xFFFFFF);

  // Layer 4: light shadow (top+1 + left+1, face color)
  hline(parent, x+1,   y+1,   w-2, 0xD4D0C8);
  vline(parent, x+1,   y+1,   h-2, 0xD4D0C8);

  // Layer 2: shadow (bottom-1 + right-1)
  hline(parent, x+1,   y+h-2, w-2, 0x808080);
  vline(parent, x+w-2, y+1,   h-2, 0x808080);

  // Layer 1: dark shadow (bottom + right, outermost)
  hline(parent, x,     y+h-1, w,   0x404040);
  vline(parent, x+w-1, y,     h,   0x404040);
}

void win95_draw_sunken(lv_obj_t* obj) {
  // Same as raised but swap highlight (0xFFFFFF) ↔ dark shadow (0x404040)
  // and light shadow (0xD4D0C8) ↔ shadow (0x808080)
  // (implementation mirrors raised with colors swapped)
}
```

---

## Screen Layout (320×480)

```
┌────────────────────────────────────────┐  y=0
│  [icon]  [icon]  [icon]               │
│                                        │  Desktop (teal)
│  [icon]  [icon]                        │  320 × 448px
│                                        │
│   [ App Window ]                       │
│   ┌──────────────────────────────┐     │
│   │ Title Bar  [_][□][X]        │     │
│   ├──────────────────────────────┤     │
│   │                              │     │
│   │   Content area               │     │
│   │                              │     │
│   └──────────────────────────────┘     │
│                                        │  y=448
├────────────────────────────────────────┤
│[Start][▌] │[App1][App2]  │  [tray] 3:14│  y=448–480 taskbar (32px)
└────────────────────────────────────────┘  y=480
```

---

## Taskbar — Win95 Style

Win95 taskbar is grey with a raised bevel on the top edge only (not full surround).
Start button has the Win logo + text. The app area is in the middle. Tray is sunken
on the right.

```cpp
// win95_taskbar.cpp

void taskbar_build_win95(lv_obj_t* parent) {

  // Taskbar base — grey, not navy (Win95 difference from CE)
  lv_obj_t* bar = lv_obj_create(parent);
  lv_obj_set_size(bar, 320, 32);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0xC0C0C0), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  // Top highlight line (raised bevel top edge only)
  lv_obj_t* top_line = lv_obj_create(bar);
  lv_obj_set_size(top_line, 320, 1);
  lv_obj_set_pos(top_line, 0, 0);
  lv_obj_set_style_bg_color(top_line, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(top_line, 0, 0);

  // ── Start button ──────────────────────────────────────────────────────────
  lv_obj_t* start = lv_btn_create(bar);
  lv_obj_set_size(start, 58, 24);
  lv_obj_set_pos(start, 2, 4);
  lv_obj_set_style_bg_color(start, lv_color_hex(0xD4D0C8), 0);
  lv_obj_set_style_radius(start, 0, 0);
  win95_draw_raised(start);

  // Win logo placeholder (small colored square — real logo needs BMP asset)
  lv_obj_t* logo = lv_obj_create(start);
  lv_obj_set_size(logo, 14, 14);
  lv_obj_set_pos(logo, 2, 4);
  lv_obj_set_style_bg_color(logo, lv_color_hex(0xFF0000), 0);  // Red quadrant placeholder
  lv_obj_set_style_border_width(logo, 0, 0);

  lv_obj_t* start_lbl = lv_label_create(start);
  lv_label_set_text(start_lbl, "Start");
  lv_obj_set_style_text_font(start_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(start_lbl, lv_color_hex(0x000000), 0);
  lv_obj_set_pos(start_lbl, 18, 5);
  lv_obj_add_event_cb(start, start_btn_cb, LV_EVENT_CLICKED, NULL);

  // ── App slots (center) ────────────────────────────────────────────────────
  for (int i = 0; i < 3; i++) {
    lv_obj_t* slot = lv_btn_create(bar);
    lv_obj_set_size(slot, 72, 24);
    lv_obj_set_pos(slot, 64 + (i * 76), 4);
    lv_obj_set_style_bg_color(slot, lv_color_hex(0xD4D0C8), 0);
    lv_obj_set_style_radius(slot, 0, 0);
    win95_draw_raised(slot);
    lv_obj_add_flag(slot, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* slot_lbl = lv_label_create(slot);
    lv_label_set_text(slot_lbl, "");
    lv_obj_set_style_text_font(slot_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(slot_lbl, LV_ALIGN_LEFT_MID, 4, 0);
  }

  // ── System tray (right, sunken) ───────────────────────────────────────────
  lv_obj_t* tray = lv_obj_create(bar);
  lv_obj_set_size(tray, 100, 24);
  lv_obj_align(tray, LV_ALIGN_RIGHT_MID, -2, 0);
  lv_obj_set_style_bg_color(tray, lv_color_hex(0xC0C0C0), 0);
  lv_obj_set_style_border_width(tray, 0, 0);
  win95_draw_sunken(tray);
  lv_obj_clear_flag(tray, LV_OBJ_FLAG_SCROLLABLE);

  // Tray icons (16px each, 2px gap)
  // LoRa icon
  lv_obj_t* lora_icon = lv_label_create(tray);
  lv_label_set_text(lora_icon, "L");
  lv_obj_set_style_text_font(lora_icon, &lv_font_montserrat_10, 0);
  lv_obj_set_pos(lora_icon, 4, 5);

  // WiFi icon
  lv_obj_t* wifi_icon = lv_label_create(tray);
  lv_label_set_text(wifi_icon, "W");
  lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_10, 0);
  lv_obj_set_pos(wifi_icon, 20, 5);

  // BT icon
  lv_obj_t* bt_icon = lv_label_create(tray);
  lv_label_set_text(bt_icon, "B");
  lv_obj_set_style_text_font(bt_icon, &lv_font_montserrat_10, 0);
  lv_obj_set_pos(bt_icon, 36, 5);

  // Clock
  lv_obj_t* clock_lbl = lv_label_create(tray);
  lv_label_set_text(clock_lbl, "3:14");
  lv_obj_set_style_text_font(clock_lbl, &lv_font_montserrat_10, 0);
  lv_obj_align(clock_lbl, LV_ALIGN_RIGHT_MID, -4, 0);
}
```

---

## Start Menu — Win95 Style

Win95 Start menu is taller and has a colored side banner with the OS name + version.
It also has a Programs submenu indicator (arrow) even if we don't implement submenus
on a 320px screen.

```cpp
// win95_start_menu.cpp

void start_menu_open_win95(lv_obj_t* parent) {

  // Outer container — appears just above taskbar
  lv_obj_t* menu = lv_obj_create(parent);
  lv_obj_set_size(menu, 190, 350);
  lv_obj_align(menu, LV_ALIGN_BOTTOM_LEFT, 2, -34);
  lv_obj_set_style_bg_color(menu, lv_color_hex(0xD4D0C8), 0);
  lv_obj_set_style_radius(menu, 0, 0);
  lv_obj_set_style_border_width(menu, 2, 0);
  lv_obj_set_style_border_color(menu, lv_color_hex(0x000000), 0);
  win95_draw_raised(menu);
  lv_obj_clear_flag(menu, LV_OBJ_FLAG_SCROLLABLE);

  // ── Side banner (left strip) ──────────────────────────────────────────────
  lv_obj_t* banner = lv_obj_create(menu);
  lv_obj_set_size(banner, 24, 346);
  lv_obj_set_pos(banner, 0, 0);
  lv_obj_set_style_bg_color(banner, lv_color_hex(0x000080), 0);
  lv_obj_set_style_border_width(banner, 0, 0);
  lv_obj_set_style_radius(banner, 0, 0);

  // "PURR OS 1.0" vertical text (rotated)
  lv_obj_t* ver_lbl = lv_label_create(banner);
  lv_label_set_text(ver_lbl, "PURR OS 1.0");
  lv_obj_set_style_text_color(ver_lbl, lv_color_hex(0x404080), 0);  // Dim blue
  lv_obj_set_style_text_font(ver_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_transform_angle(ver_lbl, 900, 0);
  lv_obj_align(ver_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);

  // ── Menu items ────────────────────────────────────────────────────────────
  // Each item is 165×26px, positioned right of banner

  const char* top_items[] = { "Programs ►", "Documents", "Settings", "Find", "Help", "Run..." };
  int top_count = 6;

  int item_y = 8;
  for (int i = 0; i < top_count; i++) {
    lv_obj_t* item = lv_obj_create(menu);
    lv_obj_set_size(item, 163, 26);
    lv_obj_set_pos(item, 25, item_y);
    lv_obj_set_style_bg_color(item, lv_color_hex(0xD4D0C8), 0);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_style_radius(item, 0, 0);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);

    // Hover highlight effect
    lv_obj_add_event_cb(item, [](lv_event_t* e) {
      lv_obj_t* obj = lv_event_get_target(e);
      lv_event_code_t code = lv_event_get_code(e);
      if (code == LV_EVENT_FOCUSED || code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x000080), 0);
        lv_obj_t* lbl = lv_obj_get_child(obj, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
      } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_RELEASED) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(0xD4D0C8), 0);
        lv_obj_t* lbl = lv_obj_get_child(obj, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
      }
    }, LV_EVENT_ALL, NULL);

    lv_obj_t* item_lbl = lv_label_create(item);
    lv_label_set_text(item_lbl, top_items[i]);
    lv_obj_set_style_text_font(item_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(item_lbl, lv_color_hex(0x000000), 0);
    lv_obj_align(item_lbl, LV_ALIGN_LEFT_MID, 24, 0);

    item_y += 26;
  }

  // ── Divider above Shut Down ───────────────────────────────────────────────
  lv_obj_t* sep1 = lv_obj_create(menu);
  lv_obj_set_size(sep1, 163, 1);
  lv_obj_set_pos(sep1, 25, item_y + 2);
  lv_obj_set_style_bg_color(sep1, lv_color_hex(0x808080), 0);
  lv_obj_set_style_border_width(sep1, 0, 0);

  lv_obj_t* sep2 = lv_obj_create(menu);
  lv_obj_set_size(sep2, 163, 1);
  lv_obj_set_pos(sep2, 25, item_y + 3);
  lv_obj_set_style_bg_color(sep2, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(sep2, 0, 0);

  item_y += 8;

  // Shut Down item
  lv_obj_t* shutdown = lv_obj_create(menu);
  lv_obj_set_size(shutdown, 163, 26);
  lv_obj_set_pos(shutdown, 25, item_y);
  lv_obj_set_style_bg_color(shutdown, lv_color_hex(0xD4D0C8), 0);
  lv_obj_set_style_border_width(shutdown, 0, 0);
  lv_obj_set_style_radius(shutdown, 0, 0);
  lv_obj_add_event_cb(shutdown, shutdown_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* sd_lbl = lv_label_create(shutdown);
  lv_label_set_text(sd_lbl, "Shut Down...");
  lv_obj_set_style_text_font(sd_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(sd_lbl, LV_ALIGN_LEFT_MID, 24, 0);

  // ── Programs submenu area (below top items, above separator) ──────────────
  // Filled dynamically with .paw apps and .purr firmware from KITT
  // Uses same item style as top_items

  // Apps from KITT
  int app_count = kitt_app_list_count();
  for (int i = 0; i < app_count && i < 6; i++) {
    kitt_app_entry_t app;
    kitt_app_get_entry(i, &app);
    // Add to Programs submenu list (same style as top_items)
    // For now: append below "Programs ►" as flat list (no true submenu on 320px)
  }
}
```

---

## Window Chrome — Win95 Style

Win95 windows have a title bar with three buttons: minimize, maximize, close.
On a 320×480 screen we skip minimize/maximize for fullscreen apps (no room) but
include them for lightweight overlay windows.

```cpp
// win95_window.cpp

lv_obj_t* win95_titlebar_create(lv_obj_t* parent,
                                 const char* title,
                                 bool show_minmax,
                                 bool active) {

  lv_obj_t* bar = lv_obj_create(parent);
  lv_obj_set_size(bar, lv_obj_get_width(parent) - 4, 22);
  lv_obj_set_pos(bar, 2, 2);
  lv_obj_set_style_bg_color(bar,
    active ? lv_color_hex(0x000080) : lv_color_hex(0x808080), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  // Title label
  lv_obj_t* lbl = lv_label_create(bar);
  lv_label_set_text(lbl, title);
  lv_obj_set_style_text_color(lbl,
    active ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xC0C0C0), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);

  // Close button
  lv_obj_t* close = lv_btn_create(bar);
  lv_obj_set_size(close, 16, 14);
  lv_obj_align(close, LV_ALIGN_RIGHT_MID, -2, 0);
  lv_obj_set_style_bg_color(close, lv_color_hex(0xD4D0C8), 0);
  lv_obj_set_style_radius(close, 0, 0);
  win95_draw_raised(close);
  lv_obj_t* x = lv_label_create(close);
  lv_label_set_text(x, "X");
  lv_obj_set_style_text_font(x, &lv_font_montserrat_10, 0);
  lv_obj_center(x);

  if (show_minmax) {
    // Maximize button
    lv_obj_t* maxbtn = lv_btn_create(bar);
    lv_obj_set_size(maxbtn, 16, 14);
    lv_obj_align(maxbtn, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(maxbtn, lv_color_hex(0xD4D0C8), 0);
    lv_obj_set_style_radius(maxbtn, 0, 0);
    win95_draw_raised(maxbtn);
    lv_obj_t* sq = lv_label_create(maxbtn);
    lv_label_set_text(sq, "□");
    lv_obj_set_style_text_font(sq, &lv_font_montserrat_10, 0);
    lv_obj_center(sq);

    // Minimize button
    lv_obj_t* minbtn = lv_btn_create(bar);
    lv_obj_set_size(minbtn, 16, 14);
    lv_obj_align(minbtn, LV_ALIGN_RIGHT_MID, -38, 0);
    lv_obj_set_style_bg_color(minbtn, lv_color_hex(0xD4D0C8), 0);
    lv_obj_set_style_radius(minbtn, 0, 0);
    win95_draw_raised(minbtn);
    lv_obj_t* dash = lv_label_create(minbtn);
    lv_label_set_text(dash, "_");
    lv_obj_set_style_text_font(dash, &lv_font_montserrat_10, 0);
    lv_obj_center(dash);
  }

  return bar;
}
```

---

## Win95 Dialog Box

Classic Win95 message box — icon on left, message on right, buttons bottom-right.

```cpp
void win95_show_dialog(lv_obj_t* parent,
                       const char* title,
                       const char* message,
                       const char* btn1_text,
                       const char* btn2_text,       // NULL = single button
                       void (*btn1_cb)(void),
                       void (*btn2_cb)(void)) {

  // Dim overlay
  lv_obj_t* overlay = lv_obj_create(parent);
  lv_obj_set_size(overlay, 320, 480);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_30, 0);
  lv_obj_set_style_border_width(overlay, 0, 0);

  // Dialog box
  lv_obj_t* dlg = lv_obj_create(overlay);
  lv_obj_set_size(dlg, 260, btn2_text ? 160 : 140);
  lv_obj_align(dlg, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(dlg, lv_color_hex(0xD4D0C8), 0);
  lv_obj_set_style_radius(dlg, 0, 0);
  lv_obj_set_style_border_width(dlg, 2, 0);
  win95_draw_raised(dlg);
  lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

  // Title bar
  win95_titlebar_create(dlg, title, false, true);

  // Info icon (placeholder — Win95 used exclamation / question mark icons)
  lv_obj_t* icon = lv_label_create(dlg);
  lv_label_set_text(icon, "!");
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
  lv_obj_set_pos(icon, 10, 32);

  // Message
  lv_obj_t* msg = lv_label_create(dlg);
  lv_label_set_text(msg, message);
  lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
  lv_obj_set_width(msg, 200);
  lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(msg, 36, 30);

  // Buttons — right-aligned, Win95 style
  int btn_y = lv_obj_get_height(dlg) - 36;
  int btn_x = lv_obj_get_width(dlg) - 10;

  if (btn2_text) {
    // Two buttons
    lv_obj_t* btn2 = lv_btn_create(dlg);
    lv_obj_set_size(btn2, 72, 24);
    lv_obj_set_pos(btn2, btn_x - 72, btn_y);
    lv_obj_set_style_bg_color(btn2, lv_color_hex(0xD4D0C8), 0);
    lv_obj_set_style_radius(btn2, 0, 0);
    win95_draw_raised(btn2);
    lv_obj_t* b2l = lv_label_create(btn2);
    lv_label_set_text(b2l, btn2_text);
    lv_obj_set_style_text_font(b2l, &lv_font_montserrat_12, 0);
    lv_obj_center(b2l);
    lv_obj_add_event_cb(btn2, [](lv_event_t* e) {
      // Dismiss overlay + call btn2_cb
      lv_obj_del(lv_obj_get_parent(lv_event_get_target(e)));
      // btn2_cb passed as user_data
      void (*cb)(void) = (void(*)(void))lv_event_get_user_data(e);
      if (cb) cb();
    }, LV_EVENT_CLICKED, (void*)btn2_cb);

    btn_x -= 80;
  }

  lv_obj_t* btn1 = lv_btn_create(dlg);
  lv_obj_set_size(btn1, 72, 24);
  lv_obj_set_pos(btn1, btn_x - 72, btn_y);
  lv_obj_set_style_bg_color(btn1, lv_color_hex(0xD4D0C8), 0);
  lv_obj_set_style_radius(btn1, 0, 0);
  win95_draw_raised(btn1);
  lv_obj_t* b1l = lv_label_create(btn1);
  lv_label_set_text(b1l, btn1_text);
  lv_obj_set_style_text_font(b1l, &lv_font_montserrat_12, 0);
  lv_obj_center(b1l);
  lv_obj_add_event_cb(btn1, [](lv_event_t* e) {
    lv_obj_del(lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target(e))));
    void (*cb)(void) = (void(*)(void))lv_event_get_user_data(e);
    if (cb) cb();
  }, LV_EVENT_CLICKED, (void*)btn1_cb);
}
```

---

## Desktop Icons

Win95 has desktop icons — 32×32 BMP with a text label below. On 320px you can fit
about 4 columns of icons.

```cpp
// win95_desktop.cpp

typedef struct {
  lv_obj_t* icon_img;
  lv_obj_t* icon_lbl;
  lv_obj_t* container;
  char app_path[128];
} desktop_icon_t;

desktop_icon_t* win95_desktop_icon_create(lv_obj_t* parent,
                                           const char* label,
                                           const char* icon_bmp_path,
                                           int col, int row,
                                           const char* app_path) {
  desktop_icon_t* icon = (desktop_icon_t*)malloc(sizeof(desktop_icon_t));

  int x = 8 + (col * 72);   // 72px column spacing
  int y = 8 + (row * 72);   // 72px row spacing

  icon->container = lv_obj_create(parent);
  lv_obj_set_size(icon->container, 64, 64);
  lv_obj_set_pos(icon->container, x, y);
  lv_obj_set_style_bg_opa(icon->container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(icon->container, 0, 0);
  lv_obj_add_flag(icon->container, LV_OBJ_FLAG_CLICKABLE);
  strlcpy(icon->app_path, app_path, sizeof(icon->app_path));

  // Icon image (32×32 BMP)
  icon->icon_img = lv_img_create(icon->container);
  lv_img_set_src(icon->icon_img, icon_bmp_path);
  lv_obj_align(icon->icon_img, LV_ALIGN_TOP_MID, 0, 0);

  // Label below icon with text shadow effect
  icon->icon_lbl = lv_label_create(icon->container);
  lv_label_set_text(icon->icon_lbl, label);
  lv_obj_set_style_text_font(icon->icon_lbl, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(icon->icon_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_width(icon->icon_lbl, 64);
  lv_label_set_long_mode(icon->icon_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(icon->icon_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(icon->icon_lbl, LV_ALIGN_BOTTOM_MID, 0, 0);

  // Single tap = highlight, double tap = launch
  lv_obj_add_event_cb(icon->container, desktop_icon_click_cb,
                      LV_EVENT_CLICKED, (void*)icon);

  return icon;
}

static void desktop_icon_click_cb(lv_event_t* e) {
  desktop_icon_t* icon = (desktop_icon_t*)lv_event_get_user_data(e);
  static uint32_t last_click_time = 0;
  static desktop_icon_t* last_clicked = NULL;

  uint32_t now = millis();
  if (last_clicked == icon && now - last_click_time < 500) {
    // Double tap — launch
    system_app_launch(icon->app_path);
  } else {
    // Single tap — select (highlight)
    lv_obj_set_style_bg_color(icon->container, lv_color_hex(0x000080), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(icon->container, LV_OPA_50, LV_STATE_FOCUSED);
  }
  last_click_time = now;
  last_clicked = icon;
}
```

---

## Win95 vs CE — Which to Use

| Criteria | Use Win95 Theme | Use CE Theme |
|---|---|---|
| Screen size | 320×480 (CattoPad) | Either |
| Touch input | Touch available | Either |
| Target feel | Desktop-like, icon-driven | Mobile-like, app-list driven |
| Desktop icons wanted | Yes | No |
| Taskbar style | Grey with raised bevel | Navy flat |
| Dialog style | Two-button right-aligned | Single centered button |

Both themes use identical KITT + system.paw contract calls. The only difference is
the visual implementation of explorer.paw. To switch, drop the alternate explorer.paw
bundle into /system/ and reboot.

---

## File Structure

```
system/explorer_win95.paw/
├── main.cpp
├── manifest.json
├── win95_bevel.h/.cpp         # 4-layer bevel draw helpers
├── win95_taskbar.h/.cpp       # Grey taskbar + Start button + tray
├── win95_start_menu.h/.cpp    # Win95 Start menu with side banner
├── win95_window.h/.cpp        # Title bar with min/max/close buttons
├── win95_dialog.h/.cpp        # Win95 message box
├── win95_desktop.h/.cpp       # Teal desktop + desktop icons
├── win95_toast.h/.cpp         # Toast notification (same as CE)
├── win95_file_explorer.h/.cpp # File manager (same structure as CE)
└── assets/
    ├── icons/
    │   ├── mycomputer_32.bmp
    │   ├── recycle_32.bmp
    │   ├── notes_32.bmp
    │   ├── msn_32.bmp
    │   └── controlpanel_32.bmp
    ├── start_logo.bmp          # 16×14 Win logo for Start button
    └── wallpaper.bmp           # Optional 320×448 desktop wallpaper
```
