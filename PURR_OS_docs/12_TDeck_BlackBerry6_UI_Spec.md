# PURR OS — BlackBerry OS 6 UI Spec (T-Deck)

## Overview

This spec recreates the BlackBerry OS 6 UI for the LilyGo T-Deck running PURR OS
on an ESP32-S3. BB OS 6 introduced five swipeable home screen panes, context-sensitive
action menus, a visual multitasking grid, and universal search — all navigable via
trackpad/trackball and keyboard. The T-Deck's 320×240 IPS display, physical QWERTY
keyboard, and trackball make it a near-perfect physical match for a BlackBerry Bold
or Torch running OS 6.

**Target hardware:**
- ESP32-S3FN16R8 — dual core 240MHz, 16MB flash, 8MB PSRAM
- 2.8" IPS LCD 320×240 (landscape)
- Physical QWERTY keyboard (ESP32-C3 sub-MCU via I2C)
- Trackball (GPIO — up/down/left/right/click)
- LoRa SX1262
- WiFi + Bluetooth 5
- Microphone + speaker

**UI framework:** LVGL 8.x, Arduino IDE

**Target size:** 1–1.5MB compiled

---

## Visual Identity — BB OS 6

BB OS 6 (launched 2010 on BlackBerry Torch 9800) has a specific look:

| Element | Description |
|---|---|
| Home screen | Dark gradient background (near black to dark grey) |
| Icons | Rounded square, glossy, 48×48px |
| Status bar | Top 20px — signal, WiFi, battery, clock, notifications |
| Home pane tabs | Five horizontal tabs at top below status bar |
| App grid | 4×3 icon grid per pane, centered |
| Selected icon | Bright highlight ring (white or color glow) |
| Action menu | Semi-transparent dark overlay, rounded list |
| Multitask grid | 3×2 thumbnail grid, dark overlay |
| Font | Clean sans-serif, white text throughout |
| Notification LED | Top-left corner indicator (simulated on screen) |

---

## Color Palette

| Element | Hex | Notes |
|---|---|---|
| Background gradient top | `0x1A1A2E` | Deep navy |
| Background gradient bottom | `0x0D0D0D` | Near black |
| Status bar bg | `0x000000` | Solid black |
| Status bar text | `0xFFFFFF` | White |
| Pane tab active | `0xFFFFFF` | White dot/line |
| Pane tab inactive | `0x555555` | Grey dot |
| Icon label | `0xFFFFFF` | White |
| Icon selected ring | `0x4FC3F7` | BB blue glow |
| Action menu bg | `0xCC000000` | 80% transparent black |
| Action menu item | `0xFFFFFF` | White text |
| Action menu selected | `0x4FC3F7` | BB blue highlight |
| Multitask bg | `0xDD000000` | 87% transparent black |
| Notification red | `0xFF0000` | Unread indicator |
| Battery full | `0x00CC00` | Green |
| Battery low | `0xFF4444` | Red |
| Signal bars | `0xFFFFFF` | White |

---

## Screen Layout (320×240, landscape)

```
┌────────────────────────────────────────────────┐  y=0
│ ●●●  WiFi  BB  LoRa          12:34  🔋 ██████  │  Status bar (20px)
├────────────────────────────────────────────────┤  y=20
│  [All] [Fav] [Media] [DL] [Freq]               │  Pane tabs (16px)
├────────────────────────────────────────────────┤  y=36
│                                                │
│  [📨]  [📞]  [🌐]  [📷]                       │
│  Msgs  Phone  Web  Camera                      │  App grid
│                                                │  (4 cols × 3 rows)
│  [🎵]  [📅]  [⚙️]  [🔍]                       │  48×48px icons
│  Music  Cal  Settings Search                   │  36px spacing
│                                                │
│  [📁]  [📻]  [💬]  [🗺️]                       │
│  Files  LoRa  Chat  Maps                       │
│                                                │  y=204
├────────────────────────────────────────────────┤
│ [Menu]  App Name / Context          [Back]     │  Soft key bar (36px)
└────────────────────────────────────────────────┘  y=240
```

Content area: 320 × 168px (between pane tabs and soft key bar)

---

## File Structure

