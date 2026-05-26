# PURR OS — Mac System 7.5 Color UI Spec (explorer.paw alternate theme)

## Overview

This is a third alternate theme for explorer.paw that mimics the classic Mac System
7.5 color aesthetic — the Platinum grey interface introduced in System 7.5 and refined
through Mac OS 8. Key elements: menu bar at the top (not bottom), the Apple menu,
Finder-style windows with a close box and title drag bar, the classic striped window
title bar, scroll bars with arrows at both ends, and the distinctive Platinum grey
color scheme. Everything still runs on LVGL on the ESP32-S3 with the same KITT
contract calls as the CE and Win95 themes.

**Target size:** 900KB–1.3MB compiled

---

## Visual Identity — System 7.5 Color / Platinum

System 7.5 Color (also called "Platinum" from System 7.5.3 onward) has a very
specific look that differs from both Win95 and CE:

- Menu bar is at the **top**, not the bottom
- No taskbar — running apps appear in the Application menu (top right)
- Windows have a **close box** (top left), **zoom box** (top right), and a
  **collapse box** (System 7.5 introduced window shading — double-click title = roll up)
- Title bar has the classic **horizontal pinstripe** pattern
- Scroll bars have arrows at **both ends** of the track
- Default pointer is the classic Mac arrow cursor
- Alert dialogs use the classic **bomb** or **caution** icon on the left
- The desktop is a **grey** pattern (not solid color) by default, or user-set
- Menus drop down from the menu bar with a **1px black border** and **white background**
- Selected menu items have a **black highlight with white text**
- The Apple logo (🍎) anchors the menu bar on the far left

---

## Color Palette

| Element | Hex | Notes |
|---|---|---|
| Platinum grey (window face) | `0xDDDDDD` | Slightly lighter than Win95 grey |
| Platinum dark (shadow) | `0xAAAAAA` | Scroll bar track, inactive elements |
| Platinum highlight | `0xEEEEEE` | Button top/left bevel |
| Platinum dark shadow | `0x888888` | Button bottom/right outer bevel |
| Menu bar background | `0xDDDDDD` | Same as Platinum |
| Menu bar text | `0x000000` | Black |
| Menu dropdown bg | `0xFFFFFF` | White |
| Menu dropdown border | `0x000000` | 1px black |
| Menu item selected bg | `0x000000` | Black highlight |
| Menu item selected text | `0xFFFFFF` | White |
| Menu separator | `0xAAAAAA` | 1px grey line |
| Title bar active (pinstripe) | `0x000000` / `0xDDDDDD` | Alternating 1px black/grey lines |
| Title bar inactive | `0xDDDDDD` | Flat grey, no pinstripe |
| Title bar text active | `0x000000` | Black, centered |
| Title bar text inactive | `0xAAAAAA` | Grey, centered |
| Close box | `0xDDDDDD` + black border | Small square top-left of title bar |
| Zoom box | `0xDDDDDD` + black border | Small square top-right of title bar |
| Scroll bar track | `0xBBBBBB` | With dot pattern if possible |
| Scroll bar thumb | `0xDDDDDD` | Raised bevel |
| Scroll arrow buttons | `0xDDDDDD` | Raised bevel, arrows at both ends |
| Desktop pattern | `0x888888` / `0xAAAAAA` | Classic grey checkerboard 2×2 |
| Desktop solid fallback | `0x888888` | If pattern too expensive |
| Default button ring | `0x000000` | 3px black ring around default button |
| Alert diamond bg | `0xFFFFFF` | White with black border |
| Window content area | `0xFFFFFF` | White inside the window |
| Window border | `0x000000` | 1px black outer border |

---

## Bevel System — Mac Platinum Style

Mac Platinum uses a simpler 2-layer bevel compared to Win95's 4-layer.
The highlight is on top/left, shadow on bottom/right, both 1px.

