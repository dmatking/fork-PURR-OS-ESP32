#!/usr/bin/env python3
"""
purrstrap — PURR OS build system
pmbootstrap-style CLI for ESP32 firmware management.

Usage:
  purrstrap init                  interactive device wizard, saves .purrstrap
  purrstrap status                show current workspace config
  purrstrap list                  list all supported devices
  purrstrap build [DEVICE]        build firmware
  purrstrap flash [DEVICE] [-p]   flash to device
  purrstrap install [DEVICE] [-p] build + flash
  purrstrap monitor [-p PORT]     open serial monitor
  purrstrap clean [DEVICE]        delete build dir
  purrstrap bake [DEVICE|all]     build + pack to baked/<device>/
  purrstrap release [SET]         batch release build
  purrstrap doctor                check environment and repo health

Device config is stored in .purrstrap at the repo root.
Build artifacts go to CoreOS/build_<device>/ (unchanged from IDF convention).
Release artifacts go to baked/<device>/.
"""

import argparse
import datetime
import glob
import json
import os
import re
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

PURROS_VERSION = "0.11.0"
KITT_VERSION   = "0.8.0"

def info(msg):        print(f"{C_GRN}[purr]{C_RST} {msg}")
def warn(msg):        print(f"{C_YLW}[warn]{C_RST} {msg}")
def die(msg, code=1): print(f"{C_RED}[err] {C_RST} {msg}", file=sys.stderr); sys.exit(code)
def div():            print(f"{C_GRY}" + "─" * 50 + C_RST)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR   = SCRIPT_DIR
COREOS_DIR = os.path.join(REPO_DIR, "CoreOS")
DRIVERS_DIR= os.path.join(REPO_DIR, "drivers")
DEVICES_DIR= os.path.join(REPO_DIR, "devices")
BAKED_DIR  = os.path.join(REPO_DIR, "baked")
CFG_FILE   = os.path.join(REPO_DIR, ".purrstrap")

# ── Device registry ───────────────────────────────────────────────────────────

DEVICES = {
    "heltec": {
        "chip":        "esp32s3",
        "desc":        "Heltec WiFi LoRa 32 V3",
        "spec":        "ESP32-S3  8MB  SSD1306 OLED  SX1262 LoRa  KittenUI",
        "default_ui":  "none",
        "default_lora": True,
        "fixed":       False,
        "flash_size":  "8MB",
        "flash_freq":  "80m",
        "flash_mode":  "dio",
        "boot_offset": "0x0",
    },
    "tembed_cc1101": {
        "chip":        "esp32s3",
        "desc":        "LilyGo T-Embed CC1101",
        "spec":        "ESP32-S3R8  16MB  8MB PSRAM  ST7789 170x320  CC1101 sub-GHz  rotary encoder",
        "default_ui":  "none",
        "default_lora": False,
        "fixed":       False,
        "flash_size":  "16MB",
        "flash_freq":  "80m",
        "flash_mode":  "dio",
        "boot_offset": "0x0",
    },
    "cyd": {
        "chip":        "esp32",
        "desc":        "CYD (generic, maps to cyd_s028r/cyd_s024c)",
        "spec":        "ESP32  4MB  ILI9341 320x240  MiniWin WM",
        "default_ui":  "miniwin",
        "default_lora": False,
        "fixed":       False,
        "flash_size":  "4MB",
        "flash_freq":  "40m",
        "flash_mode":  "dio",
        "boot_offset": "0x1000",
    },
    "cyd_s028r": {
        "chip":        "esp32",
        "desc":        "CYD S028R (ESP32-2432S028R, original)",
        "spec":        "ESP32  4MB  ILI9341 2.4\" 320x240  XPT2046 SPI touch  MiniWin WM",
        "default_ui":  "miniwin",
        "default_lora": False,
        "fixed":       False,
        "flash_size":  "4MB",
        "flash_freq":  "40m",
        "flash_mode":  "dio",
        "boot_offset": "0x1000",
    },
    "cyd_s024c": {
        "chip":        "esp32",
        "desc":        "CYD S024C (ESP32-2432S024C, newer)",
        "spec":        "ESP32  4MB  ILI9341 2.4\" 240x320  CST816S I2C touch  MiniWin WM",
        "default_ui":  "miniwin",
        "default_lora": False,
        "fixed":       False,
        "flash_size":  "4MB",
        "flash_freq":  "40m",
        "flash_mode":  "dio",
        "boot_offset": "0x1000",
    },
    "cyd_boot": {
        "chip":        "esp32",
        "desc":        "CYD PURR Kernel (factory, all CYD variants)",
        "spec":        "ESP32  4MB  factory kernel, chainloads ota_0",
        "default_ui":  "none",
        "default_lora": False,
        "fixed":       True,
        "flash_size":  "4MB",
        "flash_freq":  "40m",
        "flash_mode":  "dio",
        "boot_offset": "0x1000",
    },
    "tdeck": {
        "chip":        "esp32s3",
        "desc":        "LilyGo T-Deck",
        "spec":        "ESP32-S3  16MB  ST7789 320x240  trackball  SX1262 LoRa  (WIP)",
        "default_ui":  "none",
        "default_lora": True,
        "fixed":       False,
        "flash_size":  "16MB",
        "flash_freq":  "80m",
        "flash_mode":  "dio",
        "boot_offset": "0x0",
    },
    "tdeck_plus": {
        "chip":        "esp32s3",
        "desc":        "LilyGo T-Deck Plus",
        "spec":        "ESP32-S3  16MB  8MB PSRAM  ST7789 320x240  GT911 cap touch  SPI3 SD  keyboard  trackball",
        "default_ui":  "miniwin",
        "default_lora": True,
        "fixed":       False,
        "flash_size":  "16MB",
        "flash_freq":  "80m",
        "flash_mode":  "dio",
        "boot_offset": "0x0",
    },
    "jc3248w535": {
        "chip":        "esp32s3",
        "desc":        "JC3248W535 3.5\"",
        "spec":        "ESP32-S3  16MB  8MB PSRAM  AXS15231B 320x480 QSPI  AXS15231B I2C touch",
        "default_ui":  "miniwin",
        "default_lora": False,
        "fixed":       False,
        "flash_size":  "16MB",
        "flash_freq":  "80m",
        "flash_mode":  "dio",
        "boot_offset": "0x0",
    },
    "waveshare169": {
        "chip":        "esp32s3",
        "desc":        "Waveshare 1.69\" ESP32-S3",
        "spec":        "ESP32-S3  4MB  ST7789 240x280  CST816S I2C touch",
        "default_ui":  "miniwin",
        "default_lora": False,
        "fixed":       False,
        "flash_size":  "4MB",
        "flash_freq":  "80m",
        "flash_mode":  "dio",
        "boot_offset": "0x0",
    },
}

