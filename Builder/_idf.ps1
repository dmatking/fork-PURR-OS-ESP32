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
