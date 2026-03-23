/**
 ******************************************************************************
 * @file           : stm32f1_flash.cpp
 * @brief          : Flash driver implementation for STM32F1 series
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "stm32f1_flash.h"
#include "flash_utils.h"
#include <cstring>

using namespace stm32f1_flash;

uint32_t Stm32F1FlashDriver::keyrReg(uint32_t addr) { return (addr >= BANK2_START) ? FLASH_KEYR2 : FLASH_KEYR; }
uint32_t Stm32F1FlashDriver::srReg(uint32_t addr)   { return (addr >= BANK2_START) ? FLASH_SR2   : FLASH_SR; }
uint32_t Stm32F1FlashDriver::crReg(uint32_t addr)   { return (addr >= BANK2_START) ? FLASH_CR2   : FLASH_CR; }
uint32_t Stm32F1FlashDriver::arReg(uint32_t addr)   { return (addr >= BANK2_START) ? FLASH_AR2   : FLASH_AR; }

ProgrammerStatus Stm32F1FlashDriver::unlock(Transport& transport, uint32_t flash_addr) {
    auto status = flash_write_word(transport, keyrReg(flash_addr), KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, keyrReg(flash_addr), KEY2);
}

ProgrammerStatus Stm32F1FlashDriver::lock(Transport& transport, uint32_t flash_addr) {
    return flash_write_word(transport, crReg(flash_addr), CR_LOCK);
}

ProgrammerStatus Stm32F1FlashDriver::unlockOptionBytes(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY2);
}

ProgrammerStatus Stm32F1FlashDriver::waitReady(Transport& transport, uint32_t flash_addr) {
    return flash_wait_busy(transport, srReg(flash_addr), SR_BSY,
                           SR_PGERR | SR_WRPRTERR, 100000, &m_error_);
}

ProgrammerStatus Stm32F1FlashDriver::clearErrors(Transport& transport, uint32_t flash_addr) {
    return flash_write_word(transport, srReg(flash_addr), SR_PGERR | SR_WRPRTERR | SR_EOP);
}

ProgrammerStatus Stm32F1FlashDriver::erasePage(Transport& transport, uint32_t address) {
    auto cr = crReg(address);
    auto status = flash_write_word(transport, cr, CR_PER);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, arReg(address), address);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, cr, CR_PER | CR_STRT);
    if (status != ProgrammerStatus::Ok) return status;
    return waitReady(transport, address);
}

RdpLevel Stm32F1FlashDriver::readRdpLevel(Transport& transport) {
    uint32_t obr;
    if (flash_read_word(transport, FLASH_OBR, obr) != ProgrammerStatus::Ok)
        return RdpLevel::Unknown;
    // Bit 1 (RDPRT): 0 = Level 0, 1 = Level 1 (Level 2 disables debug entirely)
    if (obr & (1UL << 1))
        return RdpLevel::Level1;
    return RdpLevel::Level0;
}

ProgrammerStatus Stm32F1FlashDriver::eraseFlash(Transport& transport) {
    // Bank 1
    auto status = unlock(transport, FLASH_START);
    if (status != ProgrammerStatus::Ok) return status;
    status = clearErrors(transport, FLASH_START);
    if (status != ProgrammerStatus::Ok) { lock(transport, FLASH_START); return status; }

    status = flash_write_word(transport, FLASH_CR, CR_MER);
    if (status != ProgrammerStatus::Ok) { lock(transport, FLASH_START); return status; }
    status = flash_write_word(transport, FLASH_CR, CR_MER | CR_STRT);
    if (status != ProgrammerStatus::Ok) { lock(transport, FLASH_START); return status; }
    status = waitReady(transport, FLASH_START);
    lock(transport, FLASH_START);
    if (status != ProgrammerStatus::Ok) return status;

    // Bank 2 (XL-density only — unlock will silently succeed on non-XL parts
    // since the registers are reserved/read-as-zero, and mass erase has no effect)
    auto status2 = unlock(transport, BANK2_START);
    if (status2 != ProgrammerStatus::Ok) return ProgrammerStatus::Ok; // no bank 2
    status2 = clearErrors(transport, BANK2_START);
    if (status2 != ProgrammerStatus::Ok) { lock(transport, BANK2_START); return ProgrammerStatus::Ok; }

    status2 = flash_write_word(transport, FLASH_CR2, CR_MER);
    if (status2 != ProgrammerStatus::Ok) { lock(transport, BANK2_START); return ProgrammerStatus::Ok; }
    status2 = flash_write_word(transport, FLASH_CR2, CR_MER | CR_STRT);
    if (status2 != ProgrammerStatus::Ok) { lock(transport, BANK2_START); return ProgrammerStatus::Ok; }
    waitReady(transport, BANK2_START);
    lock(transport, BANK2_START);

    return ProgrammerStatus::Ok;
}

ProgrammerStatus Stm32F1FlashDriver::writeFlash(Transport& transport,
                                                  const uint8_t* data,
                                                  uint32_t address,
                                                  uint32_t size) {
    if (use_stub_ && (address + size) <= BANK2_START)
        return writeFlashFast(transport, data, address, size);
    return writeFlashSlow(transport, data, address, size);
}

ProgrammerStatus Stm32F1FlashDriver::writeFlashSlow(Transport& transport,
                                                      const uint8_t* data,
                                                      uint32_t address,
                                                      uint32_t size) {
    uint32_t end_addr = address + size;
    bool uses_bank1 = (address < BANK2_START);
    bool uses_bank2 = (end_addr > BANK2_START);

    // Unlock needed banks
    if (uses_bank1) {
        auto status = unlock(transport, FLASH_START);
        if (status != ProgrammerStatus::Ok) return status;
        status = clearErrors(transport, FLASH_START);
        if (status != ProgrammerStatus::Ok) { lock(transport, FLASH_START); return status; }
    }
    if (uses_bank2) {
        auto status = unlock(transport, BANK2_START);
        if (status != ProgrammerStatus::Ok) {
            if (uses_bank1) lock(transport, FLASH_START);
            return status;
        }
        status = clearErrors(transport, BANK2_START);
        if (status != ProgrammerStatus::Ok) {
            if (uses_bank1) lock(transport, FLASH_START);
            lock(transport, BANK2_START);
            return status;
        }
    }

    auto status = ProgrammerStatus::Ok;
    for (uint32_t i = 0; i < size; i += PROG_WIDTH) {
        uint32_t cur_addr = address + i;

        status = flash_write_word(transport, crReg(cur_addr), CR_PG);
        if (status != ProgrammerStatus::Ok) break;

        uint16_t hw = static_cast<uint16_t>(data[i]);
        if (i + 1 < size)
            hw |= static_cast<uint16_t>(data[i + 1]) << 8;

        status = flash_write_halfword(transport, cur_addr, hw);
        if (status != ProgrammerStatus::Ok) break;

        status = waitReady(transport, cur_addr);
        if (status != ProgrammerStatus::Ok) break;

        // Report progress at page boundaries
        uint32_t next = i + PROG_WIDTH;
        if (next % PAGE_SIZE_LOW == 0 || next >= size) {
            if (!reportProgress(next < size ? next : size, size)) {
                status = ProgrammerStatus::ErrorAborted;
                break;
            }
        }
    }

    if (uses_bank1) lock(transport, FLASH_START);
    if (uses_bank2) lock(transport, BANK2_START);
    return status;
}

ProgrammerStatus Stm32F1FlashDriver::writeFlashFast(Transport& transport,
                                                      const uint8_t* data,
                                                      uint32_t address,
                                                      uint32_t size) {
    using namespace f1_flash_loader;

    // Halt core — required for setting registers and running the stub
    auto status = transport.haltCore();
    if (status != ProgrammerStatus::Ok) return status;

    // Upload stub to SRAM
    status = transport.writeMemory(stub, STUB_ADDR, STUB_SIZE);
    if (status != ProgrammerStatus::Ok) return status;

    // Unlock flash (bank 1 only) + clear error flags
    status = unlock(transport, FLASH_START);
    if (status != ProgrammerStatus::Ok) return status;
    status = clearErrors(transport, FLASH_START);
    if (status != ProgrammerStatus::Ok) { lock(transport, FLASH_START); return status; }

    uint32_t offset = 0;
    while (offset < size) {
        // Determine chunk size (real data bytes in this iteration)
        uint32_t chunk_data = size - offset;
        if (chunk_data > BUFFER_SIZE) chunk_data = BUFFER_SIZE;

        // Round up to half-word alignment for the stub
        uint32_t chunk_padded = (chunk_data + PROG_WIDTH - 1) / PROG_WIDTH * PROG_WIDTH;

        // Upload data to SRAM — full half-words directly from input
        uint32_t full = chunk_data & ~(PROG_WIDTH - 1);
        if (full > 0) {
            status = transport.writeMemory(data + offset, BUFFER_ADDR, full);
            if (status != ProgrammerStatus::Ok) break;
        }

        // Pad last partial half-word with 0xFF
        if (chunk_data > full) {
            uint8_t hw_buf[PROG_WIDTH];
            std::memset(hw_buf, 0xFF, PROG_WIDTH);
            std::memcpy(hw_buf, data + offset + full, chunk_data - full);
            status = transport.writeMemory(hw_buf, BUFFER_ADDR + full, PROG_WIDTH);
            if (status != ProgrammerStatus::Ok) break;
        }

        // Write config struct
        uint32_t config[5];
        config[0] = address + offset;   // flash_dest
        config[1] = BUFFER_ADDR;        // data_src
        config[2] = chunk_padded;        // data_size (multiple of 2)
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
    lock(transport, FLASH_START);
    return status;
}

ProgrammerStatus Stm32F1FlashDriver::writeOptionBytes(Transport& transport,
                                                        const uint8_t* data,
                                                        uint32_t address,
                                                        uint32_t size, bool unsafe) {
    // Read-merge-write: read full OB image so partial writes don't leave
    // erased bytes (0xFF) in unwritten positions (RDP 0xFF = Level 1)
    uint8_t full_ob[OB_SIZE];
    auto status = transport.readMemory(full_ob, OB_BASE, OB_SIZE);
    if (status != ProgrammerStatus::Ok) return status;

    // Merge caller's data into the full image
    if (address < OB_BASE || (address - OB_BASE) + size > OB_SIZE)
        return ProgrammerStatus::ErrorWrite;
    uint32_t offset = address - OB_BASE;
    std::memcpy(full_ob + offset, data, size);

    if (!unsafe) {
        // SAFETY: reject RDP Level 2 — permanently disables debug, irreversible
        if (full_ob[0] == RDP_LEVEL_2) {
            m_error_ = "RDP Level 2 would permanently lock the chip";
            return ProgrammerStatus::ErrorProtected;
        }
    }

    status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;
    status = unlockOptionBytes(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    status = clearErrors(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    // Erase option bytes
    status = flash_write_word(transport, FLASH_CR, CR_OPTWRE | CR_OPTER);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    status = flash_write_word(transport, FLASH_CR, CR_OPTWRE | CR_OPTER | CR_STRT);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    status = waitReady(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    // Program full option byte image
    for (uint32_t i = 0; i < OB_SIZE; i += 2) {
        status = flash_write_word(transport, FLASH_CR, CR_OPTWRE | CR_OPTPG);
        if (status != ProgrammerStatus::Ok) break;
        uint16_t hw = static_cast<uint16_t>(full_ob[i])
                    | (static_cast<uint16_t>(full_ob[i + 1]) << 8);
        status = flash_write_halfword(transport, OB_BASE + i, hw);
        if (status != ProgrammerStatus::Ok) break;
        status = waitReady(transport);
        if (status != ProgrammerStatus::Ok) break;
    }

    // F1 has no OBL_LAUNCH — option bytes reload on system reset (onDisconnect)
    lock(transport);
    return status;
}

ProgrammerStatus Stm32F1FlashDriver::writeOtp(Transport& transport,
                                                const uint8_t* data,
                                                uint32_t address,
                                                uint32_t size) {
    m_error_ = "OTP not available on this chip family";
    return ProgrammerStatus::ErrorWrite;  // F1 has no dedicated OTP
}
