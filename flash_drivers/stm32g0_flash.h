/**
 ******************************************************************************
 * @file           : stm32g0_flash.h
 * @brief          : Flash driver for STM32G0 series
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
 * Flash driver for STM32G0 series (Cortex-M0+).
 * Page-based flash with double-word (64-bit) programming.
 * Uses the newer "L4-style" flash controller shared by G0/G4/L4/L5.
 *
 * Features:
 * - Page erase (2 KB pages) and mass erase
 * - Double-word (64-bit) flash programming
 * - Option byte read/write with RDP, WRP, PCROP, and secure area support
 * - OTP area access (1 KB)
 * - Dual-bank support on G0B1/G0C1 variants
 *
 * Usage Example:
 * ```cpp
 * Stm32G0FlashDriver flash_driver;
 * Stm32Programmer programmer(transport, flash_driver);
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "../stm32_programmer.h"

namespace stm32g0_flash {
    constexpr uint32_t FLASH_BASE       = 0x40022000;

    constexpr uint32_t FLASH_ACR        = FLASH_BASE + 0x00;
    constexpr uint32_t FLASH_KEYR       = FLASH_BASE + 0x08;
    constexpr uint32_t FLASH_OPTKEYR    = FLASH_BASE + 0x0C;
    constexpr uint32_t FLASH_SR         = FLASH_BASE + 0x10;
    constexpr uint32_t FLASH_CR         = FLASH_BASE + 0x14;
    constexpr uint32_t FLASH_ECCR       = FLASH_BASE + 0x18;
    constexpr uint32_t FLASH_OPTR       = FLASH_BASE + 0x20;
    constexpr uint32_t FLASH_PCROP1ASR  = FLASH_BASE + 0x24;
    constexpr uint32_t FLASH_PCROP1AER  = FLASH_BASE + 0x28;
    constexpr uint32_t FLASH_WRP1AR     = FLASH_BASE + 0x2C;
    constexpr uint32_t FLASH_WRP1BR     = FLASH_BASE + 0x30;
    constexpr uint32_t FLASH_PCROP1BSR  = FLASH_BASE + 0x34;
    constexpr uint32_t FLASH_PCROP1BER  = FLASH_BASE + 0x38;
    constexpr uint32_t FLASH_SECR       = FLASH_BASE + 0x80;

    constexpr uint32_t KEY1             = 0x45670123;
    constexpr uint32_t KEY2             = 0xCDEF89AB;
    constexpr uint32_t OPT_KEY1        = 0x08192A3B;
    constexpr uint32_t OPT_KEY2        = 0x4C5D6E7F;

    // FLASH_CR bits
    constexpr uint32_t CR_PG            = (1UL << 0);   // programming
    constexpr uint32_t CR_PER           = (1UL << 1);   // page erase
    constexpr uint32_t CR_MER1          = (1UL << 2);   // mass erase bank 1
    constexpr uint32_t CR_PNB_SHIFT     = 3;            // page number shift
    constexpr uint32_t CR_PNB_MASK      = 0x3FF;        // page number mask (10 bits)
    constexpr uint32_t CR_BKER          = (1UL << 13);  // bank selection for erase (dual-bank)
    constexpr uint32_t CR_MER2          = (1UL << 15);  // mass erase bank 2 (dual-bank)
    constexpr uint32_t CR_STRT          = (1UL << 16);  // start
    constexpr uint32_t CR_OPTSTRT       = (1UL << 17);  // option byte start
    constexpr uint32_t CR_FSTPG         = (1UL << 18);  // fast programming
    constexpr uint32_t CR_OBL_LAUNCH    = (1UL << 27);  // option byte load launch
    constexpr uint32_t CR_OPTLOCK       = (1UL << 30);  // option byte lock
    constexpr uint32_t CR_LOCK          = (1UL << 31);  // flash lock

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
    constexpr uint32_t SR_BSY1          = (1UL << 16);  // busy bank 1
    constexpr uint32_t SR_BSY2          = (1UL << 17);  // busy bank 2 (dual-bank)
    constexpr uint32_t SR_CFGBSY        = (1UL << 18);  // config busy

    constexpr uint32_t SR_ALL_ERRORS    = SR_OPERR | SR_PROGERR | SR_WRPERR | SR_PGAERR |
                                          SR_SIZERR | SR_PGSERR | SR_MISERR | SR_FASTERR |
                                          SR_RDERR | SR_OPTVERR;

    // Memory layout
    constexpr uint32_t FLASH_START      = 0x08000000;
    constexpr uint32_t PAGE_SIZE        = 2048;          // 2 KB
    constexpr uint32_t OB_BASE          = 0x1FFF7800;
    constexpr uint32_t OB_SIZE          = 128;
    constexpr uint32_t OTP_BASE         = 0x1FFF7000;
    constexpr uint32_t OTP_SIZE         = 1024;

    // Programming
    constexpr uint32_t PROG_WIDTH       = 8;             // double-word (64-bit)
    constexpr uint32_t ROW_SIZE         = 256;           // fast programming row (32 double-words)

    // RDP values
    constexpr uint8_t  RDP_LEVEL_0      = 0xAA;
    constexpr uint8_t  RDP_LEVEL_2      = 0xCC;
}

// SRAM flash loader stub layout (conservative: 8 KB SRAM, smallest G0)
namespace g0_flash_loader {
    constexpr uint32_t SRAM_BASE    = 0x20000000;
    constexpr uint32_t STUB_ADDR    = SRAM_BASE;            // stub code
    constexpr uint32_t CONFIG_ADDR  = SRAM_BASE + 0x80;     // config struct
    constexpr uint32_t BUFFER_ADDR  = SRAM_BASE + 0x100;    // data buffer
    constexpr uint32_t SRAM_MIN     = 8192;                  // 8 KB (smallest G0)
    constexpr uint32_t SRAM_TOP     = SRAM_BASE + SRAM_MIN;  // stack top
    constexpr uint32_t BUFFER_SIZE  = SRAM_TOP - BUFFER_ADDR; // 7936 bytes (31 rows)

    // Config struct status values
    constexpr uint32_t STATUS_RUNNING = 0;
    constexpr uint32_t STATUS_OK      = 1;
    constexpr uint32_t STATUS_ERROR   = 2;

    // Cortex-M0+ Thumb-1 flash loader stub (96 bytes)
    // Programs flash rows using FSTPG mode, then halts via BKPT.
    // Input: R0 = config struct address
    //   config[0]  flash_dest  — flash address to program
    //   config[4]  data_src    — SRAM buffer address
    //   config[8]  data_size   — bytes to program (multiple of 256)
    //   config[12] status      — 0=running, 1=done_ok, 2=error
    //   config[16] flash_sr    — FLASH_SR value on error
    constexpr uint8_t stub[] = {
        // entry:
        0x72, 0xB6,             // CPSID  I                ; disable interrupts
        0x06, 0x46,             // MOV    R6, R0           ; save config addr
        0x31, 0x68,             // LDR    R1, [R6, #0]     ; flash_dest
        0x72, 0x68,             // LDR    R2, [R6, #4]     ; data_src
        0xB3, 0x68,             // LDR    R3, [R6, #8]     ; data_size
        0x11, 0x4C,             // LDR    R4, [PC, #0x44]  ; =FLASH_CR (0x40022014)
        0x11, 0x4D,             // LDR    R5, [PC, #0x44]  ; =FLASH_SR (0x40022010)
        0x12, 0x4F,             // LDR    R7, [PC, #0x48]  ; =CR_FSTPG (0x00040000)
        0x27, 0x60,             // STR    R7, [R4, #0]     ; FLASH_CR = FSTPG
        // row_loop:
        0x00, 0x2B,             // CMP    R3, #0
        0x11, 0xD0,             // BEQ    done
        0x40, 0x27,             // MOVS   R7, #64          ; 64 words = 256 bytes
        // word_copy:
        0x10, 0x68,             // LDR    R0, [R2, #0]
        0x08, 0x60,             // STR    R0, [R1, #0]
        0x04, 0x31,             // ADDS   R1, #4
        0x04, 0x32,             // ADDS   R2, #4
        0x01, 0x3F,             // SUBS   R7, #1
        0xF9, 0xD1,             // BNE    word_copy
        // wait_bsy:
        0x28, 0x68,             // LDR    R0, [R5, #0]     ; read FLASH_SR
        0x05, 0x27,             // MOVS   R7, #5
        0x3F, 0x04,             // LSLS   R7, R7, #16      ; R7 = 0x00050000 (BSY1|CFGBSY)
        0x38, 0x42,             // TST    R0, R7
        0xFA, 0xD1,             // BNE    wait_bsy
        0x0B, 0x4F,             // LDR    R7, [PC, #0x2C]  ; =ERR_MASK (0x000003F8)
        0x38, 0x42,             // TST    R0, R7
        0x07, 0xD1,             // BNE    error
        0x80, 0x3B,             // SUBS   R3, #128
        0x80, 0x3B,             // SUBS   R3, #128          ; R3 -= 256
        0xEB, 0xE7,             // B      row_loop
        // done:
        0x00, 0x20,             // MOVS   R0, #0
        0x20, 0x60,             // STR    R0, [R4, #0]     ; clear FSTPG
        0x01, 0x20,             // MOVS   R0, #1
        0xF0, 0x60,             // STR    R0, [R6, #12]    ; status = OK
        0x00, 0xBE,             // BKPT   #0
        // error:
        0x30, 0x61,             // STR    R0, [R6, #16]    ; flash_sr = FLASH_SR value
        0x00, 0x20,             // MOVS   R0, #0
        0x20, 0x60,             // STR    R0, [R4, #0]     ; clear FSTPG
        0x02, 0x20,             // MOVS   R0, #2
        0xF0, 0x60,             // STR    R0, [R6, #12]    ; status = ERROR
        0x00, 0xBE,             // BKPT   #0
        // literal pool:
        0x14, 0x20, 0x02, 0x40, // FLASH_CR  = 0x40022014
        0x10, 0x20, 0x02, 0x40, // FLASH_SR  = 0x40022010
        0x00, 0x00, 0x04, 0x00, // CR_FSTPG  = 0x00040000
        0xF8, 0x03, 0x00, 0x00, // ERR_MASK  = 0x000003F8
    };
    constexpr uint32_t STUB_SIZE = sizeof(stub);
}

class Stm32G0FlashDriver : public FlashDriver {
public:
    RdpLevel         readRdpLevel(Transport& transport) override;
    ProgrammerStatus eraseFlash(Transport& transport) override;
    ProgrammerStatus writeFlash(Transport& transport, const uint8_t* data,
                                uint32_t address, uint32_t size) override;
    ProgrammerStatus writeOptionBytes(Transport& transport, const uint8_t* data,
                                      uint32_t address, uint32_t size, bool unsafe) override;
    ProgrammerStatus writeOtp(Transport& transport, const uint8_t* data,
                              uint32_t address, uint32_t size) override;

private:
    ProgrammerStatus unlock(Transport& transport);
    ProgrammerStatus lock(Transport& transport);
    ProgrammerStatus unlockOptionBytes(Transport& transport);
    ProgrammerStatus waitReady(Transport& transport);
    ProgrammerStatus erasePage(Transport& transport, uint32_t page_num);
    ProgrammerStatus programDoubleWord(Transport& transport, uint32_t address,
                                       uint32_t word_lo, uint32_t word_hi);
    ProgrammerStatus writeFlashSlow(Transport& transport, const uint8_t* data,
                                     uint32_t address, uint32_t size);
    ProgrammerStatus writeFlashFast(Transport& transport, const uint8_t* data,
                                     uint32_t address, uint32_t size);
};
