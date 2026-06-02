#Requires -Version 5.1
# PURR OS -- Build & Module Installer (PowerShell)
# No arguments -> interactive wizard.
# With -Target  -> direct build using saved or default module config.
#
# Examples:
#   .\Build.ps1                                     # interactive wizard
#   .\Build.ps1 -Target heltec -Mini -Flash COM5   # direct build
#   .\Build.ps1 -Setup                             # re-run wizard

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
    [switch]$NoLora,
    [switch]$TdeckPlus,
    [switch]$WithMesh
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$BuilderDir = Split-Path -Parent $PSCommandPath
$RepoDir    = Split-Path -Parent $BuilderDir
$CoreOsDir  = Join-Path $RepoDir 'CoreOS'
$LoraDir    = Join-Path $RepoDir 'LoRa Kernels'
$ConfigFile = Join-Path $BuilderDir 'purr_build.cfg'

# ---- Module state -----------------------------------------------------------
$ModBt          = 1
$ModMtp         = 0
$ModFlasher     = 0
$ModLora        = 1
$ModMesh        = 0
$ModTdeckPlus   = 0
$MiniBuild      = [int]$Mini.IsPresent
$FlashPort      = $Flash
$MonPort        = $Monitor
$LoraKern       = $LoraKernel
$ExplicitNoLora = $NoLora.IsPresent

if ($NoBt.IsPresent)        { $ModBt = 0 }
if ($WithMtp.IsPresent)     { $ModMtp = 1 }
if ($WithFlasher.IsPresent) { $ModFlasher = 1 }
if ($NoLora.IsPresent)      { $ModLora = 0 }
if ($TdeckPlus.IsPresent)   { $ModTdeckPlus = 1 }
if ($WithMesh.IsPresent)    { $ModMesh = 1 }

# ---- Output helpers ---------------------------------------------------------
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
    Write-Host ('-' * 42) -ForegroundColor DarkGray
}

# ---- Target helpers ---------------------------------------------------------
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

# ---- Config persistence -----------------------------------------------------
function Read-Config {
    if (-not (Test-Path $ConfigFile)) { return }
    foreach ($line in (Get-Content $ConfigFile -Encoding UTF8)) {
        $parts = $line -split '=', 2
        if ($parts.Count -ne 2) { continue }
        switch ($parts[0].Trim()) {
            'TARGET'      { $script:Target     = $parts[1].Trim() }
            'MINI'        { $script:MiniBuild  = [int]$parts[1].Trim() }
            'LORA_KERNEL' { $script:LoraKern   = $parts[1].Trim() }
            'MOD_BT'       { $script:ModBt        = [int]$parts[1].Trim() }
            'MOD_MTP'      { $script:ModMtp       = [int]$parts[1].Trim() }
            'MOD_FLASHER'  { $script:ModFlasher   = [int]$parts[1].Trim() }
            'MOD_LORA'     { $script:ModLora      = [int]$parts[1].Trim() }
            'MOD_MESH'     { $script:ModMesh      = [int]$parts[1].Trim() }
            'TDECK_PLUS'   { $script:ModTdeckPlus = [int]$parts[1].Trim() }
        }
    }
    Write-PurrInfo 'Loaded purr_build.cfg  (-Setup to change)'
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
        "MOD_MESH=$ModMesh"
        "TDECK_PLUS=$ModTdeckPlus"
    ) | Set-Content $ConfigFile -Encoding UTF8
    Write-PurrInfo 'Config saved -> purr_build.cfg'
}

# ---- Interactive: device picker ---------------------------------------------
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

# ---- Interactive: T-Deck variant picker -------------------------------------
function Select-TdeckVariant {
    Write-Host ''
    Write-Host '  T-Deck variant:' -ForegroundColor White
    Write-Host ''
    Write-Host '  [1]  Normal  ' -NoNewline; Write-Host 'Original T-Deck (no GPS, no battery management)' -ForegroundColor DarkGray
    Write-Host '  [2]  Plus    ' -NoNewline; Write-Host 'T-Deck Plus (u-blox MIA-M10Q GPS + larger battery)' -ForegroundColor DarkGray
    Write-Host ''
    $choice = Read-Host '  Choice [1]'
    if ($choice -eq '2') { $script:ModTdeckPlus = 1 } else { $script:ModTdeckPlus = 0 }
}

