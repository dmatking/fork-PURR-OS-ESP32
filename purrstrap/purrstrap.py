#!/usr/bin/env python3
"""
purrstrap — PURR OS final image builder

Reads source/devices/<device>/device.pcat, resolves all driver + module
dependencies, generates glue, calls IDF to compile the kernel spine, then
assembles the final flashable image in cattobaked/<device>/.

Usage:
  purrstrap build <device>       build firmware for device
  purrstrap flash <device> [-p]  build + flash to connected device
  purrstrap clean <device>       remove build artifacts for device
  purrstrap list                 list supported devices (reads source/devices/)
  purrstrap status               show current .purrstrap workspace config
  purrstrap doctor               check environment health (IDF, tools present)

Output: cattobaked/<device>/
  firmware.bin          complete merged flash image
  bootloader.bin
  partition-table.bin
  purr_kernel.bin       kernel spine only
  build.json            build metadata (device, versions, timestamp)
"""

import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys

os.system("")  # enable ANSI on Windows

C_RST  = "\033[0m"
C_BOLD = "\033[1m"
C_GRY  = "\033[90m"
C_RED  = "\033[91m"
C_GRN  = "\033[92m"
C_YLW  = "\033[93m"
C_CYN  = "\033[96m"
C_WHT  = "\033[97m"

PURROS_VERSION = "0.12.1"
KITT_VERSION   = "0.9.1"

def info(msg):        print(f"{C_GRN}[purrstrap]{C_RST} {msg}")
def warn(msg):        print(f"{C_YLW}[warn]     {C_RST} {msg}")
def die(msg, code=1): print(f"{C_RED}[err]      {C_RST} {msg}", file=sys.stderr); sys.exit(code)
def div(label=""):
    if label:
        line = f"─ {label} " + "─" * max(0, 52 - len(label) - 2)
    else:
        line = "─" * 52
    print(f"{C_GRY}{line}{C_RST}")

REPO_DIR    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_DIR  = os.path.join(REPO_DIR, "source")
DEVICES_DIR = os.path.join(SOURCE_DIR, "devices")
KERNEL_DIR  = os.path.join(SOURCE_DIR, "kernel")
OUTPUT_DIR  = os.path.join(REPO_DIR, "cattobaked")
CFG_FILE    = os.path.join(REPO_DIR, ".purrstrap")

# ── .pcat parser (minimal TOML-subset) ───────────────────────────────────────

def parse_pcat(path):
    """Parse a .pcat file into a flat dict. Handles [sections] and key=value."""
    result = {}
    section = ""
    try:
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"): continue
                if line.startswith("["):
                    section = line.strip("[]").strip()
                    continue
                if "=" in line:
                    k, _, v = line.partition("=")
                    key = f"{section}.{k.strip()}" if section else k.strip()
                    result[key] = v.strip().strip('"')
    except FileNotFoundError:
        pass
    return result

# ── Device listing ────────────────────────────────────────────────────────────

def list_devices():
    if not os.path.isdir(DEVICES_DIR):
        die(f"source/devices/ not found — run from repo root or check SOURCE_DIR")
    devices = []
    for entry in sorted(os.listdir(DEVICES_DIR)):
        pcat = os.path.join(DEVICES_DIR, entry, "device.pcat")
        if os.path.isfile(pcat):
            cfg = parse_pcat(pcat)
            chip = cfg.get("device.chip", "?")
            name = cfg.get("device.name", entry)
            devices.append((entry, name, chip))
    return devices

def cmd_list(args):
    div()
    print(f"{C_BOLD}Supported devices{C_RST}")
    div()
    for slug, name, chip in list_devices():
        print(f"  {C_CYN}{slug:<20}{C_RST}  {name}  ({chip})")
    div()

# ── Status ────────────────────────────────────────────────────────────────────

