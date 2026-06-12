#!/usr/bin/env bash
# PURR OS — Build & Module Installer
# Run from Builder/:  ./build.sh
#   No arguments → interactive wizard
#
# Direct build examples:
#   ./build.sh --target heltec --mini --flash COM5
#   ./build.sh --target cyd --clean
#   ./build.sh --setup           # re-run wizard, update saved config

set -euo pipefail

BUILDER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$BUILDER_DIR")"
COREOS_DIR="$REPO_DIR/CoreOS"
LORA_DIR="$REPO_DIR/LoRa Kernels"
CONFIG_FILE="$BUILDER_DIR/purr_build.cfg"

# ── Defaults ──────────────────────────────────────────────────────────────────
TARGET=""
MINI=0
CLEAN=0
SETUP=0
FLASH_PORT=""
MONITOR_PORT=""
LORA_KERNEL="sx1262"
UI_KERNEL="none"
MOD_BT=1
MOD_MTP=0
MOD_FLASHER=0
MOD_LORA=1       # adjusted by apply_target_defaults
MOD_MESH=0
TDECK_PLUS=0

# ── Terminal colour ───────────────────────────────────────────────────────────
if [[ -t 1 ]]; then
    BOLD=$'\033[1m' RESET=$'\033[0m' GREEN=$'\033[32m'
    YELLOW=$'\033[33m' RED=$'\033[31m' DIM=$'\033[2m' CYAN=$'\033[36m'
else
    BOLD="" RESET="" GREEN="" YELLOW="" RED="" DIM="" CYAN=""
fi
pinfo() { printf "%s[purr]%s %s\n" "$GREEN"  "$RESET" "$*"; }
pwarn() { printf "%s[warn]%s %s\n" "$YELLOW" "$RESET" "$*"; }
perr()  { printf "%s[err]%s  %s\n" "$RED"    "$RESET" "$*" >&2; exit 1; }
pdiv()  { printf "%s%s%s\n" "$DIM" "────────────────────────────────────────" "$RESET"; }

# ── Help ──────────────────────────────────────────────────────────────────────
show_help() {
    printf "%sPURR OS Build & Module Installer%s\n\n" "$BOLD" "$RESET"
    cat <<'EOF'
Usage:
  ./build.sh                              interactive wizard (no args)
  ./build.sh --target TARGET [options]    direct build — skips wizard
  ./build.sh --setup                      re-run wizard to update saved config

Targets:
  heltec   ESP32-S3  8MB  SSD1306 128×64  SX1262 LoRa
  cyd      ESP32     4MB  ILI9341 320×240  XPT2046 touch
  tdeck    ESP32-S3  16MB ST7789 320×240   trackball + BB kbd (WIP)

Build flags:
  --mini              Strip MicroPython (.meow apps require full build)
  --clean             Wipe build dir first (required after target switch)

Module flags (override wizard / saved config):
  --no-bt             Disable Bluetooth module
  --with-mtp          Enable MTP USB module
  --with-flasher      Enable OTA flasher module
  --no-lora           Disable LoRa module
  --with-mesh         Enable Meshtastic co-resident stack (requires LoRa)

LoRa:
  --lora-kernel K     Backend: sx1262 (default) | rak3172 | sx1276
                      Copies kernel files into CoreOS/system/kernel/modules/

Flash / monitor:
  --flash PORT        Flash after build  (COM5 or /dev/ttyUSB0)
  --monitor PORT      Open serial monitor after flash (Ctrl+] to exit)

Config:
  Choices are saved to purr_build.cfg and reused on the next run.
  Pass flags to override individual settings, or --setup to re-run the wizard.
EOF
}

# ── Target helpers ────────────────────────────────────────────────────────────
get_chip() {
    case "$TARGET" in
        heltec|tdeck) printf "esp32s3" ;;
        cyd)          printf "esp32" ;;
        *)            perr "Unknown target: $TARGET" ;;
    esac
}

