#Requires -Version 5.1
# PURR OS — LilyGo T-Deck build script (WIP — ST7789 driver not yet complete)
# ESP32-S3, 16MB flash, ST7789 display, trackball, SX1262 LoRa (default).
#
# Usage:
#   .\build_tdeck.ps1                                        # interactive menu
#   .\build_tdeck.ps1 -Build -Flash COM6
#   .\build_tdeck.ps1 -Build -Shell blackberry -Flash COM6
#   .\build_tdeck.ps1 -Build -TdeckPlus -Flash COM6          # T-Deck Plus (GPS)
#   .\build_tdeck.ps1 -Build -WithMesh -LoraKernel sx1262 -Flash COM6

[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('blackberry','smol')]
    [string]$Shell      = '',

    [switch]$Build,
    [string]$Flash      = '',
    [string]$Monitor    = '',
    [switch]$Configure,

    [switch]$Clean,
    [switch]$Mini,                # strip MicroPython runtime
    [switch]$NoBt,                # disable Bluetooth
    [switch]$NoLora,              # disable LoRa radio
    [switch]$WithMesh,            # enable Meshtastic co-resident stack (requires LoRa)
    [switch]$WithMtp,             # enable MTP USB
    [switch]$WithFlasher,         # enable OTA flasher module
    [switch]$TdeckPlus,           # T-Deck Plus variant (u-blox MIA-M10Q GPS)

    [ValidateSet('sx1262','rak3172','sx1276')]
    [string]$LoraKernel = '',     # LoRa backend (default: sx1262)

    [int]$Baud          = 0
)

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $PSCommandPath
. (Join-Path $ScriptDir '_idf.ps1')

$py = if ($env:IDF_PYTHON -and (Test-Path $env:IDF_PYTHON)) { $env:IDF_PYTHON } else { (Get-Command python -ErrorAction SilentlyContinue).Source }
if (-not $py) { Write-Error '[sdk] python not found on PATH'; exit 1 }
$sdk = Join-Path $ScriptDir 'sdk_core.py'

$pyArgs = [System.Collections.Generic.List[string]]@('--target', 'tdeck')
if ($Shell)       { $pyArgs.AddRange([string[]]@('--shell',       $Shell)) }
if ($Build)       { $pyArgs.Add('--build') }
if ($Flash)       { $pyArgs.AddRange([string[]]@('--flash',       $Flash)) }
if ($Monitor)     { $pyArgs.AddRange([string[]]@('--monitor',     $Monitor)) }
if ($Configure)   { $pyArgs.Add('--configure') }
if ($Clean)       { $pyArgs.Add('--clean') }
if ($Mini)        { $pyArgs.Add('--mini') }
if ($NoBt)        { $pyArgs.Add('--no-bt') }
if ($NoLora)      { $pyArgs.Add('--no-lora') }
if ($WithMesh)    { $pyArgs.Add('--mesh') }
if ($WithMtp)     { $pyArgs.Add('--mtp') }
if ($WithFlasher) { $pyArgs.Add('--flasher') }
if ($TdeckPlus)   { $pyArgs.Add('--tdeck-plus') }
if ($LoraKernel)  { $pyArgs.AddRange([string[]]@('--lora-kernel', $LoraKernel)) }
if ($Baud -gt 0)  { $pyArgs.AddRange([string[]]@('--baud',        [string]$Baud)) }

& $py $sdk @pyArgs
exit $LASTEXITCODE
