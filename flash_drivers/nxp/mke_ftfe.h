/**
 ******************************************************************************
 * @file           : mke_ftfe.h
 * @brief          : Target driver for NXP Kinetis KE14/KE15/KE16/KE18 (FTFE flash)
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
 * Target driver for newer NXP Kinetis KE series (MKE14, MKE15, MKE16, MKE18)
 * which use the FTFE flash controller. These are 5V-tolerant Cortex-M0+/M4F
 * parts with larger flash and more advanced features.
 *
 * The FTFE flash controller uses a command-based programming model via
 * FCCOB registers (12 bytes), similar to FTMRH but with a different
 * register layout and additional commands (FlexNVM, FlexRAM, etc.).
 *
 * Features:
 * - Command-based flash programming via FCCOB0-FCCOBB registers
 * - Sector erase (2 KB / 4 KB depending on part) and mass erase
 * - Phrase (8-byte) flash programming
 * - Watchdog (WDOG32) auto-disable on connect
 * - FlexNVM / FlexRAM support on F-series parts
 * - Flash security byte read for protection detection
 * - SIM_SDID-based chip identification
 *
 * Usage Example:
 * ```cpp
 * MkeFtfeTargetDriver target_driver;
 * Stm32Programmer programmer(transport, target_driver);
 * programmer.connect();  // auto-disables watchdog
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "../../stm32_programmer.h"

namespace mke_ftfe {
    // FTFE flash controller base address
    constexpr uint32_t FTFE_BASE        = 0x40020000;

    // FTFE registers
    constexpr uint32_t FTFE_FSTAT       = FTFE_BASE + 0x00;  // status
    constexpr uint32_t FTFE_FCNFG       = FTFE_BASE + 0x01;  // configuration
    constexpr uint32_t FTFE_FSEC        = FTFE_BASE + 0x02;  // security
    constexpr uint32_t FTFE_FOPT        = FTFE_BASE + 0x03;  // flash option
    constexpr uint32_t FTFE_FCCOB3      = FTFE_BASE + 0x04;  // command object [3] (command code)
    constexpr uint32_t FTFE_FCCOB2      = FTFE_BASE + 0x05;  // command object [2] (address high)
    constexpr uint32_t FTFE_FCCOB1      = FTFE_BASE + 0x06;  // command object [1] (address mid)
    constexpr uint32_t FTFE_FCCOB0      = FTFE_BASE + 0x07;  // command object [0] (address low)
    constexpr uint32_t FTFE_FCCOB7      = FTFE_BASE + 0x08;  // command object [7] (data 0)
    constexpr uint32_t FTFE_FCCOB6      = FTFE_BASE + 0x09;  // command object [6] (data 1)
    constexpr uint32_t FTFE_FCCOB5      = FTFE_BASE + 0x0A;  // command object [5] (data 2)
    constexpr uint32_t FTFE_FCCOB4      = FTFE_BASE + 0x0B;  // command object [4] (data 3)
    constexpr uint32_t FTFE_FCCOBB      = FTFE_BASE + 0x0C;  // command object [B] (data 4)
    constexpr uint32_t FTFE_FCCOBA      = FTFE_BASE + 0x0D;  // command object [A] (data 5)
    constexpr uint32_t FTFE_FCCOB9      = FTFE_BASE + 0x0E;  // command object [9] (data 6)
    constexpr uint32_t FTFE_FCCOB8      = FTFE_BASE + 0x0F;  // command object [8] (data 7)
    constexpr uint32_t FTFE_FPROT3      = FTFE_BASE + 0x10;  // P-Flash protection 3
    constexpr uint32_t FTFE_FPROT2      = FTFE_BASE + 0x11;  // P-Flash protection 2
    constexpr uint32_t FTFE_FPROT1      = FTFE_BASE + 0x12;  // P-Flash protection 1
    constexpr uint32_t FTFE_FPROT0      = FTFE_BASE + 0x13;  // P-Flash protection 0
    constexpr uint32_t FTFE_FDPROT      = FTFE_BASE + 0x17;  // D-Flash protection
    constexpr uint32_t FTFE_FEPROT      = FTFE_BASE + 0x16;  // EEPROM protection

    // FSTAT bits
    constexpr uint8_t FSTAT_CCIF        = (1 << 7);  // command complete
    constexpr uint8_t FSTAT_RDCOLERR    = (1 << 6);  // read collision error
    constexpr uint8_t FSTAT_ACCERR      = (1 << 5);  // access error
    constexpr uint8_t FSTAT_FPVIOL      = (1 << 4);  // protection violation
    constexpr uint8_t FSTAT_MGSTAT0     = (1 << 0);  // command completion status

    constexpr uint8_t FSTAT_ERRORS      = FSTAT_RDCOLERR | FSTAT_ACCERR | FSTAT_FPVIOL;

    // FSEC bits
    constexpr uint8_t FSEC_SEC_MASK     = 0x03;
    constexpr uint8_t FSEC_UNSECURED    = 0x02;

    // Flash commands
    constexpr uint8_t CMD_READ_1S_BLOCK = 0x00;  // read 1s block
    constexpr uint8_t CMD_READ_1S_SEC   = 0x01;  // read 1s section
    constexpr uint8_t CMD_PROGRAM_CHECK = 0x02;  // program check
    constexpr uint8_t CMD_READ_RESOURCE = 0x03;  // read resource
    constexpr uint8_t CMD_PROGRAM_PHRASE = 0x07; // program phrase (8 bytes)
    constexpr uint8_t CMD_ERASE_SECTOR  = 0x09;  // erase flash sector
    constexpr uint8_t CMD_READ_1S_ALL   = 0x40;  // read 1s all blocks
    constexpr uint8_t CMD_READ_ONCE     = 0x41;  // read once
    constexpr uint8_t CMD_PROGRAM_ONCE  = 0x43;  // program once
    constexpr uint8_t CMD_ERASE_ALL     = 0x44;  // erase all blocks
    constexpr uint8_t CMD_VERIFY_KEY    = 0x45;  // verify backdoor access key
    constexpr uint8_t CMD_PROGRAM_PART  = 0x80;  // program partition (FlexNVM)
    constexpr uint8_t CMD_SET_FLEXRAM   = 0x81;  // set FlexRAM function

    // WDOG32 registers (newer KE14/15/16/18 use 32-bit watchdog)
    constexpr uint32_t WDOG_BASE        = 0x40052000;
    constexpr uint32_t WDOG_CS          = WDOG_BASE + 0x00;  // control and status
    constexpr uint32_t WDOG_CNT         = WDOG_BASE + 0x04;  // counter
    constexpr uint32_t WDOG_TOVAL       = WDOG_BASE + 0x08;  // timeout value
    constexpr uint32_t WDOG_WIN         = WDOG_BASE + 0x0C;  // window

    // WDOG32 unlock sequence
    constexpr uint32_t WDOG_UNLOCK_1    = 0xC520;
    constexpr uint32_t WDOG_UNLOCK_2    = 0xD928;

    // WDOG_CS bits
    constexpr uint32_t WDOG_CS_EN       = (1UL << 7);   // watchdog enable
    constexpr uint32_t WDOG_CS_UPDATE   = (1UL << 5);   // allow updates
    constexpr uint32_t WDOG_CS_CMD32EN  = (1UL << 13);  // 32-bit command support

    // SIM for chip ID
    constexpr uint32_t SIM_SDID         = 0x40048024;

    // Memory layout
    constexpr uint32_t FLASH_START      = 0x00000000;
    constexpr uint32_t SECTOR_SIZE_2K   = 2048;          // 2 KB (KE14Z/KE15Z)
    constexpr uint32_t SECTOR_SIZE_4K   = 4096;          // 4 KB (KE14F/KE16F/KE18F)
    constexpr uint32_t FLASH_CONFIG     = 0x00000400;    // flash config field

    // Programming width: phrase = 8 bytes
    constexpr uint32_t PROG_WIDTH       = 8;

    // MKE14/15/16/18 DEV_ID values from SIM_SDID
    constexpr uint16_t SDID_KE14Z       = 0x140;
    constexpr uint16_t SDID_KE15Z       = 0x150;
    constexpr uint16_t SDID_KE14F       = 0x141;
    constexpr uint16_t SDID_KE16F       = 0x161;
    constexpr uint16_t SDID_KE18F       = 0x181;
}

class MkeFtfeTargetDriver : public TargetDriver {
public:
    // ── TargetDriver hooks ──────────────────────────────────

    /// Disable WDOG32 after connect to prevent reset during programming
    ProgrammerStatus onConnect(Transport& transport) override;

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
    ProgrammerStatus waitCommandComplete(Transport& transport);
    ProgrammerStatus clearErrors(Transport& transport);
    ProgrammerStatus launchCommand(Transport& transport);
    ProgrammerStatus eraseSector(Transport& transport, uint32_t address);
    ProgrammerStatus programPhrase(Transport& transport, uint32_t address,
                                   const uint8_t* data);  // 8 bytes
};
