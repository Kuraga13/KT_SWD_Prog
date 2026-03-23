# ==============================================================================
#  STM32H7 full dump script (PowerShell)
#  Reads: flash (auto-detected size), option bytes (32 B), OTP (1 KB), RDP level
#  Output: dumps\h7\ directory
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
if (-not $OutDir) { $OutDir = Join-Path $ScriptDir "..\dumps\h7" }

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

Write-Host "=== STM32H7 Full Dump ===" -ForegroundColor Cyan
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

# Read flash size register (0x1FF1E880, 16-bit value in KB)
$FlashSizeBin = Join-Path $OutDir "flashsize.bin"
Write-Host "[4/7] Reading flash size register..." -ForegroundColor Yellow
Invoke-CLI "read", "flash", "0x1FF1E880", "0x02", "-o", $FlashSizeBin
$FlashSizeBytes = [System.IO.File]::ReadAllBytes($FlashSizeBin)
$FlashSizeKB = [BitConverter]::ToUInt16($FlashSizeBytes, 0)
$FlashSize = $FlashSizeKB * 1024
$FlashSizeHex = "0x{0:X}" -f $FlashSize
Write-Host "Flash size: $FlashSizeKB KB ($FlashSizeHex bytes)" -ForegroundColor Green
Remove-Item $FlashSizeBin
Write-Host ""

# H7 option bytes: CUR (read) and PRG (write) registers alternate.
# Dump the 8 CUR registers, concatenate into one 32-byte file.
#   Bank 1: OPTSR_CUR  0x5200201C   PRAR_CUR1 0x52002028
#           SCAR_CUR1  0x52002030   WPSN_CUR1 0x52002038
#           BOOT_CUR   0x52002040
#   Bank 2: PRAR_CUR2  0x52002128   SCAR_CUR2 0x52002130
#           WPSN_CUR2  0x52002138
Write-Host "[5/7] Reading option bytes (8 CUR registers)..." -ForegroundColor Yellow
$ObBin = Join-Path $OutDir "optionbytes.bin"
$ObRegs = @(
    "0x5200201C",  # OPTSR_CUR
    "0x52002028",  # PRAR_CUR1
    "0x52002030",  # SCAR_CUR1
    "0x52002038",  # WPSN_CUR1
    "0x52002040",  # BOOT_CUR
    "0x52002128",  # PRAR_CUR2
    "0x52002130",  # SCAR_CUR2
    "0x52002138"   # WPSN_CUR2
)
$ObData = [byte[]]::new(0)
foreach ($reg in $ObRegs) {
    $TmpBin = Join-Path $OutDir "ob_tmp.bin"
    Invoke-CLI "read", "ob", $reg, "0x04", "-o", $TmpBin
    $ObData += [System.IO.File]::ReadAllBytes($TmpBin)
    Remove-Item $TmpBin
}
[System.IO.File]::WriteAllBytes($ObBin, $ObData)
Write-Host ""

Write-Host "[6/7] Reading OTP (1 KB at 0x08FFF000)..." -ForegroundColor Yellow
Invoke-CLI "read", "otp", "0x08FFF000", "0x400", "-o", (Join-Path $OutDir "otp.bin")
Write-Host ""

Write-Host "[7/7] Reading flash ($FlashSizeKB KB at 0x08000000)..." -ForegroundColor Yellow
Invoke-CLI "read", "flash", "0x08000000", $FlashSizeHex, "-o", (Join-Path $OutDir "flash.bin")
Write-Host ""

Write-Host "=== Dump complete ===" -ForegroundColor Green
Write-Host "Output directory: $OutDir"
Get-ChildItem $OutDir | Format-Table Name, Length -AutoSize