```
system/explorer_bb6.paw/
├── main.cpp
├── manifest.json
├── bb6_homescreen.h/.cpp      # Five-pane home screen + icon grid
├── bb6_statusbar.h/.cpp       # Signal, WiFi, LoRa, battery, clock
├── bb6_pane_tabs.h/.cpp       # All/Favorites/Media/Downloads/Frequent
├── bb6_action_menu.h/.cpp     # Context-sensitive action overlay
├── bb6_multitask.h/.cpp       # Visual multitask grid (Menu key hold)
├── bb6_search.h/.cpp          # Universal search
├── bb6_trackball.h/.cpp       # Trackball input handler
├── bb6_keyboard.h/.cpp        # Physical keyboard input + shortcuts
├── bb6_notification.h/.cpp    # Notification LED simulation + toasts
└── assets/
    ├── icons/
    │   ├── messages_48.bmp
    │   ├── phone_48.bmp
    │   ├── browser_48.bmp
    │   ├── camera_48.bmp
    │   ├── music_48.bmp
    │   ├── calendar_48.bmp
    │   ├── settings_48.bmp
    │   ├── search_48.bmp
    │   ├── files_48.bmp
    │   ├── lora_48.bmp
    │   ├── chat_48.bmp
    │   └── maps_48.bmp
    └── wallpapers/
        └── bb6_dark_gradient.bmp
```

---

## Status Bar

20px tall, always visible. Black background, white icons and text.

```cpp
// bb6_statusbar.cpp

typedef struct {
  lv_obj_t* bar;
  lv_obj_t* signal_bars;
  lv_obj_t* wifi_icon;
  lv_obj_t* lora_icon;
  lv_obj_t* bt_icon;
  lv_obj_t* notif_icon;     // Envelope if unread messages
  lv_obj_t* clock_label;
  lv_obj_t* battery_bar;
  lv_obj_t* battery_label;
} bb6_statusbar_t;

static bb6_statusbar_t sb = {0};

void bb6_statusbar_build(lv_obj_t* parent) {
  sb.bar = lv_obj_create(parent);
  lv_obj_set_size(sb.bar, 320, 20);
  lv_obj_set_pos(sb.bar, 0, 0);
  lv_obj_set_style_bg_color(sb.bar, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(sb.bar, 0, 0);
  lv_obj_set_style_radius(sb.bar, 0, 0);
  lv_obj_clear_flag(sb.bar, LV_OBJ_FLAG_SCROLLABLE);

  // Signal strength bars (left side)
  // 4 vertical bars, height proportional to signal
  for (int i = 0; i < 4; i++) {
    lv_obj_t* bar = lv_obj_create(sb.bar);
    lv_obj_set_size(bar, 3, 4 + (i * 3));   // 4, 7, 10, 13px tall
    lv_obj_set_pos(bar, 2 + (i * 5), 20 - (4 + (i * 3)) - 2);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
  }

  // WiFi icon
  sb.wifi_icon = lv_label_create(sb.bar);
  lv_label_set_text(sb.wifi_icon, "W");
  lv_obj_set_style_text_font(sb.wifi_icon, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(sb.wifi_icon, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_pos(sb.wifi_icon, 26, 4);

  // LoRa icon
  sb.lora_icon = lv_label_create(sb.bar);
  lv_label_set_text(sb.lora_icon, "L");
  lv_obj_set_style_text_font(sb.lora_icon, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(sb.lora_icon, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_pos(sb.lora_icon, 40, 4);

  // Notification indicator (envelope)
  sb.notif_icon = lv_label_create(sb.bar);
  lv_label_set_text(sb.notif_icon, "");  // Hidden by default
  lv_obj_set_style_text_color(sb.notif_icon, lv_color_hex(0xFF0000), 0);
  lv_obj_set_pos(sb.notif_icon, 56, 4);

  // Clock (center)
  sb.clock_label = lv_label_create(sb.bar);
  lv_label_set_text(sb.clock_label, "12:34");
  lv_obj_set_style_text_font(sb.clock_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(sb.clock_label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(sb.clock_label, LV_ALIGN_CENTER, 0, 0);

  // Battery bar (right side)
  sb.battery_bar = lv_bar_create(sb.bar);
  lv_obj_set_size(sb.battery_bar, 24, 10);
  lv_obj_align(sb.battery_bar, LV_ALIGN_RIGHT_MID, -28, 0);
  lv_bar_set_range(sb.battery_bar, 0, 100);
  lv_bar_set_value(sb.battery_bar, 80, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(sb.battery_bar, lv_color_hex(0x00CC00),
                             LV_PART_INDICATOR);

  // Battery percentage
  sb.battery_label = lv_label_create(sb.bar);
  lv_label_set_text(sb.battery_label, "80%");
  lv_obj_set_style_text_font(sb.battery_label, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(sb.battery_label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(sb.battery_label, LV_ALIGN_RIGHT_MID, -2, 0);
}

void bb6_statusbar_update(const kitt_tray_state_t* state) {
  // Update clock
  lv_label_set_text(sb.clock_label, state->time_str);

  // Update battery color
  lv_color_t bat_color = (state->battery_percent < 20) ?
    lv_color_hex(0xFF4444) : lv_color_hex(0x00CC00);
  lv_obj_set_style_bg_color(sb.battery_bar, bat_color, LV_PART_INDICATOR);
  lv_bar_set_value(sb.battery_bar, state->battery_percent, LV_ANIM_ON);

  // Update WiFi color
  lv_color_t wifi_color = state->wifi_connected ?
    lv_color_hex(0xFFFFFF) : lv_color_hex(0x444444);
  lv_obj_set_style_text_color(sb.wifi_icon, wifi_color, 0);

  // Update LoRa color
  lv_color_t lora_color = state->lora_enabled ?
    lv_color_hex(0xFFFFFF) : lv_color_hex(0x444444);
  lv_obj_set_style_text_color(sb.lora_icon, lora_color, 0);
}
```

