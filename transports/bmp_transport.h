/**
 ******************************************************************************
 * @file           : bmp_transport.h
 * @brief          : Black Magic Probe transport layer over GDB serial protocol
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
 * Transport implementation for Black Magic Probe (BMP) debug probes.
 * Communicates via GDB Remote Serial Protocol over a virtual serial port
 * (USB CDC/ACM). The probe appears as a COM port on Windows or
 * /dev/ttyACM on Linux/macOS.
 *
 * Features:
 * - Cross-platform serial port communication (Windows HANDLE / POSIX fd)
 * - Full GDB Remote Serial Protocol packet framing ($data#checksum)
 * - Memory read ('m') and write ('M') via hex-encoded GDB commands
 * - Automatic SWD scan and target attach on connect
 * - Chunked transfers to stay within GDB packet size limits
 *
 * Usage Example:
 * ```cpp
 * BmpTransport transport("COM3");  // or "/dev/ttyACM0"
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
#include <string>

/// Black Magic Probe communicates via GDB Remote Serial Protocol
/// over a virtual serial port (USB CDC/ACM).
namespace bmp {
    /// Default baud rate for BMP serial connection
    constexpr uint32_t DEFAULT_BAUD = 115200;

    /// GDB packet structure: $<data>#<checksum>
    constexpr char PACKET_START     = '$';
    constexpr char PACKET_END       = '#';
    constexpr char ACK              = '+';
    constexpr char NACK             = '-';

    /// Max GDB packet payload size
    constexpr size_t MAX_PACKET     = 4096;
}

/// Black Magic Probe transport — communicates via GDB Remote
/// Serial Protocol over a virtual serial port.
class BmpTransport : public Transport {
public:
    /// Create with a serial port path (e.g. "COM3" or "/dev/ttyACM0")
    explicit BmpTransport(const std::string& port,
                          uint32_t baud = bmp::DEFAULT_BAUD);
    ~BmpTransport() override;

    BmpTransport(const BmpTransport&)            = delete;
    BmpTransport& operator=(const BmpTransport&) = delete;

    /// List serial ports that may be BMP devices.
    /// On Windows: enumerates COM ports. On Linux/macOS: looks for /dev/ttyACM*.
    static std::vector<ProbeInfo> listPorts();

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

private:
    // ── Serial helpers ──────────────────────────────────────

    /// Open the serial port
    ProgrammerStatus serialOpen();

    /// Close the serial port
    void serialClose();

    /// Send raw bytes over serial
    ProgrammerStatus serialWrite(const uint8_t* data, size_t size);

    /// Receive raw bytes from serial
    ProgrammerStatus serialRead(uint8_t* buffer, size_t size);

    // ── GDB protocol helpers ────────────────────────────────

    /// Send a GDB packet: $<data>#<checksum>
    ProgrammerStatus gdbSend(const char* data, size_t length);

    /// Receive a GDB packet, returns payload
    ProgrammerStatus gdbReceive(uint8_t* buffer, size_t& length);

    /// Send a command and wait for response
    ProgrammerStatus gdbCommand(const char*  cmd,    size_t cmd_len,
                                uint8_t*     reply,  size_t& reply_len);

    /// Calculate GDB checksum (sum of all bytes, mod 256)
    static uint8_t gdbChecksum(const char* data, size_t length);

    // ── GDB memory commands ─────────────────────────────────

    /// Read memory: 'm addr,length'
    ProgrammerStatus gdbReadMemory(uint8_t* buffer, uint32_t address, uint32_t size);

    /// Write memory: 'M addr,length:data'
    ProgrammerStatus gdbWriteMemory(const uint8_t* data, uint32_t address, uint32_t size);

    /// Attach to target: 'vAttach;1'
    ProgrammerStatus gdbAttach();

    /// Detach from target: 'D'
    ProgrammerStatus gdbDetach();

    // ── State ───────────────────────────────────────────────

    std::string port_;
    uint32_t    baud_;
#ifdef _WIN32
    void*       serial_handle_ = nullptr;  // HANDLE on Windows
#else
    int         serial_fd_     = -1;
#endif
    bool        connected_     = false;
};
