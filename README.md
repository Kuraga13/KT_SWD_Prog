# KuragaSTM32Tool

Universal ARM Cortex-M programmer library and CLI supporting STM32 and NXP Kinetis KE
families through multiple debug probes.

## Supported hardware

### Debug probes

| Probe | Interface | Dependency |
|-------|-----------|------------|
| ST-Link V2/V2-1/V3 | USB (libusb) | libusb-1.0 |
| Black Magic Probe | Serial (GDB protocol) | None |
| SEGGER J-Link | Runtime DLL loading | JLinkARM library |

### Target families

| Family | Variants | Flash controller | Tested |
|--------|----------|------------------|--------|
| **STM32F0** | F03x, F04x, F05x, F07x, F09x | FPEC (page-based) | No |
| **STM32F1** | F100–F107 (low/medium/high/XL/connectivity) | FPEC (page-based) | **Yes** |
| **STM32F2** | F2xx | OPTCR (sector-based) | No |
| **STM32F3** | F301–F373 | FPEC (page-based) | No |
| **STM32F4** | F401–F479 | OPTCR (sector-based, dual-bank) | **Yes** |
| **STM32F7** | F72x–F77x | OPTCR (sector-based) | No |
| **STM32G0** | G03x–G0Cx | PECR (page-based) | **Yes** |
| **STM32G4** | G431–G4Ax | PECR (page-based) | No |
| **STM32H5** | H503, H523, H533, H562, H563 | PECR (sector-based) | No |
| **STM32H7** | H72x–H75x | Dual-bank sector (256-bit) | **Yes** |
| **STM32H7AB** | H7A3, H7B0, H7B3 | Dual-bank sector (128-bit) | No |
| **STM32L0** | L01x–L08x | PELOCK (page-based, EEPROM) | No |
| **STM32L1** | L1 Cat1–Cat6 | PELOCK (page-based, EEPROM) | No |
| **STM32L4** | L41x–L4Sx | PECR (page-based) | No |
| **STM32L5** | L552, L562 | PECR (page-based, TrustZone) | No |
| **STM32U5** | U535–U5Gx | PECR (page-based, TrustZone) | No |
| **STM32WB** | WB30, WB35, WB50, WB55 | PECR (page-based) | No |
| **STM32WL** | WL55, WLE5 | PECR (page-based) | No |
| **NXP MKE02** | MKE02Z | FTMRH | No |
| **NXP MKE04** | MKE04Z | FTMRH | No |
| **NXP MKE06** | MKE06Z | FTMRH | No |
| **NXP MKE14** | MKE14Z/F | FTFE | No |
| **NXP MKE15** | MKE15Z | FTFE | No |
| **NXP MKE16** | MKE16F | FTFE | No |
| **NXP MKE18** | MKE18F | FTFE | No |

Families marked **Yes** have been tested on real hardware. Other families have driver
implementations based on reference manuals but have not yet been validated on silicon.

All families automatically reset and resume the MCU after programming via the base
class `onDisconnect` hook (`resetTarget()` + `resumeCore()`). STM32F4 and STM32H7
override this with a direct Cortex-M AIRCR `SYSRESETREQ` write instead.

## Building

Requires CMake 3.20+ and a C++17 compiler.

```bash
cmake -B build
cmake --build build
```

By default only the ST-Link transport is enabled (requires libusb-1.0).
To enable additional transports:

```bash
cmake -B build -DKT_TRANSPORT_BMP=ON -DKT_TRANSPORT_JLINK=ON
cmake --build build
```

### Build options

| Option | Default | Description |
|---|---|---|
| `KT_TRANSPORT_STLINK` | ON | ST-Link transport (requires libusb) |
| `KT_TRANSPORT_BMP` | OFF | Black Magic Probe transport |
| `KT_TRANSPORT_JLINK` | OFF | J-Link transport |
| `KT_BUILD_CLI` | ON | Build `kt_swd_cli` executable |

### Install

```bash
cmake --install build --prefix <install-path>
```

## CLI usage

```
kt_swd_cli <command> [options]
```

### Commands

```bash
kt_swd_cli detect                                    # auto-detect chip
kt_swd_cli read flash <addr> <size> -o <file>        # read flash to file
kt_swd_cli read ob <addr> <size> -o <file>           # read option bytes
kt_swd_cli read otp <addr> <size> -o <file>          # read OTP
kt_swd_cli write flash <file> <addr>                 # write flash from file
kt_swd_cli write ob <file> <addr>                    # write option bytes (sanitized)
kt_swd_cli write ob <file> <addr> --unsafe           # write option bytes as-is
kt_swd_cli write otp <file> <addr>                   # write OTP
kt_swd_cli erase                                     # mass erase
kt_swd_cli rdp                                       # check RDP level
```

### Options

| Option | Description |
|---|---|
| `--probe stlink\|bmp\|jlink` | Debug probe type (default: stlink) |
| `--port <path>` | Serial port for BMP (e.g. COM3, /dev/ttyACM0) |
| `--speed <kHz>` | SWD speed for J-Link (default: 4000) |
| `--chip <family>` | Override auto-detect (e.g. F4, G0, KE02) |
| `--unsafe` | Skip option byte sanitization (write as-is) |

### Examples

