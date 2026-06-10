#!/usr/bin/env python3
"""PURR OS SDK v0.9.5 — interactive build / flash / monitor tool.
Invoked by SDK.ps1 (or directly: python sdk_core.py [flags]).
"""

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys

# ── ANSI colours (enable VT100 on Windows) ───────────────────────────────────
os.system("")
C_RST  = "\033[0m"
C_BOLD = "\033[1m"
C_GRY  = "\033[90m"
C_RED  = "\033[91m"
C_GRN  = "\033[92m"
C_YLW  = "\033[93m"
C_CYN  = "\033[96m"
C_WHT  = "\033[97m"

PURROS_VERSION = "0.9.5"
KITT_VERSION   = "0.6.0"

def info(msg):            print(f"{C_GRN}[purr]{C_RST} {msg}")
def warn(msg):            print(f"{C_YLW}[warn]{C_RST} {msg}")
def err(msg, code=1):     print(f"{C_RED}[err] {C_RST} {msg}", file=sys.stderr); sys.exit(code)
def div():                print(f"{C_GRY}" + "─" * 44 + C_RST)
def header(msg):          print(f"\n{C_WHT}{C_BOLD}  {msg}{C_RST}\n")

# ── Paths ─────────────────────────────────────────────────────────────────────
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR   = os.path.dirname(SCRIPT_DIR)
COREOS_DIR = os.path.join(REPO_DIR, "CoreOS")
LORA_DIR   = os.path.join(REPO_DIR, "LoRa Kernels")
CFG_FILE   = os.path.join(SCRIPT_DIR, "purr_sdk.cfg")


# ── Serial port scanner ───────────────────────────────────────────────────────

# USB VID/PID pairs common on ESP32 dev boards
_ESP32_VIDS = {
    0x1A86,   # CH340 / CH341 (most CYD boards)
    0x10C4,   # CP210x (Silicon Labs)
    0x0403,   # FTDI
    0x303A,   # Espressif native USB (ESP32-S3)
    0x239A,   # Adafruit
}

def _scan_serial_ports():
    """Return list of (port, description) for likely ESP32 serial devices."""
    results = []

    # Try pyserial first — gives VID/PID + friendly description
    try:
        from serial.tools import list_ports
        for p in list_ports.comports():
            vid = p.vid if hasattr(p, "vid") else None
            if vid in _ESP32_VIDS or (p.device and (
                    "USB" in (p.description or "").upper() or
                    "CH340" in (p.description or "").upper() or
                    "CP210" in (p.description or "").upper() or
                    "FTDI"  in (p.description or "").upper())):
                results.append((p.device, p.description or ""))
    except ImportError:
        pass

    # Fallback: glob /dev/tty* patterns on Linux (USB, ACM, AMC/Qualcomm)
    if not results and sys.platform.startswith("linux"):
        for pattern in ("/dev/ttyUSB*", "/dev/ttyACM*", "/dev/ttyAMC*"):
            for dev in sorted(glob.glob(pattern)):
                if dev not in [r[0] for r in results]:
                    results.append((dev, ""))

    return results


def _normalize_port(port):
    """Ensure Linux/macOS ports have their /dev/ prefix."""
    if port and sys.platform != "win32" and not port.startswith("/"):
        return "/dev/" + port
    return port


def _pick_port(prompt_label, saved=""):
    """
    Show available serial ports and let the user pick one.
    If exactly one ESP32-looking port is found, offer it as default.
    Accepts 'auto' on CLI to select the first found port without prompting.
    Returns the chosen port string, or "" if skipped.
    """
    ports = _scan_serial_ports()

    if not ports:
        # No devices found — fall back to manual entry
        if sys.platform.startswith("linux"):
            example = "/dev/ttyUSB0 (or /dev/ttyAMC0 for T-Deck Plus)"
        elif sys.platform == "darwin":
            example = "/dev/cu.usbserial-0001"
        else:
            example = "COM8"
        val = input(f"  {prompt_label} (e.g. {example}, blank to skip) [{saved}]: ").strip()
        return val or saved

    # Show discovered ports
    print(f"\n  {C_CYN}Detected serial ports:{C_RST}")
    for i, (dev, desc) in enumerate(ports, 1):
        tag = f"  {C_GRY}{desc}{C_RST}" if desc else ""
        print(f"  [{i}] {dev}{tag}")

    default_port = ports[0][0] if len(ports) == 1 else (saved if saved in [p[0] for p in ports] else ports[0][0])
    default_label = f"{default_port}" if default_port else ""

    raw = input(f"\n  {prompt_label} — number or path (Enter = {default_label}, blank to skip): ").strip()

    if not raw:
        return default_port
    if raw.isdigit():
        idx = int(raw) - 1
        if 0 <= idx < len(ports):
            return ports[idx][0]
        warn(f"Invalid choice — using {default_port}")
        return default_port
    return _normalize_port(raw)


def _idf_py():
    """Return the idf.py invocation as a list using the IDF virtualenv python."""
    idf_path = _resolve_idf_path()
    if idf_path:
        candidate = os.path.join(idf_path, "tools", "idf.py")
        if os.path.exists(candidate):
            # Prefer the IDF virtualenv python (has click, pyparsing, etc.).
            # Falls back to sys.executable if no venv found (export.sh already sourced).
            import glob as _glob
            pattern = os.path.join(
                os.path.expanduser("~"), ".espressif",
                "python_env", "idf*", "bin", "python"
            )
            matches = sorted(_glob.glob(pattern), reverse=True)
            python = matches[0] if matches else sys.executable
            return [python, candidate]
    return ["idf.py"]


# ── Per-target build directories ──────────────────────────────────────────────
# Each target gets its own build dir and sdkconfig so builds never contaminate
# each other. Switching targets no longer requires a clean.

_BUILD_DIRS = {
    "heltec":          "build_heltec",
    "tembed_cc1101":   "build_tembed_cc1101",
    "cyd":             "build_cyd",
    "cyd_s028r":       "build_cyd_s028r",
    "cyd_s024c":       "build_cyd_s024c",
    "cyd_boot":        "build_cyd_boot",
    "tdeck":           "build_tdeck",
    "tdeck_plus":      "build_tdeck_plus",
    "jc3248w535":      "build_jc3248w535",
    "waveshare169":    "build_waveshare169",
}

def _build_dir(cfg):
    name = _BUILD_DIRS.get(cfg["target"], f"build_{cfg['target']}")
    return os.path.join(COREOS_DIR, name)

def _sdkconfig(cfg):
    return os.path.join(COREOS_DIR, f"sdkconfig_{cfg['target']}")

# ── Target definitions ────────────────────────────────────────────────────────
TARGETS = {
    "heltec": {
        "chip":         "esp32s3",
        "desc":         "Heltec WiFi LoRa 32 V3",
        "spec":         "ESP32-S3  8MB  SSD1306  SX1262 LoRa  KittenUI (auto)",
        "shells":       ["kitten_ui"],
        "default_lora": True,
        "default_ui":   "none",
        "fixed":        False,
    },
    "tembed_cc1101": {
        "chip":         "esp32s3",
        "desc":         "LilyGo T-Embed CC1101",
        "spec":         "ESP32-S3R8  16MB  8MB PSRAM  ST7789 170x320  CC1101 sub-GHz  rotary encoder  KittenUI (auto)",
        "shells":       ["kitten_ui"],
        "default_lora": False,
        "default_ui":   "none",
        "fixed":        False,
    },
    "cyd_s028r": {
        "chip":         "esp32",
        "desc":         "CYD S028R (ESP32-2432S028R, original)",
        "spec":         "ESP32  4MB  ILI9341 2.4\"  XPT2046 SPI touch  MiniWin WM — full OS (ota_0)",
        "shells":       [],
        "default_lora": False,
        "default_ui":   "miniwin",
        "fixed":        False,
    },
    "cyd_s024c": {
        "chip":         "esp32",
        "desc":         "CYD S024C (ESP32-2432S024C, newer)",
        "spec":         "ESP32  4MB  ILI9341 2.4\"  CST816S I2C touch  MiniWin WM — full OS (ota_0)",
        "shells":       [],
        "default_lora": False,
        "default_ui":   "miniwin",
        "fixed":        False,
    },
    "cyd": {
        "chip":         "esp32",
        "desc":         "CYD (alias for S024C)",
        "spec":         "Use cyd_s028r or cyd_s024c instead",
        "shells":       [],
        "default_lora": False,
        "default_ui":   "miniwin",
        "fixed":        False,
    },
    "cyd_boot": {
        "chip":         "esp32",
        "desc":         "CYD PURR Kernel (factory, all variants)",
        "spec":         "ESP32  4MB  ILI9341 2.4\" — factory kernel (OTA-immune, chainloads ota_0)",
        "shells":       [],
        "default_lora": False,
        "default_ui":   "none",
        "fixed":        True,   # no module toggles; always mini
    },
    "tdeck": {
        "chip":         "esp32s3",
        "desc":         "LilyGo T-Deck",
        "spec":         "ESP32-S3  16MB  ST7789  trackball — no touch (WIP)",
        "shells":       ["kitten_ui"],
        "default_lora": True,
        "default_ui":   "none",
        "fixed":        False,
    },
    "tdeck_plus": {
        "chip":         "esp32s3",
        "desc":         "LilyGo T-Deck Plus",
        "spec":         "ESP32-S3  ST7789 320x240  GT911 cap touch  SPI3 MOSI=41 SCK=40  I2C SDA=18 SCL=8",
        "shells":       ["kitten_ui"],
        "default_lora": True,
        "default_ui":   "miniwin",
        "fixed":        False,
    },
    "jc3248w535": {
        "chip":         "esp32s3",
        "desc":         "JC3248W535 3.5\"",
        "spec":         "ESP32-S3  16MB  8MB PSRAM  ST7796 480x320  GT911 I2C SDA=19 SCL=20",
        "shells":       [],
        "default_lora": False,
        "default_ui":   "miniwin",
        "fixed":        False,
    },
    "waveshare169": {
        "chip":         "esp32s3",
        "desc":         "Waveshare 1.69\" ESP32-S3",
        "spec":         "ESP32-S3  4MB  ST7789 240x280  CST816S I2C SDA=6 SCL=7 — verify pins",
        "shells":       [],
        "default_lora": False,
        "default_ui":   "miniwin",
        "fixed":        False,
    },
}

