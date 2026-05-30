#Requires -Version 5.1
<#
.SYNOPSIS
  PURR OS — Build & Module Installer (PowerShell)
.DESCRIPTION
  No arguments → interactive wizard.
  With -Target → direct build using saved or default module config.
.EXAMPLE
  .\Build.ps1                                      # interactive wizard
  .\Build.ps1 -Target heltec -Mini -Flash COM5    # direct build
  .\Build.ps1 -Setup                              # re-run wizard
#>
[CmdletBinding()]
param(
    [string]$Target       = '',
    [switch]$Mini,
    [switch]$Clean,
    [switch]$Setup,
    [string]$Flash        = '',
    [string]$Monitor      = '',
    [string]$LoraKernel   = 'sx1262',
    [switch]$NoBt,
    [switch]$WithMtp,
    [switch]$WithFlasher,
    [switch]$NoLora
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$BuilderDir = Split-Path -Parent $PSCommandPath
$RepoDir    = Split-Path -Parent $BuilderDir
$CoreOsDir  = Join-Path $RepoDir 'CoreOS'
$LoraDir    = Join-Path $RepoDir 'LoRa Kernels'
$ConfigFile = Join-Path $BuilderDir 'purr_build.cfg'

# ── Module state ──────────────────────────────────────────────────────────────
$ModBt       = 1
$ModMtp      = 0
$ModFlasher  = 0
$ModLora     = 1
$MiniBuild   = [int]$Mini.IsPresent
$FlashPort   = $Flash
$MonPort     = $Monitor
$LoraKern    = $LoraKernel
$ExplicitNoLora = $NoLora.IsPresent

if ($NoBt.IsPresent)        { $ModBt = 0 }
if ($WithMtp.IsPresent)     { $ModMtp = 1 }
if ($WithFlasher.IsPresent) { $ModFlasher = 1 }
if ($NoLora.IsPresent)      { $ModLora = 0 }

# ── Output helpers ────────────────────────────────────────────────────────────
function Write-PurrInfo([string]$Msg) {
    Write-Host '[purr] ' -ForegroundColor Green -NoNewline; Write-Host $Msg
}
function Write-PurrWarn([string]$Msg) {
    Write-Host '[warn] ' -ForegroundColor Yellow -NoNewline; Write-Host $Msg
}
function Write-PurrErr([string]$Msg) {
    Write-Host '[err]  ' -ForegroundColor Red -NoNewline; Write-Host $Msg; exit 1
}
function Write-Divider {
    Write-Host ('─' * 42) -ForegroundColor DarkGray
}

# ── Target helpers ────────────────────────────────────────────────────────────
function Get-Chip {
    switch ($Target) {
        'heltec' { return 'esp32s3' }
        'tdeck'  { return 'esp32s3' }
        'cyd'    { return 'esp32'   }
        default  { Write-PurrErr "Unknown target: $Target" }
    }
}

function Set-TargetDefaults {
    switch ($Target) {
        { $_ -in 'heltec','tdeck' } { $script:ModLora = 1 }
        'cyd'                       { $script:ModLora = 0 }
    }
}

# ── Config persistence ────────────────────────────────────────────────────────
function Read-Config {
    if (-not (Test-Path $ConfigFile)) { return }
    foreach ($line in (Get-Content $ConfigFile -Encoding UTF8)) {
        $parts = $line -split '=', 2
        if ($parts.Count -ne 2) { continue }
        switch ($parts[0].Trim()) {
            'TARGET'      { $script:Target    = $parts[1].Trim() }
            'MINI'        { $script:MiniBuild = [int]$parts[1].Trim() }
            'LORA_KERNEL' { $script:LoraKern  = $parts[1].Trim() }
            'MOD_BT'      { $script:ModBt     = [int]$parts[1].Trim() }
            'MOD_MTP'     { $script:ModMtp    = [int]$parts[1].Trim() }
            'MOD_FLASHER' { $script:ModFlasher= [int]$parts[1].Trim() }
            'MOD_LORA'    { $script:ModLora   = [int]$parts[1].Trim() }
        }
    }
    Write-PurrInfo "Loaded purr_build.cfg  (-Setup to change)"
}

function Save-Config {
    @(
        "TARGET=$Target"
        "MINI=$MiniBuild"
        "LORA_KERNEL=$LoraKern"
        "MOD_BT=$ModBt"
        "MOD_MTP=$ModMtp"
        "MOD_FLASHER=$ModFlasher"
        "MOD_LORA=$ModLora"
    ) | Set-Content $ConfigFile -Encoding UTF8
    Write-PurrInfo "Config saved → purr_build.cfg"
}

# ── Interactive: device picker ────────────────────────────────────────────────
function Select-Target {
    Write-Host ''
    Write-Host '  Select target device:' -ForegroundColor White
    Write-Host ''
    Write-Host '  [1]  Heltec WiFi LoRa 32 V3   ' -NoNewline
    Write-Host 'ESP32-S3  8MB   SSD1306  SX1262 LoRa' -ForegroundColor DarkGray
    Write-Host '  [2]  CYD (ESP32-2432S028R)    ' -NoNewline
    Write-Host 'ESP32     4MB   ILI9341  XPT2046 touch' -ForegroundColor DarkGray
    Write-Host '  [3]  LilyGo T-Deck            ' -NoNewline
    Write-Host 'ESP32-S3  16MB  ST7789   trackball (WIP)' -ForegroundColor DarkGray
    Write-Host ''
    $choice = Read-Host '  Choice [1]'
    switch ($choice) {
        '2'     { $script:Target = 'cyd'    }
        '3'     { $script:Target = 'tdeck'  }
        default { $script:Target = 'heltec' }
    }
}

# ── Interactive: LoRa kernel picker ──────────────────────────────────────────
function Select-LoraKernel {
    Write-Host ''
    Write-Host '  LoRa kernel backend:' -ForegroundColor White
    Write-Host ''
    Write-Host '  [1]  SX1262   ' -NoNewline; Write-Host 'SPI — Heltec V3, T-Deck (default)' -ForegroundColor DarkGray
    Write-Host '  [2]  RAK3172  ' -NoNewline; Write-Host 'UART AT — CattoBoardV1 PCB' -ForegroundColor DarkGray
    Write-Host '  [3]  SX1276   ' -NoNewline; Write-Host 'SPI — generic RFM95W breakout' -ForegroundColor DarkGray
    Write-Host ''
    $choice = Read-Host '  Choice [1]'
    switch ($choice) {
        '2'     { $script:LoraKern = 'rak3172' }
        '3'     { $script:LoraKern = 'sx1276'  }
        default { $script:LoraKern = 'sx1262'  }
    }
}

# ── Interactive: module wizard ────────────────────────────────────────────────
function Invoke-ModuleWizard {
    $keys     = @('BT',         'MTP',         'FLASHER',           'LORA',          'MICROPYTHON'  )
    $names    = @('Bluetooth',  'MTP USB',     'OTA Flasher',       'LoRa Radio',    'MicroPython'  )
    $descs    = @(
        'bt_manager — BLE + Classic stack (~200KB flash)',
        'mtp_manager — USB file transfer',
        'flasher — OTA partition flasher',
        'lora_manager — LoRa radio driver',
        'mpython_runtime — .meow app interpreter'
    )
    $hideCyd  = @($false, $false, $false, $true, $false)

    # State array mirrors: ModBt, ModMtp, ModFlasher, ModLora, MicroPython=1-Mini
    $state = [int[]]@($script:ModBt, $script:ModMtp, $script:ModFlasher, $script:ModLora, (1 - $script:MiniBuild))

    while ($true) {
        Write-Divider
        Write-Host ''
        Write-Host "  Kernel modules — " -NoNewline
        Write-Host $Target -ForegroundColor Cyan
        Write-Host ''
        Write-Host '  Always compiled:  ' -ForegroundColor DarkGray -NoNewline
        Write-Host 'wifi_manager · power_manager' -ForegroundColor DarkGray
        switch ($Target) {
            'heltec' { Write-Host '                    display_ssd1306' -ForegroundColor DarkGray }
            'cyd'    { Write-Host '                    display_ili9341 · touch_xpt2046 · partition_manager' -ForegroundColor DarkGray }
            'tdeck'  { Write-Host '                    display_ssd1306' -ForegroundColor DarkGray }
        }
        Write-Host ''
        Write-Host '  Optional (enter number to toggle):' -ForegroundColor White
        Write-Host ''

        $visibleCount = 0
        $visibleMap   = @()

        for ($i = 0; $i -lt $keys.Count; $i++) {
            if ($Target -eq 'cyd' -and $hideCyd[$i]) { continue }
            $visibleCount++
            $visibleMap += $i
            if ($state[$i] -eq 1) {
                Write-Host "  [$visibleCount]  " -NoNewline
                Write-Host '●' -ForegroundColor Green -NoNewline
                Write-Host "  $($names[$i].PadRight(14))  " -NoNewline
                Write-Host 'ON   ' -ForegroundColor Green -NoNewline
            } else {
                Write-Host "  [$visibleCount]  " -NoNewline
                Write-Host '○' -ForegroundColor DarkGray -NoNewline
                Write-Host "  $($names[$i].PadRight(14))  " -NoNewline
                Write-Host 'OFF  ' -ForegroundColor DarkGray -NoNewline
            }
            Write-Host $descs[$i] -ForegroundColor DarkGray
        }

        # LoRa kernel sub-item
        if ($Target -ne 'cyd' -and $state[3] -eq 1) {
            Write-Host "       " -NoNewline
            Write-Host "└─ kernel: $LoraKern  ([k] to change)" -ForegroundColor DarkGray
        }

        Write-Host ''
        $prompt = "  Toggle [1-$visibleCount]"
        if ($Target -ne 'cyd') { $prompt += ', [k] LoRa kernel' }
        $prompt += ', or Enter to build'
        $choice = Read-Host $prompt

        if ([string]::IsNullOrWhiteSpace($choice)) { break }

        if ($choice -eq 'k' -and $Target -ne 'cyd') {
            Select-LoraKernel; continue
        }

        $idx = 0
        if ([int]::TryParse($choice, [ref]$idx) -and $idx -ge 1 -and $idx -le $visibleCount) {
            $arrIdx = $visibleMap[$idx - 1]
            $state[$arrIdx] = 1 - $state[$arrIdx]
        } else {
            Write-PurrWarn "Enter a number between 1 and $visibleCount"
        }
    }

    $script:ModBt      = $state[0]
    $script:ModMtp     = $state[1]
    $script:ModFlasher = $state[2]
    $script:ModLora    = $state[3]
    $script:MiniBuild  = 1 - $state[4]
}

# ── LoRa kernel installer ─────────────────────────────────────────────────────
function Install-LoraKernel {
    $folderMap = @{ 'sx1262' = 'SX1262'; 'rak3172' = 'RAK3172'; 'sx1276' = 'SX1276_RFM95W' }
    $folder = $folderMap[$LoraKern]
    if (-not $folder) { Write-PurrWarn "Unknown LoRa kernel '$LoraKern'"; return }

    $src = Join-Path $LoraDir $folder
    $dst = Join-Path $CoreOsDir 'system\kernel\modules'

    if (-not (Test-Path $src)) { Write-PurrWarn "LoRa kernel not found: $src"; return }
    Write-PurrInfo "LoRa kernel → $folder → modules/"
    Copy-Item "$src\*" $dst -Recurse -Force
}

# ── Environment check ─────────────────────────────────────────────────────────
function Test-BuildEnv {
    if (-not $env:IDF_PATH) {
        Write-PurrErr "IDF_PATH not set. Open the 'ESP-IDF 5.x CMD' shortcut from Start Menu."
    }
    if (-not (Test-Path $CoreOsDir)) {
        Write-PurrErr "CoreOS directory not found: $CoreOsDir"
    }
    $mpHeader = Join-Path $CoreOsDir 'components\micropython\ports\embed\port\micropython_embed.h'
    if ($MiniBuild -eq 0 -and -not (Test-Path $mpHeader)) {
        Write-PurrWarn "MicroPython submodule missing — full build will fail."
        Write-PurrWarn "Add -Mini or clone the submodule (see Builder/HOWTO.md §3)."
        Write-Host ''
    }
}

# ── Build summary banner ──────────────────────────────────────────────────────
function Show-Banner {
    $chip = Get-Chip
    Write-Divider
    Write-Host ''
    Write-Host '  PURR OS  ' -NoNewline
    Write-Host "$Target ($chip)" -ForegroundColor Cyan
    $variant = if ($MiniBuild -eq 1) { 'mini — no MicroPython' } else { 'full — with MicroPython' }
    Write-Host "  Variant  : $variant"
    $mods = @()
    if ($ModBt      -eq 1) { $mods += 'bt' }
    if ($ModLora    -eq 1) { $mods += "lora($LoraKern)" }
    if ($ModMtp     -eq 1) { $mods += 'mtp' }
    if ($ModFlasher -eq 1) { $mods += 'flasher' }
    Write-Host "  Modules  : $($mods -join '  ')"
    if ($FlashPort) { Write-Host "  Flash    : $FlashPort" }
    if ($MonPort)   { Write-Host "  Monitor  : $MonPort" }
    Write-Divider
    Write-Host ''
}

# ── Main ──────────────────────────────────────────────────────────────────────

Read-Config

# Wizard: no target given, or -Setup
if ([string]::IsNullOrEmpty($Target) -or $Setup.IsPresent) {
    if ([string]::IsNullOrEmpty($Target)) { Select-Target }
    Set-TargetDefaults
    Invoke-ModuleWizard
    Write-Host ''
    $fp = Read-Host '  Flash port (COM5 / COM8, blank to skip)'
    if ($fp) { $FlashPort = $fp }
    $mp = Read-Host '  Monitor port (blank to skip)'
    if ($mp) { $MonPort = $mp }
    Save-Config
} else {
    if (-not $ExplicitNoLora) { Set-TargetDefaults }
}

Test-BuildEnv
Show-Banner

if ($ModLora -eq 1 -and $Target -ne 'cyd') { Install-LoraKernel }

$defaultsSrc = Join-Path $BuilderDir "targets\$Target.defaults"
if (Test-Path $defaultsSrc) {
    Write-PurrInfo "$Target.defaults → sdkconfig.defaults"
    Copy-Item $defaultsSrc (Join-Path $CoreOsDir 'sdkconfig.defaults') -Force
} else {
    Write-PurrWarn "targets/$Target.defaults not found, keeping existing sdkconfig.defaults"
}

Set-Location $CoreOsDir

if ($Clean.IsPresent) {
    Write-PurrInfo 'fullclean...'
    idf.py fullclean
    $sdkconfig = Join-Path $CoreOsDir 'sdkconfig'
    if (Test-Path $sdkconfig) { Remove-Item $sdkconfig }
}

$chip = Get-Chip
Write-PurrInfo "set-target $chip"
idf.py set-target $chip

Write-PurrInfo "build  TARGET=$Target  BT=$ModBt  MTP=$ModMtp  FLASHER=$ModFlasher  LORA=$ModLora  MINI=$MiniBuild"
idf.py `
    "-DTARGET_DEVICE=$Target" `
    "-DBUILD_MINI=$MiniBuild" `
    "-DPURR_ENABLE_BT=$ModBt" `
    "-DPURR_ENABLE_MTP=$ModMtp" `
    "-DPURR_ENABLE_FLASHER=$ModFlasher" `
    "-DPURR_ENABLE_LORA=$ModLora" `
    build

if ($FlashPort) {
    Write-PurrInfo "flashing → $FlashPort"
    idf.py -p $FlashPort flash
}

if ($MonPort) {
    Write-PurrInfo "monitor on $MonPort  (Ctrl+] to exit)"
    idf.py -p $MonPort monitor
}

Write-PurrInfo 'done.'