apply_target_defaults() {
    case "$TARGET" in
        heltec|tdeck) MOD_LORA=1; UI_KERNEL="none"    ;;
        cyd)          MOD_LORA=0; UI_KERNEL="miniwin"  ;;
    esac
}

# ── Config ────────────────────────────────────────────────────────────────────
load_config() {
    [[ ! -f "$CONFIG_FILE" ]] && return
    local key val
    while IFS='=' read -r key val; do
        case "$key" in
            TARGET)      TARGET="$val"      ;;
            MINI)        MINI="$val"        ;;
            LORA_KERNEL) LORA_KERNEL="$val" ;;
            UI_KERNEL)   UI_KERNEL="$val"   ;;
            MOD_BT)      MOD_BT="$val"      ;;
            MOD_MTP)     MOD_MTP="$val"     ;;
            MOD_FLASHER) MOD_FLASHER="$val" ;;
            MOD_LORA)    MOD_LORA="$val"    ;;
            MOD_MESH)    MOD_MESH="$val"    ;;
            TDECK_PLUS)  TDECK_PLUS="$val"  ;;
        esac
    done < "$CONFIG_FILE"
    pinfo "Loaded purr_build.cfg  (--setup to change)"
}

save_config() {
    cat > "$CONFIG_FILE" <<EOF
TARGET=$TARGET
MINI=$MINI
LORA_KERNEL=$LORA_KERNEL
UI_KERNEL=$UI_KERNEL
MOD_BT=$MOD_BT
MOD_MTP=$MOD_MTP
MOD_FLASHER=$MOD_FLASHER
MOD_LORA=$MOD_LORA
MOD_MESH=$MOD_MESH
TDECK_PLUS=$TDECK_PLUS
EOF
    pinfo "Config saved → purr_build.cfg"
}

# ── Interactive: device picker ────────────────────────────────────────────────
pick_target() {
    printf "\n%s  Select target device:%s\n\n" "$BOLD" "$RESET"
    printf "  [1]  Heltec WiFi LoRa 32 V3   %sESP32-S3  8MB   SSD1306  SX1262 LoRa%s\n" "$DIM" "$RESET"
    printf "  [2]  CYD (ESP32-2432S028R)    %sESP32     4MB   ILI9341  XPT2046 touch%s\n" "$DIM" "$RESET"
    printf "  [3]  LilyGo T-Deck            %sESP32-S3  16MB  ST7789   trackball (WIP)%s\n" "$DIM" "$RESET"
    printf "\n  Choice [1]: "
    read -r choice
    case "${choice:-1}" in
        1) TARGET="heltec" ;;
        2) TARGET="cyd"    ;;
        3) TARGET="tdeck"  ;;
        *) pwarn "Invalid, using heltec"; TARGET="heltec" ;;
    esac
}

# ── Interactive: T-Deck variant picker ───────────────────────────────────────
pick_tdeck_variant() {
    printf "\n%s  T-Deck variant:%s\n\n" "$BOLD" "$RESET"
    printf "  [1]  Normal  %sOriginal T-Deck (no GPS, no battery management)%s\n" "$DIM" "$RESET"
    printf "  [2]  Plus    %sT-Deck Plus (u-blox MIA-M10Q GPS + larger battery)%s\n" "$DIM" "$RESET"
    printf "\n  Choice [1]: "
    read -r choice
    case "${choice:-1}" in
        2) TDECK_PLUS=1 ;;
        *) TDECK_PLUS=0 ;;
    esac
}

