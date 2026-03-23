/**
 ******************************************************************************
 * @file           : mke_ftfe.cpp
 * @brief          : Target driver implementation for MKE14/KE15/KE16/KE18 (FTFE)
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "mke_ftfe.h"
#include "../flash_utils.h"
#include <cstdio>
#include <cstring>

using namespace mke_ftfe;

// ── Helpers ─────────────────────────────────────────────────

static ProgrammerStatus read_byte(Transport& transport, uint32_t address, uint8_t& value) {
    return transport.readMemory(&value, address, 1);
}

static ProgrammerStatus write_byte(Transport& transport, uint32_t address, uint8_t value) {
    return transport.writeMemory(&value, address, 1);
}

// ── Private methods ─────────────────────────────────────────

ProgrammerStatus MkeFtfeTargetDriver::disableWatchdog(Transport& transport) {
    // WDOG32 unlock: write unlock words to WDOG_CNT
    auto status = flash_write_word(transport, WDOG_CNT, WDOG_UNLOCK_1);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, WDOG_CNT, WDOG_UNLOCK_2);
    if (status != ProgrammerStatus::Ok) return status;

    // Disable: set UPDATE, clear EN
    uint32_t cs;
    status = flash_read_word(transport, WDOG_CS, cs);
    if (status != ProgrammerStatus::Ok) return status;

    cs &= ~WDOG_CS_EN;
    cs |= WDOG_CS_UPDATE;
    return flash_write_word(transport, WDOG_CS, cs);
}

ProgrammerStatus MkeFtfeTargetDriver::waitCommandComplete(Transport& transport) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t fstat;
        auto status = read_byte(transport, FTFE_FSTAT, fstat);
        if (status != ProgrammerStatus::Ok)
            return status;

        if (fstat & FSTAT_ERRORS) {
            char buf[64];
            snprintf(buf, sizeof(buf), "FTFE FSTAT error: 0x%02X (ACCERR=%d, FPVIOL=%d)",
                     fstat, !!(fstat & FSTAT_ACCERR), !!(fstat & FSTAT_FPVIOL));
            m_error_ = buf;
            return ProgrammerStatus::ErrorWrite;
        }

        if (fstat & FSTAT_CCIF)
            return ProgrammerStatus::Ok;
    }
    m_error_ = "FTFE command timeout";
    return ProgrammerStatus::ErrorWrite;  // timeout
}

ProgrammerStatus MkeFtfeTargetDriver::clearErrors(Transport& transport) {
    return write_byte(transport, FTFE_FSTAT, FSTAT_ERRORS);
}

ProgrammerStatus MkeFtfeTargetDriver::launchCommand(Transport& transport) {
    // Clear CCIF to launch the command
    return write_byte(transport, FTFE_FSTAT, FSTAT_CCIF);
}

ProgrammerStatus MkeFtfeTargetDriver::eraseSector(Transport& transport, uint32_t address) {
    auto status = clearErrors(transport);
    if (status != ProgrammerStatus::Ok) return status;

    // Load FCCOB: command + address
    status = write_byte(transport, FTFE_FCCOB3, CMD_ERASE_SECTOR);
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTFE_FCCOB2, static_cast<uint8_t>(address >> 16));
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTFE_FCCOB1, static_cast<uint8_t>(address >> 8));
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTFE_FCCOB0, static_cast<uint8_t>(address));
    if (status != ProgrammerStatus::Ok) return status;

    status = launchCommand(transport);
    if (status != ProgrammerStatus::Ok) return status;

    return waitCommandComplete(transport);
}

ProgrammerStatus MkeFtfeTargetDriver::programPhrase(Transport& transport,
                                                     uint32_t address,
                                                     const uint8_t* data) {
    auto status = clearErrors(transport);
    if (status != ProgrammerStatus::Ok) return status;

    // FCCOB3: command code
    status = write_byte(transport, FTFE_FCCOB3, CMD_PROGRAM_PHRASE);
    if (status != ProgrammerStatus::Ok) return status;

    // FCCOB2-0: 24-bit address
    status = write_byte(transport, FTFE_FCCOB2, static_cast<uint8_t>(address >> 16));
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTFE_FCCOB1, static_cast<uint8_t>(address >> 8));
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTFE_FCCOB0, static_cast<uint8_t>(address));
    if (status != ProgrammerStatus::Ok) return status;

    // FCCOB7-4: data bytes 0-3
    status = write_byte(transport, FTFE_FCCOB7, data[0]);
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTFE_FCCOB6, data[1]);
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTFE_FCCOB5, data[2]);
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTFE_FCCOB4, data[3]);
    if (status != ProgrammerStatus::Ok) return status;

    // FCCOBB-8: data bytes 4-7
    status = write_byte(transport, FTFE_FCCOBB, data[4]);
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTFE_FCCOBA, data[5]);
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTFE_FCCOB9, data[6]);
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTFE_FCCOB8, data[7]);
    if (status != ProgrammerStatus::Ok) return status;

    status = launchCommand(transport);
    if (status != ProgrammerStatus::Ok) return status;

    return waitCommandComplete(transport);
}

// ── TargetDriver interface ──────────────────────────────────

ProgrammerStatus MkeFtfeTargetDriver::onConnect(Transport& transport) {
    return disableWatchdog(transport);
}

RdpLevel MkeFtfeTargetDriver::readRdpLevel(Transport& transport) {
    uint8_t fsec;
    if (read_byte(transport, FTFE_FSEC, fsec) != ProgrammerStatus::Ok)
        return RdpLevel::Unknown;

    if ((fsec & FSEC_SEC_MASK) == FSEC_UNSECURED)
        return RdpLevel::Level0;
    return RdpLevel::Level1;
}

ProgrammerStatus MkeFtfeTargetDriver::eraseFlash(Transport& transport) {
    auto status = clearErrors(transport);
    if (status != ProgrammerStatus::Ok) return status;

    // Erase all blocks command
    status = write_byte(transport, FTFE_FCCOB3, CMD_ERASE_ALL);
    if (status != ProgrammerStatus::Ok) return status;

    status = launchCommand(transport);
    if (status != ProgrammerStatus::Ok) return status;

    return waitCommandComplete(transport);
}

ProgrammerStatus MkeFtfeTargetDriver::writeFlash(Transport& transport,
                                                   const uint8_t* data,
                                                   uint32_t address,
                                                   uint32_t size) {
    // Program phrase (8 bytes) at a time
    for (uint32_t i = 0; i < size; i += PROG_WIDTH) {
        uint8_t phrase[PROG_WIDTH];
        std::memset(phrase, 0xFF, PROG_WIDTH);

        uint32_t remaining = size - i;
        uint32_t copy_size = (remaining < PROG_WIDTH) ? remaining : PROG_WIDTH;
        std::memcpy(phrase, &data[i], copy_size);

        auto status = programPhrase(transport, address + i, phrase);
        if (status != ProgrammerStatus::Ok)
            return status;

        // Report progress every 2 KB
        uint32_t next = i + PROG_WIDTH;
        if (next % SECTOR_SIZE_2K == 0 || next >= size) {
            if (!reportProgress(next < size ? next : size, size))
                return ProgrammerStatus::ErrorAborted;
        }
    }
    return ProgrammerStatus::Ok;
}

ProgrammerStatus MkeFtfeTargetDriver::writeOptionBytes(Transport& transport,
                                                         const uint8_t* data,
                                                         uint32_t address,
                                                         uint32_t size,
                                                         bool unsafe) {
    (void)unsafe;
    // Flash config field at 0x400-0x40F
    // Erase the sector containing it first
    auto status = eraseSector(transport, FLASH_CONFIG & ~(SECTOR_SIZE_2K - 1));
    if (status != ProgrammerStatus::Ok) return status;

    return writeFlash(transport, data, address, size);
}

ProgrammerStatus MkeFtfeTargetDriver::writeOtp(Transport& transport,
                                                 const uint8_t* data,
                                                 uint32_t address,
                                                 uint32_t size) {
    // Program Once command — one record at a time (8 bytes per record)
    for (uint32_t i = 0; i < size; i += PROG_WIDTH) {
        auto status = clearErrors(transport);
        if (status != ProgrammerStatus::Ok) return status;

        uint8_t record_index = static_cast<uint8_t>(i / PROG_WIDTH);

        status = write_byte(transport, FTFE_FCCOB3, CMD_PROGRAM_ONCE);
        if (status != ProgrammerStatus::Ok) return status;
        status = write_byte(transport, FTFE_FCCOB2, record_index);
        if (status != ProgrammerStatus::Ok) return status;

        // Load data bytes
        const uint8_t* phrase = &data[i];
        write_byte(transport, FTFE_FCCOB7, phrase[0]);
        write_byte(transport, FTFE_FCCOB6, phrase[1]);
        write_byte(transport, FTFE_FCCOB5, phrase[2]);
        write_byte(transport, FTFE_FCCOB4, phrase[3]);
        write_byte(transport, FTFE_FCCOBB, (i + 4 < size) ? phrase[4] : 0xFF);
        write_byte(transport, FTFE_FCCOBA, (i + 5 < size) ? phrase[5] : 0xFF);
        write_byte(transport, FTFE_FCCOB9, (i + 6 < size) ? phrase[6] : 0xFF);
        write_byte(transport, FTFE_FCCOB8, (i + 7 < size) ? phrase[7] : 0xFF);

        status = launchCommand(transport);
        if (status != ProgrammerStatus::Ok) return status;

        status = waitCommandComplete(transport);
        if (status != ProgrammerStatus::Ok) return status;
    }
    return ProgrammerStatus::Ok;
}