# ---- Interactive: LoRa kernel picker ----------------------------------------
function Select-LoraKernel {
    Write-Host ''
    Write-Host '  LoRa kernel backend:' -ForegroundColor White
    Write-Host ''
    Write-Host '  [1]  SX1262   ' -NoNewline; Write-Host 'SPI -- Heltec V3, T-Deck (default)' -ForegroundColor DarkGray
    Write-Host '  [2]  RAK3172  ' -NoNewline; Write-Host 'UART AT -- CattoBoardV1 PCB' -ForegroundColor DarkGray
    Write-Host '  [3]  SX1276   ' -NoNewline; Write-Host 'SPI -- generic RFM95W breakout' -ForegroundColor DarkGray
    Write-Host ''
    $choice = Read-Host '  Choice [1]'
    switch ($choice) {
        '2'     { $script:LoraKern = 'rak3172' }
        '3'     { $script:LoraKern = 'sx1276'  }
        default { $script:LoraKern = 'sx1262'  }
    }
}

# ---- Interactive: module wizard ---------------------------------------------
function Invoke-ModuleWizard {
    $keys     = @('BT',        'MTP',        'FLASHER',           'LORA',         'MESH',                      'MICROPYTHON'  )
    $names    = @('Bluetooth', 'MTP USB',    'OTA Flasher',       'LoRa Radio',   'Meshtastic',                'MicroPython'  )
    $descs    = @(
        'bt_manager      -- BLE + Classic stack (~200KB flash)',
        'mtp_manager     -- USB file transfer',
        'flasher         -- OTA partition flasher',
        'lora_manager    -- LoRa radio driver',
        'mesh_manager    -- Meshtastic co-resident stack (requires LoRa)',
        'mpython_runtime -- .meow app interpreter'
    )
    $hideCyd  = @($false, $false, $false, $true, $true, $false)

    $state = [int[]]@($script:ModBt, $script:ModMtp, $script:ModFlasher, $script:ModLora, $script:ModMesh, (1 - $script:MiniBuild))

    while ($true) {
        Write-Divider
        Write-Host ''
        Write-Host '  Kernel modules -- ' -NoNewline
        Write-Host $Target -ForegroundColor Cyan
        Write-Host ''
        Write-Host '  Always compiled:  ' -ForegroundColor DarkGray -NoNewline
        Write-Host 'wifi_manager  power_manager' -ForegroundColor DarkGray
        switch ($Target) {
            'heltec' { Write-Host '                    display_ssd1306' -ForegroundColor DarkGray }
            'cyd'    { Write-Host '                    display_ili9341  touch_xpt2046  partition_manager' -ForegroundColor DarkGray }
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
                Write-Host '[*]' -ForegroundColor Green -NoNewline
                Write-Host "  $($names[$i].PadRight(14))  " -NoNewline
                Write-Host 'ON   ' -ForegroundColor Green -NoNewline
            } else {
                Write-Host "  [$visibleCount]  " -NoNewline
                Write-Host '[ ]' -ForegroundColor DarkGray -NoNewline
                Write-Host "  $($names[$i].PadRight(14))  " -NoNewline
                Write-Host 'OFF  ' -ForegroundColor DarkGray -NoNewline
            }
            Write-Host $descs[$i] -ForegroundColor DarkGray
        }

        if ($Target -ne 'cyd' -and $state[3] -eq 1) {
            Write-Host "       " -NoNewline
            Write-Host "  +-- kernel: $LoraKern  ([k] to change)" -ForegroundColor DarkGray
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
    $script:ModMesh    = $state[4]
    $script:MiniBuild  = 1 - $state[5]
}

# ---- LoRa kernel installer --------------------------------------------------
function Install-LoraKernel {
    $folderMap = @{ 'sx1262' = 'SX1262'; 'rak3172' = 'RAK3172'; 'sx1276' = 'SX1276_RFM95W' }
    $folder = $folderMap[$LoraKern]
    if (-not $folder) { Write-PurrWarn "Unknown LoRa kernel '$LoraKern'"; return }

    $src = Join-Path $LoraDir $folder
    $dst = Join-Path $CoreOsDir 'system\kernel\modules'

    if (-not (Test-Path $src)) { Write-PurrWarn "LoRa kernel not found: $src"; return }
    Write-PurrInfo "LoRa kernel -> $folder -> modules/"
    Copy-Item "$src\*" $dst -Recurse -Force
}

# ---- Environment check ------------------------------------------------------
function Test-BuildEnv {
    if (-not $env:IDF_PATH) {
        Write-PurrErr "IDF_PATH not set. Open the 'ESP-IDF 5.x CMD' shortcut from Start Menu (requires IDF 5.1.x or 5.2.x)."
    }
    if (-not (Test-Path $CoreOsDir)) {
        Write-PurrErr "CoreOS directory not found: $CoreOsDir"
    }
    $mpHeader = Join-Path $CoreOsDir 'components\micropython\ports\embed\port\micropython_embed.h'
    if ($MiniBuild -eq 0 -and -not (Test-Path $mpHeader)) {
        Write-PurrWarn 'MicroPython submodule missing -- full build will fail.'
        Write-PurrWarn 'Add -Mini or clone the submodule (see Builder/HOWTO.md §3).'
        Write-Host ''
    }
}

# ---- Build summary banner ---------------------------------------------------
function Show-Banner {
    $chip = Get-Chip
    $displayTarget = if ($Target -eq 'tdeck' -and $ModTdeckPlus -eq 1) { 'tdeck-plus' } else { $Target }
    Write-Divider
    Write-Host ''
    Write-Host '  PURR OS  ' -NoNewline
    Write-Host "$displayTarget ($chip)" -ForegroundColor Cyan
    $variant = if ($MiniBuild -eq 1) { 'mini -- no MicroPython' } else { 'full -- with MicroPython' }
    Write-Host "  Variant  : $variant"
    $mods = @()
    if ($ModBt      -eq 1) { $mods += 'bt' }
    if ($ModLora    -eq 1) { $mods += "lora($LoraKern)" }
    if ($ModMesh    -eq 1) { $mods += 'mesh' }
    if ($ModMtp     -eq 1) { $mods += 'mtp' }
    if ($ModFlasher -eq 1) { $mods += 'flasher' }
    Write-Host "  Modules  : $($mods -join '  ')"
    if ($FlashPort) { Write-Host "  Flash    : $FlashPort" }
    if ($MonPort)   { Write-Host "  Monitor  : $MonPort" }
    Write-Divider
    Write-Host ''
}

# ---- Main -------------------------------------------------------------------

Read-Config

if ([string]::IsNullOrEmpty($Target) -or $Setup.IsPresent) {
    if ([string]::IsNullOrEmpty($Target)) { Select-Target }
    if ($Target -eq 'tdeck') { Select-TdeckVariant }
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
    Write-PurrInfo "$Target.defaults -> sdkconfig.defaults"
    Copy-Item $defaultsSrc (Join-Path $CoreOsDir 'sdkconfig.defaults') -Force
} else {
    Write-PurrWarn "targets/$Target.defaults not found, keeping existing sdkconfig.defaults"
}

Set-Location $CoreOsDir

# arduino-esp32 3.x targets IDF 5.1.x; bypass its version gate on IDF 6.x
$env:ARDUINO_SKIP_IDF_VERSION_CHECK = '1'

if ($Clean.IsPresent) {
    Write-PurrInfo 'clean...'
    $buildDir = Join-Path $CoreOsDir 'build'
    if (Test-Path $buildDir) { Remove-Item $buildDir -Recurse -Force -Confirm:$false }
    $sdkcfg = Join-Path $CoreOsDir 'sdkconfig'
    if (Test-Path $sdkcfg) { Remove-Item $sdkcfg -Force }
}

$chip = Get-Chip
$cmakeFlags = @(
    "-DTARGET_DEVICE=$Target"
    "-DBUILD_MINI=$MiniBuild"
    "-DBUILD_TDECK_PLUS=$ModTdeckPlus"
    "-DPURR_ENABLE_BT=$ModBt"
    "-DPURR_ENABLE_MTP=$ModMtp"
    "-DPURR_ENABLE_FLASHER=$ModFlasher"
    "-DPURR_ENABLE_LORA=$ModLora"
    "-DPURR_ENABLE_MESH=$ModMesh"
)

function Invoke-Idf {
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    idf.py @args
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) { Write-PurrErr "idf.py exited $code" }
}

