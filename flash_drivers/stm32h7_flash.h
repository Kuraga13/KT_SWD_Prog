/**
 ******************************************************************************
 * @file           : stm32h7_flash.h
 * @brief          : Flash driver for STM32H7 series
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
 * Flash driver for STM32H7 series (Cortex-M7, some dual-core M7+M4).
 * Sector-based flash with 256-bit (32-byte) flash word programming.
 * Dual-bank architecture with independent flash controllers per bank.
 *
 * Features:
 * - Sector erase (128 KB sectors) and mass erase per bank
 * - 256-bit flash word programming (32 bytes at a time)
 * - Dual-bank with independent controllers (bank 1 and bank 2)
 * - Option byte read/write with RDP, PCROP, secure area support
 * - No OTP on RM0433/RM0468 parts (OTP exists only on H7Ax/H7Bx, see stm32h7ab_flash)
 *
 * NOTE: This driver covers RM0433 (H742, H743, H750, H753) and RM0468
 * (H723, H733) parts with 256-bit flash word programming.  H7Ax/H7Bx
 * (RM0455) have a different flash controller and use stm32h7ab_flash.
 *
 * Usage Example:
 * ```cpp
 * Stm32H7FlashDriver flash_driver;
 * Stm32Programmer programmer(transport, flash_driver);
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "../stm32_programmer.h"

namespace stm32h7_flash {
    // Bank 1 flash controller
    constexpr uint32_t FLASH_BASE1      = 0x52002000;
    // Bank 2 flash controller
    constexpr uint32_t FLASH_BASE2      = 0x52002100;

    // Bank 1 registers
    constexpr uint32_t FLASH_ACR        = FLASH_BASE1 + 0x00;
    constexpr uint32_t FLASH_KEYR1      = FLASH_BASE1 + 0x04;
    constexpr uint32_t FLASH_OPTKEYR    = FLASH_BASE1 + 0x08;
    constexpr uint32_t FLASH_CR1        = FLASH_BASE1 + 0x0C;
    constexpr uint32_t FLASH_SR1        = FLASH_BASE1 + 0x10;
    constexpr uint32_t FLASH_CCR1       = FLASH_BASE1 + 0x14;
    constexpr uint32_t FLASH_OPTCR      = FLASH_BASE1 + 0x18;
    constexpr uint32_t FLASH_OPTSR_CUR  = FLASH_BASE1 + 0x1C;
    constexpr uint32_t FLASH_OPTSR_PRG  = FLASH_BASE1 + 0x20;
    constexpr uint32_t FLASH_OPTCCR     = FLASH_BASE1 + 0x24;

    // Bank 2 registers
    constexpr uint32_t FLASH_KEYR2      = FLASH_BASE2 + 0x04;
    constexpr uint32_t FLASH_CR2        = FLASH_BASE2 + 0x0C;
    constexpr uint32_t FLASH_SR2        = FLASH_BASE2 + 0x10;
    constexpr uint32_t FLASH_CCR2       = FLASH_BASE2 + 0x14;

    constexpr uint32_t KEY1             = 0x45670123;
    constexpr uint32_t KEY2             = 0xCDEF89AB;
    constexpr uint32_t OPT_KEY1         = 0x08192A3B;
    constexpr uint32_t OPT_KEY2         = 0x4C5D6E7F;

    // FLASH_OPTCR bits
    constexpr uint32_t OPTCR_OPTLOCK    = (1UL << 0);   // option byte lock
    constexpr uint32_t OPTCR_OPTSTRT    = (1UL << 1);   // option byte start
    // FLASH_CR bits
    constexpr uint32_t CR_LOCK          = (1UL << 0);
    constexpr uint32_t CR_PG            = (1UL << 1);
    constexpr uint32_t CR_SER           = (1UL << 2);
    constexpr uint32_t CR_BER           = (1UL << 3);   // bank erase
    constexpr uint32_t CR_PSIZE_SHIFT   = 4;
    constexpr uint32_t CR_PSIZE_X8      = (0UL << 4);
    constexpr uint32_t CR_PSIZE_X16     = (1UL << 4);
    constexpr uint32_t CR_PSIZE_X32     = (2UL << 4);
    constexpr uint32_t CR_PSIZE_X64     = (3UL << 4);
    constexpr uint32_t CR_FW            = (1UL << 6);   // force write
    constexpr uint32_t CR_START         = (1UL << 7);
    constexpr uint32_t CR_SNB_SHIFT     = 8;
    constexpr uint32_t CR_SNB_MASK      = 0x07;

    // FLASH_SR bits
    constexpr uint32_t SR_BSY           = (1UL << 0);
    constexpr uint32_t SR_WBNE          = (1UL << 1);   // write buffer not empty
    constexpr uint32_t SR_QW            = (1UL << 2);   // wait for data phase queue
    constexpr uint32_t SR_WRPERR        = (1UL << 17);
    constexpr uint32_t SR_PGSERR        = (1UL << 18);
    constexpr uint32_t SR_STRBERR       = (1UL << 19);  // strobe error
    constexpr uint32_t SR_INCERR        = (1UL << 21);  // inconsistency error
    constexpr uint32_t SR_OPERR         = (1UL << 22);
    constexpr uint32_t SR_RDPERR        = (1UL << 23);
    constexpr uint32_t SR_RDSERR        = (1UL << 24);
    constexpr uint32_t SR_SNECCERR      = (1UL << 25);
    constexpr uint32_t SR_DBECCERR      = (1UL << 26);
    constexpr uint32_t SR_EOP           = (1UL << 16);

    constexpr uint32_t SR_ALL_ERRORS    = SR_WRPERR | SR_PGSERR | SR_STRBERR |
                                          SR_INCERR | SR_OPERR | SR_RDPERR |
                                          SR_RDSERR | SR_SNECCERR | SR_DBECCERR;

    constexpr uint32_t FLASH_START      = 0x08000000;
    constexpr uint32_t SECTOR_SIZE      = 131072;        // 128 KB
    constexpr uint32_t BANK2_START      = 0x08100000;
    // Option bytes: OB registers are non-contiguous (CUR/PRG alternate,
    // bank 2 at separate base) — no single size covers them.
    // See h7_dump.ps1 / h7_upload.ps1 for the complete register set.
    constexpr uint32_t OB_BASE          = 0x5200201C;    // OPTSR_CUR
    // No OTP on H74x/H75x/H72x/H73x — OTP exists only on H7Ax/H7Bx (stm32h7ab_flash)
    constexpr uint32_t OTP_BASE         = 0;
    constexpr uint32_t OTP_SIZE         = 0;

    // Programming width: 256-bit flash word = 32 bytes
    constexpr uint32_t PROG_WIDTH       = 32;

    constexpr uint8_t  RDP_LEVEL_0      = 0xAA;
    constexpr uint8_t  RDP_LEVEL_2      = 0xCC;
}

// SRAM flash loader stub layout (128 KB DTCM, all supported H7 variants)
// Memory map:
//   [STUB_ADDR   .. +0x80)   stub code (80 bytes)
//   [CONFIG_ADDR .. +0x100)  config struct (28 bytes)
//   [BUFFER_ADDR .. SRAM_TOP) data buffer
// Stack (SP = SRAM_TOP) grows down into the buffer area.  The stub uses
// no stack (no PUSH/CALL/locals), so this overlap is safe.
namespace h7_flash_loader {
    constexpr uint32_t SRAM_BASE    = 0x20000000;              // DTCM-RAM (not cached)
    constexpr uint32_t STUB_ADDR    = SRAM_BASE;               // stub code
    constexpr uint32_t CONFIG_ADDR  = SRAM_BASE + 0x80;        // config struct
    constexpr uint32_t BUFFER_ADDR  = SRAM_BASE + 0x100;       // data buffer
    constexpr uint32_t SRAM_SIZE    = 131072;                   // 128 KB DTCM
    constexpr uint32_t SRAM_TOP     = SRAM_BASE + SRAM_SIZE;   // stack top (stub uses no stack)
    constexpr uint32_t BUFFER_SIZE  = SRAM_TOP - BUFFER_ADDR;  // 130816 bytes

    // Config struct status values
    constexpr uint32_t STATUS_RUNNING = 0;
    constexpr uint32_t STATUS_OK      = 1;
    constexpr uint32_t STATUS_ERROR   = 2;

    // Cortex-M7 Thumb-2 flash loader stub (80 bytes)
    // Programs 256-bit (32-byte) flash words using PG + PSIZE_X64 mode, then halts via BKPT.
    // Uses LDMIA/STMIA for efficient 32-byte bulk transfer.
    // Input: R0 = config struct address
    //   config[0]  flash_dest  — flash address to program
    //   config[4]  data_src    — SRAM buffer address
    //   config[8]  data_size   — bytes to program (multiple of 32)
    //   config[12] status      — 0=running, 1=done_ok, 2=error
    //   config[16] flash_sr    — FLASH_SR value on error
    //   config[20] cr_addr     — FLASH_CR register address (bank-specific)
    //   config[24] sr_addr     — FLASH_SR register address (bank-specific)
    constexpr uint8_t stub[] = {
        // entry:
        0x72, 0xB6,             // CPSID  I                ; disable interrupts
        0x06, 0x46,             // MOV    R6, R0           ; save config addr
        0x31, 0x68,             // LDR    R1, [R6, #0]     ; flash_dest
        0x72, 0x68,             // LDR    R2, [R6, #4]     ; data_src
        0xB3, 0x68,             // LDR    R3, [R6, #8]     ; data_size
        0x74, 0x69,             // LDR    R4, [R6, #20]    ; cr_addr
        0xB5, 0x69,             // LDR    R5, [R6, #24]    ; sr_addr
        // flash_word_loop:
        0x00, 0x2B,             // CMP    R3, #0
        0x13, 0xD0,             // BEQ    done
        0x32, 0x27,             // MOVS   R7, #0x32        ; CR_PG(1<<1) | CR_PSIZE_X64(3<<4)
        0x27, 0x60,             // STR    R7, [R4, #0]     ; FLASH_CR = PG | PSIZE_X64
        0xB2, 0xE8, 0x81, 0x5F,// LDMIA.W R2!, {R0,R7-R12,LR} ; load 32 bytes from SRAM
        0xA1, 0xE8, 0x81, 0x5F,// STMIA.W R1!, {R0,R7-R12,LR} ; store 32 bytes to flash
        0xBF, 0xF3, 0x4F, 0x8F,// DSB    SY               ; ensure writes reach flash controller
        // wait_bsy:
        0x28, 0x68,             // LDR    R0, [R5, #0]     ; read FLASH_SR
        0x05, 0x27,             // MOVS   R7, #5           ; BSY(1<<0) | QW(1<<2)
        0x38, 0x42,             // TST    R0, R7
        0xFB, 0xD1,             // BNE    wait_bsy
        0x40, 0xF2, 0x00, 0x07,// MOVW   R7, #0x0000      ; error mask low
        0xC0, 0xF2, 0xEE, 0x77,// MOVT   R7, #0x07EE      ; R7 = 0x07EE0000 (SR_ALL_ERRORS)
        0x38, 0x42,             // TST    R0, R7
        0x06, 0xD1,             // BNE    error
        0x20, 0x3B,             // SUBS   R3, #32          ; remaining -= 32
        0xE9, 0xE7,             // B      flash_word_loop
        // done:
        0x00, 0x20,             // MOVS   R0, #0
        0x20, 0x60,             // STR    R0, [R4, #0]     ; clear PG in CR
        0x01, 0x20,             // MOVS   R0, #1
        0xF0, 0x60,             // STR    R0, [R6, #12]    ; status = OK
        0x00, 0xBE,             // BKPT   #0
        // error:
        0x30, 0x61,             // STR    R0, [R6, #16]    ; flash_sr = SR value
        0x00, 0x20,             // MOVS   R0, #0
        0x20, 0x60,             // STR    R0, [R4, #0]     ; clear PG in CR
        0x02, 0x20,             // MOVS   R0, #2
        0xF0, 0x60,             // STR    R0, [R6, #12]    ; status = ERROR
        0x00, 0xBE,             // BKPT   #0
    };
    constexpr uint32_t STUB_SIZE = sizeof(stub);
}

class Stm32H7FlashDriver : public FlashDriver {
public:
    ProgrammerStatus resetTarget(Transport& transport) override;
    void             clearFlashErrors(Transport& transport) override;
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
    ProgrammerStatus unlockBank(Transport& transport, uint32_t keyr, uint32_t cr);
    ProgrammerStatus lockBank(Transport& transport, uint32_t cr);
    ProgrammerStatus unlockOptionBytes(Transport& transport);
    ProgrammerStatus clearErrors(Transport& transport, uint32_t ccr);
    ProgrammerStatus waitReady(Transport& transport, uint32_t sr);
    ProgrammerStatus eraseSector(Transport& transport, uint8_t sector, uint32_t cr, uint32_t sr);
    ProgrammerStatus programFlashWord(Transport& transport, uint32_t address,
                                      const uint8_t* data);  // 32 bytes
    ProgrammerStatus writeFlashSlow(Transport& transport, const uint8_t* data,
                                     uint32_t address, uint32_t size);
    ProgrammerStatus writeFlashFast(Transport& transport, const uint8_t* data,
                                     uint32_t address, uint32_t size);
};
