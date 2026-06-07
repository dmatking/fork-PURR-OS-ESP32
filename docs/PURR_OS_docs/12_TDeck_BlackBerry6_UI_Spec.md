# PURR OS — BlackberryUI Spec

Inspired by BlackBerry OS 6. Targets: **T-Deck Plus** (trackball + physical QWERTY) and **CYD ESP32-2432S028R** (capacitive touch). Both run a 320×240 landscape display.

---

## Layout Zones

```
┌─────────────────────────────────────────────────────┐  ← y=0
│              STATUS BAR  (20px)                     │    battery · OS name · WiFi · signal
├─────────────────────────────────────────────────────┤  ← y=20
│              TIME + DATE  (30px)                    │    large centered clock, date below
├─────────────────────────────────────────────────────┤  ← y=50
│              NOTIFICATION BAR  (20px)               │    speaker · app badges · search
├─────────────────────────────────────────────────────┤  ← y=70
│                                                     │
│         CONTENT AREA  (134px)                       │    home: wallpaper + OS logo
│                                                     │    drawer: 4×N app icon grid
│                                                     │
├─────────────────────────────────────────────────────┤  ← y=204
│              TAB STRIP  (16px)                      │    Frequent · All · Favorites
├─────────────────────────────────────────────────────┤  ← y=220
│              DOCK  (20px)                           │    4 pinned app icons
└─────────────────────────────────────────────────────┘  ← y=240
```

---

## Screen States

### Home — wallpaper visible, 4-icon dock at bottom

```
┌─────────────────────────────────────────────────────┐
│ ▓▓▓   PURR OS                  WiFi  ▰▰▰▱   ▓▓▓  │
│                                                     │
│                    1 4 : 3 2                        │
│                  Wed, June 2                        │
│                                                     │
├─────────────────────────────────────────────────────┤
│  ◈)   MESH ②   MSGS ③   GPS ●   LOGS       [🔍]  │
├─────────────────────────────────────────────────────┤
│                                                     │
│          · · · · · · · · · · · · · · ·             │
│        ·                               ·            │
│       ·          P U R R  O S           ·           │
│        ·                               ·            │
│          · · · · · · · · · · · · · · ·             │
│                                                     │
├───────────────┬────────────────┬────────────────────┤
│   Frequent    │      All       │    Favorites       │
├─────────────────────────────────────────────────────┤
│ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────┐ │
│ │          │ │          │ │          │ │        │ │
│ │  MESH ②  │ │  MSGS ③  │ │   GPS    │ │ FLASH  │ │
│ └──────────┘ └──────────┘ └──────────┘ └────────┘ │
└─────────────────────────────────────────────────────┘
```

### App Drawer — scroll down or tap All tab

```
┌─────────────────────────────────────────────────────┐
│ ▓▓▓   PURR OS                  WiFi  ▰▰▰▱   ▓▓▓  │
│                  1 4 : 3 2  ·  Wed, June 2          │
├─────────────────────────────────────────────────────┤
│  ◈)   MESH ②   MSGS ③   GPS ●   LOGS       [🔍]  │
├───────────────┬────────────────┬────────────────────┤
│   Frequent    │  ▌ All ▐        │    Favorites       │
├─────────────────────────────────────────────────────┤
│ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────┐ │
│ │          │ │          │ │          │ │        │ │
│ │  MESH ②  │ │  MSGS ③  │ │   GPS    │ │ FLASH  │ │
│ └──────────┘ └──────────┘ └──────────┘ └────────┘ │
│ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────┐ │
│ │          │ │          │ │          │ │        │ │
│ │ SETTINGS │ │   LOGS   │ │   APP 1  │ │ APP 2  │ │
│ └──────────┘ └──────────┘ └──────────┘ └────────┘ │
└─────────────────────────────────────────────────────┘
```

When scrolled down, the wallpaper collapses to a single compact status line to make room for the grid. The tab strip shifts up to just below the notification bar.

---

## Colour Palette

| Element | Hex | Notes |
|---|---|---|
| Background | `0x1A1A2E` → `0x0D0D0D` | Dark navy to near-black gradient |
| Status bar | `0x000000` | Solid black |
| Status text | `0xFFFFFF` | White |
| Notification bar | `0x111111` | Slightly lighter than status |
| Tab active | `0xFFFFFF` | White label + underline |
| Tab inactive | `0x555555` | Grey |
| Icon tile bg | `0x1E1E2E` | Dark tile |
| Icon tile border | `0x2A2A3E` | Subtle border |
| Icon selected ring | `0x4FC3F7` | BB blue glow |
| Icon label | `0xFFFFFF` | White |
| Badge red | `0xFF3333` | Unread count |
| Badge bg | `0xCC0000` | Badge background |
| Dock bar | `0x080810` | Near-black with slight transparency |
| Watermark | `0x1C1C30` | Faint logo on wallpaper |
| Battery full | `0x00CC00` | Green |
| Battery low | `0xFF4444` | Red |