def cmd_status(args):
    if not os.path.isfile(CFG_FILE):
        warn("no .purrstrap workspace config found — run 'purrstrap build <device>' to create one")
        return
    with open(CFG_FILE) as f:
        cfg = json.load(f)
    div()
    print(f"{C_BOLD}purrstrap workspace{C_RST}")
    div()
    for k, v in cfg.items():
        print(f"  {C_CYN}{k:<20}{C_RST}  {v}")
    div()

# ── Doctor ────────────────────────────────────────────────────────────────────

def cmd_doctor(args):
    div()
    print(f"{C_BOLD}purrstrap doctor{C_RST}")
    div()
    checks = [
        ("idf.py",        "IDF build tool"),
        ("esptool.py",    "esptool (flash)"),
        ("python3",       "Python 3"),
        ("git",           "Git"),
    ]
    all_ok = True
    for tool, desc in checks:
        found = shutil.which(tool)
        if found:
            print(f"  {C_GRN}[OK]{C_RST}  {desc:<20}  {found}")
        else:
            print(f"  {C_RED}[XX]{C_RST}  {desc:<20}  not found")
            all_ok = False
    # Check IDF_PATH
    idf_path = os.environ.get("IDF_PATH", "")
    if idf_path and os.path.isdir(idf_path):
        print(f"  {C_GRN}[OK]{C_RST}  IDF_PATH              {idf_path}")
    else:
        print(f"  {C_RED}[XX]{C_RST}  IDF_PATH              not set or invalid")
        all_ok = False
    # Check spiffsgen
    idf = _idf_path()
    sg = _spiffsgen(idf) if idf else None
    if sg:
        print(f"  {C_GRN}[OK]{C_RST}  spiffsgen             {sg}")
    else:
        print(f"  {C_YLW}[--]{C_RST}  spiffsgen             not found (IDF_PATH required)")
        all_ok = False
    # Check source tree
    for rel in ["source/kernel/core/boot.c", "source/kernel/core/purr_kernel.h"]:
        p = os.path.join(REPO_DIR, rel)
        if os.path.isfile(p):
            print(f"  {C_GRN}[OK]{C_RST}  {rel}")
        else:
            print(f"  {C_RED}[XX]{C_RST}  {rel}  — missing")
            all_ok = False
    div()
    if all_ok:
        info("all checks passed")
    else:
        warn("some checks failed — fix above before building")

# ── Build helpers ─────────────────────────────────────────────────────────────

def resolve_device(device_slug):
    pcat_path = os.path.join(DEVICES_DIR, device_slug, "device.pcat")
    if not os.path.isfile(pcat_path):
        die(f"no device.pcat found for '{device_slug}' — check source/devices/")
    return parse_pcat(pcat_path), pcat_path

def _idf_path():
    p = os.environ.get("IDF_PATH", "")
    if p and os.path.isdir(p):
        return p
    for c in (os.path.expanduser("~/esp/idf"), os.path.expanduser("~/esp/esp-idf"), "/opt/esp-idf"):
        if os.path.isdir(c):
            os.environ["IDF_PATH"] = c
            return c
    return ""

def _spiffsgen(idf_path):
    candidate = os.path.join(idf_path, "components", "spiffs", "spiffsgen.py")
    if os.path.isfile(candidate):
        return candidate
    return None

def run_live(cmd, cwd=None):
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding="utf-8", errors="replace")
    try:
        for line in proc.stdout:
            print(line, end="", flush=True)
        proc.wait()
    except KeyboardInterrupt:
        proc.terminate(); proc.wait()
        warn("Interrupted."); sys.exit(0)
    return proc.returncode

# ── Parse [flash] section from device.pcat ────────────────────────────────────
#
# Returns list of (module_slug, priority) tuples.
# module_slug is like "miniwin", "display/st7789", etc.
# These map to cattobaked/modules/<name>.purr or cattobaked/drivers/<type>/<name>.purr

def parse_flash_manifest(cfg):
    """
    Read all 'flash.*' keys from a parsed device.pcat and return a list of
    (slug, priority) tuples that should be baked into the SPIFFS image.
    """
    entries = []
    for key, val in cfg.items():
        if not key.startswith("flash."): continue
        slug = key[len("flash."):]        # e.g. "miniwin", "display/st7789"
        try:
            priority = int(val)
        except ValueError:
            priority = 3
        entries.append((slug, priority))
    return entries

