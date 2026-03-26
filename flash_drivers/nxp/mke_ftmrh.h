/**
 ******************************************************************************
 * @file           : mke_ftmrh.h
 * @brief          : Target driver for NXP Kinetis KE02/KE04/KE06 (FTMRH flash)
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
 * Target driver for older NXP Kinetis KE series (MKE02, MKE04, MKE06)
 * which use the FTMRH flash controller. These are 5V-tolerant Cortex-M0+
 * parts designed for harsh environments.
 *
 * The FTMRH flash controller uses a command-based programming model:
 * write command code, address, and data to FCCOB registers, then
 * launch by clearing CCIF in FSTAT. This is fundamentally different
 * from STM32's register-bit approach.
 *
 * Features:
 * - Command-based flash programming via FCCOB registers
 * - Sector erase (512 bytes) and mass erase
 * - Byte/word flash programming
 * - Watchdog (WDOG) auto-disable on connect
 * - Flash security byte read for protection detection
 * - SIM_SDID-based chip identification
 *
 * Usage Example:
 * ```cpp
 * MkeFtmrhTargetDriver target_driver;
 * Stm32Programmer programmer(transport, target_driver);
 * programmer.connect();  // auto-disables watchdog
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "../../stm32_programmer.h"

namespace mke_ftmrh {
    // FTMRH flash controller base address
    constexpr uint32_t FTMRH_BASE       = 0x40020000;

    // FTMRH registers
    constexpr uint32_t FTMRH_FCLKDIV    = FTMRH_BASE + 0x00;  // clock divider
    constexpr uint32_t FTMRH_FSEC       = FTMRH_BASE + 0x01;  // security
    constexpr uint32_t FTMRH_FCCOBIX    = FTMRH_BASE + 0x02;  // command index
    constexpr uint32_t FTMRH_FCNFG      = FTMRH_BASE + 0x04;  // configuration
    constexpr uint32_t FTMRH_FERCNFG    = FTMRH_BASE + 0x05;  // error config
    constexpr uint32_t FTMRH_FSTAT      = FTMRH_BASE + 0x06;  // status
    constexpr uint32_t FTMRH_FERSTAT    = FTMRH_BASE + 0x07;  // error status
    constexpr uint32_t FTMRH_FPROT      = FTMRH_BASE + 0x08;  // protection
    constexpr uint32_t FTMRH_EEPROT     = FTMRH_BASE + 0x09;  // EEPROM protection
    constexpr uint32_t FTMRH_FCCOBHI    = FTMRH_BASE + 0x0A;  // command object high
    constexpr uint32_t FTMRH_FCCOBLO    = FTMRH_BASE + 0x0B;  // command object low
    constexpr uint32_t FTMRH_FOPT       = FTMRH_BASE + 0x0C;  // flash option

    // FSTAT bits
    constexpr uint8_t FSTAT_CCIF        = (1 << 7);  // command complete
    constexpr uint8_t FSTAT_ACCERR      = (1 << 5);  // access error
    constexpr uint8_t FSTAT_FPVIOL      = (1 << 4);  // protection violation
    constexpr uint8_t FSTAT_MGBUSY      = (1 << 3);  // memory controller busy
    constexpr uint8_t FSTAT_MGSTAT1     = (1 << 1);  // command completion status
    constexpr uint8_t FSTAT_MGSTAT0     = (1 << 0);  // command completion status

    // FSEC bits
    constexpr uint8_t FSEC_SEC_MASK     = 0x03;
    constexpr uint8_t FSEC_UNSECURED    = 0x02;      // chip is unsecured

    // Flash commands
    constexpr uint8_t CMD_ERASE_VERIFY  = 0x01;  // erase verify all blocks
    constexpr uint8_t CMD_ERASE_ALL     = 0x08;  // erase all blocks
    constexpr uint8_t CMD_ERASE_SECTOR  = 0x0A;  // erase flash sector
    constexpr uint8_t CMD_PROGRAM       = 0x06;  // program flash
    constexpr uint8_t CMD_SET_USER_MARGIN = 0x0D; // set user margin level

    // Watchdog (WDOG) registers — must be disabled on connect
    constexpr uint32_t WDOG_BASE        = 0x40052000;
    constexpr uint32_t WDOG_CS1         = WDOG_BASE + 0x00;
    constexpr uint32_t WDOG_CS2         = WDOG_BASE + 0x01;
    constexpr uint32_t WDOG_CNT         = WDOG_BASE + 0x02;  // 16-bit counter
    constexpr uint32_t WDOG_TOVAL       = WDOG_BASE + 0x04;  // 16-bit timeout

    // WDOG unlock sequence
    constexpr uint16_t WDOG_UNLOCK_1    = 0x20C5;
    constexpr uint16_t WDOG_UNLOCK_2    = 0x28D9;

    // SIM (System Integration Module) for chip ID
    constexpr uint32_t SIM_SDID         = 0x40048024;

    // Memory layout
    constexpr uint32_t FLASH_START      = 0x00000000;
    constexpr uint32_t SECTOR_SIZE      = 512;           // 512 bytes
    constexpr uint32_t FLASH_CONFIG     = 0x00000400;    // flash config field (security byte)
    constexpr uint32_t EEPROM_START     = 0x10000000;

    constexpr uint32_t PROG_WIDTH       = 2;             // word (16-bit) for FTMRH

    // MKE02/04/06 DEV_ID values from SIM_SDID (bits 11:0 — subfamily ID)
    constexpr uint16_t SDID_KE02        = 0x020;
    constexpr uint16_t SDID_KE04        = 0x040;
    constexpr uint16_t SDID_KE06        = 0x060;
}

class MkeFtmrhTargetDriver : public TargetDriver {
public:
    // ── Core control ───────────────────────────────────────

    /// Halt core + disable watchdog + set clock divider
    ProgrammerStatus haltTarget(Transport& transport) override;

    // ── Protection ──────────────────────────────────────────

    RdpLevel readRdpLevel(Transport& transport) override;

    // ── Write ───────────────────────────────────────────────

    ProgrammerStatus eraseFlash(Transport& transport) override;

    ProgrammerStatus writeFlash(Transport& transport, const uint8_t* data,
                                uint32_t address, uint32_t size) override;

    ProgrammerStatus writeOptionBytes(Transport& transport, const uint8_t* data,
                                      uint32_t address, uint32_t size, bool unsafe) override;

    ProgrammerStatus writeOtp(Transport& transport, const uint8_t* data,
                              uint32_t address, uint32_t size) override;

private:
    ProgrammerStatus disableWatchdog(Transport& transport);
    ProgrammerStatus setClockDivider(Transport& transport);
    ProgrammerStatus waitCommandComplete(Transport& transport);
    ProgrammerStatus clearErrors(Transport& transport);
    ProgrammerStatus executeCommand(Transport& transport, uint8_t cmd,
                                    uint32_t address, const uint8_t* data, uint8_t data_len);
    ProgrammerStatus eraseSector(Transport& transport, uint32_t address);
};
