#Requires -Version 5.1
# PURR OS — CYD factory bootloader build script
# Minimal recovery image — flashes to factory partition (0x10000).
# Flash this ONCE on a new device. OTA never touches this partition.
#
# Usage:
#   .\build_cyd_boot.ps1 -Build -Flash COM8        # build + flash bootloader only
#   .\build_cyd_boot.ps1 -Build -Clean -Flash COM8  # clean build + flash
#
# For a complete first-time device setup (factory + OS in one go), use build_cyd.ps1:
#   .\build_cyd.ps1 -FullBuild -Clean -FullFlash COM8

[CmdletBinding(PositionalBinding = $false)]
param(
    [switch]$Build,
    [string]$Flash      = '',
    [string]$Monitor    = '',
    [switch]$Configure,
    [switch]$Clean,
    [int]$Baud          = 0
)

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $PSCommandPath
. (Join-Path $ScriptDir '_idf.ps1')

$py  = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $py) { Write-Error '[sdk] python not found on PATH'; exit 1 }
$sdk = Join-Path $ScriptDir 'sdk_core.py'

$pyArgs = [System.Collections.Generic.List[string]]@('--target', 'cyd_boot')
if ($Build)      { $pyArgs.Add('--build') }
if ($Flash)      { $pyArgs.AddRange([string[]]@('--flash',   $Flash)) }
if ($Monitor)    { $pyArgs.AddRange([string[]]@('--monitor', $Monitor)) }
if ($Configure)  { $pyArgs.Add('--configure') }
if ($Clean)      { $pyArgs.Add('--clean') }
if ($Baud -gt 0) { $pyArgs.AddRange([string[]]@('--baud',   [string]$Baud)) }

& $py $sdk @pyArgs
exit $LASTEXITCODE