```cpp
// mac_bevel.h

void mac_draw_raised(lv_obj_t* obj) {
  // Top + left: highlight (light grey / white)
  // Bottom + right: shadow (medium grey)
  // Outer border: 1px black

  lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(obj, 1, 0);

  // Inner highlight line — top + left (drawn as child objects)
  lv_obj_t* parent = obj;
  int w = lv_obj_get_width(obj);
  int h = lv_obj_get_height(obj);

  // Top highlight
  lv_obj_t* hl_top = lv_obj_create(parent);
  lv_obj_set_size(hl_top, w - 2, 1);
  lv_obj_set_pos(hl_top, 1, 1);
  lv_obj_set_style_bg_color(hl_top, lv_color_hex(0xEEEEEE), 0);
  lv_obj_set_style_border_width(hl_top, 0, 0);
  lv_obj_set_style_radius(hl_top, 0, 0);

  // Left highlight
  lv_obj_t* hl_left = lv_obj_create(parent);
  lv_obj_set_size(hl_left, 1, h - 2);
  lv_obj_set_pos(hl_left, 1, 1);
  lv_obj_set_style_bg_color(hl_left, lv_color_hex(0xEEEEEE), 0);
  lv_obj_set_style_border_width(hl_left, 0, 0);
  lv_obj_set_style_radius(hl_left, 0, 0);

  // Bottom shadow
  lv_obj_t* sh_bot = lv_obj_create(parent);
  lv_obj_set_size(sh_bot, w - 2, 1);
  lv_obj_set_pos(sh_bot, 1, h - 2);
  lv_obj_set_style_bg_color(sh_bot, lv_color_hex(0x888888), 0);
  lv_obj_set_style_border_width(sh_bot, 0, 0);
  lv_obj_set_style_radius(sh_bot, 0, 0);

  // Right shadow
  lv_obj_t* sh_right = lv_obj_create(parent);
  lv_obj_set_size(sh_right, 1, h - 2);
  lv_obj_set_pos(sh_right, w - 2, 1);
  lv_obj_set_style_bg_color(sh_right, lv_color_hex(0x888888), 0);
  lv_obj_set_style_border_width(sh_right, 0, 0);
  lv_obj_set_style_radius(sh_right, 0, 0);
}

void mac_draw_sunken(lv_obj_t* obj) {
  // Reverse: shadow on top/left, highlight on bottom/right
  lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  // (mirror of raised with colors swapped — same structure)
}

// Default button ring — 3px black border ring (Mac default button indicator)
void mac_draw_default_ring(lv_obj_t* btn) {
  lv_obj_set_style_outline_color(btn, lv_color_hex(0x000000), 0);
  lv_obj_set_style_outline_width(btn, 3, 0);
  lv_obj_set_style_outline_pad(btn, 2, 0);
}
```

---

## Screen Layout (320×480)

```
┌────────────────────────────────────────┐  y=0
│🍎│ File  Edit  View  Apps  Special    ▸│  y=0–20 Menu bar (20px)
├────────────────────────────────────────┤  y=20
│                                        │
│  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  │  Desktop (grey checkerboard)
│  ░                                 ░  │  320 × 428px
│  ░  ┌─[×]──── My App ────────[□]─┐ ░  │
│  ░  │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│ ░  │  Window with pinstripe title
│  ░  ├────────────────────────────┤ ░  │
│  ░  │                         ▲  │ ░  │
│  ░  │   Content area          │  │ ░  │
│  ░  │                         ▼  │ ░  │
│  ░  │                         ▲  │ ░  │
│  ░  └────────────────────────────┘ ░  │
│  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  │
│                                        │  y=448
└────────────────────────────────────────┘  y=480
  No taskbar — App switcher is in menu bar
```

---

## Menu Bar

The menu bar runs the full width at the top. 20px tall. Platinum grey background.
Items: Apple menu (🍎), File, Edit, View, Apps (running app list), Special.
The Application menu (top right) shows the current foreground app name.

