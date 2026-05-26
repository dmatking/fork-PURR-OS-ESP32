# controlpanel.meow — Detailed Specification

## Overview

controlpanel.meow is a lightweight overlay app — it floats on top of explorer.meow and can run alongside other lightweight apps (notes, calc, msn). It is the settings frontend for PURR OS. It owns no hardware state — all changes go through KITT APIs. It is blocked from launching during fullscreen firmware exclusivity (Meshtastic, Bruce) unless the device has enough free RAM to permit overlays.

**Target size:** 250–350KB compiled

---

## File Structure

```
apps/controlpanel.meow/
├── main.cpp               # Entry point — controlpanel_create(), _update(), _destroy()
├── manifest.json          # App metadata
├── kitt_api.h             # Extern declarations for KITT API calls
├── system_api.h           # Extern declarations for system.meow calls
│
├── panels/
│   ├── wifi_panel.h/.cpp
│   ├── bt_panel.h/.cpp
│   ├── lora_panel.h/.cpp
│   ├── battery_panel.h/.cpp
│   └── handoff_panel.h/.cpp
│
└── assets/
    └── icon.bmp
```

### manifest.json

```json
{
  "name": "Control Panel",
  "version": "1.0.0",
  "author": "PURR OS",
  "is_lightweight": true,
  "needs_wifi": false,
  "needs_bt": false,
  "needs_lora": false,
  "min_ram_kb": 64,
  "icon": "assets/icon.bmp"
}
```

---

## main.cpp — Entry Point

