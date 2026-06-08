#!/bin/bash
# PURR OS Linux/macOS Setup Script
# Installs ESP-IDF 5.3.5, dependencies, and configures environment

set -e

# ─────────────────────────────────────────────────────────────────────────────
# Colors for output
# ─────────────────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ─────────────────────────────────────────────────────────────────────────────
# Helper functions
# ─────────────────────────────────────────────────────────────────────────────

header() {
    echo -e "\n${BLUE}════════════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}════════════════════════════════════════════════════════════${NC}\n"
}

info() {
    echo -e "${GREEN}[✓]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[!]${NC} $1"
}

error() {
    echo -e "${RED}[✗]${NC} $1" >&2
    exit 1
}

command_exists() {
    command -v "$1" &>/dev/null
}

# ─────────────────────────────────────────────────────────────────────────────
# Detect OS
# ─────────────────────────────────────────────────────────────────────────────

header "Detecting OS"

OS_TYPE=$(uname -s)
case "$OS_TYPE" in
    Linux*)
        OS="Linux"
        info "Detected: Linux"
        ;;
    Darwin*)
        OS="macOS"
        info "Detected: macOS"
        ;;
    *)
        error "Unsupported OS: $OS_TYPE (Linux and macOS only)"
        ;;
esac

# ─────────────────────────────────────────────────────────────────────────────
# Check prerequisites
# ─────────────────────────────────────────────────────────────────────────────

header "Checking prerequisites"

if ! command_exists python3; then
    error "Python 3 is required. Install with: apt-get install python3 (Linux) or brew install python3 (macOS)"
fi
info "Python 3: $(python3 --version)"

if ! command_exists git; then
    error "Git is required. Install with: apt-get install git (Linux) or brew install git (macOS)"
fi
info "Git: $(git --version | cut -d' ' -f3)"

# ─────────────────────────────────────────────────────────────────────────────
# Install system dependencies
# ─────────────────────────────────────────────────────────────────────────────

header "Installing system dependencies"

if [[ "$OS" == "Linux" ]]; then
    # Detect package manager
    if command_exists apt-get; then
        info "Using apt (Debian/Ubuntu)"
        DEPS="git wget flex bison gperf python3-pip python3-venv cmake ninja-build"

        echo "Installing: $DEPS"
        sudo apt-get update
        sudo apt-get install -y $DEPS
    elif command_exists yum; then
        info "Using yum (RedHat/CentOS)"
        DEPS="git wget flex bison gperf python3-pip cmake ninja-build"

        echo "Installing: $DEPS"
        sudo yum groupinstall -y "Development Tools"
        sudo yum install -y $DEPS
    else
        warn "Could not detect package manager. Please install manually:"
        echo "  Debian/Ubuntu: sudo apt-get install git wget flex bison gperf python3-pip python3-venv cmake ninja-build"
        echo "  RedHat/CentOS: sudo yum install git wget flex bison gperf python3-pip cmake ninja-build gcc g++"
        read -p "Press Enter when done..."
    fi
elif [[ "$OS" == "macOS" ]]; then
    if command_exists brew; then
        info "Using Homebrew"
        DEPS="git wget flex bison gperf ninja cmake"
        echo "Installing: $DEPS"
        brew install $DEPS
    else
        error "Homebrew not found. Install from https://brew.sh"
    fi
fi

info "System dependencies installed"

# ─────────────────────────────────────────────────────────────────────────────
# Install/upgrade pip packages
# ─────────────────────────────────────────────────────────────────────────────

header "Installing Python packages"

pip3 install --upgrade pip
pip3 install esptool pyserial

info "esptool: $(pip3 show esptool | grep Version | cut -d' ' -f2)"

# ─────────────────────────────────────────────────────────────────────────────
# Install ESP-IDF
# ─────────────────────────────────────────────────────────────────────────────

header "Installing ESP-IDF 5.3.5"

# Check if user wants system-wide or local install
echo "Where should ESP-IDF be installed?"
echo "  [1] ~/esp/idf (local, recommended for development)"
echo "  [2] /opt/esp-idf (system-wide, requires sudo)"
echo ""
read -p "Choice [1]: " choice

case "$choice" in
    2)
        IDF_INSTALL_DIR="/opt/esp-idf"
        NEEDS_SUDO=1
        ;;
    *)
        IDF_INSTALL_DIR="$HOME/esp/idf"
        NEEDS_SUDO=0
        ;;
esac

info "Installing to: $IDF_INSTALL_DIR"

