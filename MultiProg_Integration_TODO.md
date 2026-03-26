# MultiProg Integration TODO

What needs to be done to make this library compatible with MultiProg as a backend.

Reference: existing backends live in `MultiProg/backend_cpp/`.
The closest analog is `StLinkBackend` (`stlink_backend/stlink_backend_i.h/.cpp`) which
has its own SWD/ADIv5 stack but write/verify/erase are **all stubbed as TODO**.
This library replaces that backend's internals.


## Architecture Overview

MultiProg backend integration follows this pattern:

```
  JsonTargetManager (shared)         BackendI interface
        |                                  |
        v                                  v
  JSON target files  ──>  BackendWrapper (Qt, signals/slots)
  (families, targets,          |
   memory regions)             v
                         kt_swd_prog library (this project)
                         Transport + TargetDriver + Stm32Programmer
```

The wrapper is a Qt `QObject` + `BackendI` subclass that:
- Receives commands from the GUI (connect/read/write/verify/erase)
- Calls into this library for actual hardware operations
- Emits Qt signals for progress, errors, buffer updates, target info


## JSON Target Approach

MultiProg uses JSON files to define target databases. Each backend registers with a
programmer type string (e.g. `"ST-Link"`, `"TGSN"`) and loads families from
`JsonTargetManager`. The library does NOT need to parse JSON itself — the wrapper
handles that.

### JSON target file structure

```json
{
  "vendor": "STMicroelectronics",
  "prog": "ST-Link",
  "family_name": "STM32G0",
  "baud_default": [],
  "devices": [
    {
      "bl_size": 0,
      "subl_size": 0,
      "t_name": "STM32G030F6",
      "arguments": "",
      "regions": [
        {
          "cell_size": 1,
          "name": "Flash",
          "start_addr": "0x08000000",
          "size": "32KB"
        },
        {
          "cell_size": 1,
          "name": "Option Bytes",
          "start_addr": "0x1FFF7800",
          "size": 128,
          "arguments": "option_bytes"
        },
        {
          "cell_size": 1,
          "name": "OTP",
          "start_addr": "0x1FFF7000",
          "size": 1024
        }
      ],
      "target_custom_fields": [
        { "key": "chip_family", "type": "String", "value": "G0" }
      ]
    }
  ]
}
```

### How other backends use JSON targets

1. On init: `json_target_manager->familyListFromProg("ST-Link", families)`
2. User selects family + target in the GUI
3. Backend calls `json_target_manager->targetDescrFromFamily(family, target, desc)`
4. Backend reads `desc.regions` to initialize `MemoryManager` (hex buffer tabs)
5. Backend reads `desc.target_custom_fields` to select the right driver
6. All read/write/erase operations use region addresses from JSON, not hardcoded

### What we need: `chip_family` custom field

The `target_custom_fields` array should contain a `"chip_family"` key that maps to
our `ChipFamily` enum. The wrapper will parse this string and call
`chip_factory::createDriver()`. This replaces auto-detection with explicit selection.

Need to add: `chip_factory::familyFromString(const char* name) -> ChipFamily`
to convert JSON string values like `"G0"`, `"F4"`, `"H7"`, `"KE15"` to enum values.


## TODO Items


### 1. Add `verifyFlash()` to the library

**Priority:** Critical
**Why:** Every MultiProg backend implements verify. The GUI highlights mismatched bytes
in the hex buffer using `signalHighLightVerifError(VerificationError)`.

**What to add:**

In `stm32_programmer.h`, add a mismatch structure and verify method:

```cpp
struct FlashMismatch {
    uint32_t address;
    uint8_t  expected;
    uint8_t  actual;
};

// In Stm32Programmer:
ProgrammerStatus verifyFlash(const uint8_t* expected,
                             uint32_t       address,
                             uint32_t       size,
                             std::vector<FlashMismatch>& mismatches);
```

