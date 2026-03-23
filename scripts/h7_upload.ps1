# ==============================================================================
#  STM32H7 upload (restore) script (PowerShell)
#  Writes back: flash, option bytes from a previous dump
#  Source: dumps\h7\ directory
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
if (-not $SrcDir) { $SrcDir = Join-Path $ScriptDir "..\dumps\h7" }

if (-not (Test-Path $CLI)) {
    Write-Error "Error: $CLI not found. Build the project first."
    exit 1
}

$FlashBin = Join-Path $SrcDir "flash.bin"
if (-not (Test-Path $FlashBin)) {
    Write-Error "Error: $FlashBin not found. Run h7_dump.ps1 first."
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

Write-Host "=== STM32H7 Upload (Restore) ===" -ForegroundColor Cyan
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
#
# H7 option bytes: write each word to the corresponding PRG register.
# File layout (32 bytes): OPTSR | PRAR1 | SCAR1 | WPSN1 | BOOT | PRAR2 | SCAR2 | WPSN2
#   Bank 1: OPTSR_PRG  0x52002020   PRAR_PRG1 0x5200202C
#           SCAR_PRG1  0x52002034   WPSN_PRG1 0x5200203C
#           BOOT_PRG   0x52002044
#   Bank 2: PRAR_PRG2  0x5200212C   SCAR_PRG2 0x52002134
#           WPSN_PRG2  0x5200213C
$ObBin = Join-Path $SrcDir "optionbytes.bin"
Write-Host "[4/6] Writing option bytes..." -ForegroundColor Yellow
if (Test-Path $ObBin) {
    $ObData = [System.IO.File]::ReadAllBytes($ObBin)
    if ($ObData.Length -ne 32) {
        Write-Error "Error: optionbytes.bin should be 32 bytes, got $($ObData.Length)"
        exit 1
    }
    $ObPrgRegs = @(
        "0x52002020",  # OPTSR_PRG
        "0x5200202C",  # PRAR_PRG1
        "0x52002034",  # SCAR_PRG1
        "0x5200203C",  # WPSN_PRG1
        "0x52002044",  # BOOT_PRG
        "0x5200212C",  # PRAR_PRG2
        "0x52002134",  # SCAR_PRG2
        "0x5200213C"   # WPSN_PRG2
    )
    for ($i = 0; $i -lt 8; $i++) {
        $TmpBin = Join-Path $SrcDir "ob_tmp.bin"
        [System.IO.File]::WriteAllBytes($TmpBin, $ObData[($i*4)..($i*4+3)])
        Invoke-CLI "write", "ob", $TmpBin, $ObPrgRegs[$i]
        Remove-Item $TmpBin
    }
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