```cpp
#include <Arduino.h>
#include <lvgl.h>
#include "kitt_api.h"
#include "system_api.h"
#include "panels/wifi_panel.h"
#include "panels/bt_panel.h"
#include "panels/lora_panel.h"
#include "panels/battery_panel.h"
#include "panels/handoff_panel.h"

// ─── Context ──────────────────────────────────────────────────────────────────

typedef struct {
  lv_obj_t* screen;       // Root screen object
  lv_obj_t* titlebar;     // Top title bar
  lv_obj_t* close_btn;    // X button in titlebar
  lv_obj_t* tabview;      // Tab container
  lv_timer_t* poll_timer; // 200ms update timer

  // Tab references
  lv_obj_t* tab_wifi;
  lv_obj_t* tab_bt;
  lv_obj_t* tab_lora;
  lv_obj_t* tab_battery;
  lv_obj_t* tab_handoff;
} controlpanel_ctx_t;

static controlpanel_ctx_t ctx = {0};

// ─── Forward declarations ─────────────────────────────────────────────────────

static void build_titlebar(void);
static void build_tabs(void);
static void poll_timer_cb(lv_timer_t* t);
static void close_btn_cb(lv_event_t* e);

// ─── Lifecycle ────────────────────────────────────────────────────────────────

// Called by system.meow when user opens Control Panel
void controlpanel_create(lv_obj_t* parent) {
  ctx.screen = lv_obj_create(parent);
  lv_obj_set_size(ctx.screen, 320, 480);
  lv_obj_set_pos(ctx.screen, 0, 0);
  lv_obj_set_style_bg_color(ctx.screen, lv_color_hex(0xC0C0C0), 0); // Classic grey

  build_titlebar();
  build_tabs();

  // Start 200ms polling timer
  ctx.poll_timer = lv_timer_create(poll_timer_cb, 200, NULL);
}

// Called by system.meow on close or app switch
void controlpanel_destroy(void) {
  if (ctx.poll_timer) {
    lv_timer_del(ctx.poll_timer);
    ctx.poll_timer = NULL;
  }
  if (ctx.screen) {
    lv_obj_del(ctx.screen);
    ctx.screen = NULL;
  }
  memset(&ctx, 0, sizeof(ctx));
}

// ─── Titlebar ─────────────────────────────────────────────────────────────────

static void build_titlebar(void) {
  ctx.titlebar = lv_obj_create(ctx.screen);
  lv_obj_set_size(ctx.titlebar, 320, 32);
  lv_obj_set_pos(ctx.titlebar, 0, 0);
  lv_obj_set_style_bg_color(ctx.titlebar, lv_color_hex(0x000080), 0); // Windows CE navy
  lv_obj_set_style_border_width(ctx.titlebar, 0, 0);
  lv_obj_clear_flag(ctx.titlebar, LV_OBJ_FLAG_SCROLLABLE);

  // Title label
  lv_obj_t* title = lv_label_create(ctx.titlebar);
  lv_label_set_text(title, "Control Panel");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

  // Close button
  ctx.close_btn = lv_btn_create(ctx.titlebar);
  lv_obj_set_size(ctx.close_btn, 24, 20);
  lv_obj_align(ctx.close_btn, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_bg_color(ctx.close_btn, lv_color_hex(0xC0C0C0), 0);

  lv_obj_t* x_label = lv_label_create(ctx.close_btn);
  lv_label_set_text(x_label, "X");
  lv_obj_center(x_label);

  lv_obj_add_event_cb(ctx.close_btn, close_btn_cb, LV_EVENT_CLICKED, NULL);
}

static void close_btn_cb(lv_event_t* e) {
  system_app_close("controlpanel"); // Tells system.meow to call controlpanel_destroy()
}

// ─── Tabs ─────────────────────────────────────────────────────────────────────

static void build_tabs(void) {
  ctx.tabview = lv_tabview_create(ctx.screen, LV_DIR_TOP, 40);
  lv_obj_set_pos(ctx.tabview, 0, 32);
  lv_obj_set_size(ctx.tabview, 320, 448);  // 480 - 32px titlebar
  lv_obj_set_style_bg_color(ctx.tabview, lv_color_hex(0xC0C0C0), 0);

  ctx.tab_wifi    = lv_tabview_add_tab(ctx.tabview, "WiFi");
  ctx.tab_bt      = lv_tabview_add_tab(ctx.tabview, "BT");
  ctx.tab_lora    = lv_tabview_add_tab(ctx.tabview, "LoRa");
  ctx.tab_battery = lv_tabview_add_tab(ctx.tabview, "Power");
  ctx.tab_handoff = lv_tabview_add_tab(ctx.tabview, "Handoff");

  wifi_panel_build(ctx.tab_wifi);
  bt_panel_build(ctx.tab_bt);
  lora_panel_build(ctx.tab_lora);
  battery_panel_build(ctx.tab_battery);
  handoff_panel_build(ctx.tab_handoff);
}

// ─── Poll Timer ───────────────────────────────────────────────────────────────

static void poll_timer_cb(lv_timer_t* t) {
  // Determine which tab is active and only update that one
  uint16_t active = lv_tabview_get_tab_act(ctx.tabview);
  switch (active) {
    case 0: wifi_panel_update();    break;
    case 1: bt_panel_update();      break;
    case 2: lora_panel_update();    break;
    case 3: battery_panel_update(); break;
    case 4: handoff_panel_update(); break;
  }
}
```

---

## Panel 1: wifi_panel.cpp