---

## Pane Tabs

Five swipeable panes matching BB OS 6 exactly:
`All | Favorites | Media | Downloads | Frequent`

```cpp
// bb6_pane_tabs.cpp

static const char* PANE_NAMES[] = { "All", "Fav", "Media", "DL", "Freq" };
static int active_pane = 0;  // Default: All

static lv_obj_t* tab_dots[5];
static lv_obj_t* pane_label;

void bb6_pane_tabs_build(lv_obj_t* parent) {
  lv_obj_t* tab_bar = lv_obj_create(parent);
  lv_obj_set_size(tab_bar, 320, 16);
  lv_obj_set_pos(tab_bar, 0, 20);  // Below status bar
  lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_width(tab_bar, 0, 0);
  lv_obj_clear_flag(tab_bar, LV_OBJ_FLAG_SCROLLABLE);

  // Five dot indicators + labels
  int spacing = 320 / 5;
  for (int i = 0; i < 5; i++) {
    // Dot
    lv_obj_t* dot = lv_obj_create(tab_bar);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_pos(dot, (i * spacing) + (spacing / 2) - 3, 2);
    lv_obj_set_style_radius(dot, 3, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_color(dot,
      i == active_pane ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x444444), 0);
    tab_dots[i] = dot;

    // Label
    lv_obj_t* lbl = lv_label_create(tab_bar);
    lv_label_set_text(lbl, PANE_NAMES[i]);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl,
      i == active_pane ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x555555), 0);
    lv_obj_set_pos(lbl, (i * spacing) + (spacing / 2) - 10, 8);
  }
}

void bb6_pane_switch(int pane_index) {
  // Update dot indicators
  for (int i = 0; i < 5; i++) {
    lv_obj_set_style_bg_color(tab_dots[i],
      i == pane_index ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x444444), 0);
  }
  active_pane = pane_index;
  bb6_homescreen_load_pane(pane_index);
}
```

---

## Home Screen Icon Grid

4 columns × 3 rows = 12 icons per pane. 48×48px icons with label below.
Trackball navigates between icons. Selected icon gets BB blue glow ring.

