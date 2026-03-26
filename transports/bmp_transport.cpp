/**
 ******************************************************************************
 * @file           : bmp_transport.cpp
 * @brief          : Black Magic Probe transport layer implementation
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "bmp_transport.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
    #include <termios.h>
    #include <errno.h>
#endif

/// Serial read/write timeout in milliseconds
static constexpr uint32_t SERIAL_TIMEOUT = 2000;

// ── Port scanning ───────────────────────────────────────────

std::vector<ProbeInfo> BmpTransport::listPorts() {
    std::vector<ProbeInfo> result;

#ifdef _WIN32
    // Enumerate COM1..COM256
    char port_name[16];
    char target_path[256];
    for (int i = 1; i <= 256; ++i) {
        snprintf(port_name, sizeof(port_name), "COM%d", i);
        DWORD ret = QueryDosDeviceA(port_name, target_path, sizeof(target_path));
        if (ret != 0) {
            ProbeInfo info;
            info.name = std::string("BMP (") + port_name + ")";
            info.serial = port_name;
            result.push_back(std::move(info));
        }
    }
#else
    // Look for /dev/ttyACM* (typical BMP devices on Linux)
    for (int i = 0; i < 16; ++i) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/ttyACM%d", i);
        if (access(path, F_OK) == 0) {
            ProbeInfo info;
            info.name = std::string("BMP (") + path + ")";
            info.serial = path;
            result.push_back(std::move(info));
        }
    }
#endif

    return result;
}

// ── Hex encoding helpers ────────────────────────────────────

static char nibble_to_hex(uint8_t nibble) {
    return (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
}

static int hex_to_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void encode_hex(char* dst, const uint8_t* src, size_t len) {
    for (size_t i = 0; i < len; i++) {
        dst[i * 2]     = nibble_to_hex(src[i] >> 4);
        dst[i * 2 + 1] = nibble_to_hex(src[i] & 0x0F);
    }
}

static bool decode_hex(uint8_t* dst, const char* src, size_t hex_len) {
    for (size_t i = 0; i + 1 < hex_len; i += 2) {
        int hi = hex_to_nibble(src[i]);
        int lo = hex_to_nibble(src[i + 1]);
        if (hi < 0 || lo < 0) return false;
        dst[i / 2] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

// ── Construction / Destruction ──────────────────────────────

BmpTransport::BmpTransport(const std::string& port, uint32_t baud)
    : port_(port), baud_(baud) {}

BmpTransport::~BmpTransport() {
    disconnect();
}

// ── Transport interface ─────────────────────────────────────

ProgrammerStatus BmpTransport::connect() {
    auto status = serialOpen();
    if (status != ProgrammerStatus::Ok)
        return status;

    status = gdbAttach();
    if (status != ProgrammerStatus::Ok) {
        serialClose();
        return status;
    }

    connected_ = true;
    return ProgrammerStatus::Ok;
}

void BmpTransport::disconnect() {
    if (!connected_)
        return;

    gdbDetach();
    connected_ = false;
    serialClose();
}

bool BmpTransport::isConnected() const {
    return connected_;
}

ProgrammerStatus BmpTransport::readMemory(uint8_t* buffer,
                                           uint32_t address,
                                           uint32_t size) {
    if (!connected_) {
        m_error_ = "BMP not connected";
        return ProgrammerStatus::ErrorConnect;
    }
    return gdbReadMemory(buffer, address, size);
}

ProgrammerStatus BmpTransport::writeMemory(const uint8_t* data,
                                            uint32_t       address,
                                            uint32_t       size) {
    if (!connected_) {
        m_error_ = "BMP not connected";
        return ProgrammerStatus::ErrorConnect;
    }
    return gdbWriteMemory(data, address, size);
}

// ── Serial helpers ──────────────────────────────────────────

ProgrammerStatus BmpTransport::serialOpen() {
#ifdef _WIN32
    serial_handle_ = CreateFileA(port_.c_str(),
                                 GENERIC_READ | GENERIC_WRITE,
                                 0, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (serial_handle_ == INVALID_HANDLE_VALUE) {
        serial_handle_ = nullptr;
        m_error_ = "Failed to open serial port " + port_;
        return ProgrammerStatus::ErrorConnect;
    }

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(serial_handle_, &dcb)) {
        serialClose();
        m_error_ = "Failed to get serial port config for " + port_;
        return ProgrammerStatus::ErrorConnect;
    }

    dcb.BaudRate = baud_;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    dcb.fParity  = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl  = DTR_CONTROL_ENABLE;
    dcb.fRtsControl  = RTS_CONTROL_ENABLE;

    if (!SetCommState(serial_handle_, &dcb)) {
        serialClose();
        m_error_ = "Failed to configure serial port " + port_;
        return ProgrammerStatus::ErrorConnect;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout         = 50;
    timeouts.ReadTotalTimeoutConstant    = SERIAL_TIMEOUT;
    timeouts.ReadTotalTimeoutMultiplier  = 10;
    timeouts.WriteTotalTimeoutConstant   = SERIAL_TIMEOUT;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(serial_handle_, &timeouts);

    return ProgrammerStatus::Ok;
#else
    serial_fd_ = open(port_.c_str(), O_RDWR | O_NOCTTY);
    if (serial_fd_ < 0) {
        m_error_ = "Failed to open serial port " + port_;
        return ProgrammerStatus::ErrorConnect;
    }

    struct termios tty = {};
    if (tcgetattr(serial_fd_, &tty) != 0) {
        serialClose();
        m_error_ = "Failed to get terminal attributes for " + port_;
        return ProgrammerStatus::ErrorConnect;
    }

    cfmakeraw(&tty);

    speed_t speed = B115200;
    switch (baud_) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        case 460800: speed = B460800; break;
        case 921600: speed = B921600; break;
    }
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = SERIAL_TIMEOUT / 100;

    if (tcsetattr(serial_fd_, TCSANOW, &tty) != 0) {
        serialClose();
        m_error_ = "Failed to set terminal attributes for " + port_;
        return ProgrammerStatus::ErrorConnect;
    }

    return ProgrammerStatus::Ok;
#endif
}

void BmpTransport::serialClose() {
#ifdef _WIN32
    if (serial_handle_) {
        CloseHandle(serial_handle_);
        serial_handle_ = nullptr;
    }
#else
    if (serial_fd_ >= 0) {
        close(serial_fd_);
        serial_fd_ = -1;
    }
#endif
}

ProgrammerStatus BmpTransport::serialWrite(const uint8_t* data, size_t size) {
#ifdef _WIN32
    DWORD written = 0;
    if (!WriteFile(serial_handle_, data, static_cast<DWORD>(size), &written, nullptr)) {
        m_error_ = "Serial write failed on " + port_;
        return ProgrammerStatus::ErrorWrite;
    }
    if (written != size) {
        m_error_ = "Serial write failed on " + port_;
        return ProgrammerStatus::ErrorWrite;
    }
    return ProgrammerStatus::Ok;
#else
    ssize_t written = write(serial_fd_, data, size);
    if (written != static_cast<ssize_t>(size)) {
        m_error_ = "Serial write failed on " + port_;
        return ProgrammerStatus::ErrorWrite;
    }
    return ProgrammerStatus::Ok;
#endif
}

ProgrammerStatus BmpTransport::serialRead(uint8_t* buffer, size_t size) {
#ifdef _WIN32
    DWORD read_count = 0;
    if (!ReadFile(serial_handle_, buffer, static_cast<DWORD>(size), &read_count, nullptr)) {
        m_error_ = "Serial read failed on " + port_;
        return ProgrammerStatus::ErrorRead;
    }
    if (read_count == 0) {
        m_error_ = "Serial read failed on " + port_;
        return ProgrammerStatus::ErrorRead;
    }
    return ProgrammerStatus::Ok;
#else
    ssize_t read_count = read(serial_fd_, buffer, size);
    if (read_count <= 0) {
        m_error_ = "Serial read failed on " + port_;
        return ProgrammerStatus::ErrorRead;
    }
    return ProgrammerStatus::Ok;
#endif
}

// ── GDB protocol helpers ────────────────────────────────────

uint8_t BmpTransport::gdbChecksum(const char* data, size_t length) {
    uint8_t sum = 0;
    for (size_t i = 0; i < length; i++)
        sum += static_cast<uint8_t>(data[i]);
    return sum;
}

ProgrammerStatus BmpTransport::gdbSend(const char* data, size_t length) {
    // Format: $<data>#<checksum_hex>
    uint8_t checksum = gdbChecksum(data, length);

    // Build packet: $ + data + # + 2 hex digits
    size_t pkt_size = 1 + length + 3;
    uint8_t pkt[bmp::MAX_PACKET + 4];
    if (pkt_size > sizeof(pkt))
        return ProgrammerStatus::ErrorWrite;

    pkt[0] = bmp::PACKET_START;
    std::memcpy(&pkt[1], data, length);
    pkt[1 + length] = bmp::PACKET_END;
    pkt[2 + length] = nibble_to_hex(checksum >> 4);
    pkt[3 + length] = nibble_to_hex(checksum & 0x0F);

    return serialWrite(pkt, pkt_size);
}

ProgrammerStatus BmpTransport::gdbReceive(uint8_t* buffer, size_t& length) {
    // Read until we find $ ... # XX
    uint8_t byte;
    length = 0;

    // Wait for '$'
    do {
        auto status = serialRead(&byte, 1);
        if (status != ProgrammerStatus::Ok)
            return status;
    } while (byte != bmp::PACKET_START);

    // Read payload until '#'
    while (length < bmp::MAX_PACKET) {
        auto status = serialRead(&byte, 1);
        if (status != ProgrammerStatus::Ok)
            return status;
        if (byte == bmp::PACKET_END)
            break;
        buffer[length++] = byte;
    }

    // Read 2-byte checksum (we don't verify it for now)
    uint8_t checksum_hex[2];
    auto status = serialRead(checksum_hex, 2);
    if (status != ProgrammerStatus::Ok)
        return status;

    // Send ACK
    uint8_t ack = bmp::ACK;
    return serialWrite(&ack, 1);
}

ProgrammerStatus BmpTransport::gdbCommand(const char* cmd,    size_t cmd_len,
                                           uint8_t*    reply,  size_t& reply_len) {
    auto status = gdbSend(cmd, cmd_len);
    if (status != ProgrammerStatus::Ok)
        return status;

    // Read ACK
    uint8_t ack;
    status = serialRead(&ack, 1);
    if (status != ProgrammerStatus::Ok)
        return status;
    if (ack != bmp::ACK) {
        m_error_ = "GDB bad ACK (expected '+')";
        return ProgrammerStatus::ErrorRead;
    }

    return gdbReceive(reply, reply_len);
}

// ── GDB memory commands ─────────────────────────────────────

ProgrammerStatus BmpTransport::gdbReadMemory(uint8_t* buffer,
                                              uint32_t address,
                                              uint32_t size) {
    // GDB read memory: 'm addr,length'
    // Response: hex-encoded bytes
    constexpr uint32_t CHUNK = bmp::MAX_PACKET / 2;  // hex encoding doubles size

    while (size > 0) {
        uint32_t chunk = std::min(size, CHUNK);

        char cmd[64];
        int cmd_len = snprintf(cmd, sizeof(cmd), "m%x,%x", address, chunk);

        uint8_t reply[bmp::MAX_PACKET];
        size_t reply_len = 0;
        auto status = gdbCommand(cmd, cmd_len, reply, reply_len);
        if (status != ProgrammerStatus::Ok)
            return status;

        // Check for error response
        if (reply_len > 0 && reply[0] == 'E') {
            char buf[64];
            snprintf(buf, sizeof(buf), "GDB read memory error response at 0x%08X", address);
            m_error_ = buf;
            return ProgrammerStatus::ErrorRead;
        }

        // Decode hex response into buffer
        if (!decode_hex(buffer, reinterpret_cast<const char*>(reply), reply_len)) {
            m_error_ = "GDB hex decode failure in read response";
            return ProgrammerStatus::ErrorRead;
        }

        buffer  += chunk;
        address += chunk;
        size    -= chunk;
    }
    return ProgrammerStatus::Ok;
}

ProgrammerStatus BmpTransport::gdbWriteMemory(const uint8_t* data,
                                               uint32_t       address,
                                               uint32_t       size) {
    // GDB write memory: 'M addr,length:hexdata'
    constexpr uint32_t CHUNK = (bmp::MAX_PACKET - 64) / 2;  // leave room for header

    while (size > 0) {
        uint32_t chunk = std::min(size, CHUNK);

        char cmd[bmp::MAX_PACKET];
        int header_len = snprintf(cmd, sizeof(cmd), "M%x,%x:", address, chunk);

        encode_hex(&cmd[header_len], data, chunk);
        size_t cmd_len = header_len + chunk * 2;

        uint8_t reply[bmp::MAX_PACKET];
        size_t reply_len = 0;
        auto status = gdbCommand(cmd, cmd_len, reply, reply_len);
        if (status != ProgrammerStatus::Ok)
            return status;

        // Expect "OK" response
        if (reply_len < 2 || reply[0] != 'O' || reply[1] != 'K') {
            char buf[64];
            snprintf(buf, sizeof(buf), "GDB write not acknowledged at 0x%08X", address);
            m_error_ = buf;
            return ProgrammerStatus::ErrorWrite;
        }

        data    += chunk;
        address += chunk;
        size    -= chunk;
    }
    return ProgrammerStatus::Ok;
}

ProgrammerStatus BmpTransport::gdbAttach() {
    // Scan for targets: 'qRcmd,7377616E' ("swdp_scan" in hex)
    const char scan_cmd[] = "qRcmd,737764705f7363616e";
    uint8_t reply[bmp::MAX_PACKET];
    size_t reply_len = 0;

    auto status = gdbCommand(scan_cmd, sizeof(scan_cmd) - 1, reply, reply_len);
    if (status != ProgrammerStatus::Ok)
        return status;

    // Attach to target 1: 'vAttach;1'
    const char attach_cmd[] = "vAttach;1";
    reply_len = 0;
    return gdbCommand(attach_cmd, sizeof(attach_cmd) - 1, reply, reply_len);
}

ProgrammerStatus BmpTransport::gdbDetach() {
    const char detach_cmd[] = "D";
    uint8_t reply[bmp::MAX_PACKET];
    size_t reply_len = 0;
    return gdbCommand(detach_cmd, 1, reply, reply_len);
}
