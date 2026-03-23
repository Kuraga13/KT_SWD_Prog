# ==============================================================================
#  STM32G0 flash read/write test script (PowerShell)
#  Writes dummy data to flash and option bytes, reads back and verifies.
#  Uses a temp directory for test files, cleans up on exit.
# ==============================================================================

param(
    [string]$Probe = "",
    [string]$Port = "",
    [int]$Speed = 0
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$CLI = Join-Path $ScriptDir "..\build\kt_swd_cli.exe"

if (-not (Test-Path $CLI)) {
    Write-Error "Error: $CLI not found. Build the project first."
    exit 1
}

$TempDir = Join-Path ([System.IO.Path]::GetTempPath()) "g0_test_$(Get-Random)"
New-Item -ItemType Directory -Force -Path $TempDir | Out-Null

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

function Compare-BinaryFiles {
    param([string]$FileA, [string]$FileB)
    $bytesA = [System.IO.File]::ReadAllBytes($FileA)
    $bytesB = [System.IO.File]::ReadAllBytes($FileB)
    if ($bytesA.Length -ne $bytesB.Length) { return $false }
    for ($i = 0; $i -lt $bytesA.Length; $i++) {
        if ($bytesA[$i] -ne $bytesB[$i]) { return $false }
    }
    return $true
}

$TestPassed = $true

try {
    Write-Host "=== STM32G0 Flash Read/Write Test ===" -ForegroundColor Cyan
    Write-Host ""

    # ------------------------------------------------------------------
    #  Setup: probe, detect, RDP check
    # ------------------------------------------------------------------
    Write-Host "[1/11] Checking debug probe..." -ForegroundColor Yellow
    Invoke-CLI "probe"
    Write-Host ""

    Write-Host "[2/11] Detecting chip..." -ForegroundColor Yellow
    Invoke-CLI "detect"
    Write-Host ""

    Write-Host "[3/11] Checking RDP level..." -ForegroundColor Yellow
    Invoke-CLI "rdp"
    Write-Host ""

    # ------------------------------------------------------------------
    #  Read flash size and page size
    # ------------------------------------------------------------------
    $FlashSizeBin = Join-Path $TempDir "flashsize.bin"
    Write-Host "[4/11] Reading flash size register..." -ForegroundColor Yellow
    Invoke-CLI "read", "flash", "0x1FFF75E0", "0x02", "-o", $FlashSizeBin
    $FlashSizeBytes = [System.IO.File]::ReadAllBytes($FlashSizeBin)
    $FlashSizeKB = [BitConverter]::ToUInt16($FlashSizeBytes, 0)
    Remove-Item $FlashSizeBin

    # G0 page size is always 2 KB
    $PageSize = 2048
    Write-Host "Flash: $FlashSizeKB KB, page size: $PageSize bytes" -ForegroundColor Green
    Write-Host ""

    # ------------------------------------------------------------------
    #  Generate test pattern (one page of flash)
    # ------------------------------------------------------------------
    $TestFlashBin = Join-Path $TempDir "test_flash.bin"
    $ReadFlashBin = Join-Path $TempDir "read_flash.bin"

    # Pattern: repeating 0x00..0xFF across the page
    $Pattern = [byte[]]::new($PageSize)
    for ($i = 0; $i -lt $PageSize; $i++) {
        $Pattern[$i] = ($i % 256)
    }
    [System.IO.File]::WriteAllBytes($TestFlashBin, $Pattern)

    # ------------------------------------------------------------------
    #  Flash test: erase -> write -> read -> compare (slow path, then fast)
    # ------------------------------------------------------------------
    $TestAddr = "0x08000000"
    $TestSizeHex = "0x{0:X}" -f $PageSize

    # --- Slow path (double-word SWD writes, no stub) ---
    Write-Host "[5/11] Erasing flash..." -ForegroundColor Yellow
    Invoke-CLI "erase"
    Write-Host ""

    Write-Host "[6/11] Writing test pattern - SLOW path ($PageSize bytes at $TestAddr)..." -ForegroundColor Yellow
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    Invoke-CLI "write", "flash", $TestFlashBin, $TestAddr, "--no-stub"
    $sw.Stop()
    $SlowMs = $sw.ElapsedMilliseconds
    Write-Host "  Slow write time: $SlowMs ms" -ForegroundColor Cyan
    Write-Host ""

    Write-Host "[7/11] Reading back flash..." -ForegroundColor Yellow
    Invoke-CLI "read", "flash", $TestAddr, $TestSizeHex, "-o", $ReadFlashBin
    Write-Host ""

    if (Compare-BinaryFiles $TestFlashBin $ReadFlashBin) {
        Write-Host "  FLASH TEST (SLOW): PASSED" -ForegroundColor Green
    } else {
        Write-Host "  FLASH TEST (SLOW): FAILED (data mismatch)" -ForegroundColor Red
        $TestPassed = $false
    }
    Write-Host ""

    # --- Fast path (SRAM stub) ---
    Write-Host "[8/11] Erasing flash..." -ForegroundColor Yellow
    Invoke-CLI "erase"
    Write-Host ""

    Write-Host "[9/11] Writing test pattern - FAST path ($PageSize bytes at $TestAddr)..." -ForegroundColor Yellow
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    Invoke-CLI "write", "flash", $TestFlashBin, $TestAddr
    $sw.Stop()
    $FastMs = $sw.ElapsedMilliseconds
    Write-Host "  Fast write time: $FastMs ms" -ForegroundColor Cyan
    if ($FastMs -gt 0 -and $SlowMs -gt 0) {
        $Speedup = [math]::Round($SlowMs / [math]::Max($FastMs, 1), 1)
        Write-Host "  Speedup: ${Speedup}x" -ForegroundColor Cyan
    }
    Write-Host ""

    Write-Host "[10/11] Reading back flash..." -ForegroundColor Yellow
    Invoke-CLI "read", "flash", $TestAddr, $TestSizeHex, "-o", $ReadFlashBin
    Write-Host ""

    if (Compare-BinaryFiles $TestFlashBin $ReadFlashBin) {
        Write-Host "  FLASH TEST (FAST): PASSED" -ForegroundColor Green
    } else {
        Write-Host "  FLASH TEST (FAST): FAILED (data mismatch)" -ForegroundColor Red
        $TestPassed = $false
    }
    Write-Host ""

    # ------------------------------------------------------------------
    #  Option bytes test: read current -> modify -> write -> verify -> restore
    #
    #  FLASH_OPTR (4 bytes at 0x40022020):
    #    Bits 7:0:   RDP (must stay 0xAA = Level 0!)
    #    Bits 10:8:  BOR_EN, BORR_LEV
    #    Bit 15:     IWDG_SW
    #    Bit 17:     IWDG_STDBY  -- we toggle this as test marker
    #    Bits 27:16: various config bits
    # ------------------------------------------------------------------
    $OrigObBin = Join-Path $TempDir "orig_ob.bin"
    $TestObBin = Join-Path $TempDir "test_ob.bin"
    $ReadObBin = Join-Path $TempDir "read_ob.bin"

    Write-Host "[11/11] Writing and verifying option bytes..." -ForegroundColor Yellow

    # Read current FLASH_OPTR
    Invoke-CLI "read", "ob", "0x40022020", "0x04", "-o", $OrigObBin
    $OrigOb = [System.IO.File]::ReadAllBytes($OrigObBin)
    Write-Host "  Current OPTR: 0x$([BitConverter]::ToString($OrigOb[3..0]) -replace '-','')" -ForegroundColor Cyan

    # Toggle IWDG_STDBY (bit 17 = byte[2] bit 1) as test marker
    $TestOb = [byte[]]$OrigOb.Clone()
    $TestOb[2] = $TestOb[2] -bxor 0x02
    [System.IO.File]::WriteAllBytes($TestObBin, $TestOb)
    Write-Host "  Test   OPTR: 0x$([BitConverter]::ToString($TestOb[3..0]) -replace '-','')" -ForegroundColor Cyan

    # Write test value and read back
    Invoke-CLI "write", "ob", $TestObBin, "0x40022020"
    Invoke-CLI "read", "ob", "0x40022020", "0x04", "-o", $ReadObBin
    $ReadOb = [System.IO.File]::ReadAllBytes($ReadObBin)
    Write-Host "  Read   OPTR: 0x$([BitConverter]::ToString($ReadOb[3..0]) -replace '-','')" -ForegroundColor Cyan

    if (Compare-BinaryFiles $TestObBin $ReadObBin) {
        Write-Host "  OPTION BYTES TEST: PASSED" -ForegroundColor Green
    } else {
        Write-Host "  OPTION BYTES TEST: FAILED (data mismatch)" -ForegroundColor Red
        $TestPassed = $false
    }

    # Restore original option bytes
    Write-Host "  Restoring original option bytes..." -ForegroundColor Yellow
    Invoke-CLI "write", "ob", $OrigObBin, "0x40022020"
    Write-Host ""

    # ------------------------------------------------------------------
    #  Result
    # ------------------------------------------------------------------
    if ($TestPassed) {
        Write-Host "=== ALL TESTS PASSED ===" -ForegroundColor Green
    } else {
        Write-Host "=== SOME TESTS FAILED ===" -ForegroundColor Red
        exit 1
    }
}
finally {
    # Clean up temp directory
    if (Test-Path $TempDir) {
        Remove-Item -Recurse -Force $TempDir
    }
}