```cpp
// mac_menubar.cpp

static lv_obj_t* menubar       = NULL;
static lv_obj_t* apple_menu    = NULL;
static lv_obj_t* app_menu_lbl  = NULL;  // Top-right — current app name

static const char* menu_items[] = { "File", "Edit", "View", "Special" };
static lv_obj_t*  menu_labels[4];

void mac_menubar_build(lv_obj_t* parent) {
  menubar = lv_obj_create(parent);
  lv_obj_set_size(menubar, 320, 20);
  lv_obj_set_pos(menubar, 0, 0);
  lv_obj_set_style_bg_color(menubar, lv_color_hex(0xDDDDDD), 0);
  lv_obj_set_style_border_width(menubar, 0, 0);
  lv_obj_set_style_radius(menubar, 0, 0);
  lv_obj_clear_flag(menubar, LV_OBJ_FLAG_SCROLLABLE);

  // Bottom border line (1px black)
  lv_obj_t* border = lv_obj_create(menubar);
  lv_obj_set_size(border, 320, 1);
  lv_obj_set_pos(border, 0, 19);
  lv_obj_set_style_bg_color(border, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(border, 0, 0);

  // Apple menu (🍎 or "Apple")
  apple_menu = lv_label_create(menubar);
  lv_label_set_text(apple_menu, LV_SYMBOL_HOME);  // Placeholder — use Apple BMP if available
  lv_obj_set_style_text_font(apple_menu, &lv_font_montserrat_12, 0);
  lv_obj_set_pos(apple_menu, 6, 2);
  lv_obj_add_flag(apple_menu, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(apple_menu, apple_menu_cb, LV_EVENT_CLICKED, NULL);

  // Menu items
  int x = 28;
  for (int i = 0; i < 4; i++) {
    menu_labels[i] = lv_label_create(menubar);
    lv_label_set_text(menu_labels[i], menu_items[i]);
    lv_obj_set_style_text_font(menu_labels[i], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(menu_labels[i], lv_color_hex(0x000000), 0);
    lv_obj_set_pos(menu_labels[i], x, 2);
    lv_obj_add_flag(menu_labels[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(menu_labels[i], menu_item_cb,
                        LV_EVENT_CLICKED, (void*)(intptr_t)i);
    x += lv_obj_get_width(menu_labels[i]) + 12;
  }

  // Application menu (right side) — shows active app name
  app_menu_lbl = lv_label_create(menubar);
  lv_label_set_text(app_menu_lbl, "Finder ▸");
  lv_obj_set_style_text_font(app_menu_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(app_menu_lbl, LV_ALIGN_RIGHT_MID, -6, 0);
  lv_obj_add_flag(app_menu_lbl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(app_menu_lbl, app_switcher_cb, LV_EVENT_CLICKED, NULL);
}

// Called when a new app becomes foreground
void mac_menubar_set_app(const char* app_name) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%s ▸", app_name);
  lv_label_set_text(app_menu_lbl, buf);
}
```

---

## Menu Dropdown

Mac menus drop down from the menu bar with a white background and 1px black border.
Selected item has solid black highlight, white text. No submenus on 320px.

