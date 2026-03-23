# STM32G0 Series — Flashing Tools Comparison

## Tools Overview

- **STM32CubeProgrammer** — ST's official tool (GUI + CLI), closed-source
- **OpenOCD** — open-source, uses `stm32l4x` flash driver for G0 series
- **probe-rs** — modern Rust-based toolkit, open-source
- **KuragaSTM32Tool** — our own programmer (see `STM32_Programmer_Architecture.md`)

## Capability Comparison

| Operation            | CubeProgrammer       | OpenOCD              | probe-rs             | KuragaSTM32Tool          |
|----------------------|----------------------|----------------------|----------------------|----------------------|
| Read flash           | Easy                 | Easy                 | Easy                 | Easy                 |
| Write flash          | Easy                 | Easy                 | Easy                 | Easy                 |
| Read option bytes    | Easy                 | Easy                 | Easy                 | Easy                 |
| Write option bytes   | Easy (one command)   | Medium (per register)| Hard (manual unlock) | Easy                 |
| Read OTP             | Easy                 | Easy                 | Easy                 | Easy                 |
| Write OTP            | Easy (one command)   | Hard (manual unlock) | Hard (manual unlock) | Easy                 |

## STM32CubeProgrammer CLI

```bash
# Connect via ST-Link
STM32_Programmer_CLI -c port=SWD

# --- READ ---

# Display option bytes (includes RDP level)
STM32_Programmer_CLI -c port=SWD -ob displ

# Read option bytes to file (128 bytes)
STM32_Programmer_CLI -c port=SWD -r 0x1FFF7800 0x80 option_bytes.bin

# Read flash (example: 128 KB for G071)
STM32_Programmer_CLI -c port=SWD -r 0x08000000 0x20000 flash_dump.bin

# Read OTP (1 KB)
STM32_Programmer_CLI -c port=SWD -r 0x1FFF7000 0x400 otp_dump.bin

# --- WRITE ---

# 1. Erase flash
STM32_Programmer_CLI -c port=SWD -e all

# 2. Write flash
STM32_Programmer_CLI -c port=SWD -w flash_dump.bin 0x08000000

# 3. Write OTP (if needed — irreversible!)
STM32_Programmer_CLI -c port=SWD -w otp_dump.bin 0x1FFF7000

# 4. Write option bytes last (triggers reset)
STM32_Programmer_CLI -c port=SWD -w option_bytes.bin 0x1FFF7800
```

## OpenOCD

Config: `interface/stlink.cfg` + `target/stm32g0x.cfg`
Flash driver: `stm32l4x` (G0 shares the same flash controller as L4/G4)

```bash
# --- READ ---

# Read flash
openocd -f interface/stlink.cfg -f target/stm32g0x.cfg \
  -c "init; reset halt; flash read_image flash_dump.bin 0x08000000 0x20000 bin; shutdown"

# Read option bytes (display as words)
openocd -f interface/stlink.cfg -f target/stm32g0x.cfg \
  -c "init; reset halt; mdw 0x1FFF7800 32; shutdown"

# Read option bytes via flash driver
openocd -f interface/stlink.cfg -f target/stm32g0x.cfg \
  -c "init; reset halt; stm32l4x option_read 0 0x1FFF7800; shutdown"

# Read OTP to file
openocd -f interface/stlink.cfg -f target/stm32g0x.cfg \
  -c "init; reset halt; dump_image otp_dump.bin 0x1FFF7000 0x400; shutdown"

# --- WRITE ---

# Write flash (with erase)
openocd -f interface/stlink.cfg -f target/stm32g0x.cfg \
  -c "init; reset halt; flash write_image erase flash_dump.bin 0x08000000 bin; shutdown"

# Write a single option byte register (e.g. FLASH_OPTR)
openocd -f interface/stlink.cfg -f target/stm32g0x.cfg \
  -c "init; reset halt; stm32l4x option_write 0 0xFFFFFEAA 0x1FFF7800; shutdown"
```

**Limitations:**
- Option bytes must be written one register at a time (no bulk restore from file)
- OTP write requires manual flash controller unlock via `mww` — no built-in command

## probe-rs

```bash
# --- READ ---

# Read flash
probe-rs read --chip STM32G071RBTx 0x08000000 0x20000 > flash_dump.bin

# Read option bytes (128 bytes)
probe-rs read --chip STM32G071RBTx 0x1FFF7800 0x80 > option_bytes.bin

# Read OTP (1 KB)
probe-rs read --chip STM32G071RBTx 0x1FFF7000 0x400 > otp_dump.bin

# --- WRITE ---

# Erase flash
probe-rs erase --chip STM32G071RBTx

# Write flash
probe-rs download --chip STM32G071RBTx flash_dump.bin --base-address 0x08000000
```

**Limitations:**
- No built-in option byte write command — requires manual flash unlock sequence
- No built-in OTP write command — same issue
- Best suited for flash-only workflows

## Custom Tool

Our own programmer built with a composition architecture (see `stm32_programmer.h`):

- **Transport layer** — raw memory read/write over SWD, one implementation per probe type
- **Flash Driver layer** — flash controller logic, one implementation per STM32 family
- **Stm32Programmer** — composes Transport + FlashDriver at runtime

```
Transport (per probe)            FlashDriver (per family)
├── StLinkTransport              ├── Stm32G0FlashDriver
├── BmpTransport                 ├── Stm32F4FlashDriver
└── JLinkTransport               └── ...
```

**Key design decisions:**
- Reads go directly through Transport (memory-mapped, same for all families)
- Writes go through FlashDriver → Transport (family-specific unlock/program sequences)
- Adding a new probe = one Transport class, works with all families
- Adding a new STM32 family = one FlashDriver class, works with all probes

See `STM32_Programmer_Architecture.md` for full details.

## Recommendation

- **Existing tools**: CubeProgrammer is the most complete for option bytes / OTP writes.
  OpenOCD and probe-rs are faster and open-source but lack easy option byte / OTP write support.
- **KuragaSTM32Tool**: will handle all operations uniformly across all STM32 families and probes,
  with flash controller knowledge built into per-family FlashDriver implementations.
