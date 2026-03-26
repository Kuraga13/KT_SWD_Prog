/**
 ******************************************************************************
 * @file           : stm32_programmer.h
 * @brief          : Base interfaces for KuragaSTM32Tool programmer
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
 * Defines the core interfaces for the KuragaSTM32Tool universal programmer:
 * - Transport: raw memory read/write over SWD (one impl per probe type)
 * - TargetDriver: target-specific logic (one impl per chip family)
 * - Stm32Programmer: composes Transport + TargetDriver
 *
 * Features:
 * - Composition architecture: transports + families instead of transports x families
 * - All operations routed through TargetDriver with safe defaults
 * - TargetDriver provides hooks for connect/disconnect and memory reads
 * - ProgrammerStatus enum for uniform error handling
 * - RdpLevel enum for read-out protection level detection
 * - Safe-by-default option byte writes (rejects RDP Level 2, sanitizes WRP)
 * - ProgressCallback for GUI progress bars with cancellation support
 * - Extensible to non-STM32 targets (NXP Kinetis, etc.)
 *
 * Usage Example:
 * ```cpp
 * StLinkTransport transport;
 * Stm32G0TargetDriver target_driver;
 * Stm32Programmer programmer(transport, target_driver);
 *
 * programmer.setProgressCallback([](uint32_t done, uint32_t total, void*) {
 *     printf("\r%u / %u bytes", done, total);
 *     return true;  // return false to abort
 * });
 * programmer.connect();
 * programmer.readFlash(buffer, 0x08000000, 0x20000);
 * programmer.writeFlash(data, 0x08000000, 0x20000);
 * programmer.disconnect();
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

/// Programmer operation result
enum class ProgrammerStatus : uint8_t {
    Ok,
    ErrorConnect,
    ErrorProtected,
    ErrorErase,
    ErrorRead,
    ErrorWrite,
    ErrorVerify,
    ErrorAborted,
};

/// Progress callback for long-running operations (write, erase).
/// Return true to continue, false to abort (returns ErrorAborted).
/// @param bytes_done  Bytes completed so far
/// @param bytes_total Total bytes for the operation
/// @param user_data   Opaque pointer passed through from setProgressCallback()
using ProgressCallback = bool(*)(uint32_t bytes_done, uint32_t bytes_total, void* user_data);

/// Single byte mismatch found during verification
struct FlashMismatch {
    uint32_t address;
    uint8_t  expected;
    uint8_t  actual;
};

/// Entry for scattered option byte writes (mapped CUR/PRG register pairs)
struct ObWriteEntry {
    uint32_t addr;      ///< PRG register address to write to
    uint32_t value;     ///< 32-bit word value to write
};

/// Read-out protection level
enum class RdpLevel : uint8_t {
    Level0   = 0,     // no protection — full debug access
    Level1   = 1,     // flash read blocked via debug; mass erase to revert
    Level2   = 2,     // debug permanently disabled — irreversible
    Unknown  = 0xFF,
};

/// Transport interface — raw memory access over SWD/JTAG.
/// Knows nothing about target flash controllers.
/// Implementations: ST-Link, Black Magic Probe, J-Link, etc.
class Transport {
public:
    virtual ~Transport() = default;

    /// Last error detail string set at the failure site.
    const std::string& getLastError() const { return m_error_; }
    void clearError() { m_error_.clear(); }

    virtual ProgrammerStatus connect()           = 0;
    virtual void             disconnect()        = 0;
    virtual bool             isConnected() const = 0;

    virtual ProgrammerStatus readMemory(uint8_t*       buffer,
                                        uint32_t       address,
                                        uint32_t       size) = 0;

    virtual ProgrammerStatus writeMemory(const uint8_t* data,
                                         uint32_t       address,
                                         uint32_t       size) = 0;

    /// Open the probe (USB/serial) without connecting to the target.
    /// Allows reading probe info before attempting SWD entry.
    virtual ProgrammerStatus openProbe() { return ProgrammerStatus::ErrorConnect; }

    /// Close the probe opened by openProbe().
    virtual void closeProbe() {}

    /// Probe name (e.g. "ST-Link V2"). Valid after openProbe() or connect().
    virtual const char* probeName() { return "Unknown"; }

    /// Probe JTAG/SWD firmware version, or -1 if not available.
    virtual int probeFirmwareVersion() { return -1; }

    /// Target voltage in volts, or -1.0 if not available.
    virtual float targetVoltage() { return -1.0f; }

    /// Halt the target core. Default: no-op (Ok).
    virtual ProgrammerStatus haltCore()    { return ProgrammerStatus::Ok; }

    /// Resume the target core. Default: no-op (Ok).
    virtual ProgrammerStatus resumeCore()  { return ProgrammerStatus::Ok; }

    /// Reset the target (system reset). Default: no-op (Ok).
    virtual ProgrammerStatus resetTarget() { return ProgrammerStatus::Ok; }

protected:
    std::string m_error_;  ///< Detail string set at the failure site
};

/// Target driver interface — target-specific logic for one chip family.
/// All operations are routed through this interface with safe defaults.
/// Override only what your target needs.
/// Implementations: STM32 G0/F4/H7/..., NXP Kinetis KE, etc.
class TargetDriver {
public:
    virtual ~TargetDriver() = default;

    /// Last error detail string set at the failure site.
    const std::string& getLastError() const { return m_error_; }
    void clearError() { m_error_.clear(); }

    // ── Connection (defaults passthrough to Transport) ─────

    /// Connect the debug probe to the target.
    virtual ProgrammerStatus connect(Transport& transport) {
        return transport.connect();
    }

    /// Disconnect the debug probe from the target.
    virtual void disconnect(Transport& transport) {
        transport.disconnect();
    }

    /// Clear stale flash error flags. Call before flash operations
    /// to avoid spurious errors from power-on or previous sessions.
    virtual void clearFlashErrors(Transport& /*transport*/) {}

    // ── Core control (defaults passthrough to Transport) ─────

    /// Halt the target core. Default passes through to Transport.
    virtual ProgrammerStatus haltTarget(Transport& transport) {
        return transport.haltCore();
    }

    /// Reset the target. Override if the target needs a different reset
    /// method (e.g. AIRCR system reset instead of transport-level reset).
    virtual ProgrammerStatus resetTarget(Transport& transport) {
        return transport.resetTarget();
    }

    /// Resume the target core (run from current PC / reset vector).
    virtual ProgrammerStatus runTarget(Transport& transport) {
        return transport.resumeCore();
    }

    // ── Read (defaults passthrough to Transport) ────────────

    /// Read memory via Transport. Override if target needs core halted,
    /// bus clock configuration, or read-while-write restrictions.
    virtual ProgrammerStatus readMemory(Transport& transport,
                                        uint8_t*   buffer,
                                        uint32_t   address,
                                        uint32_t   size) {
        return transport.readMemory(buffer, address, size);
    }

    // ── Protection ──────────────────────────────────────────

    virtual RdpLevel readRdpLevel(Transport& transport) = 0;

    // ── Write (pure virtual — must be implemented) ──────────

    virtual ProgrammerStatus eraseFlash(Transport& transport) = 0;

    virtual ProgrammerStatus writeFlash(Transport&     transport,
                                        const uint8_t* data,
                                        uint32_t       address,
                                        uint32_t       size) = 0;

    virtual ProgrammerStatus writeOptionBytes(Transport&     transport,
                                              const uint8_t* data,
                                              uint32_t       address,
                                              uint32_t       size,
                                              bool           unsafe = false) = 0;

    /// Write option bytes from a scatter list (mapped CUR/PRG register pairs).
    /// Each entry is written to its own address, then a single commit is issued.
    /// Default: falls back to one writeOptionBytes() call per entry.
    virtual ProgrammerStatus writeOptionBytesMapped(Transport&          transport,
                                                     const ObWriteEntry* entries,
                                                     size_t              count,
                                                     bool                unsafe = false) {
        for (size_t i = 0; i < count; i++) {
            uint8_t data[4];
            std::memcpy(data, &entries[i].value, 4);
            auto status = writeOptionBytes(transport, data, entries[i].addr, 4, unsafe);
            if (status != ProgrammerStatus::Ok) return status;
        }
        return ProgrammerStatus::Ok;
    }

    virtual ProgrammerStatus writeOtp(Transport&     transport,
                                      const uint8_t* data,
                                      uint32_t       address,
                                      uint32_t       size) = 0;

    /// Enable/disable flash loader stub acceleration (default: disabled).
    /// Callers must opt in after confirming the transport supports core control
    /// (haltCore/resumeCore). Without real core control the stub never executes.
    void setUseStub(bool use) { use_stub_ = use; }

    /// Set progress callback for writeFlash/eraseFlash operations.
    void setProgressCallback(ProgressCallback cb, void* user_data = nullptr) {
        progress_cb_ = cb;
        progress_user_data_ = user_data;
    }