def _find_purr_blob(slug):
    """
    Find the compiled blob for a given slug in cattobaked/.
    Slug can be:
      "miniwin"         → cattobaked/modules/miniwin.purr
      "app_manager"     → cattobaked/modules/app_manager.purr
      "display/st7789"  → cattobaked/drivers/display/st7789.purr
      "apps/terminal"   → cattobaked/apps/terminal.claw (or .paws or .meow)
    """
    parts = slug.split("/")
    if len(parts) == 2 and parts[0] == "apps":
        name = parts[1]
        app_dir = os.path.join(OUTPUT_DIR, "apps")
        for ext in ("claw", "paws", "meow"):
            p = os.path.join(app_dir, f"{name}.{ext}")
            if os.path.isfile(p):
                return p
        # Also accept the .meta.json registration as a stand-in (no binary yet)
        meta = os.path.join(app_dir, f"{name}.claw.meta.json")
        if not os.path.isfile(meta):
            meta = os.path.join(app_dir, f"{name}.paws.meta.json")
        if os.path.isfile(meta):
            return meta   # caller will handle .meta.json → stage as placeholder
        return None
    if len(parts) == 1:
        # module
        p = os.path.join(OUTPUT_DIR, "modules", f"{slug}.purr")
        if os.path.isfile(p):
            return p
    else:
        # driver: parts[0]=type, parts[1]=name
        p = os.path.join(OUTPUT_DIR, "drivers", parts[0], f"{parts[1]}.purr")
        if os.path.isfile(p):
            return p
    return None

# ── SPIFFS staging + image generation ────────────────────────────────────────

