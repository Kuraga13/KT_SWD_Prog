/**
 ******************************************************************************
 * @file           : stm32f0_flash.h
 * @brief          : Flash driver for STM32F0 series
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
 * Flash driver for STM32F0 series (Cortex-M0).
 * Page-based flash with half-word (16-bit) programming.
 *
 * Features:
 * - Page erase and mass erase
 * - Half-word flash programming
 * - Option byte read/write with RDP support
 * - OTP area access
 *
 * Usage Example:
 * ```cpp
 * Stm32F0FlashDriver flash_driver;
 * Stm32Programmer programmer(transport, flash_driver);
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "../stm32_programmer.h"

namespace stm32f0_flash {
    // Flash controller base address
    constexpr uint32_t FLASH_BASE       = 0x40022000;

    // Flash controller registers
    constexpr uint32_t FLASH_ACR        = FLASH_BASE + 0x00;
    constexpr uint32_t FLASH_KEYR       = FLASH_BASE + 0x04;
    constexpr uint32_t FLASH_OPTKEYR    = FLASH_BASE + 0x08;
    constexpr uint32_t FLASH_SR         = FLASH_BASE + 0x0C;
    constexpr uint32_t FLASH_CR         = FLASH_BASE + 0x10;
    constexpr uint32_t FLASH_AR         = FLASH_BASE + 0x14;
    constexpr uint32_t FLASH_OBR        = FLASH_BASE + 0x1C;
    constexpr uint32_t FLASH_WRPR       = FLASH_BASE + 0x20;

    // Unlock keys
    constexpr uint32_t KEY1             = 0x45670123;
    constexpr uint32_t KEY2             = 0xCDEF89AB;
    constexpr uint32_t OPT_KEY1         = 0x45670123;
    constexpr uint32_t OPT_KEY2         = 0xCDEF89AB;

    // FLASH_CR bits
    constexpr uint32_t CR_PG            = (1UL << 0);   // programming
    constexpr uint32_t CR_PER           = (1UL << 1);   // page erase
    constexpr uint32_t CR_MER           = (1UL << 2);   // mass erase
    constexpr uint32_t CR_OPTPG         = (1UL << 4);   // option byte programming
    constexpr uint32_t CR_OPTER         = (1UL << 5);   // option byte erase
    constexpr uint32_t CR_STRT          = (1UL << 6);   // start
    constexpr uint32_t CR_LOCK          = (1UL << 7);   // lock
    constexpr uint32_t CR_OPTWRE        = (1UL << 9);   // option byte write enable
    constexpr uint32_t CR_OBL_LAUNCH    = (1UL << 13);  // option byte load launch

    // FLASH_SR bits
    constexpr uint32_t SR_BSY           = (1UL << 0);   // busy
    constexpr uint32_t SR_PGERR         = (1UL << 2);   // programming error
    constexpr uint32_t SR_WRPERR        = (1UL << 4);   // write protection error
    constexpr uint32_t SR_EOP           = (1UL << 5);   // end of operation

    // Memory layout
    constexpr uint32_t FLASH_START      = 0x08000000;
    constexpr uint32_t PAGE_SIZE        = 1024;          // 1 KB (some parts use 2 KB)
    constexpr uint32_t OB_BASE          = 0x1FFFF800;
    constexpr uint32_t OB_SIZE          = 16;
    constexpr uint32_t OTP_BASE         = 0x1FFFF800;    // F0 has limited OTP
    constexpr uint32_t OTP_SIZE         = 0;             // no dedicated OTP on most F0

    // Programming width
    constexpr uint32_t PROG_WIDTH       = 2;             // half-word (16-bit)

    // RDP values
    constexpr uint8_t  RDP_LEVEL_0      = 0xAA;
    constexpr uint8_t  RDP_LEVEL_2      = 0xCC;
}

class Stm32F0FlashDriver : public FlashDriver {
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
    ProgrammerStatus erasePage(Transport& transport, uint32_t address);
};
