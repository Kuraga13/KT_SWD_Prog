/**
  ******************************************************************************
  * @file           : stm32h7rs_flash.h
  * @brief          : Flash driver for STM32H7Rx/H7Sx series (RM0477)
  * @author         : Evgeny Kudryavtsev
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Kuraga Tech.
  * Licensed under the MIT License. See LICENSE file for details.
  *
  ******************************************************************************
  * @details
  *
  * Flash driver for STM32H7R3, H7R7, H7S3, H7S7 (RM0477, Cortex-M7).
  * Single-bank 64 KB boot flash with 128-bit (16-byte) flash word programming.
  *
  * This driver is SEPARATE from stm32h7_flash (RM0433) and stm32h7ab_flash
  * (RM0455) because the flash controller is fundamentally different:
  * - Register offsets changed: FLASH_CR at 0x010 (not 0x0C), SR at 0x014
  * - Error flags moved to separate FLASH_ISR register (0x024)
  * - Error clearing via FLASH_ICR (0x028), not via CCR
  * - Option byte layout completely redesigned (NVSR, OBW1SR, OBW2SR, etc.)
  * - No RDP — replaced by NVSTATE product lifecycle
  * - OPTKEYR at offset 0x100 (not 0x08)
  * - Single-bank only (8 sectors x 8 KB = 64 KB)
  * - 128-bit flash word programming (16 bytes at a time)
  * - Architecture closer to STM32H5 than to other STM32H7
  *
  * Features:
  * - Sector erase (8 KB sectors) and mass erase
  * - 128-bit flash word programming (16 bytes at a time)
  * - Single-bank architecture (no bank 2)
  * - Option byte read/write with NVSTATE protection
  * - OTP area access via PG_OTP mechanism
  *
  * Usage Example:
  * ```cpp
  * Stm32H7rsFlashDriver flash_driver;
  * Stm32Programmer programmer(transport, flash_driver);
  * ```
  *
  ******************************************************************************
  */

#pragma once

#include "../stm32_programmer.h"

namespace stm32h7rs_flash {
    // Flash controller base (single bank)
    constexpr uint32_t FLASH_BASE        = 0x52002000;

    // Flash registers (offsets differ from H74x/H7AB!)
    constexpr uint32_t FLASH_ACR         = FLASH_BASE + 0x000;
    constexpr uint32_t FLASH_KEYR        = FLASH_BASE + 0x004;
    constexpr uint32_t FLASH_CR          = FLASH_BASE + 0x010;  // 0x010, not 0x0C!
    constexpr uint32_t FLASH_SR          = FLASH_BASE + 0x014;  // 0x014, not 0x10!
    constexpr uint32_t FLASH_IER         = FLASH_BASE + 0x020;
    constexpr uint32_t FLASH_ISR         = FLASH_BASE + 0x024;  // error flags here, not in SR!
    constexpr uint32_t FLASH_ICR         = FLASH_BASE + 0x028;  // error clear register
    constexpr uint32_t FLASH_OPTKEYR     = FLASH_BASE + 0x100;  // 0x100, not 0x08!
    constexpr uint32_t FLASH_OPTCR       = FLASH_BASE + 0x104;

    // Option byte status registers (CUR=read-only, PRG=read-write)
    constexpr uint32_t FLASH_NVSR        = FLASH_BASE + 0x200;  // NVSTATE current
    constexpr uint32_t FLASH_NVSRP       = FLASH_BASE + 0x204;  // NVSTATE programming
    constexpr uint32_t FLASH_ROTSR       = FLASH_BASE + 0x208;  // RoT status current
    constexpr uint32_t FLASH_ROTSRP      = FLASH_BASE + 0x20C;  // RoT status programming
    constexpr uint32_t FLASH_OTPLSR      = FLASH_BASE + 0x210;  // OTP lock status current
    constexpr uint32_t FLASH_OTPLSRP     = FLASH_BASE + 0x214;  // OTP lock status programming
    constexpr uint32_t FLASH_WRPSR       = FLASH_BASE + 0x218;  // write protection current
    constexpr uint32_t FLASH_WRPSRP      = FLASH_BASE + 0x21C;  // write protection programming
    constexpr uint32_t FLASH_HDPSR       = FLASH_BASE + 0x230;  // HDP status current
    constexpr uint32_t FLASH_HDPSRP      = FLASH_BASE + 0x234;  // HDP status programming
    constexpr uint32_t FLASH_EPOCHSR     = FLASH_BASE + 0x250;  // epoch status current
    constexpr uint32_t FLASH_EPOCHSRP    = FLASH_BASE + 0x254;  // epoch status programming
    constexpr uint32_t FLASH_OBW1SR      = FLASH_BASE + 0x260;  // option byte word 1 current
    constexpr uint32_t FLASH_OBW1SRP     = FLASH_BASE + 0x264;  // option byte word 1 programming
    constexpr uint32_t FLASH_OBW2SR      = FLASH_BASE + 0x268;  // option byte word 2 current
    constexpr uint32_t FLASH_OBW2SRP     = FLASH_BASE + 0x26C;  // option byte word 2 programming

