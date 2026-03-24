/**
 ******************************************************************************
 * @file           : stm32f2_flash.cpp
 * @brief          : Flash driver implementation for STM32F2 series
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "stm32f2_flash.h"
#include "flash_utils.h"

using namespace stm32f2_flash;

ProgrammerStatus Stm32F2FlashDriver::unlock(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_KEYR, KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_KEYR, KEY2);
}

ProgrammerStatus Stm32F2FlashDriver::lock(Transport& transport) {
    uint32_t cr;
    auto status = flash_read_word(transport, FLASH_CR, cr);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_CR, cr | CR_LOCK);
}

ProgrammerStatus Stm32F2FlashDriver::unlockOptionBytes(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY2);
}

ProgrammerStatus Stm32F2FlashDriver::waitReady(Transport& transport) {
    return flash_wait_busy(transport, FLASH_SR, SR_BSY,
                           SR_PGSERR | SR_PGPERR | SR_PGAERR | SR_WRPERR | SR_OPERR,
                           100000, &m_error_);
}

ProgrammerStatus Stm32F2FlashDriver::eraseSector(Transport& transport, uint8_t sector) {
    uint32_t cr = CR_SER | CR_PSIZE_X32
                | (static_cast<uint32_t>(sector & CR_SNB_MASK) << CR_SNB_SHIFT);
    auto status = flash_write_word(transport, FLASH_CR, cr);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, FLASH_CR, cr | CR_STRT);
    if (status != ProgrammerStatus::Ok) return status;
    return waitReady(transport);
}

RdpLevel Stm32F2FlashDriver::readRdpLevel(Transport& transport) {
    uint32_t optcr;
    if (flash_read_word(transport, FLASH_OPTCR, optcr) != ProgrammerStatus::Ok)
        return RdpLevel::Unknown;
    uint8_t rdp = static_cast<uint8_t>(optcr >> 8);
    if (rdp == RDP_LEVEL_0) return RdpLevel::Level0;
    if (rdp == RDP_LEVEL_2) return RdpLevel::Level2;
    return RdpLevel::Level1;
}

ProgrammerStatus Stm32F2FlashDriver::eraseFlash(Transport& transport) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;
    uint32_t cr = CR_MER | CR_PSIZE_X32;
    status = flash_write_word(transport, FLASH_CR, cr);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    status = flash_write_word(transport, FLASH_CR, cr | CR_STRT);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    status = waitReady(transport);
    lock(transport);
    return status;
}

ProgrammerStatus Stm32F2FlashDriver::writeFlash(Transport& transport,
                                                  const uint8_t* data,
                                                  uint32_t address,
                                                  uint32_t size) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;

    for (uint32_t i = 0; i < size; i += PROG_WIDTH) {
        status = flash_write_word(transport, FLASH_CR, CR_PG | CR_PSIZE_X32);
        if (status != ProgrammerStatus::Ok) break;

        uint32_t word = 0xFFFFFFFF;
        for (uint32_t b = 0; b < PROG_WIDTH && (i + b) < size; b++)
            word = (word & ~(0xFFUL << (b * 8))) | (static_cast<uint32_t>(data[i + b]) << (b * 8));

        status = flash_write_word(transport, address + i, word);
        if (status != ProgrammerStatus::Ok) break;
        status = waitReady(transport);
        if (status != ProgrammerStatus::Ok) break;

        // Report progress every 4 KB
        uint32_t next = i + PROG_WIDTH;
        if (next % 4096 == 0 || next >= size) {
            if (!reportProgress(next < size ? next : size, size)) {
                status = ProgrammerStatus::ErrorAborted;
                break;
            }
        }
    }

    lock(transport);
    return status;
}

ProgrammerStatus Stm32F2FlashDriver::writeOptionBytes(Transport& transport,
                                                        const uint8_t* data,
                                                        uint32_t address,
                                                        uint32_t size,
                                                        bool unsafe) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;
    status = unlockOptionBytes(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    if (size >= 4) {
        uint32_t optcr = static_cast<uint32_t>(data[0])
                       | (static_cast<uint32_t>(data[1]) << 8)
                       | (static_cast<uint32_t>(data[2]) << 16)
                       | (static_cast<uint32_t>(data[3]) << 24);

        if (!unsafe) {
            // SAFETY: reject RDP Level 2 — permanently disables debug, irreversible
            uint8_t rdp = static_cast<uint8_t>(optcr >> 8);
            if (rdp == RDP_LEVEL_2) {
                m_error_ = "Rejected: RDP Level 2 (0xCC) permanently disables debug — chip cannot be recovered";
                lock(transport);
                return ProgrammerStatus::ErrorProtected;
            }

            // SAFETY: force all sectors unprotected to prevent accidental lockout
            optcr |= (0xFFFUL << 16);
        }

        // Strip control bits, keep configuration fields only
        optcr &= ~(OPTCR_OPTLOCK | OPTCR_OPTSTRT);

        status = flash_write_word(transport, FLASH_OPTCR, optcr);
        if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
        status = flash_write_word(transport, FLASH_OPTCR, optcr | OPTCR_OPTSTRT);
        if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
        status = waitReady(transport);
    }

    lock(transport);
    return status;
}

ProgrammerStatus Stm32F2FlashDriver::writeOtp(Transport& transport,
                                                const uint8_t* data,
                                                uint32_t address,
                                                uint32_t size) {
    return writeFlash(transport, data, address, size);
}
