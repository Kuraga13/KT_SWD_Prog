/**
 ******************************************************************************
 * @file           : stm32g4_flash.cpp
 * @brief          : Flash driver implementation for STM32G4 series
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "stm32g4_flash.h"
#include "flash_utils.h"

#include <cstring>

using namespace stm32g4_flash;

// ── Helpers ─────────────────────────────────────────────────

ProgrammerStatus Stm32G4FlashDriver::unlock(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_KEYR, KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_KEYR, KEY2);
}

ProgrammerStatus Stm32G4FlashDriver::lock(Transport& transport) {
    uint32_t cr;
    auto status = flash_read_word(transport, FLASH_CR, cr);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_CR, cr | CR_LOCK);
}

ProgrammerStatus Stm32G4FlashDriver::unlockOptionBytes(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY2);
}

ProgrammerStatus Stm32G4FlashDriver::waitReady(Transport& transport) {
    return flash_wait_busy(transport, FLASH_SR, SR_BSY, SR_ALL_ERRORS, 100000, &m_error_);
}

ProgrammerStatus Stm32G4FlashDriver::erasePage(Transport& transport, uint32_t page_num, bool bank2) {
    uint32_t cr = CR_PER | (static_cast<uint32_t>(page_num & CR_PNB_MASK) << CR_PNB_SHIFT);
    if (bank2) cr |= CR_BKER;
    auto status = flash_write_word(transport, FLASH_CR, cr);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, FLASH_CR, cr | CR_STRT);
    if (status != ProgrammerStatus::Ok) return status;
    return waitReady(transport);
}

ProgrammerStatus Stm32G4FlashDriver::programDoubleWord(Transport& transport,
                                                        uint32_t address,
                                                        uint32_t word_lo,
                                                        uint32_t word_hi) {
    auto status = flash_write_word(transport, FLASH_CR, CR_PG);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, address, word_lo);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, address + 4, word_hi);
    if (status != ProgrammerStatus::Ok) return status;
    return waitReady(transport);
}

RdpLevel Stm32G4FlashDriver::readRdpLevel(Transport& transport) {
    uint32_t optr;
    if (flash_read_word(transport, FLASH_OPTR, optr) != ProgrammerStatus::Ok)
        return RdpLevel::Unknown;
    uint8_t rdp = static_cast<uint8_t>(optr);
    if (rdp == RDP_LEVEL_0) return RdpLevel::Level0;
    if (rdp == RDP_LEVEL_2) return RdpLevel::Level2;
    return RdpLevel::Level1;
}

ProgrammerStatus Stm32G4FlashDriver::eraseFlash(Transport& transport) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;
    flash_write_word(transport, FLASH_SR, SR_ALL_ERRORS | SR_EOP);
    status = flash_write_word(transport, FLASH_CR, CR_MER1 | CR_MER2);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    status = flash_write_word(transport, FLASH_CR, CR_MER1 | CR_MER2 | CR_STRT);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    status = waitReady(transport);

    // Clear MER bits (HAL does this explicitly after mass erase)
    flash_write_word(transport, FLASH_CR, 0);

    // Flush ART Accelerator caches — required on Cortex-M4 after erase.
    // Stale I/D-cache entries from pre-erase flash content can cause PGAERR
    // during subsequent programming.  G0 (Cortex-M0+) has no cache.
    uint32_t acr;
    if (flash_read_word(transport, FLASH_ACR, acr) == ProgrammerStatus::Ok) {
        if (acr & ACR_ICEN) {
            flash_write_word(transport, FLASH_ACR, acr & ~ACR_ICEN);            // disable I-cache
            flash_write_word(transport, FLASH_ACR, (acr & ~ACR_ICEN) | ACR_ICRST); // reset
            flash_write_word(transport, FLASH_ACR, acr);                         // re-enable
        }
        if (acr & ACR_DCEN) {
            flash_write_word(transport, FLASH_ACR, acr & ~ACR_DCEN);            // disable D-cache
            flash_write_word(transport, FLASH_ACR, (acr & ~ACR_DCEN) | ACR_DCRST); // reset
            flash_write_word(transport, FLASH_ACR, acr);                         // re-enable
        }
    }

    lock(transport);
    return status;
}

ProgrammerStatus Stm32G4FlashDriver::writeFlash(Transport& transport,
                                                  const uint8_t* data,
                                                  uint32_t address,
                                                  uint32_t size) {
    if (use_stub_)
        return writeFlashFast(transport, data, address, size);
    return writeFlashSlow(transport, data, address, size);
}

ProgrammerStatus Stm32G4FlashDriver::writeFlashSlow(Transport& transport,
                                                      const uint8_t* data,
                                                      uint32_t address,
                                                      uint32_t size) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;

    // Clear error flags
    flash_write_word(transport, FLASH_SR, SR_ALL_ERRORS | SR_EOP);

    // Set PG once for the entire write (not per double-word)
    status = flash_write_word(transport, FLASH_CR, CR_PG);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    // Program double-words: write 8 bytes in one bulk transfer, then poll BSY
    for (uint32_t i = 0; i < size; i += PROG_WIDTH) {
        uint8_t dw[8];
        for (uint32_t b = 0; b < 8; b++)
            dw[b] = (i + b < size) ? data[i + b] : 0xFF;

        status = transport.writeMemory(dw, address + i, 8);
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

    // Clear PG bit
    flash_write_word(transport, FLASH_CR, 0);
    lock(transport);
    return status;
}

ProgrammerStatus Stm32G4FlashDriver::writeFlashFast(Transport& transport,
                                                      const uint8_t* data,
                                                      uint32_t address,
                                                      uint32_t size) {
    using namespace g4_flash_loader;

    // Halt core — required for setting registers and running the stub
    auto status = transport.haltCore();
    if (status != ProgrammerStatus::Ok) return status;

    // Upload stub to SRAM
    status = transport.writeMemory(stub, STUB_ADDR, STUB_SIZE);
    if (status != ProgrammerStatus::Ok) return status;

    // Unlock flash + clear error flags + ensure clean FLASH_CR
    status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, FLASH_SR, SR_ALL_ERRORS | SR_EOP);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    // Clear any stale bits in FLASH_CR (MER1/MER2 from prior erase, etc.)
    // LOCK and OPTLOCK are hardware-protected — writing 0 has no effect on them.
    status = flash_write_word(transport, FLASH_CR, 0);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    uint32_t offset = 0;
    while (offset < size) {
        // Determine chunk size (real data bytes in this iteration)
        uint32_t chunk_data = size - offset;
        if (chunk_data > BUFFER_SIZE) chunk_data = BUFFER_SIZE;

        // Round up to PROG_WIDTH (8 bytes) for double-word alignment
        uint32_t chunk_padded = (chunk_data + PROG_WIDTH - 1) / PROG_WIDTH * PROG_WIDTH;

        // Upload data to SRAM — full double-words directly from input
        uint32_t full = chunk_data & ~(PROG_WIDTH - 1);
        if (full > 0) {
            status = transport.writeMemory(data + offset, BUFFER_ADDR, full);
            if (status != ProgrammerStatus::Ok) break;
        }

        // Pad last partial double-word with 0xFF
        if (chunk_data > full) {
            uint8_t dw_buf[PROG_WIDTH];
            memset(dw_buf, 0xFF, PROG_WIDTH);
            memcpy(dw_buf, data + offset + full, chunk_data - full);
            status = transport.writeMemory(dw_buf, BUFFER_ADDR + full, PROG_WIDTH);
            if (status != ProgrammerStatus::Ok) break;
        }

        // Write config struct
        uint32_t config[5];
        config[0] = address + offset;  // flash_dest
        config[1] = BUFFER_ADDR;       // data_src
        config[2] = chunk_padded;       // data_size (multiple of 8)
        config[3] = STATUS_RUNNING;     // status
        config[4] = 0;                  // flash_sr
        status = transport.writeMemory(reinterpret_cast<const uint8_t*>(config),
                                        CONFIG_ADDR, sizeof(config));
        if (status != ProgrammerStatus::Ok) break;

        // Set CPU registers
        status = cortex_write_core_reg(transport, cortex_debug::REG_R0, CONFIG_ADDR, &m_error_);
        if (status != ProgrammerStatus::Ok) break;
        status = cortex_write_core_reg(transport, cortex_debug::REG_SP, SRAM_TOP, &m_error_);
        if (status != ProgrammerStatus::Ok) break;
        status = cortex_write_core_reg(transport, cortex_debug::REG_PC, STUB_ADDR, &m_error_);
        if (status != ProgrammerStatus::Ok) break;
        status = cortex_write_core_reg(transport, cortex_debug::REG_xPSR, 0x01000000, &m_error_);
        if (status != ProgrammerStatus::Ok) break;

        // Run stub
        status = transport.resumeCore();
        if (status != ProgrammerStatus::Ok) break;

        // Poll config.status until stub completes
        bool done = false;
        for (uint32_t poll = 0; poll < 500000; poll++) {
            uint32_t s;
            status = flash_read_word(transport, CONFIG_ADDR + 12, s);
            if (status != ProgrammerStatus::Ok) { done = true; break; }
            if (s == STATUS_OK) { done = true; break; }
            if (s == STATUS_ERROR) {
                m_error_ = "Flash loader stub error";
                uint32_t stub_sr;
                if (flash_read_word(transport, CONFIG_ADDR + 16, stub_sr) == ProgrammerStatus::Ok)
                    m_error_ += " (FLASH_SR=" + to_hex(stub_sr) + ")";
                done = true;
                status = ProgrammerStatus::ErrorWrite;
                break;
            }
        }
        if (!done) {
            m_error_ = "Flash loader stub timed out";
            transport.haltCore();
            status = ProgrammerStatus::ErrorWrite;
            break;
        }
        if (status != ProgrammerStatus::Ok) break;

        offset += chunk_padded;

        if (!reportProgress(offset < size ? offset : size, size)) {
            transport.haltCore();
            status = ProgrammerStatus::ErrorAborted;
            break;
        }
    }

    transport.haltCore();
    lock(transport);
    return status;
}

ProgrammerStatus Stm32G4FlashDriver::writeOptionBytes(Transport& transport,
                                                        const uint8_t* data,
                                                        uint32_t address,
                                                        uint32_t size, bool unsafe) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;
    status = unlockOptionBytes(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    // Clear stale error flags
    flash_write_word(transport, FLASH_SR, SR_ALL_ERRORS | SR_EOP);

    if (!unsafe) {
        if (address <= FLASH_OPTR && (address + size) > FLASH_OPTR) {
            uint8_t rdp = data[FLASH_OPTR - address];
            if (rdp == RDP_LEVEL_2) {
                m_error_ = "Rejected: RDP Level 2 (0xCC) permanently disables debug — chip cannot be recovered";
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

ProgrammerStatus Stm32G4FlashDriver::writeOptionBytesMapped(Transport& transport,
                                                               const ObWriteEntry* entries,
                                                               size_t count,
                                                               bool unsafe) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;

    status = unlockOptionBytes(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    if (!unsafe) {
        // SAFETY: reject RDP Level 2 — permanently disables debug, irreversible
        for (size_t i = 0; i < count; i++) {
            if (entries[i].addr == FLASH_OPTR) {
                uint8_t rdp = static_cast<uint8_t>(entries[i].value & 0xFF);
                if (rdp == RDP_LEVEL_2) {
                    m_error_ = "Rejected: RDP Level 2 (0xCC) permanently disables debug — chip cannot be recovered";
                    lock(transport);
                    return ProgrammerStatus::ErrorProtected;
                }
                break;
            }
        }
    }

    // Write all option byte registers first (no OPTSTRT yet)
    for (size_t i = 0; i < count; i++) {
        status = flash_write_word(transport, entries[i].addr, entries[i].value);
        if (status != ProgrammerStatus::Ok) {
            lock(transport);
            return status;
        }
    }

    // Single OPTSTRT to commit all option bytes atomically
    uint32_t cr;
    flash_read_word(transport, FLASH_CR, cr);
    flash_write_word(transport, FLASH_CR, cr | CR_OPTSTRT);
    status = waitReady(transport);

    // Launch option byte load
    if (status == ProgrammerStatus::Ok) {
        flash_read_word(transport, FLASH_CR, cr);
        flash_write_word(transport, FLASH_CR, cr | CR_OBL_LAUNCH);
    }

    lock(transport);
    return status;
}

ProgrammerStatus Stm32G4FlashDriver::writeOtp(Transport& transport,
                                                const uint8_t* data,
                                                uint32_t address,
                                                uint32_t size) {
    // OTP uses standard double-word programming (no fast mode)
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;

    flash_write_word(transport, FLASH_SR, SR_ALL_ERRORS | SR_EOP);

    for (uint32_t i = 0; i < size; i += PROG_WIDTH) {
        uint32_t word_lo = 0xFFFFFFFF;
        uint32_t word_hi = 0xFFFFFFFF;

        for (uint32_t b = 0; b < 4 && (i + b) < size; b++)
            word_lo = (word_lo & ~(0xFFUL << (b * 8))) | (static_cast<uint32_t>(data[i + b]) << (b * 8));
        for (uint32_t b = 0; b < 4 && (i + 4 + b) < size; b++)
            word_hi = (word_hi & ~(0xFFUL << (b * 8))) | (static_cast<uint32_t>(data[i + 4 + b]) << (b * 8));

        status = programDoubleWord(transport, address + i, word_lo, word_hi);
        if (status != ProgrammerStatus::Ok) break;
    }

    flash_write_word(transport, FLASH_CR, 0);
    lock(transport);
    return status;
}