def build_flash_image(device, pcat_cfg, out_dir, spiffs_size_kb=512):
    """
    1. Run modulestrap to build all .purr blobs.
    2. Read [flash] section from device.pcat to find which blobs to include.
    3. Copy blobs into staging dir (preserving modules/ vs drivers/<type>/ layout).
    4. Run spiffsgen.py to produce flash.bin.
    Returns path to flash.bin or None on failure.
    """
    MODULESTRAP_PY = os.path.join(REPO_DIR, "modulestrap", "modulestrap.py")
    staging_dir    = os.path.join(out_dir, "spiffs_staging")
    flash_bin      = os.path.join(out_dir, "flash.bin")

    # ── Step 1: build all .purr blobs + register all apps ─────────────────────
    info("invoking modulestrap to build .purr blobs...")
    rc = run_live([sys.executable, MODULESTRAP_PY, "build", "all"])
    if rc != 0:
        warn("modulestrap exited with errors — some modules may be missing")

    CATSTRAP_PY = os.path.join(REPO_DIR, "catstrap", "catstrap.py")
    info("invoking catstrap to register apps...")
    rc = run_live([sys.executable, CATSTRAP_PY, "build", "all"])
    if rc != 0:
        warn("catstrap exited with errors — some apps may be missing")

    # ── Step 2: read flash manifest ────────────────────────────────────────────
    flash_entries = parse_flash_manifest(pcat_cfg)
    if not flash_entries:
        warn("no [flash] section in device.pcat — flash.bin will be empty filesystem")

    # ── Step 3: stage files ────────────────────────────────────────────────────
    if os.path.isdir(staging_dir):
        shutil.rmtree(staging_dir)
    os.makedirs(os.path.join(staging_dir, "modules"),  exist_ok=True)
    os.makedirs(os.path.join(staging_dir, "drivers"),  exist_ok=True)
    os.makedirs(os.path.join(staging_dir, "apps"),     exist_ok=True)

    staged = 0
    missing_required = []

    for slug, priority in sorted(flash_entries, key=lambda x: x[1]):
        src = _find_purr_blob(slug)
        prio_str = f"P{priority}"

        if src is None:
            msg = f"{prio_str} {slug} — blob not found in cattobaked/"
            if priority == 1:
                missing_required.append(slug)
                print(f"  {C_RED}[XX]{C_RST}  {msg}")
            else:
                print(f"  {C_YLW}[--]{C_RST}  {msg}")
            continue

        # Determine staging destination
        parts = slug.split("/")
        if len(parts) == 2 and parts[0] == "apps":
            name = parts[1]
            app_staging = os.path.join(staging_dir, "apps")
            os.makedirs(app_staging, exist_ok=True)
            if src.endswith(".meta.json"):
                # Pre-linked app — registered in firmware, copy manifest as marker
                dst = os.path.join(app_staging, f"{name}.meta.json")
            else:
                ext = os.path.splitext(src)[1]
                dst = os.path.join(app_staging, f"{name}{ext}")
        elif len(parts) == 1:
            dst = os.path.join(staging_dir, "modules", f"{slug}.purr")
        else:
            type_dir = os.path.join(staging_dir, "drivers", parts[0])
            os.makedirs(type_dir, exist_ok=True)
            dst = os.path.join(type_dir, f"{parts[1]}.purr")

        shutil.copy2(src, dst)
        size_kb = os.path.getsize(src) // 1024
        print(f"  {C_GRN}[OK]{C_RST}  {prio_str} {slug:<30}  {size_kb} KB")
        staged += 1

    if missing_required:
        warn(f"{len(missing_required)} REQUIRED (P1) module(s) missing from build:")
        for s in missing_required:
            warn(f"  {s}")
        warn("The kernel will panic at boot if these are not available on SD card.")

    info(f"staged {staged}/{len(flash_entries)} modules into spiffs_staging/")

    # ── Step 4: run spiffsgen.py ───────────────────────────────────────────────
    idf = _idf_path()
    spiffsgen = _spiffsgen(idf) if idf else None

    if not spiffsgen:
        warn("spiffsgen.py not found — cannot produce flash.bin")
        warn("Set IDF_PATH and activate the IDF environment, then rebuild.")
        return None

    spiffs_size_bytes = spiffs_size_kb * 1024
    cmd = [sys.executable, spiffsgen, str(spiffs_size_bytes), staging_dir, flash_bin]
    info(f"running spiffsgen ({spiffs_size_kb} KB)...")
    rc = run_live(cmd)
    if rc != 0:
        warn(f"spiffsgen exited {rc} — flash.bin may be corrupt or missing")
        return None

    size_kb = os.path.getsize(flash_bin) // 1024
    info(f"flash.bin ready  ({size_kb} KB)  →  {os.path.relpath(flash_bin, REPO_DIR)}")
    return flash_bin

# ── Glue layer generation ─────────────────────────────────────────────────────
#
# Generates purr_device_glue.c for the target device — a thin C file that
# #includes the right driver headers and provides a purr_device_init() that
# configures pin numbers from device.pcat before calling purr_kernel_scan_modules().