```bash
# Detect connected chip via ST-Link
kt_swd_cli detect

# Read 128 KB of flash starting at 0x08000000
kt_swd_cli read flash 0x08000000 0x20000 -o firmware.bin

# Write firmware via J-Link at 8 MHz
kt_swd_cli write flash firmware.bin 0x08000000 --probe jlink --speed 8000

# Check RDP level via Black Magic Probe
kt_swd_cli rdp --probe bmp --port COM3

# Mass erase with explicit chip family
kt_swd_cli erase --chip G0

# Write option bytes (safe mode — rejects RDP Level 2, clears write protection)
kt_swd_cli write ob optionbytes.bin 0x40023C14

# Write option bytes without sanitization (use with caution)
kt_swd_cli write ob optionbytes.bin 0x40023C14 --unsafe
```

### Option byte safety

By default, `write ob` sanitizes option bytes before writing:
- **Rejects RDP Level 2** (0xCC) — this permanently disables debug access and is irreversible
- **Forces write protection off** (OPTCR-style families: F2, F4, F7) — prevents accidental sector lockout

Pass `--unsafe` to bypass sanitization and write option bytes exactly as provided.

## Library usage

### Via add_subdirectory

```cmake
add_subdirectory(vendor/kt_swd_prog)
target_link_libraries(my_app PRIVATE kt_swd_prog)
```

### Via find_package (after install)

```cmake
find_package(kt_swd_prog REQUIRED)
target_link_libraries(my_app PRIVATE kt_swd_prog::kt_swd_prog)
```

### API example

```cpp
#include "stm32_programmer.h"
#include "stm32_factory.h"
#include "transports/stlink_transport.h"

StLinkTransport transport;
transport.connect();

// Auto-detect chip
ChipFamily family;
uint16_t dev_id, rev_id;
TargetDriver* driver = chip_factory::detect(transport, family, dev_id, rev_id);

if (driver) {
    Stm32Programmer programmer(transport, *driver);
    programmer.connect();

    // Read flash
    uint8_t buffer[0x20000];
    programmer.readFlash(buffer, 0x08000000, sizeof(buffer));

    // Write flash
    programmer.writeFlash(data, 0x08000000, data_size);

    programmer.disconnect();
}
```

### Progress reporting

Set a progress callback to receive updates during `writeFlash` and `eraseFlash`.
The callback returns `bool` — return `false` to abort the operation.

```cpp
Stm32Programmer programmer(transport, *driver);

// Simple progress display
programmer.setProgressCallback([](uint32_t done, uint32_t total, void*) -> bool {
    printf("\r%u / %u bytes (%u%%)", done, total, done * 100 / total);
    return true;  // return false to abort
});

// GUI integration with cancellation
programmer.setProgressCallback([](uint32_t done, uint32_t total, void* ctx) -> bool {
    auto* bar = static_cast<ProgressBar*>(ctx);
    bar->setValue(done * 100 / total);
    return !bar->wasCancelled();
}, &progressBar);
```

Progress is reported at natural chunk boundaries (per flash page/sector in slow mode,
per SRAM buffer in stub-accelerated mode), not per byte — safe for GUI use without
throttling.

## Architecture

Composition pattern: **Transport** (probe communication) + **TargetDriver** (chip-specific logic).

```
┌──────────────────┐     ┌──────────────────┐
│   StLinkTransport│     │  Stm32G0TargetDrv│
│   BmpTransport   │     │  Stm32F4TargetDrv│
│   JLinkTransport │     │  MkeFtfeTargetDrv│
│       ...        │     │       ...        │
└────────┬─────────┘     └────────┬─────────┘
         │  implements            │  implements
    ┌────▼────┐             ┌─────▼──────┐
    │Transport│             │TargetDriver│
    └────┬────┘             └─────┬──────┘
         │                        │
         └───────┐    ┌───────────┘
              ┌──▼────▼──┐
              │Stm32      │
              │Programmer │
              └───────────┘
```

Adding a new chip family requires no changes to transports.
Adding a new debug probe requires no changes to target drivers.

See `STM32_Programmer_Architecture.md` for full design details.

## Project structure

```
CMakeLists.txt                  Build system
main.cpp                        CLI entry point
stm32_programmer.h              Core interfaces (Transport, TargetDriver, Stm32Programmer)
stm32_chipid.h                  Chip ID constants (STM32 DBGMCU_IDCODE + NXP SIM_SDID)
stm32_factory.h/.cpp            Auto-detection and driver factory
transports/                     Debug probe implementations
  stlink_transport.h/.cpp         ST-Link V2/V3 (USB via libusb)
  bmp_transport.h/.cpp            Black Magic Probe (GDB serial protocol)
  jlink_transport.h/.cpp          SEGGER J-Link (runtime DLL loading)
flash_drivers/                  Target driver implementations
  flash_utils.h                   Shared helpers (word read/write, busy-wait)
  stm32*.h/.cpp                   STM32 families (F0-F7, G0, G4, H5, H7, L0-L5, U5, WB, WL)
  nxp/                            NXP Kinetis KE
    mke_ftmrh.h/.cpp                MKE02/KE04/KE06 (FTMRH flash controller)
    mke_ftfe.h/.cpp                 MKE14/KE15/KE16/KE18 (FTFE flash controller)
cmake/                          CMake package config template
```

## License

This project is licensed under the [MIT License](LICENSE).

Copyright (c) 2026 Kuraga Tech.
