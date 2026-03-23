# ==============================================================================
#  STM32G0 upload (restore) script (PowerShell)
#  Writes back: flash, option bytes from a previous dump
#  Source: dumps\g0\ directory
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
if (-not $SrcDir) { $SrcDir = Join-Path $ScriptDir "..\dumps\g0" }

if (-not (Test-Path $CLI)) {
    Write-Error "Error: $CLI not found. Build the project first."
    exit 1
}

$FlashBin = Join-Path $SrcDir "flash.bin"
if (-not (Test-Path $FlashBin)) {
    Write-Error "Error: $FlashBin not found. Run g0_dump.ps1 first."
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

Write-Host "=== STM32G0 Upload (Restore) ===" -ForegroundColor Cyan
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
# byte write (with RDP=Level 0) triggers a system reset + RDP regression
# (hardware mass erase). Without this, flash writes fail with WRPERR.
$ObBin = Join-Path $SrcDir "optionbytes.bin"
Write-Host "[4/6] Writing option bytes..." -ForegroundColor Yellow
if (Test-Path $ObBin) {
    Invoke-CLI "write", "ob", $ObBin, "0x40022020"
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