```cpp
// bb6_homescreen.cpp

#define ICON_SIZE    48
#define ICON_COLS    4
#define ICON_ROWS    3
#define GRID_START_X 16
#define GRID_START_Y 36    // Below status bar + pane tabs
#define ICON_SPACING_X 76  // (320 - 16*2 - 48*4) / 3 gaps = ~76
#define ICON_SPACING_Y 52  // (168 - 48*3) / 2 gaps = ~36 + label space

typedef struct {
  char name[32];
  char icon_path[64];
  char app_path[128];
} bb6_icon_t;

static bb6_icon_t pane_icons[5][12];  // 5 panes, 12 icons each
static int selected_icon = 0;          // 0-11
static lv_obj_t* icon_containers[12];

void bb6_homescreen_build(lv_obj_t* parent) {
  // Dark gradient background
  lv_obj_t* bg = lv_obj_create(parent);
  lv_obj_set_size(bg, 320, 240);
  lv_obj_set_pos(bg, 0, 0);
  lv_obj_set_style_bg_color(bg, lv_color_hex(0x1A1A2E), 0);
  lv_obj_set_style_bg_grad_color(bg, lv_color_hex(0x0D0D0D), 0);
  lv_obj_set_style_bg_grad_dir(bg, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(bg, 0, 0);
  lv_obj_set_style_radius(bg, 0, 0);
  lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

  bb6_statusbar_build(bg);
  bb6_pane_tabs_build(bg);
  bb6_softkey_bar_build(bg);

  // Build icon grid
  for (int i = 0; i < 12; i++) {
    int col = i % ICON_COLS;
    int row = i / ICON_COLS;
    int x = GRID_START_X + col * ICON_SPACING_X;
    int y = GRID_START_Y + row * ICON_SPACING_Y;

    icon_containers[i] = lv_obj_create(bg);
    lv_obj_set_size(icon_containers[i], ICON_SIZE + 8, ICON_SIZE + 16);
    lv_obj_set_pos(icon_containers[i], x - 4, y - 4);
    lv_obj_set_style_bg_opa(icon_containers[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_containers[i], 0, 0);
    lv_obj_set_style_radius(icon_containers[i], 8, 0);

    // Icon image
    lv_obj_t* img = lv_img_create(icon_containers[i]);
    lv_obj_set_pos(img, 4, 0);
    // lv_img_set_src(img, pane_icons[0][i].icon_path);

    // Icon label
    lv_obj_t* lbl = lv_label_create(icon_containers[i]);
    lv_label_set_text(lbl, pane_icons[0][i].name);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(lbl, ICON_SIZE + 8);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, 0);
  }

  bb6_homescreen_set_selected(0);
}

void bb6_homescreen_set_selected(int index) {
  // Clear previous selection
  lv_obj_set_style_bg_opa(icon_containers[selected_icon], LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(icon_containers[selected_icon], 0, 0);

  selected_icon = index;

  // Apply BB blue glow ring to selected
  lv_obj_set_style_bg_color(icon_containers[selected_icon],
    lv_color_hex(0x4FC3F7), 0);
  lv_obj_set_style_bg_opa(icon_containers[selected_icon], LV_OPA_20, 0);
  lv_obj_set_style_border_color(icon_containers[selected_icon],
    lv_color_hex(0x4FC3F7), 0);
  lv_obj_set_style_border_width(icon_containers[selected_icon], 2, 0);
}

void bb6_homescreen_load_pane(int pane) {
  // Reload icon grid for the selected pane
  for (int i = 0; i < 12; i++) {
    lv_obj_t* lbl = lv_obj_get_child(icon_containers[i], 1);
    if (lbl) lv_label_set_text(lbl, pane_icons[pane][i].name);
  }
}
```

---

## Trackball Input

The T-Deck trackball connects to 4 GPIO pins (up/down/left/right) plus a click.
Maps to icon navigation and menu scrolling.

