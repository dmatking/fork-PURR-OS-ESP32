#!/usr/bin/env python3
"""PURR OS SDK v0.7.0 — interactive build / flash / monitor tool.
Invoked by SDK.ps1 (or directly: python sdk_core.py [flags]).
"""

import argparse
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


def _idf_py():
    """Return the idf.py invocation as a list.
    On Windows, bare 'idf.py' won't be found by subprocess — resolve via IDF_PATH."""
    idf_path = os.environ.get("IDF_PATH", "")
    if idf_path:
        candidate = os.path.join(idf_path, "tools", "idf.py")
        if os.path.exists(candidate):
            return [sys.executable, candidate]
    return ["idf.py"]


# ── Per-target build directories ──────────────────────────────────────────────
# Each target gets its own build dir and sdkconfig so builds never contaminate
# each other. Switching targets no longer requires a clean.

_BUILD_DIRS = {
    "heltec":       "build_heltec",
    "cyd":          "build_cyd",
    "cyd_boot":     "build_cyd_boot",
    "tdeck":        "build_tdeck",
    "jc3248w535":   "build_jc3248w535",
    "waveshare169": "build_waveshare169",
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
        "spec":         "ESP32-S3  8MB  SSD1306  SX1262 LoRa",
        "shells":       [],
        "default_lora": True,
        "fixed":        False,
    },
    "cyd_s028r": {
        "chip":         "esp32",
        "desc":         "CYD S028R (ESP32-2432S028R, original)",
        "spec":         "ESP32  4MB  ILI9341 2.4\"  XPT2046 SPI touch  LVGL + PURR WM — full OS (ota_0)",
        "shells":       ["blackberry", "explorer", "classicmac", "both", "none"],
        "default_lora": False,
        "fixed":        False,
    },
    "cyd_s024c": {
        "chip":         "esp32",
        "desc":         "CYD S024C (ESP32-2432S024C, newer)",
        "spec":         "ESP32  4MB  ILI9341 2.4\"  CST816S I2C touch  LVGL + PURR WM — full OS (ota_0)",
        "shells":       ["blackberry", "explorer", "classicmac", "both", "none"],
        "default_lora": False,
        "fixed":        False,
    },
    "cyd": {
        "chip":         "esp32",
        "desc":         "CYD (alias for S024C)",
        "spec":         "Use cyd_s028r or cyd_s024c instead",
        "shells":       ["blackberry", "explorer", "classicmac", "both", "none"],
        "default_lora": False,
        "fixed":        False,
    },
    "cyd_boot": {
        "chip":         "esp32",
        "desc":         "CYD PURR Kernel (factory, all variants)",
        "spec":         "ESP32  4MB  ILI9341 2.4\" — factory kernel (OTA-immune, chainloads ota_0)",
        "shells":       [],
        "default_lora": False,
        "fixed":        True,   # no module toggles; always mini
    },
    "tdeck": {
        "chip":         "esp32s3",
        "desc":         "LilyGo T-Deck",
        "spec":         "ESP32-S3  16MB  ST7789  trackball (WIP)",
        "shells":       ["blackberry", "smol"],
        "default_lora": True,
        "fixed":        False,
    },
    "jc3248w535": {
        "chip":         "esp32s3",
        "desc":         "JC3248W535 3.5\" (WIP)",
        "spec":         "ESP32-S3  16MB  8MB PSRAM  ST7796 480x320  GT911 cap touch — WIP, verify pins",
        "shells":       [],
        "default_lora": False,
        "fixed":        True,   # WIP: no module toggles until stable
    },
    "waveshare169": {
        "chip":         "esp32s3",
        "desc":         "Waveshare 1.69\" ESP32-S3 (WIP)",
        "spec":         "ESP32-S3  4MB  ST7789 240x280  CST816S cap touch — WIP, verify pins",
        "shells":       [],
        "default_lora": False,
        "fixed":        True,   # WIP: no module toggles until stable
    },
}

