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
#include <string>

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

    // ── Hooks (defaults do nothing / passthrough) ───────────

    /// Called after Transport::connect(). Override to disable watchdog,
    /// check debug lock registers, or perform target-specific init.
    virtual ProgrammerStatus onConnect(Transport& transport) {
        return ProgrammerStatus::Ok;
    }

    /// Called before Transport::disconnect(). Resets and resumes the core
    /// so the MCU runs from its reset vector after the probe disconnects.
    /// Override if the target needs a different reset method (e.g. AIRCR).
    virtual ProgrammerStatus onDisconnect(Transport& transport) {
        transport.resetTarget();
        transport.resumeCore();
        return ProgrammerStatus::Ok;
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
        auto status = transport_.connect();
        if (status != ProgrammerStatus::Ok) {
            last_error_ = transport_.getLastError();
            return status;
        }
        status = target_driver_.onConnect(transport_);
        if (status != ProgrammerStatus::Ok)
            forwardError();
        return status;
    }

    void disconnect() {
        target_driver_.onDisconnect(transport_);
        transport_.disconnect();
    }

    bool isConnected() const { return transport_.isConnected(); }

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

    /// Set progress callback for long-running operations (write, erase).
    void setProgressCallback(ProgressCallback cb, void* user_data = nullptr) {
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

    ProgrammerStatus writeOtp(const uint8_t* data,
                              uint32_t       address,
                              uint32_t       size) {
        clearErrors();
        auto status = target_driver_.writeOtp(transport_, data, address, size);
        if (status != ProgrammerStatus::Ok)
            forwardError();
        return status;
    }

private:
    Transport&    transport_;
    TargetDriver& target_driver_;
    std::string   last_error_;

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
