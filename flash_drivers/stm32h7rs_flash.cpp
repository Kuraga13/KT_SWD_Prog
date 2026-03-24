/**
  ******************************************************************************
  * @file           : stm32h7rs_flash.cpp
  * @brief          : Flash driver implementation for STM32H7Rx/H7Sx (RM0477)
  * @author         : Evgeny Kudryavtsev
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Kuraga Tech.
  * Licensed under the MIT License. See LICENSE file for details.
  *
  ******************************************************************************
  */

#include "stm32h7rs_flash.h"
#include "flash_utils.h"
#include <cstring>

using namespace stm32h7rs_flash;

// ── Helpers ─────────────────────────────────────────────────

ProgrammerStatus Stm32H7rsFlashDriver::unlockFlash(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_KEYR, KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_KEYR, KEY2);
}

ProgrammerStatus Stm32H7rsFlashDriver::lockFlash(Transport& transport) {
    uint32_t val;
    auto status = flash_read_word(transport, FLASH_CR, val);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_CR, val | CR_LOCK);
}

ProgrammerStatus Stm32H7rsFlashDriver::unlockOptionBytes(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY2);
}

ProgrammerStatus Stm32H7rsFlashDriver::clearErrors(Transport& transport) {
    // RM0477: error flags are in FLASH_ISR, cleared by writing 1s to FLASH_ICR
    return flash_write_word(transport, FLASH_ICR, ISR_ALL_ERRORS | ISR_EOPF);
}

ProgrammerStatus Stm32H7rsFlashDriver::waitReady(Transport& transport) {
    // RM0477: busy flags in FLASH_SR, error flags in FLASH_ISR (separate registers)
    // First poll SR for busy to clear
    for (uint32_t i = 0; i < 100000; i++) {
        uint32_t sr;
        auto status = flash_read_word(transport, FLASH_SR, sr);
        if (status != ProgrammerStatus::Ok) {
            m_error_ = "Failed to read FLASH_SR at " + to_hex(FLASH_SR);
            return status;
        }

        if (!(sr & (SR_BUSY | SR_QW))) {
            // Busy cleared — now check ISR for errors
            uint32_t isr;
            status = flash_read_word(transport, FLASH_ISR, isr);
            if (status != ProgrammerStatus::Ok) {
                m_error_ = "Failed to read FLASH_ISR at " + to_hex(FLASH_ISR);
                return status;
            }
            if (isr & ISR_ALL_ERRORS) {
                m_error_ = "FLASH_ISR error bits: " + to_hex(isr & ISR_ALL_ERRORS)
                         + " (ISR=" + to_hex(isr) + ")";
                return ProgrammerStatus::ErrorWrite;
            }
            return ProgrammerStatus::Ok;
        }
    }
    m_error_ = "FLASH_SR busy timeout (SR at " + to_hex(FLASH_SR) + ")";
    return ProgrammerStatus::ErrorWrite;
}

ProgrammerStatus Stm32H7rsFlashDriver::eraseSector(Transport& transport, uint8_t sector) {
    // RM0477: SSN at bits [8:6], START at bit 5 (no PSIZE)
    uint32_t val = CR_SER
                 | (static_cast<uint32_t>(sector & CR_SSN_MASK) << CR_SSN_SHIFT);
    auto status = flash_write_word(transport, FLASH_CR, val);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, FLASH_CR, val | CR_START);
    if (status != ProgrammerStatus::Ok) return status;
    return waitReady(transport);
}

ProgrammerStatus Stm32H7rsFlashDriver::programFlashWord(Transport& transport,
                                                          uint32_t address,
                                                          const uint8_t* data) {
    // RM0477: set PG only (128-bit is the natural flash word, no FW needed)
    auto status = flash_write_word(transport, FLASH_CR, CR_PG);
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

    return waitReady(transport);
}

// ── Public interface ────────────────────────────────────────