```cpp
#include <lvgl.h>
#include "kitt_api.h"

static lv_obj_t* sw_wifi;
static lv_obj_t* dd_networks;
static lv_obj_t* ta_password;
static lv_obj_t* btn_scan;
static lv_obj_t* btn_connect;
static lv_obj_t* btn_forget;
static lv_obj_t* lbl_status;
static lv_obj_t* bar_signal;

static void wifi_toggle_cb(lv_event_t* e) {
  if (lv_obj_has_state(sw_wifi, LV_STATE_CHECKED)) {
    kitt_wifi_enable();
  } else {
    kitt_wifi_disconnect();
    kitt_wifi_disable();
    lv_label_set_text(lbl_status, "WiFi Off");
  }
}

static void scan_cb(lv_event_t* e) {
  lv_label_set_text(lbl_status, "Scanning...");
  lv_obj_add_state(btn_scan, LV_STATE_DISABLED);

  kitt_wifi_scan_start();

  // Poll until done — handled in wifi_panel_update()
}

static void connect_cb(lv_event_t* e) {
  char ssid[64] = {0};
  lv_dropdown_get_selected_str(dd_networks, ssid, sizeof(ssid));
  const char* pwd = lv_textarea_get_text(ta_password);

  kitt_wifi_connect(ssid, pwd);
  lv_label_set_text(lbl_status, "Connecting...");
  lv_obj_add_state(btn_connect, LV_STATE_DISABLED);
}

static void forget_cb(lv_event_t* e) {
  char ssid[64] = {0};
  lv_dropdown_get_selected_str(dd_networks, ssid, sizeof(ssid));
  kitt_wifi_forget(ssid);
  lv_label_set_text(lbl_status, "Forgotten.");
}

void wifi_panel_build(lv_obj_t* parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 8, 0);
  lv_obj_set_style_pad_all(parent, 8, 0);

  // WiFi toggle row
  lv_obj_t* row1 = lv_obj_create(parent);
  lv_obj_set_size(row1, 300, 40);
  lv_obj_set_style_border_width(row1, 0, 0);
  lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);

  lv_obj_t* lbl_wifi = lv_label_create(row1);
  lv_label_set_text(lbl_wifi, "WiFi");
  lv_obj_align(lbl_wifi, LV_ALIGN_LEFT_MID, 0, 0);

  sw_wifi = lv_switch_create(row1);
  lv_obj_align(sw_wifi, LV_ALIGN_RIGHT_MID, 0, 0);
  if (kitt_wifi_enabled()) lv_obj_add_state(sw_wifi, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw_wifi, wifi_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // Network dropdown
  dd_networks = lv_dropdown_create(parent);
  lv_obj_set_width(dd_networks, 300);
  lv_dropdown_set_options(dd_networks, "Press Scan to search...");

  // Scan button
  btn_scan = lv_btn_create(parent);
  lv_obj_set_size(btn_scan, 300, 36);
  lv_obj_t* lbl_scan = lv_label_create(btn_scan);
  lv_label_set_text(lbl_scan, "Scan");
  lv_obj_center(lbl_scan);
  lv_obj_add_event_cb(btn_scan, scan_cb, LV_EVENT_CLICKED, NULL);

  // Password field
  lv_obj_t* lbl_pwd = lv_label_create(parent);
  lv_label_set_text(lbl_pwd, "Password:");

  ta_password = lv_textarea_create(parent);
  lv_obj_set_width(ta_password, 300);
  lv_textarea_set_password_mode(ta_password, true);
  lv_textarea_set_max_length(ta_password, 64);
  lv_textarea_set_one_line(ta_password, true);
  lv_textarea_set_placeholder_text(ta_password, "Enter password...");

  // Connect / Forget buttons
  lv_obj_t* btn_row = lv_obj_create(parent);
  lv_obj_set_size(btn_row, 300, 40);
  lv_obj_set_style_border_width(btn_row, 0, 0);
  lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(btn_row, 8, 0);

  btn_connect = lv_btn_create(btn_row);
  lv_obj_set_size(btn_connect, 140, 36);
  lv_obj_t* lbl_conn = lv_label_create(btn_connect);
  lv_label_set_text(lbl_conn, "Connect");
  lv_obj_center(lbl_conn);
  lv_obj_add_event_cb(btn_connect, connect_cb, LV_EVENT_CLICKED, NULL);

  btn_forget = lv_btn_create(btn_row);
  lv_obj_set_size(btn_forget, 140, 36);
  lv_obj_t* lbl_fgt = lv_label_create(btn_forget);
  lv_label_set_text(lbl_fgt, "Forget");
  lv_obj_center(lbl_fgt);
  lv_obj_add_event_cb(btn_forget, forget_cb, LV_EVENT_CLICKED, NULL);

  // Signal bar
  bar_signal = lv_bar_create(parent);
  lv_obj_set_size(bar_signal, 300, 12);
  lv_bar_set_range(bar_signal, -90, -30);  // dBm range

  // Status label
  lbl_status = lv_label_create(parent);
  lv_label_set_text(lbl_status, kitt_wifi_connected() ? "Connected" : "Not connected");
}

void wifi_panel_update(void) {
  // Scan done?
  if (kitt_wifi_scan_done()) {
    int count = kitt_wifi_scan_count();
    if (count == 0) {
      lv_dropdown_set_options(dd_networks, "No networks found");
    } else {
      char options[512] = {0};
      for (int i = 0; i < count && i < 10; i++) {
        char ssid[64] = {0};
        kitt_wifi_scan_get_ssid(i, ssid, sizeof(ssid));
        strncat(options, ssid, sizeof(options) - strlen(options) - 2);
        if (i < count - 1) strncat(options, "\n", sizeof(options) - strlen(options) - 1);
      }
      lv_dropdown_set_options(dd_networks, options);
    }
    lv_obj_clear_state(btn_scan, LV_STATE_DISABLED);
    lv_label_set_text(lbl_status, "Scan complete");
  }

  // Connection status
  if (kitt_wifi_connected()) {
    char ssid[64] = {0};
    kitt_wifi_get_connected_ssid(ssid, sizeof(ssid));
    char buf[96];
    snprintf(buf, sizeof(buf), "Connected: %s", ssid);
    lv_label_set_text(lbl_status, buf);
    lv_obj_clear_state(btn_connect, LV_STATE_DISABLED);

    int rssi = kitt_wifi_signal_strength();
    lv_bar_set_value(bar_signal, rssi, LV_ANIM_ON);
  }
}
```