protected:
    std::string m_error_;  ///< Detail string set at the failure site
    bool use_stub_ = false;
    ProgressCallback progress_cb_ = nullptr;
    void* progress_user_data_ = nullptr;

    /// Report progress. Returns false if the user requested abort.
    bool reportProgress(uint32_t bytes_done, uint32_t bytes_total) {
        if (!progress_cb_) return true;
        return progress_cb_(bytes_done, bytes_total, progress_user_data_);
    }
};

// Keep FlashDriver as an alias for backward compatibility
using FlashDriver = TargetDriver;

/// Programmer — composes a Transport and a TargetDriver.
/// All operations are routed through TargetDriver.
class Stm32Programmer {
public:
    Stm32Programmer(Transport& transport, TargetDriver& target_driver)
        : transport_(transport), target_driver_(target_driver) {}

    /// Last error detail string forwarded from the failing layer.
    const std::string& getLastError() const { return last_error_; }

    // ── Connection ──────────────────────────────────────────

    ProgrammerStatus connect() {
        clearErrors();
        auto status = target_driver_.connect(transport_);
        if (status != ProgrammerStatus::Ok) {
            forwardError();
            return status;
        }
        status = target_driver_.haltTarget(transport_);
        if (status != ProgrammerStatus::Ok)
            forwardError();
        return status;
    }

