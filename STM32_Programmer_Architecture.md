# KuragaSTM32Tool — Architecture

## Goal

Build a universal programmer tool that supports reading and flashing any
ARM Cortex-M target (STM32 + NXP Kinetis KE) through any SWD debug probe,
without duplicating code.

## The Problem

Different chip families have different flash controllers — different register
addresses, unlock keys, page sizes, programming sequences, and even
different programming models (register-bit vs command-based).

Different debug probes (ST-Link, Black Magic Probe, J-Link, ...) have
different communication protocols.

A naive approach requires **transports x families** implementations.

## The Solution — Composition

Separate three independent concerns:

1. **Transport** — how to talk to the probe (raw memory read/write over SWD)
2. **TargetDriver** — target-specific logic for one chip family
3. **Auto-detection** — identify the connected chip and select the right driver

Compose them at runtime:

```
3 transports + 19 target drivers = 22 classes
(vs. 3 x 19 = 57 classes with inheritance)
```

Adding a new probe = one Transport class, works with all targets.
Adding a new chip family = one TargetDriver class, works with all probes.

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    Stm32Programmer                       │
│             (composes Transport + TargetDriver)           │
│                                                          │
│  connect() ──► Transport::connect()                      │
│              + TargetDriver::onConnect()                  │
│                                                          │
│  Reads  ──────► TargetDriver::readMemory() ──► Transport │
│  Writes ──────► TargetDriver::writeFlash() ──► Transport │
│                                                          │
│  disconnect() ► TargetDriver::onDisconnect()             │
│              + Transport::disconnect()                   │
└──────────────────────────────────────────────────────────┘

Transport (one per probe)           TargetDriver (one per chip family)
┌──────────────────────┐            ┌────────────────────────────────┐
│ ● connect()          │            │ ● onConnect()    [hook]        │
│ ● disconnect()       │            │ ● onDisconnect() [hook]        │
│ ● readMemory()       │            │ ● readMemory()   [default]     │
│ ● writeMemory()      │            │ ● readRdpLevel()               │
└──────────────────────┘            │ ● eraseFlash()                 │
 Implementations:                   │ ● writeFlash()                 │
 - StLinkTransport                  │ ● writeOptionBytes()           │
 - BmpTransport                     │ ● writeOtp()                   │
 - JLinkTransport                   │ ● setProgressCallback() [base] │
                                    │ ● reportProgress()     [base]  │
                                    └────────────────────────────────┘
                                     STM32 implementations:
                                     - F0, F1, F2, F3, F4, F7
                                     - G0, G4, H5, H7
                                     - L0, L1, L4, L5, U5
                                     - WB, WL
                                     NXP implementations:
                                     - MKE FTMRH (KE02/04/06)
                                     - MKE FTFE  (KE14/15/16/18)
