# 10 — Module Loading, Priority, and SD Fallback

This document covers how PURR OS loads kernel modules at boot: the priority system, the two-stage flash-then-SD strategy, the panic screen, and the SD card directory layout.

---

## Module Priority

Every `.purr` binary declares a `load_priority` in its `purr_module_header_t`:

| Level | Constant | Value | Meaning |
|---|---|---|---|
| **REQUIRED** | `PURR_PRIORITY_REQUIRED` | `1` | Kernel **panics** if this fails to load from both flash and SD. Use for display driver, touch driver — anything without which the OS cannot function. |
| **IMPORTANT** | `PURR_PRIORITY_IMPORTANT` | `2` | Loaded before apps. Kernel logs a warning and continues if missing. Use for radio, GPS, secondary input. |
| **OPTIONAL** | `PURR_PRIORITY_OPTIONAL` | `3` | Silent skip if missing. Use for non-essential add-ons. |

Setting priority in a module's header:

```c
purr_module_header_t purr_module = {
    .magic         = PURR_MODULE_MAGIC,
    .abi_version   = PURR_MODULE_ABI_VERSION,
    .module_type   = PURR_MOD_DRIVER,
    .load_priority = PURR_PRIORITY_REQUIRED,   // ← kernel panics without this
    .name          = "ili9341",
    .version       = "1.0.0",
    .kernel_min    = "0.9.0",
    .provided_catcalls = CATCALL_FLAG_DISPLAY,
    .init   = ili9341_init,
    .deinit = ili9341_deinit,
};
```

---

## Boot Load Sequence

```
app_main()
  │
  ├─ NVS init
  ├─ Mount /flash  (SPIFFS — core modules baked in by purrstrap)
  ├─ Mount /sdcard (FAT — optional, SD driver handles this)
  │
  ├─ Phase 1: scan /flash/modules  + /flash/drivers  (priority-sorted)
  │     For each .purr found:
  │       sort by load_priority (P1 first)
  │       attempt load from flash
  │       if fail AND P1 → try /sdcard/modules (or /sdcard/drivers)
  │                      → still fail → PANIC
  │       if fail AND P2 → log warning, continue
  │       if fail AND P3 → silent continue
  │
  └─ Phase 2: scan /sdcard/modules + /sdcard/drivers  (extras, all treated as optional)
```

The sort guarantees that `PURR_PRIORITY_REQUIRED` (P1) drivers — particularly the display driver — are loaded and have registered their catcalls **before** any P2 or P3 module tries to use them.

---

## Flash Partition — What Goes In

The SPIFFS flash partition (`/flash`) is built by **purrstrap** using `spiffsgen.py` from the IDF. It contains only what the `[flash]` section of `device.pcat` lists:

```toml
[flash]
# name = priority
display/ili9341   = 1    # P1: required — kernel panics without display
touch/cst816s     = 1    # P1: required — no touch = can't use UI
miniwin           = 2    # P2: important — UI framework
app_manager       = 2    # P2: important — without this no apps run
radio/sx1262      = 3    # P3: optional
```

The flash layout inside SPIFFS:

```
/flash/
├── modules/
│   ├── miniwin.purr
│   └── app_manager.purr
└── drivers/
    ├── display/
    │   └── ili9341.purr
    ├── touch/
    │   └── cst816s.purr
    └── radio/
        └── sx1262.purr
```

---

## SD Card — Layout and Fallback

The SD card serves two roles:

1. **Fallback** for P1 modules missing from flash (corrupt flash, dev workflow, field update)
2. **Extension** — additional modules, user apps, ROM files, config

```
/sdcard/
├── modules/              ← P1/P2 fallbacks and extra system modules
│   └── *.purr
├── drivers/              ← P1/P2 driver fallbacks and extras
│   ├── display/
│   ├── touch/
│   ├── input/
│   ├── radio/
│   └── gps/
├── apps/                 ← user apps loaded by app_manager
│   ├── *.meow            — Lua scripts
│   ├── *.paws            — compiled userland apps
│   └── *.claw            — compiled kernel-access apps
├── magicmac/             ← MagicMac emulator files
│   ├── mac.rom           — 512KB Mac Plus ROM (user-supplied)
│   └── mac.conf          — override config (optional)
├── magidos/              ← MagiDOS emulator files
│   ├── disk.img          — DOS disk image
│   └── magidos.conf
└── system/
    └── logs/             ← kernel + module log files (future)
```

The `boot.c` `ensure_sd_dirs()` call creates any missing directories when the SD mounts, so a freshly-formatted card is immediately usable.

---

## Kernel Panic Screen

`purr_kernel_panic(const char *reason)` is called when a P1 module fails to load from both flash and SD. It:

1. Logs `KERNEL PANIC` + reason to serial (always — serial works even with no display)
2. If a display catcall is registered (meaning the display driver itself loaded fine but something else failed): draws a red panic screen with white stripes so the user knows the device has halted — not just a blank screen
3. Halts in an infinite `vTaskDelay` loop

> **If the display driver is the P1 module that failed**, the panic screen will not draw (no display catcall registered). Serial log is the only output. Connect a serial monitor to diagnose.

---

## Declaring a Module as Required

Both the module header and `device.pcat` participate:

- **Module header**: set `load_priority = PURR_PRIORITY_REQUIRED` — this is the module's self-declaration
- **device.pcat** `[flash]` section: set `display/ili9341 = 1` — this tells purrstrap to include it in the flash image at all

A module not listed in `[flash]` won't be in the SPIFFS image. If it's P1 and missing from flash, the kernel will attempt the SD fallback. If SD also doesn't have it, it panics.

---

## Adding a New Required Driver

1. Set `load_priority = PURR_PRIORITY_REQUIRED` in the driver's `purr_module_header_t`
2. Add it to the device's `device.pcat` under `[flash]` with priority `1`
3. Run `purrstrap build <device>` — modulestrap builds the blob, it gets staged into SPIFFS, flash.bin is produced
4. Flash with `purrstrap flash <device>`

If you want it on SD as fallback only (e.g. for dev/testing), skip step 3-4 and manually copy the `.purr` file to `/sdcard/drivers/<type>/` on the card.
