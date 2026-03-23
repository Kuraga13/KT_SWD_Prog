# ==============================================================================
#  STM32F3 full dump script (PowerShell)
#  Reads: flash (auto-detected size), option bytes (16 B), RDP level
#  Output: dumps\f3\ directory
# ==============================================================================

param(
    [string]$Probe = "",
    [string]$Port = "",
    [int]$Speed = 0,
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$CLI = Join-Path $ScriptDir "..\build\kt_swd_cli.exe"
if (-not $OutDir) { $OutDir = Join-Path $ScriptDir "..\dumps\f3" }

if (-not (Test-Path $CLI)) {
    Write-Error "Error: $CLI not found. Build the project first."
    exit 1
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

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

Write-Host "=== STM32F3 Full Dump ===" -ForegroundColor Cyan
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

# Read flash size register (0x1FFFF7CC, 16-bit value in KB)
$FlashSizeBin = Join-Path $OutDir "flashsize.bin"
Write-Host "[4/6] Reading flash size register..." -ForegroundColor Yellow
Invoke-CLI "read", "flash", "0x1FFFF7CC", "0x02", "-o", $FlashSizeBin
$FlashSizeBytes = [System.IO.File]::ReadAllBytes($FlashSizeBin)
$FlashSizeKB = [BitConverter]::ToUInt16($FlashSizeBytes, 0)
$FlashSize = $FlashSizeKB * 1024
$FlashSizeHex = "0x{0:X}" -f $FlashSize
Write-Host "Flash size: $FlashSizeKB KB ($FlashSizeHex bytes)" -ForegroundColor Green
Remove-Item $FlashSizeBin
Write-Host ""

Write-Host "[5/6] Reading option bytes (16 bytes at 0x1FFFF800)..." -ForegroundColor Yellow
Invoke-CLI "read", "ob", "0x1FFFF800", "0x10", "-o", (Join-Path $OutDir "optionbytes.bin")
Write-Host ""

Write-Host "[6/6] Reading flash ($FlashSizeKB KB at 0x08000000)..." -ForegroundColor Yellow
Invoke-CLI "read", "flash", "0x08000000", $FlashSizeHex, "-o", (Join-Path $OutDir "flash.bin")
Write-Host ""

Write-Host "=== Dump complete ===" -ForegroundColor Green
Write-Host "Output directory: $OutDir"
Get-ChildItem $OutDir | Format-Table Name, Length -AutoSize