```

## TargetDriver Hooks

All operations are routed through TargetDriver with safe defaults:

| Method | Default behavior | Override when... |
|---|---|---|
| `onConnect()` | Does nothing | Target needs watchdog disable, debug unlock |
| `onDisconnect()` | Resets and resumes core | Target needs different reset method (e.g. AIRCR) |
| `readMemory()` | Passthrough to Transport | Target needs core halted for reads |
| `readRdpLevel()` | — (pure virtual) | Always implemented per target |
| `eraseFlash()` | — (pure virtual) | Always implemented per target |
| `writeFlash()` | — (pure virtual) | Always implemented per target |
| `writeOptionBytes(unsafe)` | — (pure virtual, safe by default) | Always implemented per target |
| `writeOtp()` | — (pure virtual) | Always implemented per target |
| `setProgressCallback()` | No-op (null callback) | Set by caller for GUI progress/cancel |
| `reportProgress()` | Returns true if no callback | Called by drivers at page boundaries |

**Example:** NXP Kinetis KE parts override `onConnect()` to disable the watchdog
(WDOG/WDOG32) immediately after SWD connection, preventing a reset during programming.
STM32 parts use the default (no-op) since they don't have this issue.

## Auto-Detection

The factory probes identification registers in order:

1. **STM32 DBGMCU_IDCODE** — 4 addresses for different core types
   - `0xE0042000` (Cortex-M3/M4/M7)
   - `0x40015800` (Cortex-M0/M0+)
   - `0x5C001000` (H7 debug domain)
   - `0xE0044000` (Cortex-M33 / TrustZone)
2. **NXP SIM_SDID** — `0x40048024` (Kinetis KE)

The DEV_ID / SDID value maps to a `ChipFamily` enum, which maps to a TargetDriver.

## Why Reads Go Through TargetDriver

While most ARM Cortex-M targets have memory-mapped flash readable via plain
SWD reads, some targets may need:
- Core halted before flash reads work
- Bus clock configuration for certain memory regions
- Read-while-write restrictions

The default `readMemory()` passes through to Transport directly, but targets
can override this if needed.

## Progress Reporting

All `writeFlash` and `eraseFlash` implementations call `reportProgress(bytes_done, bytes_total)`
at natural chunk boundaries. The caller sets a `ProgressCallback` via
`Stm32Programmer::setProgressCallback()` — the callback receives `(done, total, user_data)`
and returns `bool` (`false` aborts the operation with `ErrorAborted`).

**Granularity by mode:**

| Write mode | Report interval | Rationale |
|---|---|---|
| Slow (word-by-word) | Per flash page (1-8 KB) | Natural unit, ~10-100 reports per 64 KB |
| Fast (SRAM stub) | Per SRAM chunk (8-32 KB) | One report per stub execution |
| Page-by-page erase (L0, L1) | Per page or every N pages | Matches the erase loop |
| Mass erase (all others) | No progress | Single hardware command |

This design keeps the callback rate GUI-friendly (tens of callbacks, not thousands)
without requiring the library to implement its own throttling.

## Programming Models

### STM32 — Register-bit based
```
unlock → set PG bit → write data to flash address → wait BSY → lock
```

### NXP Kinetis KE — Command based
```
write command + address + data to FCCOB registers → launch via FSTAT → wait CCIF
```

Both models are abstracted behind the same TargetDriver interface.

## Usage Example

```cpp
StLinkTransport transport;
transport.connect();

// Auto-detect chip
ChipFamily family;
uint16_t dev_id, rev_id;
TargetDriver* driver = chip_factory::detect(transport, family, dev_id, rev_id);

if (!driver) {
    printf("Unknown chip!\n");
    return;
}

printf("Detected: %s (DEV_ID=0x%03X, REV_ID=0x%04X)\n",
       chip_factory::familyName(family), dev_id, rev_id);

// Use it
Stm32Programmer programmer(transport, *driver);
programmer.setProgressCallback([](uint32_t done, uint32_t total, void*) {
    printf("\r%u / %u bytes", done, total);
    return true;
});
programmer.connect();
programmer.readFlash(buffer, 0x08000000, 0x20000);
programmer.disconnect();
```

## File Structure

```
stm32_programmer.h              Base interfaces (Transport, TargetDriver, Stm32Programmer)
stm32_chipid.h                  Chip identification constants (STM32 + NXP)
stm32_factory.h/.cpp            Auto-detection and driver factory
transports/
  stlink_transport.h/.cpp       ST-Link V2/V3 implementation
  bmp_transport.h/.cpp          Black Magic Probe implementation
  jlink_transport.h/.cpp        SEGGER J-Link implementation
flash_drivers/
  flash_utils.h                 Shared helpers (word read/write, busy-wait)
  stm32f0_flash.h/.cpp          STM32F0
  stm32f1_flash.h/.cpp          STM32F1
  stm32f2_flash.h/.cpp          STM32F2
  stm32f3_flash.h/.cpp          STM32F3
  stm32f4_flash.h/.cpp          STM32F4
  stm32f7_flash.h/.cpp          STM32F7
  stm32g0_flash.h/.cpp          STM32G0
  stm32g4_flash.h/.cpp          STM32G4
  stm32h5_flash.h/.cpp          STM32H5
  stm32h7_flash.h/.cpp          STM32H7
  stm32l0_flash.h/.cpp          STM32L0
  stm32l1_flash.h/.cpp          STM32L1
  stm32l4_flash.h/.cpp          STM32L4
  stm32l5_flash.h/.cpp          STM32L5
  stm32u5_flash.h/.cpp          STM32U5
  stm32wb_flash.h/.cpp          STM32WB
  stm32wl_flash.h/.cpp          STM32WL
  nxp/
    mke_ftmrh.h/.cpp            NXP MKE02/KE04/KE06 (FTMRH flash)
    mke_ftfe.h/.cpp             NXP MKE14/KE15/KE16/KE18 (FTFE flash)
```