# ── Interactive: LoRa kernel picker ──────────────────────────────────────────
pick_lora_kernel() {
    printf "\n%s  LoRa kernel backend:%s\n\n" "$BOLD" "$RESET"
    printf "  [1]  SX1262   %sSPI — Heltec V3, T-Deck (default)%s\n"      "$DIM" "$RESET"
    printf "  [2]  RAK3172  %sUART AT — CattoBoardV1 PCB%s\n"              "$DIM" "$RESET"
    printf "  [3]  SX1276   %sSPI — generic RFM95W breakout%s\n"           "$DIM" "$RESET"
    printf "\n  Choice [1]: "
    read -r choice
    case "${choice:-1}" in
        1) LORA_KERNEL="sx1262"  ;;
        2) LORA_KERNEL="rak3172" ;;
        3) LORA_KERNEL="sx1276"  ;;
        *) pwarn "Invalid, using sx1262"; LORA_KERNEL="sx1262" ;;
    esac
}

# ── Interactive: module wizard ────────────────────────────────────────────────
module_wizard() {
    # Parallel arrays indexed 0-5
    local keys=(  "BT"         "MTP"         "FLASHER"           "LORA"           "MESH"                       "MICROPYTHON"  )
    local names=( "Bluetooth"  "MTP USB"     "OTA Flasher"       "LoRa Radio"     "Meshtastic"                 "MicroPython"  )
    local descs=(
        "bt_manager — BLE + Classic stack (~200KB flash)"
        "mtp_manager — USB file transfer"
        "flasher — OTA partition flasher"
        "lora_manager — LoRa radio driver"
        "mesh_manager — Meshtastic co-resident stack (requires LoRa)"
        "mpython_runtime — .meow app interpreter"
    )
    # 1 = hide this module on cyd
    local hide_on_cyd=( 0 0 0 1 1 0 )

    # Current state (must stay in sync with keys order)
    local state=( "$MOD_BT" "$MOD_MTP" "$MOD_FLASHER" "$MOD_LORA" "$MOD_MESH" "$((1 - MINI))" )

    while true; do
        pdiv
        printf "\n%s  Kernel modules — %s%s%s\n" "$BOLD" "$CYAN" "$TARGET" "$RESET"
        printf "\n  %sAlways compiled:%s  wifi_manager · power_manager\n" "$DIM" "$RESET"
        case "$TARGET" in
            heltec) printf "  %s             %s  display_ssd1306\n" "$DIM" "$RESET" ;;
            cyd)    printf "  %s             %s  display_ili9341 · touch_xpt2046 · partition_manager\n" "$DIM" "$RESET" ;;
            tdeck)  printf "  %s             %s  display_ssd1306\n" "$DIM" "$RESET" ;;
        esac
        printf "\n%s  Optional (enter number to toggle):%s\n\n" "$BOLD" "$RESET"

        local visible_count=0
        local visible_map=()

        for i in "${!keys[@]}"; do
            [[ "$TARGET" == "cyd" && "${hide_on_cyd[$i]}" == "1" ]] && continue
            ((visible_count++))
            visible_map+=("$i")
            local s="${state[$i]}"
            local mark; [[ "$s" == "1" ]] && mark="${GREEN}●${RESET}" || mark="${DIM}○${RESET}"
            local status; [[ "$s" == "1" ]] && status="${GREEN}ON${RESET} " || status="${DIM}OFF${RESET}"
            printf "  [%d]  %b  %-14s  %b  %s%s%s\n" \
                "$visible_count" "$mark" "${names[$i]}" "$status" "$DIM" "${descs[$i]}" "$RESET"
        done

        if [[ "$TARGET" != "cyd" && "${state[3]}" == "1" ]]; then
            printf "       %s└─ kernel: %s  ([k] to change)%s\n" "$DIM" "$LORA_KERNEL" "$RESET"
        fi

        printf "\n  Toggle [1-%d]" "$visible_count"
        [[ "$TARGET" != "cyd" ]] && printf ", [k] LoRa kernel"
        printf ", or Enter to build: "
        read -r choice

        [[ -z "$choice" ]] && break

        if [[ "$choice" == "k" && "$TARGET" != "cyd" ]]; then
            pick_lora_kernel; continue
        fi

        if [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= visible_count )); then
            local arr_idx="${visible_map[$((choice - 1))]}"
            state[$arr_idx]=$(( 1 - state[$arr_idx] ))
        else
            pwarn "Enter a number 1–$visible_count"
        fi
    done

    # Sync state back to individual vars
    MOD_BT="${state[0]}"
    MOD_MTP="${state[1]}"
    MOD_FLASHER="${state[2]}"
    MOD_LORA="${state[3]}"
    MOD_MESH="${state[4]}"
    MINI=$(( 1 - state[5] ))
}