```cpp
// mac_menu_dropdown.cpp

static lv_obj_t* active_dropdown = NULL;

void mac_menu_open(lv_obj_t* parent,
                   int menu_x,
                   const char** items,
                   int count,
                   void (*item_cb)(int index)) {

  if (active_dropdown) {
    lv_obj_del(active_dropdown);
    active_dropdown = NULL;
  }

  // Calculate width from longest item
  int menu_w = 120;

  active_dropdown = lv_obj_create(parent);
  lv_obj_set_size(active_dropdown, menu_w, count * 22 + 4);
  lv_obj_set_pos(active_dropdown, menu_x, 20);
  lv_obj_set_style_bg_color(active_dropdown, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(active_dropdown, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(active_dropdown, 1, 0);
  lv_obj_set_style_radius(active_dropdown, 0, 0);
  lv_obj_set_style_pad_all(active_dropdown, 2, 0);
  lv_obj_clear_flag(active_dropdown, LV_OBJ_FLAG_SCROLLABLE);

  // Drop shadow (classic Mac menu has a 1px offset shadow on right + bottom)
  lv_obj_set_style_shadow_color(active_dropdown, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_width(active_dropdown, 2, 0);
  lv_obj_set_style_shadow_ofs_x(active_dropdown, 2, 0);
  lv_obj_set_style_shadow_ofs_y(active_dropdown, 2, 0);

  for (int i = 0; i < count; i++) {
    // Separator check (item == "---")
    if (strcmp(items[i], "---") == 0) {
      lv_obj_t* sep = lv_obj_create(active_dropdown);
      lv_obj_set_size(sep, menu_w - 8, 1);
      lv_obj_set_pos(sep, 4, 2 + i * 22 + 10);
      lv_obj_set_style_bg_color(sep, lv_color_hex(0xAAAAAA), 0);
      lv_obj_set_style_border_width(sep, 0, 0);
      continue;
    }

    lv_obj_t* item = lv_obj_create(active_dropdown);
    lv_obj_set_size(item, menu_w - 4, 20);
    lv_obj_set_pos(item, 2, 2 + i * 22);
    lv_obj_set_style_bg_color(item, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_style_radius(item, 0, 0);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* item_lbl = lv_label_create(item);
    lv_label_set_text(item_lbl, items[i]);
    lv_obj_set_style_text_font(item_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(item_lbl, lv_color_hex(0x000000), 0);
    lv_obj_align(item_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    // Hover: black bg + white text
    lv_obj_add_event_cb(item, [](lv_event_t* e) {
      lv_obj_t* obj = lv_event_get_target(e);
      lv_event_code_t code = lv_event_get_code(e);
      lv_obj_t* lbl = lv_obj_get_child(obj, 0);
      if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
      } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_DEFOCUSED) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(0xFFFFFF), 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
      }
    }, LV_EVENT_ALL, NULL);

    // Store index as user data for click callback
    lv_obj_add_event_cb(item, [](lv_event_t* e) {
      int idx = (int)(intptr_t)lv_event_get_user_data(e);
      // Close dropdown
      if (active_dropdown) {
        lv_obj_del(active_dropdown);
        active_dropdown = NULL;
      }
      // Call parent callback
      // (stored separately since lambda can't capture item_cb cleanly)
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }

  // Tap anywhere outside = close dropdown
  lv_obj_add_event_cb(lv_scr_act(), [](lv_event_t* e) {
    if (active_dropdown && lv_event_get_target(e) != active_dropdown) {
      lv_obj_del(active_dropdown);
      active_dropdown = NULL;
    }
  }, LV_EVENT_CLICKED, NULL);
}
```

---

## Finder Window

The classic Mac window: 1px black outer border, Platinum grey chrome, pinstripe title
bar, close box top-left, zoom box top-right, white content area, scroll bars on
right and bottom with arrows at both ends of each track.

