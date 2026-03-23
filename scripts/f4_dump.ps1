# ==============================================================================
#  STM32F4 full dump script (PowerShell)
#  Reads: flash (auto-detected size), option bytes, OTP, RDP level
#  Output: dumps\f4\ directory
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
if (-not $OutDir) { $OutDir = Join-Path $ScriptDir "..\dumps\f4" }

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

Write-Host "=== STM32F4 Full Dump ===" -ForegroundColor Cyan
Write-Host ""

Write-Host "[1/7] Checking debug probe..." -ForegroundColor Yellow
Invoke-CLI "probe"
Write-Host ""

Write-Host "[2/7] Detecting chip..." -ForegroundColor Yellow
Invoke-CLI "detect"
Write-Host ""

Write-Host "[3/7] Checking RDP level..." -ForegroundColor Yellow
Invoke-CLI "rdp"
Write-Host ""

# Read flash size register (0x1FFF7A22, 16-bit value in KB)
$FlashSizeBin = Join-Path $OutDir "flashsize.bin"
Write-Host "[4/7] Reading flash size register..." -ForegroundColor Yellow
Invoke-CLI "read", "flash", "0x1FFF7A22", "0x02", "-o", $FlashSizeBin
$FlashSizeBytes = [System.IO.File]::ReadAllBytes($FlashSizeBin)
$FlashSizeKB = [BitConverter]::ToUInt16($FlashSizeBytes, 0)
$FlashSize = $FlashSizeKB * 1024
$FlashSizeHex = "0x{0:X}" -f $FlashSize
Write-Host "Flash size: $FlashSizeKB KB ($FlashSizeHex bytes)" -ForegroundColor Green
Remove-Item $FlashSizeBin
Write-Host ""

Write-Host "[5/7] Reading option bytes (FLASH_OPTCR, 8 bytes at 0x40023C14)..." -ForegroundColor Yellow
Invoke-CLI "read", "ob", "0x40023C14", "0x08", "-o", (Join-Path $OutDir "optionbytes.bin")
Write-Host ""

Write-Host "[6/7] Reading OTP (528 bytes at 0x1FFF7800)..." -ForegroundColor Yellow
Invoke-CLI "read", "otp", "0x1FFF7800", "0x210", "-o", (Join-Path $OutDir "otp.bin")
Write-Host ""

Write-Host "[7/7] Reading flash ($FlashSizeKB KB at 0x08000000)..." -ForegroundColor Yellow
Invoke-CLI "read", "flash", "0x08000000", $FlashSizeHex, "-o", (Join-Path $OutDir "flash.bin")
Write-Host ""

Write-Host "=== Dump complete ===" -ForegroundColor Green
Write-Host "Output directory: $OutDir"
Get-ChildItem $OutDir | Format-Table Name, Length -AutoSize