# Create parent directory
PARENT_DIR=$(dirname "$IDF_INSTALL_DIR")
if [[ $NEEDS_SUDO -eq 1 ]]; then
    if [[ ! -d "$PARENT_DIR" ]]; then
        sudo mkdir -p "$PARENT_DIR"
    fi
    sudo git clone --branch v5.3.5 --depth 1 https://github.com/espressif/esp-idf.git "$IDF_INSTALL_DIR"
else
    mkdir -p "$PARENT_DIR"
    git clone --branch v5.3.5 --depth 1 https://github.com/espressif/esp-idf.git "$IDF_INSTALL_DIR"
fi

info "ESP-IDF cloned"

# Run install script
if [[ $NEEDS_SUDO -eq 1 ]]; then
    cd "$IDF_INSTALL_DIR"
    sudo ./install.sh esp32
else
    cd "$IDF_INSTALL_DIR"
    ./install.sh esp32
fi

info "ESP-IDF installed and tools configured"

# ─────────────────────────────────────────────────────────────────────────────
# Configure environment
# ─────────────────────────────────────────────────────────────────────────────

header "Configuring environment"

SHELL_RC=""
if [[ -f ~/.bashrc ]]; then
    SHELL_RC=~/.bashrc
elif [[ -f ~/.bash_profile ]]; then
    SHELL_RC=~/.bash_profile
elif [[ -f ~/.zshrc ]]; then
    SHELL_RC=~/.zshrc
fi

if [[ -n "$SHELL_RC" ]]; then
    # Check if IDF_PATH is already set
    if ! grep -q "IDF_PATH=" "$SHELL_RC"; then
        echo "" >> "$SHELL_RC"
        echo "# PURR OS — ESP-IDF" >> "$SHELL_RC"
        echo "export IDF_PATH=\"$IDF_INSTALL_DIR\"" >> "$SHELL_RC"
        info "Added IDF_PATH to $SHELL_RC"
    else
        warn "IDF_PATH already set in $SHELL_RC"
    fi

    # Source the install script
    echo ". $IDF_INSTALL_DIR/export.sh" >> "$SHELL_RC"
    info "Added IDF export.sh sourcing"
else
    warn "Could not find shell config file (.bashrc, .zshrc, etc.)"
    warn "Please add manually to your shell config:"
    echo "  export IDF_PATH=\"$IDF_INSTALL_DIR\""
    echo "  . $IDF_INSTALL_DIR/export.sh"
fi

# Set for current session
export IDF_PATH="$IDF_INSTALL_DIR"
source "$IDF_INSTALL_DIR/export.sh"

info "Environment configured for current session"

# ─────────────────────────────────────────────────────────────────────────────
# Make SDK script executable
# ─────────────────────────────────────────────────────────────────────────────

header "Setting up PURR OS SDK"

SDK_SCRIPT="$(cd "$(dirname "$0")" && pwd)/sdk.sh"
if [[ -f "$SDK_SCRIPT" ]]; then
    chmod +x "$SDK_SCRIPT"
    info "Made sdk.sh executable"
else
    warn "sdk.sh not found at $SDK_SCRIPT"
fi

# ─────────────────────────────────────────────────────────────────────────────
# Test build (optional)
# ─────────────────────────────────────────────────────────────────────────────

header "Setup complete!"

echo ""
echo "PURR OS is ready to build on $OS"
echo ""
echo "Next steps:"
echo ""
echo "  1. Start a new terminal or run:"
echo "     source ~/.bashrc     (or ~/.zshrc for macOS)"
echo ""
echo "  2. Build for your CYD:"
echo "     ./SDK/sdk.sh --target cyd_s024c --build"
echo ""
echo "  3. Flash to device (port is auto-detected if only one is connected):"
echo "     ./SDK/sdk.sh --target cyd_s024c --flash auto"
echo ""
echo "  4. Or use interactive mode (recommended):"
echo "     ./SDK/sdk.sh"
echo ""
echo "For more info, see docs/QUICKSTART.md"
echo ""

# Optional: test the build
read -p "Test build now? (y/n) [n]: " do_test

if [[ "$do_test" == "y" || "$do_test" == "Y" ]]; then
    echo ""
    header "Running test build (heltec target)"

    cd "$(dirname "$0")/CoreOS"

    if $IDF_PATH/tools/idf.py --version &>/dev/null; then
        info "idf.py working correctly"
        $IDF_PATH/tools/idf.py -DTARGET_DEVICE=heltec -B build_test set-target esp32s3
        $IDF_PATH/tools/idf.py -DTARGET_DEVICE=heltec -B build_test build
        info "Test build successful!"

        # Clean up
        rm -rf build_test
    else
        error "idf.py not working. Please check ESP-IDF installation"
    fi
fi

echo -e "\n${GREEN}Setup complete!${NC}\n"