# ── Module definitions ────────────────────────────────────────────────────────
# inverted=True means cmake flag is BUILD_MINI and ON in cfg means the flag = 0
_ALL_MINIWIN = ["cyd", "cyd_s028r", "cyd_s024c", "tdeck_plus", "jc3248w535", "waveshare169"]
_ALL_TARGETS = ["heltec", "tembed_cc1101"] + _ALL_MINIWIN + ["tdeck"]

MODULES = [
    {
        "key":      "wifi",
        "cmake":    "PURR_ENABLE_WIFI",
        "label":    "WiFi",
        "desc":     "wifi_manager — WiFi stack + HTTP server (~150 KB flash)",
        "targets":  _ALL_TARGETS,
        "default":  True,
        "inverted": False,
    },
    {
        "key":      "bt",
        "cmake":    "PURR_ENABLE_BT",
        "label":    "Bluetooth",
        "desc":     "bt_manager — BLE + Classic stack (~200 KB flash)",
        "targets":  _ALL_TARGETS,
        "default":  True,
        "inverted": False,
    },
    {
        "key":      "mtp",
        "cmake":    "PURR_ENABLE_MTP",
        "label":    "MTP USB",
        "desc":     "mtp_manager — USB file transfer",
        "targets":  _ALL_TARGETS,
        "default":  False,
        "inverted": False,
    },
    {
        "key":      "flasher",
        "cmake":    "PURR_ENABLE_FLASHER",
        "label":    "OTA Flasher",
        "desc":     "flasher — OTA partition flasher",
        "targets":  _ALL_TARGETS,
        "default":  False,
        "inverted": False,
    },
    {
        "key":      "lora",
        "cmake":    "PURR_ENABLE_LORA",
        "label":    "LoRa Radio",
        "desc":     "lora_manager — LoRa radio driver",
        "targets":  ["heltec", "tdeck", "tdeck_plus"],
        "default":  True,
        "inverted": False,
    },
    {
        "key":      "mesh",
        "cmake":    "PURR_ENABLE_MESH",
        "label":    "Meshtastic",
        "desc":     "mesh_manager — Meshtastic co-resident stack (requires LoRa)",
        "targets":  ["heltec", "tdeck", "tdeck_plus"],
        "default":  False,
        "inverted": False,
    },
    {
        "key":      "micropython",
        "cmake":    "BUILD_MINI",
        "label":    "MicroPython",
        "desc":     "mpython_runtime — .meow app interpreter",
        "targets":  _ALL_TARGETS,
        "default":  True,
        "inverted": True,   # micropython ON  → BUILD_MINI=0
    },
    {
        "key":      "shell",
        "cmake":    "PURR_ENABLE_SHELL",
        "label":    "Debug Shell",
        "desc":     "drv_shell — USB serial REPL (gpio-set, display-color, reboot, ...)",
        "targets":  _ALL_TARGETS,
        "default":  True,
        "inverted": False,
    },
    {
        "key":      "lua",
        "cmake":    "PURR_ENABLE_LUA",
        "label":    "Lua Runtime",
        "desc":     "lib_lua — .paws/.claw script execution (~120 KB flash)",
        "targets":  _ALL_MINIWIN,
        "default":  True,
        "inverted": False,
    },
    {
        "key":      "magidos",
        "cmake":    "PURR_ENABLE_MAGIDOS",
        "label":    "MagiDOS",
        "desc":     "8086 DOS emulator app (WIP — compiled but stub)",
        "targets":  ["cyd", "cyd_s028r", "cyd_s024c", "tdeck_plus", "jc3248w535"],
        "default":  False,
        "inverted": False,
    },
    {
        "key":      "magicmac",
        "cmake":    "PURR_ENABLE_MAGICMAC",
        "label":    "MagicMac",
        "desc":     "Mac Plus emulator app (WIP — compiled but stub)",
        "targets":  ["cyd", "cyd_s028r", "cyd_s024c", "tdeck_plus", "jc3248w535"],
        "default":  False,
        "inverted": False,
    },
    {
        "key":      "gps",
        "cmake":    "PURR_ENABLE_GPS",
        "label":    "GPS",
        "desc":     "gps_manager — u-blox MIA-M10Q UART (T-Deck Plus only)",
        "targets":  ["tdeck_plus"],
        "default":  False,
        "inverted": False,
    },
]

LORA_KERNELS = {
    "sx1262":  "SX1262",
    "rak3172": "RAK3172",
    "sx1276":  "SX1276_RFM95W",
}

LORA_KERNEL_DESCS = {
    "sx1262":  "SPI — Heltec V3, T-Deck (default)",
    "rak3172": "UART AT — CattoBoardV1 PCB",
    "sx1276":  "SPI — generic RFM95W breakout",
}

UI_KERNELS = {
    "miniwin": "MiniWin",
    "none":    "none",
}

UI_KERNEL_DESCS = {
    "miniwin": "MiniWin embedded WM — device HAL in devices/<target>/ (all targets except heltec)",
    "none":    "no UI framework — headless / raw display only",
}

UI_THEMES = {
    "wce":        "WCE Classic",
    "blackberry": "Blackberry (green-on-black terminal theme)",
}

UI_THEME_DESCS = {
    "wce":        "Windows CE-style gray shell with Start menu and taskbar",
    "blackberry": "Phosphor green-on-black shell — terminal aesthetic, tap wallpaper for app drawer",
}

SHELL_DESCS = {
    "kitten_ui": "KittenUI — text-mode shell for small displays (OLED)",
    "none": "Headless — no UI shell compiled",
}

# ── Config ────────────────────────────────────────────────────────────────────
DEFAULT_CFG = {
    "target":       "heltec",
    "shell":        "kitten_ui",
    "lora_kernel":  "sx1262",
    "ui_kernel":    "none",
    "flash_port":   "",
    "flash_baud":   460800,
    "monitor_port": "",
    "tdeck_plus":   False,
    "cyd_variant":  "s028r",
    "modules": {
        "wifi":        True,
        "bt":          True,
        "mtp":         False,
        "flasher":     False,
        "lora":        True,
        "mesh":        False,
        "micropython": True,
        "lua":         True,
        "magidos":     False,
        "magicmac":    False,
        "gps":         False,
    },
    "ui_theme": "wce",
}


def load_cfg():
    if os.path.exists(CFG_FILE):
        try:
            with open(CFG_FILE, "r", encoding="utf-8") as f:
                stored = json.load(f)
            cfg = dict(DEFAULT_CFG)
            cfg["modules"] = dict(DEFAULT_CFG["modules"])
            for k, v in stored.items():
                if k == "modules" and isinstance(v, dict):
                    cfg["modules"].update(v)
                else:
                    cfg[k] = v
            _sanitize_cfg(cfg)
            # Normalize port paths (fix bare ttyUSBx saved without /dev/ prefix)
            for key in ("flash_port", "monitor_port"):
                if cfg.get(key):
                    cfg[key] = _normalize_port(cfg[key])
            info("Loaded purr_sdk.cfg  (--configure to change)")
            return cfg
        except Exception:
            warn("purr_sdk.cfg is malformed — using defaults")
    return {k: (dict(v) if isinstance(v, dict) else v) for k, v in DEFAULT_CFG.items()}


def save_cfg(cfg):
    with open(CFG_FILE, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)
    info("Config saved → purr_sdk.cfg")