```cpp
// mac_window.cpp

typedef struct {
  lv_obj_t* window;
  lv_obj_t* titlebar;
  lv_obj_t* title_label;
  lv_obj_t* close_box;
  lv_obj_t* zoom_box;
  lv_obj_t* content;
  lv_obj_t* scroll_v;    // Vertical scrollbar
  lv_obj_t* scroll_h;    // Horizontal scrollbar
  char title[64];
} mac_window_t;

// Pinstripe pattern — alternating 1px black / 1px grey horizontal lines
static void draw_pinstripe_titlebar(lv_obj_t* bar) {
  int w = lv_obj_get_width(bar);
  int h = lv_obj_get_height(bar);

  for (int y = 0; y < h; y++) {
    lv_obj_t* stripe = lv_obj_create(bar);
    lv_obj_set_size(stripe, w, 1);
    lv_obj_set_pos(stripe, 0, y);
    lv_obj_set_style_border_width(stripe, 0, 0);
    lv_obj_set_style_radius(stripe, 0, 0);
    // Alternate black and grey lines
    uint32_t color = (y % 2 == 0) ? 0x000000 : 0x888888;
    lv_obj_set_style_bg_color(stripe, lv_color_hex(color), 0);
  }
}

// Mac-style close/zoom box — small square with 1px black border
static lv_obj_t* mac_chrome_box_create(lv_obj_t* parent, int x, int y) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_set_size(box, 12, 12);
  lv_obj_set_pos(box, x, y);
  lv_obj_set_style_bg_color(box, lv_color_hex(0xDDDDDD), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_radius(box, 0, 0);
  lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
  return box;
}

// Scroll bar with arrows at BOTH ends (top arrow + bottom arrow on vertical bar)
static lv_obj_t* mac_scrollbar_vertical(lv_obj_t* parent, int x, int y, int h) {
  lv_obj_t* track = lv_obj_create(parent);
  lv_obj_set_size(track, 16, h);
  lv_obj_set_pos(track, x, y);
  lv_obj_set_style_bg_color(track, lv_color_hex(0xBBBBBB), 0);
  lv_obj_set_style_border_color(track, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(track, 1, 0);
  lv_obj_set_style_radius(track, 0, 0);
  lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);

  // Top arrow button
  lv_obj_t* arrow_top = lv_btn_create(track);
  lv_obj_set_size(arrow_top, 14, 14);
  lv_obj_set_pos(arrow_top, 1, 1);
  lv_obj_set_style_bg_color(arrow_top, lv_color_hex(0xDDDDDD), 0);
  lv_obj_set_style_radius(arrow_top, 0, 0);
  mac_draw_raised(arrow_top);
  lv_obj_t* up_lbl = lv_label_create(arrow_top);
  lv_label_set_text(up_lbl, LV_SYMBOL_UP);
  lv_obj_set_style_text_font(up_lbl, &lv_font_montserrat_10, 0);
  lv_obj_center(up_lbl);

  // Bottom arrow (top of bottom pair)
  lv_obj_t* arrow_bot1 = lv_btn_create(track);
  lv_obj_set_size(arrow_bot1, 14, 14);
  lv_obj_set_pos(arrow_bot1, 1, h - 30);
  lv_obj_set_style_bg_color(arrow_bot1, lv_color_hex(0xDDDDDD), 0);
  lv_obj_set_style_radius(arrow_bot1, 0, 0);
  mac_draw_raised(arrow_bot1);
  lv_obj_t* up2_lbl = lv_label_create(arrow_bot1);
  lv_label_set_text(up2_lbl, LV_SYMBOL_UP);
  lv_obj_set_style_text_font(up2_lbl, &lv_font_montserrat_10, 0);
  lv_obj_center(up2_lbl);

  // Bottom arrow (bottom of bottom pair)
  lv_obj_t* arrow_bot2 = lv_btn_create(track);
  lv_obj_set_size(arrow_bot2, 14, 14);
  lv_obj_set_pos(arrow_bot2, 1, h - 15);
  lv_obj_set_style_bg_color(arrow_bot2, lv_color_hex(0xDDDDDD), 0);
  lv_obj_set_style_radius(arrow_bot2, 0, 0);
  mac_draw_raised(arrow_bot2);
  lv_obj_t* dn_lbl = lv_label_create(arrow_bot2);
  lv_label_set_text(dn_lbl, LV_SYMBOL_DOWN);
  lv_obj_set_style_text_font(dn_lbl, &lv_font_montserrat_10, 0);
  lv_obj_center(dn_lbl);

  // Scroll thumb (middle — draggable placeholder)
  lv_obj_t* thumb = lv_obj_create(track);
  lv_obj_set_size(thumb, 14, 40);
  lv_obj_set_pos(thumb, 1, 18);
  lv_obj_set_style_bg_color(thumb, lv_color_hex(0xDDDDDD), 0);
  lv_obj_set_style_radius(thumb, 0, 0);
  mac_draw_raised(thumb);

  return track;
}

mac_window_t* mac_window_create(lv_obj_t* parent,
                                 const char* title,
                                 int x, int y,
                                 int w, int h,
                                 bool fullscreen) {
  mac_window_t* win = (mac_window_t*)malloc(sizeof(mac_window_t));
  memset(win, 0, sizeof(mac_window_t));
  strlcpy(win->title, title, sizeof(win->title));

  if (fullscreen) { x = 0; y = 20; w = 320; h = 460; }

  // Outer window — 1px black border
  win->window = lv_obj_create(parent);
  lv_obj_set_size(win->window, w, h);
  lv_obj_set_pos(win->window, x, y);
  lv_obj_set_style_bg_color(win->window, lv_color_hex(0xDDDDDD), 0);
  lv_obj_set_style_border_color(win->window, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(win->window, 1, 0);
  lv_obj_set_style_radius(win->window, 0, 0);
  lv_obj_clear_flag(win->window, LV_OBJ_FLAG_SCROLLABLE);

  // Title bar (19px tall, pinstripe active)
  win->titlebar = lv_obj_create(win->window);
  lv_obj_set_size(win->titlebar, w - 2, 19);
  lv_obj_set_pos(win->titlebar, 1, 1);
  lv_obj_set_style_bg_color(win->titlebar, lv_color_hex(0xDDDDDD), 0);
  lv_obj_set_style_border_width(win->titlebar, 0, 0);
  lv_obj_set_style_radius(win->titlebar, 0, 0);
  lv_obj_clear_flag(win->titlebar, LV_OBJ_FLAG_SCROLLABLE);

  // Draw pinstripe (active window)
  draw_pinstripe_titlebar(win->titlebar);

  // Close box (top-left, 4px from edge)
  win->close_box = mac_chrome_box_create(win->titlebar, 4, 3);
  lv_obj_add_event_cb(win->close_box, [](lv_event_t* e) {
    mac_window_t* w = (mac_window_t*)lv_event_get_user_data(e);
    system_app_close(w->title);
    lv_obj_del(w->window);
    free(w);
  }, LV_EVENT_CLICKED, (void*)win);

  // Zoom box (top-right, 4px from edge)
  win->zoom_box = mac_chrome_box_create(win->titlebar, w - 20, 3);

  // Title label (centered in title bar, on top of pinstripe)
  win->title_label = lv_label_create(win->titlebar);
  lv_label_set_text(win->title_label, title);
  lv_obj_set_style_text_font(win->title_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(win->title_label, lv_color_hex(0x000000), 0);
  // White bg behind text to appear over pinstripe
  lv_obj_set_style_bg_color(win->title_label, lv_color_hex(0xDDDDDD), 0);
  lv_obj_set_style_bg_opa(win->title_label, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(win->title_label, 4, 0);
  lv_obj_align(win->title_label, LV_ALIGN_CENTER, 0, 0);

  // Title bar bottom border line
  lv_obj_t* title_line = lv_obj_create(win->window);
  lv_obj_set_size(title_line, w - 2, 1);
  lv_obj_set_pos(title_line, 1, 20);
  lv_obj_set_style_bg_color(title_line, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(title_line, 0, 0);

  // Content area (white, inset by scroll bar width on right)
  int content_w = w - 2 - 16;  // Leave room for vertical scrollbar
  int content_h = h - 22 - 16; // Leave room for horizontal scrollbar

  win->content = lv_obj_create(win->window);
  lv_obj_set_size(win->content, content_w, content_h);
  lv_obj_set_pos(win->content, 1, 21);
  lv_obj_set_style_bg_color(win->content, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(win->content, 0, 0);
  lv_obj_set_style_radius(win->content, 0, 0);

  // Vertical scrollbar (right side)
  win->scroll_v = mac_scrollbar_vertical(win->window, w - 17, 21, content_h);

  // Horizontal scrollbar (bottom)
  // Mirror of vertical — arrows at left and right ends
  // (abbreviated — same structure as vertical, rotated)

  return win;
}
```

