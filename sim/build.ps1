# build.ps1 — Build and run the PURR OS MiniWin simulator (Windows, 320x240)
# Requires: cmake + a C compiler (MSVC via Visual Studio, or MinGW via scoop/choco)
#
# Usage:
#   .\build.ps1           — build and run
#   .\build.ps1 -Build    — build only
#   .\build.ps1 -Run      — run last build

param(
    [switch]$Build,
    [switch]$Run,
    [switch]$Clean
)

$SimDir   = $PSScriptRoot
$BuildDir = Join-Path $SimDir "build"

# Default: build + run
if (-not $Build -and -not $Run) { $Build = $true; $Run = $true }

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "[sim] Cleaning build dir..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

if ($Build) {
    if (-not (Test-Path $BuildDir)) { New-Item -ItemType Directory $BuildDir | Out-Null }

    Write-Host "[sim] Configuring..." -ForegroundColor Cyan
    Push-Location $BuildDir

    # Try MinGW first (lighter, no VS install needed), fall back to default generator
    $mingw = Get-Command mingw32-make -ErrorAction SilentlyContinue
    if ($mingw) {
        cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    } else {
        cmake .. -DCMAKE_BUILD_TYPE=Release
    }

    if ($LASTEXITCODE -ne 0) { Pop-Location; exit 1 }

    Write-Host "[sim] Building..." -ForegroundColor Cyan
    cmake --build . --config Release

    Pop-Location

    if ($LASTEXITCODE -ne 0) {
        Write-Host "[sim] Build FAILED" -ForegroundColor Red
        exit 1
    }
    Write-Host "[sim] Build OK" -ForegroundColor Green
}

if ($Run) {
    $exe = Get-ChildItem $BuildDir -Recurse -Filter "purr_sim.exe" | Select-Object -First 1
    if (-not $exe) {
        Write-Host "[sim] purr_sim.exe not found — run with -Build first" -ForegroundColor Red
        exit 1
    }
    Write-Host "[sim] Launching $($exe.FullName)" -ForegroundColor Green
    & $exe.FullName
}