def _sanitize_cfg(cfg):
    """Enforce hard hardware constraints — call after any target change."""
    target = cfg["target"]
    # Devices with no LoRa hardware — strip flags regardless of what config says
    _no_lora = ("cyd", "cyd_boot", "cyd_s028r", "cyd_s024c", "jc3248w535", "waveshare169", "tembed_cc1101")
    if target in _no_lora:
        cfg["modules"]["lora"] = False
        cfg["modules"]["mesh"] = False
    # cyd_boot is fully fixed — wipe all optional module flags
    if TARGETS.get(target, {}).get("fixed"):
        for key in ("bt", "lora", "mesh", "mtp", "flasher", "micropython"):
            cfg["modules"][key] = False
    # mesh requires lora — strip if lora was just disabled
    if not cfg["modules"].get("lora"):
        cfg["modules"]["mesh"] = False
    # ui_kernel: encoder-only devices don't support MiniWin touch-driven WM
    if target in ("heltec", "tembed_cc1101"):
        cfg["ui_kernel"] = "none"
    elif TARGETS.get(target, {}).get("fixed"):
        # fixed targets keep their current ui_kernel (no wizard override)
        pass
    elif "ui_kernel" not in cfg or cfg["ui_kernel"] not in UI_KERNELS:
        cfg["ui_kernel"] = TARGETS.get(target, {}).get("default_ui", "none")
    # ui_theme: only meaningful when ui_kernel == miniwin
    if "ui_theme" not in cfg or cfg["ui_theme"] not in UI_THEMES:
        cfg["ui_theme"] = "wce"


# ── Wizard ────────────────────────────────────────────────────────────────────

def pick_target(cfg):
    header("Select target device:")
    keys = list(TARGETS.keys())
    for i, key in enumerate(keys, 1):
        t = TARGETS[key]
        print(f"  [{i}]  {t['desc']:<32}  {C_GRY}{t['spec']}{C_RST}")
    print()
    raw = input("  Choice [1]: ").strip() or "1"
    try:
        idx = int(raw) - 1
        cfg["target"] = keys[idx] if 0 <= idx < len(keys) else keys[0]
    except ValueError:
        cfg["target"] = keys[0]
    _sanitize_cfg(cfg)


def pick_tdeck_variant(cfg):
    header("T-Deck variant:")
    print(f"  [1]  Normal   {C_GRY}Original T-Deck (no GPS){C_RST}")
    print(f"  [2]  Plus     {C_GRY}T-Deck Plus (u-blox MIA-M10Q GPS + larger battery){C_RST}")
    print()
    cfg["tdeck_plus"] = (input("  Choice [1]: ").strip() == "2")


def pick_cyd_variant(cfg):
    header("CYD display variant:")
    print(f"  [1]  S028R    {C_GRY}Original (ESP32-2432S028R, v0.4.0/v0.5.0) — XPT2046 SPI touch{C_RST}")
    print(f"  [2]  S024C    {C_GRY}Newer variant (ESP32-2432S024C) — CST816S I2C touch{C_RST}")
    print()
    choice = input("  Choice [1]: ").strip() or "1"
    cfg["cyd_variant"] = "s024c" if choice == "2" else "s028r"


def pick_lora_kernel(cfg):
    header("LoRa kernel backend:")
    keys = list(LORA_KERNELS.keys())
    for i, k in enumerate(keys, 1):
        print(f"  [{i}]  {k:<10}  {C_GRY}{LORA_KERNEL_DESCS[k]}{C_RST}")
    print()
    raw = input("  Choice [1]: ").strip() or "1"
    try:
        idx = int(raw) - 1
        cfg["lora_kernel"] = keys[idx] if 0 <= idx < len(keys) else "sx1262"
    except ValueError:
        cfg["lora_kernel"] = "sx1262"


def pick_ui_kernel(cfg):
    header("UI kernel:")
    keys = list(UI_KERNELS.keys())
    for i, k in enumerate(keys, 1):
        print(f"  [{i}]  {k:<10}  {C_GRY}{UI_KERNEL_DESCS[k]}{C_RST}")
    print()
    default_ui = TARGETS[cfg["target"]].get("default_ui", "none")
    default_idx = keys.index(default_ui) + 1 if default_ui in keys else 1
    raw = input(f"  Choice [{default_idx}]: ").strip() or str(default_idx)
    try:
        idx = int(raw) - 1
        cfg["ui_kernel"] = keys[idx] if 0 <= idx < len(keys) else default_ui
    except ValueError:
        cfg["ui_kernel"] = default_ui


def pick_ui_theme(cfg):
    header("MiniWin shell theme:")
    keys = list(UI_THEMES.keys())
    for i, k in enumerate(keys, 1):
        print(f"  [{i}]  {k:<14}  {C_GRY}{UI_THEME_DESCS[k]}{C_RST}")
    print()
    cur  = cfg.get("ui_theme", "wce")
    didx = keys.index(cur) + 1 if cur in keys else 1
    raw  = input(f"  Choice [{didx}]: ").strip() or str(didx)
    try:
        idx = int(raw) - 1
        cfg["ui_theme"] = keys[idx] if 0 <= idx < len(keys) else cur
    except ValueError:
        cfg["ui_theme"] = cur


def pick_modules(cfg):
    target = cfg["target"]
    if TARGETS[target]["fixed"]:
        print(f"\n  {C_GRY}{target}: fixed module set — no toggles{C_RST}\n")
        return

    visible = [m for m in MODULES if target in m["targets"]]

    while True:
        div()
        print(f"\n  Kernel modules — {C_CYN}{target}{C_RST}\n")
        print(f"  {C_GRY}Always compiled: power_manager{C_RST}")
        if target in ("cyd", "cyd_s024c", "cyd_s028r"):
            print(f"  {C_GRY}                  display_ili9341  touch_cst816s  partition_manager{C_RST}")
        elif target == "jc3248w535":
            print(f"  {C_GRY}                  display_st7796  touch_gt911{C_RST}")
        elif target == "tdeck_plus":
            print(f"  {C_GRY}                  display_st7789  touch_gt911  keyboard(I2C)  trackball(GPIO){C_RST}")
        elif target == "waveshare169":
            print(f"  {C_GRY}                  display_st7789  touch_cst816s{C_RST}")
        elif target in ("heltec",):
            print(f"  {C_GRY}                  display_ssd1306{C_RST}")
        print(f"\n  {C_WHT}Optional (enter number to toggle):{C_RST}\n")

        for i, m in enumerate(visible, 1):
            key = m["key"]
            on = cfg["modules"].get(key, m["default"])
            locked = key == "mesh" and not cfg["modules"].get("lora", False)
            if locked:
                on = False
                cfg["modules"]["mesh"] = False
            if on:
                row = f"  [{i}]  {C_GRN}[*]{C_RST}  {m['label']:<14}  {C_GRN}ON {C_RST}  {C_GRY}{m['desc']}{C_RST}"
            else:
                row = f"  [{i}]  {C_GRY}[ ]{C_RST}  {m['label']:<14}  {C_GRY}OFF{C_RST}  {C_GRY}{m['desc']}{C_RST}"
            if locked:
                row += f"  {C_YLW}[requires LoRa]{C_RST}"
            print(row)

        has_lora = cfg["modules"].get("lora", False) and target not in ("cyd", "cyd_boot")
        if has_lora:
            print(f"\n        {C_GRY}+-- kernel: {cfg['lora_kernel']}  ([k] to change){C_RST}")

        is_miniwin = target in _ALL_MINIWIN
        if is_miniwin:
            ui    = cfg.get("ui_kernel", TARGETS[target].get("default_ui", "none"))
            theme = cfg.get("ui_theme", "wce")
            if ui == "miniwin":
                print(f"\n        {C_GRY}+-- ui: {ui} / theme: {theme}  ([u] kernel, [t] theme){C_RST}")
            else:
                print(f"\n        {C_GRY}+-- ui: {ui}  ([u] to change){C_RST}")

        print()
        prompt = f"  Toggle [1-{len(visible)}]"
        if has_lora:
            prompt += ", [k] LoRa kernel"
        if is_miniwin:
            prompt += ", [u] UI kernel"
            if cfg.get("ui_kernel") == "miniwin":
                prompt += ", [t] theme"
        raw = input(prompt + ", or Enter to continue: ").strip()

        if not raw:
            break
        if raw.lower() == "k" and has_lora:
            pick_lora_kernel(cfg)
            continue
        if raw.lower() == "u" and is_miniwin:
            pick_ui_kernel(cfg)
            continue
        if raw.lower() == "t" and is_miniwin and cfg.get("ui_kernel") == "miniwin":
            pick_ui_theme(cfg)
            continue
        try:
            idx = int(raw) - 1
            if not (0 <= idx < len(visible)):
                warn(f"Enter a number between 1 and {len(visible)}")
                continue
            m = visible[idx]
            key = m["key"]
            if key == "mesh" and not cfg["modules"].get("lora", False):
                warn("Mesh requires LoRa — enable LoRa first")
                continue
            cfg["modules"][key] = not cfg["modules"].get(key, m["default"])
        except ValueError:
            warn(f"Enter a number between 1 and {len(visible)}")


def pick_ports(cfg):
    print()
    fp = _pick_port("Flash port", saved=cfg.get("flash_port", ""))
    if fp:
        cfg["flash_port"] = fp
    # Default monitor to same port as flash unless already set differently
    saved_mon = cfg.get("monitor_port", "") or fp
    mp = _pick_port("Monitor port", saved=saved_mon)
    if mp:
        cfg["monitor_port"] = mp


def configure(cfg, full=True):
    if full:
        pick_target(cfg)
        if cfg["target"] == "tdeck":
            pick_tdeck_variant(cfg)
        elif cfg["target"] == "cyd":
            pick_cyd_variant(cfg)
    pick_modules(cfg)
    pick_ports(cfg)
    save_cfg(cfg)


