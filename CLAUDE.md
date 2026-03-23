# KuragaSTM32Tool

Universal ARM Cortex-M programmer supporting STM32 and NXP Kinetis KE families
through multiple debug probes (ST-Link, Black Magic Probe, J-Link).

## Architecture

Composition pattern: **Transport** (probe communication) + **TargetDriver** (chip-specific logic).
See `STM32_Programmer_Architecture.md` for full details.

### Key interfaces (in `stm32_programmer.h`)
- `Transport` — raw SWD memory read/write (one impl per probe type)
- `TargetDriver` — target-specific flash/option byte/OTP logic (one impl per chip family)
- `Stm32Programmer` — composes Transport + TargetDriver, routes all operations through both

### Naming conventions
- `ChipFamily` is the canonical enum (alias: `Stm32Family`)
- `TargetDriver` is the canonical base class (alias: `FlashDriver`)
- `chip_factory` is the canonical namespace (alias: `stm32_factory`)
- Aliases exist for backward compatibility — new code should use canonical names

## Project structure

```
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
```

## Coding conventions

- All `.h` and `.cpp` files must have Kuraga Tech file headers (use `/add-headers` skill)
- Use `#pragma once` for include guards
- Flash register constants: `constexpr uint32_t` in a namespace, with inline comments
- Bit definitions: `(1UL << N)` shift expressions, not raw hex
- Error handling: return `ProgrammerStatus` enum from all operations
- TargetDriver hooks (`onConnect`, `onDisconnect`, `readMemory`) have default
  implementations — only override when the target actually needs it
- `writeOptionBytes` is safe by default (`unsafe = false`): rejects RDP Level 2,
  forces write protection off on OPTCR families. Pass `unsafe = true` to bypass.
- Progress reporting: `writeFlash` and `eraseFlash` call `reportProgress()` at
  page/sector boundaries. Set via `Stm32Programmer::setProgressCallback()`.
  Callback returns `bool` — return `false` to abort (`ErrorAborted`).

## Adding a new target family

1. Create `flash_drivers/<vendor>/<family>.h` with register constants and class declaration
2. Create `flash_drivers/<vendor>/<family>.cpp` with implementation
3. Add the family to `ChipFamily` enum in `stm32_chipid.h`
4. Add identification constants to the appropriate `*_chipid` namespace
5. Add a static driver instance and switch cases in `stm32_factory.cpp`
6. Add `reportProgress()` calls in `writeFlash` (at page/sector boundaries)
   and `eraseFlash` (if page-by-page). Check return value — false means abort.
7. Add Kuraga Tech headers with `/add-headers`

## Adding a new debug probe

1. Create `transports/<probe>_transport.h/.cpp` inheriting from `Transport`
2. Implement `connect()`, `disconnect()`, `readMemory()`, `writeMemory()`
3. No changes needed anywhere else — it works with all existing targets

## Building

Requires CMake 3.20+ and a C++17 compiler.

### Quick build (ST-Link only)

```bash
cmake -B build
cmake --build build
```

### Build with additional transports

```bash
cmake -B build -DKT_TRANSPORT_BMP=ON -DKT_TRANSPORT_JLINK=ON
cmake --build build
```

### Build options

| Option               | Default | Description                            |
|----------------------|---------|----------------------------------------|
| KT_TRANSPORT_STLINK  | ON      | ST-Link transport (requires libusb)    |
| KT_TRANSPORT_BMP     | OFF     | Black Magic Probe transport            |
| KT_TRANSPORT_JLINK   | OFF     | J-Link transport                       |
| KT_BUILD_CLI         | ON      | Build `kt_swd_cli` executable          |

### Install

```bash
cmake --install build --prefix <install-path>
```

### Consuming as a library

Via `add_subdirectory()`:
```cmake
add_subdirectory(vendor/kt_swd_prog)
target_link_libraries(my_app PRIVATE kt_swd_prog)
```

Via `find_package()` (after install):
```cmake
find_package(kt_swd_prog REQUIRED)
target_link_libraries(my_app PRIVATE kt_swd_prog::kt_swd_prog)
```

## Documentation files

- `STM32_Programmer_Architecture.md` — architecture overview and design rationale
- `STM32G0_Memory_Map.md` — STM32G0 memory map and read/write procedures
- `STM32G0_Tool_Comparison.md` — comparison with CubeProgrammer, OpenOCD, probe-rs
