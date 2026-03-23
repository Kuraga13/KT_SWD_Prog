/**
 ******************************************************************************
 * @file           : stm32f3_flash.cpp
 * @brief          : Flash driver implementation for STM32F3 series
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "stm32f3_flash.h"
#include "flash_utils.h"
#include <cstring>

using namespace stm32f3_flash;

ProgrammerStatus Stm32F3FlashDriver::onDisconnect(Transport& transport) {
    // Trigger a clean system reset via Cortex-M AIRCR register.
    // This resets the chip independently of debug state, so the core
    // boots normally from the reset vector after the probe disconnects.
    constexpr uint32_t AIRCR              = 0xE000ED0C;
    constexpr uint32_t AIRCR_VECTKEY      = 0x05FA0000;
    constexpr uint32_t AIRCR_SYSRESETREQ  = (1UL << 2);
    flash_write_word(transport, AIRCR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);
    return ProgrammerStatus::Ok;
}

ProgrammerStatus Stm32F3FlashDriver::unlock(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_KEYR, KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_KEYR, KEY2);
}

ProgrammerStatus Stm32F3FlashDriver::lock(Transport& transport) {
    uint32_t cr;
    auto status = flash_read_word(transport, FLASH_CR, cr);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_CR, cr | CR_LOCK);
}

ProgrammerStatus Stm32F3FlashDriver::unlockOptionBytes(Transport& transport) {
    auto status = flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY1);
    if (status != ProgrammerStatus::Ok) return status;
    return flash_write_word(transport, FLASH_OPTKEYR, OPT_KEY2);
}

ProgrammerStatus Stm32F3FlashDriver::waitReady(Transport& transport) {
    return flash_wait_busy(transport, FLASH_SR, SR_BSY,
                           SR_PGERR | SR_WRPERR, 100000, &m_error_);
}

ProgrammerStatus Stm32F3FlashDriver::erasePage(Transport& transport, uint32_t address) {
    auto status = flash_write_word(transport, FLASH_CR, CR_PER);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, FLASH_AR, address);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, FLASH_CR, CR_PER | CR_STRT);
    if (status != ProgrammerStatus::Ok) return status;
    return waitReady(transport);
}

RdpLevel Stm32F3FlashDriver::readRdpLevel(Transport& transport) {
    uint32_t obr;
    if (flash_read_word(transport, FLASH_OBR, obr) != ProgrammerStatus::Ok)
        return RdpLevel::Unknown;
    uint8_t rdp = static_cast<uint8_t>(obr >> 1) & 0x03;
    if (rdp == 0) return RdpLevel::Level0;
    if (rdp == 3) return RdpLevel::Level2;
    return RdpLevel::Level1;
}

ProgrammerStatus Stm32F3FlashDriver::eraseFlash(Transport& transport) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;
    flash_write_word(transport, FLASH_SR, SR_PGERR | SR_WRPERR | SR_EOP);
    status = flash_write_word(transport, FLASH_CR, CR_MER);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    status = flash_write_word(transport, FLASH_CR, CR_MER | CR_STRT);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    status = waitReady(transport);
    lock(transport);
    return status;
}

ProgrammerStatus Stm32F3FlashDriver::writeFlash(Transport& transport,
                                                  const uint8_t* data,
                                                  uint32_t address,
                                                  uint32_t size) {
    if (use_stub_)
        return writeFlashFast(transport, data, address, size);
    return writeFlashSlow(transport, data, address, size);
}

ProgrammerStatus Stm32F3FlashDriver::writeFlashSlow(Transport& transport,
                                                      const uint8_t* data,
                                                      uint32_t address,
                                                      uint32_t size) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;
    flash_write_word(transport, FLASH_SR, SR_PGERR | SR_WRPERR | SR_EOP);

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

ProgrammerStatus Stm32F3FlashDriver::writeFlashFast(Transport& transport,
                                                      const uint8_t* data,
                                                      uint32_t address,
                                                      uint32_t size) {
    using namespace f3_flash_loader;

    // Halt core — required for setting registers and running the stub
    auto status = transport.haltCore();
    if (status != ProgrammerStatus::Ok) return status;

    // Upload stub to SRAM
    status = transport.writeMemory(stub, STUB_ADDR, STUB_SIZE);
    if (status != ProgrammerStatus::Ok) return status;

    // Unlock flash + clear error flags
    status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;
    status = flash_write_word(transport, FLASH_SR, SR_PGERR | SR_WRPERR | SR_EOP);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

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
    lock(transport);
    return status;
}

ProgrammerStatus Stm32F3FlashDriver::writeOptionBytes(Transport& transport,
                                                        const uint8_t* data,
                                                        uint32_t address,
                                                        uint32_t size, bool unsafe) {
    auto status = unlock(transport);
    if (status != ProgrammerStatus::Ok) return status;
    status = unlockOptionBytes(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    flash_write_word(transport, FLASH_SR, SR_PGERR | SR_WRPERR | SR_EOP);

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

    status = flash_write_word(transport, FLASH_CR, CR_OPTWRE | CR_OPTER);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    status = flash_write_word(transport, FLASH_CR, CR_OPTWRE | CR_OPTER | CR_STRT);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }
    status = waitReady(transport);
    if (status != ProgrammerStatus::Ok) { lock(transport); return status; }

    for (uint32_t i = 0; i < size; i += 2) {
        status = flash_write_word(transport, FLASH_CR, CR_OPTWRE | CR_OPTPG);
        if (status != ProgrammerStatus::Ok) break;
        uint16_t hw = static_cast<uint16_t>(data[i]);
        if (i + 1 < size)
            hw |= static_cast<uint16_t>(data[i + 1]) << 8;
        status = flash_write_halfword(transport, address + i, hw);
        if (status != ProgrammerStatus::Ok) break;
        status = waitReady(transport);
        if (status != ProgrammerStatus::Ok) break;
    }

    flash_write_word(transport, FLASH_CR, CR_OBL_LAUNCH);
    lock(transport);
    return status;
}

ProgrammerStatus Stm32F3FlashDriver::writeOtp(Transport& transport,
                                                const uint8_t* data,
                                                uint32_t address,
                                                uint32_t size) {
    m_error_ = "OTP not available on this chip family";
    return ProgrammerStatus::ErrorWrite;  // F3 has no dedicated OTP
}
