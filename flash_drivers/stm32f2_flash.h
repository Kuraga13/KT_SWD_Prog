/**
 ******************************************************************************
 * @file           : stm32f2_flash.h
 * @brief          : Flash driver for STM32F2 series
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
 * Flash driver for STM32F2 series (Cortex-M3).
 * Sector-based flash with configurable programming width
 * (byte / half-word / word / double-word via PSIZE).
 *
 * Features:
 * - Sector erase and mass erase
 * - Configurable programming parallelism (PSIZE)
 * - Option byte read/write with RDP and PCROP support
 * - OTP area access (512 bytes + 16 lock bytes)
 *
 * Usage Example:
 * ```cpp
 * Stm32F2FlashDriver flash_driver;
 * Stm32Programmer programmer(transport, flash_driver);
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "../stm32_programmer.h"

namespace stm32f2_flash {
    constexpr uint32_t FLASH_BASE       = 0x40023C00;

    constexpr uint32_t FLASH_ACR        = FLASH_BASE + 0x00;
    constexpr uint32_t FLASH_KEYR       = FLASH_BASE + 0x04;
    constexpr uint32_t FLASH_OPTKEYR    = FLASH_BASE + 0x08;
    constexpr uint32_t FLASH_SR         = FLASH_BASE + 0x0C;
    constexpr uint32_t FLASH_CR         = FLASH_BASE + 0x10;
    constexpr uint32_t FLASH_OPTCR      = FLASH_BASE + 0x14;

    constexpr uint32_t KEY1             = 0x45670123;
    constexpr uint32_t KEY2             = 0xCDEF89AB;
    constexpr uint32_t OPT_KEY1         = 0x08192A3B;
    constexpr uint32_t OPT_KEY2         = 0x4C5D6E7F;

    // FLASH_CR bits
    constexpr uint32_t CR_PG            = (1UL << 0);   // programming
    constexpr uint32_t CR_SER           = (1UL << 1);   // sector erase
    constexpr uint32_t CR_MER           = (1UL << 2);   // mass erase
    constexpr uint32_t CR_SNB_SHIFT     = 3;             // sector number shift
    constexpr uint32_t CR_SNB_MASK      = 0x0F;          // sector number mask
    constexpr uint32_t CR_PSIZE_SHIFT   = 8;             // program size shift
    constexpr uint32_t CR_PSIZE_X8      = (0UL << 8);   // byte
    constexpr uint32_t CR_PSIZE_X16     = (1UL << 8);   // half-word
    constexpr uint32_t CR_PSIZE_X32     = (2UL << 8);   // word
    constexpr uint32_t CR_PSIZE_X64     = (3UL << 8);   // double-word
    constexpr uint32_t CR_STRT          = (1UL << 16);  // start
    constexpr uint32_t CR_LOCK          = (1UL << 31);  // lock

    // FLASH_SR bits
    constexpr uint32_t SR_BSY           = (1UL << 16);  // busy
    constexpr uint32_t SR_PGSERR        = (1UL << 7);   // programming sequence error
    constexpr uint32_t SR_PGPERR        = (1UL << 6);   // programming parallelism error
    constexpr uint32_t SR_PGAERR        = (1UL << 5);   // programming alignment error
    constexpr uint32_t SR_WRPERR        = (1UL << 4);   // write protection error
    constexpr uint32_t SR_OPERR         = (1UL << 1);   // operation error
    constexpr uint32_t SR_EOP           = (1UL << 0);   // end of operation

    constexpr uint32_t FLASH_START      = 0x08000000;
    constexpr uint32_t OB_BASE          = 0x1FFFC000;
    constexpr uint32_t OB_SIZE          = 16;
    constexpr uint32_t OTP_BASE         = 0x1FFF7800;
    constexpr uint32_t OTP_SIZE         = 528;           // 512 + 16 lock bytes

    constexpr uint32_t PROG_WIDTH       = 4;             // word (32-bit) default

    constexpr uint8_t  RDP_LEVEL_0      = 0xAA;
    constexpr uint8_t  RDP_LEVEL_2      = 0xCC;

    // FLASH_OPTCR bits
    constexpr uint32_t OPTCR_OPTLOCK    = (1UL << 0);   // option lock
    constexpr uint32_t OPTCR_OPTSTRT    = (1UL << 1);   // option start
}

class Stm32F2FlashDriver : public FlashDriver {
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
    ProgrammerStatus eraseSector(Transport& transport, uint8_t sector);
};