    void disconnect() {
        target_driver_.resetTarget(transport_);
        target_driver_.runTarget(transport_);
        target_driver_.disconnect(transport_);
    }

    bool isConnected() const { return transport_.isConnected(); }

    // ── Core control ────────────────────────────────────────

    /// Halt the target core (routed through TargetDriver).
    ProgrammerStatus haltTarget() {
        auto status = target_driver_.haltTarget(transport_);
        if (status != ProgrammerStatus::Ok) forwardError();
        return status;
    }

    /// Resume the target core (routed through TargetDriver).
    ProgrammerStatus runTarget() {
        auto status = target_driver_.runTarget(transport_);
        if (status != ProgrammerStatus::Ok) forwardError();
        return status;
    }

    /// Reset the target (routed through TargetDriver).
    ProgrammerStatus resetTarget() {
        auto status = target_driver_.resetTarget(transport_);
        if (status != ProgrammerStatus::Ok) forwardError();
        return status;
    }

    /// Clear stale flash error flags (family-specific).
    void clearFlashErrors() { target_driver_.clearFlashErrors(transport_); }

    // ── Protection ──────────────────────────────────────────

    RdpLevel readRdpLevel() { return target_driver_.readRdpLevel(transport_); }

    // ── Read (routed through TargetDriver) ──────────────────

    ProgrammerStatus readFlash(uint8_t* buffer,
                               uint32_t address,
                               uint32_t size) {
        clearErrors();
        auto status = target_driver_.readMemory(transport_, buffer, address, size);
        if (status != ProgrammerStatus::Ok)
            forwardError();
        return status;
    }

    ProgrammerStatus readOptionBytes(uint8_t* buffer,
                                     uint32_t address,
                                     uint32_t size) {
        clearErrors();
        auto status = target_driver_.readMemory(transport_, buffer, address, size);
        if (status != ProgrammerStatus::Ok)
            forwardError();
        return status;
    }

    ProgrammerStatus readOtp(uint8_t* buffer,
                             uint32_t address,
                             uint32_t size) {
        clearErrors();
        auto status = target_driver_.readMemory(transport_, buffer, address, size);
        if (status != ProgrammerStatus::Ok)
            forwardError();
        return status;
    }

    // ── Write (routed through TargetDriver) ─────────────────

    ProgrammerStatus eraseFlash() {
        clearErrors();
        auto status = target_driver_.eraseFlash(transport_);
        if (status != ProgrammerStatus::Ok)
            forwardError();
        return status;
    }

