/**
 ******************************************************************************
 * @file           : stm32l1_flash.cpp
 * @brief          : Flash driver implementation for STM32L1 series
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "stm32l1_flash.h"
#include "flash_utils.h"

using namespace stm32l1_flash;

ProgrammerStatus Stm32L1FlashDriver::unlockPE(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_PEKEYR, PEKEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_PEKEYR, PEKEY2);
}

ProgrammerStatus Stm32L1FlashDriver::unlockProgram(Transport& transport) {
    auto status = unlockPE(transport);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, FLASH_PRGKEYR, PRGKEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_PRGKEYR, PRGKEY2);
}

ProgrammerStatus Stm32L1FlashDriver::unlockOptionBytes(Transport& transport) {
    auto status = unlockPE(transport);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY2);
}

ProgrammerStatus Stm32L1FlashDriver::lock(Transport& transport) {
    uint32_t pecr;
    auto status = flash_read_word(transport, FLASH_PECR, pecr);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_PECR, pecr | PECR_PELOCK);
}

ProgrammerStatus Stm32L1FlashDriver::waitReady(Transport& transport) {
    return flash_wait_busy(transport, FLASH_SR, SR_BSY, SR_ALL_ERRORS,
                           100000, &m_error_);
}

ProgrammerStatus Stm32L1FlashDriver::erasePage(Transport& transport, uint32_t address) {
    auto status = flash_write_word(transport, FLASH_PECR, PECR_ERASE | PECR_PROG);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, address, 0x00000000);
    if (status != ProgrammerStatus::Ok) return status;
    return waitReady(transport);
}

RdpLevel Stm32L1FlashDriver::readRdpLevel(Transport& transport) {
    uint32_t obr;
    if (flash_read_word(transport, FLASH_OBR, obr) != ProgrammerStatus::Ok)
        return RdpLevel::Unknown;
    uint8_t rdp = static_cast<uint8_t>(obr);
    if (rdp == RDP_LEVEL_0) return RdpLevel::Level0;
    if (rdp == RDP_LEVEL_2) return RdpLevel::Level2;
    return RdpLevel::Level1;
}

ProgrammerStatus Stm32L1FlashDriver::eraseFlash(Transport& transport) {
    auto status = unlockProgram(transport);
    if (status != ProgrammerStatus::Ok) return status;

    // L1: erase page by page (256 bytes per page, up to 512 KB)
    for (uint32_t page = 0; page < 2048; page++) {
        status = erasePage(transport, FLASH_START + page * PAGE_SIZE);
        if (status != ProgrammerStatus::Ok) break;
        if ((page + 1) % 8 == 0 || page == 2047) {
            if (!reportProgress((page + 1) * PAGE_SIZE, 2048 * PAGE_SIZE)) {
                status = ProgrammerStatus::ErrorAborted;
                break;
            }
        }
    }

    lock(transport);
    return status;
}

ProgrammerStatus Stm32L1FlashDriver::writeFlash(Transport& transport,
                                                  const uint8_t* data,
                                                  uint32_t address,
                                                  uint32_t size) {
    auto status = unlockProgram(transport);
    if (status != ProgrammerStatus::Ok) return status;

    for (uint32_t i = 0; i < size; i += PROG_WIDTH) {
        uint32_t word = 0xFFFFFFFF;
        for (uint32_t b = 0; b < 4 && (i + b) < size; b++)
            word = (word & ~(0xFFUL << (b * 8))) | (static_cast<uint32_t>(data[i + b]) << (b * 8));
        status = flash_write_word(transport, address + i, word);
        if (status != ProgrammerStatus::Ok) break;
        status = waitReady(transport);
        if (status != ProgrammerStatus::Ok) break;

        // Report progress every 1 KB
        uint32_t next = i + PROG_WIDTH;
        if (next % 1024 == 0 || next >= size) {
            if (!reportProgress(next < size ? next : size, size)) {
                status = ProgrammerStatus::ErrorAborted;
                break;
            }
        }
    }

    lock(transport);
    return status;
}

ProgrammerStatus Stm32L1FlashDriver::writeOptionBytes(Transport& transport,
                                                        const uint8_t* data,
                                                        uint32_t address,
                                                        uint32_t size, bool unsafe) {
    auto status = unlockOptionBytes(transport);
    if (status != ProgrammerStatus::Ok) return status;

    if (!unsafe) {
        // SAFETY: reject RDP Level 2 — permanently disables debug, irreversible
        if (address <= OB_BASE && (address + size) > OB_BASE) {
            uint8_t rdp = data[OB_BASE - address];
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
        status = waitReady(transport);
        if (status != ProgrammerStatus::Ok) break;
    }

    if (status == ProgrammerStatus::Ok) {
        uint32_t pecr;
        flash_read_word(transport, FLASH_PECR, pecr);
        flash_write_word(transport, FLASH_PECR, pecr | PECR_OBL_LAUNCH);
    }

    lock(transport);
    return status;
}

ProgrammerStatus Stm32L1FlashDriver::writeOtp(Transport& transport,
                                                const uint8_t* data,
                                                uint32_t address,
                                                uint32_t size) {
    m_error_ = "OTP not available on this chip family";
    return ProgrammerStatus::ErrorWrite;  // L1 has no dedicated OTP
}