MODULES = [
    {"key": "wifi",       "cmake": "PURR_ENABLE_WIFI",     "label": "WiFi",           "default": True,  "devices": None},
    {"key": "bt",         "cmake": "PURR_ENABLE_BT",       "label": "Bluetooth",      "default": True,  "devices": None},
    {"key": "shell",      "cmake": "PURR_ENABLE_SHELL",    "label": "Debug Shell",    "default": True,  "devices": None},
    {"key": "lua",        "cmake": "PURR_ENABLE_LUA",      "label": "Lua Runtime",    "default": True,  "devices": None},
    {"key": "micropython","cmake": "BUILD_MINI",           "label": "MicroPython",    "default": True,  "devices": None, "inverted": True},
    {"key": "lora",       "cmake": "PURR_ENABLE_LORA",     "label": "LoRa Radio",     "default": False, "devices": ["heltec","tdeck","tdeck_plus"]},
    {"key": "mesh",       "cmake": "PURR_ENABLE_MESH",     "label": "Meshtastic",     "default": False, "devices": ["heltec","tdeck","tdeck_plus"]},
    {"key": "gps",        "cmake": "PURR_ENABLE_GPS",      "label": "GPS",            "default": False, "devices": ["tdeck_plus"]},
    {"key": "magidos",    "cmake": "PURR_ENABLE_MAGIDOS",  "label": "MagiDOS",        "default": False, "devices": ["tdeck_plus","jc3248w535"]},
    {"key": "magicmac",   "cmake": "PURR_ENABLE_MAGICMAC", "label": "MagicMac",       "default": False, "devices": ["tdeck_plus","jc3248w535"]},
    {"key": "mtp",        "cmake": "PURR_ENABLE_MTP",      "label": "MTP USB",        "default": False, "devices": None},
    {"key": "flasher",    "cmake": "PURR_ENABLE_FLASHER",  "label": "OTA Flasher",    "default": False, "devices": None},
    {"key": "lte",        "cmake": "PURR_ENABLE_LTE",      "label": "LTE (exp)",      "default": False, "devices": None},
]

LORA_KERNELS = {"sx1262": "SX1262 SPI (Heltec V3, T-Deck)", "rak3172": "RAK3172 UART AT", "sx1276": "SX1276 SPI (RFM95W)"}
UI_KERNELS   = {"miniwin": "MiniWin WM", "none": "headless"}
UI_THEMES    = {"wce": "WCE Classic", "blackberry": "Blackberry", "luna": "Luna (XP blue)"}

DEFAULT_CFG = {
    "device":      "heltec",
    "ui_kernel":   None,
    "ui_theme":    "wce",
    "lora_kernel": "sx1262",
    "flash_port":  "",
    "flash_baud":  460800,
    "monitor_port":"",
    "modules": {m["key"]: m["default"] for m in MODULES},
}


# ── Config ────────────────────────────────────────────────────────────────────

def load_cfg():
    if os.path.exists(CFG_FILE):
        try:
            with open(CFG_FILE, "r", encoding="utf-8") as f:
                stored = json.load(f)
            cfg = {k: (dict(v) if isinstance(v, dict) else v) for k, v in DEFAULT_CFG.items()}
            for k, v in stored.items():
                if k == "modules" and isinstance(v, dict):
                    cfg["modules"].update(v)
                else:
                    cfg[k] = v
            _sanitize(cfg)
            return cfg
        except Exception as e:
            warn(f".purrstrap malformed ({e}) — using defaults")
    return {k: (dict(v) if isinstance(v, dict) else v) for k, v in DEFAULT_CFG.items()}


def save_cfg(cfg):
    with open(CFG_FILE, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)
    info("Config saved → .purrstrap")


def _sanitize(cfg):
    device = cfg.get("device", "heltec")
    d = DEVICES.get(device, {})

    if d.get("fixed"):
        for key in ("bt", "lora", "mesh", "mtp", "flasher", "micropython"):
            cfg["modules"][key] = False

    if device not in ("heltec", "tdeck", "tdeck_plus"):
        cfg["modules"]["lora"] = False
        cfg["modules"]["mesh"] = False

    if not cfg["modules"].get("lora"):
        cfg["modules"]["mesh"] = False

    if device in ("heltec", "tembed_cc1101"):
        cfg["ui_kernel"] = "none"
    elif cfg.get("ui_kernel") is None:
        cfg["ui_kernel"] = d.get("default_ui", "none")
    elif cfg.get("ui_kernel") not in UI_KERNELS:
        cfg["ui_kernel"] = d.get("default_ui", "none")

    if cfg.get("ui_theme") not in UI_THEMES:
        cfg["ui_theme"] = "wce"


# ── Serial port scanner ───────────────────────────────────────────────────────

_ESP32_VIDS = {0x1A86, 0x10C4, 0x0403, 0x303A, 0x239A}

def _scan_ports():
    results = []
    try:
        from serial.tools import list_ports
        for p in list_ports.comports():
            vid = getattr(p, "vid", None)
            desc = p.description or ""
            if vid in _ESP32_VIDS or any(s in desc.upper() for s in ("USB","CH340","CP210","FTDI")):
                results.append((p.device, desc))
    except ImportError:
        pass
    if not results and sys.platform.startswith("linux"):
        for pat in ("/dev/ttyUSB*", "/dev/ttyACM*", "/dev/ttyAMC*"):
            for dev in sorted(glob.glob(pat)):
                if dev not in [r[0] for r in results]:
                    results.append((dev, ""))
    return results


