# ==============================================================================
#  STM32F3 upload (restore) script (PowerShell)
#  Writes back: flash, option bytes from a previous dump
#  Source: dumps\f3\ directory
# ==============================================================================

param(
    [string]$Probe = "",
    [string]$Port = "",
    [int]$Speed = 0,
    [string]$SrcDir = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$CLI = Join-Path $ScriptDir "..\build\kt_swd_cli.exe"
if (-not $SrcDir) { $SrcDir = Join-Path $ScriptDir "..\dumps\f3" }

if (-not (Test-Path $CLI)) {
    Write-Error "Error: $CLI not found. Build the project first."
    exit 1
}

$FlashBin = Join-Path $SrcDir "flash.bin"
if (-not (Test-Path $FlashBin)) {
    Write-Error "Error: $FlashBin not found. Run f3_dump.ps1 first."
    exit 1
}

# Build probe options array
$ProbeOpts = @()
if ($Probe) { $ProbeOpts += "--probe", $Probe }
if ($Port)  { $ProbeOpts += "--port", $Port }
if ($Speed) { $ProbeOpts += "--speed", $Speed }

function Invoke-CLI {
    param([string[]]$CliArgs)
    & $CLI @CliArgs @ProbeOpts
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Command failed: kt_swd_cli $($CliArgs -join ' ')"
        exit 1
    }
}

Write-Host "=== STM32F3 Upload (Restore) ===" -ForegroundColor Cyan
Write-Host "Source: $SrcDir"
Write-Host ""

Write-Host "[1/6] Checking debug probe..." -ForegroundColor Yellow
Invoke-CLI "probe"
Write-Host ""

Write-Host "[2/6] Detecting chip..." -ForegroundColor Yellow
Invoke-CLI "detect"
Write-Host ""

Write-Host "[3/6] Checking RDP level..." -ForegroundColor Yellow
Invoke-CLI "rdp"
Write-Host ""

# Write option bytes before flash: if the chip is at RDP Level 1, the option
# byte write (with RDP=0xAA) triggers OBL_LAUNCH + reset, which causes the
# hardware to perform RDP regression (mass erase + Level 0 restore).
# Without this, flash writes fail with WRPERR while RDP Level 1 is active.
$ObBin = Join-Path $SrcDir "optionbytes.bin"
Write-Host "[4/6] Writing option bytes..." -ForegroundColor Yellow
if (Test-Path $ObBin) {
    Invoke-CLI "write", "ob", $ObBin, "0x1FFFF800"
} else {
    Write-Host "Skipped: optionbytes.bin not found" -ForegroundColor DarkYellow
}
Write-Host ""

Write-Host "[5/6] Erasing flash..." -ForegroundColor Yellow
Invoke-CLI "erase"
Write-Host ""

Write-Host "[6/6] Writing flash..." -ForegroundColor Yellow
Invoke-CLI "write", "flash", $FlashBin, "0x08000000"
Write-Host ""

Write-Host "=== Upload complete ===" -ForegroundColor Green