def _generate_glue(device, cfg, out_dir):
    """Write source/glue/purr_device_glue_<device>.c from device.pcat [pins] section."""
    glue_dir = os.path.join(out_dir, "glue")
    os.makedirs(glue_dir, exist_ok=True)
    glue_path = os.path.join(glue_dir, "purr_device_glue.c")

    chip        = cfg.get("device.chip", "esp32")
    display_drv = cfg.get("drivers.display", "")
    touch_drv   = cfg.get("drivers.touch", "")
    radio_drv   = cfg.get("drivers.radio", "")
    gps_drv     = cfg.get("drivers.gps", "")
    input_drv   = cfg.get("drivers.input", "")

    def pin(key, default=-1):
        val = cfg.get(f"pins.{key}", str(default))
        try:    return int(val)
        except: return default

    lines = [
        f"// purr_device_glue.c — auto-generated by purrstrap for {device}",
        f"// Device: {cfg.get('device.name', device)}  Chip: {chip}",
        f"// Do not edit — regenerated on every purrstrap build.",
        "",
        '#include "purr_kernel.h"',
        '#include "purr_module.h"',
        "",
    ]

    # Pin #defines from [pins] section
    pin_map = {
        "CONFIG_DRV_DISPLAY_CS_PIN":    pin("display_cs"),
        "CONFIG_DRV_DISPLAY_DC_PIN":    pin("display_dc"),
        "CONFIG_DRV_DISPLAY_MOSI_PIN":  pin("display_mosi"),
        "CONFIG_DRV_DISPLAY_SCLK_PIN":  pin("display_sclk"),
        "CONFIG_DRV_DISPLAY_RST_PIN":   pin("display_rst"),
        "CONFIG_DRV_DISPLAY_BL_PIN":    pin("display_bl"),
        "CONFIG_DRV_TOUCH_SDA_PIN":     pin("touch_sda"),
        "CONFIG_DRV_TOUCH_SCL_PIN":     pin("touch_scl"),
        "CONFIG_DRV_TOUCH_INT_PIN":     pin("touch_int", 0xFF),
        "CONFIG_DRV_TOUCH_RST_PIN":     pin("touch_rst", 0xFF),
        "CONFIG_GPS_TX_PIN":            pin("gps_tx", 43),
        "CONFIG_GPS_RX_PIN":            pin("gps_rx", 44),
        "CONFIG_LORA_MOSI_PIN":         pin("lora_mosi"),
        "CONFIG_LORA_MISO_PIN":         pin("lora_miso"),
        "CONFIG_LORA_SCLK_PIN":         pin("lora_sclk"),
        "CONFIG_LORA_CS_PIN":           pin("lora_cs"),
        "CONFIG_LORA_RST_PIN":          pin("lora_rst"),
        "CONFIG_LORA_IRQ_PIN":          pin("lora_irq"),
    }
    for macro, val in pin_map.items():
        lines.append(f"#define {macro} {val}")

    # Radio capability flags from [radio] section
    def radio_bool(key):
        return cfg.get(f"radio.{key}", "false").lower() in ("true", "1", "yes")

    lines += [""]
    lines.append(f"#define CONFIG_PURR_WIFI  {'1' if radio_bool('wifi') else '0'}")
    lines.append(f"#define CONFIG_PURR_BT    {'1' if radio_bool('bt')   else '0'}")
    lora_drv = cfg.get("radio.lora", "")
    lines.append(f"#define CONFIG_PURR_LORA  {'1' if lora_drv else '0'}")
    if lora_drv:
        lines.append(f'#define CONFIG_PURR_LORA_DRIVER "{lora_drv}"')

    lines += [
        "",
        "// Called from boot.c before purr_kernel_scan_modules().",
        "void purr_device_init(void) {",
        f"    // Device: {device}",
        "}",
        "",
        "// Flash and SD module directories for this device.",
        'const char *purr_flash_module_dir  = "/flash/modules";',
        'const char *purr_flash_driver_dir  = "/flash/drivers";',
        'const char *purr_flash_app_dir     = "/flash/apps";',
        'const char *purr_sd_module_dir     = "/sdcard/modules";',
        'const char *purr_sd_driver_dir     = "/sdcard/drivers";',
        'const char *purr_sd_app_dir        = "/sdcard/apps";',
    ]

    with open(glue_path, "w") as f:
        f.write("\n".join(lines) + "\n")

    info(f"  glue layer → {os.path.relpath(glue_path, REPO_DIR)}")
    return glue_path

# ── IDF kernel spine build ────────────────────────────────────────────────────