    constexpr uint32_t KEY1              = 0x45670123;
    constexpr uint32_t KEY2              = 0xCDEF89AB;
    constexpr uint32_t OPT_KEY1          = 0x08192A3B;
    constexpr uint32_t OPT_KEY2          = 0x4C5D6E7F;

    // FLASH_OPTCR bits
    constexpr uint32_t OPTCR_OPTLOCK     = (1UL << 0);   // option byte lock
    constexpr uint32_t OPTCR_PG_OPT      = (1UL << 1);   // program options (start option write)

    // FLASH_CR bits (RM0477 — different offsets and layout from RM0433/RM0455!)
    constexpr uint32_t CR_LOCK           = (1UL << 0);    // configuration lock
    constexpr uint32_t CR_PG             = (1UL << 1);    // program enable
    constexpr uint32_t CR_SER            = (1UL << 2);    // sector erase request
    constexpr uint32_t CR_BER            = (1UL << 3);    // bank erase request
    constexpr uint32_t CR_FW             = (1UL << 4);    // force write (partial flash word)
    constexpr uint32_t CR_START          = (1UL << 5);    // erase start (bit 5, not 7!)
    constexpr uint32_t CR_SSN_SHIFT      = 6;             // sector select number starts at bit 6
    constexpr uint32_t CR_SSN_MASK       = 0x07;          // 3-bit mask for 8 sectors (0-7)
    constexpr uint32_t CR_PG_OTP         = (1UL << 16);   // OTP program enable

    // FLASH_SR bits (status only — errors are in FLASH_ISR!)
    constexpr uint32_t SR_BUSY           = (1UL << 0);    // flash busy
    constexpr uint32_t SR_WBNE           = (1UL << 1);    // write buffer not empty
    constexpr uint32_t SR_QW             = (1UL << 2);    // wait queue flag

    // FLASH_ISR error flag bits (separate register from SR!)
    constexpr uint32_t ISR_EOPF          = (1UL << 16);   // end of program
    constexpr uint32_t ISR_WRPERRF       = (1UL << 17);   // write protection error
    constexpr uint32_t ISR_PGSERRF       = (1UL << 18);   // programming sequence error
    constexpr uint32_t ISR_STRBERRF      = (1UL << 19);   // strobe error
    constexpr uint32_t ISR_OBLERRF       = (1UL << 20);   // option byte loading error
    constexpr uint32_t ISR_INCERRF       = (1UL << 21);   // inconsistency error
    constexpr uint32_t ISR_RDSERRF       = (1UL << 24);   // read security error
    constexpr uint32_t ISR_SNECCERRF     = (1UL << 25);   // ECC single correction error
    constexpr uint32_t ISR_DBECCERRF     = (1UL << 26);   // ECC double detection error

    constexpr uint32_t ISR_ALL_ERRORS    = ISR_WRPERRF | ISR_PGSERRF | ISR_STRBERRF |
                                           ISR_OBLERRF | ISR_INCERRF | ISR_RDSERRF |
                                           ISR_SNECCERRF | ISR_DBECCERRF;

    constexpr uint32_t FLASH_START       = 0x08000000;
    constexpr uint32_t SECTOR_SIZE       = 8192;           // 8 KB
    constexpr uint32_t SECTOR_COUNT      = 8;              // 8 sectors = 64 KB
    constexpr uint32_t FLASH_TOTAL_SIZE  = SECTOR_SIZE * SECTOR_COUNT;  // 64 KB

    constexpr uint32_t OTP_BASE          = 0x08FFF000;
    constexpr uint32_t OTP_SIZE          = 1024;           // 1 KB (16 blocks of 64 bytes)
    constexpr uint32_t FLASH_SIZE_REG    = 0x08FFF80C;

    // Programming width: 128-bit flash word = 16 bytes
    constexpr uint32_t PROG_WIDTH        = 16;

    // NVSTATE values (replaces RDP on H7RS)
    constexpr uint8_t  NVSTATE_OPEN      = 0xB4;          // ~RDP Level 0: full debug access
    constexpr uint8_t  NVSTATE_CLOSED    = 0x51;          // ~RDP Level 1: debug locked, regression possible
}

class Stm32H7rsFlashDriver : public FlashDriver {
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
    ProgrammerStatus unlockFlash(Transport& transport);
    ProgrammerStatus lockFlash(Transport& transport);
    ProgrammerStatus unlockOptionBytes(Transport& transport);
    ProgrammerStatus clearErrors(Transport& transport);
    ProgrammerStatus waitReady(Transport& transport);
    ProgrammerStatus eraseSector(Transport& transport, uint8_t sector);
    ProgrammerStatus programFlashWord(Transport& transport, uint32_t address,
                                      const uint8_t* data);  // 16 bytes
};