def _pick_port(label, saved=""):
    ports = _scan_ports()
    if not ports:
        example = "/dev/ttyUSB0" if sys.platform.startswith("linux") else "COM8"
        val = input(f"  {label} (e.g. {example}, blank to skip) [{saved}]: ").strip()
        return val or saved
    print(f"\n  {C_CYN}Detected serial ports:{C_RST}")
    for i, (dev, desc) in enumerate(ports, 1):
        tag = f"  {C_GRY}{desc}{C_RST}" if desc else ""
        print(f"  [{i}] {dev}{tag}")
    default = ports[0][0] if len(ports) == 1 else (saved if saved in [p[0] for p in ports] else ports[0][0])
    raw = input(f"\n  {label} — number or path (Enter = {default}): ").strip()
    if not raw:
        return default
    if raw.isdigit():
        idx = int(raw) - 1
        return ports[idx][0] if 0 <= idx < len(ports) else default
    return raw if raw.startswith("/") else "/dev/" + raw


# ── IDF helpers ───────────────────────────────────────────────────────────────

def _idf_path():
    p = os.environ.get("IDF_PATH", "")
    if p and os.path.isfile(os.path.join(p, "tools", "idf.py")):
        return p
    for c in (os.path.expanduser("~/esp/idf"), os.path.expanduser("~/esp/esp-idf"), "/opt/esp-idf"):
        if os.path.isfile(os.path.join(c, "tools", "idf.py")):
            os.environ["IDF_PATH"] = c
            return c
    return ""


def _idf_py():
    idf = _idf_path()
    if idf:
        candidate = os.path.join(idf, "tools", "idf.py")
        if os.path.exists(candidate):
            matches = sorted(glob.glob(os.path.join(os.path.expanduser("~"), ".espressif",
                "python_env", "idf*", "bin", "python")), reverse=True)
            python = matches[0] if matches else sys.executable
            return [python, candidate]
    return ["idf.py"]


def _build_dir(device):
    return os.path.join(COREOS_DIR, f"build_{device}")


def _sdkconfig_path(device):
    return os.path.join(COREOS_DIR, f"sdkconfig_{device}")


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


# ── CMake flag builder ────────────────────────────────────────────────────────

def _cmake_flags(cfg):
    device = cfg["device"]
    d = DEVICES[device]
    mods = cfg["modules"]
    fixed = d["fixed"]

    flags = [f"-DTARGET_DEVICE={device}"]

    for m in MODULES:
        key = m["key"]
        inverted = m.get("inverted", False)
        cmake_name = m["cmake"]
        allowed = (m["devices"] is None) or (device in m["devices"])
        val = (not fixed) and allowed and mods.get(key, m["default"])
        if key == "mesh" and not mods.get("lora"):
            val = False
        cmake_val = (0 if val else 1) if inverted else (1 if val else 0)
        flags.append(f"-D{cmake_name}={cmake_val}")

    axs_path = cfg.get("axs15231b_path", "")
    if axs_path:
        flags.append(f"-DAXS15231B_COMPONENT_PATH={axs_path}")

    ui = "none" if fixed else cfg.get("ui_kernel", d.get("default_ui", "none"))
    flags.append(f"-DPURR_UI_KERNEL={ui}")
    theme = cfg.get("ui_theme", "wce") if ui == "miniwin" and not fixed else "wce"
    flags.append(f"-DPURR_UI_THEME={theme}")

    return flags


# ── SPIFFS builder ────────────────────────────────────────────────────────────

def _build_spiffs(device):
    idf = _idf_path()
    spiffsgen = os.path.join(idf, "components", "spiffs", "spiffsgen.py") if idf else ""
    if not spiffsgen or not os.path.exists(spiffsgen):
        warn("spiffsgen.py not found — skipping SPIFFS image"); return

    bdir = _build_dir(device)
    staging = os.path.join(bdir, "spiffs_staging")
    if os.path.exists(staging):
        shutil.rmtree(staging)
    for d in ("system/kernel", "system/logs", "apps"):
        os.makedirs(os.path.join(staging, d), exist_ok=True)

    _cyd_family = ("cyd", "cyd_boot", "cyd_s024c", "cyd_s028r")
    dev_target = "cyd" if device in _cyd_family else device
    device_src = os.path.join(COREOS_DIR, "system", "kernel", "devices", f"{dev_target}.json")
    if not os.path.exists(device_src):
        device_src = os.path.join(COREOS_DIR, "system", "kernel", "device.json")
    if not os.path.exists(device_src):
        warn(f"No device JSON for '{dev_target}' — skipping SPIFFS"); return

    shutil.copy2(device_src, os.path.join(staging, "system", "kernel", "device.json"))

    spiffs_size = 458752 if device in _cyd_family else 327680
    img = os.path.join(bdir, "spiffs.bin")
    rc = run_live([sys.executable, spiffsgen, str(spiffs_size), staging, img])
    if rc != 0:
        die(f"spiffsgen failed (exit {rc})")
    info(f"SPIFFS image: build_{device}/spiffs.bin ({spiffs_size // 1024} KB)")


# ── LoRa kernel installer ─────────────────────────────────────────────────────

_LORA_KERNEL_MAP = {"sx1262": "SX1262", "rak3172": "RAK3172", "sx1276": "SX1276_RFM95W"}

def _install_lora_kernel(cfg):
    device = cfg["device"]
    if not cfg["modules"].get("lora"):
        return
    folder = _LORA_KERNEL_MAP.get(cfg.get("lora_kernel", "sx1262"))
    if not folder:
        return
    bundled = os.path.join(DRIVERS_DIR, "drv_lora", "kernels", folder)
    if os.path.isdir(bundled):
        return
    src = os.path.join(REPO_DIR, "LoRa Kernels", folder)
    dst = os.path.join(COREOS_DIR, "system", "kernel", "modules")
    if not os.path.isdir(src):
        warn(f"LoRa kernel not found: {src}"); return
    info(f"LoRa kernel → {folder}")
    for item in os.listdir(src):
        s, d = os.path.join(src, item), os.path.join(dst, item)
        shutil.copy2(s, d) if os.path.isfile(s) else shutil.copytree(s, d, dirs_exist_ok=True)


# ── Defaults installer ────────────────────────────────────────────────────────

def _install_defaults(device):
    defaults_target = "cyd" if device == "cyd_boot" else device
    sdk_dir = os.path.join(REPO_DIR, "SDK", "targets")
    src = os.path.join(sdk_dir, f"{defaults_target}.defaults")
    dst = os.path.join(COREOS_DIR, "sdkconfig.defaults")
    if os.path.exists(src):
        shutil.copy2(src, dst)
        info(f"sdkconfig.defaults ← {defaults_target}.defaults")
    else:
        warn(f"No SDK/targets/{defaults_target}.defaults found — using existing sdkconfig.defaults")


# ── Build ─────────────────────────────────────────────────────────────────────

