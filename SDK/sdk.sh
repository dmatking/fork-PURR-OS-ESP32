#!/bin/bash
# PURR OS SDK — Bash wrapper for Linux/Mac
# Sources ESP-IDF environment, then delegates all logic to sdk_core.py.
#
# Usage (interactive):
#   ./sdk.sh
#
# Usage (direct / CI):
#   ./sdk.sh --target cyd_s028r --build                         # original 2432S028R with XPT2046 SPI touch
#   ./sdk.sh --target cyd_s024c --build                         # newer 2432S024C with CST816S I2C touch
#   ./sdk.sh --target cyd_s028r --full-build --clean            # full build (S028R variant)
#   ./sdk.sh --target cyd_boot --build
#   ./sdk.sh --target heltec --build --mini --no-lora
#
# All switches are forwarded to sdk_core.py as CLI args.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_SDK_PY="$SCRIPT_DIR/sdk_core.py"

if [[ ! -f "$CORE_SDK_PY" ]]; then
    echo "[sdk] ERROR: sdk_core.py not found at: $CORE_SDK_PY" >&2
    exit 1
fi

# ── Detect ESP-IDF environment ─────────────────────────────────────────────────

detect_idf_path() {
    # Priority:
    # 1. IDF_PATH environment variable
    # 2. ~/esp/idf (common Linux install)
    # 3. ~/.esp-idf (alternative)
    # 4. /opt/esp-idf (system install)

    if [[ -n "$IDF_PATH" ]] && [[ -f "$IDF_PATH/tools/idf.py" ]]; then
        echo "$IDF_PATH"
        return 0
    fi

    for path in \
        "$HOME/esp/idf" \
        "$HOME/.esp-idf" \
        "/opt/esp-idf" \
        "/usr/local/esp-idf"; do
        if [[ -f "$path/tools/idf.py" ]]; then
            echo "$path"
            return 0
        fi
    done

    return 1
}

detect_python() {
    # Try python3 first, then python
    if command -v python3 &>/dev/null; then
        echo "python3"
    elif command -v python &>/dev/null; then
        echo "python"
    else
        return 1
    fi
}

IDF_PATH=$(detect_idf_path)
if [[ -z "$IDF_PATH" ]]; then
    echo "[sdk] ERROR: ESP-IDF not found. Set IDF_PATH or install ESP-IDF to ~/esp/idf" >&2
    exit 1
fi

export IDF_PATH

# Source IDF environment — activates the IDF virtualenv and updates PATH.
# Suppressed because export.sh is chatty; errors are non-fatal (SDK may still work).
if [[ -f "$IDF_PATH/export.sh" ]]; then
    . "$IDF_PATH/export.sh" > /dev/null 2>&1 || true
fi

PYTHON=$(detect_python)
if [[ -z "$PYTHON" ]]; then
    echo "[sdk] ERROR: Python 3 not found" >&2
    exit 1
fi

# ── Convert PowerShell-style args to CLI args for Python ──────────────────────

declare -a args

while [[ $# -gt 0 ]]; do
    case "$1" in
        # Target & shell
        -Target|--target)
            args+=("--target" "$2")
            shift 2
            ;;
        -Shell|--shell)
            args+=("--shell" "$2")
            shift 2
            ;;
        -CydVariant|--cyd-variant)
            args+=("--cyd-variant" "$2")
            shift 2
            ;;
        # Actions
        -Build|--build)
            args+=("--build")
            shift
            ;;
        -FullBuild|--full-build)
            args+=("--full-build")
            shift
            ;;
        -Flash|--flash)
            args+=("--flash" "$2")
            shift 2
            ;;
        -FullFlash|--full-flash)
            args+=("--full-flash" "$2")
            shift 2
            ;;
        -Monitor|--monitor)
            args+=("--monitor" "$2")
            shift 2
            ;;
        -Configure|--configure)
            args+=("--configure")
            shift
            ;;
        # Build options
        -Clean|--clean)
            args+=("--clean")
            shift
            ;;
        -Mini|--mini)
            args+=("--mini")
            shift
            ;;
        # Kernel modules
        -NoBt|--no-bt)
            args+=("--no-bt")
            shift
            ;;
        -Lora|--lora)
            args+=("--lora")
            shift
            ;;
        -NoLora|--no-lora)
            args+=("--no-lora")
            shift
            ;;
        -Mesh|--mesh)
            args+=("--mesh")
            shift
            ;;
        -Mtp|--mtp)
            args+=("--mtp")
            shift
            ;;
        -Flasher|--flasher)
            args+=("--flasher")
            shift
            ;;
        # LoRa / hardware
        -LoraKernel|--lora-kernel)
            args+=("--lora-kernel" "$2")
            shift 2
            ;;
        -TdeckPlus|--tdeck-plus)
            args+=("--tdeck-plus")
            shift
            ;;
        # Flash options
        -Baud|--baud)
            args+=("--baud" "$2")
            shift 2
            ;;
        -h|--help)
            echo "PURR OS SDK — Build / Flash / Monitor Tool"
            echo ""
            echo "Usage:"
            echo "  ./sdk.sh [OPTIONS]"
            echo ""
            echo "Targets:"
            echo "  --target heltec|cyd_s028r|cyd_s024c|cyd|cyd_boot|tdeck|jc3248w535|waveshare169"
            echo ""
            echo "Build actions:"
            echo "  --build                Build target"
            echo "  --full-build           Build cyd_boot + cyd (CYD only)"
            echo "  --clean                Clean build dir before building"
            echo ""
            echo "Flash/Monitor:"
            echo "  --flash PORT           Flash to serial port"
            echo "  --full-flash PORT      Flash factory + ota_0 (CYD only)"
            echo "  --monitor PORT         Open serial monitor"
            echo ""
            echo "Build options:"
            echo "  --mini                 Strip MicroPython runtime"
            echo "  --no-bt                Disable Bluetooth"
            echo "  --lora                 Enable LoRa"
            echo "  --no-lora              Disable LoRa"
            echo "  --mesh                 Enable Meshtastic mesh"
            echo "  --mtp                  Enable MTP USB"
            echo "  --flasher              Enable OTA flasher"
            echo ""
            echo "Hardware options:"
            echo "  --lora-kernel KERNEL   LoRa kernel (sx1262|rak3172|sx1276)"
            echo "  --tdeck-plus           T-Deck Plus (with GPS)"
            echo "  --cyd-variant VARIANT  CYD display variant (s028r|s024c)"
            echo ""
            exit 0
            ;;
        *)
            echo "[sdk] ERROR: Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# ── Run sdk_core.py ────────────────────────────────────────────────────────────

exec "$PYTHON" "$CORE_SDK_PY" "${args[@]}"