function Invoke-ArduinoPatches {
    $coresDir = Join-Path $CoreOsDir 'managed_components\espressif__arduino-esp32\cores\esp32'
    if (-not (Test-Path $coresDir)) { return }
    $adcH = Join-Path $coresDir 'esp32-hal-adc.h'
    $adcC = Join-Path $coresDir 'esp32-hal-adc.c'
    $i2cS = Join-Path $coresDir 'esp32-hal-i2c-slave.c'
    $hTxt = Get-Content $adcH -Raw
    if ($hTxt -match '\badc_continuous_data_t\b') {
        Write-PurrInfo 'arduino patch: adc_continuous_data_t rename'
        $hTxt -replace '\badc_continuous_data_t\b', 'arduino_adc_cont_data_t' | Set-Content $adcH -NoNewline -Encoding UTF8
        (Get-Content $adcC -Raw) -replace '\badc_continuous_data_t\b', 'arduino_adc_cont_data_t' | Set-Content $adcC -NoNewline -Encoding UTF8
    }
    $i2cTxt = Get-Content $i2cS -Raw
    if ($i2cTxt -notmatch 'i2c_ll_slave_init.*do') {
        Write-PurrInfo 'arduino patch: I2C slave LL stubs'
        $stub = "`n#include `"esp_idf_version.h`"`n#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)`n#define i2c_ll_slave_init(dev)           do {} while (0)`n#define i2c_ll_set_fifo_mode(dev, mode)  do {} while (0)`n#define i2c_ll_cal_bus_clk(a, b, c)      do {} while (0)`n#define i2c_ll_set_bus_timing(dev, cfg)  do {} while (0)`n#define i2c_ll_set_filter(dev, n)        do {} while (0)`n#endif"
        $i2cTxt -replace '(#include "esp_private/periph_ctrl\.h")', "`$1$stub" | Set-Content $i2cS -NoNewline -Encoding UTF8
    }
}

