#Requires -Version 5.1
# PURR OS Simulator — build + run
#
# Usage:
#   .\build.ps1                           # blackberry shell, build + run
#   .\build.ps1 -Shell blackberry         # BlackberryUI (320x240)
#   .\build.ps1 -Shell explorer           # Explorer shell (320x240)
#   .\build.ps1 -Shell classic            # standalone mw_user.c
#   .\build.ps1 -Shell blackberry -Build  # build only, don't launch
#   .\build.ps1 -Clean                    # wipe build dir first

param(
    [ValidateSet('blackberry','explorer','classic')]
    [string]$Shell  = 'blackberry',
    [switch]$Build,      # build only (no auto-launch)
    [switch]$Run,        # run only (skip build)
    [switch]$Clean
)

# Default: build + run
if (-not $Build -and -not $Run) { $Build = $true; $Run = $true }

$SimDir   = $PSScriptRoot
$BuildDir = Join-Path $SimDir "build_$Shell"

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "[sim] cleaning $BuildDir" -ForegroundColor DarkGray
    Remove-Item -Recurse -Force $BuildDir
}

if ($Build) {
    New-Item -ItemType Directory -Force $BuildDir | Out-Null

    # Detect C++ compiler
    $generator = $null
    $mingw = Get-Command mingw32-make -ErrorAction SilentlyContinue
    if ($mingw) {
        $generator = "MinGW Makefiles"
        Write-Host "[sim] using MinGW g++" -ForegroundColor DarkGray
    } elseif (Get-Command cl -ErrorAction SilentlyContinue) {
        Write-Host "[sim] using MSVC" -ForegroundColor DarkGray
    } else {
        Write-Error "[sim] No C++ compiler found. Install MinGW (via scoop/choco) or Visual Studio Build Tools."
    }

    Write-Host "[sim] configuring  shell=$Shell  $BuildDir" -ForegroundColor Cyan
    $cfg = @("-S", $SimDir, "-B", $BuildDir, "-DPURR_SHELL=$Shell", "-DCMAKE_BUILD_TYPE=Release")
    if ($generator) { $cfg += @("-G", $generator) }
    & cmake @cfg
    if ($LASTEXITCODE -ne 0) { Write-Error "[sim] cmake configure failed" }

    Write-Host "[sim] building..." -ForegroundColor Cyan
    & cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) { Write-Error "[sim] cmake build failed" }
    Write-Host "[sim] build OK" -ForegroundColor Green
}

if ($Run) {
    $exe = Get-ChildItem -Recurse $BuildDir -Filter "purr_sim.exe" -ErrorAction SilentlyContinue |
           Select-Object -First 1 -ExpandProperty FullName
    if (-not $exe) { Write-Error "[sim] purr_sim.exe not found — run with -Build first" }
    Write-Host "[sim] launching $exe" -ForegroundColor Green
    & $exe
}
