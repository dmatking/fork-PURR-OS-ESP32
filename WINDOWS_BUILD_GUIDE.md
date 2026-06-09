# PURR OS — Windows Build Guide

This guide takes you from a fresh Windows machine to a flashed PURR OS device.
Estimated time: **~20 minutes** (most of it is downloading ESP-IDF).

---

## What you need

- A Windows PC (Windows 10 or 11)
- A PURR OS compatible board — CYD (ESP32-2432S028R / S024C), JC3248W535, T-Deck Plus, or Waveshare 1.69"
- A USB cable that does **data** (not charge-only)
- Git installed — [git-scm.com](https://git-scm.com/download/win)

---

## Step 1 — Install ESP-IDF 5.3.5

ESP-IDF is Espressif's toolchain. PURR OS requires **version 5.3.5 exactly**.

1. Go to the [ESP-IDF releases page](https://github.com/espressif/esp-idf/releases/tag/v5.3.5)
2. Under **Assets**, download `esp-idf-tools-setup-offline-5.3.5.exe` (the offline installer — ~1.5 GB)
3. Run the installer. Accept all defaults. When asked where to install, the default `C:\Espressif` is fine.
4. When the installer finishes, it will create a shortcut called **"ESP-IDF 5.3.5 CMD"** in your Start menu. You do **not** need to use that shortcut — the SDK handles it automatically.

> **Only install one ESP-IDF version at a time.** If you have an older version (5.1, 5.2, etc.) already installed, the SDK will try to use it. Uninstall it first or use `C:\esp\v5.3.5\` as the install path so versions don't conflict.

---

## Step 2 — Get the PURR OS source code

Open **PowerShell** (press `Win + X` → Windows PowerShell) and run:

```powershell
cd C:\
git clone https://github.com/CattoFace/PURR-OS-ESP32.git
cd PURR-OS-ESP32
```

> If you already have the repo, just `cd` into it and run `git pull` to get the latest changes.

---

## Step 3 — Allow PowerShell scripts to run

Windows blocks unsigned scripts by default. Run this once:

```powershell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
```

Type `Y` and press Enter when prompted.

---

## Step 4 — Configure the SDK

Run the interactive setup wizard:

```powershell
.\SDK\SDK.ps1
```

The wizard will:
1. Ask you to pick a **target device** — choose the one matching your board
2. Let you toggle **optional modules** (Lua scripting, Bluetooth, shell, etc.) — the defaults are fine for a first build
3. Ask for your **COM port** — plug in your board first, then check Device Manager if you're not sure which port it is

> **Finding your COM port:** Plug in the board, open Device Manager (`Win + X` → Device Manager), expand **Ports (COM & LPT)**. Your board will show as something like `Silicon Labs CP210x USB to UART Bridge (COM8)`. Note the `COMx` number.

After the wizard finishes it saves your choices to `SDK/purr_sdk.cfg`. You won't need to run configure again unless you change targets.

---

## Step 5 — Build and flash

Once configured, build and flash in one command:

```powershell
.\SDK\SDK.ps1 -Build -Flash COM8
```

Replace `COM8` with your actual COM port number.

This will:
- Set up the ESP-IDF environment automatically
- Compile the firmware (~2–5 minutes on first build, faster after)
- Flash the binary to your board
- Print `[purr] Flash done` when finished

> **First-time flash on a brand-new board:** Use `-FullFlash` instead of `-Flash` to also write the partition table and bootloader:
> ```powershell
> .\SDK\SDK.ps1 -Build -FullFlash COM8
> ```

---

## Step 6 — Check it's working

After flashing, open the serial monitor to see the boot log:

```powershell
.\SDK\SDK.ps1 -Monitor COM8
```

You should see KITT boot messages like:

```
[KITT] PURR OS v0.9.5 / KITT v0.6.0
[KITT] Step 1: NVS init... OK
[KITT] Step 2: SPIFFS mount... OK
...
[KITT] Boot complete.
```

Press `Ctrl + ]` to exit the monitor.

Your board should now be showing the PURR OS UI on screen.

---

## Common commands

| What you want to do | Command |
|---|---|
| Build only | `.\SDK\SDK.ps1 -Build` |
| Build + flash | `.\SDK\SDK.ps1 -Build -Flash COM8` |
| Build + flash + monitor | `.\SDK\SDK.ps1 -Build -Flash COM8 -Monitor COM8` |
| Flash again without rebuilding | `.\SDK\SDK.ps1 -Flash COM8` |
| Open serial monitor | `.\SDK\SDK.ps1 -Monitor COM8` |
| Change target / modules | `.\SDK\SDK.ps1 -Configure` |
| Clean build (fixes weird errors) | `.\SDK\SDK.ps1 -Build -Clean` |
| First-time full flash | `.\SDK\SDK.ps1 -Build -FullFlash COM8` |

---

## Troubleshooting

**"running scripts is disabled on this system"**
Run `Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned` (Step 3 above).

**"ESP-IDF not found"**
The SDK couldn't find your IDF install. Run the ESP-IDF CMD shortcut from Start first, then try again — or set the path manually:
```powershell
$env:IDF_EXPORT_PATH = "C:\Espressif\frameworks\esp-idf-v5.3.5\export.ps1"
.\SDK\SDK.ps1 -Build -Flash COM8
```

**Board not showing up in Device Manager**
- Try a different USB cable (charge-only cables won't work)
- Install the CH340 or CP2102 driver — most CYD boards use one of these. Search "CH340 driver Windows" or "CP210x driver Windows" and download from the manufacturer.

**"idf.py build exited 1" / compile errors**
Run a clean build — stale cmake cache is the most common cause:
```powershell
.\SDK\SDK.ps1 -Build -Clean -Flash COM8
```

**Flash fails with "Failed to connect to ESP32"**
- Hold the **BOOT** button on the board while the flash starts, then release it
- Try a lower baud rate: `.\SDK\SDK.ps1 -Build -Flash COM8 -Baud 115200`

**Screen stays blank after flashing**
Open the monitor (`.\SDK\SDK.ps1 -Monitor COM8`) and check the boot log for any `[KITT] ERR:` lines. The first error line tells you exactly what failed.

---

## Putting Lua apps on the SD card

PURR OS can run Lua scripts from an SD card. Format the card as **FAT32** and create this folder structure:

```
SD card root\
  apps\
    hello.paws       ← normal app (sandboxed)
    admin.claw       ← admin app (full system access)
```

Scripts appear automatically in the app drawer. `.paws` files get the `win.*` and `sd.*` APIs. `.claw` files additionally get `kitt.*` (reboot, RAM info, WiFi status, etc.).

---

## Supported devices

| Board | Target name | Display | Touch |
|---|---|---|---|
| ESP32-2432S028R (CYD original) | `cyd_s028r` | ILI9341 2.4" 320×240 | XPT2046 SPI |
| ESP32-2432S024C (CYD newer) | `cyd_s024c` | ILI9341 2.4" 320×240 | CST816S I2C |
| JC3248W535 3.5" | `jc3248w535` | ST7796 480×320 | GT911 I2C |
| LilyGo T-Deck Plus | `tdeck_plus` | ST7789 320×240 | GT911 I2C |
| Waveshare 1.69" ESP32-S3 | `waveshare169` | ST7789 240×280 | CST816S I2C |

Pass the target name with `-Target`:
```powershell
.\SDK\SDK.ps1 -Target jc3248w535 -Build -Flash COM8
```