# ── CMake flag builder ────────────────────────────────────────────────────────

def _cmake_flags(cfg):
    target = cfg["target"]

    # Map display type targets to cyd
    if target in ("cyd_s028r", "cyd_s024c"):
        cmake_target = "cyd"
        display_variant = target.split("_")[1]  # s028r or s024c
    else:
        cmake_target = target
        display_variant = cfg.get("cyd_variant", "s028r") if target == "cyd" else None

    t      = TARGETS[target]
    mods   = cfg["modules"]
    fixed  = t["fixed"]

    flags = [f"-DTARGET_DEVICE={cmake_target}"]

    # BUILD_MINI (inverted micropython)
    if fixed:
        flags.append("-DBUILD_MINI=1")
    else:
        flags.append(f"-DBUILD_MINI={0 if mods.get('micropython', True) else 1}")

    def _flag(cmake_name, key, default):
        val = 0 if fixed else (1 if mods.get(key, default) else 0)
        # mesh requires lora
        if key == "mesh" and not mods.get("lora", False):
            val = 0
        flags.append(f"-D{cmake_name}={val}")

    # Hard-enforce: CYD has no LoRa/Mesh hardware regardless of saved config
    if cmake_target == "cyd" or target == "cyd_boot":
        mods = dict(mods)
        mods["lora"] = False
        mods["mesh"] = False

    _flag("PURR_ENABLE_WIFI",     "wifi",     True)
    _flag("PURR_ENABLE_BT",       "bt",       True)
    _flag("PURR_ENABLE_LORA",     "lora",     t["default_lora"])
    _flag("PURR_ENABLE_MESH",     "mesh",     False)
    _flag("PURR_ENABLE_MTP",      "mtp",      False)
    _flag("PURR_ENABLE_FLASHER",  "flasher",  False)
    _flag("PURR_ENABLE_SHELL",    "shell",    True)
    _flag("PURR_ENABLE_LUA",      "lua",      True)
    _flag("PURR_ENABLE_MAGIDOS",  "magidos",  False)
    _flag("PURR_ENABLE_MAGICMAC", "magicmac", False)
    _flag("PURR_ENABLE_GPS",      "gps",      False)

    flags.append(f"-DBUILD_TDECK_PLUS={1 if cfg.get('tdeck_plus') else 0}")

    # CYD display variant (s028r=original XPT2046, s024c=newer CST816S)
    if cmake_target == "cyd" and display_variant:
        flags.append(f"-DCYD_DISPLAY_VARIANT={display_variant}")

    # UI kernel (miniwin | none)
    ui = cfg.get("ui_kernel", TARGETS[target].get("default_ui", "none"))
    if fixed:
        ui = "none"
    flags.append(f"-DPURR_UI_KERNEL={ui}")

    # Shell theme (wce | blackberry) — only meaningful when ui_kernel=miniwin
    theme = cfg.get("ui_theme", "wce") if ui == "miniwin" and not fixed else "wce"
    flags.append(f"-DPURR_UI_THEME={theme}")

    return flags


# ── Arduino patches ───────────────────────────────────────────────────────────

def _arduino_patches(coreos_dir):
    cores = os.path.join(coreos_dir, "managed_components",
                         "espressif__arduino-esp32", "cores", "esp32")
    if not os.path.isdir(cores):
        warn(f"arduino patches: managed_components not found at {cores} — skipping")
        return

    # Patch 1: adc_continuous_data_t rename
    adc_h = os.path.join(cores, "esp32-hal-adc.h")
    adc_c = os.path.join(cores, "esp32-hal-adc.c")
    if os.path.exists(adc_h):
        txt = open(adc_h, "r", encoding="utf-8").read()
        if re.search(r'\badc_continuous_data_t\b', txt):
            info("arduino patch: adc_continuous_data_t rename")
            def _rename(t):
                return re.sub(r'\badc_continuous_data_t\b', 'arduino_adc_cont_data_t', t)
            _write(adc_h, _rename(txt))
            if os.path.exists(adc_c):
                _write(adc_c, _rename(open(adc_c, "r", encoding="utf-8").read()))

    # Patch 2: I2C slave LL stubs
    i2c_s = os.path.join(cores, "esp32-hal-i2c-slave.c")
    if os.path.exists(i2c_s):
        txt = open(i2c_s, "r", encoding="utf-8").read()
        if "i2c_ll_slave_init" not in txt or "do {} while" not in txt:
            info("arduino patch: I2C slave LL stubs")
            stub = (
                '\n#include "esp_idf_version.h"\n'
                '#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)\n'
                '#define i2c_ll_slave_init(dev)           do {} while (0)\n'
                '#define i2c_ll_set_fifo_mode(dev, mode)  do {} while (0)\n'
                '#define i2c_ll_cal_bus_clk(a, b, c)      do {} while (0)\n'
                '#define i2c_ll_set_bus_timing(dev, cfg)  do {} while (0)\n'
                '#define i2c_ll_set_filter(dev, n)        do {} while (0)\n'
                '#endif'
            )
            _write(i2c_s, re.sub(
                r'(#include "esp_private/periph_ctrl\.h")',
                r'\1' + stub, txt
            ))

    # Patch 3: ESP_SR missing ESP_I2S.h
    sr_src  = os.path.join(coreos_dir, "managed_components", "espressif__arduino-esp32",
                           "libraries", "ESP_SR", "src", "ESP_I2S.h")
    i2s_src = os.path.join(coreos_dir, "managed_components", "espressif__arduino-esp32",
                           "libraries", "ESP_I2S", "src", "ESP_I2S.h")
    if not os.path.exists(sr_src) and os.path.exists(i2s_src):
        info("arduino patch: ESP_SR missing ESP_I2S.h stub")
        shutil.copy2(i2s_src, sr_src)

    # Patch 4: add esp_timer + esp_driver_gpio to arduino-esp32 REQUIRES
    # esp32-hal-log.h needs esp_timer.h; esp32-hal-gpio.h needs driver/gpio.h
    # from esp_driver_gpio (IDF 5.x split the driver component).
    arduino_cmake = os.path.join(coreos_dir, "managed_components",
                                 "espressif__arduino-esp32", "CMakeLists.txt")
    if os.path.exists(arduino_cmake):
        txt = open(arduino_cmake, "r", encoding="utf-8").read()
        needs_patch = re.search(r'set\(requires spi_flash', txt) and \
                      (not re.search(r'set\(requires[^)]*esp_timer', txt) or
                       not re.search(r'set\(requires[^)]*esp_driver_gpio', txt))
        if needs_patch:
            info("arduino patch: add esp_timer + esp_driver_gpio to arduino-esp32 REQUIRES")
            # First add esp_timer if missing
            if not re.search(r'set\(requires[^)]*esp_timer', txt):
                txt = re.sub(r'(set\(requires spi_flash[^)]*)(driver\))',
                             r'\1driver esp_timer)', txt)
            # Then add esp_driver_gpio if missing
            if not re.search(r'set\(requires[^)]*esp_driver_gpio', txt):
                txt = re.sub(r'(set\(requires spi_flash[^)]*)(esp_timer\))',
                             r'\1esp_timer esp_driver_gpio)', txt)
            _write(arduino_cmake, txt)


def _write(path, text):
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


# ── LoRa kernel installer ─────────────────────────────────────────────────────

def _install_lora_kernel(cfg):
    target = cfg["target"]
    if target in ("cyd", "cyd_boot") or not cfg["modules"].get("lora", False):
        return
    folder = LORA_KERNELS.get(cfg.get("lora_kernel", "sx1262"))
    if not folder:
        warn(f"Unknown LoRa kernel '{cfg.get('lora_kernel')}'")
        return
    src = os.path.join(LORA_DIR, folder)
    dst = os.path.join(COREOS_DIR, "system", "kernel", "modules")
    if not os.path.isdir(src):
        warn(f"LoRa kernel not found: {src}")
        return
    info(f"LoRa kernel → {folder} → modules/")
    for item in os.listdir(src):
        s = os.path.join(src, item)
        d = os.path.join(dst, item)
        if os.path.isfile(s):
            shutil.copy2(s, d)
        elif os.path.isdir(s):
            shutil.copytree(s, d, dirs_exist_ok=True)


# ── Live subprocess runner ────────────────────────────────────────────────────