Implementation: read back flash in chunks via `readFlash()`, compare byte-by-byte,
collect mismatches. Report progress via the existing callback. Stop after a reasonable
number of mismatches (e.g. 1000) to avoid flooding the GUI.

The wrapper converts `FlashMismatch` to MultiProg's `backend::Mismatch`:
```cpp
struct Mismatch {
    size_t  address;
    size_t  cell_index;   // byte index within the cell
    size_t  cell_size;    // always 1 for ARM targets
    uint8_t expected;
    uint8_t actual;
};
```


### 2. Add probe scanning/enumeration to transports

**Priority:** Critical
**Why:** `BackendI::get_programmers_list(bool scan)` must return a list of connected
probes with name, serial number, and type. The GUI shows these in a dropdown.

**What to add:**

A `ProbeInfo` struct and static scan methods:

```cpp
// In stm32_programmer.h or a new probe_info.h:
struct ProbeInfo {
    std::string name;       // "ST-Link V2", "J-Link", "Black Magic Probe"
    std::string serial;     // serial number string
    uint16_t    vid = 0;    // USB VID (for re-opening by identity)
    uint16_t    pid = 0;    // USB PID
};

// In StLinkTransport:
static std::vector<ProbeInfo> listProbes();

// In JLinkTransport:
static std::vector<ProbeInfo> listProbes();

// In BmpTransport (serial ports):
static std::vector<ProbeInfo> listPorts();
```

Also need: a way to open a **specific** probe (not just the first one):

```cpp
// StLinkTransport:
void selectProbe(const ProbeInfo& probe);  // call before connect()

// JLinkTransport:
void selectProbe(uint32_t serial_number);

// BmpTransport already takes port name in constructor
```


### 3. Add `familyFromString()` to the factory

**Priority:** Critical
**Why:** JSON targets use a string `"chip_family"` custom field to identify which
`TargetDriver` to use. Need to convert `"G0"` -> `ChipFamily::G0`.

**What to add in `stm32_factory.h`:**

```cpp
namespace chip_factory {
    /// Parse a family name string (e.g. "G0", "F4", "H7", "KE15") to ChipFamily.
    /// Returns ChipFamily::Unknown if the string doesn't match.
    ChipFamily familyFromString(const char* name);
}
```

Map: `"F0"`->F0, `"F1"`->F1, ..., `"KE02"`->KE02, etc.
Should be case-insensitive or at least match the `familyName()` output.


### 4. Expose memory region info from TargetDriver

**Priority:** Critical
**Why:** The wrapper needs to know which addresses to read/write for each region type.
Currently region addresses are hardcoded constants inside each driver's header —
not accessible programmatically. When a user hits "Read", the wrapper iterates
over JSON regions and must route each region to the right library method
(`readFlash` vs `readOptionBytes` vs `readOtp`) based on region type.

**Option A — Region descriptors from the driver (recommended):**

```cpp
// In stm32_programmer.h:
struct MemoryRegionInfo {
    enum Type { Flash, OptionBytes, OTP };
    Type        type;
    uint32_t    start_addr;
    uint32_t    size;
    uint32_t    page_size;  // erase granularity (0 if not applicable)
};

// In TargetDriver:
virtual std::vector<MemoryRegionInfo> getMemoryRegions(Transport& transport) const;
```

This lets the library report its actual memory map after connecting (some chips
need to read flash size registers to know the exact size). The wrapper can
cross-reference this with JSON regions.

**Option B — Route by JSON `arguments` field (simpler):**

Use the JSON region's `arguments` field to decide the operation:
- `""` (empty) or absent -> `readFlash()` / `writeFlash()`
- `"option_bytes"` -> `readOptionBytes()` / `writeOptionBytes()`
- `"otp"` -> `readOtp()` / `writeOtp()`

This is how the existing `StLinkBackend` works — it doesn't query the driver for
regions, it just trusts the JSON. This is simpler but means the JSON must be
accurate. **This is probably sufficient for the initial integration.**


### 5. Create JSON target definition files