def _build_kernel_spine(device, cfg, out_dir):
    """
    Run idf.py build on the CoreOS kernel spine for the target device.
    Returns path to firmware.bin on success, None on failure or unavailability.
    """
    idf = _idf_path()
    if not idf:
        warn("IDF_PATH not set — skipping kernel spine build")
        return None

    coreos_dir = os.path.join(REPO_DIR, "CoreOS")
    if not os.path.isdir(coreos_dir):
        warn(f"CoreOS/ not found at {coreos_dir} — skipping IDF build")
        return None

    chip = cfg.get("device.chip", "esp32")
    valid_chips = ("esp32", "esp32s3", "esp32c3", "esp32s2", "esp32h2")
    if chip not in valid_chips:
        chip = "esp32"

    idf_py = os.path.join(idf, "tools", "idf.py")
    if not os.path.isfile(idf_py):
        idf_py = "idf.py"

    env = os.environ.copy()
    env["IDF_PATH"] = idf
    env["PURR_DEVICE"] = device

    # Chain sdkconfig: base defaults + device overrides (IDF v5 supports semicolons)
    sdkconfig_base   = os.path.join(coreos_dir, "sdkconfig.defaults")
    sdkconfig_device = os.path.join(coreos_dir, f"sdkconfig_{device}")
    if os.path.isfile(sdkconfig_base) and os.path.isfile(sdkconfig_device):
        env["SDKCONFIG_DEFAULTS"] = f"{sdkconfig_base};{sdkconfig_device}"
    elif os.path.isfile(sdkconfig_device):
        env["SDKCONFIG_DEFAULTS"] = sdkconfig_device
    elif os.path.isfile(sdkconfig_base):
        env["SDKCONFIG_DEFAULTS"] = sdkconfig_base

    build_dir = os.path.join(coreos_dir, f"build_{device}")
    firmware_out = os.path.join(out_dir, "firmware.bin")
    bootloader_out = os.path.join(out_dir, "bootloader.bin")
    partitions_out = os.path.join(out_dir, "partition-table.bin")

    div("kernel spine")
    info(f"  idf.py set-target {chip}")
    rc = run_live([sys.executable, idf_py, "-C", coreos_dir, "-B", build_dir,
                   "set-target", chip], env=env)
    if rc != 0:
        warn(f"  set-target failed (rc={rc})")
        return None

    info(f"  idf.py build")
    rc = run_live([sys.executable, idf_py, "-C", coreos_dir, "-B", build_dir,
                   "build"], env=env)
    if rc != 0:
        warn(f"  idf.py build failed (rc={rc})")
        return None

    # Copy binaries to out_dir
    import shutil as _sh
    for src_name, dst in (
        (f"CoreOS.bin",         firmware_out),
        ("bootloader/bootloader.bin", bootloader_out),
        ("partition_table/partition-table.bin", partitions_out),
    ):
        src = os.path.join(build_dir, src_name)
        if os.path.isfile(src):
            _sh.copy2(src, dst)
            info(f"  {C_GRN}OK{C_RST}  {os.path.basename(dst)}")
        else:
            warn(f"  not found: {src_name}")

    return firmware_out if os.path.isfile(firmware_out) else None

def _merge_flash_image(device, cfg, out_dir, firmware_bin, flash_bin):
    """
    Use esptool merge_bin to combine firmware + SPIFFS into one flashable image.
    Output: out_dir/PURR_OS_<device>.bin
    """
    spiffs_offset = cfg.get("device.spiffs_offset", "0x290000")
    merged_out    = os.path.join(out_dir, f"PURR_OS_{device}.bin")
    bootloader    = os.path.join(out_dir, "bootloader.bin")
    partitions    = os.path.join(out_dir, "partition-table.bin")

    parts = []
    if os.path.isfile(bootloader):   parts += ["0x1000",         bootloader]
    if os.path.isfile(partitions):   parts += ["0x8000",         partitions]
    parts += ["0x10000", firmware_bin]
    parts += [spiffs_offset, flash_bin]

    cmd = ["esptool.py", "--chip", cfg.get("device.chip", "esp32"),
           "merge_bin", "-o", merged_out] + parts

    div("merge")
    info(f"  merging final image → {os.path.relpath(merged_out, REPO_DIR)}")
    rc = run_live(cmd)
    if rc == 0:
        size_kb = os.path.getsize(merged_out) // 1024
        info(f"  {C_GRN}PURR_OS_{device}.bin{C_RST}  {size_kb} KB")
    else:
        warn(f"  merge_bin failed (rc={rc})")
    return merged_out if os.path.isfile(merged_out) else None

