/**
 ******************************************************************************
 * @file           : stm32h7_flash.cpp
 * @brief          : Flash driver implementation for STM32H7 series
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "stm32h7_flash.h"
#include "flash_utils.h"
#include <cstring>

using namespace stm32h7_flash;

// ── Helpers ─────────────────────────────────────────────────

ProgrammerStatus Stm32H7FlashDriver::unlockBank(Transport& transport, uint32_t keyr, uint32_t cr) {
    auto status = flash_write_word(transport, keyr, KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, keyr, KEY2);
}

ProgrammerStatus Stm32H7FlashDriver::lockBank(Transport& transport, uint32_t cr) {
    uint32_t val;
    auto status = flash_read_word(transport, cr, val);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, cr, val | CR_LOCK);
}

ProgrammerStatus Stm32H7FlashDriver::unlockOptionBytes(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY2);
}

ProgrammerStatus Stm32H7FlashDriver::clearErrors(Transport& transport, uint32_t ccr) {
    return flash_write_word(transport, ccr, SR_ALL_ERRORS | SR_EOP);
}

ProgrammerStatus Stm32H7FlashDriver::waitReady(Transport& transport, uint32_t sr) {
    return flash_wait_busy(transport, sr, SR_BSY | SR_QW, SR_ALL_ERRORS, 100000, &m_error_);
}

ProgrammerStatus Stm32H7FlashDriver::eraseSector(Transport& transport, uint8_t sector,
                                                   uint32_t cr, uint32_t sr) {
    uint32_t val = CR_SER | CR_PSIZE_X64
                 | (static_cast<uint32_t>(sector & CR_SNB_MASK) << CR_SNB_SHIFT);
    auto status = flash_write_word(transport, cr, val);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, cr, val | CR_START);
    if (status != ProgrammerStatus::Ok) return status;
    return waitReady(transport, sr);
}

ProgrammerStatus Stm32H7FlashDriver::programFlashWord(Transport& transport,
                                                       uint32_t address,
                                                       const uint8_t* data) {
    // Determine which bank
    uint32_t cr = (address < BANK2_START) ? FLASH_CR1 : FLASH_CR2;
    uint32_t sr = (address < BANK2_START) ? FLASH_SR1 : FLASH_SR2;

    auto status = flash_write_word(transport, cr, CR_PG | CR_PSIZE_X64);
    if (status != ProgrammerStatus::Ok) return status;

    // Write 32 bytes (8 words) — this triggers the 256-bit programming
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

RdpLevel Stm32H7FlashDriver::readRdpLevel(Transport& transport) {
    uint32_t optsr;
    if (flash_read_word(transport, FLASH_OPTSR_CUR, optsr) != ProgrammerStatus::Ok)
        return RdpLevel::Unknown;
    uint8_t rdp = static_cast<uint8_t>(optsr >> 8);
    if (rdp == RDP_LEVEL_0) return RdpLevel::Level0;
    if (rdp == RDP_LEVEL_2) return RdpLevel::Level2;
    return RdpLevel::Level1;
}

ProgrammerStatus Stm32H7FlashDriver::eraseFlash(Transport& transport) {
    // Unlock and erase both banks
    auto status = unlockBank(transport, FLASH_KEYR1, FLASH_CR1);
    if (status != ProgrammerStatus::Ok) return status;

    status = clearErrors(transport, FLASH_CCR1);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }
    status = flash_write_word(transport, FLASH_CR1, CR_BER | CR_PSIZE_X64);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }
    status = flash_write_word(transport, FLASH_CR1, CR_BER | CR_PSIZE_X64 | CR_START);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }
    status = waitReady(transport, FLASH_SR1);
    lockBank(transport, FLASH_CR1);
    if (status != ProgrammerStatus::Ok) return status;

    // Bank 2
    status = unlockBank(transport, FLASH_KEYR2, FLASH_CR2);
    if (status != ProgrammerStatus::Ok) return status;

    status = clearErrors(transport, FLASH_CCR2);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR2); return status; }
    status = flash_write_word(transport, FLASH_CR2, CR_BER | CR_PSIZE_X64);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR2); return status; }
    status = flash_write_word(transport, FLASH_CR2, CR_BER | CR_PSIZE_X64 | CR_START);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR2); return status; }
    status = waitReady(transport, FLASH_SR2);
    lockBank(transport, FLASH_CR2);

    return status;
}

ProgrammerStatus Stm32H7FlashDriver::writeFlash(Transport& transport,
                                                  const uint8_t* data,
                                                  uint32_t address,
                                                  uint32_t size) {
    if (use_stub_)
        return writeFlashFast(transport, data, address, size);
    return writeFlashSlow(transport, data, address, size);
}

ProgrammerStatus Stm32H7FlashDriver::writeFlashSlow(Transport& transport,
                                                      const uint8_t* data,
                                                      uint32_t address,
                                                      uint32_t size) {
    // Unlock the appropriate bank(s)
    auto status = unlockBank(transport, FLASH_KEYR1, FLASH_CR1);
    if (status != ProgrammerStatus::Ok) return status;
    status = unlockBank(transport, FLASH_KEYR2, FLASH_CR2);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }

    // Clear error flags from any previous operation
    status = clearErrors(transport, FLASH_CCR1);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); lockBank(transport, FLASH_CR2); return status; }
    status = clearErrors(transport, FLASH_CCR2);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); lockBank(transport, FLASH_CR2); return status; }

    // Program 32 bytes (flash word) at a time
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

ProgrammerStatus Stm32H7FlashDriver::writeFlashFast(Transport& transport,
                                                      const uint8_t* data,
                                                      uint32_t address,
                                                      uint32_t size) {
    using namespace h7_flash_loader;

    // H7 requires 256-bit (32-byte) aligned flash word programming
    if (address % PROG_WIDTH != 0) {
        m_error_ = "Flash address must be 32-byte aligned for stub programming";
        return ProgrammerStatus::ErrorWrite;
    }

    // Halt core — required for setting registers and running the stub
    auto status = transport.haltCore();
    if (status != ProgrammerStatus::Ok) return status;

    // Upload stub to DTCM-RAM
    status = transport.writeMemory(stub, STUB_ADDR, STUB_SIZE);
    if (status != ProgrammerStatus::Ok) return status;

    // Unlock both banks + clear error flags
    status = unlockBank(transport, FLASH_KEYR1, FLASH_CR1);
    if (status != ProgrammerStatus::Ok) return status;
    status = unlockBank(transport, FLASH_KEYR2, FLASH_CR2);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); return status; }
    status = clearErrors(transport, FLASH_CCR1);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); lockBank(transport, FLASH_CR2); return status; }
    status = clearErrors(transport, FLASH_CCR2);
    if (status != ProgrammerStatus::Ok) { lockBank(transport, FLASH_CR1); lockBank(transport, FLASH_CR2); return status; }

    uint32_t offset = 0;
    while (offset < size) {
        uint32_t current_addr = address + offset;

        // Determine chunk size (real data bytes in this iteration)
        uint32_t chunk_data = size - offset;
        if (chunk_data > BUFFER_SIZE) chunk_data = BUFFER_SIZE;

        // Don't let a single chunk cross the bank boundary
        if (current_addr < BANK2_START && current_addr + chunk_data > BANK2_START)
            chunk_data = BANK2_START - current_addr;

        // Round up to flash word alignment (32 bytes)
        uint32_t chunk_padded = (chunk_data + PROG_WIDTH - 1) / PROG_WIDTH * PROG_WIDTH;

        // Upload data to SRAM buffer — full flash words directly from input
        uint32_t full = chunk_data & ~(PROG_WIDTH - 1);
        if (full > 0) {
            status = transport.writeMemory(data + offset, BUFFER_ADDR, full);
            if (status != ProgrammerStatus::Ok) break;
        }

        // Pad last partial flash word with 0xFF
        if (chunk_data > full) {
            uint8_t fw_buf[PROG_WIDTH];
            std::memset(fw_buf, 0xFF, PROG_WIDTH);
            std::memcpy(fw_buf, data + offset + full, chunk_data - full);
            status = transport.writeMemory(fw_buf, BUFFER_ADDR + full, PROG_WIDTH);
            if (status != ProgrammerStatus::Ok) break;
        }

        // Select bank-specific registers
        uint32_t cr_addr = (current_addr < BANK2_START) ? FLASH_CR1 : FLASH_CR2;
        uint32_t sr_addr = (current_addr < BANK2_START) ? FLASH_SR1 : FLASH_SR2;

        // Write config struct
        uint32_t config[7];
        config[0] = current_addr;       // flash_dest
        config[1] = BUFFER_ADDR;        // data_src
        config[2] = chunk_padded;        // data_size (multiple of 32)
        config[3] = STATUS_RUNNING;      // status
        config[4] = 0;                   // flash_sr
        config[5] = cr_addr;             // FLASH_CR address
        config[6] = sr_addr;             // FLASH_SR address
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

        // Poll config.status until stub completes.
        // On error the stub writes flash_sr (config[16]) before status (config[12]),
        // then hits BKPT — so flash_sr is guaranteed valid when we read it here.
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
    lockBank(transport, FLASH_CR1);
    lockBank(transport, FLASH_CR2);
    return status;
}

ProgrammerStatus Stm32H7FlashDriver::writeOptionBytes(Transport& transport,
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
        // SAFETY: reject RDP Level 2 — permanently disables debug, irreversible
        // H7 OPTSR: RDP is in bits [15:8], i.e. byte offset 1 within the register
        if (address <= FLASH_OPTSR_PRG && (address + size) > (FLASH_OPTSR_PRG + 1)) {
            uint8_t rdp = data[FLASH_OPTSR_PRG - address + 1];
            if (rdp == RDP_LEVEL_2) {
                m_error_ = "RDP Level 2 would permanently lock the chip";
                lockBank(transport, FLASH_CR1);
                return ProgrammerStatus::ErrorProtected;
            }
        }
    }

    // Write option byte registers (OPTSR_PRG and others)
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
        flash_read_word(transport, FLASH_OPTCR, optcr);
        flash_write_word(transport, FLASH_OPTCR, optcr | OPTCR_OPTSTRT);
        status = waitReady(transport, FLASH_SR1);
    }

    lockBank(transport, FLASH_CR1);
    return status;
}

ProgrammerStatus Stm32H7FlashDriver::writeOtp(Transport& transport,
                                                const uint8_t* data,
                                                uint32_t address,
                                                uint32_t size) {
    // OTP is not available on H74x/H75x (RM0433) or H72x/H73x (RM0468).
    // H7Ax/H7Bx OTP is handled by the dedicated stm32h7ab_flash driver.
    m_error_ = "OTP is not available on this H7 variant";
    return ProgrammerStatus::ErrorWrite;
}

ProgrammerStatus Stm32H7FlashDriver::onDisconnect(Transport& transport) {
    // Trigger a clean system reset via Cortex-M AIRCR register.
    // This resets the chip independently of debug state, so the core
    // boots normally from the reset vector after the probe disconnects.
    constexpr uint32_t AIRCR              = 0xE000ED0C;
    constexpr uint32_t AIRCR_VECTKEY      = 0x05FA0000;
    constexpr uint32_t AIRCR_SYSRESETREQ  = (1UL << 2);
    flash_write_word(transport, AIRCR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);
    return ProgrammerStatus::Ok;
}
