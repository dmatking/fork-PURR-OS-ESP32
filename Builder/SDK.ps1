#Requires -Version 5.1
# PURR OS SDK — PowerShell wrapper
# Sources ESP-IDF environment, then delegates all logic to sdk_core.py.
#
# Usage (interactive):
#   .\SDK.ps1
#
# Usage (direct / CI):
#   .\SDK.ps1 -Target cyd -Shell explorer -Build -Flash COM8
#   .\SDK.ps1 -Target cyd -FullBuild -Clean -FullFlash COM8   # first-time CYD setup
#   .\SDK.ps1 -Target cyd_boot -Build -Flash COM8 -Clean
#   .\SDK.ps1 -Target heltec -Build -Mini -NoLora
#
# All switches are forwarded to sdk_core.py as CLI args.

[CmdletBinding(PositionalBinding = $false)]
param(
    # ── Target & shell ───────────────────────────────────────────────────────────
    [ValidateSet('heltec','cyd','cyd_boot','tdeck')]
    [string]$Target      = '',

    [ValidateSet('both','blackberry','explorer','smol','none')]
    [string]$Shell       = '',

    # ── Actions ──────────────────────────────────────────────────────────────────
    [switch]$Build,              # build selected target
    [switch]$FullBuild,          # build cyd_boot + cyd back-to-back (CYD only)
    [string]$Flash       = '',   # flash selected target to port
    [string]$FullFlash   = '',   # flash factory + ota_0 + SPIFFS in one pass (CYD only)
    [string]$Monitor     = '',   # open serial monitor
    [switch]$Configure,          # run wizard only, save config, then exit

    # ── Build options ─────────────────────────────────────────────────────────────
    [switch]$Clean,              # wipe build dir before building
    [switch]$Mini,               # strip MicroPython runtime

    # ── Kernel modules ────────────────────────────────────────────────────────────
    [switch]$NoBt,               # disable Bluetooth
    [switch]$Lora,               # enable LoRa (override default — heltec/tdeck only)
    [switch]$NoLora,             # disable LoRa
    [switch]$Mesh,               # enable Meshtastic (requires LoRa)
    [switch]$Mtp,                # enable MTP USB
    [switch]$Flasher,            # enable OTA flasher module

    # ── LoRa / hardware ───────────────────────────────────────────────────────────
    [ValidateSet('sx1262','rak3172','sx1276')]
    [string]$LoraKernel  = '',
    [switch]$TdeckPlus,          # T-Deck Plus variant (GPS)

    # ── Flash options ─────────────────────────────────────────────────────────────
    [int]$Baud           = 0     # flash baud rate (default 460800)
)

$ErrorActionPreference = 'Stop'
$ScriptDir  = Split-Path -Parent $PSCommandPath
$CoreSdkPy  = Join-Path $ScriptDir 'sdk_core.py'

if (-not (Test-Path $CoreSdkPy)) {
    Write-Error "[sdk] sdk_core.py not found at: $CoreSdkPy"
    exit 1
}

. (Join-Path $ScriptDir '_idf.ps1')

# ── Map PowerShell switches → python CLI args ─────────────────────────────────
$py     = (Get-Command python -ErrorAction SilentlyContinue).Source
$pyArgs = [System.Collections.Generic.List[string]]::new()

if ($Target)          { $pyArgs.AddRange([string[]]@('--target',      $Target)) }
if ($Shell)           { $pyArgs.AddRange([string[]]@('--shell',       $Shell)) }
if ($FullBuild)       { $pyArgs.Add('--full-build') }
elseif ($Build)       { $pyArgs.Add('--build') }
if ($FullFlash)       { $pyArgs.AddRange([string[]]@('--full-flash',  $FullFlash)) }
elseif ($Flash)       { $pyArgs.AddRange([string[]]@('--flash',       $Flash)) }
if ($Monitor)         { $pyArgs.AddRange([string[]]@('--monitor',     $Monitor)) }
if ($Configure)       { $pyArgs.Add('--configure') }
if ($Clean)           { $pyArgs.Add('--clean') }
if ($Mini)            { $pyArgs.Add('--mini') }
if ($NoBt)            { $pyArgs.Add('--no-bt') }
if ($Lora)            { $pyArgs.Add('--lora') }
if ($NoLora)          { $pyArgs.Add('--no-lora') }
if ($Mesh)            { $pyArgs.Add('--mesh') }
if ($Mtp)             { $pyArgs.Add('--mtp') }
if ($Flasher)         { $pyArgs.Add('--flasher') }
if ($LoraKernel)      { $pyArgs.AddRange([string[]]@('--lora-kernel', $LoraKernel)) }
if ($TdeckPlus)       { $pyArgs.Add('--tdeck-plus') }
if ($Baud -gt 0)      { $pyArgs.AddRange([string[]]@('--baud',        [string]$Baud)) }

# ── Launch Python core ────────────────────────────────────────────────────────
& $py $CoreSdkPy @pyArgs
exit $LASTEXITCODE
