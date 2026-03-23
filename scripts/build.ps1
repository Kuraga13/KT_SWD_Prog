# ==============================================================================
#  Build script (PowerShell)
#  Configures and builds the project using CMake + Ninja.
#  Run from anywhere — automatically finds the project root.
#
#  Usage:
#    .\scripts\build.ps1                  # default build (ST-Link only)
#    .\scripts\build.ps1 -Clean           # clean rebuild from scratch
#    .\scripts\build.ps1 -BMP -JLink      # enable additional transports
#    .\scripts\build.ps1 -NoCLI           # library only, no CLI executable
#    .\scripts\build.ps1 -Clean -BMP      # clean rebuild with BMP transport
# ==============================================================================

param(
    [switch]$Clean,
    [switch]$BMP,
    [switch]$JLink,
    [switch]$NoCLI
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $ProjectRoot "build"

# Clean build: remove build directory and reconfigure
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Removing build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

# Configure if needed (first build or after clean)
if (-not (Test-Path (Join-Path $BuildDir "build.ninja"))) {
    Write-Host "Configuring..." -ForegroundColor Cyan

    $CmakeArgs = @("-B", $BuildDir, "-S", $ProjectRoot)

    if ($BMP)   { $CmakeArgs += "-DKT_TRANSPORT_BMP=ON" }
    if ($JLink) { $CmakeArgs += "-DKT_TRANSPORT_JLINK=ON" }
    if ($NoCLI) { $CmakeArgs += "-DKT_BUILD_CLI=OFF" }

    & cmake @CmakeArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configure failed"
        exit 1
    }
    Write-Host ""
}

# Build
Write-Host "Building..." -ForegroundColor Cyan
& cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed"
    exit 1
}

Write-Host ""
Write-Host "Build complete." -ForegroundColor Green
