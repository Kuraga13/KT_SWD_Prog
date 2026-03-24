/**
 ******************************************************************************
 * @file           : stm32f4_flash.h
 * @brief          : Flash driver for STM32F4 series
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
 * Flash driver for STM32F4 series (Cortex-M4F).
 * Sector-based flash with configurable programming width
 * (byte / half-word / word / double-word via PSIZE).
 * Sectors: 4x16KB + 1x64KB + Nx128KB.
 *
 * Features:
 * - Sector erase and mass erase
 * - Configurable programming parallelism (PSIZE)
 * - Option byte read/write with RDP and PCROP support
 * - OTP area access (512 bytes + 16 lock bytes)
 * - Dual-bank support on high-density parts (F42x/F43x)
 *
 * Usage Example:
 * ```cpp
 * Stm32F4FlashDriver flash_driver;
 * Stm32Programmer programmer(transport, flash_driver);
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "../stm32_programmer.h"

namespace stm32f4_flash {
    constexpr uint32_t FLASH_BASE       = 0x40023C00;

    constexpr uint32_t FLASH_ACR        = FLASH_BASE + 0x00;
    constexpr uint32_t FLASH_KEYR       = FLASH_BASE + 0x04;
    constexpr uint32_t FLASH_OPTKEYR    = FLASH_BASE + 0x08;
    constexpr uint32_t FLASH_SR         = FLASH_BASE + 0x0C;
    constexpr uint32_t FLASH_CR         = FLASH_BASE + 0x10;
    constexpr uint32_t FLASH_OPTCR      = FLASH_BASE + 0x14;
    constexpr uint32_t FLASH_OPTCR1     = FLASH_BASE + 0x18;

    constexpr uint32_t KEY1             = 0x45670123;
    constexpr uint32_t KEY2             = 0xCDEF89AB;
    constexpr uint32_t OPT_KEY1         = 0x08192A3B;
    constexpr uint32_t OPT_KEY2         = 0x4C5D6E7F;

    // FLASH_CR bits
    constexpr uint32_t CR_PG            = (1UL << 0);
    constexpr uint32_t CR_SER           = (1UL << 1);
    constexpr uint32_t CR_MER           = (1UL << 2);
    constexpr uint32_t CR_MER1          = (1UL << 15);  // mass erase bank 2 (dual-bank)
    constexpr uint32_t CR_SNB_SHIFT     = 3;
    constexpr uint32_t CR_SNB_MASK      = 0x1F;
    constexpr uint32_t CR_PSIZE_X8      = (0UL << 8);
    constexpr uint32_t CR_PSIZE_X16     = (1UL << 8);
    constexpr uint32_t CR_PSIZE_X32     = (2UL << 8);
    constexpr uint32_t CR_PSIZE_X64     = (3UL << 8);
    constexpr uint32_t CR_STRT          = (1UL << 16);
    constexpr uint32_t CR_LOCK          = (1UL << 31);

    // FLASH_SR bits
    constexpr uint32_t SR_BSY           = (1UL << 16);
    constexpr uint32_t SR_PGSERR        = (1UL << 7);
    constexpr uint32_t SR_PGPERR        = (1UL << 6);
    constexpr uint32_t SR_PGAERR        = (1UL << 5);
    constexpr uint32_t SR_WRPERR        = (1UL << 4);
    constexpr uint32_t SR_OPERR         = (1UL << 1);
    constexpr uint32_t SR_EOP           = (1UL << 0);

    constexpr uint32_t FLASH_START      = 0x08000000;
    constexpr uint32_t OB_BASE          = 0x1FFFC000;
    constexpr uint32_t OB_SIZE          = 16;
    constexpr uint32_t OTP_BASE         = 0x1FFF7800;
    constexpr uint32_t OTP_SIZE         = 528;

    constexpr uint32_t PROG_WIDTH       = 4;             // word (32-bit) default

    // Flash size register (16-bit, value in KB)
    constexpr uint32_t FLASH_SIZE_REG   = 0x1FFF7A22;

    // Sector layout: 4x16KB + 1x64KB + 7x128KB per bank (12 sectors per bank)
    constexpr uint8_t  SECTORS_PER_BANK = 12;
    inline uint32_t sectorSize(uint8_t sector) {
        uint8_t rel = sector % SECTORS_PER_BANK;         // bank-relative
        if (rel < 4) return 16 * 1024;                   // 16 KB
        if (rel == 4) return 64 * 1024;                  // 64 KB
        return 128 * 1024;                               // 128 KB
    }

    // FLASH_SR error mask (all error bits)
    constexpr uint32_t SR_ERR_MASK      = SR_PGSERR | SR_PGPERR | SR_PGAERR
                                        | SR_WRPERR | SR_OPERR;

    // FLASH_OPTCR bits
    constexpr uint32_t OPTCR_OPTLOCK    = (1UL << 0);
    constexpr uint32_t OPTCR_OPTSTRT    = (1UL << 1);

    constexpr uint8_t  RDP_LEVEL_0      = 0xAA;
    constexpr uint8_t  RDP_LEVEL_2      = 0xCC;
}

// SRAM flash loader stub layout (conservative: 32 KB SRAM, smallest F4)
namespace f4_flash_loader {
    constexpr uint32_t SRAM_BASE    = 0x20000000;
    constexpr uint32_t STUB_ADDR    = SRAM_BASE;             // stub code
    constexpr uint32_t CONFIG_ADDR  = SRAM_BASE + 0x80;      // config struct
    constexpr uint32_t BUFFER_ADDR  = SRAM_BASE + 0x100;     // data buffer
    constexpr uint32_t SRAM_MIN     = 32768;                  // 32 KB (smallest F4)
    constexpr uint32_t SRAM_TOP     = SRAM_BASE + SRAM_MIN;  // stack top
    constexpr uint32_t BUFFER_SIZE  = SRAM_TOP - BUFFER_ADDR; // 32512 bytes

    // Config struct status values
    constexpr uint32_t STATUS_RUNNING = 0;
    constexpr uint32_t STATUS_OK      = 1;
    constexpr uint32_t STATUS_ERROR   = 2;

    // Cortex-M4 Thumb flash loader stub (84 bytes)
    // Programs flash words using PG + PSIZE_X32 mode, then halts via BKPT.
    // Input: R0 = config struct address
    //   config[0]  flash_dest  — flash address to program
    //   config[4]  data_src    — SRAM buffer address
    //   config[8]  data_size   — bytes to program (multiple of 4)
    //   config[12] status      — 0=running, 1=done_ok, 2=error
    //   config[16] flash_sr    — FLASH_SR value on error
    constexpr uint8_t stub[] = {
        // entry:
        0x72, 0xB6,             // CPSID  I                ; disable interrupts
        0x06, 0x46,             // MOV    R6, R0           ; save config addr
        0x31, 0x68,             // LDR    R1, [R6, #0]     ; flash_dest
        0x72, 0x68,             // LDR    R2, [R6, #4]     ; data_src
        0xB3, 0x68,             // LDR    R3, [R6, #8]     ; data_size
        0x0F, 0x4C,             // LDR    R4, [PC, #60]    ; =FLASH_CR (0x40023C10)
        0x0F, 0x4D,             // LDR    R5, [PC, #60]    ; =FLASH_SR (0x40023C0C)
        0x10, 0x4F,             // LDR    R7, [PC, #64]    ; =PG_PSIZE (0x00000201)
        0x27, 0x60,             // STR    R7, [R4, #0]     ; FLASH_CR = PG | PSIZE_X32
        // word_loop:
        0x00, 0x2B,             // CMP    R3, #0
        0x0D, 0xD0,             // BEQ    done
        0x10, 0x68,             // LDR    R0, [R2, #0]     ; load word from SRAM
        0x08, 0x60,             // STR    R0, [R1, #0]     ; write word to flash
        // wait_bsy:
        0x28, 0x68,             // LDR    R0, [R5, #0]     ; read FLASH_SR
        0x01, 0x27,             // MOVS   R7, #1
        0x3F, 0x04,             // LSLS   R7, R7, #16      ; R7 = 0x00010000 (BSY)
        0x38, 0x42,             // TST    R0, R7
        0xFA, 0xD1,             // BNE    wait_bsy
        0xF2, 0x27,             // MOVS   R7, #0xF2        ; ERR_MASK
        0x38, 0x42,             // TST    R0, R7
        0x08, 0xD1,             // BNE    error
        0x04, 0x31,             // ADDS   R1, #4
        0x04, 0x32,             // ADDS   R2, #4
        0x04, 0x3B,             // SUBS   R3, #4
        0xEF, 0xE7,             // B      word_loop
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
        0x10, 0x3C, 0x02, 0x40, // FLASH_CR  = 0x40023C10
        0x0C, 0x3C, 0x02, 0x40, // FLASH_SR  = 0x40023C0C
        0x01, 0x02, 0x00, 0x00, // PG_PSIZE  = 0x00000201
    };
    constexpr uint32_t STUB_SIZE = sizeof(stub);
}

class Stm32F4FlashDriver : public FlashDriver {
public:
    ProgrammerStatus resetTarget(Transport& transport) override;

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
    ProgrammerStatus clearErrors(Transport& transport);
    ProgrammerStatus eraseSector(Transport& transport, uint8_t sector);
    ProgrammerStatus writeFlashSlow(Transport& transport, const uint8_t* data,
                                     uint32_t address, uint32_t size);
    ProgrammerStatus writeFlashFast(Transport& transport, const uint8_t* data,
                                     uint32_t address, uint32_t size);
};