---

## Mac Alert Dialog

Classic Mac alert — caution icon (⚠) or stop icon on the left, message right,
default button has 3px ring, buttons right-aligned. OK is the default button.

```cpp
// mac_dialog.cpp

void mac_show_alert(lv_obj_t* parent,
                    const char* message,
                    const char* default_btn,   // Gets 3px ring
                    const char* cancel_btn,    // NULL = single button
                    void (*default_cb)(void),
                    void (*cancel_cb)(void)) {

  // Overlay
  lv_obj_t* overlay = lv_obj_create(parent);
  lv_obj_set_size(overlay, 320, 480);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_0, 0);  // Mac dialogs — no dim overlay
  lv_obj_set_style_border_width(overlay, 0, 0);

  // Dialog box
  lv_obj_t* dlg = lv_obj_create(overlay);
  lv_obj_set_size(dlg, 280, cancel_btn ? 160 : 130);
  lv_obj_align(dlg, LV_ALIGN_CENTER, 0, -20);
  lv_obj_set_style_bg_color(dlg, lv_color_hex(0xDDDDDD), 0);
  lv_obj_set_style_border_color(dlg, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(dlg, 1, 0);
  lv_obj_set_style_radius(dlg, 8, 0);  // Mac dialogs have rounded corners
  lv_obj_set_style_shadow_color(dlg, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_width(dlg, 4, 0);
  lv_obj_set_style_shadow_ofs_x(dlg, 3, 0);
  lv_obj_set_style_shadow_ofs_y(dlg, 3, 0);
  lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

  // Caution icon (⚠ — yield-sign shape with ! inside)
  lv_obj_t* icon = lv_label_create(dlg);
  lv_label_set_text(icon, "⚠");
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
  lv_obj_set_pos(icon, 12, 16);

  // Message text
  lv_obj_t* msg = lv_label_create(dlg);
  lv_label_set_text(msg, message);
  lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
  lv_obj_set_width(msg, 220);
  lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(msg, 44, 12);

  // Buttons — right-aligned, bottom of dialog
  int btn_y = lv_obj_get_height(dlg) - 36;
  int btn_x = lv_obj_get_width(dlg) - 12;

  if (cancel_btn) {
    lv_obj_t* cbtn = lv_btn_create(dlg);
    lv_obj_set_size(cbtn, 80, 24);
    lv_obj_set_pos(cbtn, btn_x - 80, btn_y);
    lv_obj_set_style_bg_color(cbtn, lv_color_hex(0xDDDDDD), 0);
    lv_obj_set_style_radius(cbtn, 4, 0);
    mac_draw_raised(cbtn);
    lv_obj_t* cl = lv_label_create(cbtn);
    lv_label_set_text(cl, cancel_btn);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_12, 0);
    lv_obj_center(cl);
    lv_obj_add_event_cb(cbtn, [](lv_event_t* e) {
      lv_obj_del(lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target(e))));
      void (*cb)(void) = (void(*)(void))lv_event_get_user_data(e);
      if (cb) cb();
    }, LV_EVENT_CLICKED, (void*)cancel_cb);
    btn_x -= 88;
  }

  // Default button (with 3px ring)
  lv_obj_t* dbtn = lv_btn_create(dlg);
  lv_obj_set_size(dbtn, 80, 24);
  lv_obj_set_pos(dbtn, btn_x - 80, btn_y);
  lv_obj_set_style_bg_color(dbtn, lv_color_hex(0xDDDDDD), 0);
  lv_obj_set_style_radius(dbtn, 4, 0);
  mac_draw_raised(dbtn);
  mac_draw_default_ring(dbtn);  // 3px black ring = default button indicator
  lv_obj_t* dl = lv_label_create(dbtn);
  lv_label_set_text(dl, default_btn);
  lv_obj_set_style_text_font(dl, &lv_font_montserrat_12, 0);
  lv_obj_center(dl);
  lv_obj_add_event_cb(dbtn, [](lv_event_t* e) {
    lv_obj_del(lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target(e))));
    void (*cb)(void) = (void(*)(void))lv_event_get_user_data(e);
    if (cb) cb();
  }, LV_EVENT_CLICKED, (void*)default_cb);
}
```

