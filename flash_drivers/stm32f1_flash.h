/**
 ******************************************************************************
 * @file           : stm32f1_flash.h
 * @brief          : Flash driver for STM32F1 series
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
 * Flash driver for STM32F1 series (Cortex-M3).
 * Page-based flash with half-word (16-bit) programming.
 * Supports low/medium/high/XL density variants with different page sizes.
 *
 * Features:
 * - Page erase and mass erase
 * - Half-word flash programming
 * - Option byte read/write with RDP support
 * - Support for 1 KB pages (low/medium) and 2 KB pages (high/XL density)
 * - SRAM flash loader stub for accelerated writes (bank 1 only)
 *
 * Usage Example:
 * ```cpp
 * Stm32F1FlashDriver flash_driver;
 * Stm32Programmer programmer(transport, flash_driver);
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "../stm32_programmer.h"

namespace stm32f1_flash {
    constexpr uint32_t FLASH_BASE       = 0x40022000;

    constexpr uint32_t FLASH_ACR        = FLASH_BASE + 0x00;
    constexpr uint32_t FLASH_KEYR       = FLASH_BASE + 0x04;
    constexpr uint32_t FLASH_OPTKEYR    = FLASH_BASE + 0x08;
    constexpr uint32_t FLASH_SR         = FLASH_BASE + 0x0C;
    constexpr uint32_t FLASH_CR         = FLASH_BASE + 0x10;
    constexpr uint32_t FLASH_AR         = FLASH_BASE + 0x14;
    constexpr uint32_t FLASH_OBR        = FLASH_BASE + 0x1C;
    constexpr uint32_t FLASH_WRPR       = FLASH_BASE + 0x20;

    // Bank 2 registers (XL-density, addresses >= 0x08080000)
    constexpr uint32_t FLASH_KEYR2      = FLASH_BASE + 0x44;
    constexpr uint32_t FLASH_SR2        = FLASH_BASE + 0x4C;
    constexpr uint32_t FLASH_CR2        = FLASH_BASE + 0x50;
    constexpr uint32_t FLASH_AR2        = FLASH_BASE + 0x54;

    constexpr uint32_t KEY1             = 0x45670123;
    constexpr uint32_t KEY2             = 0xCDEF89AB;
    constexpr uint32_t OPT_KEY1         = 0x45670123;
    constexpr uint32_t OPT_KEY2         = 0xCDEF89AB;

    // FLASH_CR bits
    constexpr uint32_t CR_PG            = (1UL << 0);
    constexpr uint32_t CR_PER           = (1UL << 1);
    constexpr uint32_t CR_MER           = (1UL << 2);
    constexpr uint32_t CR_OPTPG         = (1UL << 4);
    constexpr uint32_t CR_OPTER         = (1UL << 5);
    constexpr uint32_t CR_STRT          = (1UL << 6);
    constexpr uint32_t CR_LOCK          = (1UL << 7);
    constexpr uint32_t CR_OPTWRE        = (1UL << 9);

    // FLASH_SR bits
    constexpr uint32_t SR_BSY           = (1UL << 0);
    constexpr uint32_t SR_PGERR         = (1UL << 2);
    constexpr uint32_t SR_WRPRTERR      = (1UL << 4);
    constexpr uint32_t SR_EOP           = (1UL << 5);

    constexpr uint32_t FLASH_START      = 0x08000000;
    constexpr uint32_t BANK2_START      = 0x08080000;     // Bank 2 start (XL-density)
    constexpr uint32_t PAGE_SIZE_LOW    = 1024;          // 1 KB (low/medium density)
    constexpr uint32_t PAGE_SIZE_HIGH   = 2048;          // 2 KB (high/XL density)
    constexpr uint32_t OB_BASE          = 0x1FFFF800;
    constexpr uint32_t OB_SIZE          = 16;
    constexpr uint32_t OTP_BASE         = 0;              // F1 has no OTP
    constexpr uint32_t OTP_SIZE         = 0;

    constexpr uint32_t PROG_WIDTH       = 2;             // half-word (16-bit)

    constexpr uint8_t  RDP_LEVEL_0      = 0xA5;
    constexpr uint8_t  RDP_LEVEL_2      = 0xCC;
}

// SRAM flash loader stub layout (conservative: 8 KB SRAM, smallest supported F1)
namespace f1_flash_loader {
    constexpr uint32_t SRAM_BASE    = 0x20000000;
    constexpr uint32_t STUB_ADDR    = SRAM_BASE;              // stub code
    constexpr uint32_t CONFIG_ADDR  = SRAM_BASE + 0x80;       // config struct
    constexpr uint32_t BUFFER_ADDR  = SRAM_BASE + 0x100;      // data buffer
    constexpr uint32_t SRAM_MIN     = 8192;                    // 8 KB (smallest supported F1)
    constexpr uint32_t SRAM_TOP     = SRAM_BASE + SRAM_MIN;   // stack top
    constexpr uint32_t BUFFER_SIZE  = SRAM_TOP - BUFFER_ADDR;  // 7936 bytes

    // Config struct status values
    constexpr uint32_t STATUS_RUNNING = 0;
    constexpr uint32_t STATUS_OK      = 1;
    constexpr uint32_t STATUS_ERROR   = 2;

    // Cortex-M3 Thumb flash loader stub (80 bytes)
    // Programs flash half-words using PG mode, then halts via BKPT.
    // Input: R0 = config struct address
    //   config[0]  flash_dest  — flash address to program
    //   config[4]  data_src    — SRAM buffer address
    //   config[8]  data_size   — bytes to program (multiple of 2)
    //   config[12] status      — 0=running, 1=done_ok, 2=error
    //   config[16] flash_sr    — FLASH_SR value on error
    constexpr uint8_t stub[] = {
        // entry:
        0x72, 0xB6,             // CPSID  I                ; disable interrupts
        0x06, 0x46,             // MOV    R6, R0           ; save config addr
        0x31, 0x68,             // LDR    R1, [R6, #0]     ; flash_dest
        0x72, 0x68,             // LDR    R2, [R6, #4]     ; data_src
        0xB3, 0x68,             // LDR    R3, [R6, #8]     ; data_size
        0x0F, 0x4C,             // LDR    R4, [PC, #60]    ; =FLASH_CR (0x40022010)
        0x0F, 0x4D,             // LDR    R5, [PC, #60]    ; =FLASH_SR (0x4002200C)
        0x01, 0x27,             // MOVS   R7, #1           ; CR_PG
        0x27, 0x60,             // STR    R7, [R4, #0]     ; FLASH_CR = PG
        // halfword_loop:
        0x00, 0x2B,             // CMP    R3, #0
        0x0C, 0xD0,             // BEQ    done
        0x10, 0x88,             // LDRH   R0, [R2, #0]     ; load halfword from SRAM
        0x08, 0x80,             // STRH   R0, [R1, #0]     ; write halfword to flash
        // wait_bsy:
        0x28, 0x68,             // LDR    R0, [R5, #0]     ; read FLASH_SR
        0x01, 0x27,             // MOVS   R7, #1           ; BSY mask (bit 0)
        0x38, 0x42,             // TST    R0, R7
        0xFB, 0xD1,             // BNE    wait_bsy
        0x14, 0x27,             // MOVS   R7, #0x14        ; PGERR | WRPRTERR
        0x38, 0x42,             // TST    R0, R7
        0x08, 0xD1,             // BNE    error
        0x02, 0x31,             // ADDS   R1, #2           ; dest += 2
        0x02, 0x32,             // ADDS   R2, #2           ; src += 2
        0x02, 0x3B,             // SUBS   R3, #2           ; size -= 2
        0xF0, 0xE7,             // B      halfword_loop
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
        // (2 bytes padding for word-aligned literal pool)
        0x00, 0x00,
        // literal pool:
        0x10, 0x20, 0x02, 0x40, // FLASH_CR  = 0x40022010
        0x0C, 0x20, 0x02, 0x40, // FLASH_SR  = 0x4002200C
    };
    constexpr uint32_t STUB_SIZE = sizeof(stub);
}

class Stm32F1FlashDriver : public FlashDriver {
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
    // Bank-aware register selection (XL-density dual-bank support)
    static uint32_t keyrReg(uint32_t addr);
    static uint32_t srReg(uint32_t addr);
    static uint32_t crReg(uint32_t addr);
    static uint32_t arReg(uint32_t addr);

    ProgrammerStatus unlock(Transport& transport, uint32_t flash_addr = stm32f1_flash::FLASH_START);
    ProgrammerStatus lock(Transport& transport, uint32_t flash_addr = stm32f1_flash::FLASH_START);
    ProgrammerStatus unlockOptionBytes(Transport& transport);
    ProgrammerStatus waitReady(Transport& transport, uint32_t flash_addr = stm32f1_flash::FLASH_START);
    ProgrammerStatus clearErrors(Transport& transport, uint32_t flash_addr = stm32f1_flash::FLASH_START);
    ProgrammerStatus erasePage(Transport& transport, uint32_t address);
    ProgrammerStatus writeFlashSlow(Transport& transport, const uint8_t* data,
                                     uint32_t address, uint32_t size);
    ProgrammerStatus writeFlashFast(Transport& transport, const uint8_t* data,
                                     uint32_t address, uint32_t size);
};