# ── Build ─────────────────────────────────────────────────────────────────────

def cmd_build(args):
    device = args.device
    cfg, pcat_path = resolve_device(device)

    chip    = cfg.get("device.chip", "esp32")
    name    = cfg.get("device.name", device)
    out_dir = os.path.join(OUTPUT_DIR, device)

    div()
    info(f"building {name} ({chip})")
    info(f"output → cattobaked/{device}/")
    div()

    os.makedirs(out_dir, exist_ok=True)

    # Save workspace config
    workspace = {
        "device":         device,
        "chip":           chip,
        "purros_version": PURROS_VERSION,
        "kitt_version":   KITT_VERSION,
        "last_build":     datetime.datetime.now().isoformat(),
    }
    with open(CFG_FILE, "w") as f:
        json.dump(workspace, f, indent=2)

    # ── Generate device glue layer ─────────────────────────────────────────────
    _generate_glue(device, cfg, out_dir)

    # ── SPIFFS flash image (modules + drivers baked in) ────────────────────────
    # Determine SPIFFS partition size from device.pcat (default 512 KB)
    spiffs_kb = int(cfg.get("device.spiffs_kb", "512"))
    flash_bin = build_flash_image(device, cfg, out_dir, spiffs_size_kb=spiffs_kb)

    div()

    # ── Kernel spine (IDF build) ───────────────────────────────────────────────
    firmware_bin = _build_kernel_spine(device, cfg, out_dir)

    # ── Merge final image ──────────────────────────────────────────────────────
    if firmware_bin and flash_bin:
        _merge_flash_image(device, cfg, out_dir, firmware_bin, flash_bin)
    elif not firmware_bin:
        warn("kernel spine not built — IDF unavailable or build failed")
        warn("SPIFFS flash.bin is ready; run IDF manually then merge with esptool merge_bin")

    # Write build metadata
    meta = {
        **workspace,
        "pcat":         pcat_path,
        "flash_bin":    flash_bin or "not built",
        "firmware_bin": firmware_bin or "not built",
        "spiffs_kb":    spiffs_kb,
    }
    with open(os.path.join(out_dir, "build.json"), "w") as f:
        json.dump(meta, f, indent=2)

    info(f"build.json written")
    div()

def cmd_flash(args):
    cmd_build(args)
    port = getattr(args, "port", None) or "auto"
    out_dir   = os.path.join(OUTPUT_DIR, args.device)
    flash_bin = os.path.join(out_dir, "flash.bin")
    div()
    info(f"flash target: {args.device}  port: {port}")

    # Determine flash offset for SPIFFS partition (device-specific, from partition table)
    # Default offset matches the common 4MB layout: 0x290000
    cfg, _ = resolve_device(args.device)
    spiffs_offset = cfg.get("device.spiffs_offset", "0x290000")

    idf = _idf_path()
    if not idf:
        warn("IDF_PATH not set — cannot flash")
        return

    # Flash SPIFFS image only (firmware.bin flashed separately after IDF build)
    if os.path.isfile(flash_bin):
        cmd = [
            "esptool.py",
            "--port", port if port != "auto" else "/dev/ttyUSB0",
            "--baud", "460800",
            "write_flash",
            spiffs_offset, flash_bin,
        ]
        info(f"flashing flash.bin to {spiffs_offset}...")
        run_live(cmd)
    else:
        warn("flash.bin not built — run purrstrap build first")

def cmd_clean(args):
    device = args.device
    build_dir = os.path.join(REPO_DIR, "source", f"build_{device}")
    out_dir   = os.path.join(OUTPUT_DIR, device)
    for d in [build_dir, out_dir]:
        if os.path.isdir(d):
            shutil.rmtree(d)
            info(f"removed {d}")
        else:
            warn(f"nothing to clean: {d}")

# ── Bake ──────────────────────────────────────────────────────────────────────

RELEASES_DIR = os.path.join(REPO_DIR, "releases")

