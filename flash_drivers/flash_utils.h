/**
 ******************************************************************************
 * @file           : flash_utils.h
 * @brief          : Common helper functions for flash driver implementations
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 * @details
 *
 * Shared utility functions used by all flash driver implementations.
 * Provides word-level read/write wrappers around the Transport interface
 * and a busy-wait polling helper.
 *
 * Features:
 * - 32-bit word read/write via Transport with endianness handling
 * - Busy-wait polling with timeout and error flag checking
 * - Half-word (16-bit) write helper for F0/F1/F3 families
 *
 * Usage Example:
 * ```cpp
 * #include "flash_utils.h"
 * uint32_t val = flash_read_word(transport, 0x40022010);
 * flash_write_word(transport, 0x40022014, 0x00000001);
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "../stm32_programmer.h"
#include <string>
#include <cstdio>

/// Read a 32-bit word from a memory-mapped register
inline ProgrammerStatus flash_read_word(Transport& transport, uint32_t address, uint32_t& value) {
    uint8_t buf[4];
    auto status = transport.readMemory(buf, address, 4);
    if (status != ProgrammerStatus::Ok)
        return status;
    value = static_cast<uint32_t>(buf[0])
          | (static_cast<uint32_t>(buf[1]) << 8)
          | (static_cast<uint32_t>(buf[2]) << 16)
          | (static_cast<uint32_t>(buf[3]) << 24);
    return ProgrammerStatus::Ok;
}

/// Write a 32-bit word to a memory-mapped register
inline ProgrammerStatus flash_write_word(Transport& transport, uint32_t address, uint32_t value) {
    uint8_t buf[4] = {
        static_cast<uint8_t>(value),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 24),
    };
    return transport.writeMemory(buf, address, 4);
}

/// Format a uint32_t as a "0x" hex string
inline std::string to_hex(uint32_t value) {
    char buf[12];
    snprintf(buf, sizeof(buf), "0x%08X", value);
    return buf;
}

/// Write a 16-bit half-word to a memory-mapped register
inline ProgrammerStatus flash_write_halfword(Transport& transport, uint32_t address, uint16_t value) {
    uint8_t buf[2] = {
        static_cast<uint8_t>(value),
        static_cast<uint8_t>(value >> 8),
    };
    return transport.writeMemory(buf, address, 2);
}

// Cortex-M debug port registers (used by flash loader stubs)
namespace cortex_debug {
    constexpr uint32_t DHCSR         = 0xE000EDF0;  // Debug Halting Control and Status
    constexpr uint32_t DCRSR         = 0xE000EDF4;  // Debug Core Register Selector
    constexpr uint32_t DCRDR         = 0xE000EDF8;  // Debug Core Register Data

    // DHCSR bits
    constexpr uint32_t DHCSR_S_REGRDY = (1UL << 16);  // register read/write complete

    // DCRSR bits
    constexpr uint32_t DCRSR_REGWnR   = (1UL << 16);  // 1 = write, 0 = read

    // Core register numbers
    constexpr uint32_t REG_R0    = 0;
    constexpr uint32_t REG_SP    = 13;
    constexpr uint32_t REG_PC    = 15;
    constexpr uint32_t REG_xPSR  = 16;
}

/// Write a Cortex-M core register via the debug port (core must be halted)
inline ProgrammerStatus cortex_write_core_reg(Transport& transport,
                                               uint32_t reg_num, uint32_t value,
                                               std::string* error_out = nullptr) {
    auto status = flash_write_word(transport, cortex_debug::DCRDR, value);
    if (status != ProgrammerStatus::Ok) return status;

    status = flash_write_word(transport, cortex_debug::DCRSR,
                               cortex_debug::DCRSR_REGWnR | reg_num);
    if (status != ProgrammerStatus::Ok) return status;

    for (uint32_t i = 0; i < 1000; i++) {
        uint32_t dhcsr;
        status = flash_read_word(transport, cortex_debug::DHCSR, dhcsr);
        if (status != ProgrammerStatus::Ok) return status;
        if (dhcsr & cortex_debug::DHCSR_S_REGRDY)
            return ProgrammerStatus::Ok;
    }
    if (error_out) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Cortex debug register write timeout (reg %u)", reg_num);
        *error_out = buf;
    }
    return ProgrammerStatus::ErrorWrite;
}

/// Poll a status register until a busy flag clears or an error is detected.
/// Returns Ok if busy clears without errors, ErrorWrite if error flags are set.
inline ProgrammerStatus flash_wait_busy(Transport& transport, uint32_t sr_addr,
                                         uint32_t busy_mask, uint32_t error_mask,
                                         uint32_t max_polls = 100000,
                                         std::string* error_out = nullptr) {
    for (uint32_t i = 0; i < max_polls; i++) {
        uint32_t sr;
        auto status = flash_read_word(transport, sr_addr, sr);
        if (status != ProgrammerStatus::Ok) {
            if (error_out)
                *error_out = "Failed to read FLASH_SR at " + to_hex(sr_addr);
            return status;
        }

        if (sr & error_mask) {
            if (error_out)
                *error_out = "FLASH_SR error bits: " + to_hex(sr & error_mask)
                           + " (SR=" + to_hex(sr) + " at " + to_hex(sr_addr) + ")";
            return ProgrammerStatus::ErrorWrite;
        }

        if (!(sr & busy_mask))
            return ProgrammerStatus::Ok;
    }
    if (error_out) {
        char buf[96];
        snprintf(buf, sizeof(buf), "FLASH_SR busy timeout after %u polls (SR at %s)",
                 max_polls, to_hex(sr_addr).c_str());
        *error_out = buf;
    }
    return ProgrammerStatus::ErrorWrite;  // timeout
}
