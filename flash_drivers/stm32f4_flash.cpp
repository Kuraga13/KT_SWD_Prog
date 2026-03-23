/**
 ******************************************************************************
 * @file           : stm32f4_flash.cpp
 * @brief          : Flash driver implementation for STM32F4 series
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "stm32f4_flash.h"
#include "flash_utils.h"
#include <algorithm>
#include <cstring>

using namespace stm32f4_flash;

// ── Hooks ───────────────────────────────────────────────────

ProgrammerStatus Stm32F4FlashDriver::onConnect(Transport& transport) {
    return transport.haltCore();
}

ProgrammerStatus Stm32F4FlashDriver::onDisconnect(Transport& transport) {
    // Trigger a clean system reset via Cortex-M AIRCR register.
    // This resets the chip independently of debug state, so the core
    // boots normally from the reset vector after the probe disconnects.
    constexpr uint32_t AIRCR          = 0xE000ED0C;
    constexpr uint32_t AIRCR_VECTKEY  = 0x05FA0000;
    constexpr uint32_t AIRCR_SYSRESETREQ = (1UL << 2);
    flash_write_word(transport, AIRCR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);
    return ProgrammerStatus::Ok;
}

// ── Helpers ─────────────────────────────────────────────────

ProgrammerStatus Stm32F4FlashDriver::unlock(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_KEYR, KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_KEYR, KEY2);
}

ProgrammerStatus Stm32F4FlashDriver::lock(Transport& transport) {
    uint32_t cr;
    auto status = flash_read_word(transport, FLASH_CR, cr);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_CR, cr | CR_LOCK);
}

ProgrammerStatus Stm32F4FlashDriver::unlockOptionBytes(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY2);
}

ProgrammerStatus Stm32F4FlashDriver::waitReady(Transport& transport) {
    return flash_wait_busy(transport, FLASH_SR, SR_BSY, SR_ERR_MASK, 100000, &m_error_);
}

ProgrammerStatus Stm32F4FlashDriver::clearErrors(Transport& transport) {
    return flash_write_word(transport, FLASH_SR, SR_ERR_MASK | SR_EOP);
}

ProgrammerStatus Stm32F4FlashDriver::eraseSector(Transport& transport, uint8_t sector) {
    // Dual-bank SNB encoding: bank 2 sectors 12-23 map to SNB 16-27 (bit 4 = bank select)
    uint8_t snb = sector;
    if (sector >= SECTORS_PER_BANK)
        snb = (sector - SECTORS_PER_BANK) + 16;

    uint32_t cr = CR_SER | CR_PSIZE_X32
                | (static_cast<uint32_t>(snb & CR_SNB_MASK) << CR_SNB_SHIFT);
    auto status = flash_write_word(transport, FLASH_CR, cr);
    if (status != ProgrammerStatus::Ok) return status;

    status = flash_write_word(transport, FLASH_CR, cr | CR_STRT);
    if (status != ProgrammerStatus::Ok) return status;

    return waitReady(transport);
}

// ── Protection ──────────────────────────────────────────────

RdpLevel Stm32F4FlashDriver::readRdpLevel(Transport& transport) {
    uint32_t optcr;
    if (flash_read_word(transport, FLASH_OPTCR, optcr) != ProgrammerStatus::Ok)
        return RdpLevel::Unknown;

    uint8_t rdp = static_cast<uint8_t>(optcr >> 8);
    if (rdp == RDP_LEVEL_0) return RdpLevel::Level0;
    if (rdp == RDP_LEVEL_2) return RdpLevel::Level2;
    return RdpLevel::Level1;
}

// ── Erase ───────────────────────────────────────────────────

ProgrammerStatus Stm32F4FlashDriver::eraseFlash(Transport& transport) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;

    status = clearErrors(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    // Check if any sectors are write-protected and remove protection first
    uint32_t optcr;
    status = flash_read_word(transport, FLASH_OPTCR, optcr);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    uint32_t nwrp = (optcr >> 16) & 0xFFF;
    if (nwrp != 0xFFF) {
        // Some sectors are write-protected — clear WRP via option bytes
        status = unlockOptionBytes(transport);
        if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

        uint32_t new_optcr = (optcr & 0xF000FFFF) | (0xFFFUL << 16);
        new_optcr &= ~(OPTCR_OPTLOCK | OPTCR_OPTSTRT);

        status = flash_write_word(transport, FLASH_OPTCR, new_optcr);
        if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

        status = flash_write_word(transport, FLASH_OPTCR, new_optcr | OPTCR_OPTSTRT);
        if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

        status = waitReady(transport);
        if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    }

    if (!progress_cb_) {
        // No progress callback — use hardware mass erase (fastest)
        // CR_MER1 erases bank 2 on dual-bank parts; ignored on single-bank
        uint32_t cr = CR_MER | CR_MER1 | CR_PSIZE_X32;
        status = flash_write_word(transport, FLASH_CR, cr);
        if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

        status = flash_write_word(transport, FLASH_CR, cr | CR_STRT);
        if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

        status = waitReady(transport);
    } else {
        // Progress callback set — erase sector by sector for progress reporting
        uint16_t flash_kb = 0;
        auto rd = transport.readMemory(reinterpret_cast<uint8_t*>(&flash_kb), FLASH_SIZE_REG, 2);
        if (rd != ProgrammerStatus::Ok || flash_kb == 0) flash_kb = 1024;  // default 1 MB

        uint32_t flash_size = static_cast<uint32_t>(flash_kb) * 1024;

        // Count sectors that fit in the flash (max 2 banks x 12 sectors)
        uint8_t num_sectors = 0;
        uint32_t cumulative = 0;
        while (cumulative < flash_size && num_sectors < 2 * SECTORS_PER_BANK) {
            cumulative += sectorSize(num_sectors);
            num_sectors++;
        }

        uint32_t erased = 0;
        for (uint8_t s = 0; s < num_sectors; s++) {
            status = eraseSector(transport, s);
            if (status != ProgrammerStatus::Ok) break;

            erased += sectorSize(s);
            if (!reportProgress(erased, flash_size)) {
                status = ProgrammerStatus::ErrorAborted;
                break;
            }
        }
    }

    flash_write_word(transport, FLASH_CR, 0);
    lock(transport);
    return status;
}

// ── Flash write ─────────────────────────────────────────────

ProgrammerStatus Stm32F4FlashDriver::writeFlash(Transport& transport,
                                                  const uint8_t* data,
                                                  uint32_t address,
                                                  uint32_t size) {
    if (use_stub_)
        return writeFlashFast(transport, data, address, size);
    return writeFlashSlow(transport, data, address, size);
}

ProgrammerStatus Stm32F4FlashDriver::writeFlashSlow(Transport& transport,
                                                      const uint8_t* data,
                                                      uint32_t address,
                                                      uint32_t size) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;

    status = clearErrors(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    // Set PG + PSIZE=x32 once for the entire programming session
    status = flash_write_word(transport, FLASH_CR, CR_PG | CR_PSIZE_X32);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    // Program word at a time
    for (uint32_t i = 0; i < size; i += PROG_WIDTH) {
        uint32_t word = 0xFFFFFFFF;
        uint32_t remain = std::min(PROG_WIDTH, size - i);
        for (uint32_t b = 0; b < remain; b++)
            word = (word & ~(0xFFUL << (b * 8)))
                 | (static_cast<uint32_t>(data[i + b]) << (b * 8));

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

    // Clear PG
    flash_write_word(transport, FLASH_CR, 0);
    lock(transport);
    return status;
}

ProgrammerStatus Stm32F4FlashDriver::writeFlashFast(Transport& transport,
                                                      const uint8_t* data,
                                                      uint32_t address,
                                                      uint32_t size) {
    using namespace f4_flash_loader;

    // Upload stub to SRAM
    auto status = transport.writeMemory(stub, STUB_ADDR, STUB_SIZE);
    if (status != ProgrammerStatus::Ok) return status;

    // Unlock flash + clear error flags
    status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;
    status = clearErrors(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    uint32_t offset = 0;
    while (offset < size) {
        // Determine chunk size (real data bytes in this iteration)
        uint32_t chunk_data = size - offset;
        if (chunk_data > BUFFER_SIZE) chunk_data = BUFFER_SIZE;

        // Round up to word alignment for the stub
        uint32_t chunk_padded = (chunk_data + PROG_WIDTH - 1) / PROG_WIDTH * PROG_WIDTH;

        // Upload data to SRAM — full words directly from input
        uint32_t full = chunk_data & ~(PROG_WIDTH - 1);
        if (full > 0) {
            status = transport.writeMemory(data + offset, BUFFER_ADDR, full);
            if (status != ProgrammerStatus::Ok) break;
        }

        // Pad last partial word with 0xFF
        if (chunk_data > full) {
            uint8_t word_buf[PROG_WIDTH];
            memset(word_buf, 0xFF, PROG_WIDTH);
            memcpy(word_buf, data + offset + full, chunk_data - full);
            status = transport.writeMemory(word_buf, BUFFER_ADDR + full, PROG_WIDTH);
            if (status != ProgrammerStatus::Ok) break;
        }

        // Write config struct
        uint32_t config[5];
        config[0] = address + offset;   // flash_dest
        config[1] = BUFFER_ADDR;        // data_src
        config[2] = chunk_padded;        // data_size (multiple of 4)
        config[3] = STATUS_RUNNING;      // status
        config[4] = 0;                   // flash_sr
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

// ── Option bytes ────────────────────────────────────────────

ProgrammerStatus Stm32F4FlashDriver::writeOptionBytes(Transport& transport,
                                                        const uint8_t* data,
                                                        uint32_t address,
                                                        uint32_t size,
                                                        bool unsafe) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;

    status = unlockOptionBytes(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    // Data is expected in FLASH_OPTCR register format (4 bytes LE).
    // Write OPTCR (and optionally OPTCR1 for dual-bank parts).
    if (size >= 4) {
        uint32_t optcr = static_cast<uint32_t>(data[0])
                       | (static_cast<uint32_t>(data[1]) << 8)
                       | (static_cast<uint32_t>(data[2]) << 16)
                       | (static_cast<uint32_t>(data[3]) << 24);

        if (!unsafe) {
            // SAFETY: reject RDP Level 2 — permanently disables debug, irreversible
            uint8_t rdp = static_cast<uint8_t>(optcr >> 8);
            if (rdp == RDP_LEVEL_2) {
                m_error_ = "RDP Level 2 would permanently lock the chip";
                lock(transport);
                return ProgrammerStatus::ErrorProtected;
            }

            // SAFETY: force all sectors unprotected to prevent accidental lockout.
            // Sector write protection blocks mass erase and can only be removed
            // by reprogramming option bytes — which requires a working SWD link.
            optcr |= (0xFFFUL << 16);
        }

        // Strip control bits, keep configuration fields only
        optcr &= ~(OPTCR_OPTLOCK | OPTCR_OPTSTRT);

        status = flash_write_word(transport, FLASH_OPTCR, optcr);
        if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

        // Set OPTSTRT to apply
        status = flash_write_word(transport, FLASH_OPTCR, optcr | OPTCR_OPTSTRT);
        if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

        status = waitReady(transport);
    }

    // OPTCR1 for dual-bank parts (F42x/F43x)
    if (status == ProgrammerStatus::Ok && size >= 8) {
        uint32_t optcr1 = static_cast<uint32_t>(data[4])
                        | (static_cast<uint32_t>(data[5]) << 8)
                        | (static_cast<uint32_t>(data[6]) << 16)
                        | (static_cast<uint32_t>(data[7]) << 24);

        status = flash_write_word(transport, FLASH_OPTCR1, optcr1);
        if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

        // Trigger OPTSTRT to commit OPTCR1
        uint32_t optcr_val;
        status = flash_read_word(transport, FLASH_OPTCR, optcr_val);
        if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

        optcr_val &= ~OPTCR_OPTLOCK;
        status = flash_write_word(transport, FLASH_OPTCR, optcr_val | OPTCR_OPTSTRT);
        if (status == ProgrammerStatus::Ok)
            status = waitReady(transport);
    }

    lock(transport);
    return status;
}

// ── OTP ─────────────────────────────────────────────────────

ProgrammerStatus Stm32F4FlashDriver::writeOtp(Transport& transport,
                                                const uint8_t* data,
                                                uint32_t address,
                                                uint32_t size) {
    return writeFlashSlow(transport, data, address, size);
}
