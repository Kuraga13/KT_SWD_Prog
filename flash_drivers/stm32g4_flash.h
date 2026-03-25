/**
 ******************************************************************************
 * @file           : stm32g4_flash.h
 * @brief          : Flash driver for STM32G4 series
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 * @details
 *
 * Flash driver for STM32G4 series (Cortex-M4F).
 * Page-based flash with double-word (64-bit) programming.
 * L4-style flash controller, very similar to G0 but with larger
 * flash capacity and dual-bank support on most variants.
 *
 * Features:
 * - Page erase (2 KB single-bank / 4 KB dual-bank) and mass erase
 * - Double-word (64-bit) flash programming
 * - Option byte read/write with RDP, WRP, PCROP support
 * - OTP area access (1 KB)
 * - Dual-bank support
 *
 * Usage Example:
 * ```cpp
 * Stm32G4FlashDriver flash_driver;
 * Stm32Programmer programmer(transport, flash_driver);
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "../stm32_programmer.h"

namespace stm32g4_flash {
    constexpr uint32_t FLASH_BASE       = 0x40022000;

    constexpr uint32_t FLASH_ACR        = FLASH_BASE + 0x00;
    constexpr uint32_t FLASH_PDKEYR     = FLASH_BASE + 0x04;
    constexpr uint32_t FLASH_KEYR       = FLASH_BASE + 0x08;
    constexpr uint32_t FLASH_OPTKEYR    = FLASH_BASE + 0x0C;
    constexpr uint32_t FLASH_SR         = FLASH_BASE + 0x10;
    constexpr uint32_t FLASH_CR         = FLASH_BASE + 0x14;
    constexpr uint32_t FLASH_ECCR       = FLASH_BASE + 0x18;
    constexpr uint32_t FLASH_OPTR       = FLASH_BASE + 0x20;
    constexpr uint32_t FLASH_PCROP1SR   = FLASH_BASE + 0x24;
    constexpr uint32_t FLASH_PCROP1ER   = FLASH_BASE + 0x28;
    constexpr uint32_t FLASH_WRP1AR     = FLASH_BASE + 0x2C;
    constexpr uint32_t FLASH_WRP1BR     = FLASH_BASE + 0x30;
    constexpr uint32_t FLASH_PCROP2SR   = FLASH_BASE + 0x44;
    constexpr uint32_t FLASH_PCROP2ER   = FLASH_BASE + 0x48;
    constexpr uint32_t FLASH_WRP2AR     = FLASH_BASE + 0x4C;
    constexpr uint32_t FLASH_WRP2BR     = FLASH_BASE + 0x50;
    constexpr uint32_t FLASH_SEC1R      = FLASH_BASE + 0x70;
    constexpr uint32_t FLASH_SEC2R      = FLASH_BASE + 0x74;

    constexpr uint32_t KEY1             = 0x45670123;
    constexpr uint32_t KEY2             = 0xCDEF89AB;
    constexpr uint32_t OPT_KEY1         = 0x08192A3B;
    constexpr uint32_t OPT_KEY2         = 0x4C5D6E7F;

    // FLASH_ACR bits (ART Accelerator cache control)
    constexpr uint32_t ACR_ICEN          = (1UL << 9);   // instruction cache enable
    constexpr uint32_t ACR_DCEN          = (1UL << 10);  // data cache enable
    constexpr uint32_t ACR_ICRST         = (1UL << 11);  // instruction cache reset
    constexpr uint32_t ACR_DCRST         = (1UL << 12);  // data cache reset

    // FLASH_CR bits
    constexpr uint32_t CR_PG            = (1UL << 0);
    constexpr uint32_t CR_PER           = (1UL << 1);
    constexpr uint32_t CR_MER1          = (1UL << 2);
    constexpr uint32_t CR_PNB_SHIFT     = 3;
    constexpr uint32_t CR_PNB_MASK      = 0x7F;
    constexpr uint32_t CR_BKER          = (1UL << 11);  // bank selection
    constexpr uint32_t CR_MER2          = (1UL << 15);
    constexpr uint32_t CR_STRT          = (1UL << 16);
    constexpr uint32_t CR_OPTSTRT       = (1UL << 17);
    constexpr uint32_t CR_FSTPG         = (1UL << 18);
    constexpr uint32_t CR_OBL_LAUNCH    = (1UL << 27);
    constexpr uint32_t CR_OPTLOCK       = (1UL << 30);
    constexpr uint32_t CR_LOCK          = (1UL << 31);

    // FLASH_SR bits
    constexpr uint32_t SR_EOP           = (1UL << 0);   // end of operation
    constexpr uint32_t SR_OPERR         = (1UL << 1);   // operation error
    constexpr uint32_t SR_PROGERR       = (1UL << 3);   // programming error
    constexpr uint32_t SR_WRPERR        = (1UL << 4);   // write protection error
    constexpr uint32_t SR_PGAERR        = (1UL << 5);   // programming alignment error
    constexpr uint32_t SR_SIZERR        = (1UL << 6);   // size error
    constexpr uint32_t SR_PGSERR        = (1UL << 7);   // programming sequence error
    constexpr uint32_t SR_MISERR        = (1UL << 8);   // fast programming data miss error
    constexpr uint32_t SR_FASTERR       = (1UL << 9);   // fast programming error
    constexpr uint32_t SR_RDERR         = (1UL << 14);  // PCROP read error
    constexpr uint32_t SR_OPTVERR       = (1UL << 15);  // option byte validity error
    constexpr uint32_t SR_BSY           = (1UL << 16);  // busy

    constexpr uint32_t SR_ALL_ERRORS    = SR_OPERR | SR_PROGERR | SR_WRPERR | SR_PGAERR |
                                          SR_SIZERR | SR_PGSERR | SR_MISERR | SR_FASTERR |
                                          SR_RDERR | SR_OPTVERR;

    constexpr uint32_t FLASH_START      = 0x08000000;
    constexpr uint32_t PAGE_SIZE        = 2048;          // 2 KB (dual bank mode, default)
    constexpr uint32_t PAGE_SIZE_DUAL   = 4096;          // 4 KB (single bank mode)
    constexpr uint32_t OB_BASE          = 0x1FFF7800;
    constexpr uint32_t OB_SIZE          = 128;
    constexpr uint32_t OTP_BASE         = 0x1FFF7000;
    constexpr uint32_t OTP_SIZE         = 1024;

    constexpr uint32_t PROG_WIDTH       = 8;             // double-word (64-bit)

    constexpr uint8_t  RDP_LEVEL_0      = 0xAA;
    constexpr uint8_t  RDP_LEVEL_2      = 0xCC;
}

// SRAM flash loader stub layout (conservative: 32 KB SRAM, smallest G4)
// Memory map:
//   [STUB_ADDR   .. +0x60)   stub code (96 bytes)
//   [CONFIG_ADDR .. +0x100)  config struct (20 bytes)
//   [BUFFER_ADDR .. SRAM_TOP) data buffer
// Stack (SP = SRAM_TOP) grows down into the buffer area.  The stub uses
// no stack (no PUSH/CALL/locals), so this overlap is safe.
namespace g4_flash_loader {
    constexpr uint32_t SRAM_BASE    = 0x20000000;
    constexpr uint32_t STUB_ADDR    = SRAM_BASE;               // stub code
    constexpr uint32_t CONFIG_ADDR  = SRAM_BASE + 0x80;        // config struct
    constexpr uint32_t BUFFER_ADDR  = SRAM_BASE + 0x100;       // data buffer
    constexpr uint32_t SRAM_MIN     = 32768;                    // 32 KB (smallest G4)
    constexpr uint32_t SRAM_TOP     = SRAM_BASE + SRAM_MIN;    // stack top (stub uses no stack)
    constexpr uint32_t BUFFER_SIZE  = SRAM_TOP - BUFFER_ADDR;  // 32512 bytes

    // Config struct status values
    constexpr uint32_t STATUS_RUNNING = 0;
    constexpr uint32_t STATUS_OK      = 1;
    constexpr uint32_t STATUS_ERROR   = 2;

    // Cortex-M4 Thumb-2 flash loader stub (96 bytes)
    // Uses standard PG mode (double-word programming), same approach as OpenOCD.
    // Speed comes from on-chip BSY polling (no SWD round-trip per double-word).
    // PG mode has no special prerequisites — works after any erase (mass or page).
    // Input: R0 = config struct address
    //   config[0]  flash_dest  — flash address to program (8-byte aligned)
    //   config[4]  data_src    — SRAM buffer address
    //   config[8]  data_size   — bytes to program (multiple of 8)
    //   config[12] status      — 0=running, 1=done_ok, 2=error
    //   config[16] flash_sr    — FLASH_SR value on error
    constexpr uint8_t stub[] = {
        // entry:
        0x72, 0xB6,             // CPSID  I                ; disable interrupts
        0x06, 0x46,             // MOV    R6, R0           ; save config addr
        0x31, 0x68,             // LDR    R1, [R6, #0]     ; flash_dest
        0x72, 0x68,             // LDR    R2, [R6, #4]     ; data_src
        0xB3, 0x68,             // LDR    R3, [R6, #8]     ; data_size
        0x12, 0x4C,             // LDR    R4, [PC, #0x48]  ; =FLASH_CR (0x40022014)
        0x12, 0x4D,             // LDR    R5, [PC, #0x48]  ; =FLASH_SR (0x40022010)
        0x01, 0x27,             // MOVS   R7, #1           ; CR_PG = bit 0
        0x27, 0x60,             // STR    R7, [R4, #0]     ; FLASH_CR = PG
        // dword_loop:
        0x00, 0x2B,             // CMP    R3, #0
        0x13, 0xD0,             // BEQ    done
        // copy double-word (8 bytes) — second STR triggers programming
        0x10, 0x68,             // LDR    R0, [R2, #0]     ; load low word
        0x08, 0x60,             // STR    R0, [R1, #0]     ; write low to flash
        0x50, 0x68,             // LDR    R0, [R2, #4]     ; load high word
        0x48, 0x60,             // STR    R0, [R1, #4]     ; write high to flash
        0xBF, 0xF3, 0x4F, 0x8F,// DSB    SY               ; ensure writes reach flash controller
        // wait_bsy:
        0x28, 0x68,             // LDR    R0, [R5, #0]     ; read FLASH_SR
        0x01, 0x27,             // MOVS   R7, #1           ; BSY (bit 16)
        0x3F, 0x04,             // LSLS   R7, R7, #16      ; R7 = 0x00010000
        0x38, 0x42,             // TST    R0, R7
        0xFA, 0xD1,             // BNE    wait_bsy
        // check errors
        0x0B, 0x4F,             // LDR    R7, [PC, #0x2C]  ; =ERR_MASK (0x0000C3FA)
        0x38, 0x42,             // TST    R0, R7
        0x0A, 0xD1,             // BNE    error
        // clear EOP + advance pointers
        0x01, 0x27,             // MOVS   R7, #1           ; SR_EOP = bit 0
        0x2F, 0x60,             // STR    R7, [R5, #0]     ; clear EOP (write-1-to-clear)
        0x08, 0x31,             // ADDS   R1, #8           ; flash_dest += 8
        0x08, 0x32,             // ADDS   R2, #8           ; data_src += 8
        0x08, 0x3B,             // SUBS   R3, #8           ; data_size -= 8
        0xE9, 0xE7,             // B      dword_loop
        // done:
        0x00, 0x20,             // MOVS   R0, #0
        0x20, 0x60,             // STR    R0, [R4, #0]     ; clear PG
        0x01, 0x20,             // MOVS   R0, #1
        0xF0, 0x60,             // STR    R0, [R6, #12]    ; status = OK
        0x00, 0xBE,             // BKPT   #0
        // error:
        0x30, 0x61,             // STR    R0, [R6, #16]    ; flash_sr = FLASH_SR value
        0x00, 0x20,             // MOVS   R0, #0
        0x20, 0x60,             // STR    R0, [R4, #0]     ; clear PG
        0x02, 0x20,             // MOVS   R0, #2
        0xF0, 0x60,             // STR    R0, [R6, #12]    ; status = ERROR
        0x00, 0xBE,             // BKPT   #0
        // literal pool:
        0x14, 0x20, 0x02, 0x40, // FLASH_CR  = 0x40022014
        0x10, 0x20, 0x02, 0x40, // FLASH_SR  = 0x40022010
        0xFA, 0xC3, 0x00, 0x00, // ERR_MASK  = 0x0000C3FA (SR_ALL_ERRORS)
    };
    constexpr uint32_t STUB_SIZE = sizeof(stub);
}

class Stm32G4FlashDriver : public FlashDriver {
public:
    RdpLevel         readRdpLevel(Transport& transport) override;
    ProgrammerStatus eraseFlash(Transport& transport) override;
    ProgrammerStatus writeFlash(Transport& transport, const uint8_t* data,
                                uint32_t address, uint32_t size) override;
    ProgrammerStatus writeOptionBytes(Transport& transport, const uint8_t* data,
                                      uint32_t address, uint32_t size, bool unsafe) override;
    ProgrammerStatus writeOptionBytesMapped(Transport& transport,
                                             const ObWriteEntry* entries, size_t count,
                                             bool unsafe) override;
    ProgrammerStatus writeOtp(Transport& transport, const uint8_t* data,
                              uint32_t address, uint32_t size) override;

private:
    ProgrammerStatus unlock(Transport& transport);
    ProgrammerStatus lock(Transport& transport);
    ProgrammerStatus unlockOptionBytes(Transport& transport);
    ProgrammerStatus waitReady(Transport& transport);
    ProgrammerStatus erasePage(Transport& transport, uint32_t page_num, bool bank2 = false);
    ProgrammerStatus programDoubleWord(Transport& transport, uint32_t address,
                                       uint32_t word_lo, uint32_t word_hi);
    ProgrammerStatus writeFlashSlow(Transport& transport, const uint8_t* data,
                                     uint32_t address, uint32_t size);
    ProgrammerStatus writeFlashFast(Transport& transport, const uint8_t* data,
                                     uint32_t address, uint32_t size);
};
