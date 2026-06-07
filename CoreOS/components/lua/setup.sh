#!/bin/bash
# setup.sh — Download Lua 5.4 source to CoreOS/components/lua/src/

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$SCRIPT_DIR/src"
LUA_VERSION="5.4.6"
LUA_URL="https://www.lua.org/ftp/lua-$LUA_VERSION.tar.gz"
LUA_TAR="$SCRIPT_DIR/lua-$LUA_VERSION.tar.gz"

if [ ! -d "$SRC_DIR" ]; then
    mkdir -p "$SRC_DIR"
fi

if [ ! -f "$LUA_TAR" ]; then
    echo "[lua] Downloading Lua $LUA_VERSION..."
    curl -L -o "$LUA_TAR" "$LUA_URL"
fi

echo "[lua] Extracting Lua source..."
tar -xzf "$LUA_TAR" -C "$SCRIPT_DIR"
mv "$SCRIPT_DIR/lua-$LUA_VERSION/src"/* "$SRC_DIR/"
rm -rf "$SCRIPT_DIR/lua-$LUA_VERSION" "$LUA_TAR"

echo "[lua] Setup complete. Lua $LUA_VERSION source in $SRC_DIR/"
