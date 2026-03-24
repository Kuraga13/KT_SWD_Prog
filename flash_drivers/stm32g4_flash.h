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
};
