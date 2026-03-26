/**
 ******************************************************************************
 * @file           : stm32h7ab_flash.cpp
 * @brief          : Flash driver implementation for STM32H7Ax/H7Bx (RM0455)
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "stm32h7ab_flash.h"
#include "flash_utils.h"
#include <cstring>

using namespace stm32h7ab_flash;

// ── Helpers ─────────────────────────────────────────────────

ProgrammerStatus Stm32H7abFlashDriver::unlockBank(Transport& transport, uint32_t keyr, uint32_t cr) {
    auto status = flash_write_word(transport, keyr, KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, keyr, KEY2);
}

ProgrammerStatus Stm32H7abFlashDriver::lockBank(Transport& transport, uint32_t cr) {
    uint32_t val;
    auto status = flash_read_word(transport, cr, val);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, cr, val | CR_LOCK);
}

ProgrammerStatus Stm32H7abFlashDriver::unlockOptionBytes(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY2);
}

ProgrammerStatus Stm32H7abFlashDriver::clearErrors(Transport& transport, uint32_t ccr) {
    return flash_write_word(transport, ccr, SR_ALL_ERRORS | SR_EOP);
}

ProgrammerStatus Stm32H7abFlashDriver::waitReady(Transport& transport, uint32_t sr) {
    return flash_wait_busy(transport, sr, SR_BSY | SR_QW, SR_ALL_ERRORS, 100000, &m_error_);
}

ProgrammerStatus Stm32H7abFlashDriver::eraseSector(Transport& transport, uint8_t sector,
                                                     uint32_t cr, uint32_t sr) {
    // RM0455: SNB at bits [12:6], START at bit 5 (no PSIZE)
    uint32_t val = CR_SER
                 | (static_cast<uint32_t>(sector & CR_SNB_MASK) << CR_SNB_SHIFT);
    auto status = flash_write_word(transport, cr, val);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, cr, val | CR_START);
    if (status != ProgrammerStatus::Ok) return status;
    return waitReady(transport, sr);
}

ProgrammerStatus Stm32H7abFlashDriver::programFlashWord(Transport& transport,
                                                          uint32_t address,
                                                          const uint8_t* data) {
    // Determine which bank
    uint32_t cr = (address < BANK2_START) ? FLASH_CR1 : FLASH_CR2;
    uint32_t sr = (address < BANK2_START) ? FLASH_SR1 : FLASH_SR2;

    // RM0455: set PG only (no PSIZE — 128-bit is the only width)
    auto status = flash_write_word(transport, cr, CR_PG);
    if (status != ProgrammerStatus::Ok) return status;

    // Write 16 bytes (4 words) — the 4th word triggers 128-bit programming
    for (uint32_t i = 0; i < PROG_WIDTH; i += 4) {
        uint32_t word = static_cast<uint32_t>(data[i])
                      | (static_cast<uint32_t>(data[i + 1]) << 8)
                      | (static_cast<uint32_t>(data[i + 2]) << 16)
                      | (static_cast<uint32_t>(data[i + 3]) << 24);
        status = flash_write_word(transport, address + i, word);
        if (status != ProgrammerStatus::Ok) return status;
    }

    return waitReady(transport, sr);
}

// ── Public interface ────────────────────────────────────────

RdpLevel Stm32H7abFlashDriver::readRdpLevel(Transport& transport) {
    uint32_t optsr;
    if (flash_read_word(transport, FLASH_OPTSR_CUR, optsr) != ProgrammerStatus::Ok)
        return RdpLevel::Unknown;
    uint8_t rdp = static_cast<uint8_t>(optsr >> 8);
    if (rdp == RDP_LEVEL_0) return RdpLevel::Level0;
    if (rdp == RDP_LEVEL_2) return RdpLevel::Level2;
    return RdpLevel::Level1;
}

ProgrammerStatus Stm32H7abFlashDriver::eraseFlash(Transport& transport) {
    // Bank 1: BER + START (no PSIZE on RM0455)
    auto status = unlockBank(transport, FLASH_KEYR1, FLASH_CR1);
    if (status != ProgrammerStatus::Ok) return status;

    status = clearErrors(transport, FLASH_CCR1);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }
    status = flash_write_word(transport, FLASH_CR1, CR_BER);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }
    status = flash_write_word(transport, FLASH_CR1, CR_BER | CR_START);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }
    status = waitReady(transport, FLASH_SR1);
    lockBank(transport, FLASH_CR1);
    if (status != ProgrammerStatus::Ok) return status;

    // Bank 2
    status = unlockBank(transport, FLASH_KEYR2, FLASH_CR2);
    if (status != ProgrammerStatus::Ok) return status;

    status = clearErrors(transport, FLASH_CCR2);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR2); return status; }
    status = flash_write_word(transport, FLASH_CR2, CR_BER);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR2); return status; }
    status = flash_write_word(transport, FLASH_CR2, CR_BER | CR_START);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR2); return status; }
    status = waitReady(transport, FLASH_SR2);
    lockBank(transport, FLASH_CR2);

    return status;
}

ProgrammerStatus Stm32H7abFlashDriver::writeFlash(Transport& transport,
                                                     const uint8_t* data,
                                                     uint32_t address,
                                                     uint32_t size) {
    // Unlock both banks
    auto status = unlockBank(transport, FLASH_KEYR1, FLASH_CR1);
    if (status != ProgrammerStatus::Ok) return status;
    status = unlockBank(transport, FLASH_KEYR2, FLASH_CR2);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }

    // Clear error flags
    status = clearErrors(transport, FLASH_CCR1);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); lockBank(transport, FLASH_CR2); return status; }
    status = clearErrors(transport, FLASH_CCR2);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); lockBank(transport, FLASH_CR2); return status; }

    // Program 16 bytes (128-bit flash word) at a time
    for (uint32_t i = 0; i < size; i += PROG_WIDTH) {
        uint8_t flash_word[PROG_WIDTH];
        std::memset(flash_word, 0xFF, PROG_WIDTH);

        uint32_t remaining = size - i;
        uint32_t copy_size = (remaining < PROG_WIDTH) ? remaining : PROG_WIDTH;
        std::memcpy(flash_word, &data[i], copy_size);

        status = programFlashWord(transport, address + i, flash_word);
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

    lockBank(transport, FLASH_CR1);
    lockBank(transport, FLASH_CR2);
    return status;
}

ProgrammerStatus Stm32H7abFlashDriver::writeOptionBytes(Transport& transport,
                                                           const uint8_t* data,
                                                           uint32_t address,
                                                           uint32_t size,
                                                           bool unsafe) {
    auto status = unlockBank(transport, FLASH_KEYR1, FLASH_CR1);
    if (status != ProgrammerStatus::Ok) return status;
    status = unlockOptionBytes(transport);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }

    status = clearErrors(transport, FLASH_CCR1);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }

    if (!unsafe) {
        // SAFETY: reject RDP Level 2 — permanently disables debug access, IRREVERSIBLE.
        if (address <= FLASH_OPTSR_PRG && (address + size) > (FLASH_OPTSR_PRG + 1)) {
            uint8_t rdp = data[FLASH_OPTSR_PRG - address + 1];
            if (rdp == RDP_LEVEL_2) {
                m_error_ = "Rejected: RDP Level 2 (0xCC) permanently disables debug — chip cannot be recovered";
                lockBank(transport, FLASH_CR1);
                return ProgrammerStatus::ErrorProtected;
            }
        }
    }

    // Write option byte registers
    for (uint32_t i = 0; i < size; i += 4) {
        uint32_t word = 0xFFFFFFFF;
        for (uint32_t b = 0; b < 4 && (i + b) < size; b++)
            word = (word & ~(0xFFUL << (b * 8))) | (static_cast<uint32_t>(data[i + b]) << (b * 8));
        status = flash_write_word(transport, address + i, word);
        if (status != ProgrammerStatus::Ok) break;
    }

    if (status == ProgrammerStatus::Ok) {
        // Start option byte programming
        uint32_t optcr;
        status = flash_read_word(transport, FLASH_OPTCR, optcr);
        if (status == ProgrammerStatus::Ok)
            status = flash_write_word(transport, FLASH_OPTCR, optcr | OPTCR_OPTSTRT);
        if (status == ProgrammerStatus::Ok)
            status = waitReady(transport, FLASH_SR1);
    }

    lockBank(transport, FLASH_CR1);
    return status;
}

ProgrammerStatus Stm32H7abFlashDriver::writeOptionBytesMapped(Transport& transport,
                                                                 const ObWriteEntry* entries,
                                                                 size_t count,
                                                                 bool unsafe) {
    using namespace stm32h7ab_flash;

    auto status = unlockBank(transport, FLASH_KEYR1, FLASH_CR1);
    if (status != ProgrammerStatus::Ok) return status;
    status = unlockOptionBytes(transport);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }

    status = clearErrors(transport, FLASH_CCR1);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }

    if (!unsafe) {
        // SAFETY: reject RDP Level 2 — permanently disables debug access, IRREVERSIBLE.
        for (size_t i = 0; i < count; i++) {
            if (entries[i].addr == FLASH_OPTSR_PRG) {
                uint8_t rdp = static_cast<uint8_t>((entries[i].value >> 8) & 0xFF);
                if (rdp == RDP_LEVEL_2) {
                    m_error_ = "Rejected: RDP Level 2 (0xCC) permanently disables debug — chip cannot be recovered";
                    lockBank(transport, FLASH_CR1);
                    return ProgrammerStatus::ErrorProtected;
                }
                break;
            }
        }
    }

    for (size_t i = 0; i < count; i++) {
        status = flash_write_word(transport, entries[i].addr, entries[i].value);
        if (status != ProgrammerStatus::Ok) {
            lockBank(transport, FLASH_CR1);
            return status;
        }
    }

    uint32_t optcr;
    status = flash_read_word(transport, FLASH_OPTCR, optcr);
    if (status == ProgrammerStatus::Ok)
        status = flash_write_word(transport, FLASH_OPTCR, optcr | OPTCR_OPTSTRT);
    if (status == ProgrammerStatus::Ok)
        status = waitReady(transport, FLASH_SR1);

    lockBank(transport, FLASH_CR1);
    return status;
}

ProgrammerStatus Stm32H7abFlashDriver::writeOtp(Transport& transport,
                                                   const uint8_t* data,
                                                   uint32_t address,
                                                   uint32_t size) {
    // OTP (RM0455): 16-bit halfword writes via PG_OTP in FLASH_OPTCR.
    // OTP area: 0x08FFF000-0x08FFF3FF (1 KB, 16 blocks of 64 bytes).
    // Each halfword is programmed individually — no erase needed (bits go 1->0 only).
    if (address < OTP_BASE || (address + size) > (OTP_BASE + OTP_SIZE)) {
        m_error_ = "OTP address out of range (0x08FFF000-0x08FFF3FF)";
        return ProgrammerStatus::ErrorWrite;
    }

    auto status = unlockBank(transport, FLASH_KEYR1, FLASH_CR1);
    if (status != ProgrammerStatus::Ok) return status;
    status = unlockOptionBytes(transport);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }

    status = clearErrors(transport, FLASH_CCR1);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }

    // Wait for any previous operation to complete before setting PG_OTP
    status = waitReady(transport, FLASH_SR1);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }

    // Enable OTP programming mode (PG_OTP in OPTCR, gated by OPTLOCK)
    uint32_t optcr;
    status = flash_read_word(transport, FLASH_OPTCR, optcr);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }
    status = flash_write_word(transport, FLASH_OPTCR, optcr | OPTCR_PG_OTP);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }

    // Program halfwords
    for (uint32_t i = 0; i < size; i += 2) {
        uint16_t hw = static_cast<uint16_t>(data[i]);
        if (i + 1 < size)
            hw |= static_cast<uint16_t>(data[i + 1]) << 8;
        else
            hw |= 0xFF00;  // pad last odd byte with 0xFF (no-op for OTP)

        status = flash_write_halfword(transport, address + i, hw);
        if (status != ProgrammerStatus::Ok) break;

        status = waitReady(transport, FLASH_SR1);
        if (status != ProgrammerStatus::Ok) break;
    }

    // Disable OTP programming mode and lock
    flash_write_word(transport, FLASH_OPTCR, optcr & ~OPTCR_PG_OTP);
    lockBank(transport, FLASH_CR1);
    return status;
}
