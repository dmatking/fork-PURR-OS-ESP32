# Export — PURR OS session dump

Generated on request, 2026-07-11. Contains the assistant's persistent memory
for this project plus a snapshot of in-progress work at the time of export.

---

## Persistent memory (auto-memory system)

### Index (MEMORY.md)

- [OOBE node identity](oobe_node_identity.md) — Meshtastic name/hw_model hardcoded
  on purpose, deferred to future v1.0 first-run setup flow
- [Perf debug mode idea](perf_debug_mode_idea.md) — future cupcake/MiniWin
  perf-debugging mode, talking stage only, scope not decided

### oobe-node-identity

`mesh_router_encode_nodeinfo()` in `source/modules/meshtastic/mesh_router.c`
hardcodes the Meshtastic node identity: `long_name = "PURR-XXXXXXXX"`,
`short_name = "PRR"`, `hw_model = HELTEC_V3` (wrong for T-Deck Plus,
cosmetic only — wrong device icon on real Meshtastic clients).

**Why:** User confirmed live testing works correctly with these placeholder
values (visible to other real nodes, hw_model shows as Heltec V3 as expected
given the hardcoded value) and explicitly does not want this fixed now — it's
planned to be part of a future out-of-box-experience (OOBE) onboarding flow
for PURR OS v1.0: on first flash, walk the user through a setup process
(presumably including device name/identity configuration) before dropping
them to the desktop.

**How to apply:** Don't proactively "fix" the hardcoded name/hw_model as a
bug — it's known and intentionally deferred. When OOBE/first-run setup work
actually starts, this is one of the things it should configure. Don't
re-raise this as an issue unless the user brings up OOBE/setup work
specifically.

### perf-debug-mode-idea

Idea floated, still at the talking stage — not implemented, no design
decided yet: add a "performance bugs" debug mode to both the cupcake and
MiniWin UI shell modules (`source/modules/cupcake/`, `source/modules/miniwin/`),
meant to help diagnose UI performance issues (jank, slow redraws, etc.) in
those two shells specifically.

**Why:** user wants to shift focus toward Meshtastic-side work next, but
flagged this as something to come back to and add more debug tooling for
later.

**How to apply:** When next asked to build this out, don't assume scope —
clarify whether it means an on-screen debug overlay (FPS/frame time/heap/task
stack high-water marks), serial-log-only perf output, or both. That question
was asked once already and the answer at the time was "just make a note for
now," so the actual design is still open.

---

## Session snapshot at time of export

Standing instruction in effect for this whole session: **switch the UI to
WinCE/MiniWin, and avoid the Arduino kernel at all costs** —
`tdeck_plus_arduino` is only ever built to verify compilation, never flashed;
`tdeck_plus` (module-based MiniWin desktop) is the only target flashed to
real hardware (`/dev/ttyACM0`).

### Just completed: 4-window WinCE desktop split + stale-pixel fix

Split the single full-screen `desktop_paint()` window into four real,
z-ordered MiniWin windows — wallpaper, desktop icons, taskbar+Start Menu
(resizes live to cover the menu popup), and a dedicated lock overlay
(invisible/inert until triggered) — in both
`source/modules/miniwin/miniwin_wince_desktop.c` (generic MiniWin module)
and `source/kernel/kernel_tdeck_plus_arduino/wince_shell.cpp` (Arduino
kernel, compile-only target).

Root cause of a stale-pixel bug reported three times by the user (Start Menu
close, app-window close, lock/unlock all left old pixels on screen): MiniWin
does not auto-clear a window's client area before invoking its paint
callback. `icons_paint()`/`dtbtn_paint()`/`taskbar_paint()`'s menu-open gap
region never filled their own background — they relied on the separate,
lower z-order wallpaper window having already cleared that area, which
wasn't reliable across independent window repaints. Fixed by making every
window's paint function self-sufficient (fill its own current rect first,
exactly like the old single-window design implicitly did). All three
remaining call sites (`taskbar_paint()` in both files, `dtbtn_paint()` in
`wince_shell.cpp`) were patched with this fix; `icons_paint()` had already
received it. **Not yet rebuilt/reflashed/re-verified on hardware as of this
export.**

### Queued, not yet started

Three requests arrived back-to-back while the stale-pixel fix was in
progress, in this order:

1. **Overlapping app windows**: stop opening every app full-screen; support
   multiple movable/resizable app windows open at once, overlapping each
   other, in the MiniWin desktop (`tdeck_plus` primarily, mirrored to
   `wince_shell.cpp`). Not yet designed — was about to read
   `purr_win_create()`/`miniwin_win.c` to see how app windows are created
   today before this was interrupted.
2. **Real wallpaper image**: convert `~/Downloads/wallpaper.webp` (498×337,
   VP8) to a bitmap the wallpaper window can render, replacing the current
   flat `WCE_DESKTOP`-colored fill. Found the source image and the existing
   icon-conversion pipeline (`source/assets/icons/convert_icons.py`, MDI
   SVG → LVGL `lv_img_dsc_t` RGB565+alpha C arrays) as the closest existing
   precedent — MiniWin's wallpaper window isn't LVGL-based though, so the
   actual bitmap format/loading mechanism MiniWin itself uses (`mw_gl_*`
   bitmap draw calls, if any) still needs to be checked before writing a
   converter. Not started.
3. This commit/export request itself.

### Uncommitted working-tree state at export time

Modified (tracked): `CoreOS/sdkconfig_heltec`, `CoreOS/sdkconfig_tdeck_plus`,
`purrstrap/purrstrap.py`, `source/apps/system/meshchat/meshchat.c`,
`source/apps/system/taskmgr/taskmgr.c`, `source/devices/heltec/device.pcat`,
`source/devices/tdeck_plus/device.pcat`,
`source/drivers/battery/adc_battery/adc_battery.c`,
`source/drivers/display/ssd1306/ssd1306.c`, `source/kernel/core/boot.c`,
`source/kernel/core/purr_kernel.c`, `source/kernel/core/purr_kernel.h`,
`source/kernel/kernel_tdeck_plus_arduino/wince_shell.cpp`,
`source/modules/meshtastic/meshtastic_module.c`,
`source/modules/miniwin/CMakeLists.txt`,
`source/modules/miniwin/miniwin_cursor.{c,h}`,
`source/modules/miniwin/miniwin_keyboard.c`,
`source/modules/miniwin/miniwin_module.c`,
`source/modules/miniwin/miniwin_win.c`,
`source/modules/miniwin/miniwin_wince_desktop.{c,h}`,
`source/modules/oled_ui/CMakeLists.txt`,
`source/modules/oled_ui/oled_ui_module.c`.

New (untracked, included in this commit): `source/drivers/input/heltec_button/`,
`source/modules/miniwin/miniwin_lock.{c,h}` (the dedicated lock-overlay
mechanism referenced above).

Left untracked/uncommitted by choice: `CatReleases/` (463MB of packaged
release zips — a separate staging folder, not the established
`releases/vX.Y.Z/` convention already tracked in git; not added without
explicit confirmation given its size). `releases/v1.0.0-dp/` and
`releases/v1.0.0-dp4/` DO match the existing tracked convention and were
included.
