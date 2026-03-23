/**
 ******************************************************************************
 * @file           : stm32f7_flash.h
 * @brief          : Flash driver for STM32F7 series
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
 * Flash driver for STM32F7 series (Cortex-M7).
 * Sector-based flash similar to F4, with larger sectors
 * (32KB / 128KB / 256KB). Configurable programming width.
 *
 * Features:
 * - Sector erase and mass erase
 * - Configurable programming parallelism (PSIZE)
 * - Option byte read/write with RDP support
 * - OTP area access (1024 bytes + 16 lock bytes)
 * - Dual-bank support on some variants
 *
 * Usage Example:
 * ```cpp
 * Stm32F7FlashDriver flash_driver;
 * Stm32Programmer programmer(transport, flash_driver);
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "../stm32_programmer.h"

namespace stm32f7_flash {
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
    constexpr uint32_t CR_SNB_SHIFT     = 3;
    constexpr uint32_t CR_SNB_MASK      = 0x0F;
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
    constexpr uint32_t OB_BASE          = 0x1FFF0000;
    constexpr uint32_t OB_SIZE          = 32;
    constexpr uint32_t OTP_BASE         = 0x1FF07800;
    constexpr uint32_t OTP_SIZE         = 1040;          // 1024 + 16 lock bytes

    constexpr uint32_t PROG_WIDTH       = 4;

    constexpr uint8_t  RDP_LEVEL_0      = 0xAA;
    constexpr uint8_t  RDP_LEVEL_2      = 0xCC;

    // FLASH_OPTCR bits
    constexpr uint32_t OPTCR_OPTLOCK    = (1UL << 0);   // option lock
    constexpr uint32_t OPTCR_OPTSTRT    = (1UL << 1);   // option start
}

class Stm32F7FlashDriver : public FlashDriver {
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