# ── LoRa kernel installer ─────────────────────────────────────────────────────
install_lora_kernel() {
    local folder
    case "$LORA_KERNEL" in
        sx1262)  folder="SX1262"       ;;
        rak3172) folder="RAK3172"      ;;
        sx1276)  folder="SX1276_RFM95W" ;;
        *)       pwarn "Unknown LoRa kernel '$LORA_KERNEL', skipping"; return ;;
    esac

    local src="$LORA_DIR/$folder"
    local dst="$COREOS_DIR/system/kernel/modules"

    if [[ ! -d "$src" ]]; then
        pwarn "LoRa kernel dir not found: $src"
        return
    fi
    pinfo "LoRa kernel → $folder → modules/"
    cp -r "$src/." "$dst/"
}

# ── Environment check ─────────────────────────────────────────────────────────
check_env() {
    [[ -z "${IDF_PATH:-}" ]] && perr "IDF_PATH not set. Source ESP-IDF first:\n  . \$IDF_PATH/export.sh"
    [[ ! -d "$COREOS_DIR" ]] && perr "CoreOS directory not found: $COREOS_DIR"
    if [[ $MINI -eq 0 && ! -f "$COREOS_DIR/components/micropython/ports/embed/port/micropython_embed.h" ]]; then
        pwarn "MicroPython submodule missing — full build will fail."
        pwarn "Use --mini, or clone the submodule (see Builder/HOWTO.md §3)."
        echo ""
    fi
}

# ── Build summary banner ──────────────────────────────────────────────────────
print_banner() {
    local chip; chip="$(get_chip)"
    local display_target="$TARGET"
    [[ "$TARGET" == "tdeck" && $TDECK_PLUS -eq 1 ]] && display_target="tdeck-plus"
    pdiv
    printf "\n  %sPURR OS%s  %s%s (%s)%s\n" "$BOLD" "$RESET" "$CYAN" "$display_target" "$chip" "$RESET"
    printf "  Variant  : %s\n" "$( [[ $MINI -eq 1 ]] && echo 'mini — no MicroPython' || echo 'full — with MicroPython' )"
    printf "  Modules  :"
    [[ $MOD_BT      -eq 1 ]] && printf " bt"
    [[ $MOD_LORA    -eq 1 ]] && printf " lora(%s)" "$LORA_KERNEL"
    [[ $MOD_MESH    -eq 1 ]] && printf " mesh"
    [[ $MOD_MTP     -eq 1 ]] && printf " mtp"
    [[ $MOD_FLASHER -eq 1 ]] && printf " flasher"
    printf "\n"
    [[ -n "$FLASH_PORT"   ]] && printf "  Flash    : %s\n" "$FLASH_PORT"
    [[ -n "$MONITOR_PORT" ]] && printf "  Monitor  : %s\n" "$MONITOR_PORT"
    pdiv
    echo ""
}

# ── Main ──────────────────────────────────────────────────────────────────────

load_config