def cmd_bake(args):
    """
    Build all devices and copy outputs to releases/v<version>/.
    Creates a manifest.json with per-device build results.
    """
    devices = list_devices()
    if not devices:
        die("no devices found in source/devices/")

    release_dir = os.path.join(RELEASES_DIR, f"v{PURROS_VERSION}")
    os.makedirs(release_dir, exist_ok=True)

    div()
    info(f"baking PURR OS v{PURROS_VERSION} — {len(devices)} device(s)")
    info(f"output → releases/v{PURROS_VERSION}/")
    div()

    manifest = {
        "purros_version": PURROS_VERSION,
        "kitt_version":   KITT_VERSION,
        "baked_at":       datetime.datetime.now().isoformat(),
        "devices":        {},
    }

    ok_count = 0
    for slug, name, chip in devices:
        div(slug)

        # Reuse cmd_build logic
        class _Args: device = slug
        try:
            cmd_build(_Args())
        except SystemExit as e:
            manifest["devices"][slug] = {"status": "error", "error": str(e)}
            continue

        out_dir  = os.path.join(OUTPUT_DIR, slug)
        dest_dir = os.path.join(release_dir, slug)
        os.makedirs(dest_dir, exist_ok=True)

        # Copy artifacts
        copied = []
        for fname in ("flash.bin", "build.json", "firmware.bin"):
            src = os.path.join(out_dir, fname)
            if os.path.isfile(src):
                shutil.copy2(src, os.path.join(dest_dir, fname))
                copied.append(fname)

        # Copy spiffs_staging manifest if present
        staging_meta = os.path.join(out_dir, "spiffs_staging")
        if os.path.isdir(staging_meta):
            # Just record what was staged, don't copy the whole tree
            staged = []
            for root, _, files in os.walk(staging_meta):
                for f in files:
                    if f.endswith(".purr"):
                        rel = os.path.relpath(os.path.join(root, f), staging_meta)
                        staged.append(rel)
            manifest["devices"][slug] = {
                "status":  "ok",
                "chip":    chip,
                "name":    name,
                "copied":  copied,
                "staged":  staged,
            }
        else:
            manifest["devices"][slug] = {
                "status": "partial",
                "chip":   chip,
                "name":   name,
                "copied": copied,
            }

        status = f"{C_GRN}OK{C_RST}" if "flash.bin" in copied else f"{C_YLW}partial{C_RST}"
        info(f"  {name} ({chip})  [{status}]  files: {', '.join(copied) or 'none'}")
        ok_count += 1

    # Write manifest
    manifest_path = os.path.join(release_dir, "manifest.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)

    div()
    info(f"bake complete: {ok_count}/{len(devices)} devices")
    info(f"manifest: releases/v{PURROS_VERSION}/manifest.json")
    div()

# ── CLI ───────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(prog="purrstrap", description="PURR OS final image builder")
    sub = parser.add_subparsers(dest="cmd")

    p_build = sub.add_parser("build",  help="Build firmware for a device")
    p_build.add_argument("device")

    p_flash = sub.add_parser("flash",  help="Build + flash to device")
    p_flash.add_argument("device")
    p_flash.add_argument("-p", "--port", default=None)

    p_clean = sub.add_parser("clean",  help="Remove build artifacts")
    p_clean.add_argument("device")

    sub.add_parser("bake",   help=f"Build all devices → releases/v{PURROS_VERSION}/")
    sub.add_parser("list",   help="List supported devices")
    sub.add_parser("status", help="Show workspace config")
    sub.add_parser("doctor", help="Check environment health")

    args = parser.parse_args()
    dispatch = {
        "build":  cmd_build,
        "flash":  cmd_flash,
        "clean":  cmd_clean,
        "bake":   cmd_bake,
        "list":   cmd_list,
        "status": cmd_status,
        "doctor": cmd_doctor,
    }
    if args.cmd not in dispatch:
        parser.print_help()
        sys.exit(0)
    dispatch[args.cmd](args)

if __name__ == "__main__":
    main()
