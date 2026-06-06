# Building PURR OS on Linux

PURR OS can be built on Linux (and macOS) without any source code changes. The build system uses the cross-platform ESP-IDF toolchain.

## Prerequisites

### 1. Install ESP-IDF 5.3.5

```bash
mkdir -p ~/esp
cd ~/esp
git clone --branch v5.3.5 https://github.com/espressif/esp-idf.git idf
cd idf
./install.sh esp32
```

Or install to system location:
```bash
sudo git clone --branch v5.3.5 https://github.com/espressif/esp-idf.git /opt/esp-idf
cd /opt/esp-idf
sudo ./install.sh esp32
```

### 2. Set IDF_PATH (optional if installed to default location)

```bash
export IDF_PATH=~/esp/idf
# Or add to ~/.bashrc or ~/.zshrc for persistence
```

### 3. Install esptool (for flashing)

```bash
pip3 install esptool
```

## Building

### Using the SDK Script

```bash
cd PURR-OS-ESP32-MicroPython
chmod +x Builder/sdk.sh

# Interactive mode
./Builder/sdk.sh

# Command-line mode
./Builder/sdk.sh --target cyd_s028r --build
./Builder/sdk.sh --target cyd_s024c --build
./Builder/sdk.sh --target cyd_s028r --full-build --clean
```

### Direct with idf.py

```bash
cd CoreOS
$IDF_PATH/tools/idf.py -DTARGET_DEVICE=cyd_s028r set-target esp32
$IDF_PATH/tools/idf.py -DTARGET_DEVICE=cyd_s028r build
```

## Flashing

### Using the SDK

```bash
./Builder/sdk.sh --target cyd_s028r --flash /dev/ttyUSB0
./Builder/sdk.sh --target cyd_s028r --full-flash /dev/ttyUSB0  # factory + OS
```

### Using esptool directly

```bash
# Find your serial port (usually /dev/ttyUSB0 or /dev/ttyACM0)
ls /dev/tty*

# Flash bootloader
esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash \
  0x1000 CoreOS/build_cyd/bootloader/bootloader.bin

# Flash main OS
esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash \
  0x10000 CoreOS/build_cyd/purr_os_core.bin
```

## Monitoring Serial Output

```bash
./Builder/sdk.sh --target cyd_s028r --monitor /dev/ttyUSB0

# Or manually with picocom or screen
picocom /dev/ttyUSB0 -b 115200
screen /dev/ttyUSB0 115200
```

## Troubleshooting

### ESP-IDF not found
```bash
# Check IDF_PATH
echo $IDF_PATH

# Or install to default location (~/esp/idf)
export IDF_PATH=~/esp/idf
```

### Python not found
```bash
# Ensure Python 3 is installed
python3 --version

# Or create a symlink
sudo ln -s /usr/bin/python3 /usr/bin/python
```

### Serial port permission denied
```bash
# Add user to dialout group (Linux)
sudo usermod -a -G dialout $USER
# Log out and back in, or:
newgrp dialout
```

### Build fails with CMake issues
```bash
# Clean build directory
rm -rf CoreOS/build_cyd
./Builder/sdk.sh --target cyd_s028r --build
```

## CYD Display Variants

- **cyd_s028r** — Original ESP32-2432S028R (v0.4.0/v0.5.0) with XPT2046 SPI touch
- **cyd_s024c** — Newer ESP32-2432S024C with CST816S I2C touch

Use whichever matches your hardware.

## Notes

- All source code is identical — no modifications for Linux
- Build system is pure ESP-IDF + CMake (cross-platform)
- Paths are automatically detected and converted
- Both command-line and interactive workflows supported