def cmd_build(cfg, clean=False):
    device = cfg["device"]
    d = DEVICES[device]
    chip = d["chip"]
    flags = _cmake_flags(cfg)

    _install_defaults(device)
    bdir = _build_dir(device)
    bdn = os.path.basename(bdir)
    sdkcfg = _sdkconfig_path(device)

    if clean:
        info(f"clean {bdn}...")
        if os.path.isdir(bdir): shutil.rmtree(bdir)
        if os.path.exists(sdkcfg): os.remove(sdkcfg)

    _install_lora_kernel(cfg)

    idf_args = ["-B", bdn, f"-DSDKCONFIG={sdkcfg}"]

    info(f"set-target {chip}  [{bdn}]")
    rc = run_live(_idf_py() + idf_args + flags + ["set-target", chip], cwd=COREOS_DIR)
    if rc != 0: die(f"set-target failed (exit {rc})")

    mods = cfg["modules"]
    info(f"build {device}  bt={int(mods.get('bt',True))}  lora={int(mods.get('lora',False))}  "
         f"ui={cfg.get('ui_kernel','none')}/{cfg.get('ui_theme','wce')}")
    rc = run_live(_idf_py() + idf_args + flags + ["build"], cwd=COREOS_DIR)
    if rc != 0: die(f"build failed (exit {rc})")

    _build_spiffs(device)
    info(f"build done → {bdn}/")


# ── Flash ─────────────────────────────────────────────────────────────────────

def cmd_flash(cfg, port=None):
    device = cfg["device"]
    d = DEVICES[device]
    port = port or cfg.get("flash_port") or ""
    if not port or port == "auto":
        port = _pick_port("Flash port", saved=cfg.get("flash_port",""))
        if not port: die("No flash port. Connect your device and try again.")
        cfg["flash_port"] = port
        save_cfg(cfg)

    bdir = _build_dir(device)
    spiffs_img = os.path.join(bdir, "spiffs.bin")

    _cyd_targets = ("cyd", "cyd_s024c", "cyd_s028r", "cyd_boot")
    app_offset    = "0x10000"
    spiffs_offset = "0x390000" if device in _cyd_targets else "0x3b0000"
    boot_offset   = d.get("boot_offset", "0x0")

    cmd = [
        sys.executable, "-m", "esptool",
        "--chip",   d["chip"],
        "--port",   port,
        "-b",       str(cfg.get("flash_baud", 460800)),
        "--before", "default_reset",
        "--after",  "hard_reset",
        "write_flash",
        "--flash_mode", d["flash_mode"],
        "--flash_size", d["flash_size"],
        "--flash_freq", d["flash_freq"],
        boot_offset,  os.path.join(bdir, "bootloader", "bootloader.bin"),
        "0x8000",     os.path.join(bdir, "partition_table", "partition-table.bin"),
        app_offset,   os.path.join(bdir, "purr_os_core.bin"),
    ]
    ota_bin = os.path.join(bdir, "ota_data_initial.bin")
    if os.path.exists(ota_bin):
        cmd += ["0xe000", ota_bin]
    if os.path.exists(spiffs_img):
        cmd += [spiffs_offset, spiffs_img]
        info(f"including SPIFFS at {spiffs_offset}")

    info(f"flashing {device} → {port}")
    rc = run_live(cmd)
    if rc != 0: die(f"flash failed (exit {rc})")


# ── Monitor ───────────────────────────────────────────────────────────────────

def cmd_monitor(cfg, port=None):
    port = port or cfg.get("monitor_port") or cfg.get("flash_port") or ""
    if not port or port == "auto":
        port = _pick_port("Monitor port", saved=cfg.get("flash_port",""))
        if not port: die("No monitor port.")
        cfg["monitor_port"] = port
        save_cfg(cfg)
    info(f"monitor on {port}  (Ctrl+] to exit)")
    bdn = os.path.basename(_build_dir(cfg["device"]))
    run_live(_idf_py() + ["-B", bdn, "-p", port, "monitor"], cwd=COREOS_DIR)


# ── Clean ─────────────────────────────────────────────────────────────────────

def cmd_clean(device):
    bdir = _build_dir(device)
    sdkcfg = _sdkconfig_path(device)
    removed = False
    if os.path.isdir(bdir):
        shutil.rmtree(bdir)
        info(f"removed {os.path.basename(bdir)}/")
        removed = True
    if os.path.exists(sdkcfg):
        os.remove(sdkcfg)
        info(f"removed {os.path.basename(sdkcfg)}")
        removed = True
    if not removed:
        info(f"nothing to clean for {device}")


# ── Bake (pack to baked/) ─────────────────────────────────────────────────────

_DEVICE_NOTES = {
    "tdeck_plus": [
        "GT911 capacitive touch — no resistive calibration screen, calibration is MiniWin 3-point on first boot.",
        "I2C bus shared between GT911 (0x5D) and keyboard co-processor (0x55) on SDA=18 SCL=8.",
        "ST7789 landscape — swap_xy + mirror applied in driver, no rotation needed.",
        "Trackball cursor auto-hides after 5s of inactivity.",
        "SPI3 shared bus: display (CS=12) + SD card (CS=39) on MOSI=41 MISO=38 SCLK=40.",
        "GPIO 10: peripheral power enable — must be HIGH before SPI init.",
    ],
    "cyd_s028r": [
        "XPT2046 SPI resistive touch — 3-point calibration runs on first boot, stored in NVS.",
        "Display: ILI9341 2.4\" 320x240 landscape. Backlight GPIO 21.",
    ],
    "cyd_s024c": [
        "CST816S I2C capacitive touch on SDA=33 SCL=32. Backlight GPIO 27 (not 21).",
        "Display: ILI9341 2.4\" 240x320 portrait.",
    ],
    "cyd_boot": [
        "Factory kernel — chainloads ota_0. Flash this first, then flash OS to ota_0 (0x10000).",
    ],
    "jc3248w535": [
        "GT911 capacitive touch on SDA=19 SCL=20.",
        "8MB PSRAM — MagiDOS and MagicMac enabled in release builds.",
        "Display: ST7796 3.5\" 480x320.",
    ],
    "waveshare169": [
        "CST816S I2C touch on SDA=6 SCL=7.",
        "Display: ST7789 1.69\" 240x280.",
    ],
    "heltec": [
        "SX1262 LoRa enabled by default. Antenna required before transmitting.",
        "Display: SSD1306 0.96\" OLED 128x64. KittenUI shell.",
    ],
    "tdeck": [
        "WIP — trackball + keyboard work; touch not yet functional.",
        "SX1262 LoRa enabled by default.",
    ],
}