EXPLICIT_LORA=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --target)       TARGET="$2";    shift 2 ;;
        --mini)         MINI=1;         shift ;;
        --clean)        CLEAN=1;        shift ;;
        --setup)        SETUP=1;        shift ;;
        --flash)        FLASH_PORT="$2"; shift 2 ;;
        --monitor)      MONITOR_PORT="$2"; shift 2 ;;
        --lora-kernel)  LORA_KERNEL="$2"; shift 2 ;;
        --ui-kernel)    UI_KERNEL="$2";   shift 2 ;;
        --no-bt)        MOD_BT=0;       shift ;;
        --with-mtp)     MOD_MTP=1;      shift ;;
        --with-flasher) MOD_FLASHER=1;  shift ;;
        --no-lora)      MOD_LORA=0; EXPLICIT_LORA=1; shift ;;
        --with-mesh)    MOD_MESH=1; shift ;;
        --tdeck-plus)   TDECK_PLUS=1; shift ;;
        -h|--help)      show_help; exit 0 ;;
        *) perr "Unknown argument: $1. Try --help" ;;
    esac
done

if [[ -n "$TARGET" ]]; then
    case "$TARGET" in
        heltec|cyd|tdeck) ;;
        *) perr "Unknown target '$TARGET'. Valid: heltec, cyd, tdeck" ;;
    esac
fi

# Wizard: no target given, or --setup
if [[ -z "$TARGET" || $SETUP -eq 1 ]]; then
    [[ -z "$TARGET" ]] && pick_target
    [[ "$TARGET" == "tdeck" ]] && pick_tdeck_variant
    apply_target_defaults
    module_wizard
    printf "\n  Flash port (COM5 / /dev/ttyUSBx, blank to skip): "
    read -r fp; [[ -n "$fp" ]] && FLASH_PORT="$fp"
    printf "  Monitor port (blank to skip): "
    read -r mp; [[ -n "$mp" ]] && MONITOR_PORT="$mp"
    save_config
else
    [[ $EXPLICIT_LORA -eq 0 ]] && apply_target_defaults
fi

check_env
print_banner

[[ $MOD_LORA -eq 1 && "$TARGET" != "cyd" ]] && install_lora_kernel

DEFAULTS_SRC="$BUILDER_DIR/targets/$TARGET.defaults"
if [[ -f "$DEFAULTS_SRC" ]]; then
    pinfo "$TARGET.defaults → sdkconfig.defaults"
    cp "$DEFAULTS_SRC" "$COREOS_DIR/sdkconfig.defaults"
else
    pwarn "targets/$TARGET.defaults not found, keeping existing sdkconfig.defaults"
fi

cd "$COREOS_DIR"

# arduino-esp32 3.x targets IDF 5.1.x; bypass its version gate on IDF 6.x
export ARDUINO_SKIP_IDF_VERSION_CHECK=1

if [[ $CLEAN -eq 1 ]]; then
    pinfo "fullclean..."
    idf.py fullclean
    rm -f sdkconfig
fi

CHIP="$(get_chip)"
CMAKE_FLAGS=(
    -DTARGET_DEVICE="$TARGET"
    -DBUILD_MINI="$MINI"
    -DBUILD_TDECK_PLUS="$TDECK_PLUS"
    -DPURR_ENABLE_BT="$MOD_BT"
    -DPURR_ENABLE_MTP="$MOD_MTP"
    -DPURR_ENABLE_FLASHER="$MOD_FLASHER"
    -DPURR_ENABLE_LORA="$MOD_LORA"
    -DPURR_ENABLE_MESH="$MOD_MESH"
    -DPURR_UI_KERNEL="$UI_KERNEL"
)

pinfo "set-target $CHIP"
idf.py "${CMAKE_FLAGS[@]}" set-target "$CHIP"

pinfo "build  TARGET=$TARGET  BT=$MOD_BT  MTP=$MOD_MTP  FLASHER=$MOD_FLASHER  LORA=$MOD_LORA  MINI=$MINI"
idf.py "${CMAKE_FLAGS[@]}" build

if [[ -n "$FLASH_PORT" ]]; then
    pinfo "flashing → $FLASH_PORT"
    idf.py -p "$FLASH_PORT" flash
fi

if [[ -n "$MONITOR_PORT" ]]; then
    pinfo "monitor on $MONITOR_PORT  (Ctrl+] to exit)"
    idf.py -p "$MONITOR_PORT" monitor
fi

pinfo "done."
