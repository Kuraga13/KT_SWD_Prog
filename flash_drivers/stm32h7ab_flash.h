/**
 ******************************************************************************
 * @file           : stm32h7ab_flash.h
 * @brief          : Flash driver for STM32H7Ax/H7Bx series (RM0455)
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
 * Flash driver for STM32H7A3, H7B0, H7B3 (RM0455, Cortex-M7).
 * Sector-based flash with 128-bit (16-byte) flash word programming.
 * Dual-bank architecture with independent flash controllers per bank.
 *
 * This driver is SEPARATE from stm32h7_flash (RM0433) because the FLASH_CR
 * bit layout is fundamentally different:
 * - No PSIZE field (128-bit is the only programming width)
 * - START at bit 5 (not bit 7)
 * - FW at bit 4 (not bit 6)
 * - SNB at bits [12:6] with 7-bit mask (not [10:8] with 3-bit mask)
 * - 8 KB sectors (not 128 KB), up to 128 per bank
 *
 * Features:
 * - Sector erase (8 KB sectors) and mass erase per bank
 * - 128-bit flash word programming (16 bytes at a time)
 * - Dual-bank with independent controllers (bank 1 and bank 2)
 * - Option byte read/write with RDP, PCROP, secure area support
 * - OTP area access via PG_OTP mechanism
 *
 * Usage Example:
 * ```cpp
 * Stm32H7abFlashDriver flash_driver;
 * Stm32Programmer programmer(transport, flash_driver);
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "../stm32_programmer.h"

namespace stm32h7ab_flash {
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
    constexpr uint32_t OPTCR_PG_OTP     = (1UL << 5);   // OTP program enable

    // FLASH_CR bits (RM0455 — different from RM0433!)
    constexpr uint32_t CR_LOCK          = (1UL << 0);
    constexpr uint32_t CR_PG            = (1UL << 1);    // program
    constexpr uint32_t CR_SER           = (1UL << 2);    // sector erase
    constexpr uint32_t CR_BER           = (1UL << 3);    // bank erase
    constexpr uint32_t CR_FW            = (1UL << 4);    // force write (bit 4, not 6!)
    constexpr uint32_t CR_START         = (1UL << 5);    // start (bit 5, not 7!)
    constexpr uint32_t CR_SNB_SHIFT     = 6;             // sector number starts at bit 6 (not 8!)
    constexpr uint32_t CR_SNB_MASK      = 0x7F;          // 7-bit mask for 128 sectors (not 3-bit!)
    // No CR_PSIZE — 128-bit is the only supported programming width

    // FLASH_SR bits (same as RM0433 except bit 22 OPERR is reserved)
    constexpr uint32_t SR_BSY           = (1UL << 0);
    constexpr uint32_t SR_WBNE          = (1UL << 1);    // write buffer not empty
    constexpr uint32_t SR_QW            = (1UL << 2);    // wait for data phase queue
    constexpr uint32_t SR_WRPERR        = (1UL << 17);
    constexpr uint32_t SR_PGSERR        = (1UL << 18);
    constexpr uint32_t SR_STRBERR       = (1UL << 19);   // strobe error
    constexpr uint32_t SR_INCERR        = (1UL << 21);   // inconsistency error
    // bit 22 (OPERR) is reserved on RM0455 — not defined here
    constexpr uint32_t SR_RDPERR        = (1UL << 23);
    constexpr uint32_t SR_RDSERR        = (1UL << 24);
    constexpr uint32_t SR_SNECCERR      = (1UL << 25);
    constexpr uint32_t SR_DBECCERR      = (1UL << 26);
    constexpr uint32_t SR_EOP           = (1UL << 16);

    constexpr uint32_t SR_ALL_ERRORS    = SR_WRPERR | SR_PGSERR | SR_STRBERR |
                                          SR_INCERR | SR_RDPERR |
                                          SR_RDSERR | SR_SNECCERR | SR_DBECCERR;

    constexpr uint32_t FLASH_START      = 0x08000000;
    constexpr uint32_t SECTOR_SIZE      = 8192;           // 8 KB
    constexpr uint32_t BANK2_START      = 0x08100000;
    // Option bytes: non-contiguous CUR/PRG registers (same layout as H7)
    constexpr uint32_t OB_BASE          = 0x5200201C;     // OPTSR_CUR
    constexpr uint32_t OTP_BASE         = 0x08FFF000;
    constexpr uint32_t OTP_SIZE         = 1024;
    constexpr uint32_t FLASH_SIZE_REG   = 0x08FFF80C;     // different from H743!

    // Programming width: 128-bit flash word = 16 bytes
    constexpr uint32_t PROG_WIDTH       = 16;

    constexpr uint8_t  RDP_LEVEL_0      = 0xAA;
    constexpr uint8_t  RDP_LEVEL_2      = 0xCC;
}

class Stm32H7abFlashDriver : public FlashDriver {
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
    ProgrammerStatus unlockBank(Transport& transport, uint32_t keyr, uint32_t cr);
    ProgrammerStatus lockBank(Transport& transport, uint32_t cr);
    ProgrammerStatus unlockOptionBytes(Transport& transport);
    ProgrammerStatus clearErrors(Transport& transport, uint32_t ccr);
    ProgrammerStatus waitReady(Transport& transport, uint32_t sr);
    ProgrammerStatus eraseSector(Transport& transport, uint8_t sector,
                                  uint32_t cr, uint32_t sr);
    ProgrammerStatus programFlashWord(Transport& transport, uint32_t address,
                                      const uint8_t* data);  // 16 bytes
};