    /// Enable/disable flash loader stub for writeFlash().
    /// Must be called before writeFlash() if stub acceleration is desired.
    void setUseStub(bool use) { target_driver_.setUseStub(use); }

    /// Set progress callback for long-running operations (write, erase, verify).
    void setProgressCallback(ProgressCallback cb, void* user_data = nullptr) {
        progress_cb_ = cb;
        progress_user_data_ = user_data;
        target_driver_.setProgressCallback(cb, user_data);
    }

    ProgrammerStatus writeFlash(const uint8_t* data,
                                uint32_t       address,
                                uint32_t       size) {
        clearErrors();
        auto status = target_driver_.writeFlash(transport_, data, address, size);
        if (status != ProgrammerStatus::Ok)
            forwardError();
        return status;
    }

    ProgrammerStatus writeOptionBytes(const uint8_t* data,
                                      uint32_t       address,
                                      uint32_t       size,
                                      bool           unsafe = false) {
        clearErrors();
        auto status = target_driver_.writeOptionBytes(transport_, data, address, size, unsafe);
        if (status != ProgrammerStatus::Ok)
            forwardError();
        return status;
    }

    ProgrammerStatus writeOptionBytesMapped(const ObWriteEntry* entries,
                                             size_t              count,
                                             bool                unsafe = false) {
        clearErrors();
        auto status = target_driver_.writeOptionBytesMapped(transport_, entries, count, unsafe);
        if (status != ProgrammerStatus::Ok)
            forwardError();
        return status;
    }

    ProgrammerStatus writeOtp(const uint8_t* data,
                              uint32_t       address,
                              uint32_t       size) {
        clearErrors();
        auto status = target_driver_.writeOtp(transport_, data, address, size);
        if (status != ProgrammerStatus::Ok)
            forwardError();
        return status;
    }

    // ── Verify ──────────────────────────────────────────────

    /// Verify flash contents against expected data.
    /// Reads flash in chunks, compares byte-by-byte, collects mismatches.
    /// Reports progress via the callback set with setProgressCallback().
    /// Stops collecting mismatches after max_mismatches to avoid flooding.
    /// @return Ok if all bytes match, ErrorVerify if mismatches found,
    ///         ErrorRead on read failure, ErrorAborted if cancelled.
    ProgrammerStatus verifyFlash(const uint8_t*              expected,
                                uint32_t                    address,
                                uint32_t                    size,
                                std::vector<FlashMismatch>& mismatches,
                                uint32_t                    max_mismatches = 1000) {
        clearErrors();
        mismatches.clear();
        if (max_mismatches == 0) max_mismatches = UINT32_MAX;

        constexpr uint32_t kChunkSize = 4096;
        uint8_t chunk_buf[kChunkSize];

        uint32_t offset = 0;
        while (offset < size) {
            uint32_t chunk = (size - offset < kChunkSize) ? (size - offset) : kChunkSize;

            auto status = target_driver_.readMemory(transport_, chunk_buf,
                                                    address + offset, chunk);
            if (status != ProgrammerStatus::Ok) {
                forwardError();
                return ProgrammerStatus::ErrorRead;
            }

            // Compare chunk against expected data
            if (mismatches.size() < max_mismatches) {
                for (uint32_t i = 0; i < chunk; ++i) {
                    if (chunk_buf[i] != expected[offset + i]) {
                        mismatches.push_back({address + offset + i,
                                              expected[offset + i],
                                              chunk_buf[i]});
                        if (mismatches.size() >= max_mismatches)
                            break;
                    }
                }
            }

            offset += chunk;

            // Report progress, abort if requested
            if (progress_cb_) {
                if (!progress_cb_(offset, size, progress_user_data_))
                    return ProgrammerStatus::ErrorAborted;
            }
        }

        return mismatches.empty() ? ProgrammerStatus::Ok
                                  : ProgrammerStatus::ErrorVerify;
    }

private:
    Transport&       transport_;
    TargetDriver&    target_driver_;
    std::string      last_error_;
    ProgressCallback progress_cb_        = nullptr;
    void*            progress_user_data_ = nullptr;

    void clearErrors() {
        last_error_.clear();
        transport_.clearError();
        target_driver_.clearError();
    }

    void forwardError() {
        last_error_ = target_driver_.getLastError();
        if (last_error_.empty())
            last_error_ = transport_.getLastError();
    }
};