def cmd_bake(cfg):
    device = cfg["device"]
    d = DEVICES[device]
    bdir = _build_dir(device)
    out = os.path.join(BAKED_DIR, device)
    os.makedirs(out, exist_ok=True)

    fargs_path = os.path.join(bdir, "flasher_args.json")
    if not os.path.exists(fargs_path):
        die(f"flasher_args.json not found — run `purrstrap build {device}` first")

    with open(fargs_path) as f:
        fargs = json.load(f)

    flash_files = fargs.get("flash_files", {})
    flash_mode  = fargs.get("flash_settings", {}).get("flash_mode", d["flash_mode"])
    flash_freq  = fargs.get("flash_settings", {}).get("flash_freq", d["flash_freq"])
    flash_size  = fargs.get("flash_settings", {}).get("flash_size", d["flash_size"])
    chip        = fargs.get("extra_esptool_args", {}).get("chip", d["chip"])

    copied = {}
    for offset, rel_path in flash_files.items():
        src = os.path.join(bdir, rel_path)
        if not os.path.exists(src):
            warn(f"  missing: {rel_path}"); continue
        fname = os.path.basename(rel_path)
        shutil.copy2(src, os.path.join(out, fname))
        copied[offset] = fname
        info(f"  {offset}  {fname}")

    _cyd_family = ("cyd", "cyd_boot", "cyd_s024c", "cyd_s028r")
    spiffs_src = os.path.join(bdir, "spiffs.bin")
    spiffs_off = "0x390000" if device in _cyd_family else "0x3b0000"
    if os.path.exists(spiffs_src):
        shutil.copy2(spiffs_src, os.path.join(out, "spiffs.bin"))
        copied[spiffs_off] = "spiffs.bin"
        info(f"  {spiffs_off}  spiffs.bin")

    _write_flash_sh(out, device, chip, flash_mode, flash_freq, flash_size, copied)
    _write_flash_guide(out, device, chip, flash_mode, flash_freq, flash_size, copied)
    info(f"baked → baked/{device}/")


def _write_flash_sh(out, device, chip, flash_mode, flash_freq, flash_size, files):
    file_args = " \\\n    ".join(f"{off} {fname}" for off, fname in sorted(files.items()))
    lines = [
        "#!/usr/bin/env bash",
        f"# PURR OS — {device} flash script",
        f"# Generated by purrstrap on {datetime.datetime.now().strftime('%Y-%m-%d')}",
        "",
        'PORT="${1:-/dev/ttyUSB0}"',
        "",
        "python -m esptool \\",
        f"    --chip {chip} \\",
        '    --port "$PORT" \\',
        "    -b 460800 \\",
        "    --before default_reset --after hard_reset \\",
        f"    write_flash --flash_mode {flash_mode} --flash_size {flash_size} --flash_freq {flash_freq} \\",
        f"    {file_args}",
        "",
    ]
    path = os.path.join(out, "flash.sh")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines))
    os.chmod(path, 0o755)
    info("  flash.sh written")