---

## Icon Grid

- 4 columns, N rows (scrollable)
- Each cell: 80×50px (320 / 4 cols)
- Icon tile: 60×36px, centered in cell, rounded corners
- Label: 10px below tile, centered, truncated with `…` if too long
- Badge: red circle top-right of tile, white number inside
- Selected: BB blue `0x4FC3F7` glow border, 2px
- App entries sourced from `kitt.app_list()` at runtime; no hardcoded icons

### Pinned dock (4 slots, always visible)
Default pins: `MESH`, `MSGS`, `GPS`, `FLASH OS`
User-configurable via NVS key `bbui/dock_N`.

### Tab behaviour
| Tab | Content |
|---|---|
| Frequent | Top 8 most-launched apps (tracked in NVS) |
| All | Full `kitt.app_list()` — scanned apps only |
| Favorites | User-starred apps (NVS `bbui/fav_N`) |

---

## Input Mapping

### T-Deck Plus (trackball + QWERTY)

| Input | Action |
|---|---|
| Trackball ←→↑↓ | Move icon selection cursor |
| Trackball click / Enter | Launch selected app |
| Trackball scroll down | Open app drawer |
| Trackball scroll up | Return to home wallpaper |
| `M` key | Open context action menu |
| `B` / Back button | Navigate back / close overlay |
| `S` key | Universal search |
| Number keys `1–4` | Quick-launch dock slot |
| Tab ← → (Alt+arrow) | Switch Frequent / All / Favorites |

### CYD (CST816S capacitive touch)

| Input | Action |
|---|---|
| Tap icon | Launch app |
| Tap tab | Switch Frequent / All / Favorites |
| Swipe up | Open app drawer |
| Swipe down | Return to home wallpaper |
| Tap notification badge | Open that app directly |
| Tap search | Open search overlay (text input TBD) |
| Long-press icon | Context menu (move to Favorites, etc.) |

---

## Notification Bar

Always visible row between time and content area. Left to right:

```
◈)   MESH ②   MSGS ③   GPS ●   LOGS           [🔍]
```

| Slot | Content | Condition |
|---|---|---|
| Speaker | Volume/mute indicator | Always |
| MESH ② | Mesh node count + unread messages | `PURR_HAS_MESH` |
| MSGS ③ | Inbox unread count | Always |
| GPS ● | Fix indicator (● = locked, ○ = searching) | `PURR_HAS_GPS` |
| LOGS | Recent log entry / crash indicator | Always |
| [search] | Tap/key to open search | Right-aligned |

Badges update every 2 seconds from `kitt` state. Zero counts hidden.

---

## Compile Flags

| Flag | Value | Effect |
|---|---|---|
| `PURR_HAS_BLACKBERRY_UI` | `1` | Enables BlackberryUI shell |
| `PURR_BBUI_TARGET_TOUCH` | `1` | CYD touch input path |
| `PURR_BBUI_TARGET_KEYS` | `1` | T-Deck trackball + keyboard path |

Set in `main/CMakeLists.txt` per device target.

---

## File Structure

```
system/kernel/modules/
├── blackberry_ui.h              ← public API: blackberry_ui_start()
├── blackberry_ui.cpp            ← main task, screen state machine
├── bbui_statusbar.cpp/.h        ← status + time + notification rows
├── bbui_grid.cpp/.h             ← icon grid, cell rendering, scroll
├── bbui_dock.cpp/.h             ← 4-slot pinned dock
├── bbui_tabs.cpp/.h             ← Frequent / All / Favorites tab strip
├── bbui_wallpaper.cpp/.h        ← PURR OS logo / wallpaper area
├── bbui_overlay.cpp/.h          ← context menu, search overlay
└── bbui_input_touch.cpp/.h      ← CYD CST816S touch → UI events
    bbui_input_keys.cpp/.h       ← T-Deck trackball + keyboard → UI events
```

`system/system/main.cpp` routes to `blackberry_ui_start()` when `PURR_HAS_BLACKBERRY_UI=1`, replacing `explorer_start()` on CYD and `smol_start()` on T-Deck.

---

## Status: WIP

| Item | Status |
|---|---|
| ASCII spec + layout | ✅ Done |
| CYD compile flag wired | ✅ Done |
| T-Deck compile flag wired | ✅ Done |
| `blackberry_ui.cpp` stub | ✅ Done |
| Status bar rendering | ⬜ Not started |
| Icon grid rendering | ⬜ Not started |
| Dock rendering | ⬜ Not started |
| Tab strip | ⬜ Not started |
| Wallpaper / logo | ⬜ Not started |
| CYD touch input | ⬜ Not started |
| T-Deck trackball input | ⬜ Not started (trackball GPIO TBD) |
| T-Deck ST7789 driver | ⬜ Blocked — display driver not written |
| Search overlay | ⬜ Not started |
| Context menu | ⬜ Not started |
| NVS favorites / freq tracking | ⬜ Not started |