# ── Module definitions ────────────────────────────────────────────────────────
# inverted=True means cmake flag is BUILD_MINI and ON in cfg means the flag = 0
MODULES = [
    {
        "key":      "bt",
        "cmake":    "PURR_ENABLE_BT",
        "label":    "Bluetooth",
        "desc":     "bt_manager — BLE + Classic stack (~200 KB flash)",
        "targets":  ["heltec", "cyd", "tdeck"],
        "default":  True,
        "inverted": False,
    },
    {
        "key":      "mtp",
        "cmake":    "PURR_ENABLE_MTP",
        "label":    "MTP USB",
        "desc":     "mtp_manager — USB file transfer",
        "targets":  ["heltec", "cyd", "tdeck"],
        "default":  False,
        "inverted": False,
    },
    {
        "key":      "flasher",
        "cmake":    "PURR_ENABLE_FLASHER",
        "label":    "OTA Flasher",
        "desc":     "flasher — OTA partition flasher",
        "targets":  ["heltec", "cyd", "tdeck"],
        "default":  False,
        "inverted": False,
    },
    {
        "key":      "lora",
        "cmake":    "PURR_ENABLE_LORA",
        "label":    "LoRa Radio",
        "desc":     "lora_manager — LoRa radio driver",
        "targets":  ["heltec", "tdeck"],
        "default":  True,
        "inverted": False,
    },
    {
        "key":      "mesh",
        "cmake":    "PURR_ENABLE_MESH",
        "label":    "Meshtastic",
        "desc":     "mesh_manager — Meshtastic co-resident stack (requires LoRa)",
        "targets":  ["heltec", "tdeck"],
        "default":  False,
        "inverted": False,
    },
    {
        "key":      "micropython",
        "cmake":    "BUILD_MINI",
        "label":    "MicroPython",
        "desc":     "mpython_runtime — .meow app interpreter",
        "targets":  ["heltec", "cyd", "tdeck"],
        "default":  True,
        "inverted": True,   # micropython ON  → BUILD_MINI=0
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

SHELL_DESCS = {
    "both":       "BlackberryUI + Explorer — both registered, BB launches first (LVGL)",
    "blackberry": "BlackberryUI — BB-style status bar + app drawer (LVGL)",
    "explorer":   "Explorer — Windows CE / PDA taskbar + icon grid (LVGL)",
    "classicmac": "ClassicMac — Mac System 7/8 Platinum desktop + menu bar (LVGL)",
    "smol":       "Smol — minimal OLED shell",
    "none":       "Headless — no UI shell compiled",
}

# ── Config ────────────────────────────────────────────────────────────────────
DEFAULT_CFG = {
    "target":       "heltec",
    "shell":        "both",
    "lora_kernel":  "sx1262",
    "flash_port":   "",
    "flash_baud":   460800,
    "monitor_port": "",
    "tdeck_plus":   False,
    "cyd_variant":  "s028r",
    "modules": {
        "bt":          True,
        "mtp":         False,
        "flasher":     False,
        "lora":        True,
        "mesh":        False,
        "micropython": True,
    },
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
    # CYD has no LoRa hardware — strip flags regardless of what config says
    if target in ("cyd", "cyd_boot"):
        cfg["modules"]["lora"] = False
        cfg["modules"]["mesh"] = False
    # cyd_boot is fully fixed — wipe all optional module flags
    if TARGETS.get(target, {}).get("fixed"):
        for key in ("bt", "lora", "mesh", "mtp", "flasher", "micropython"):
            cfg["modules"][key] = False
    # mesh requires lora — strip if lora was just disabled
    if not cfg["modules"].get("lora"):
        cfg["modules"]["mesh"] = False


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


def pick_shell(cfg):
    shells = TARGETS[cfg["target"]]["shells"]
    if not shells:
        return
    header(f"UI shell — {cfg['target']}:")
    current = cfg.get("shell", shells[0])
    for i, s in enumerate(shells, 1):
        marker = f"{C_GRN} *{C_RST}" if s == current else "  "
        print(f"  [{i}]{marker} {s:<12}  {C_GRY}{SHELL_DESCS.get(s, '')}{C_RST}")
    print()
    raw = input("  Choice [1]: ").strip() or "1"
    try:
        idx = int(raw) - 1
        cfg["shell"] = shells[idx] if 0 <= idx < len(shells) else shells[0]
    except ValueError:
        cfg["shell"] = shells[0]


def pick_modules(cfg):
    target = cfg["target"]
    if TARGETS[target]["fixed"]:
        print(f"\n  {C_GRY}{target}: fixed module set — no toggles{C_RST}\n")
        return

    visible = [m for m in MODULES if target in m["targets"]]

    while True:
        div()
        print(f"\n  Kernel modules — {C_CYN}{target}{C_RST}\n")
        print(f"  {C_GRY}Always compiled: wifi_manager  power_manager{C_RST}")
        if target == "cyd":
            print(f"  {C_GRY}                  display_ili9341  touch_cst816s  partition_manager{C_RST}")
        else:
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

        print()
        prompt = f"  Toggle [1-{len(visible)}]"
        if has_lora:
            prompt += ", [k] LoRa kernel"
        raw = input(prompt + ", or Enter to continue: ").strip()

        if not raw:
            break
        if raw.lower() == "k" and has_lora:
            pick_lora_kernel(cfg)
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
    fp = input(f"  Flash port (COM5 / COM8, blank to skip) [{cfg.get('flash_port','')}]: ").strip()
    if fp:
        cfg["flash_port"] = fp
    mp = input(f"  Monitor port (blank to skip) [{cfg.get('monitor_port','')}]: ").strip()
    if mp:
        cfg["monitor_port"] = mp


def configure(cfg, full=True):
    if full:
        pick_target(cfg)
        if cfg["target"] == "tdeck":
            pick_tdeck_variant(cfg)
        elif cfg["target"] == "cyd":
            pick_cyd_variant(cfg)
    pick_shell(cfg)
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

    if t["shells"]:
        flags.append(f"-DPURR_SHELL={cfg.get('shell', 'both')}")

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
    if target in ("cyd", "cyd_boot"):
        mods = dict(mods)
        mods["lora"] = False
        mods["mesh"] = False

    _flag("PURR_ENABLE_BT",      "bt",      True)
    _flag("PURR_ENABLE_LORA",    "lora",    t["default_lora"])
    _flag("PURR_ENABLE_MESH",    "mesh",    False)
    _flag("PURR_ENABLE_MTP",     "mtp",     False)
    _flag("PURR_ENABLE_FLASHER", "flasher", False)

    flags.append(f"-DBUILD_TDECK_PLUS={1 if cfg.get('tdeck_plus') else 0}")

    # CYD display variant (s028r=original XPT2046, s024c=newer CST816S)
    if cmake_target == "cyd" and display_variant:
        flags.append(f"-DCYD_DISPLAY_VARIANT={display_variant}")

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

def _build_spiffs(cfg):
    target   = cfg["target"]
    idf_path = os.environ.get("IDF_PATH", "")
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

    device_target = "cyd" if target == "cyd_boot" else target
    device_src = os.path.join(COREOS_DIR, "system", "kernel", "devices", f"{device_target}.json")
    if not os.path.exists(device_src):
        device_src = os.path.join(COREOS_DIR, "system", "kernel", "device.json")
    if not os.path.exists(device_src):
        warn(f"No device config for '{device_target}' — skipping SPIFFS image")
        return

    shutil.copy2(device_src, os.path.join(staging_dir, "system", "kernel", "device.json"))
    info(f"SPIFFS: {device_target}.json → /system/kernel/device.json")

    # cyd/cyd_boot: 0x70000 = 458752 (448 KB) at 0x390000
    # others:       0x50000 = 327680 (320 KB) at 0x3b0000
    spiffs_size = 458752 if target in ("cyd", "cyd_boot") else 327680
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


def do_flash(cfg, port=None):
    target = cfg["target"]
    chip   = TARGETS[target]["chip"]
    port   = port or cfg.get("flash_port", "")
    baud   = cfg.get("flash_baud", 460800)

    if not port:
        port = input("  Flash port (e.g. COM8): ").strip()
        if not port:
            err("No flash port specified.")
        cfg["flash_port"] = port
        save_cfg(cfg)

    build_dir  = _build_dir(cfg)
    spiffs_img = os.path.join(build_dir, "spiffs.bin")

    # cyd (ota_0) → 0x110000; all others (factory) → 0x10000
    app_offset    = "0x110000" if target == "cyd" else "0x10000"
    spiffs_offset = "0x390000" if target in ("cyd", "cyd_boot") else "0x3b0000"

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
    if not port:
        port = input("  Monitor port (e.g. COM8): ").strip()
        if not port:
            err("No monitor port specified.")
        cfg["monitor_port"] = port
        save_cfg(cfg)
    info(f"monitor on {port}  (Ctrl+] to exit)")
    bdn = os.path.basename(_build_dir(cfg))
    run_live(_idf_py() + ["-B", bdn, "-p", port, "monitor"], cwd=COREOS_DIR)


# ── Full build + full flash (CYD: factory + ota_0 together) ──────────────────

def do_full_build(cfg, clean=False):
    """Build cyd_boot (factory) then cyd (OS) back-to-back."""
    if cfg["target"] not in ("cyd", "cyd_boot"):
        warn("Full Build is only meaningful for CYD targets — running single build instead")
        do_build(cfg, clean=clean)
        return

    boot_cfg = dict(cfg)
    boot_cfg["target"]  = "cyd_boot"
    boot_cfg["modules"] = dict(DEFAULT_CFG["modules"])

    os_cfg = dict(cfg)
    os_cfg["target"] = "cyd"

    print(f"\n{C_YLW}{'━'*44}{C_RST}")
    info("Full Build step 1/2 — PURR Kernel (factory, OTA-immune)")
    print(f"{C_YLW}{'━'*44}{C_RST}\n")
    do_build(boot_cfg, clean=clean)

    print(f"\n{C_YLW}{'━'*44}{C_RST}")
    info("Full Build step 2/2 — PURR userland (ota_0)")
    print(f"{C_YLW}{'━'*44}{C_RST}\n")
    do_build(os_cfg, clean=clean)

    info("Full Build complete — PURR Kernel + userland ready to flash")


def do_full_flash(cfg, port=None):
    """Flash factory + ota_0 + SPIFFS in one esptool call."""
    port = port or cfg.get("flash_port", "")
    baud = cfg.get("flash_baud", 460800)

    if not port:
        port = input("  Flash port (e.g. COM8): ").strip()
        if not port:
            err("No flash port specified.")
        cfg["flash_port"] = port
        save_cfg(cfg)

    boot_dir   = os.path.join(COREOS_DIR, _BUILD_DIRS["cyd_boot"])
    os_dir     = os.path.join(COREOS_DIR, _BUILD_DIRS["cyd"])
    spiffs_img = os.path.join(os_dir, "spiffs.bin")

    missing = []
    for path, label in [
        (os.path.join(boot_dir, "bootloader", "bootloader.bin"),         "cyd_boot/bootloader.bin"),
        (os.path.join(boot_dir, "partition_table", "partition-table.bin"),"cyd_boot/partition-table.bin"),
        (os.path.join(boot_dir, "ota_data_initial.bin"),                  "cyd_boot/ota_data_initial.bin"),
        (os.path.join(boot_dir, "purr_os_core.bin"),                      "cyd_boot/purr_os_core.bin"),
        (os.path.join(os_dir,   "purr_os_core.bin"),                      "cyd/purr_os_core.bin"),
    ]:
        if not os.path.exists(path):
            missing.append(label)
    if missing:
        err("Missing build artifacts — run Full Build first:\n  " + "\n  ".join(missing))

    cmd = [
        sys.executable, "-m", "esptool",
        "--chip",   "esp32",
        "--port",   port,
        "-b",       str(baud),
        "--before", "default_reset",
        "--after",  "hard_reset",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_size", "detect",
        "--flash_freq", "40m",
        "0x1000",   os.path.join(boot_dir, "bootloader", "bootloader.bin"),
        "0x8000",   os.path.join(boot_dir, "partition_table", "partition-table.bin"),
        "0xe000",   os.path.join(boot_dir, "ota_data_initial.bin"),
        "0x10000",  os.path.join(boot_dir, "purr_os_core.bin"),   # factory
        "0x110000", os.path.join(os_dir,   "purr_os_core.bin"),   # ota_0
    ]
    if os.path.exists(spiffs_img):
        cmd += ["0x390000", spiffs_img]
        info("including SPIFFS at 0x390000")

    info(f"full flash → {port}  (factory 0x10000 + ota_0 0x110000 + SPIFFS 0x390000)")
    rc = run_live(cmd)
    if rc != 0:
        err(f"Full flash failed (exit {rc})")


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
    print(f"\n  {C_WHT}{C_BOLD}PURR OS v0.7.0{C_RST}  {C_GRY}KITT v0.4.0{C_RST}  {C_CYN}{display} ({t['chip']}){C_RST}{wip_tag}")

    mini = (not mods.get("micropython", True)) or t["fixed"]
    print(f"  Variant  : {'mini — no MicroPython' if mini else 'full — with MicroPython'}")

    if t["shells"] and cfg.get("shell"):
        print(f"  Shell    : {cfg['shell']}")

    mod_strs = []
    if mods.get("bt"):      mod_strs.append("bt")
    if mods.get("lora"):    mod_strs.append(f"lora({cfg.get('lora_kernel','sx1262')})")
    if mods.get("mesh"):    mod_strs.append("mesh")
    if mods.get("mtp"):     mod_strs.append("mtp")
    if mods.get("flasher"): mod_strs.append("flasher")
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
        elif choice == "~":
            sim_menu()
        else:
            warn(f"Unknown option '{choice}'")


# ── Simulator ────────────────────────────────────────────────────────────────

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
    if args.shell:
        cfg["shell"] = args.shell
    if args.mini:
        cfg["modules"]["micropython"] = False
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
    if args.lora_kernel:
        cfg["lora_kernel"] = args.lora_kernel
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
                   metavar="heltec|cyd_s028r|cyd_s024c|cyd|cyd_boot|tdeck|jc3248w535|waveshare169")
    p.add_argument("--shell",       choices=["both", "blackberry", "explorer", "smol", "none"])
    p.add_argument("--build",       action="store_true")
    p.add_argument("--flash",       metavar="PORT", default="")
    p.add_argument("--monitor",     metavar="PORT", default="")
    p.add_argument("--clean",       action="store_true")
    p.add_argument("--mini",        action="store_true")
    p.add_argument("--no-bt",       action="store_true", dest="no_bt")
    p.add_argument("--lora",        action="store_true")
    p.add_argument("--no-lora",     action="store_true", dest="no_lora")
    p.add_argument("--mesh",        action="store_true")
    p.add_argument("--mtp",         action="store_true")
    p.add_argument("--flasher",     action="store_true")
    p.add_argument("--lora-kernel", dest="lora_kernel", choices=list(LORA_KERNELS.keys()))
    p.add_argument("--tdeck-plus",  action="store_true", dest="tdeck_plus")
    p.add_argument("--cyd-variant", dest="cyd_variant", choices=["s028r", "s024c"],
                   help="CYD display variant: s028r=original/XPT2046, s024c=newer/CST816S")
    p.add_argument("--configure",   action="store_true")
    p.add_argument("--baud",        type=int, default=0)
    p.add_argument("--full-build",  action="store_true", dest="full_build",
                   help="Build cyd_boot (factory) + cyd (OS) back-to-back")
    p.add_argument("--full-flash",  metavar="PORT", default="", dest="full_flash",
                   help="Flash factory + ota_0 + SPIFFS in one esptool call")
    args = p.parse_args()

    cfg = load_cfg()

    direct = (args.build or bool(args.flash) or bool(args.monitor)
              or args.configure or args.full_build or bool(args.full_flash))
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
        if args.full_flash:
            do_full_flash(cfg, port=args.full_flash)
        elif args.flash:
            do_flash(cfg, port=args.flash)
        if args.monitor:
            do_monitor(cfg, port=args.monitor)
        return

    # Interactive — apply any pre-set target/shell before entering menu
    if args.target:
        cfg["target"] = args.target
    if args.shell:
        cfg["shell"] = args.shell
    main_menu(cfg)


if __name__ == "__main__":
    main()