```cpp
// bb6_trackball.cpp

// T-Deck trackball GPIO pins
#define TB_UP    3
#define TB_DOWN  15
#define TB_LEFT  1
#define TB_RIGHT 2
#define TB_CLICK 0

typedef enum {
  TB_NONE, TB_UP_E, TB_DOWN_E, TB_LEFT_E, TB_RIGHT_E, TB_CLICK_E
} tb_event_t;

static tb_event_t last_tb_event = TB_NONE;

void bb6_trackball_init(void) {
  pinMode(TB_UP,    INPUT_PULLUP);
  pinMode(TB_DOWN,  INPUT_PULLUP);
  pinMode(TB_LEFT,  INPUT_PULLUP);
  pinMode(TB_RIGHT, INPUT_PULLUP);
  pinMode(TB_CLICK, INPUT_PULLUP);
}

tb_event_t bb6_trackball_read(void) {
  if (!digitalRead(TB_UP))    return TB_UP_E;
  if (!digitalRead(TB_DOWN))  return TB_DOWN_E;
  if (!digitalRead(TB_LEFT))  return TB_LEFT_E;
  if (!digitalRead(TB_RIGHT)) return TB_RIGHT_E;
  if (!digitalRead(TB_CLICK)) return TB_CLICK_E;
  return TB_NONE;
}

// Navigate icon grid with trackball
void bb6_trackball_navigate(tb_event_t ev) {
  int idx = selected_icon;

  switch (ev) {
    case TB_RIGHT_E: if (idx % ICON_COLS < ICON_COLS - 1) idx++; break;
    case TB_LEFT_E:  if (idx % ICON_COLS > 0) idx--;              break;
    case TB_DOWN_E:  if (idx + ICON_COLS < 12) idx += ICON_COLS;  break;
    case TB_UP_E:    if (idx - ICON_COLS >= 0) idx -= ICON_COLS;  break;
    case TB_CLICK_E: bb6_launch_selected(); return;
    default: return;
  }

  bb6_homescreen_set_selected(idx);
}
```

---

## Action Menu

BB OS 6 action menu — hold trackball click or press Menu key.
Semi-transparent dark overlay with rounded list of context actions.

```cpp
// bb6_action_menu.cpp

static lv_obj_t* menu_overlay = NULL;
static bool menu_open = false;

typedef struct {
  char label[32];
  void (*action)(void);
} bb6_menu_item_t;

void bb6_action_menu_open(lv_obj_t* parent,
                           const bb6_menu_item_t* items,
                           int count) {
  if (menu_open) { bb6_action_menu_close(); return; }
  menu_open = true;

  // Full screen dim overlay
  menu_overlay = lv_obj_create(parent);
  lv_obj_set_size(menu_overlay, 320, 240);
  lv_obj_set_pos(menu_overlay, 0, 0);
  lv_obj_set_style_bg_color(menu_overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(menu_overlay, LV_OPA_80, 0);
  lv_obj_set_style_border_width(menu_overlay, 0, 0);
  lv_obj_set_style_radius(menu_overlay, 0, 0);

  // Menu panel — centered, rounded
  int menu_h = (count * 36) + 16;
  lv_obj_t* panel = lv_obj_create(menu_overlay);
  lv_obj_set_size(panel, 200, menu_h);
  lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x111111), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_radius(panel, 12, 0);
  lv_obj_set_style_pad_all(panel, 8, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < count; i++) {
    lv_obj_t* item = lv_obj_create(panel);
    lv_obj_set_size(item, 184, 32);
    lv_obj_set_pos(item, 0, i * 36);
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_style_radius(item, 8, 0);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* lbl = lv_label_create(item);
    lv_label_set_text(lbl, items[i].label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);

    // Hover: BB blue highlight
    lv_obj_add_event_cb(item, [](lv_event_t* e) {
      lv_obj_t* obj = lv_event_get_target(e);
      lv_event_code_t code = lv_event_get_code(e);
      lv_obj_t* lbl = lv_obj_get_child(obj, 0);
      if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x4FC3F7), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_30, 0);
      } else if (code == LV_EVENT_RELEASED) {
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
        // Call action
        void (*action)(void) = (void(*)(void))lv_event_get_user_data(e);
        bb6_action_menu_close();
        if (action) action();
      }
    }, LV_EVENT_ALL, (void*)items[i].action);

    // Separator line between items
    if (i < count - 1) {
      lv_obj_t* sep = lv_obj_create(panel);
      lv_obj_set_size(sep, 184, 1);
      lv_obj_set_pos(sep, 0, (i + 1) * 36 - 2);
      lv_obj_set_style_bg_color(sep, lv_color_hex(0x333333), 0);
      lv_obj_set_style_border_width(sep, 0, 0);
    }
  }
}

void bb6_action_menu_close(void) {
  if (menu_overlay) {
    lv_obj_del(menu_overlay);
    menu_overlay = NULL;
  }
  menu_open = false;
}
```

