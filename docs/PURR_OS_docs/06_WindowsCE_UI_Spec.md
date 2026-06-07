# PURR OS — Explorer Shell Spec (Windows CE / PDA Style)
**Target: CYD (ESP32-2432S024C) — 320×240 landscape**
**Status: Planned — awaiting PDA design references from user**

---

## Overview

`purr_explorer` is the default UI shell for PURR OS on CYD. It mimics the Windows CE / Pocket PC aesthetic — navy taskbar at the bottom, grey beveled window chrome, Start menu, system tray. It is built as a **MiniWin application** — MiniWin owns the render loop, Z-ordering, and touch routing. Explorer adds the WinCE chrome on top.

Explorer is fully swappable. The `PURR_SHELL` cmake flag controls which shell is compiled. Any replacement that initialises as a MiniWin app is a valid drop-in.

---

## MiniWin Integration

Explorer registers itself as a MiniWin window and application via the MiniWin API:

```c
// Initialise MiniWin HAL (display + touch + timer + NVS)
mw_hal_lcd_init();
mw_hal_touch_init();
mw_hal_timer_init();
mw_hal_delay_init();
mw_hal_non_vol_init();

// Initialise MiniWin core
mw_init();

// Register Explorer windows (taskbar, desktop, app windows)
// ... window registration via mw_window_add() ...

// Main loop (runs in its own FreeRTOS task)
while (true) {
    mw_task();   // MiniWin event loop + render tick
    if (mw_tick_counter != last_tick) {
        mw_hal_timer_fired();
        last_tick = mw_tick_counter;
    }
    vTaskDelay(1);
}
```

---

## Screen Layout (320×240 landscape)

```
┌──────────────────────────────────────────────┐  y=0
│                                              │
│            Desktop / App Windows             │
│                                              │
│                                              │
│                                              │  y=210
├──────────────────────────────────────────────┤
│ [⊞ Start] [running app 1] [running app 2]  🔋📶│  y=210..239
└──────────────────────────────────────────────┘  y=239
         Taskbar (30px tall)
```

---

## Taskbar (bottom 30px)

| Region | Content |
|--------|---------|
| Left | Start button (⊞) — opens Start menu on tap |
| Centre | Running app tiles — one button per open window, tap to focus |
| Right | System tray: battery %, WiFi icon, BT icon, LoRa icon, clock |

Tray data comes from the KITT tray callback (`set_tray_update_cb`). Updated every 30s or on radio state change.

---

## Start Menu

Opens upward from Start button. Full-width panel covering ~180px height.

```
┌──────────────────────┐
│  PURR OS v0.6.0      │  Header — OS name from kitt.os_name()
├──────────────────────┤
│  📱 App Name 1       │  App entries from kitt.app_list_count()
│  📱 App Name 2       │
│  ...                 │
├──────────────────────┤
│  ⚙ Settings          │  → Control Panel overlay (future)
│  🔄 Restart          │  → esp_restart()
│  ⬇ Bootloader        │  → pm_boot_to_factory()
└──────────────────────┘
```

Tap an app → `kitt.app_launch(path)` → MicroPython process starts → window appears on desktop.

---

## App Windows

MiniWin provides: title bar, close button, move gesture, Z-ordering, overlapping. Explorer sets window chrome to WinCE style (navy title bar, grey body, beveled border).

Standard window dimensions: 280×180px (centred, leaving taskbar visible).

---

## System Tray Icons

| Icon | Source | Behaviour |
|------|--------|-----------|
| Battery % | `kitt.battery_percent()` | Updates every 60s |
| WiFi | `kitt.wifi_connected()` + `wifi_signal_strength()` | 4-bar signal icon |
| Bluetooth | `kitt.bt_enabled()` | On/off icon |
| LoRa | `kitt.lora_enabled()` | On/off icon — hidden on CYD (no hardware) |
| Clock | `esp_timer_get_time()` | HH:MM, updates every minute |

---

## KITT API Calls Used by Explorer

```cpp
// Init
kitt.set_tray_update_cb(on_tray_update);
kitt.set_popup_cb(on_popup);
kitt.set_notify_cb(on_notify);
kitt.set_crash_report_cb(on_crash);
kitt.set_memory_warning_cb(on_memory_warning);

// App management
int count = kitt.app_list_count();
app_entry_t entry;
kitt.app_get_entry(i, &entry);
kitt.app_launch(entry.path);
kitt.process_running(entry.path);
kitt.process_kill(entry.path);

// System actions
kitt.os_name();           // title bar header
kitt.device_name();       // device info
kitt.free_ram_kb();       // memory status
kitt.uptime_ms();         // uptime display
pm_boot_to_factory();     // Bootloader menu item (partition_manager.h)
```

---

## Compile Flags

```cmake
PURR_HAS_EXPLORER=1
PURR_HAS_BLACKBERRY_UI=0   # when shell=explorer
PURR_BBUI_TARGET_TOUCH=1   # CST816S tap input
```

Set via `-DPURR_SHELL=explorer` or `--shell explorer` in SDK.

---

## Status

Implementation pending design reference assets from user. MiniWin HAL is fully wired and tested. The shell code in `explorer.cpp` is a stub — the MiniWin app registration, window layout, and event loop need to be written once the visual design is confirmed.