def run_live(cmd, cwd=None):
    proc = subprocess.Popen(
        cmd,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    try:
        for line in proc.stdout:
            print(line, end="", flush=True)
        proc.wait()
    except KeyboardInterrupt:
        proc.terminate()
        proc.wait()
        print()
        warn("Interrupted by user.")
        sys.exit(0)   # clean exit — not a build failure
    return proc.returncode


# ── SPIFFS image builder ──────────────────────────────────────────────────────

def _resolve_idf_path():
    """Find IDF_PATH from env or well-known locations."""
    idf_path = os.environ.get("IDF_PATH", "")
    if idf_path and os.path.isfile(os.path.join(idf_path, "tools", "idf.py")):
        return idf_path
    for candidate in (
        os.path.expanduser("~/esp/idf"),
        os.path.expanduser("~/esp/esp-idf"),
        os.path.expanduser("~/.espressif/idf"),
        "/opt/esp-idf",
    ):
        if os.path.isfile(os.path.join(candidate, "tools", "idf.py")):
            os.environ["IDF_PATH"] = candidate   # set so subsequent calls find it
            return candidate
    return ""


def _build_spiffs(cfg):
    target    = cfg["target"]
    idf_path  = _resolve_idf_path()
    spiffsgen = os.path.join(idf_path, "components", "spiffs", "spiffsgen.py") if idf_path else ""
    if not spiffsgen or not os.path.exists(spiffsgen):
        warn("spiffsgen.py not found — skipping SPIFFS image")
        return

    build_dir   = _build_dir(cfg)
    staging_dir = os.path.join(build_dir, "spiffs_staging")

    if os.path.exists(staging_dir):
        shutil.rmtree(staging_dir)
    for d in ("system/kernel", "system/logs", "apps"):
        os.makedirs(os.path.join(staging_dir, d), exist_ok=True)

    # cyd_s024c and cyd_s028r share the same device JSON as plain "cyd"
    _cyd_family = ("cyd", "cyd_boot", "cyd_s024c", "cyd_s028r")
    device_target = "cyd" if target in _cyd_family else target
    device_src = os.path.join(COREOS_DIR, "system", "kernel", "devices", f"{device_target}.json")
    if not os.path.exists(device_src):
        device_src = os.path.join(COREOS_DIR, "system", "kernel", "device.json")
    if not os.path.exists(device_src):
        warn(f"No device config for '{device_target}' — skipping SPIFFS image")
        return

    shutil.copy2(device_src, os.path.join(staging_dir, "system", "kernel", "device.json"))
    info(f"SPIFFS: {device_target}.json → /system/kernel/device.json")

    # CYD family: 0x70000 = 458752 (448 KB) at 0x390000
    # others:     0x50000 = 327680 (320 KB) at 0x3b0000
    spiffs_size = 458752 if target in _cyd_family else 327680
    spiffs_img  = os.path.join(build_dir, "spiffs.bin")

    rc = run_live([sys.executable, spiffsgen, str(spiffs_size), staging_dir, spiffs_img])
    if rc != 0:
        err(f"spiffsgen.py failed (exit {rc})")
    bdn = os.path.basename(build_dir)
    info(f"SPIFFS image: {bdn}/spiffs.bin ({spiffs_size // 1024} KB)")


# ── Build / flash / monitor ───────────────────────────────────────────────────

def do_build(cfg, clean=False):
    target = cfg["target"]
    chip   = TARGETS[target]["chip"]
    flags  = _cmake_flags(cfg)
    mods   = cfg["modules"]

    # sdkconfig.defaults: cyd_boot falls back to cyd (same hardware)
    defaults_target = "cyd" if target == "cyd_boot" else target
    defaults_src = os.path.join(SCRIPT_DIR, "targets", f"{defaults_target}.defaults")
    if os.path.exists(defaults_src):
        info(f"{defaults_target}.defaults → sdkconfig.defaults")
        shutil.copy2(defaults_src, os.path.join(COREOS_DIR, "sdkconfig.defaults"))
    else:
        warn(f"targets/{defaults_target}.defaults not found, keeping existing sdkconfig.defaults")

    bdir     = _build_dir(cfg)
    bdn      = os.path.basename(bdir)
    sdkcfg   = _sdkconfig(cfg)
    idf_args = ["-B", bdn, f"-DSDKCONFIG={sdkcfg}"]

    if clean:
        info(f"clean {bdn}...")
        if os.path.isdir(bdir):
            shutil.rmtree(bdir)
        if os.path.exists(sdkcfg):
            os.remove(sdkcfg)

    # Prevent the IDF component manager from re-downloading and overwriting managed
    # components during the cmake configure step inside idf.py build. Without this,
    # cmake triggers the component manager on every fresh build dir, restoring the
    # original (unpatched) arduino-esp32 files after we've already patched them.
    os.environ["IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS"] = "0"

    _install_lora_kernel(cfg)

    info(f"set-target {chip}  [{bdn}]")
    rc = run_live(_idf_py() + idf_args + flags + ["set-target", chip], cwd=COREOS_DIR)
    if rc != 0:
        err(f"idf.py set-target exited {rc}")

    # Apply patches after set-target populates managed_components
    _arduino_patches(COREOS_DIR)

    mini = 0 if mods.get("micropython", True) else 1
    if TARGETS[target]["fixed"]:
        mini = 1
    info(f"build  TARGET={target}  BT={int(mods.get('bt',True))}  "
         f"MTP={int(mods.get('mtp',False))}  FLASHER={int(mods.get('flasher',False))}  "
         f"LORA={int(mods.get('lora',TARGETS[target]['default_lora']))}  MINI={mini}")

    # Apply patches a second time immediately before build starts — insurance in case
    # cmake did a partial reconfigure during set-target and scheduled another one.
    # Second call is a no-op if patches already applied (idempotent check in _arduino_patches).
    _arduino_patches(COREOS_DIR)

    rc = run_live(_idf_py() + idf_args + flags + ["build"], cwd=COREOS_DIR)
    if rc != 0:
        err(f"idf.py build exited {rc}")

    _build_spiffs(cfg)


# ── Release packing ───────────────────────────────────────────────────────────

# Per-device notes included in the generated flash guide.
_DEVICE_NOTES = {
    "tdeck_plus": [
        "GT911 capacitive touch: identity calibration matrix seeded to NVS on first boot — no resistive calibration screen.",
        "I2C bus shared between GT911 (0x5D) and keyboard co-processor (0x55) on SDA=18 SCL=8.",
        "ST7789 initialised in landscape (swap_xy + mirror) — display is correct orientation out of the box.",
        "Trackball cursor hides automatically after 5 s of inactivity; reappears on trackball movement.",
        "Erase NVS partition before first flash to guarantee clean calibration seed: esptool erase_region 0x9000 0x5000",
    ],
    "cyd_s028r": [
        "XPT2046 SPI resistive touch — calibration stored in NVS; a one-time touch calibration screen runs on first boot.",
        "Display: ILI9341 2.4\" 320x240. Backlight GPIO 21.",
        "No LoRa hardware on this device.",
    ],
    "cyd_s024c": [
        "CST816S I2C capacitive touch on SDA=33 SCL=32. BL=GPIO27 (not 21).",
        "Display: ILI9341 2.4\" 320x240.",
        "No LoRa hardware on this device.",
    ],
    "cyd_boot": [
        "Factory kernel — chainloads ota_0 (the main OS). Flash this first, then flash the main OS over OTA or via ota_0 partition.",
        "Fixed build: no module toggles, always minimal. Same hardware as cyd_s028r/s024c.",
    ],
    "jc3248w535": [
        "GT911 capacitive touch on SDA=19 SCL=20. Identity calibration seeded on first boot.",
        "8MB PSRAM required — MagiDOS enabled on this build.",
        "Display: ST7796 3.5\" 480x320.",
    ],
    "waveshare169": [
        "CST816S I2C capacitive touch on SDA=6 SCL=7 — verify pin assignments before flashing.",
        "Display: ST7789 1.69\" 240x280.",
        "No LoRa hardware on this device.",
    ],
    "heltec": [
        "SX1262 LoRa enabled by default. Antenna required — do not transmit without antenna connected.",
        "Display: SSD1306 0.96\" OLED 128x64.",
        "KittenUI shell (non-MiniWin).",
    ],
    "tdeck": [
        "WIP — touch not yet functional. Trackball + keyboard work.",
        "Display: ST7789 320x240.",
        "SX1262 LoRa enabled by default.",
    ],
}

_GHREL_DIR = os.path.join(REPO_DIR, "GHReleases")

# Release build sets
RELEASE_SETS = {
    "all":     ["cyd_s028r", "cyd_s024c", "cyd_boot", "tdeck_plus", "jc3248w535", "waveshare169", "heltec"],
    "miniwin": ["cyd_s028r", "cyd_s024c", "tdeck_plus", "jc3248w535", "waveshare169"],
    "s3":      ["tdeck_plus", "jc3248w535", "waveshare169", "heltec"],
    "cyd":     ["cyd_s028r", "cyd_s024c", "cyd_boot"],
}


def _pack_release(cfg):
    """Copy build artifacts to GHReleases/<target>/ and write a flash guide."""
    import json as _json
    target    = cfg["target"]
    build_dir = _build_dir(cfg)
    out_dir   = os.path.join(_GHREL_DIR, target)
    os.makedirs(out_dir, exist_ok=True)

    # Read flasher_args.json for canonical offsets
    fargs_path = os.path.join(build_dir, "flasher_args.json")
    if not os.path.exists(fargs_path):
        warn(f"flasher_args.json not found in {os.path.basename(build_dir)} — skipping pack")
        return

    with open(fargs_path) as f:
        fargs = _json.load(f)

    chip       = fargs.get("extra_esptool_args", {}).get("chip", TARGETS[target]["chip"])
    flash_mode = fargs.get("flash_settings", {}).get("flash_mode", "dio")
    flash_freq = fargs.get("flash_settings", {}).get("flash_freq", "80m")
    flash_size = fargs.get("flash_settings", {}).get("flash_size", "detect")
    flash_files = fargs.get("flash_files", {})  # { "0x0": "bootloader/bootloader.bin", ... }

    # Copy each bin and build the offset table
    copied = {}  # offset -> filename
    for offset, rel_path in flash_files.items():
        src = os.path.join(build_dir, rel_path)
        if not os.path.exists(src):
            warn(f"  missing: {rel_path}")
            continue
        fname = os.path.basename(rel_path)
        dst   = os.path.join(out_dir, fname)
        shutil.copy2(src, dst)
        copied[offset] = fname
        info(f"  {offset}  {fname}")

    # SPIFFS (not in flasher_args.json — SDK-generated)
    spiffs_src = os.path.join(build_dir, "spiffs.bin")
    _cyd_family = ("cyd", "cyd_boot", "cyd_s024c", "cyd_s028r")
    spiffs_offset = "0x390000" if target in _cyd_family else "0x3b0000"
    if os.path.exists(spiffs_src):
        shutil.copy2(spiffs_src, os.path.join(out_dir, "spiffs.bin"))
        copied[spiffs_offset] = "spiffs.bin"
        info(f"  {spiffs_offset}  spiffs.bin")

    # Write flash guide
    _write_flash_guide(target, out_dir, chip, flash_mode, flash_freq, flash_size, copied)
    info(f"release packed → GHReleases/{target}/")


def _write_flash_guide(target, out_dir, chip, flash_mode, flash_freq, flash_size, files):
    """Write FLASH_GUIDE.md for a device release."""
    t     = TARGETS[target]
    notes = _DEVICE_NOTES.get(target, [])

    # Build esptool one-liner
    file_args = " ".join(f"{off} {fname}" for off, fname in sorted(files.items()))
    esptool_cmd = (
        f"python -m esptool --chip {chip} -p PORT -b 460800 "
        f"--before default_reset --after hard_reset "
        f"write_flash --flash_mode {flash_mode} --flash_size {flash_size} --flash_freq {flash_freq} "
        f"{file_args}"
    )

    import datetime as _dt
    build_date = _dt.datetime.now().strftime("%Y-%m-%d")

    lines = [
        f"# PURR OS — {t['desc']}",
        "",
        f"**PURR OS:** v{PURROS_VERSION}  ",
        f"**KITT:** v{KITT_VERSION}  ",
        f"**Built:** {build_date}  ",
        f"**Target:** `{target}`  ",
        f"**Chip:** {chip}  ",
        f"**Flash:** {flash_mode.upper()} / {flash_freq} / {flash_size}  ",
        "",
        "---",
        "",
        "## Files",
        "",
        "| Offset | File | Purpose |",
        "|--------|------|---------|",
    ]

    _purpose = {
        "bootloader.bin":       "ESP-IDF bootloader",
        "partition-table.bin":  "Partition table",
        "ota_data_initial.bin": "OTA slot selector (boot to ota_0)",
        "purr_os_core.bin":     "PURR OS application",
        "spiffs.bin":           "SPIFFS filesystem (device config, assets)",
    }
    for off, fname in sorted(files.items()):
        purpose = _purpose.get(fname, fname)
        lines.append(f"| `{off}` | `{fname}` | {purpose} |")

    lines += [
        "",
        "---",
        "",
        "## Flash with ESP Web Flasher",
        "",
        "1. Open **https://esp.huhn.me** (or **https://espressif.github.io/esptool-js**) in Chrome/Edge.",
        "2. Click **Connect** and select the device's COM port.",
        f"3. Set chip to **{chip.upper()}**.",
        "4. Add each file below with its offset:",
        "",
    ]
    for off, fname in sorted(files.items()):
        lines.append(f"   - Offset `{off}` → `{fname}`")

    lines += [
        "",
        "5. Click **Program** and wait for completion.",
        "6. Press reset on the device.",
        "",
        "> **Tip:** If the device was previously flashed with different firmware, erase flash first:",
        f"> `python -m esptool --chip {chip} -p PORT erase_flash`",
        "",
        "---",
        "",
        "## Flash with esptool (command line)",
        "",
        "```bash",
        esptool_cmd,
        "```",
        "",
        "Replace `PORT` with your serial port (e.g. `/dev/ttyUSB0`, `/dev/ttyAMC0`, or `COM3`).",
        "",
        "---",
        "",
        "## Device notes",
        "",
    ]
    for note in notes:
        lines.append(f"- {note}")

    if not notes:
        lines.append(f"- See PURR OS documentation for details.")

    lines += [
        "",
        "---",
        "",
        "_Generated automatically by PURR OS SDK pack_release._",
    ]

    guide_path = os.path.join(out_dir, "FLASH_GUIDE.md")
    _write(guide_path, "\n".join(lines) + "\n")
    info(f"  FLASH_GUIDE.md written")


def do_release_build(targets_list, clean=False):
    """Build each target sequentially and pack to GHReleases/ after each success."""
    total  = len(targets_list)
    passed = []
    failed = []

    for i, target in enumerate(targets_list, 1):
        header(f"Release build {i}/{total}: {target}")
        # Build a minimal cfg for this target
        rcfg = load_cfg()
        rcfg["target"] = target
        _sanitize_cfg(rcfg)

        # Auto-enable MagiDOS for PSRAM S3 targets
        if target in ("tdeck_plus", "jc3248w535"):
            rcfg["modules"]["magidos"] = True

        try:
            do_build(rcfg, clean=clean)
            _pack_release(rcfg)
            passed.append(target)
            info(f"[{i}/{total}] {target} — DONE")
        except SystemExit as e:
            failed.append(target)
            warn(f"[{i}/{total}] {target} — FAILED (exit {e.code})")
            warn("Continuing with next target...")

    div()
    if passed:
        info(f"Built OK : {' '.join(passed)}")
    if failed:
        warn(f"Failed   : {' '.join(failed)}")
    div()


def do_flash(cfg, port=None):
    target = cfg["target"]
    chip   = TARGETS[target]["chip"]
    port   = port or cfg.get("flash_port", "")
    baud   = cfg.get("flash_baud", 460800)

    if not port or port == "auto":
        port = _pick_port("Flash port", saved=cfg.get("flash_port", ""))
        if not port:
            err("No flash port specified. Connect your CYD and try again.")
        cfg["flash_port"] = port
        save_cfg(cfg)

    build_dir  = _build_dir(cfg)
    spiffs_img = os.path.join(build_dir, "spiffs.bin")

    # All CYD variants flash OS directly to factory (0x10000); spiffs at 0x390000
    _cyd_targets = ("cyd", "cyd_s024c", "cyd_s028r")
    app_offset    = "0x10000"
    spiffs_offset = "0x390000" if target in (*_cyd_targets, "cyd_boot") else "0x3b0000"

    cmd = [
        sys.executable, "-m", "esptool",
        "--chip",   chip,
        "--port",   port,
        "-b",       str(baud),
        "--before", "default_reset",
        "--after",  "hard_reset",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_size", "detect",
        "--flash_freq", "40m",
        "0x1000",    os.path.join(build_dir, "bootloader", "bootloader.bin"),
        "0x8000",    os.path.join(build_dir, "partition_table", "partition-table.bin"),
        "0xe000",    os.path.join(build_dir, "ota_data_initial.bin"),
        app_offset,  os.path.join(build_dir, "purr_os_core.bin"),
    ]
    if os.path.exists(spiffs_img):
        cmd += [spiffs_offset, spiffs_img]
        info(f"including SPIFFS image at {spiffs_offset}")

    info(f"flashing → {port}")
    rc = run_live(cmd)
    if rc != 0:
        err(f"Flash failed (exit {rc})")


def do_monitor(cfg, port=None):
    port = port or cfg.get("monitor_port", "") or cfg.get("flash_port", "")
    if not port or port == "auto":
        port = _pick_port("Monitor port", saved=cfg.get("flash_port", ""))
        if not port:
            err("No monitor port specified. Connect your CYD and try again.")
        cfg["monitor_port"] = port
        save_cfg(cfg)
    info(f"monitor on {port}  (Ctrl+] to exit)")
    bdn = os.path.basename(_build_dir(cfg))
    run_live(_idf_py() + ["-B", bdn, "-p", port, "monitor"], cwd=COREOS_DIR)


# ── Full build + full flash (CYD: factory + ota_0 together) ──────────────────

def do_full_build(cfg, clean=False):
    """Alias for do_build — OS goes directly to factory, no separate bootloader needed."""
    do_build(cfg, clean=clean)


def do_full_flash(cfg, port=None):
    """Full flash: partition table + bootloader + OS + SPIFFS in one esptool call.
    OS goes directly to factory (0x10000) — no separate cyd_boot binary needed."""
    do_flash(cfg, port=port)


def do_flash_full_explicit(cfg, port=None):
    """Full erase + explicit offset flash for T-Deck Plus.
    Uses esptool directly with all offsets verified against partition table."""
    if cfg["target"] != "tdeck_plus":
        err("--flash-full only supported for T-Deck Plus")

    port = _pick_port("Flash port", cfg.get("flash_port", ""))
    if not port:
        warn("Flash port not selected.")
        return

    out_dir = os.path.join(COREOS_DIR, "build_tdeck_plus")

    # Verify all required files exist
    files_needed = {
        "bootloader.bin": os.path.join(out_dir, "bootloader/bootloader.bin"),
        "partition-table.bin": os.path.join(out_dir, "partition_table/partition-table.bin"),
        "ota_data_initial.bin": os.path.join(out_dir, "ota_data_initial.bin"),
        "purr_os_core.bin": os.path.join(out_dir, "purr_os_core.bin"),
    }

    for name, path in files_needed.items():
        if not os.path.exists(path):
            err(f"Missing {name} at {path} — run --build first")

    info(f"Erasing flash on {port}...")
    cmd_erase = ["esptool.py", "--chip", "esp32s3", "-p", port, "erase_flash"]
    result = subprocess.run(cmd_erase, cwd=REPO_DIR)
    if result.returncode != 0:
        err("Erase failed")

    info(f"Flashing firmware to {port}...")
    cmd_flash = [
        "esptool.py", "--chip", "esp32s3", "-p", port, "-b", "460800",
        "--before", "default_reset", "--after", "hard_reset",
        "write_flash", "--flash_mode", "dio", "--flash_size", "16MB", "--flash_freq", "80m",
        "0x0",      files_needed["bootloader.bin"],
        "0x8000",   files_needed["partition-table.bin"],
        "0xe000",   files_needed["ota_data_initial.bin"],
        "0x10000",  files_needed["purr_os_core.bin"],
    ]
    result = subprocess.run(cmd_flash, cwd=REPO_DIR)
    if result.returncode != 0:
        err("Flash failed")

    info(f"{C_GRN}✓ Flash complete!{C_RST}")
    cfg["flash_port"] = port
    save_cfg(cfg)


# ── Banner ────────────────────────────────────────────────────────────────────

def show_banner(cfg):
    target = cfg["target"]
    t      = TARGETS[target]
    mods   = cfg["modules"]

    if target == "cyd_boot":
        display = "cyd [PURR Kernel]"
    elif target == "tdeck" and cfg.get("tdeck_plus"):
        display = "tdeck-plus"
    else:
        display = target

    div()
    wip_tag = f"  {C_YLW}[WIP]{C_RST}" if "WIP" in t.get("spec", "") else ""
    print(f"\n  {C_WHT}{C_BOLD}PURR OS v{PURROS_VERSION}{C_RST}  {C_GRY}KITT v{KITT_VERSION}{C_RST}  {C_CYN}{display} ({t['chip']}){C_RST}{wip_tag}")

    mini = (not mods.get("micropython", True)) or t["fixed"]
    print(f"  Variant  : {'mini — no MicroPython' if mini else 'full — with MicroPython'}")

    mod_strs = []
    if mods.get("wifi"):    mod_strs.append("wifi")
    if mods.get("bt"):      mod_strs.append("bt")
    if mods.get("lora"):    mod_strs.append(f"lora({cfg.get('lora_kernel','sx1262')})")
    if mods.get("mesh"):    mod_strs.append("mesh")
    if mods.get("mtp"):     mod_strs.append("mtp")
    if mods.get("flasher"): mod_strs.append("flasher")
    if mods.get("gps"):     mod_strs.append("gps")
    ui = cfg.get("ui_kernel", TARGETS[target].get("default_ui", "none"))
    if ui != "none":
        theme = cfg.get("ui_theme", "wce") if ui == "miniwin" else ""
        mod_strs.append(f"ui({ui}/{theme})" if theme else f"ui({ui})")
    print(f"  Modules  : {' '.join(mod_strs) if mod_strs else '(none)'}")

    if cfg.get("flash_port"):   print(f"  Flash    : {cfg['flash_port']}")
    if cfg.get("monitor_port"): print(f"  Monitor  : {cfg['monitor_port']}")
    div()
    print()


# ── Interactive menu ──────────────────────────────────────────────────────────

def main_menu(cfg):
    while True:
        show_banner(cfg)
        is_cyd = cfg["target"] in ("cyd", "cyd_boot")

        if is_cyd:
            print(f"  {C_WHT}[b]{C_RST} Build              {C_GRY}(kernel + userland){C_RST}")
            print(f"  {C_WHT}[B]{C_RST} Full Clean Build   {C_GRY}(kernel + userland, clean){C_RST}")
            print(f"  {C_WHT}[k]{C_RST} Build Kernel only  {C_GRY}(factory partition only){C_RST}")
        else:
            print(f"  {C_WHT}[b]{C_RST} Build              {C_WHT}[B]{C_RST} Clean Build")

        print(f"  {C_WHT}[f]{C_RST} Flash               {C_WHT}[m]{C_RST} Monitor")

        if is_cyd:
            print(f"  {C_WHT}[F]{C_RST} Full Flash          {C_GRY}(kernel + userland + SPIFFS in one pass){C_RST}")

        print(f"  {C_WHT}[r]{C_RST} Build + Flash       {C_WHT}[a]{C_RST} Build + Flash + Monitor")
        print(f"  {C_WHT}[c]{C_RST} Configure           {C_WHT}[s]{C_RST} Configure + Build")
        print(f"  {C_WHT}[R]{C_RST} Release Build       {C_GRY}(batch build set → GHReleases/){C_RST}")
        print(f"  {C_WHT}[~]{C_RST} Simulator           {C_GRY}(preview UI shells on PC){C_RST}")
        print(f"  {C_WHT}[q]{C_RST} Quit")
        print()
        choice = input("  Action: ").strip()

        if choice == "q":
            break
        elif choice == "b":
            if is_cyd:
                do_full_build(cfg)
            else:
                do_build(cfg)
        elif choice == "B":
            if is_cyd:
                do_full_build(cfg, clean=True)
            else:
                do_build(cfg, clean=True)
        elif choice == "k" and is_cyd:
            do_build(cfg)
        elif choice == "f":
            do_flash(cfg)
        elif choice == "F" and is_cyd:
            do_full_flash(cfg)
        elif choice == "m":
            do_monitor(cfg)
        elif choice == "r":
            if is_cyd:
                do_full_build(cfg)
            else:
                do_build(cfg)
            do_flash(cfg)
        elif choice == "a":
            if is_cyd:
                do_full_build(cfg)
            else:
                do_build(cfg)
            do_flash(cfg)
            do_monitor(cfg)
        elif choice == "c":
            configure(cfg)
        elif choice == "s":
            configure(cfg)
            if is_cyd:
                do_full_build(cfg)
            else:
                do_build(cfg)
        elif choice == "R":
            release_menu()
        elif choice == "~":
            sim_menu()
        else:
            warn(f"Unknown option '{choice}'")


# ── Simulator ────────────────────────────────────────────────────────────────

def release_menu():
    """Interactive release-build set picker."""
    div()
    print(f"\n  {C_WHT}{C_BOLD}PURR OS Release Build{C_RST}  {C_GRY}v{PURROS_VERSION} / KITT v{KITT_VERSION}{C_RST}\n")
    print(f"  Builds each device sequentially, packs binaries + flash guide to GHReleases/.\n")

    set_keys = list(RELEASE_SETS.keys())
    for i, key in enumerate(set_keys, 1):
        targets = RELEASE_SETS[key]
        print(f"  {C_WHT}[{i}]{C_RST} {key:<10} {C_GRY}{' '.join(targets)}{C_RST}")

    print(f"\n  {C_WHT}[c]{C_RST} Custom    {C_GRY}(enter target names separated by spaces){C_RST}")
    print(f"  {C_WHT}[q]{C_RST} Back\n")

    choice = input("  Set: ").strip()

    if choice == "q":
        return

    if choice == "c":
        raw = input("  Targets (space-separated): ").strip().split()
        targets = [t for t in raw if t in TARGETS]
        invalid = [t for t in raw if t not in TARGETS]
        if invalid:
            warn(f"Unknown targets ignored: {' '.join(invalid)}")
        if not targets:
            warn("No valid targets — cancelling.")
            return
    else:
        try:
            key = set_keys[int(choice) - 1]
            targets = RELEASE_SETS[key]
        except (ValueError, IndexError):
            warn(f"Unknown option '{choice}'")
            return

    clean = input(f"\n  Clean build? [y/N]: ").strip().lower() == "y"

    print()
    info(f"Release build: {' '.join(targets)}")
    info(f"Output: GHReleases/  clean={clean}")
    print()

    do_release_build(targets, clean=clean)


SIM_SHELLS = {
    "1": ("blackberry",  "BlackberryUI",     "320x240 — LVGL, real blackberry_ui.cpp"),
    "2": ("explorer",    "Explorer",         "320x240 — LVGL, real explorer.cpp"),
    "3": ("classicmac",  "ClassicMac",       "320x240 — LVGL, Mac System 7/8 Platinum"),
    "4": ("classic",     "Classic [WIP]",    "320x240 — MiniWin standalone (TBD)"),
}

def do_sim(shell_key):
    """Generate, build, and launch the PC simulator for the given shell."""
    sim_dir = os.path.join(SCRIPT_DIR, "..", "sim")
    sim_dir = os.path.normpath(sim_dir)

    if not os.path.isdir(sim_dir):
        err(f"sim/ directory not found at {sim_dir}")

    gen_py  = os.path.join(sim_dir, "gen_sim_shell.py")
    gen_dir = os.path.join(sim_dir, f"build_{shell_key}", "generated")
    gen_out = os.path.join(gen_dir, "shell_sim.cpp")

    cmake = shutil.which("cmake")
    if not cmake:
        err("cmake not found on PATH — install CMake to use the simulator")

    mingw = shutil.which("mingw32-make") or shutil.which("make")
    generator = ["MinGW Makefiles"] if shutil.which("mingw32-make") else []

    # Step 1: generate shell source
    info(f"simulator — generating {shell_key} shell source")
    os.makedirs(gen_dir, exist_ok=True)
    rc = run_live([sys.executable, gen_py, "--shell", shell_key, "--out", gen_out],
                  cwd=sim_dir)
    if rc != 0:
        err(f"gen_sim_shell.py failed (exit {rc})")

    # Step 2: cmake configure
    build_dir = os.path.join(sim_dir, f"build_{shell_key}")
    os.makedirs(build_dir, exist_ok=True)
    info(f"simulator — cmake configure  [{shell_key}]")
    cfg_args = [cmake, "-S", sim_dir, "-B", build_dir,
                f"-DPURR_SHELL={shell_key}", "-DCMAKE_BUILD_TYPE=Release"]
    if generator:
        cfg_args += ["-G"] + generator
    rc = run_live(cfg_args, cwd=sim_dir)
    if rc != 0:
        err(f"cmake configure failed (exit {rc})")

    # Step 3: cmake build
    info(f"simulator — cmake build  [{shell_key}]")
    rc = run_live([cmake, "--build", build_dir, "--config", "Release"], cwd=sim_dir)
    if rc != 0:
        err(f"cmake build failed (exit {rc})")

    # Step 4: find and launch exe
    exe = None
    for root, _, files in os.walk(build_dir):
        for f in files:
            if f.lower() in ("purr_sim.exe", "purr_sim"):
                exe = os.path.join(root, f)
                break
        if exe:
            break

    if not exe:
        err("purr_sim executable not found after build")

    info(f"simulator — launching {exe}")
    subprocess.Popen([exe], cwd=build_dir)


def sim_menu():
    """Interactive simulator shell picker."""
    div()
    print(f"\n  {C_WHT}{C_BOLD}PURR OS Simulator{C_RST}  {C_GRY}pick a UI shell to preview{C_RST}\n")

    active = {k: v for k, v in SIM_SHELLS.items() if "WIP" not in v[1]}
    wip    = {k: v for k, v in SIM_SHELLS.items() if "WIP"     in v[1]}

    for k, (_, label, desc) in active.items():
        print(f"  {C_WHT}[{k}]{C_RST} {label:<18}{C_GRY}{desc}{C_RST}")

    if wip:
        print(f"\n  {C_YLW}── WIP / TBD ─────────────────────────────────────────{C_RST}")
        for k, (_, label, desc) in wip.items():
            print(f"  {C_YLW}[{k}]{C_RST} {label:<18}{C_GRY}{desc}{C_RST}")

    print(f"\n  {C_WHT}[q]{C_RST} Back")
    print()
    choice = input("  Shell: ").strip()
    if choice in SIM_SHELLS:
        shell_key, label, _ = SIM_SHELLS[choice]
        if "WIP" in label:
            warn(f"{label} is not yet available — TBD")
            return
        info(f"launching {label} simulator")
        do_sim(shell_key)
    elif choice != "q":
        warn(f"Unknown option '{choice}'")


# ── CLI overrides ─────────────────────────────────────────────────────────────

def _apply_cli(cfg, args):
    if args.target:
        cfg["target"] = args.target
    if args.mini:
        cfg["modules"]["micropython"] = False
    if args.no_wifi:
        cfg["modules"]["wifi"] = False
    if args.no_bt:
        cfg["modules"]["bt"] = False
    if args.lora:
        cfg["modules"]["lora"] = True
    if args.no_lora:
        cfg["modules"]["lora"] = False
    if args.mesh:
        cfg["modules"]["mesh"] = True
    if args.mtp:
        cfg["modules"]["mtp"] = True
    if args.flasher:
        cfg["modules"]["flasher"] = True
    if args.gps:
        cfg["modules"]["gps"] = True
    if args.magidos:
        cfg["modules"]["magidos"] = True
    if args.magicmac:
        cfg["modules"]["magicmac"] = True
    if args.lora_kernel:
        cfg["lora_kernel"] = args.lora_kernel
    if args.ui_kernel:
        cfg["ui_kernel"] = args.ui_kernel
    if args.tdeck_plus:
        cfg["tdeck_plus"] = True
    if args.cyd_variant:
        cfg["cyd_variant"] = args.cyd_variant
    if args.baud:
        cfg["flash_baud"] = args.baud
    # Enforce hardware constraints for the (possibly new) target
    _sanitize_cfg(cfg)


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(
        prog="sdk_core.py",
        description="PURR OS SDK — build / flash / monitor tool",
    )
    p.add_argument("--target",      choices=list(TARGETS.keys()),
                   metavar="heltec|tembed_cc1101|cyd_s028r|cyd_s024c|cyd|cyd_boot|tdeck|tdeck_plus|jc3248w535|waveshare169")
    p.add_argument("--build",       action="store_true")
    p.add_argument("--flash",       metavar="PORT|auto", default="",
                   help="Flash to port. Use 'auto' to detect automatically.")
    p.add_argument("--monitor",     metavar="PORT|auto", default="",
                   help="Open serial monitor. Use 'auto' to detect automatically.")
    p.add_argument("--scan",        action="store_true",
                   help="Scan for connected serial devices and exit")
    p.add_argument("--clean",       action="store_true")
    p.add_argument("--mini",        action="store_true")
    p.add_argument("--no-wifi",     action="store_true", dest="no_wifi")
    p.add_argument("--no-bt",       action="store_true", dest="no_bt")
    p.add_argument("--lora",        action="store_true")
    p.add_argument("--no-lora",     action="store_true", dest="no_lora")
    p.add_argument("--mesh",        action="store_true")
    p.add_argument("--mtp",         action="store_true")
    p.add_argument("--flasher",     action="store_true")
    p.add_argument("--gps",         action="store_true")
    p.add_argument("--magidos",     action="store_true",
                   help="Enable MagiDOS 8086 emulator (requires 8MB PSRAM: tdeck_plus, jc3248w535)")
    p.add_argument("--magicmac",    action="store_true",
                   help="Enable MagicMac 68k emulator (requires 8MB PSRAM: tdeck_plus, jc3248w535)")
    p.add_argument("--lora-kernel", dest="lora_kernel", choices=list(LORA_KERNELS.keys()))
    p.add_argument("--ui-kernel",   dest="ui_kernel",   choices=list(UI_KERNELS.keys()))
    p.add_argument("--tdeck-plus",  action="store_true", dest="tdeck_plus")
    p.add_argument("--cyd-variant", dest="cyd_variant", choices=["s028r", "s024c"],
                   help="CYD display variant: s028r=original/XPT2046, s024c=newer/CST816S")
    p.add_argument("--configure",   action="store_true")
    p.add_argument("--baud",        type=int, default=0)
    p.add_argument("--full-build",  action="store_true", dest="full_build",
                   help="Build cyd_boot (factory) + cyd (OS) back-to-back")
    p.add_argument("--full-flash",  metavar="PORT", default="", dest="full_flash",
                   help="Flash factory + ota_0 + SPIFFS in one esptool call")
    p.add_argument("--flash-full",  metavar="PORT|auto", default="", dest="flash_full_explicit",
                   help="[T-Deck Plus] Full erase + explicit offset flash (0x0, 0x8000, 0xe000, 0x10000)")
    args = p.parse_args()

    cfg = load_cfg()

    if args.scan:
        ports = _scan_serial_ports()
        if not ports:
            print("  No serial devices found.")
        else:
            print(f"\n  {C_CYN}Detected serial ports:{C_RST}")
            for dev, desc in ports:
                tag = f"  {C_GRY}{desc}{C_RST}" if desc else ""
                print(f"  {dev}{tag}")
        print()
        return

    direct = (args.build or bool(args.flash) or bool(args.monitor)
              or args.configure or args.full_build or bool(args.full_flash)
              or bool(args.flash_full_explicit) or args.scan)
    if direct:
        _apply_cli(cfg, args)
        if args.configure:
            configure(cfg)
            return
        show_banner(cfg)
        if args.full_build:
            do_full_build(cfg, clean=args.clean)
        elif args.build:
            do_build(cfg, clean=args.clean)
        if args.flash_full_explicit:
            do_flash_full_explicit(cfg, port=args.flash_full_explicit)
        elif args.full_flash:
            do_full_flash(cfg, port=args.full_flash)
        elif args.flash:
            do_flash(cfg, port=args.flash)
        if args.monitor:
            do_monitor(cfg, port=args.monitor)
        return

    # Interactive — apply any pre-set target before entering menu
    if args.target:
        cfg["target"] = args.target
    main_menu(cfg)


if __name__ == "__main__":
    main()
