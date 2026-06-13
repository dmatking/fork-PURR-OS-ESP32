# 09 — Build Tools

PURR OS uses three separate build tools, each responsible for a different output type, plus a master launcher that ties them together. All output lands in `cattobaked/` at the repo root.

---

## Quick Start

```bash
# Interactive — pick your tool from a menu
./purr.sh           # bash (Linux/macOS)
.\purr.ps1          # PowerShell (Windows)
python purr.py      # Python directly

# Jump to a specific tool's interactive UI
./purr.sh purrstrap
./purr.sh modulestrap
./purr.sh catstrap

# Pass-through to the underlying CLI (no UI)
./purr.sh purrstrap build tdeck_plus
./purr.sh modulestrap list
./purr.sh catstrap sdk info
```

---

## Architecture — Two Layers

Every tool has two files:

| File | Role |
|---|---|
| `<tool>.py` | Pure CLI — argparse, all logic, no prompts |
| `<tool>_ui.py` | Interactive wrapper — numbered menus → calls `<tool>.py` |

The UI layer never reimplements logic. It collects user choices, assembles the right CLI arguments, and hands off to the underlying script via `subprocess`. This means the CLI is always scriptable/CI-friendly, and the UI is always optional.

`purr.py` is the master launcher. With no args it shows the tool picker. With a tool name and no further args it opens that tool's UI. With extra args it passes them straight to the underlying CLI.

---

## Tools

### purrstrap — Firmware Image Builder
**Files:** `purrstrap/purrstrap.py` · `purrstrap/purrstrap_ui.py`

Builds the final flashable firmware image for a target device. Reads `source/devices/<device>/device.pcat` for hardware configuration.

**Output:** `cattobaked/<device>/`
- `firmware.bin` — merged flash image
- `bootloader.bin`
- `partition-table.bin`
- `purr_kernel.bin` — kernel spine only
- `build.json` — build metadata

**CLI commands:**
```
purrstrap build <device>       build firmware
purrstrap flash <device> [-p]  build + flash
purrstrap clean <device>       remove artifacts
purrstrap list                 list supported devices
purrstrap status               show .purrstrap workspace
purrstrap doctor               check IDF + environment
```

**Interactive UI actions:**
- Build, Flash, Clean (prompts for device + serial port)
- List, Status, Doctor (informational)

---

### modulestrap — Kernel Module + Driver Compiler
**Files:** `modulestrap/modulestrap.py` · `modulestrap/modulestrap_ui.py`

Compiles kernel modules and hardware drivers into `.purr` blobs. Scans `source/modules/`, `source/drivers/`, and `user_drivers/` (community drivers). Supports extra driver paths via `--drivers`.

**Output:** `cattobaked/`
- `modules/<name>.purr` — system modules (miniwin, app_manager, …)
- `drivers/<type>/<name>.purr` — hardware drivers

**CLI commands:**
```
modulestrap build all           build everything
modulestrap build modules       system modules only
modulestrap build drivers       drivers only
modulestrap build <name>        one specific target
modulestrap list [--drivers …]  list all targets
modulestrap clean [name|all]    remove output
```

**Interactive UI actions:**
- Build all / modules only / drivers only / one specific target
- List (optionally with extra driver paths)
- Clean all / one target

**Custom driver paths:** The UI prompts for additional `--drivers` paths when running list, build-all, or build-drivers. Useful for community drivers outside the repo.

---

### catstrap — App Builder + SDK
**Files:** `catstrap/catstrap.py` · `catstrap/catstrap_ui.py`

Builds user apps and the in-house system exclusives. Manages the catstrap SDK headers.

**Output:** `cattobaked/apps/`
- `<name>.meow` — Lua scripts (validated + packaged)
- `<name>.paws` — compiled userland apps
- `<name>.claw` — compiled kernel-access apps
- `<name>.catt` — in-house exclusives (MagicMac, MagiDOS)

**CLI commands:**
```
catstrap build all              build all user apps
catstrap build <name>           build one app
catstrap build magicmac         build MagicMac (.catt)
catstrap build magidos          build MagiDOS (.catt)
catstrap validate <file.meow>   syntax-check a Lua script
catstrap sdk info               show SDK version + API
catstrap sdk install            write SDK headers
catstrap list                   list all apps
catstrap clean [name|all]       remove output
```

