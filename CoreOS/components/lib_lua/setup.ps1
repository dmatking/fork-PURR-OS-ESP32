#Requires -Version 5.1
# setup.ps1 — Download and extract Lua 5.4 source

$LuaVersion = "5.4.6"
$LuaUrl = "https://www.lua.org/ftp/lua-$LuaVersion.tar.gz"
$ScriptDir = $PSScriptRoot
$SrcDir = Join-Path $ScriptDir "src"
$TarFile = Join-Path $ScriptDir "lua-$LuaVersion.tar.gz"

if (Test-Path $SrcDir) {
    Write-Host "[lua] src/ already exists, skipping download" -ForegroundColor Green
    exit 0
}

Write-Host "[lua] Creating src/ directory..." -ForegroundColor Cyan
New-Item -ItemType Directory -Force $SrcDir | Out-Null

Write-Host "[lua] Downloading Lua $LuaVersion..." -ForegroundColor Cyan
try {
    (New-Object Net.WebClient).DownloadFile($LuaUrl, $TarFile)
} catch {
    Write-Error "Failed to download Lua: $_"
    exit 1
}

Write-Host "[lua] Extracting source..." -ForegroundColor Cyan
try {
    # Use tar (available on Windows 10+)
    & tar -xzf $TarFile -C $ScriptDir
    Move-Item -Path "$ScriptDir/lua-$LuaVersion/src/*" -Destination $SrcDir -Force
    Remove-Item -Path "$ScriptDir/lua-$LuaVersion" -Recurse -Force
    Remove-Item -Path $TarFile -Force
} catch {
    Write-Error "Failed to extract: $_"
    exit 1
}

Write-Host "[lua] Setup complete. Lua $LuaVersion source in $SrcDir/" -ForegroundColor Green
