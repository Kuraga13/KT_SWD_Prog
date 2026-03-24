/**
 ******************************************************************************
 * @file           : stm32f0_flash.cpp
 * @brief          : Flash driver implementation for STM32F0 series
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "stm32f0_flash.h"
#include "flash_utils.h"

using namespace stm32f0_flash;

// ── Helpers ─────────────────────────────────────────────────

ProgrammerStatus Stm32F0FlashDriver::unlock(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_KEYR, KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_KEYR, KEY2);
}

ProgrammerStatus Stm32F0FlashDriver::lock(Transport& transport) {
    uint32_t cr;
    auto status = flash_read_word(transport, FLASH_CR, cr);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_CR, cr | CR_LOCK);
}

ProgrammerStatus Stm32F0FlashDriver::unlockOptionBytes(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY2);
}

ProgrammerStatus Stm32F0FlashDriver::waitReady(Transport& transport) {
    return flash_wait_busy(transport, FLASH_SR, SR_BSY,
                           SR_PGERR | SR_WRPERR, 100000, &m_error_);
}

ProgrammerStatus Stm32F0FlashDriver::erasePage(Transport& transport, uint32_t address) {
    auto status = flash_write_word(transport, FLASH_CR, CR_PER);
    if (status != ProgrammerStatus::Ok) return status;

    status = flash_write_word(transport, FLASH_AR, address);
    if (status != ProgrammerStatus::Ok) return status;

    status = flash_write_word(transport, FLASH_CR, CR_PER | CR_STRT);
    if (status != ProgrammerStatus::Ok) return status;

    return waitReady(transport);
}

// ── FlashDriver interface ───────────────────────────────────

RdpLevel Stm32F0FlashDriver::readRdpLevel(Transport& transport) {
    uint32_t obr;
    if (flash_read_word(transport, FLASH_OBR, obr) != ProgrammerStatus::Ok)
        return RdpLevel::Unknown;

    uint8_t rdp = static_cast<uint8_t>(obr >> 1) & 0x03;
    if (rdp == 0) return RdpLevel::Level0;
    if (rdp == 3) return RdpLevel::Level2;
    return RdpLevel::Level1;
}

ProgrammerStatus Stm32F0FlashDriver::eraseFlash(Transport& transport) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;

    status = flash_write_word(transport, FLASH_CR, CR_MER);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    status = flash_write_word(transport, FLASH_CR, CR_MER | CR_STRT);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    status = waitReady(transport);
    lock(transport);
    return status;
}

ProgrammerStatus Stm32F0FlashDriver::writeFlash(Transport& transport,
                                                  const uint8_t* data,
                                                  uint32_t address,
                                                  uint32_t size) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;

    // Program half-word at a time
    for (uint32_t i = 0; i < size; i += PROG_WIDTH) {
        status = flash_write_word(transport, FLASH_CR, CR_PG);
        if (status != ProgrammerStatus::Ok) break;

        uint16_t hw = static_cast<uint16_t>(data[i]);
        if (i + 1 < size)
            hw |= static_cast<uint16_t>(data[i + 1]) << 8;

        status = flash_write_halfword(transport, address + i, hw);
        if (status != ProgrammerStatus::Ok) break;

        status = waitReady(transport);
        if (status != ProgrammerStatus::Ok) break;

        // Report progress at page boundaries
        uint32_t next = i + PROG_WIDTH;
        if (next % PAGE_SIZE == 0 || next >= size) {
            if (!reportProgress(next < size ? next : size, size)) {
                status = ProgrammerStatus::ErrorAborted;
                break;
            }
        }
    }

    lock(transport);
    return status;
}

ProgrammerStatus Stm32F0FlashDriver::writeOptionBytes(Transport& transport,
                                                        const uint8_t* data,
                                                        uint32_t address,
                                                        uint32_t size, bool unsafe) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;

    status = unlockOptionBytes(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    if (!unsafe) {
        // SAFETY: reject RDP Level 2 — permanently disables debug, irreversible
        if (address <= OB_BASE && (address + size) > OB_BASE) {
            uint8_t rdp = data[OB_BASE - address];
            if (rdp == RDP_LEVEL_2) {
                m_error_ = "Rejected: RDP Level 2 (0xCC) permanently disables debug — chip cannot be recovered";
                lock(transport);
                return ProgrammerStatus::ErrorProtected;
            }
        }
    }

    // Erase option bytes first
    status = flash_write_word(transport, FLASH_CR, CR_OPTER);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    status = flash_write_word(transport, FLASH_CR, CR_OPTER | CR_STRT);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    status = waitReady(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    // Program option bytes half-word at a time
    for (uint32_t i = 0; i < size; i += 2) {
        status = flash_write_word(transport, FLASH_CR, CR_OPTPG);
        if (status != ProgrammerStatus::Ok) break;

        uint16_t hw = static_cast<uint16_t>(data[i]);
        if (i + 1 < size)
            hw |= static_cast<uint16_t>(data[i + 1]) << 8;

        status = flash_write_halfword(transport, address + i, hw);
        if (status != ProgrammerStatus::Ok) break;

        status = waitReady(transport);
        if (status != ProgrammerStatus::Ok) break;
    }

    // Launch option byte load
    flash_write_word(transport, FLASH_CR, CR_OBL_LAUNCH);

    lock(transport);
    return status;
}

ProgrammerStatus Stm32F0FlashDriver::writeOtp(Transport& transport,
                                                const uint8_t* data,
                                                uint32_t address,
                                                uint32_t size) {
    // F0 has no dedicated OTP area
    m_error_ = "OTP not available on this chip family";
    return ProgrammerStatus::ErrorWrite;
}
