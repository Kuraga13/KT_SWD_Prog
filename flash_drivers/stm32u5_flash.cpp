/**
 ******************************************************************************
 * @file           : stm32u5_flash.cpp
 * @brief          : Flash driver implementation for STM32U5 series
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "stm32u5_flash.h"
#include "flash_utils.h"
#include <cstring>

using namespace stm32u5_flash;

ProgrammerStatus Stm32U5FlashDriver::unlock(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_KEYR, KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_KEYR, KEY2);
}

ProgrammerStatus Stm32U5FlashDriver::lock(Transport& transport) {
    uint32_t cr;
    auto status = flash_read_word(transport, FLASH_CR, cr);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_CR, cr | CR_LOCK);
}

ProgrammerStatus Stm32U5FlashDriver::unlockOptionBytes(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY2);
}

ProgrammerStatus Stm32U5FlashDriver::waitReady(Transport& transport) {
    return flash_wait_busy(transport, FLASH_SR, SR_BSY, SR_ALL_ERRORS, 100000, &m_error_);
}

ProgrammerStatus Stm32U5FlashDriver::erasePage(Transport& transport, uint32_t page_num, bool bank2) {
    uint32_t cr = CR_PER | (static_cast<uint32_t>(page_num & CR_PNB_MASK) << CR_PNB_SHIFT);
    if (bank2) cr |= CR_BKER;
    auto status = flash_write_word(transport, FLASH_CR, cr);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, FLASH_CR, cr | CR_STRT);
    if (status != ProgrammerStatus::Ok) return status;
    return waitReady(transport);
}

ProgrammerStatus Stm32U5FlashDriver::programQuadWord(Transport& transport,
                                                      uint32_t address,
                                                      const uint8_t* data) {
    auto status = flash_write_word(transport, FLASH_CR, CR_PG);
    if (status != ProgrammerStatus::Ok) return status;

    // Write 16 bytes (4 words) — triggers 128-bit programming
    for (uint32_t i = 0; i < PROG_WIDTH; i += 4) {
        uint32_t word = static_cast<uint32_t>(data[i])
                      | (static_cast<uint32_t>(data[i + 1]) << 8)
                      | (static_cast<uint32_t>(data[i + 2]) << 16)
                      | (static_cast<uint32_t>(data[i + 3]) << 24);
        status = flash_write_word(transport, address + i, word);
        if (status != ProgrammerStatus::Ok) return status;
    }

    return waitReady(transport);
}

RdpLevel Stm32U5FlashDriver::readRdpLevel(Transport& transport) {
    uint32_t optr;
    if (flash_read_word(transport, FLASH_OPTR, optr) != ProgrammerStatus::Ok)
        return RdpLevel::Unknown;
    uint8_t rdp = static_cast<uint8_t>(optr);
    if (rdp == RDP_LEVEL_0) return RdpLevel::Level0;
    if (rdp == RDP_LEVEL_0_5) return RdpLevel::Level0;
    if (rdp == RDP_LEVEL_2) return RdpLevel::Level2;
    return RdpLevel::Level1;
}

ProgrammerStatus Stm32U5FlashDriver::eraseFlash(Transport& transport) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;
    flash_write_word(transport, FLASH_SR, SR_ALL_ERRORS | SR_EOP);
    status = flash_write_word(transport, FLASH_CR, CR_MER1 | CR_MER2);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    status = flash_write_word(transport, FLASH_CR, CR_MER1 | CR_MER2 | CR_STRT);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    status = waitReady(transport);
    lock(transport);
    return status;
}

ProgrammerStatus Stm32U5FlashDriver::writeFlash(Transport& transport,
                                                  const uint8_t* data,
                                                  uint32_t address,
                                                  uint32_t size) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;
    flash_write_word(transport, FLASH_SR, SR_ALL_ERRORS | SR_EOP);

    for (uint32_t i = 0; i < size; i += PROG_WIDTH) {
        uint8_t quad_word[PROG_WIDTH];
        std::memset(quad_word, 0xFF, PROG_WIDTH);
        uint32_t remaining = size - i;
        uint32_t copy_size = (remaining < PROG_WIDTH) ? remaining : PROG_WIDTH;
        std::memcpy(quad_word, &data[i], copy_size);

        status = programQuadWord(transport, address + i, quad_word);
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

    flash_write_word(transport, FLASH_CR, 0);
    lock(transport);
    return status;
}

ProgrammerStatus Stm32U5FlashDriver::writeOptionBytes(Transport& transport,
                                                        const uint8_t* data,
                                                        uint32_t address,
                                                        uint32_t size, bool unsafe) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;
    status = unlockOptionBytes(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    if (!unsafe) {
        // SAFETY: reject RDP Level 2 — permanently disables debug, irreversible
        if (address <= FLASH_OPTR && (address + size) > FLASH_OPTR) {
            uint8_t rdp = data[FLASH_OPTR - address];
            if (rdp == RDP_LEVEL_2) {
                m_error_ = "RDP Level 2 would permanently lock the chip";
                lock(transport);
                return ProgrammerStatus::ErrorProtected;
            }
        }
    }

    for (uint32_t i = 0; i < size; i += 4) {
        uint32_t word = 0xFFFFFFFF;
        for (uint32_t b = 0; b < 4 && (i + b) < size; b++)
            word = (word & ~(0xFFUL << (b * 8))) | (static_cast<uint32_t>(data[i + b]) << (b * 8));
        status = flash_write_word(transport, address + i, word);
        if (status != ProgrammerStatus::Ok) break;
    }

    if (status == ProgrammerStatus::Ok) {
        uint32_t cr;
        flash_read_word(transport, FLASH_CR, cr);
        flash_write_word(transport, FLASH_CR, cr | CR_OPTSTRT);
        status = waitReady(transport);
        if (status == ProgrammerStatus::Ok) {
            flash_read_word(transport, FLASH_CR, cr);
            flash_write_word(transport, FLASH_CR, cr | CR_OBL_LAUNCH);
        }
    }

    lock(transport);
    return status;
}

ProgrammerStatus Stm32U5FlashDriver::writeOtp(Transport& transport,
                                                const uint8_t* data,
                                                uint32_t address,
                                                uint32_t size) {
    return writeFlash(transport, data, address, size);
}