def _write_flash_guide(out, device, chip, flash_mode, flash_freq, flash_size, files):
    d = DEVICES[device]
    notes = _DEVICE_NOTES.get(device, [])
    file_args = " ".join(f"{off} {fname}" for off, fname in sorted(files.items()))
    cmd = (f"python -m esptool --chip {chip} -p PORT -b 460800 "
           f"--before default_reset --after hard_reset "
           f"write_flash --flash_mode {flash_mode} --flash_size {flash_size} "
           f"--flash_freq {flash_freq} {file_args}")

    _purpose = {
        "bootloader.bin": "ESP-IDF bootloader",
        "partition-table.bin": "Partition table",
        "ota_data_initial.bin": "OTA slot selector",
        "purr_os_core.bin": "PURR OS application",
        "spiffs.bin": "SPIFFS filesystem",
    }

    lines = [
        f"# PURR OS — {d['desc']}",
        "",
        f"**PURR OS:** v{PURROS_VERSION}  ",
        f"**Built:** {datetime.datetime.now().strftime('%Y-%m-%d')}  ",
        f"**Target:** `{device}`  **Chip:** {chip}  ",
        f"**Flash:** {flash_mode.upper()} / {flash_freq} / {flash_size}  ",
        "",
        "## Files",
        "",
        "| Offset | File | Purpose |",
        "|--------|------|---------|",
    ]
    for off, fname in sorted(files.items()):
        lines.append(f"| `{off}` | `{fname}` | {_purpose.get(fname, fname)} |")

    lines += [
        "",
        "## Flash command",
        "",
        "```bash",
        cmd,
        "```",
        "",
        "Or run: `./flash.sh /dev/ttyUSB0`",
        "",
        "## Device notes",
        "",
    ]
    for note in notes:
        lines.append(f"- {note}")
    if not notes:
        lines.append("- See PURR OS documentation.")

    with open(os.path.join(out, "FLASH_GUIDE.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    info("  FLASH_GUIDE.md written")


# ── Release build ─────────────────────────────────────────────────────────────

RELEASE_SETS = {
    "all":     ["cyd_s028r", "cyd_s024c", "cyd_boot", "tdeck_plus", "jc3248w535", "waveshare169", "heltec"],
    "miniwin": ["cyd_s028r", "cyd_s024c", "tdeck_plus", "jc3248w535", "waveshare169"],
    "s3":      ["tdeck_plus", "jc3248w535", "waveshare169", "heltec"],
    "cyd":     ["cyd_s028r", "cyd_s024c", "cyd_boot"],
}


def cmd_release(targets, clean=False):
    passed, failed = [], []
    for i, device in enumerate(targets, 1):
        div()
        info(f"[{i}/{len(targets)}] {device}")
        rcfg = load_cfg()
        rcfg["device"] = device
        _sanitize(rcfg)
        if device in ("tdeck_plus", "jc3248w535"):
            rcfg["modules"]["magidos"] = True
        try:
            cmd_build(rcfg, clean=clean)
            cmd_bake(rcfg)
            passed.append(device)
        except SystemExit as e:
            failed.append(device)
            warn(f"  {device} FAILED (exit {e.code}) — continuing")
    div()
    info(f"OK: {' '.join(passed)}" if passed else "No successful builds")
    if failed:
        warn(f"Failed: {' '.join(failed)}")


# ── Status ────────────────────────────────────────────────────────────────────

def cmd_status(cfg):
    device = cfg["device"]
    d = DEVICES.get(device, {})
    mods = cfg["modules"]
    bdir = _build_dir(device)
    built = os.path.exists(os.path.join(bdir, "purr_os_core.bin"))
    baked_bin = os.path.exists(os.path.join(BAKED_DIR, device, "purr_os_core.bin"))

    div()
    print(f"\n  {C_WHT}{C_BOLD}PURR OS v{PURROS_VERSION}{C_RST}  {C_GRY}KITT v{KITT_VERSION}{C_RST}")
    print(f"\n  Device   : {C_CYN}{device}{C_RST}  {C_GRY}({d.get('desc','')}){C_RST}")
    print(f"  Chip     : {d.get('chip','?')}")
    print(f"  UI       : {cfg.get('ui_kernel','none')}/{cfg.get('ui_theme','wce')}")
    print(f"  LoRa     : {'yes, kernel=' + cfg.get('lora_kernel','sx1262') if mods.get('lora') else 'no'}")

    mod_on = [m["label"] for m in MODULES if mods.get(m["key"]) and not m.get("inverted")]
    mod_on += (["MicroPython"] if mods.get("micropython") else [])
    print(f"  Modules  : {', '.join(mod_on) if mod_on else '(none)'}")

    print(f"\n  Build dir: {C_GRN if built else C_GRY}build_{device}/ {'[built]' if built else '[not built]'}{C_RST}")
    print(f"  Baked    : {C_GRN if baked_bin else C_GRY}baked/{device}/ {'[ready]' if baked_bin else '[not baked]'}{C_RST}")

    if cfg.get("flash_port"):   print(f"  Flash    : {cfg['flash_port']}")
    if cfg.get("monitor_port"): print(f"  Monitor  : {cfg['monitor_port']}")
    div()
    print()


# ── List ──────────────────────────────────────────────────────────────────────

def cmd_list():
    div()
    print(f"\n  {C_WHT}{C_BOLD}PURR OS supported devices{C_RST}\n")
    for name, d in DEVICES.items():
        bdir = _build_dir(name)
        built = os.path.exists(os.path.join(bdir, "purr_os_core.bin"))
        baked = os.path.exists(os.path.join(BAKED_DIR, name, "purr_os_core.bin"))
        tag = ""
        if built: tag += f"  {C_GRN}[built]{C_RST}"
        if baked: tag += f"  {C_GRN}[baked]{C_RST}"
        print(f"  {C_CYN}{name:<16}{C_RST} {d['desc']:<40}  {C_GRY}{d['spec']}{C_RST}{tag}")
    print()
    div()


# ── Init wizard ───────────────────────────────────────────────────────────────

def cmd_init(cfg):
    print(f"\n  {C_WHT}{C_BOLD}purrstrap init — device configuration{C_RST}\n")

    # Pick device
    keys = list(DEVICES.keys())
    for i, k in enumerate(keys, 1):
        d = DEVICES[k]
        print(f"  [{i:2}]  {k:<16}  {C_GRY}{d['desc']}{C_RST}")
    print()
    raw = input("  Device [1]: ").strip() or "1"
    try:
        cfg["device"] = keys[int(raw) - 1]
    except (ValueError, IndexError):
        cfg["device"] = keys[0]

    device = cfg["device"]
    d = DEVICES[device]
    _sanitize(cfg)

    # UI kernel
    if device not in ("heltec", "tembed_cc1101") and not d["fixed"]:
        print(f"\n  UI kernel: [1] miniwin  [2] none")
        ui_raw = input("  Choice [2]: ").strip() or "2"
        cfg["ui_kernel"] = "miniwin" if ui_raw == "1" else "none"
        if cfg["ui_kernel"] == "miniwin":
            print(f"\n  Theme: [1] wce  [2] blackberry  [3] luna")
            t_raw = input("  Choice [1]: ").strip() or "1"
            cfg["ui_theme"] = {"1": "wce", "2": "blackberry", "3": "luna"}.get(t_raw, "wce")

    # Modules toggle
    mods = cfg["modules"]
    visible = [m for m in MODULES
               if not d["fixed"]
               and (m["devices"] is None or device in m["devices"])]
    print(f"\n  {C_WHT}Modules (toggle by number, Enter to accept):{C_RST}\n")
    while True:
        for i, m in enumerate(visible, 1):
            on = mods.get(m["key"], m["default"])
            state = f"{C_GRN}ON {C_RST}" if on else f"{C_GRY}off{C_RST}"
            print(f"  [{i}]  {state}  {m['label']}")
        raw = input("\n  Toggle # or Enter: ").strip()
        if not raw:
            break
        try:
            idx = int(raw) - 1
            m = visible[idx]
            key = m["key"]
            if key == "mesh" and not mods.get("lora"):
                warn("Mesh requires LoRa — enable LoRa first"); continue
            mods[key] = not mods.get(key, m["default"])
        except (ValueError, IndexError):
            pass

    # LoRa kernel
    if mods.get("lora"):
        print(f"\n  LoRa kernel: [1] sx1262  [2] rak3172  [3] sx1276")
        lk = input("  Choice [1]: ").strip() or "1"
        cfg["lora_kernel"] = {"1": "sx1262", "2": "rak3172", "3": "sx1276"}.get(lk, "sx1262")

    # Ports
    print()
    port = _pick_port("Flash port", saved=cfg.get("flash_port",""))
    if port: cfg["flash_port"] = port
    mon = _pick_port("Monitor port", saved=cfg.get("monitor_port","") or port)
    if mon: cfg["monitor_port"] = mon

    save_cfg(cfg)
    print()
    cmd_status(cfg)


# ── Doctor ───────────────────────────────────────────────────────────────────

def cmd_doctor():
    ok_count = 0
    warn_count = 0
    fail_count = 0

    def _ok(label, detail=""):
        nonlocal ok_count; ok_count += 1
        tag = f"  {C_GRY}{detail}{C_RST}" if detail else ""
        print(f"  {C_GRN}[ok]{C_RST}   {label}{tag}")

    def _warn(label, detail="", fix=""):
        nonlocal warn_count; warn_count += 1
        tag = f"  {C_GRY}{detail}{C_RST}" if detail else ""
        print(f"  {C_YLW}[warn]{C_RST} {label}{tag}")
        if fix:
            print(f"         {C_GRY}→ {fix}{C_RST}")

    def _fail(label, detail="", fix=""):
        nonlocal fail_count; fail_count += 1
        tag = f"  {C_GRY}{detail}{C_RST}" if detail else ""
        print(f"  {C_RED}[fail]{C_RST} {label}{tag}")
        if fix:
            print(f"         {C_GRY}→ {fix}{C_RST}")

    div()
    print(f"\n  {C_WHT}{C_BOLD}purrstrap doctor{C_RST}  {C_GRY}v{PURROS_VERSION}{C_RST}\n")

    # ── Python ────────────────────────────────────────────────────────────────
    pver = sys.version_info
    if pver >= (3, 8):
        _ok("Python", f"{pver.major}.{pver.minor}.{pver.micro}")
    else:
        _fail("Python 3.8+ required", f"found {pver.major}.{pver.minor}",
              "upgrade Python")

    # ── ESP-IDF ───────────────────────────────────────────────────────────────
    idf = _idf_path()
    if not idf:
        _fail("ESP-IDF not found",
              "IDF_PATH not set and not found in ~/esp/idf or ~/esp/esp-idf",
              "cd ~/esp/idf && . ./export.sh")
    else:
        # Read version from idf_version.h or version.txt
        ver_file = os.path.join(idf, "components", "esp_common", "include", "esp_idf_version.h")
        ver_str = ""
        if os.path.exists(ver_file):
            with open(ver_file) as f:
                content = f.read()
            import re as _re
            maj = _re.search(r'ESP_IDF_VERSION_MAJOR\s+(\d+)', content)
            min_ = _re.search(r'ESP_IDF_VERSION_MINOR\s+(\d+)', content)
            pat = _re.search(r'ESP_IDF_VERSION_PATCH\s+(\d+)', content)
            if maj and min_ and pat:
                ver_str = f"{maj.group(1)}.{min_.group(1)}.{pat.group(1)}"
        label = f"ESP-IDF {ver_str}" if ver_str else "ESP-IDF (version unknown)"
        if ver_str.startswith("5.3") or ver_str.startswith("5.4"):
            _ok(label, idf)
        elif ver_str:
            _warn(label, idf, "PURR OS targets IDF 5.3.5 — other versions may have issues")
        else:
            _ok(f"ESP-IDF found", idf)

    # ── IDF environment variables ─────────────────────────────────────────────
    if os.environ.get("IDF_PATH"):
        _ok("IDF_PATH set", os.environ["IDF_PATH"])
    else:
        _fail("IDF_PATH not set",
              fix=". ~/esp/idf/export.sh")

    if os.environ.get("ESP_ROM_ELF_DIR"):
        _ok("ESP_ROM_ELF_DIR set")
    else:
        _warn("ESP_ROM_ELF_DIR not set",
              "gdbinit generation will warn but builds still succeed",
              ". ~/esp/idf/export.sh  (re-source to pick up all env vars)")

    # ── IDF Python venv ───────────────────────────────────────────────────────
    venv_matches = sorted(glob.glob(os.path.join(
        os.path.expanduser("~"), ".espressif", "python_env", "idf*", "bin", "python"
    )), reverse=True)
    if venv_matches:
        _ok("IDF Python venv", venv_matches[0])
    else:
        _warn("IDF Python venv not found",
              "~/.espressif/python_env/idf*/bin/python missing",
              "cd ~/esp/idf && ./install.sh esp32,esp32s3")

    # ── esptool ───────────────────────────────────────────────────────────────
    try:
        r = subprocess.run([sys.executable, "-m", "esptool", "version"],
                           capture_output=True, text=True, timeout=5)
        ver_line = r.stdout.strip().splitlines()[0] if r.stdout.strip() else "?"
        _ok("esptool", ver_line)
    except Exception:
        _fail("esptool not found", fix="pip install esptool")

    # ── pyserial ──────────────────────────────────────────────────────────────
    try:
        import serial
        _ok("pyserial", getattr(serial, "__version__", "installed"))
    except ImportError:
        _warn("pyserial not installed",
              "port auto-detection disabled",
              "pip install pyserial")

    # ── Repo layout ───────────────────────────────────────────────────────────
    print()
    for d_path, label in [
        (COREOS_DIR,  "CoreOS/"),
        (DRIVERS_DIR, "drivers/"),
        (os.path.join(REPO_DIR, "ui"), "ui/"),
        (DEVICES_DIR, "devices/"),
    ]:
        if os.path.isdir(d_path):
            _ok(label)
        else:
            _fail(f"{label} missing", fix="repo may be corrupted — check git status")

    # ── SDK defaults ──────────────────────────────────────────────────────────
    sdk_targets = os.path.join(REPO_DIR, "SDK", "targets")
    missing_defaults = []
    for device in DEVICES:
        target = "cyd" if device == "cyd_boot" else device
        f = os.path.join(sdk_targets, f"{target}.defaults")
        if not os.path.exists(f):
            missing_defaults.append(target)
    if missing_defaults:
        _warn("Missing sdkconfig defaults",
              f"SDK/targets/: {' '.join(missing_defaults)}",
              "build will use existing sdkconfig.defaults — may use wrong settings")
    else:
        _ok("SDK/targets/ defaults", f"{len(DEVICES)} device defaults present")

    # ── Device overlay folders ────────────────────────────────────────────────
    missing_devs = []
    for device in DEVICES:
        # cyd_boot shares the cyd HAL folder — no separate overlay needed
        folder = "cyd" if device == "cyd_boot" else device
        d_path = os.path.join(DEVICES_DIR, folder)
        if not os.path.isdir(d_path):
            missing_devs.append(device)
    if missing_devs:
        _warn("Missing device overlay folders",
              f"devices/: {' '.join(missing_devs)}")
    else:
        _ok("Device overlay folders", f"{len(DEVICES)} device folders present")

    # ── .purrstrap config ─────────────────────────────────────────────────────
    print()
    if os.path.exists(CFG_FILE):
        try:
            with open(CFG_FILE) as f:
                cfg = json.load(f)
            device = cfg.get("device", "?")
            _ok(".purrstrap config", f"device={device}  ui={cfg.get('ui_kernel','?')}")
        except Exception as e:
            _warn(".purrstrap exists but is malformed", str(e),
                  "purrstrap init  to reconfigure")
    else:
        _warn(".purrstrap not found",
              "no device configured",
              "purrstrap init")

    # ── Serial ports ──────────────────────────────────────────────────────────
    ports = _scan_ports()
    if ports:
        _ok(f"{len(ports)} serial port(s) detected",
            "  ".join(p[0] for p in ports))
    else:
        _warn("No serial ports detected", "device not connected or driver missing")

    # ── Summary ───────────────────────────────────────────────────────────────
    print()
    div()
    total = ok_count + warn_count + fail_count
    print(f"\n  {C_GRN}{ok_count} ok{C_RST}  "
          f"{C_YLW}{warn_count} warnings{C_RST}  "
          f"{C_RED}{fail_count} failures{C_RST}  "
          f"{C_GRY}({total} checks){C_RST}\n")
    if fail_count:
        print(f"  {C_RED}Build will likely fail — fix failures above first.{C_RST}\n")
    elif warn_count:
        print(f"  {C_YLW}Builds should work. Warnings won't block compilation.{C_RST}\n")
    else:
        print(f"  {C_GRN}Everything looks good. Ready to build.{C_RST}\n")


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(
        prog="purrstrap",
        description="PURR OS build system — pmbootstrap-style CLI for ESP32 firmware",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
subcommands:
  init          interactive device wizard, saves .purrstrap
  status        show current workspace config
  list          list all supported devices
  build         build firmware  [-d DEVICE] [--clean]
  flash         flash to device [-d DEVICE] [-p PORT]
  install       build + flash   [-d DEVICE] [-p PORT] [--clean]
  monitor       serial monitor  [-p PORT]
  clean         delete build dir [-d DEVICE]
  bake          build + pack to baked/ [-d DEVICE]
  release       batch release  [SET|DEVICE,...] [--clean]
""",
    )
    p.add_argument("-d", "--device", choices=list(DEVICES.keys()), metavar="DEVICE",
                   help="override configured device")
    p.add_argument("-p", "--port", default="", metavar="PORT",
                   help="serial port (or 'auto')")
    p.add_argument("--clean", action="store_true", help="clean before build")
    p.add_argument("--axs15231b-path", default="", metavar="PATH",
                   help="path to local esp_lcd_axs15231b component dir (skips managed download)")
    p.add_argument("--version", action="version",
                   version=f"purrstrap {PURROS_VERSION} / KITT {KITT_VERSION}")
    p.add_argument("subcommand", nargs="?", default="status",
                   choices=["init","status","list","build","flash","install",
                            "monitor","clean","bake","release","scan","doctor"])
    p.add_argument("args", nargs="*", help="subcommand arguments")
    args = p.parse_args()

    cfg = load_cfg()
    if args.device:
        cfg["device"] = args.device
        _sanitize(cfg)

    # AXS15231B path: CLI flag takes priority, then env var, then empty (use managed component)
    axs_path = getattr(args, "axs15231b_path", "") or os.environ.get("AXS15231B_PATH", "")
    if axs_path:
        cfg["axs15231b_path"] = os.path.abspath(axs_path)

    sub = args.subcommand

    if sub == "init":
        cmd_init(cfg)

    elif sub == "status":
        cmd_status(cfg)

    elif sub == "list":
        cmd_list()

    elif sub == "doctor":
        cmd_doctor()

    elif sub == "scan":
        ports = _scan_ports()
        if not ports:
            print("  No serial devices found.")
        else:
            print(f"\n  {C_CYN}Detected serial ports:{C_RST}")
            for dev, desc in ports:
                print(f"  {dev}  {C_GRY}{desc}{C_RST}")
        print()

    elif sub == "build":
        if args.args:
            if args.args[0] in DEVICES:
                cfg["device"] = args.args[0]; _sanitize(cfg)
        cmd_build(cfg, clean=args.clean)

    elif sub == "flash":
        if args.args and args.args[0] in DEVICES:
            cfg["device"] = args.args[0]; _sanitize(cfg)
        cmd_flash(cfg, port=args.port or None)

    elif sub == "install":
        if args.args and args.args[0] in DEVICES:
            cfg["device"] = args.args[0]; _sanitize(cfg)
        cmd_build(cfg, clean=args.clean)
        cmd_flash(cfg, port=args.port or None)

    elif sub == "monitor":
        cmd_monitor(cfg, port=args.port or None)

    elif sub == "clean":
        device = args.args[0] if args.args and args.args[0] in DEVICES else cfg["device"]
        cmd_clean(device)

    elif sub == "bake":
        if args.args:
            targets = []
            for a in args.args:
                if a == "all":
                    targets = list(DEVICES.keys()); break
                elif a in RELEASE_SETS:
                    targets = RELEASE_SETS[a]; break
                elif a in DEVICES:
                    targets.append(a)
            if not targets:
                die(f"Unknown device or set: {args.args}")
            for t in targets:
                rcfg = load_cfg()
                rcfg["device"] = t; _sanitize(rcfg)
                cmd_build(rcfg, clean=args.clean)
                cmd_bake(rcfg)
        else:
            cmd_build(cfg, clean=args.clean)
            cmd_bake(cfg)

    elif sub == "release":
        if args.args:
            a = args.args[0]
            if a in RELEASE_SETS:
                targets = RELEASE_SETS[a]
            else:
                targets = [t for t in args.args if t in DEVICES]
                invalid = [t for t in args.args if t not in DEVICES]
                if invalid:
                    warn(f"Unknown targets ignored: {' '.join(invalid)}")
                if not targets:
                    die(f"No valid targets. Sets: {list(RELEASE_SETS.keys())}")
        else:
            # interactive set picker
            div()
            print(f"\n  {C_WHT}{C_BOLD}purrstrap release — pick a build set:{C_RST}\n")
            set_keys = list(RELEASE_SETS.keys())
            for i, k in enumerate(set_keys, 1):
                print(f"  [{i}]  {k:<10}  {C_GRY}{' '.join(RELEASE_SETS[k])}{C_RST}")
            print()
            raw = input("  Set [1]: ").strip() or "1"
            try:
                targets = RELEASE_SETS[set_keys[int(raw) - 1]]
            except (ValueError, IndexError):
                targets = RELEASE_SETS["all"]

        cmd_release(targets, clean=args.clean)

    else:
        p.print_help()


if __name__ == "__main__":
    main()