---

## Panel 2: bt_panel.cpp

```cpp
#include <lvgl.h>
#include "kitt_api.h"

static lv_obj_t* sw_bt;
static lv_obj_t* dd_paired;
static lv_obj_t* btn_discover;
static lv_obj_t* btn_pair;
static lv_obj_t* btn_unpair;
static lv_obj_t* lbl_status;
static lv_obj_t* lbl_countdown;
static bool discovery_active = false;
static uint32_t discovery_start_ms = 0;
static const uint32_t DISCOVERY_TIMEOUT_MS = 30000;

static void bt_toggle_cb(lv_event_t* e) {
  if (lv_obj_has_state(sw_bt, LV_STATE_CHECKED)) {
    kitt_bt_enable();
    lv_label_set_text(lbl_status, "Bluetooth On");
  } else {
    kitt_bt_disable();
    lv_label_set_text(lbl_status, "Bluetooth Off");
  }
}

static void discover_cb(lv_event_t* e) {
  kitt_bt_start_discovery(DISCOVERY_TIMEOUT_MS);
  discovery_active = true;
  discovery_start_ms = millis();
  lv_obj_add_state(btn_discover, LV_STATE_DISABLED);
  lv_label_set_text(lbl_status, "Discovering...");
}

static void pair_cb(lv_event_t* e) {
  int idx = lv_dropdown_get_selected(dd_paired);
  kitt_bt_pair(idx);
  lv_label_set_text(lbl_status, "Pairing...");
}

static void unpair_cb(lv_event_t* e) {
  int idx = lv_dropdown_get_selected(dd_paired);
  kitt_bt_unpair(idx);
  refresh_paired_list();
  lv_label_set_text(lbl_status, "Unpaired.");
}

static void refresh_paired_list(void) {
  int count = kitt_bt_paired_count();
  if (count == 0) {
    lv_dropdown_set_options(dd_paired, "No paired devices");
    return;
  }
  char options[512] = {0};
  for (int i = 0; i < count && i < 10; i++) {
    char name[64] = {0};
    kitt_bt_get_paired_name(i, name, sizeof(name));
    strncat(options, name, sizeof(options) - strlen(options) - 2);
    if (i < count - 1) strncat(options, "\n", sizeof(options) - strlen(options) - 1);
  }
  lv_dropdown_set_options(dd_paired, options);
}

void bt_panel_build(lv_obj_t* parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 8, 0);
  lv_obj_set_style_pad_all(parent, 8, 0);

  // BT toggle row
  lv_obj_t* row1 = lv_obj_create(parent);
  lv_obj_set_size(row1, 300, 40);
  lv_obj_set_style_border_width(row1, 0, 0);
  lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);

  lv_obj_t* lbl_bt = lv_label_create(row1);
  lv_label_set_text(lbl_bt, "Bluetooth");
  lv_obj_align(lbl_bt, LV_ALIGN_LEFT_MID, 0, 0);

  sw_bt = lv_switch_create(row1);
  lv_obj_align(sw_bt, LV_ALIGN_RIGHT_MID, 0, 0);
  if (kitt_bt_enabled()) lv_obj_add_state(sw_bt, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw_bt, bt_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // Paired devices
  lv_obj_t* lbl_paired = lv_label_create(parent);
  lv_label_set_text(lbl_paired, "Paired Devices:");

  dd_paired = lv_dropdown_create(parent);
  lv_obj_set_width(dd_paired, 300);
  refresh_paired_list();

  // Buttons row
  lv_obj_t* btn_row = lv_obj_create(parent);
  lv_obj_set_size(btn_row, 300, 40);
  lv_obj_set_style_border_width(btn_row, 0, 0);
  lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(btn_row, 6, 0);

  btn_discover = lv_btn_create(btn_row);
  lv_obj_set_size(btn_discover, 90, 36);
  lv_obj_t* ld = lv_label_create(btn_discover);
  lv_label_set_text(ld, "Discover");
  lv_obj_center(ld);
  lv_obj_add_event_cb(btn_discover, discover_cb, LV_EVENT_CLICKED, NULL);

  btn_pair = lv_btn_create(btn_row);
  lv_obj_set_size(btn_pair, 90, 36);
  lv_obj_t* lp = lv_label_create(btn_pair);
  lv_label_set_text(lp, "Pair");
  lv_obj_center(lp);
  lv_obj_add_event_cb(btn_pair, pair_cb, LV_EVENT_CLICKED, NULL);

  btn_unpair = lv_btn_create(btn_row);
  lv_obj_set_size(btn_unpair, 90, 36);
  lv_obj_t* lu = lv_label_create(btn_unpair);
  lv_label_set_text(lu, "Unpair");
  lv_obj_center(lu);
  lv_obj_add_event_cb(btn_unpair, unpair_cb, LV_EVENT_CLICKED, NULL);

  // Countdown + status
  lbl_countdown = lv_label_create(parent);
  lv_label_set_text(lbl_countdown, "");

  lbl_status = lv_label_create(parent);
  lv_label_set_text(lbl_status, kitt_bt_enabled() ? "Bluetooth On" : "Bluetooth Off");
}

void bt_panel_update(void) {
  if (discovery_active) {
    uint32_t elapsed = millis() - discovery_start_ms;
    uint32_t remaining = (DISCOVERY_TIMEOUT_MS - elapsed) / 1000;
    char buf[32];
    snprintf(buf, sizeof(buf), "Discovering... %lus", remaining);
    lv_label_set_text(lbl_countdown, buf);

    if (kitt_bt_discovery_done() || elapsed >= DISCOVERY_TIMEOUT_MS) {
      discovery_active = false;
      lv_obj_clear_state(btn_discover, LV_STATE_DISABLED);
      lv_label_set_text(lbl_countdown, "");

      int found = kitt_bt_discovered_count();
      if (found > 0) {
        char options[512] = {0};
        for (int i = 0; i < found && i < 10; i++) {
          char name[64] = {0};
          kitt_bt_get_discovered_name(i, name, sizeof(name));
          strncat(options, name, sizeof(options) - strlen(options) - 2);
          if (i < found - 1) strncat(options, "\n", sizeof(options) - strlen(options) - 1);
        }
        lv_dropdown_set_options(dd_paired, options);
        char status[64];
        snprintf(status, sizeof(status), "Found %d device(s)", found);
        lv_label_set_text(lbl_status, status);
      } else {
        lv_label_set_text(lbl_status, "No devices found");
      }
    }
  }
}
```