**Interactive UI actions:**
- Build all / one user app / system exclusive (magicmac, magidos)
- Validate .meow file
- SDK info / SDK install
- List, Clean all / one

**App tiers and what each gets:**

| Extension | Tier | API access |
|---|---|---|
| `.meow` | Lua sandbox | `win.*`, `sd.*`, `kitt.*`, `purr.info()` |
| `.paws` | Compiled userland | `win.*`, `sd.*` |
| `.claw` | Compiled kernel-access | `win.*`, `sd.*`, `kitt.*`, `purr.*` |
| `.catt` | In-house exclusive | Full kernel — same as `.claw`, precompiled by the PURR OS team |

---

## Output Directory — `cattobaked/`

All three tools write here. The layout:

```
cattobaked/
├── <device>/              ← purrstrap output (one dir per device)
│   ├── firmware.bin
│   ├── bootloader.bin
│   ├── partition-table.bin
│   ├── purr_kernel.bin
│   └── build.json
├── modules/               ← modulestrap: system module blobs
│   ├── miniwin.purr
│   ├── app_manager.purr
│   └── …
├── drivers/               ← modulestrap: driver blobs
│   ├── display/
│   │   └── ili9341.purr
│   ├── touch/
│   └── …
└── apps/                  ← catstrap: app packages
    ├── magicmac.catt
    ├── magidos.catt
    ├── myapp.paws
    └── myscript.meow
```

---

## Final Binary Assembly — Who Does It?

**`purrstrap`** is responsible for the final image. The assembly flow:

1. `purrstrap build <device>` reads `source/devices/<device>/device.pcat`
2. Invokes `modulestrap build all` to compile all `.purr` blobs
3. Reads the `[flash]` section of `device.pcat` to get the list of modules to bake in (with priority levels)
4. Copies those `.purr` blobs into a staging directory (`cattobaked/<device>/spiffs_staging/`)
5. Runs `spiffsgen.py` (from IDF) to produce `cattobaked/<device>/flash.bin` — a SPIFFS filesystem image containing the core modules
6. Calls `idf.py build` to compile the kernel spine → `firmware.bin` *(IDF wiring step pending)*
7. Merges into a single flashable image with `esptool merge_bin`

**Two separate binaries are flashed:**
- `firmware.bin` — kernel spine + bootloader + partition table (from IDF)
- `flash.bin` — SPIFFS partition containing core `.purr` modules and drivers (from purrstrap)

`purrstrap flash <device>` flashes `flash.bin` to the SPIFFS partition offset. The IDF firmware is flashed separately until the IDF build step is wired in.

Apps (`.paws`/`.claw`/`.catt`/`.meow`) are **never** in the flash image. They live on SD or in a separate app partition loaded by app_manager at runtime.

See [docs/10_ModuleLoading.md](10_ModuleLoading.md) for the full priority system and SD fallback details.

---

## master script — `purr.py` / `purr.sh` / `purr.ps1`

All three launchers at the repo root are thin wrappers around `purr.py`:

| File | Platform |
|---|---|
| `purr.py` | Python directly — all platforms |
| `purr.sh` | bash — Linux / macOS |
| `purr.ps1` | PowerShell — Windows |

`purr.sh` and `purr.ps1` both `cd` to the repo root before launching, so they work regardless of where your terminal is.

---

## Workspace Config — `.purrstrap`

`purrstrap` saves the last-used device and build metadata to `.purrstrap` at the repo root (JSON). The interactive UI reads this to pre-select the last-used device. It is gitignored.

---

## Environment Requirements

Run `./purr.sh purrstrap doctor` to check your environment. Minimum requirements:

- Python 3.8+
- ESP-IDF v5.x with `IDF_PATH` set and the IDF virtualenv activated
- `idf.py` on `PATH` (or resolvable from `IDF_PATH`)
- `git`

For flashing: `pyserial` installed in the IDF Python env (usually already present).