RdpLevel Stm32H7rsFlashDriver::readRdpLevel(Transport& transport) {
    // RM0477: no RDP — uses NVSTATE in FLASH_NVSR[7:0]
    uint32_t nvsr;
    if (flash_read_word(transport, FLASH_NVSR, nvsr) != ProgrammerStatus::Ok)
        return RdpLevel::Unknown;
    uint8_t nvstate = static_cast<uint8_t>(nvsr & 0xFF);
    if (nvstate == NVSTATE_OPEN)   return RdpLevel::Level0;
    if (nvstate == NVSTATE_CLOSED) return RdpLevel::Level1;
    // Any other value — likely LOCKED state (irreversible)
    return RdpLevel::Level2;
}

ProgrammerStatus Stm32H7rsFlashDriver::eraseFlash(Transport& transport) {
    // Single-bank mass erase: BER + START (no PSIZE on RM0477)
    auto status = unlockFlash(transport);
    if (status != ProgrammerStatus::Ok) return status;

    status = clearErrors(transport);
    if (status != ProgrammerStatus::Ok) { lockFlash(transport); return status; }
    status = flash_write_word(transport, FLASH_CR, CR_BER);
    if (status != ProgrammerStatus::Ok) { lockFlash(transport); return status; }
    status = flash_write_word(transport, FLASH_CR, CR_BER | CR_START);
    if (status != ProgrammerStatus::Ok) { lockFlash(transport); return status; }
    status = waitReady(transport);
    lockFlash(transport);

    return status;
}

ProgrammerStatus Stm32H7rsFlashDriver::writeFlash(Transport& transport,
                                                     const uint8_t* data,
                                                     uint32_t address,
                                                     uint32_t size) {
    auto status = unlockFlash(transport);
    if (status != ProgrammerStatus::Ok) return status;

    status = clearErrors(transport);
    if (status != ProgrammerStatus::Ok) { lockFlash(transport); return status; }

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

    lockFlash(transport);
    return status;
}

ProgrammerStatus Stm32H7rsFlashDriver::writeOptionBytes(Transport& transport,
                                                           const uint8_t* data,
                                                           uint32_t address,
                                                           uint32_t size,
                                                           bool unsafe) {
    auto status = unlockFlash(transport);
    if (status != ProgrammerStatus::Ok) return status;
    status = unlockOptionBytes(transport);
    if (status != ProgrammerStatus::Ok) { lockFlash(transport); return status; }

    status = clearErrors(transport);
    if (status != ProgrammerStatus::Ok) { lockFlash(transport); return status; }

    if (!unsafe) {
        // SAFETY: reject NVSTATE transition to LOCKED — permanently disables debug.
        // NVSTATE_CLOSED (0x51) is recoverable via Device Authority (DA).
        // Any value other than OPEN or CLOSED means LOCKED — irreversible.
        if (address <= FLASH_NVSRP && (address + size) > FLASH_NVSRP) {
            uint32_t offset = FLASH_NVSRP - address;
            uint8_t nvstate = data[offset];
            if (nvstate != NVSTATE_OPEN && nvstate != NVSTATE_CLOSED) {
                char hex_buf[8];
                snprintf(hex_buf, sizeof(hex_buf), "0x%02X", nvstate);
                m_error_ = std::string("Rejected: NVSTATE value ")
                         + hex_buf
                         + " would permanently disable debug — chip cannot be recovered";
                lockFlash(transport);
                return ProgrammerStatus::ErrorProtected;
            }
        }
    }

    // Write option byte registers (PRG registers)
    for (uint32_t i = 0; i < size; i += 4) {
        uint32_t word = 0xFFFFFFFF;
        for (uint32_t b = 0; b < 4 && (i + b) < size; b++)
            word = (word & ~(0xFFUL << (b * 8))) | (static_cast<uint32_t>(data[i + b]) << (b * 8));
        status = flash_write_word(transport, address + i, word);
        if (status != ProgrammerStatus::Ok) break;
    }

    if (status == ProgrammerStatus::Ok) {
        // Start option byte programming via PG_OPT in OPTCR
        uint32_t optcr;
        status = flash_read_word(transport, FLASH_OPTCR, optcr);
        if (status == ProgrammerStatus::Ok)
            status = flash_write_word(transport, FLASH_OPTCR, optcr | OPTCR_PG_OPT);
        if (status == ProgrammerStatus::Ok)
            status = waitReady(transport);
    }

    lockFlash(transport);
    return status;
}

