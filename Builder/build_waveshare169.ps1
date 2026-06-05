#Requires -Version 5.1
# PURR OS — Waveshare 1.69" ESP32-S3 (ST7789 240x280, CST816S)  [WIP]
#
# WIP target — verify pin assignments before flashing to hardware.
# No shell yet; boots to "PURR OS WIP" splash via KITT.
#
# First-time flash (kernel + OS):
#   .\build_waveshare169.ps1 -FullBuild -Clean -FullFlash COM8
#
# OS update only:
#   .\build_waveshare169.ps1 -Build -Flash COM8
#
# Interactive menu (no flags):
#   .\build_waveshare169.ps1

[CmdletBinding(PositionalBinding = $false)]
param(
    [switch]$Build,
    [switch]$FullBuild,
    [string]$Flash    = '',
    [string]$FullFlash= '',
    [string]$Monitor  = '',
    [switch]$Configure,
    [switch]$Clean,
    [int]$Baud        = 0
)

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $PSCommandPath
. (Join-Path $ScriptDir '_idf.ps1')

$py = if ($env:IDF_PYTHON -and (Test-Path $env:IDF_PYTHON)) { $env:IDF_PYTHON } else { (Get-Command python -ErrorAction SilentlyContinue).Source }
if (-not $py) { Write-Error '[sdk] python not found on PATH'; exit 1 }
$sdk = Join-Path $ScriptDir 'sdk_core.py'

$pyArgs = [System.Collections.Generic.List[string]]@('--target', 'waveshare169')
if ($FullBuild)   { $pyArgs.Add('--full-build') }
elseif ($Build)   { $pyArgs.Add('--build') }
if ($FullFlash)   { $pyArgs.AddRange([string[]]@('--full-flash', $FullFlash)) }
elseif ($Flash)   { $pyArgs.AddRange([string[]]@('--flash',      $Flash)) }
if ($Monitor)     { $pyArgs.AddRange([string[]]@('--monitor',    $Monitor)) }
if ($Configure)   { $pyArgs.Add('--configure') }
if ($Clean)       { $pyArgs.Add('--clean') }
if ($Baud -gt 0)  { $pyArgs.AddRange([string[]]@('--baud', [string]$Baud)) }

& $py $sdk @pyArgs
exit $LASTEXITCODE