**Priority:** Critical
**Why:** MultiProg loads targets from JSON files. We need one JSON file per family
(or per vendor) with all supported targets and their memory maps.

**Files to create** (in MultiProg's `system_targets/` directory):

| File | Families | Targets (examples) |
|------|----------|---------------------|
| `STM32F0.json` | STM32F0 | STM32F030F4, STM32F070RB, STM32F091RC |
| `STM32F1.json` | STM32F1 | STM32F103C8, STM32F103RB, STM32F103ZE |
| `STM32F2.json` | STM32F2 | STM32F205RG, STM32F207ZG |
| `STM32F3.json` | STM32F3 | STM32F303CC, STM32F334R8, STM32F373RC |
| `STM32F4.json` | STM32F4 | STM32F401RE, STM32F407VG, STM32F446RE |
| `STM32F7.json` | STM32F7 | STM32F746ZG, STM32F767ZI |
| `STM32G0.json` | STM32G0 | STM32G030F6, STM32G071RB, STM32G0B1RE |
| `STM32G4.json` | STM32G4 | STM32G431KB, STM32G474RE |
| `STM32H5.json` | STM32H5 | STM32H503RB, STM32H563ZI |
| `STM32H7.json` | STM32H7 | STM32H743ZI, STM32H750VB, STM32H7A3ZI |
| `STM32L0.json` | STM32L0 | STM32L053R8, STM32L073RZ |
| `STM32L1.json` | STM32L1 | STM32L151RB, STM32L152RE |
| `STM32L4.json` | STM32L4 | STM32L431RC, STM32L476RG, STM32L4R5ZI |
| `STM32L5.json` | STM32L5 | STM32L552ZE |
| `STM32U5.json` | STM32U5 | STM32U575ZI, STM32U585AI |
| `STM32WB.json` | STM32WB | STM32WB55RG |
| `STM32WL.json` | STM32WL | STM32WLE5JC |
| `MKE_FTMRH.json` | KE02/KE04/KE06 | MKE02Z64, MKE04Z128, MKE06Z128 |
| `MKE_FTFE.json` | KE14-KE18 | MKE14Z256, MKE15Z256, MKE18F512 |

Each device entry needs: exact flash size, option bytes location/size, OTP location/size
(where applicable), and the `"chip_family"` custom field.

**Source of truth:** STM32 datasheets, reference manuals, and the constants already
defined in each driver header file (e.g., `stm32g0_flash.h` has `FLASH_START`,
`PAGE_SIZE`, `OB_BASE`, `OB_SIZE`, `OTP_BASE`, `OTP_SIZE`).


### 6. Add flash size detection after connect

**Priority:** Important
**Why:** Many STM32 chips have a flash size register (e.g., `0x1FFF75E0` for G0,
`0x1FFF7A22` for F4). After connecting, the library should report actual flash size
so the wrapper can update the target info panel and validate against JSON regions.

**What to add:**

```cpp
// In TargetDriver (optional override):
virtual uint32_t detectFlashSize(Transport& transport) { return 0; }
```

Each driver reads the family-specific flash size register and returns KB.
This is informational — the JSON still defines the memory map, but the wrapper
can warn if JSON size doesn't match the actual chip.


### 7. Add page/sector erase

**Priority:** Important
**Why:** Current `eraseFlash()` is mass erase only. For write operations, most drivers
already erase pages internally in `writeFlash()`. But having explicit page erase allows:
- The GUI erase button to offer "mass erase" vs "sector erase" options
- More efficient programming (erase only what's needed)
- Better progress reporting during erase

**What to add:**

```cpp
// In TargetDriver:
virtual ProgrammerStatus eraseSectors(Transport& transport,
                                      uint32_t   start_addr,
                                      uint32_t   size);
```

Most drivers already have internal page erase logic — just need to expose it.


### 8. Package connected chip info

**Priority:** Nice to have
**Why:** After connect, MultiProg shows target info (voltage, CPUID, device ID, etc.).
Currently this data is scattered across Transport (`targetVoltage()`, `probeName()`)
and factory detect output (`dev_id`, `rev_id`).

**What to add:**

```cpp
struct ChipInfo {
    ChipFamily family;
    uint16_t   dev_id;
    uint16_t   rev_id;
    uint32_t   flash_size_kb;
    float      voltage;        // from transport
    const char* probe_name;    // from transport
    const char* family_name;   // from chip_factory::familyName()
};
```

The wrapper can assemble this from existing APIs, so this is not strictly needed
in the library. But a single `getChipInfo()` method is cleaner.


### 9. Add read protection level control

**Priority:** Nice to have
**Why:** The existing `StLinkBackend` checks RDP level on connect and shows warnings.
`readRdpLevel()` already exists, but there's no way to **change** it (set/remove
read protection). For a full-featured programmer, we need:

```cpp
// In TargetDriver:
virtual ProgrammerStatus setRdpLevel(Transport& transport, RdpLevel level);
```

This is essentially an option byte write, but it's a common enough operation
to warrant a dedicated API.


### 10. Add speed measurement support

**Priority:** Minor
**Why:** MultiProg's `ProgressInfo` has a `speed` field (bytes/sec). The wrapper can
calculate this from timestamps, so it doesn't need to be in the library.

No library changes needed — handle in the wrapper.


## Wrapper Implementation Checklist

These items are implemented in the MultiProg wrapper, not in this library.
Listed here for completeness.

- [ ] Create `KtSwdBackend` class inheriting from `QObject` + `BackendI`
- [ ] Implement all `BackendI` pure virtual methods
- [ ] Define Qt signals matching `StLinkBackend` pattern
- [ ] Map JSON `"chip_family"` custom field to `ChipFamily` enum
- [ ] Initialize `MemoryManager` from JSON regions
- [ ] Route region reads/writes by `arguments` field (`""` -> flash, `"option_bytes"` -> OB, etc.)
- [ ] Convert library `ProgressCallback` to Qt `signalProgressModel`
- [ ] Convert library `ProgrammerStatus` + `getLastError()` to Qt `signalError`
- [ ] Convert library `FlashMismatch` vector to `backend::VerificationError`
- [ ] Register in `BackendDispatcher::slotBD_Init()` and `connect_*_signals()`
- [ ] Add `BackendSelector` enum value and `DebugProbeType` entries
- [ ] Add to `backend_selector_from_prog()` routing
- [ ] Implement `save_buff_to_file` / `load_buff_from_file` / `clear_buffer` / `apply_buffer_edit`
  using existing `MemoryManager` (same pattern as current `StLinkBackend`)
- [ ] Support multiple transport types (ST-Link, J-Link, BMP) selectable from the GUI
- [ ] Handle probe selection from `get_programmers_list()` scan results


## Summary — Priority Order

| # | Item | Priority | Where | Effort |
|---|------|----------|-------|--------|
| 1 | ~~`verifyFlash()` with mismatch list~~ | ~~Critical~~ | ~~Library~~ | **Done** |
| 2 | ~~Probe scanning (`listProbes()`)~~ | ~~Critical~~ | ~~Library (transports)~~ | **Done** |
| 3 | ~~`familyFromString()` for JSON mapping~~ | ~~Critical~~ | ~~Library (factory)~~ | **Done** |
| 4 | Region routing by JSON `arguments` | Critical | Wrapper (no lib change) | Small |
| 5 | JSON target definition files | Critical | MultiProg system_targets | Large |
| 6 | Flash size detection | Important | Library (drivers) | Medium |
| 7 | Page/sector erase API | Important | Library (drivers) | Medium |
| 8 | Connected chip info struct | Nice to have | Library or wrapper | Small |
| 9 | RDP level control | Nice to have | Library (drivers) | Medium |
| 10 | Speed measurement | Minor | Wrapper only | Trivial |
