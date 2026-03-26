/**
 ******************************************************************************
 * @file           : stlink_transport.h
 * @brief          : ST-Link debug probe transport layer over USB
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
 * Transport implementation for ST-Link V2/V2-1/V3 debug probes.
 * Communicates with the probe over USB bulk transfers using libusb,
 * implementing the ST-Link proprietary protocol (16-byte command blocks).
 *
 * Features:
 * - Auto-detection of ST-Link V2, V2-1, V3, V3E, and V3 MINIE probes
 * - SWD debug interface entry
 * - 32-bit aligned and 8-bit unaligned memory read/write
 * - Automatic chunking for transfers exceeding 6 KB limit
 * - Bulk endpoint auto-detection from USB descriptors
 *
 * Usage Example:
 * ```cpp
 * StLinkTransport transport;
 * transport.connect();
 * uint8_t buffer[1024];
 * transport.readMemory(buffer, 0x08000000, sizeof(buffer));
 * transport.disconnect();
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "../stm32_programmer.h"
#include "../probe_info.h"
#include <libusb-1.0/libusb.h>

/// ST-Link USB vendor/product IDs
namespace stlink {
    constexpr uint16_t USB_VID          = 0x0483;  // STMicroelectronics

    constexpr uint16_t USB_PID_V2       = 0x3748;  // ST-Link V2
    constexpr uint16_t USB_PID_V2_1     = 0x374B;  // ST-Link V2-1
    constexpr uint16_t USB_PID_V3       = 0x374E;  // ST-Link V3
    constexpr uint16_t USB_PID_V3E      = 0x3752;  // ST-Link V3E (Nucleo/Discovery)
    constexpr uint16_t USB_PID_V3_MINIE = 0x3754;  // ST-Link V3 MINIE

    /// ST-Link command bytes
    constexpr uint8_t CMD_DEBUG         = 0xF2;
    constexpr uint8_t CMD_GET_VERSION   = 0xF1;
    constexpr uint8_t CMD_GET_TARGET_VOLTAGE = 0xF7;

    /// Debug sub-commands
    constexpr uint8_t DEBUG_ENTER_SWD   = 0x30;
    constexpr uint8_t DEBUG_EXIT        = 0x21;
    constexpr uint8_t DEBUG_READMEM_32  = 0x07;
    constexpr uint8_t DEBUG_WRITEMEM_32 = 0x08;
    constexpr uint8_t DEBUG_READMEM_8   = 0x0C;
    constexpr uint8_t DEBUG_WRITEMEM_8  = 0x0D;
    constexpr uint8_t DEBUG_WRITEMEM_16 = 0x48;
    constexpr uint8_t DEBUG_GET_STATUS  = 0x01;
    constexpr uint8_t DEBUG_FORCE_HALT  = 0x02;
    constexpr uint8_t DEBUG_RUN_CORE    = 0x09;
    constexpr uint8_t DEBUG_RESET_SYS   = 0x03;

    /// Command buffer size (ST-Link expects 16-byte command blocks)
    constexpr size_t CMD_SIZE           = 16;

    /// Max transfer size per single read/write
    constexpr uint32_t MAX_TRANSFER     = 6144;
}

/// ST-Link transport — communicates with an ST-Link probe over USB.
class StLinkTransport : public Transport {
public:
    StLinkTransport();
    ~StLinkTransport() override;

    StLinkTransport(const StLinkTransport&)            = delete;
    StLinkTransport& operator=(const StLinkTransport&) = delete;

    /// Scan USB for all connected ST-Link probes (does not open them).
    static std::vector<ProbeInfo> listProbes();

    // ── Transport interface ─────────────────────────────────

    ProgrammerStatus connect()           override;
    void             disconnect()        override;
    bool             isConnected() const override;

    ProgrammerStatus readMemory(uint8_t*       buffer,
                                uint32_t       address,
                                uint32_t       size) override;

    ProgrammerStatus writeMemory(const uint8_t* data,
                                 uint32_t       address,
                                 uint32_t       size) override;

    ProgrammerStatus openProbe()          override;
    void             closeProbe()         override;
    const char*      probeName()          override;
    int              probeFirmwareVersion() override;
    float            targetVoltage()      override;

    ProgrammerStatus haltCore()    override;
    ProgrammerStatus resumeCore()  override;
    ProgrammerStatus resetTarget() override;

private:
    // ── USB helpers ─────────────────────────────────────────

    /// Open the first ST-Link device found on USB
    ProgrammerStatus usbOpen();

    /// Close the USB device
    void usbClose();

    /// Send a command and receive a response
    ProgrammerStatus usbTransfer(const uint8_t* cmd,   size_t cmd_size,
                                 uint8_t*       reply, size_t reply_size);

    /// Bulk read from the data endpoint
    ProgrammerStatus usbRead(uint8_t* buffer, size_t size);

    /// Bulk write to the data endpoint
    ProgrammerStatus usbWrite(const uint8_t* data, size_t size);

    // ── ST-Link protocol helpers ────────────────────────────

    /// Enter SWD debug mode
    ProgrammerStatus enterSwd();

    /// Read memory in chunks (handles MAX_TRANSFER splitting)
    ProgrammerStatus readMemory32(uint8_t* buffer, uint32_t address, uint32_t size);
    ProgrammerStatus readMemory8(uint8_t*  buffer, uint32_t address, uint32_t size);

    /// Write memory in chunks (handles MAX_TRANSFER splitting)
    ProgrammerStatus writeMemory32(const uint8_t* data, uint32_t address, uint32_t size);
    ProgrammerStatus writeMemory16(const uint8_t* data, uint32_t address, uint32_t size);
    ProgrammerStatus writeMemory8(const uint8_t*  data, uint32_t address, uint32_t size);

    // ── State ───────────────────────────────────────────────

    libusb_context*       usb_ctx_    = nullptr;
    libusb_device_handle* usb_handle_ = nullptr;
    uint8_t               ep_out_     = 0;    // bulk OUT endpoint
    uint8_t               ep_in_      = 0;    // bulk IN endpoint
    bool                  connected_  = false;

    // Probe info (populated by readProbeInfo)
    uint8_t               stlink_version_ = 0;  // major: V1/V2/V3
    uint8_t               jtag_version_   = 0;  // JTAG/SWD firmware version
    uint16_t              pid_            = 0;   // USB PID (identifies variant)

    /// Read version and voltage from the probe (call after usbOpen)
    void readProbeInfo();
};