---

## Multitask Grid

Hold Menu key to bring up BB OS 6 style visual multitask grid.
3×2 grid of running app thumbnails (or placeholder tiles).

```cpp
// bb6_multitask.cpp

static lv_obj_t* mt_overlay = NULL;

void bb6_multitask_open(lv_obj_t* parent) {
  mt_overlay = lv_obj_create(parent);
  lv_obj_set_size(mt_overlay, 320, 240);
  lv_obj_set_pos(mt_overlay, 0, 0);
  lv_obj_set_style_bg_color(mt_overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(mt_overlay, LV_OPA_90, 0);
  lv_obj_set_style_border_width(mt_overlay, 0, 0);

  // Title
  lv_obj_t* title = lv_label_create(mt_overlay);
  lv_label_set_text(title, "Running Apps");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  // 3×2 app thumbnail grid
  int app_count = system_running_app_count();
  for (int i = 0; i < 6; i++) {
    int col = i % 3;
    int row = i / 3;
    int x = 16 + col * 96;
    int y = 30 + row * 90;

    lv_obj_t* thumb = lv_obj_create(mt_overlay);
    lv_obj_set_size(thumb, 80, 75);
    lv_obj_set_pos(thumb, x, y);
    lv_obj_set_style_radius(thumb, 8, 0);
    lv_obj_set_style_border_width(thumb, 1, 0);

    if (i < app_count) {
      // Active app tile — dark blue tint
      lv_obj_set_style_bg_color(thumb, lv_color_hex(0x1A2A3A), 0);
      lv_obj_set_style_border_color(thumb, lv_color_hex(0x4FC3F7), 0);

      char app_name[32];
      system_get_running_app_name(i, app_name, sizeof(app_name));

      lv_obj_t* lbl = lv_label_create(thumb);
      lv_label_set_text(lbl, app_name);
      lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
      lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

      lv_obj_add_event_cb(thumb, [](lv_event_t* e) {
        int idx = (int)(intptr_t)lv_event_get_user_data(e);
        bb6_multitask_close();
        system_app_bring_to_front(idx);
      }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    } else {
      // Empty slot
      lv_obj_set_style_bg_color(thumb, lv_color_hex(0x111111), 0);
      lv_obj_set_style_border_color(thumb, lv_color_hex(0x222222), 0);
    }
  }

  // Close hint
  lv_obj_t* hint = lv_label_create(mt_overlay);
  lv_label_set_text(hint, "Hold Menu to close");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
}

void bb6_multitask_close(void) {
  if (mt_overlay) {
    lv_obj_del(mt_overlay);
    mt_overlay = NULL;
  }
}
```

---

## Soft Key Bar

36px bar at bottom. Left = Menu (context), Right = Back/Escape.
Center shows current app name or home screen label.

```cpp
void bb6_softkey_bar_build(lv_obj_t* parent) {
  lv_obj_t* bar = lv_obj_create(parent);
  lv_obj_set_size(bar, 320, 36);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_80, 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(0x222222), 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  // Menu button (left)
  lv_obj_t* menu_btn = lv_btn_create(bar);
  lv_obj_set_size(menu_btn, 60, 28);
  lv_obj_align(menu_btn, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_set_style_bg_color(menu_btn, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(menu_btn, 4, 0);
  lv_obj_t* ml = lv_label_create(menu_btn);
  lv_label_set_text(ml, "Menu");
  lv_obj_set_style_text_font(ml, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(ml, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(ml);
  lv_obj_add_event_cb(menu_btn, [](lv_event_t* e) {
    // Open action menu for current context
    bb6_open_context_menu();
  }, LV_EVENT_CLICKED, NULL);

  // Context label (center)
  lv_obj_t* ctx = lv_label_create(bar);
  lv_label_set_text(ctx, "Home");
  lv_obj_set_style_text_font(ctx, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(ctx, lv_color_hex(0xAAAAAA), 0);
  lv_obj_align(ctx, LV_ALIGN_CENTER, 0, 0);

  // Back button (right)
  lv_obj_t* back_btn = lv_btn_create(bar);
  lv_obj_set_size(back_btn, 60, 28);
  lv_obj_align(back_btn, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(back_btn, 4, 0);
  lv_obj_t* bl = lv_label_create(back_btn);
  lv_label_set_text(bl, "Back");
  lv_obj_set_style_text_font(bl, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(bl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(bl);
  lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
    bb6_navigate_back();
  }, LV_EVENT_CLICKED, NULL);
}
```