---

## Panel 3: lora_panel.cpp

```cpp
#include <lvgl.h>
#include "kitt_api.h"

static lv_obj_t* sw_lora;
static lv_obj_t* dd_region;
static lv_obj_t* slider_power;
static lv_obj_t* lbl_power;
static lv_obj_t* lbl_rssi;
static lv_obj_t* lbl_status;

static const uint32_t REGION_FREQS[] = {
  915000000,  // US915
  868000000,  // EU868
  470000000,  // CN470
  865000000,  // IN865
  923000000   // JP923
};

static void lora_toggle_cb(lv_event_t* e) {
  if (lv_obj_has_state(sw_lora, LV_STATE_CHECKED)) {
    kitt_lora_enable();
    lv_label_set_text(lbl_status, "LoRa On");
  } else {
    kitt_lora_disable();
    lv_label_set_text(lbl_status, "LoRa Off");
  }
}

static void region_cb(lv_event_t* e) {
  uint8_t idx = lv_dropdown_get_selected(dd_region);
  kitt_lora_set_frequency(REGION_FREQS[idx]);
}

static void power_slider_cb(lv_event_t* e) {
  int val = lv_slider_get_value(slider_power);
  kitt_lora_set_power((uint8_t)val);
  char buf[24];
  snprintf(buf, sizeof(buf), "Power: %d dBm", val);
  lv_label_set_text(lbl_power, buf);
}

void lora_panel_build(lv_obj_t* parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 8, 0);
  lv_obj_set_style_pad_all(parent, 8, 0);

  // LoRa toggle row
  lv_obj_t* row1 = lv_obj_create(parent);
  lv_obj_set_size(row1, 300, 40);
  lv_obj_set_style_border_width(row1, 0, 0);
  lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);

  lv_obj_t* lbl_lora = lv_label_create(row1);
  lv_label_set_text(lbl_lora, "LoRa Radio");
  lv_obj_align(lbl_lora, LV_ALIGN_LEFT_MID, 0, 0);

  sw_lora = lv_switch_create(row1);
  lv_obj_align(sw_lora, LV_ALIGN_RIGHT_MID, 0, 0);
  if (kitt_lora_enabled()) lv_obj_add_state(sw_lora, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw_lora, lora_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // Region dropdown
  lv_obj_t* lbl_region = lv_label_create(parent);
  lv_label_set_text(lbl_region, "Region / Frequency:");

  dd_region = lv_dropdown_create(parent);
  lv_obj_set_width(dd_region, 300);
  lv_dropdown_set_options(dd_region, "US915\nEU868\nCN470\nIN865\nJP923");
  lv_obj_add_event_cb(dd_region, region_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // Power slider
  lbl_power = lv_label_create(parent);
  lv_label_set_text(lbl_power, "Power: 17 dBm");

  slider_power = lv_slider_create(parent);
  lv_obj_set_width(slider_power, 300);
  lv_slider_set_range(slider_power, 2, 22);
  lv_slider_set_value(slider_power, 17, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider_power, power_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // RSSI display
  lbl_rssi = lv_label_create(parent);
  lv_label_set_text(lbl_rssi, "RSSI: --");

  // Status
  lbl_status = lv_label_create(parent);
  lv_label_set_text(lbl_status, kitt_lora_enabled() ? "LoRa On" : "LoRa Off");
}

void lora_panel_update(void) {
  if (kitt_lora_enabled() && !kitt_lora_yielded()) {
    char buf[32];
    snprintf(buf, sizeof(buf), "RSSI: %d dBm", kitt_lora_get_rssi());
    lv_label_set_text(lbl_rssi, buf);
  } else if (kitt_lora_yielded()) {
    lv_label_set_text(lbl_status, "LoRa handed to firmware");
    lv_label_set_text(lbl_rssi, "RSSI: N/A (yielded)");
  }
}
```

