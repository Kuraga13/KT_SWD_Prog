# ==============================================================================
#  STM32H7 flash read/write test script (PowerShell)
#  Writes dummy data to flash and option bytes, reads back and verifies.
#  Tests both slow (SWD word-by-word) and fast (SRAM stub) write paths.
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

$TempDir = Join-Path ([System.IO.Path]::GetTempPath()) "h7_test_$(Get-Random)"
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
    Write-Host "=== STM32H7 Flash Read/Write Test ===" -ForegroundColor Cyan
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
    #  Read flash size (informational)
    # ------------------------------------------------------------------
    $FlashSizeBin = Join-Path $TempDir "flashsize.bin"
    Write-Host "[4/11] Reading flash size register..." -ForegroundColor Yellow
    Invoke-CLI "read", "flash", "0x1FF1E880", "0x02", "-o", $FlashSizeBin
    $FlashSizeBytes = [System.IO.File]::ReadAllBytes($FlashSizeBin)
    $FlashSizeKB = [BitConverter]::ToUInt16($FlashSizeBytes, 0)
    Remove-Item $FlashSizeBin
    Write-Host "Flash: $FlashSizeKB KB" -ForegroundColor Green
    Write-Host ""

    # ------------------------------------------------------------------
    #  Generate test pattern (32 KB — aligned to 32-byte flash word)
    #  H7 programs 256 bits (32 bytes) at a time, sectors are 128 KB.
    # ------------------------------------------------------------------
    $TestSize = 32768
    $TestFlashBin = Join-Path $TempDir "test_flash.bin"
    $ReadFlashBin = Join-Path $TempDir "read_flash.bin"

    # Pattern: repeating 0x00..0xFF across the block
    $Pattern = [byte[]]::new($TestSize)
    for ($i = 0; $i -lt $TestSize; $i++) {
        $Pattern[$i] = ($i % 256)
    }
    [System.IO.File]::WriteAllBytes($TestFlashBin, $Pattern)

    # ------------------------------------------------------------------
    #  Flash test: erase -> write -> read -> compare (slow path, then fast)
    # ------------------------------------------------------------------
    $TestAddr = "0x08000000"
    $TestSizeHex = "0x{0:X}" -f $TestSize

    # --- Slow path (SWD word-by-word writes, no stub) ---
    Write-Host "[5/11] Erasing flash..." -ForegroundColor Yellow
    Invoke-CLI "erase"
    Write-Host ""

    Write-Host "[6/11] Writing test pattern - SLOW path ($TestSize bytes at $TestAddr)..." -ForegroundColor Yellow
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

    Write-Host "[9/11] Writing test pattern - FAST path ($TestSize bytes at $TestAddr)..." -ForegroundColor Yellow
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
    #  H7 option bytes use two registers:
    #    OPTSR_CUR (0x5200201C) — current values (read)
    #    OPTSR_PRG (0x52002020) — programming register (write)
    #
    #  OPTSR layout:
    #    Bit 0:      OPT_BUSY (read-only in CUR, hardware-managed)
    #    Bits [3:2]:  BOR_LEV — we toggle bit 2 as test marker
    #    Bit 4:       IWDG1_SW
    #    Bits [15:8]: RDP (must stay 0xAA = Level 0!)
    # ------------------------------------------------------------------
    $OrigObBin = Join-Path $TempDir "orig_ob.bin"
    $TestObBin = Join-Path $TempDir "test_ob.bin"
    $ReadObBin = Join-Path $TempDir "read_ob.bin"

    Write-Host "[11/11] Writing and verifying option bytes..." -ForegroundColor Yellow

    # Read current OPTSR_CUR
    Invoke-CLI "read", "ob", "0x5200201C", "0x04", "-o", $OrigObBin
    $OrigOb = [System.IO.File]::ReadAllBytes($OrigObBin)
    Write-Host "  Current OPTSR: 0x$([BitConverter]::ToString($OrigOb[3..0]) -replace '-','')" -ForegroundColor Cyan

    # Toggle BOR_LEV[0] (bit 2 of byte 0) as test marker
    $TestOb = [byte[]]$OrigOb.Clone()
    $TestOb[0] = $TestOb[0] -bxor 0x04
    # Strip OPT_BUSY (bit 0) — not valid for OPTSR_PRG
    $TestOb[0] = $TestOb[0] -band 0xFE
    [System.IO.File]::WriteAllBytes($TestObBin, $TestOb)
    Write-Host "  Test   OPTSR: 0x$([BitConverter]::ToString($TestOb[3..0]) -replace '-','')" -ForegroundColor Cyan

    # Write to OPTSR_PRG and read back from OPTSR_CUR
    Invoke-CLI "write", "ob", $TestObBin, "0x52002020"
    Invoke-CLI "read", "ob", "0x5200201C", "0x04", "-o", $ReadObBin
    $ReadOb = [System.IO.File]::ReadAllBytes($ReadObBin)
    Write-Host "  Read   OPTSR: 0x$([BitConverter]::ToString($ReadOb[3..0]) -replace '-','')" -ForegroundColor Cyan

    # Compare (mask out OPT_BUSY bit 0 — hardware always manages it)
    $TestMatch = $true
    for ($i = 0; $i -lt $TestOb.Length; $i++) {
        $mask = if ($i -eq 0) { 0xFE } else { 0xFF }
        if (($TestOb[$i] -band $mask) -ne ($ReadOb[$i] -band $mask)) {
            $TestMatch = $false
            break
        }
    }

    if ($TestMatch) {
        Write-Host "  OPTION BYTES TEST: PASSED" -ForegroundColor Green
    } else {
        Write-Host "  OPTION BYTES TEST: FAILED (data mismatch)" -ForegroundColor Red
        $TestPassed = $false
    }

    # Restore original option bytes (strip OPT_BUSY before writing back)
    Write-Host "  Restoring original option bytes..." -ForegroundColor Yellow
    $RestoreOb = [byte[]]$OrigOb.Clone()
    $RestoreOb[0] = $RestoreOb[0] -band 0xFE
    $RestoreObBin = Join-Path $TempDir "restore_ob.bin"
    [System.IO.File]::WriteAllBytes($RestoreObBin, $RestoreOb)
    Invoke-CLI "write", "ob", $RestoreObBin, "0x52002020"
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