# ---- SPIFFS image builder ---------------------------------------------------
function Invoke-SpiffsBuild {  # builds SPIFFS filesystem image from target device config
    $spiffsGen = Join-Path $env:IDF_PATH 'components\spiffs\spiffsgen.py'
    if (-not (Test-Path $spiffsGen)) {
        Write-PurrWarn "spiffsgen.py not found -- skipping SPIFFS image"; return
    }

    $buildDir   = Join-Path $CoreOsDir 'build'
    $stagingDir = Join-Path $buildDir  'spiffs_staging'

    if (Test-Path $stagingDir) { Remove-Item $stagingDir -Recurse -Force }
    New-Item -ItemType Directory -Path "$stagingDir\system\kernel" -Force | Out-Null
    New-Item -ItemType Directory -Path "$stagingDir\system\logs"   -Force | Out-Null
    New-Item -ItemType Directory -Path "$stagingDir\apps"          -Force | Out-Null

    # Target device config -> /system/kernel/device.json on SPIFFS
    $deviceSrc = Join-Path $CoreOsDir "system\kernel\devices\$Target.json"
    if (-not (Test-Path $deviceSrc)) {
        Write-PurrWarn "No device config for '$Target' at $deviceSrc"
        $deviceSrc = Join-Path $CoreOsDir 'system\kernel\device.json'
    }
    Copy-Item $deviceSrc "$stagingDir\system\kernel\device.json" -Force
    Write-PurrInfo "SPIFFS: $Target.json -> /system/kernel/device.json"

    # Build SPIFFS image -- partition is 320KB = 327680 bytes
    $spiffsImg = Join-Path $buildDir 'spiffs.bin'
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    python $spiffsGen 327680 $stagingDir $spiffsImg
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) { Write-PurrErr "spiffsgen.py failed (exit $code)" }
    Write-PurrInfo "SPIFFS image: build\spiffs.bin (320 KB)"
}

$env:IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS = '1'

Write-PurrInfo "set-target $chip"
Invoke-Idf @cmakeFlags set-target $chip
Invoke-ArduinoPatches

Write-PurrInfo "build  TARGET=$Target  BT=$ModBt  MTP=$ModMtp  FLASHER=$ModFlasher  LORA=$ModLora  MINI=$MiniBuild"
Invoke-Idf @cmakeFlags build
Invoke-SpiffsBuild

if ($FlashPort) {
    $buildDir  = Join-Path $CoreOsDir 'build'
    $spiffsImg = Join-Path $buildDir  'spiffs.bin'
    Write-PurrInfo "flashing -> $FlashPort"

    $flashArgs = @(
        '--chip', $chip, '--port', $FlashPort, '-b', '460800',
        '--before', 'default_reset', '--after', 'hard_reset',
        'write_flash', '--flash_mode', 'dio', '--flash_size', 'detect', '--flash_freq', '40m',
        '0x1000',  "$buildDir\bootloader\bootloader.bin",
        '0x8000',  "$buildDir\partition_table\partition-table.bin",
        '0xe000',  "$buildDir\ota_data_initial.bin",
        '0x10000', "$buildDir\purr_os_core.bin"
    )
    if (Test-Path $spiffsImg) {
        $flashArgs += @('0x3b0000', $spiffsImg)
        Write-PurrInfo "including SPIFFS image at 0x3b0000"
    }

    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    python -m esptool @flashArgs
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) { Write-PurrErr "Flash failed (exit $code)" }
}

if ($MonPort) {
    Write-PurrInfo "monitor on $MonPort  (Ctrl+] to exit)"
    Invoke-Idf -p $MonPort monitor
}

Write-PurrInfo 'done.'