---

## Desktop Background — Grey Checkerboard

System 7.5 default desktop is a 2×2 pixel grey checkerboard pattern.

```cpp
void mac_desktop_build(lv_obj_t* parent) {
  lv_obj_t* desktop = lv_obj_create(parent);
  lv_obj_set_size(desktop, 320, 460);  // Full height minus menu bar (20px)
  lv_obj_set_pos(desktop, 0, 20);
  lv_obj_clear_flag(desktop, LV_OBJ_FLAG_SCROLLABLE);

  // LVGL doesn't natively do checkerboard — approximate with a mid grey
  // For true checkerboard: use lv_canvas with pixel drawing (expensive on 320x460)
  // Practical approach: use 0x888888 solid (matches the visual weight of the pattern)
  lv_obj_set_style_bg_color(desktop, lv_color_hex(0x888888), 0);
  lv_obj_set_style_border_width(desktop, 0, 0);
  lv_obj_set_style_radius(desktop, 0, 0);

  // Optional true checkerboard via canvas (use only if PSRAM available):
  // lv_obj_t* canvas = lv_canvas_create(desktop);
  // lv_canvas_set_buffer(canvas, psram_buf, 320, 460, LV_IMG_CF_TRUE_COLOR);
  // for (int y = 0; y < 460; y++)
  //   for (int x = 0; x < 320; x++)
  //     lv_canvas_set_px(canvas, x, y,
  //       ((x + y) % 2 == 0) ? lv_color_hex(0x888888) : lv_color_hex(0xAAAAAA));
}
```

