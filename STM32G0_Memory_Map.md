# STM32G0 Series — Memory Map & Read/Write Procedures

## Memory Map

```
 Address          Size        Area
 ──────────────────────────────────────────────────────────
 0x0800 0000 ┬─────────────┐
             │             │
             │   Flash     │  Up to 512 KB (varies by part)
             │  (firmware) │
             │             │
 0x0807 FFFF ┴─────────────┘

             ...

 0x1FFF 0000 ┬─────────────┐
             │   System    │
             │   Memory    │  28 KB (ST bootloader ROM)
             │ (read-only) │
 0x1FFF 6FFF ┴─────────────┘

 0x1FFF 7000 ┬─────────────┐
             │    OTP      │  1 KB (one-time programmable)
 0x1FFF 73FF ┴─────────────┘

 0x1FFF 7800 ┬─────────────┐
             │  Option     │  128 bytes
             │  Bytes      │  (RDP, WRP, PCROP, boot config)
 0x1FFF 787F ┴─────────────┘
```

## Flash Size by Part

| Part             | Flash Size   | Flash End Address |
|------------------|--------------|-------------------|
| G030 / G031 / G041 | 32–64 KB  | `0x0800 FFFF`     |
| G050 / G051 / G061 | 32–64 KB  | `0x0800 FFFF`     |
| G070 / G071 / G081 | 64–128 KB | `0x0801 FFFF`     |
| G0B0 / G0B1 / G0C1 | 256–512 KB | `0x0807 FFFF`    |

## Memory Area Details

### Flash (Program Memory)

- **Address**: `0x0800 0000` — end depends on part
- **Contents**: Application firmware
- **Page size**: 2 KB
- **Access**: Read/write via SWD or bootloader (if RDP allows)

### Option Bytes

- **Address**: `0x1FFF 7800` — `0x1FFF 787F`
- **Size**: 128 bytes
- **Contents**:
  - **RDP** — Read-out protection level (Level 0 / 1 / 2)
  - **BOR** — Brown-out reset threshold
  - **WRP** — Write protection per flash sector
  - **PCROP** — Proprietary code read-out protection regions
  - **nBOOT0 / nBOOT1 / nBOOT_SEL** — Boot source selection
  - **nRST_STOP / nRST_STDBY** — Reset behavior in low-power modes
  - **IWDG / WWDG** — Hardware watchdog configuration
  - **Secure area** configuration (G0x1 parts with security features)

### OTP (One-Time Programmable)

- **Address**: `0x1FFF 7000` — `0x1FFF 73FF`
- **Size**: 1 KB (128 double-words of 8 bytes)
- **Contents**: User-defined persistent data (serial numbers, keys, calibration)
- **Write**: Each bit can be written from 1 to 0 only once — irreversible

## RDP — Read-Out Protection Levels

| RDP Byte Value | Level   | Effect                                       |
|----------------|---------|----------------------------------------------|
| `0xAA`         | Level 0 | No protection — full debug access (default)  |
| Any other      | Level 1 | Flash read blocked via debug; mass erase to revert |
| `0xCC`         | Level 2 | Debug permanently disabled — irreversible!   |

## Read Order (Dumping Full Chip State)

Perform reads in this order:

1. **Connect and check RDP level**
   - If Level 1 or 2 — flash read will fail, decide whether to mass-erase
   - If Level 0 — proceed

2. **Read Option Bytes** (`0x1FFF 7800`, 128 bytes)
   - Small and fast — gives you hardware config and protection state

3. **Read Flash** (`0x0800 0000`, size depends on part)
   - The firmware — largest area

4. **Read OTP** (`0x1FFF 7000`, 1 KB)
   - One-time data — serial numbers, custom calibration

## Write Order (Restoring / Flashing)

Perform writes in this order:

1. **Erase flash** (if needed)

2. **Write Flash** (`0x0800 0000`)
   - Program the firmware first, before any protection is applied

3. **Write OTP** (`0x1FFF 7000`) — if needed
   - Careful: one-time only, irreversible per bit

4. **Write Option Bytes last** (`0x1FFF 7800`)
   - Changing option bytes triggers a system reset
   - Writing WRP (write protection) before flash would block further flash writes
   - Writing RDP Level 1/2 before flash would block debug access
   - Always the last step to avoid locking yourself out

## STM32CubeProgrammer CLI Examples

```bash
# Connect via ST-Link
STM32_Programmer_CLI -c port=SWD

# --- READ ---

# Display option bytes (includes RDP level)
STM32_Programmer_CLI -c port=SWD -ob displ

# Read flash (example: 128 KB for G071)
STM32_Programmer_CLI -c port=SWD -r 0x08000000 0x20000 flash_dump.bin

# Read option bytes to file (128 bytes)
STM32_Programmer_CLI -c port=SWD -r 0x1FFF7800 0x80 option_bytes.bin

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