---

## Panel 4: battery_panel.cpp

```cpp
#include <lvgl.h>
#include "kitt_api.h"

static lv_obj_t* bar_battery;
static lv_obj_t* lbl_percent;
static lv_obj_t* lbl_voltage;
static lv_obj_t* lbl_current;
static lv_obj_t* lbl_charging;
static lv_obj_t* slider_cpu;
static lv_obj_t* lbl_cpu;

static void cpu_slider_cb(lv_event_t* e) {
  int raw = lv_slider_get_value(slider_cpu);
  // Snap to 80, 160, 240
  int freq = (raw < 2) ? 80 : (raw < 3) ? 160 : 240;
  kitt_cpu_set_freq_mhz(freq);
  char buf[32];
  snprintf(buf, sizeof(buf), "CPU: %d MHz", freq);
  lv_label_set_text(lbl_cpu, buf);
}

void battery_panel_build(lv_obj_t* parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 8, 0);
  lv_obj_set_style_pad_all(parent, 8, 0);

  // Battery percentage bar
  lv_obj_t* lbl_batt = lv_label_create(parent);
  lv_label_set_text(lbl_batt, "Battery:");

  bar_battery = lv_bar_create(parent);
  lv_obj_set_size(bar_battery, 300, 20);
  lv_bar_set_range(bar_battery, 0, 100);
  lv_bar_set_value(bar_battery, kitt_battery_percent(), LV_ANIM_OFF);

  lbl_percent = lv_label_create(parent);
  lbl_voltage  = lv_label_create(parent);
  lbl_current  = lv_label_create(parent);
  lbl_charging = lv_label_create(parent);

  // CPU frequency slider
  lv_obj_t* lbl_cpu_hdr = lv_label_create(parent);
  lv_label_set_text(lbl_cpu_hdr, "CPU Frequency:");

  slider_cpu = lv_slider_create(parent);
  lv_obj_set_width(slider_cpu, 300);
  lv_slider_set_range(slider_cpu, 1, 3);  // 1=80, 2=160, 3=240
  int cur_freq = kitt_cpu_get_freq_mhz();
  int slider_val = (cur_freq <= 80) ? 1 : (cur_freq <= 160) ? 2 : 3;
  lv_slider_set_value(slider_cpu, slider_val, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider_cpu, cpu_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lbl_cpu = lv_label_create(parent);
  char cpu_buf[32];
  snprintf(cpu_buf, sizeof(cpu_buf), "CPU: %d MHz", cur_freq);
  lv_label_set_text(lbl_cpu, cpu_buf);

  battery_panel_update();
}

void battery_panel_update(void) {
  int pct     = kitt_battery_percent();
  int mv      = kitt_battery_voltage_mv();
  int ma      = kitt_battery_current_ma();
  bool chg    = kitt_battery_charging();

  // Color code bar
  lv_color_t color;
  if (pct < 10)       color = lv_color_hex(0xFF0000);  // Red
  else if (pct < 25)  color = lv_color_hex(0xFF8800);  // Orange
  else                color = lv_color_hex(0x00AA00);  // Green

  lv_obj_set_style_bg_color(bar_battery, color, LV_PART_INDICATOR);
  lv_bar_set_value(bar_battery, pct, LV_ANIM_ON);

  char buf[64];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  lv_label_set_text(lbl_percent, buf);

  snprintf(buf, sizeof(buf), "Voltage: %d.%02dV", mv / 1000, (mv % 1000) / 10);
  lv_label_set_text(lbl_voltage, buf);

  if (ma > 0) {
    snprintf(buf, sizeof(buf), "Current: %d mA", ma);
    lv_label_set_text(lbl_current, buf);
  } else {
    lv_label_set_text(lbl_current, "Current: N/A");
  }

  lv_label_set_text(lbl_charging, chg ? "Status: Charging" : "Status: On battery");
}
```

