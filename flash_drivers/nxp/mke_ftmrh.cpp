/**
 ******************************************************************************
 * @file           : mke_ftmrh.cpp
 * @brief          : Target driver implementation for MKE02/KE04/KE06 (FTMRH)
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "mke_ftmrh.h"
#include "../flash_utils.h"
#include <cstdio>

using namespace mke_ftmrh;

// ── Helpers ─────────────────────────────────────────────────

static ProgrammerStatus read_byte(Transport& transport, uint32_t address, uint8_t& value) {
    return transport.readMemory(&value, address, 1);
}

static ProgrammerStatus write_byte(Transport& transport, uint32_t address, uint8_t value) {
    return transport.writeMemory(&value, address, 1);
}

static ProgrammerStatus write_halfword_be(Transport& transport, uint32_t address, uint16_t value) {
    // FTMRH FCCOB registers are big-endian
    uint8_t buf[2] = {
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value),
    };
    return transport.writeMemory(buf, address, 2);
}

// ── Private methods ─────────────────────────────────────────

ProgrammerStatus MkeFtmrhTargetDriver::disableWatchdog(Transport& transport) {
    // Write unlock sequence to WDOG_CNT
    auto status = write_halfword_be(transport, WDOG_CNT, WDOG_UNLOCK_1);
    if (status != ProgrammerStatus::Ok) return status;
    status = write_halfword_be(transport, WDOG_CNT, WDOG_UNLOCK_2);
    if (status != ProgrammerStatus::Ok) return status;

    // Disable watchdog: clear EN bit in CS1
    return write_byte(transport, WDOG_CS1, 0x00);
}

ProgrammerStatus MkeFtmrhTargetDriver::setClockDivider(Transport& transport) {
    // Set flash clock divider for ~1 MHz flash clock
    // Assuming bus clock ~20 MHz: divider = 20 - 1 = 0x13
    // Bit 6 (FDIVLCK) locks the divider after first write
    return write_byte(transport, FTMRH_FCLKDIV, 0x13);
}

ProgrammerStatus MkeFtmrhTargetDriver::waitCommandComplete(Transport& transport) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t fstat;
        auto status = read_byte(transport, FTMRH_FSTAT, fstat);
        if (status != ProgrammerStatus::Ok)
            return status;

        if (fstat & (FSTAT_ACCERR | FSTAT_FPVIOL)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "FTMRH FSTAT error: 0x%02X (ACCERR=%d, FPVIOL=%d)",
                     fstat, !!(fstat & FSTAT_ACCERR), !!(fstat & FSTAT_FPVIOL));
            m_error_ = buf;
            return ProgrammerStatus::ErrorWrite;
        }

        if (fstat & FSTAT_CCIF)
            return ProgrammerStatus::Ok;
    }
    m_error_ = "FTMRH command timeout";
    return ProgrammerStatus::ErrorWrite;  // timeout
}

ProgrammerStatus MkeFtmrhTargetDriver::clearErrors(Transport& transport) {
    // Clear ACCERR and FPVIOL by writing 1 to them
    return write_byte(transport, FTMRH_FSTAT, FSTAT_ACCERR | FSTAT_FPVIOL);
}

ProgrammerStatus MkeFtmrhTargetDriver::executeCommand(Transport& transport,
                                                        uint8_t cmd,
                                                        uint32_t address,
                                                        const uint8_t* data,
                                                        uint8_t data_len) {
    auto status = clearErrors(transport);
    if (status != ProgrammerStatus::Ok) return status;

    // Write command index 0: command code and address high byte
    status = write_byte(transport, FTMRH_FCCOBIX, 0x00);
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTMRH_FCCOBHI, cmd);
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTMRH_FCCOBLO, static_cast<uint8_t>(address >> 16));
    if (status != ProgrammerStatus::Ok) return status;

    // Write command index 1: address low word
    status = write_byte(transport, FTMRH_FCCOBIX, 0x01);
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTMRH_FCCOBHI, static_cast<uint8_t>(address >> 8));
    if (status != ProgrammerStatus::Ok) return status;
    status = write_byte(transport, FTMRH_FCCOBLO, static_cast<uint8_t>(address));
    if (status != ProgrammerStatus::Ok) return status;

    // Write data words
    for (uint8_t i = 0; i < data_len; i += 2) {
        status = write_byte(transport, FTMRH_FCCOBIX, 0x02 + i / 2);
        if (status != ProgrammerStatus::Ok) return status;
        status = write_byte(transport, FTMRH_FCCOBHI, data[i]);
        if (status != ProgrammerStatus::Ok) return status;
        if (i + 1 < data_len) {
            status = write_byte(transport, FTMRH_FCCOBLO, data[i + 1]);
            if (status != ProgrammerStatus::Ok) return status;
        }
    }

    // Launch command: clear CCIF
    status = write_byte(transport, FTMRH_FSTAT, FSTAT_CCIF);
    if (status != ProgrammerStatus::Ok) return status;

    return waitCommandComplete(transport);
}

ProgrammerStatus MkeFtmrhTargetDriver::eraseSector(Transport& transport, uint32_t address) {
    return executeCommand(transport, CMD_ERASE_SECTOR, address, nullptr, 0);
}

// ── TargetDriver interface ──────────────────────────────────

ProgrammerStatus MkeFtmrhTargetDriver::onConnect(Transport& transport) {
    auto status = disableWatchdog(transport);
    if (status != ProgrammerStatus::Ok) return status;
    return setClockDivider(transport);
}

RdpLevel MkeFtmrhTargetDriver::readRdpLevel(Transport& transport) {
    uint8_t fsec;
    if (read_byte(transport, FTMRH_FSEC, fsec) != ProgrammerStatus::Ok)
        return RdpLevel::Unknown;

    if ((fsec & FSEC_SEC_MASK) == FSEC_UNSECURED)
        return RdpLevel::Level0;
    return RdpLevel::Level1;
}

ProgrammerStatus MkeFtmrhTargetDriver::eraseFlash(Transport& transport) {
    return executeCommand(transport, CMD_ERASE_ALL, 0, nullptr, 0);
}

ProgrammerStatus MkeFtmrhTargetDriver::writeFlash(Transport& transport,
                                                    const uint8_t* data,
                                                    uint32_t address,
                                                    uint32_t size) {
    // Program word (2 bytes) at a time
    for (uint32_t i = 0; i < size; i += PROG_WIDTH) {
        uint8_t word[2] = { 0xFF, 0xFF };
        for (uint32_t b = 0; b < PROG_WIDTH && (i + b) < size; b++)
            word[b] = data[i + b];

        auto status = executeCommand(transport, CMD_PROGRAM, address + i, word, 2);
        if (status != ProgrammerStatus::Ok)
            return status;

        // Report progress every sector (512 bytes)
        uint32_t next = i + PROG_WIDTH;
        if (next % SECTOR_SIZE == 0 || next >= size) {
            if (!reportProgress(next < size ? next : size, size))
                return ProgrammerStatus::ErrorAborted;
        }
    }
    return ProgrammerStatus::Ok;
}

ProgrammerStatus MkeFtmrhTargetDriver::writeOptionBytes(Transport& transport,
                                                          const uint8_t* data,
                                                          uint32_t address,
                                                          uint32_t size,
                                                          bool unsafe) {
    (void)unsafe;
    // Option bytes on KE02/04/06 are in the flash config field at 0x400-0x40F
    // Erase the sector containing the config field first
    auto status = eraseSector(transport, FLASH_CONFIG & ~(SECTOR_SIZE - 1));
    if (status != ProgrammerStatus::Ok) return status;

    return writeFlash(transport, data, address, size);
}

ProgrammerStatus MkeFtmrhTargetDriver::writeOtp(Transport& transport,
                                                  const uint8_t* data,
                                                  uint32_t address,
                                                  uint32_t size) {
    // KE02/04/06 have no dedicated OTP area
    m_error_ = "OTP not available on this chip family";
    return ProgrammerStatus::ErrorWrite;
}