ProgrammerStatus Stm32H7rsFlashDriver::writeOptionBytesMapped(Transport& transport,
                                                                 const ObWriteEntry* entries,
                                                                 size_t count,
                                                                 bool unsafe) {
    auto status = unlockFlash(transport);
    if (status != ProgrammerStatus::Ok) return status;
    status = unlockOptionBytes(transport);
    if (status != ProgrammerStatus::Ok) { lockFlash(transport); return status; }

    status = clearErrors(transport);
    if (status != ProgrammerStatus::Ok) { lockFlash(transport); return status; }

    if (!unsafe) {
        // SAFETY: reject NVSTATE transition to LOCKED — permanently disables debug.
        for (size_t i = 0; i < count; i++) {
            if (entries[i].addr == FLASH_NVSRP) {
                uint8_t nvstate = static_cast<uint8_t>(entries[i].value & 0xFF);
                if (nvstate != NVSTATE_OPEN && nvstate != NVSTATE_CLOSED) {
                    m_error_ = "Rejected: NVSTATE would permanently disable debug — chip cannot be recovered";
                    lockFlash(transport);
                    return ProgrammerStatus::ErrorProtected;
                }
                break;
            }
        }
    }

    // Write all PRG registers
    for (size_t i = 0; i < count; i++) {
        status = flash_write_word(transport, entries[i].addr, entries[i].value);
        if (status != ProgrammerStatus::Ok) {
            lockFlash(transport);
            return status;
        }
    }

    // Single PG_OPT to commit all option bytes atomically
    uint32_t optcr;
    status = flash_read_word(transport, FLASH_OPTCR, optcr);
    if (status == ProgrammerStatus::Ok)
        status = flash_write_word(transport, FLASH_OPTCR, optcr | OPTCR_PG_OPT);
    if (status == ProgrammerStatus::Ok)
        status = waitReady(transport);

    lockFlash(transport);
    return status;
}

ProgrammerStatus Stm32H7rsFlashDriver::writeOtp(Transport& transport,
                                                   const uint8_t* data,
                                                   uint32_t address,
                                                   uint32_t size) {
    // OTP (RM0477): halfword writes via PG_OTP + FW in FLASH_CR.
    // OTP area: 0x08FFF000-0x08FFF3FF (1 KB, 16 blocks of 64 bytes).
    // Each halfword is programmed individually — no erase needed (bits go 1->0 only).
    // Sequence: set PG_OTP, write halfword to OTP address, set FW, wait for completion.
    if (address < OTP_BASE || (address + size) > (OTP_BASE + OTP_SIZE)) {
        m_error_ = "OTP address out of range (0x08FFF000-0x08FFF3FF)";
        return ProgrammerStatus::ErrorWrite;
    }

    auto status = unlockFlash(transport);
    if (status != ProgrammerStatus::Ok) return status;

    status = clearErrors(transport);
    if (status != ProgrammerStatus::Ok) { lockFlash(transport); return status; }

    status = waitReady(transport);
    if (status != ProgrammerStatus::Ok) { lockFlash(transport); return status; }

    // Program halfwords: set PG_OTP, write 16-bit value, set FW to trigger programming
    for (uint32_t i = 0; i < size; i += 2) {
        // Enable OTP programming mode
        status = flash_write_word(transport, FLASH_CR, CR_PG_OTP);
        if (status != ProgrammerStatus::Ok) break;

        // Build halfword (pad last odd byte with 0xFF)
        uint16_t hw = static_cast<uint16_t>(data[i]);
        if (i + 1 < size)
            hw |= static_cast<uint16_t>(data[i + 1]) << 8;
        else
            hw |= 0xFF00;  // pad last odd byte with 0xFF (no-op for OTP)

        // Write halfword to OTP address
        status = flash_write_halfword(transport, address + i, hw);
        if (status != ProgrammerStatus::Ok) break;

        // Trigger programming via Force Write
        status = flash_write_word(transport, FLASH_CR, CR_PG_OTP | CR_FW);
        if (status != ProgrammerStatus::Ok) break;

        status = waitReady(transport);
        if (status != ProgrammerStatus::Ok) break;
    }

    // Clear PG_OTP and lock
    flash_write_word(transport, FLASH_CR, 0);
    lockFlash(transport);
    return status;
}