---

## Keyboard Shortcuts

BB OS 6 keyboard shortcuts from the home screen — single key press opens app.

```cpp
// bb6_keyboard.cpp

// T-Deck keyboard is managed by ESP32-C3, read over I2C
// Key events arrive as characters

void bb6_keyboard_handle(char key) {
  if (bb6_multitask_visible()) {
    // In multitask grid: number keys switch apps
    if (key >= '1' && key <= '6') {
      system_app_bring_to_front(key - '1');
      bb6_multitask_close();
    }
    return;
  }

  switch (key) {
    case 'M': bb6_open_context_menu();        break;  // Menu key
    case 'B': bb6_navigate_back();             break;  // Back
    case 'S': bb6_search_open();               break;  // Universal search
    case 'N': bb6_navigate_pane(active_pane + 1); break; // Next pane
    case 'P': bb6_navigate_pane(active_pane - 1); break; // Prev pane
    case '\n': bb6_launch_selected();          break;  // Enter = launch
    // BlackBerry convenience key shortcuts
    case 'L': system_app_launch("/apps/lora.paw/");  break;
    case 'C': system_app_launch("/apps/chat.paw/");  break;
    case 'E': system_app_launch("/apps/msn.paw/");   break;
  }
}
```

---

## Universal Search

BB OS 6's universal search bar — slides down from top, searches apps, files, LoRa contacts.

```cpp
void bb6_search_open(lv_obj_t* parent) {
  lv_obj_t* search_bar = lv_obj_create(parent);
  lv_obj_set_size(search_bar, 320, 40);
  lv_obj_set_pos(search_bar, 0, 20);  // Slides down under status bar
  lv_obj_set_style_bg_color(search_bar, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(search_bar, lv_color_hex(0x4FC3F7), 0);
  lv_obj_set_style_border_side(search_bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(search_bar, 2, 0);

  // Search icon
  lv_obj_t* icon = lv_label_create(search_bar);
  lv_label_set_text(icon, LV_SYMBOL_SEARCH);
  lv_obj_set_style_text_color(icon, lv_color_hex(0x4FC3F7), 0);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, 8, 0);

  // Text area
  lv_obj_t* ta = lv_textarea_create(search_bar);
  lv_obj_set_size(ta, 260, 30);
  lv_obj_align(ta, LV_ALIGN_LEFT_MID, 30, 0);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, "Search apps, files, contacts...");
  lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ta, 0, 0);
  lv_obj_set_style_text_color(ta, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(ta, &lv_font_montserrat_12, 0);
  // Physical keyboard input feeds into this text area
}
```

---

## Summary — T-Deck BB OS 6 UI vs Physical Hardware

| BB OS 6 Feature | T-Deck Hardware | Implementation |
|---|---|---|
| Home screen panes (swipe) | Trackball left/right | Pane tab switch |
| Icon navigation | Trackball 4-way | Grid cursor movement |
| Launch app | Trackball click or Enter | app_launch() |
| Action menu | Menu soft key | bb6_action_menu_open() |
| Multitask grid | Hold Menu | bb6_multitask_open() |
| Universal search | S key shortcut | bb6_search_open() |
| Status bar | Always visible | Signal, WiFi, LoRa, battery, clock |
| Notification LED | Screen corner indicator | Red dot in status bar |
| Back navigation | Back soft key | bb6_navigate_back() |
| Keyboard shortcuts | Physical QWERTY | Single key → app launch |
| LoRa messaging | Native LoRa SX1262 | Chat.paw via SX1262 |
| WiFi | Native ESP32-S3 | Network agent |
