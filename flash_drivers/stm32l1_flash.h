/**
 ******************************************************************************
 * @file           : stm32l1_flash.h
 * @brief          : Flash driver for STM32L1 series
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
 * Flash driver for STM32L1 series (Cortex-M3, ultra-low-power).
 * NVM with word (32-bit) programming and half-page (32-word) fast write.
 * Uses PEKEY + PRGKEY double-unlock sequence, similar to L0.
 *
 * Features:
 * - Page erase (256 bytes) and mass erase
 * - Word (32-bit) programming and half-page fast write
 * - Double-unlock sequence (PEKEY then PRGKEY)
 * - Option byte read/write with RDP support
 * - Data EEPROM area access
 *
 * Usage Example:
 * ```cpp
 * Stm32L1FlashDriver flash_driver;
 * Stm32Programmer programmer(transport, flash_driver);
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "../stm32_programmer.h"

namespace stm32l1_flash {
    constexpr uint32_t FLASH_BASE       = 0x40023C00;

    constexpr uint32_t FLASH_ACR        = FLASH_BASE + 0x00;
    constexpr uint32_t FLASH_PECR       = FLASH_BASE + 0x04;
    constexpr uint32_t FLASH_PDKEYR     = FLASH_BASE + 0x08;
    constexpr uint32_t FLASH_PEKEYR     = FLASH_BASE + 0x0C;
    constexpr uint32_t FLASH_PRGKEYR    = FLASH_BASE + 0x10;
    constexpr uint32_t FLASH_OPTKEYR    = FLASH_BASE + 0x14;
    constexpr uint32_t FLASH_SR         = FLASH_BASE + 0x18;
    constexpr uint32_t FLASH_OBR        = FLASH_BASE + 0x1C;
    constexpr uint32_t FLASH_WRPR1      = FLASH_BASE + 0x20;
    constexpr uint32_t FLASH_WRPR2      = FLASH_BASE + 0x80;
    constexpr uint32_t FLASH_WRPR3      = FLASH_BASE + 0x84;

    constexpr uint32_t PEKEY1           = 0x89ABCDEF;
    constexpr uint32_t PEKEY2           = 0x02030405;
    constexpr uint32_t PRGKEY1          = 0x8C9DAEBF;
    constexpr uint32_t PRGKEY2          = 0x13141516;
    constexpr uint32_t OPT_KEY1         = 0xFBEAD9C8;
    constexpr uint32_t OPT_KEY2         = 0x24252627;

    // FLASH_PECR bits
    constexpr uint32_t PECR_PELOCK      = (1UL << 0);
    constexpr uint32_t PECR_PRGLOCK     = (1UL << 1);
    constexpr uint32_t PECR_OPTLOCK     = (1UL << 2);
    constexpr uint32_t PECR_PROG        = (1UL << 3);
    constexpr uint32_t PECR_DATA        = (1UL << 4);
    constexpr uint32_t PECR_FIX         = (1UL << 8);
    constexpr uint32_t PECR_ERASE       = (1UL << 9);
    constexpr uint32_t PECR_FPRG        = (1UL << 10);
    constexpr uint32_t PECR_OBL_LAUNCH  = (1UL << 18);

    // FLASH_SR bits
    constexpr uint32_t SR_BSY           = (1UL << 0);
    constexpr uint32_t SR_EOP           = (1UL << 1);
    constexpr uint32_t SR_ENDHV         = (1UL << 2);
    constexpr uint32_t SR_WRPERR        = (1UL << 8);
    constexpr uint32_t SR_PGAERR        = (1UL << 9);
    constexpr uint32_t SR_SIZERR        = (1UL << 10);
    constexpr uint32_t SR_OPTVERR       = (1UL << 11);

    constexpr uint32_t SR_ALL_ERRORS    = SR_WRPERR | SR_PGAERR | SR_SIZERR | SR_OPTVERR;

    constexpr uint32_t FLASH_START      = 0x08000000;
    constexpr uint32_t PAGE_SIZE        = 256;           // 256 bytes
    constexpr uint32_t HALF_PAGE_WORDS  = 32;            // 32 words = 128 bytes
    constexpr uint32_t OB_BASE          = 0x1FF80000;
    constexpr uint32_t OB_SIZE          = 32;
    constexpr uint32_t OTP_BASE         = 0x1FF00000;
    constexpr uint32_t OTP_SIZE         = 0;
    constexpr uint32_t EEPROM_BASE      = 0x08080000;

    constexpr uint32_t PROG_WIDTH       = 4;             // word (32-bit)

    constexpr uint8_t  RDP_LEVEL_0      = 0xAA;
    constexpr uint8_t  RDP_LEVEL_2      = 0xCC;
}

class Stm32L1FlashDriver : public FlashDriver {
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
    ProgrammerStatus unlockPE(Transport& transport);
    ProgrammerStatus unlockProgram(Transport& transport);
    ProgrammerStatus unlockOptionBytes(Transport& transport);
    ProgrammerStatus lock(Transport& transport);
    ProgrammerStatus waitReady(Transport& transport);
    ProgrammerStatus erasePage(Transport& transport, uint32_t address);
};
