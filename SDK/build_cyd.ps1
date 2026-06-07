#Requires -Version 5.1
# PURR OS — CYD (ESP32-2432S024C) build script
# Full OS image — flashes to ota_0 (0x110000).
# No LoRa/Mesh — CYD has no radio hardware.
#
# First-time device setup (builds factory + OS, flashes everything):
#   .\build_cyd.ps1 -FullBuild -Clean -FullFlash COM8
#
# Normal OS update after first flash:
#   .\build_cyd.ps1 -Build -Flash COM8
#
# Interactive menu (no flags):
#   .\build_cyd.ps1

[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('both','blackberry','explorer','none')]
    [string]$Shell      = '',

    [switch]$Build,               # build cyd OS only
    [switch]$FullBuild,           # build cyd_boot + cyd back-to-back
    [string]$Flash      = '',     # flash cyd OS only to port
    [string]$FullFlash  = '',     # flash factory + ota_0 + SPIFFS in one pass
    [string]$Monitor    = '',
    [switch]$Configure,

    [switch]$Clean,
    [switch]$Mini,                # strip MicroPython runtime
    [switch]$NoBt,                # disable Bluetooth
    [switch]$WithMtp,             # enable MTP USB
    [switch]$WithFlasher,         # enable OTA flasher module
    [int]$Baud          = 0
)

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $PSCommandPath
. (Join-Path $ScriptDir '_idf.ps1')

$py = if ($env:IDF_PYTHON -and (Test-Path $env:IDF_PYTHON)) { $env:IDF_PYTHON } else { (Get-Command python -ErrorAction SilentlyContinue).Source }
if (-not $py) { Write-Error '[sdk] python not found on PATH'; exit 1 }
$sdk = Join-Path $ScriptDir 'sdk_core.py'

$pyArgs = [System.Collections.Generic.List[string]]@('--target', 'cyd')
if ($Shell)       { $pyArgs.AddRange([string[]]@('--shell',      $Shell)) }
if ($FullBuild)   { $pyArgs.Add('--full-build') }
elseif ($Build)   { $pyArgs.Add('--build') }
if ($FullFlash)   { $pyArgs.AddRange([string[]]@('--full-flash', $FullFlash)) }
elseif ($Flash)   { $pyArgs.AddRange([string[]]@('--flash',      $Flash)) }
if ($Monitor)     { $pyArgs.AddRange([string[]]@('--monitor',    $Monitor)) }
if ($Configure)   { $pyArgs.Add('--configure') }
if ($Clean)       { $pyArgs.Add('--clean') }
if ($Mini)        { $pyArgs.Add('--mini') }
if ($NoBt)        { $pyArgs.Add('--no-bt') }
if ($WithMtp)     { $pyArgs.Add('--mtp') }
if ($WithFlasher) { $pyArgs.Add('--flasher') }
if ($Baud -gt 0)  { $pyArgs.AddRange([string[]]@('--baud', [string]$Baud)) }

& $py $sdk @pyArgs
exit $LASTEXITCODE
