# PURR OS Quick Start

## Windows

```powershell
# From project root
.\Builder\SDK.ps1 -Target cyd_s028r -Build
.\Builder\SDK.ps1 -Target cyd_s028r -Flash COM8
```

## Linux / macOS

### Initial Setup (one-time)

```bash
# Make setup script executable
chmod +x setup_linux.sh

# Run setup (installs ESP-IDF 5.3.5, dependencies, configures environment)
./setup_linux.sh
```

The script will:
- ✅ Detect your OS (Linux/macOS)
- ✅ Install system dependencies (git, cmake, ninja, Python packages)
- ✅ Clone ESP-IDF 5.3.5
- ✅ Install ESP toolchain
- ✅ Configure environment variables
- ✅ Make sdk.sh executable
- ✅ Optionally run a test build

### Build & Flash

```bash
# Make sure to source shell config if you just ran setup
source ~/.bashrc  # or ~/.zshrc on macOS

# Build
./Builder/sdk.sh --target cyd_s028r --build
./Builder/sdk.sh --target cyd_s024c --build

# Flash
./Builder/sdk.sh --target cyd_s028r --flash /dev/ttyUSB0

# Full setup (bootloader + OS)
./Builder/sdk.sh --target cyd_s028r --full-build --clean
./Builder/sdk.sh --target cyd_s028r --full-flash /dev/ttyUSB0

# Interactive mode
./Builder/sdk.sh
```

## Your Hardware

- **Original board** → Use `cyd_s028r` (2432S028R with XPT2046 touch)
- **Newer board** → Use `cyd_s024c` (2432S024C with CST816S touch)

Not sure? The original worked in v0.4.0/v0.5.0, so start with `cyd_s028r`.

## Common Ports

| OS | Port Example |
|---|---|
| Linux | `/dev/ttyUSB0` or `/dev/ttyACM0` |
| macOS | `/dev/cu.usbserial-*` |
| Windows | `COM8` |

Find your port: `ls /dev/tty*` (Linux/macOS) or check Device Manager (Windows)

## Troubleshooting

### "Permission denied" on Linux
```bash
chmod +x setup_linux.sh ./Builder/sdk.sh
```

### "ESP-IDF not found"
```bash
# Verify installation
echo $IDF_PATH
ls ~/esp/idf/tools/idf.py

# Or reinstall
./setup_linux.sh
```

### "Serial port in use"
```bash
# Linux: add to dialout group
sudo usermod -a -G dialout $USER
# Log out and back in
```

### Build errors after OS update
```bash
# Clean and rebuild
./Builder/sdk.sh --target cyd_s028r --clean --build
```

## Full Documentation

- **Windows**: See `README.md`
- **Linux/macOS**: See `LINUX_BUILD.md`
- **SDK usage**: Run `./Builder/sdk.sh --help`

## What Changed?

✅ **Zero source code changes** — same codebase compiles on Windows, Linux, macOS
✅ **Same ESP-IDF** — v5.3.5 cross-platform  
✅ **Same build system** — CMake + Ninja  
✅ **New SDK wrapper** — `sdk.sh` (bash) alongside `sdk.ps1` (PowerShell)

Everything else is identical between platforms.
