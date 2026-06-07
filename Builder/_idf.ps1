# Dot-source this file to set up the IDF environment.
# Usage (in any build script):  . (Join-Path $PSScriptRoot '_idf.ps1')

if (-not $env:IDF_PATH) {
    $candidates = @(
        $env:IDF_EXPORT_PATH,
        'C:\esp\v5.3.5\esp-idf\export.ps1',
        'C:\esp\esp-idf\export.ps1',
        "$env:USERPROFILE\esp\esp-idf\export.ps1",
        "$env:USERPROFILE\.espressif\frameworks\esp-idf\export.ps1"
    ) | Where-Object { $_ -and (Test-Path $_) }

    if ($candidates.Count -eq 0) {
        Write-Error @"
[sdk] ESP-IDF not found.
      Source it manually:   . "C:\esp\v5.3.5\esp-idf\export.ps1"
      Or set:               `$env:IDF_EXPORT_PATH = "path\to\export.ps1"
"@
        exit 1
    }
    Write-Host "[sdk] Sourcing IDF: $($candidates[0])" -ForegroundColor DarkGray
    . $candidates[0]
} else {
    Write-Host "[sdk] IDF active: $env:IDF_PATH" -ForegroundColor DarkGray
}

$env:ARDUINO_SKIP_IDF_VERSION_CHECK = '1'
# Prevent IDF component manager from re-downloading managed_components during build,
# which would overwrite the arduino-esp32 patches applied between set-target and build.
$env:IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS = '0'

# Locate the IDF Python venv — the one that actually has esptool + idf_component_manager.
# System Python (e.g. 3.14) won't have these even if it's first on PATH.
if (-not $env:IDF_PYTHON) {
    $found = $null
    # 1. Try PATH: find first python.exe that can import esptool
    $py = (Get-Command python -ErrorAction SilentlyContinue).Source
    if ($py) {
        & $py -c "import esptool" 2>$null; if ($?) { $found = $py }
    }
    # 2. Search IDF tools directory for a venv python
    if (-not $found -and $env:IDF_TOOLS_PATH) {
        $venvPy = Get-ChildItem "$env:IDF_TOOLS_PATH\python\*\venv\Scripts\python.exe" -ErrorAction SilentlyContinue |
                  Sort-Object FullName | Select-Object -Last 1
        if ($venvPy) { $found = $venvPy.FullName }
    }
    # 3. Hard-coded Espressif installer default path
    if (-not $found) {
        $default = 'C:\Espressif\tools\python\v5.3.5\venv\Scripts\python.exe'
        if (Test-Path $default) { $found = $default }
    }
    if ($found) {
        $env:IDF_PYTHON = $found
        Write-Host "[sdk] IDF Python: $found" -ForegroundColor DarkGray
    } else {
        Write-Warning "[sdk] Could not locate IDF Python with esptool - flash may fail"
        $env:IDF_PYTHON = 'python'
    }
}