---

## Apple Menu

Clicking 🍎 drops down the Apple menu — About, desk accessories, control panels.
For PURR OS this maps to: About PURR OS, controlpanel.paw, notes.paw, calculator.

```cpp
static const char* apple_menu_items[] = {
  "About PURR OS...",
  "---",
  "Calculator",
  "Notes",
  "---",
  "Control Panels",
  "---",
  "Restart",
  "Shut Down"
};
static int apple_menu_count = 9;

static void apple_menu_cb(lv_event_t* e) {
  mac_menu_open(lv_scr_act(), 0, apple_menu_items, apple_menu_count,
    [](int idx) {
      switch (idx) {
        case 0: mac_show_about(); break;
        case 2: system_app_launch("/apps/calc.paw/");        break;
        case 3: system_app_launch("/apps/notes.paw/");       break;
        case 5: system_app_launch("/apps/controlpanel.paw/"); break;
        case 7: esp_restart(); break;
        case 8: system_shutdown(); break;
      }
    });
}

void mac_show_about(void) {
  mac_show_alert(lv_scr_act(),
    "PURR OS v1.0\nPortable Unified Runtime & Radio\n\nRunning on ESP32-S3",
    "OK", NULL, NULL, NULL);
}
```

---

## App Switcher (Application Menu)

Top-right of menu bar. Shows running apps — tap to switch foreground app.

```cpp
static void app_switcher_cb(lv_event_t* e) {
  // Build list of running apps from system.paw
  const char* running[4];
  int count = 0;
  // Populate from system_get_running_apps()
  // ...

  mac_menu_open(lv_scr_act(), 220, running, count,
    [](int idx) {
      // Bring app to foreground
      system_app_bring_to_front(idx);
      mac_menubar_set_app(running[idx]);
    });
}
```

---

## Theme Summary vs Win95 / CE

| Element | Mac System 7.5 | Windows 95 | Windows CE |
|---|---|---|---|
| Menu bar position | Top | None (in-window) | None |
| Taskbar | None | Bottom (grey) | Bottom (navy) |
| App switching | App menu (top right) | Taskbar buttons | Taskbar buttons |
| Window close box | Top-left small square | Top-right X button | Top-right X button |
| Title bar style | Pinstripe (active) / flat (inactive) | Flat navy | Flat navy |
| Bevel layers | 2 (+ 1px black border) | 4 layers | 2 layers |
| Dialog corners | Rounded (r=8) | Square | Square |
| Default button | 3px black ring | Bold text only | Bold text only |
| Scroll arrows | Both ends of each track | One end only | One end only |
| Desktop | Grey checkerboard | Solid teal | Solid teal |
| Dropdown menus | White bg + black border | Grey bg + bevel | Grey bg + bevel |
| Menu select color | Black bg + white text | Navy bg + white text | Navy bg + white text |

---

## File Structure

```
system/explorer_mac75.paw/
├── main.cpp
├── manifest.json
├── mac_bevel.h/.cpp            # 2-layer Platinum bevel + default ring
├── mac_menubar.h/.cpp          # Top menu bar + Apple menu + App switcher
├── mac_menu_dropdown.h/.cpp    # White dropdown with Mac selection style
├── mac_window.h/.cpp           # Finder window + pinstripe + close/zoom box
├── mac_scrollbar.h/.cpp        # Scrollbar with arrows at both ends
├── mac_dialog.h/.cpp           # Alert dialog + caution icon + default ring
├── mac_desktop.h/.cpp          # Grey checkerboard desktop
└── assets/
    ├── icons/
    │   ├── generic_doc_32.bmp   # Generic document icon
    │   ├── folder_32.bmp        # Folder icon
    │   ├── notes_32.bmp
    │   ├── calc_32.bmp
    │   ├── controlpanel_32.bmp
    │   └── purr_os_logo_32.bmp  # About dialog logo
    ├── apple_logo_14.bmp        # Apple menu logo (or use LV_SYMBOL_HOME)
    └── caution_32.bmp           # Alert caution icon
```