---

## Panel 5: handoff_panel.cpp

```cpp
#include <lvgl.h>
#include "kitt_api.h"
#include "system_api.h"

static lv_obj_t* lbl_wifi_owner;
static lv_obj_t* lbl_bt_owner;
static lv_obj_t* lbl_lora_owner;
static lv_obj_t* lbl_hint;
static lv_obj_t* list_firmware;

static void handoff_btn_cb(lv_event_t* e) {
  const char* fw_path = (const char*)lv_event_get_user_data(e);
  system_request_handoff(fw_path);
}

static void reclaim_btn_cb(lv_event_t* e) {
  system_reclaim_radios();
}

void handoff_panel_build(lv_obj_t* parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 8, 0);
  lv_obj_set_style_pad_all(parent, 8, 0);

  // Radio ownership display
  lv_obj_t* lbl_hdr = lv_label_create(parent);
  lv_label_set_text(lbl_hdr, "Radio Ownership:");

  lbl_wifi_owner = lv_label_create(parent);
  lbl_bt_owner   = lv_label_create(parent);
  lbl_lora_owner = lv_label_create(parent);

  // Firmware list with handoff buttons
  lv_obj_t* lbl_fw = lv_label_create(parent);
  lv_label_set_text(lbl_fw, "Available Firmware:");

  list_firmware = lv_list_create(parent);
  lv_obj_set_size(list_firmware, 300, 160);

  int fw_count = kitt_firmware_list_count();
  for (int i = 0; i < fw_count; i++) {
    firmware_entry_t fw;
    kitt_firmware_get_entry(i, &fw);

    lv_obj_t* btn = lv_list_add_btn(list_firmware, NULL, fw.name);
    // Store firmware path as user data for callback
    lv_obj_add_event_cb(btn, handoff_btn_cb, LV_EVENT_CLICKED, (void*)strdup(fw.path));
  }

  // Reclaim button
  lv_obj_t* btn_reclaim = lv_btn_create(parent);
  lv_obj_set_size(btn_reclaim, 300, 36);
  lv_obj_t* lbl_reclaim = lv_label_create(btn_reclaim);
  lv_label_set_text(lbl_reclaim, "Reclaim All Radios");
  lv_obj_center(lbl_reclaim);
  lv_obj_add_event_cb(btn_reclaim, reclaim_btn_cb, LV_EVENT_CLICKED, NULL);

  // Reserved combo hint
  lbl_hint = lv_label_create(parent);
  lv_obj_set_style_text_font(lbl_hint, &lv_font_montserrat_10, 0);
  lv_label_set_text(lbl_hint, "Hold [PWR + SEL] to force-kill active firmware");

  handoff_panel_update();
}

void handoff_panel_update(void) {
  char buf[64];

  snprintf(buf, sizeof(buf), "WiFi: %s",
    kitt_wifi_yielded() ? "Firmware" : "KITT");
  lv_label_set_text(lbl_wifi_owner, buf);

  snprintf(buf, sizeof(buf), "BT:   %s",
    kitt_bt_yielded() ? "Firmware" : "KITT");
  lv_label_set_text(lbl_bt_owner, buf);

  snprintf(buf, sizeof(buf), "LoRa: %s",
    kitt_lora_yielded() ? "Firmware" : "KITT");
  lv_label_set_text(lbl_lora_owner, buf);
}
```

---

## Memory Budget

| Component | Estimated Size |
|---|---|
| main.cpp + titlebar + tabview | ~40KB |
| wifi_panel | ~50KB |
| bt_panel | ~45KB |
| lora_panel | ~30KB |
| battery_panel | ~30KB |
| handoff_panel | ~35KB |
| LVGL widget overhead | ~70KB |
| **Total** | **~300KB** |

---

## Integration Notes

- **Launch**: system.meow calls `controlpanel_create(explorer_screen)` and registers it as a lightweight overlay
- **Close**: user taps X → `close_btn_cb` → `system_app_close("controlpanel")` → system.meow calls `controlpanel_destroy()`
- **During firmware exclusivity**: system.meow blocks launch if `friends_ram_threshold_kb` is 0 or free RAM is below threshold
- **Polling**: only the active tab is polled every 200ms — inactive tabs do not update to save CPU
- **Keyboard input**: password field summons keyboard.meow via `system_request_keyboard(ta_password)` — keyboard.meow returns text via callback, no direct dependency
