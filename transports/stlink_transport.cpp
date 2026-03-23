/**
 ******************************************************************************
 * @file           : stlink_transport.cpp
 * @brief          : ST-Link debug probe transport layer implementation
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "stlink_transport.h"
#include <cstring>
#include <algorithm>

/// USB timeout in milliseconds
static constexpr int USB_TIMEOUT = 1000;

/// Supported ST-Link product IDs
static constexpr uint16_t SUPPORTED_PIDS[] = {
    stlink::USB_PID_V2,
    stlink::USB_PID_V2_1,
    stlink::USB_PID_V3,
    stlink::USB_PID_V3E,
    stlink::USB_PID_V3_MINIE,
};

// ── Probe name from PID ─────────────────────────────────────

static const char* probename_from_pid(uint16_t pid) {
    if (pid == stlink::USB_PID_V3 || pid == stlink::USB_PID_V3E ||
        pid == stlink::USB_PID_V3_MINIE)
        return "ST-Link V3";
    if (pid == stlink::USB_PID_V2_1)
        return "ST-Link V2-1";
    if (pid == stlink::USB_PID_V2)
        return "ST-Link V2";
    return "ST-Link";
}

// ── Probe scanning ──────────────────────────────────────────

std::vector<ProbeInfo> StLinkTransport::listProbes() {
    std::vector<ProbeInfo> result;

    libusb_context* ctx = nullptr;
    if (libusb_init(&ctx) != 0)
        return result;

    libusb_device** devs = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &devs);
    if (count < 0) {
        libusb_exit(ctx);
        return result;
    }

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(devs[i], &desc) != 0)
            continue;

        if (desc.idVendor != stlink::USB_VID)
            continue;

        // Check if PID matches a supported ST-Link
        bool supported = false;
        for (uint16_t pid : SUPPORTED_PIDS) {
            if (desc.idProduct == pid) {
                supported = true;
                break;
            }
        }
        if (!supported)
            continue;

        ProbeInfo info;
        info.name = probename_from_pid(desc.idProduct);
        info.vid  = desc.idVendor;
        info.pid  = desc.idProduct;

        // Try to read serial number string
        if (desc.iSerialNumber != 0) {
            libusb_device_handle* handle = nullptr;
            if (libusb_open(devs[i], &handle) == 0) {
                unsigned char serial_buf[128] = {};
                int len = libusb_get_string_descriptor_ascii(handle,
                    desc.iSerialNumber, serial_buf, sizeof(serial_buf));
                if (len > 0)
                    info.serial.assign(reinterpret_cast<char*>(serial_buf),
                                       static_cast<size_t>(len));
                libusb_close(handle);
            }
        }

        result.push_back(std::move(info));
    }

    libusb_free_device_list(devs, 1);
    libusb_exit(ctx);
    return result;
}

// ── Helpers ─────────────────────────────────────────────────

static void store_le16(uint8_t* dst, uint16_t val) {
    dst[0] = static_cast<uint8_t>(val);
    dst[1] = static_cast<uint8_t>(val >> 8);
}

static void store_le32(uint8_t* dst, uint32_t val) {
    dst[0] = static_cast<uint8_t>(val);
    dst[1] = static_cast<uint8_t>(val >> 8);
    dst[2] = static_cast<uint8_t>(val >> 16);
    dst[3] = static_cast<uint8_t>(val >> 24);
}

// ── Construction / Destruction ──────────────────────────────

StLinkTransport::StLinkTransport() = default;

StLinkTransport::~StLinkTransport() {
    disconnect();
}

// ── Transport interface ─────────────────────────────────────

ProgrammerStatus StLinkTransport::connect() {
    auto status = usbOpen();
    if (status != ProgrammerStatus::Ok)
        return status;

    readProbeInfo();

    status = enterSwd();
    if (status != ProgrammerStatus::Ok) {
        usbClose();
        return status;
    }

    connected_ = true;
    return ProgrammerStatus::Ok;
}

void StLinkTransport::disconnect() {
    if (!connected_)
        return;

    // Send exit debug mode command
    uint8_t cmd[stlink::CMD_SIZE] = {};
    cmd[0] = stlink::CMD_DEBUG;
    cmd[1] = stlink::DEBUG_EXIT;
    uint8_t reply[2];
    usbTransfer(cmd, sizeof(cmd), reply, sizeof(reply));

    connected_ = false;
    usbClose();
}

bool StLinkTransport::isConnected() const {
    return connected_;
}

ProgrammerStatus StLinkTransport::readMemory(uint8_t* buffer,
                                              uint32_t address,
                                              uint32_t size) {
    if (!connected_) {
        m_error_ = "ST-Link not connected";
        return ProgrammerStatus::ErrorConnect;
    }

    // Use 32-bit reads for aligned access, 8-bit for the rest
    uint32_t head_unaligned = address & 3;
    if (head_unaligned != 0) {
        uint32_t head_size = std::min(4 - head_unaligned, size);
        auto status = readMemory8(buffer, address, head_size);
        if (status != ProgrammerStatus::Ok)
            return status;
        buffer  += head_size;
        address += head_size;
        size    -= head_size;
    }

    // 32-bit aligned body
    uint32_t aligned_size = size & ~3u;
    if (aligned_size > 0) {
        auto status = readMemory32(buffer, address, aligned_size);
        if (status != ProgrammerStatus::Ok)
            return status;
        buffer  += aligned_size;
        address += aligned_size;
        size    -= aligned_size;
    }

    // Remaining tail bytes
    if (size > 0) {
        return readMemory8(buffer, address, size);
    }

    return ProgrammerStatus::Ok;
}

ProgrammerStatus StLinkTransport::writeMemory(const uint8_t* data,
                                               uint32_t       address,
                                               uint32_t       size) {
    if (!connected_) {
        m_error_ = "ST-Link not connected";
        return ProgrammerStatus::ErrorConnect;
    }

    // Handle unaligned head (bring address up to 4-byte alignment)
    if ((address & 1) && size >= 1) {
        auto status = writeMemory8(data, address, 1);
        if (status != ProgrammerStatus::Ok)
            return status;
        data++; address++; size--;
    }
    if ((address & 2) && size >= 2) {
        auto status = writeMemory16(data, address, 2);
        if (status != ProgrammerStatus::Ok)
            return status;
        data += 2; address += 2; size -= 2;
    }

    // 32-bit aligned body
    uint32_t aligned_size = size & ~3u;
    if (aligned_size > 0) {
        auto status = writeMemory32(data, address, aligned_size);
        if (status != ProgrammerStatus::Ok)
            return status;
        data    += aligned_size;
        address += aligned_size;
        size    -= aligned_size;
    }

    // Remaining 16-bit aligned tail
    if (size >= 2) {
        uint32_t hw_size = size & ~1u;
        auto status = writeMemory16(data, address, hw_size);
        if (status != ProgrammerStatus::Ok)
            return status;
        data    += hw_size;
        address += hw_size;
        size    -= hw_size;
    }

    // Remaining odd byte
    if (size > 0) {
        return writeMemory8(data, address, size);
    }

    return ProgrammerStatus::Ok;
}

// ── USB helpers ─────────────────────────────────────────────

ProgrammerStatus StLinkTransport::usbOpen() {
    int ret = libusb_init(&usb_ctx_);
    if (ret != 0) {
        m_error_ = "libusb_init failed";
        return ProgrammerStatus::ErrorConnect;
    }

    // Try each supported PID
    for (uint16_t pid : SUPPORTED_PIDS) {
        usb_handle_ = libusb_open_device_with_vid_pid(usb_ctx_,
                                                       stlink::USB_VID, pid);
        if (usb_handle_)
            break;
    }

    if (!usb_handle_) {
        m_error_ = "No ST-Link found on USB";
        libusb_exit(usb_ctx_);
        usb_ctx_ = nullptr;
        return ProgrammerStatus::ErrorConnect;
    }

    // Detach kernel driver if active
    if (libusb_kernel_driver_active(usb_handle_, 0) == 1)
        libusb_detach_kernel_driver(usb_handle_, 0);

    ret = libusb_claim_interface(usb_handle_, 0);
    if (ret != 0) {
        m_error_ = "Failed to claim USB interface (libusb error " + std::to_string(ret) + ")";
        usbClose();
        return ProgrammerStatus::ErrorConnect;
    }

    // Detect endpoints from interface descriptor
    libusb_device* dev = libusb_get_device(usb_handle_);
    struct libusb_config_descriptor* config = nullptr;
    ret = libusb_get_active_config_descriptor(dev, &config);
    if (ret != 0 || !config) {
        m_error_ = "Failed to read USB config descriptor";
        usbClose();
        return ProgrammerStatus::ErrorConnect;
    }

    const libusb_interface_descriptor* iface = &config->interface[0].altsetting[0];
    for (int i = 0; i < iface->bNumEndpoints; i++) {
        const libusb_endpoint_descriptor* ep = &iface->endpoint[i];
        if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK) {
            if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) && ep_in_ == 0)
                ep_in_ = ep->bEndpointAddress;
            else if (!(ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) && ep_out_ == 0)
                ep_out_ = ep->bEndpointAddress;
        }
    }
    libusb_free_config_descriptor(config);

    if (ep_in_ == 0 || ep_out_ == 0) {
        m_error_ = "No bulk endpoints found on ST-Link USB interface";
        usbClose();
        return ProgrammerStatus::ErrorConnect;
    }

    return ProgrammerStatus::Ok;
}

void StLinkTransport::usbClose() {
    if (usb_handle_) {
        libusb_release_interface(usb_handle_, 0);
        libusb_close(usb_handle_);
        usb_handle_ = nullptr;
    }
    if (usb_ctx_) {
        libusb_exit(usb_ctx_);
        usb_ctx_ = nullptr;
    }
    ep_in_  = 0;
    ep_out_ = 0;
}

ProgrammerStatus StLinkTransport::usbTransfer(const uint8_t* cmd,   size_t cmd_size,
                                               uint8_t*       reply, size_t reply_size) {
    int transferred = 0;

    int ret = libusb_bulk_transfer(usb_handle_, ep_out_,
                                   const_cast<uint8_t*>(cmd),
                                   static_cast<int>(cmd_size),
                                   &transferred, USB_TIMEOUT);
    if (ret != 0) {
        m_error_ = "USB bulk OUT transfer failed (libusb error " + std::to_string(ret) + ")";
        return ProgrammerStatus::ErrorWrite;
    }

    if (reply && reply_size > 0) {
        ret = libusb_bulk_transfer(usb_handle_, ep_in_,
                                   reply,
                                   static_cast<int>(reply_size),
                                   &transferred, USB_TIMEOUT);
        if (ret != 0) {
            m_error_ = "USB bulk IN transfer failed (libusb error " + std::to_string(ret) + ")";
            return ProgrammerStatus::ErrorRead;
        }
    }

    return ProgrammerStatus::Ok;
}

ProgrammerStatus StLinkTransport::usbRead(uint8_t* buffer, size_t size) {
    int transferred = 0;
    int ret = libusb_bulk_transfer(usb_handle_, ep_in_,
                                   buffer,
                                   static_cast<int>(size),
                                   &transferred, USB_TIMEOUT);
    return (ret == 0) ? ProgrammerStatus::Ok : ProgrammerStatus::ErrorRead;
}

ProgrammerStatus StLinkTransport::usbWrite(const uint8_t* data, size_t size) {
    int transferred = 0;
    int ret = libusb_bulk_transfer(usb_handle_, ep_out_,
                                   const_cast<uint8_t*>(data),
                                   static_cast<int>(size),
                                   &transferred, USB_TIMEOUT);
    return (ret == 0) ? ProgrammerStatus::Ok : ProgrammerStatus::ErrorWrite;
}

// ── Probe info ──────────────────────────────────────────────

ProgrammerStatus StLinkTransport::openProbe() {
    auto status = usbOpen();
    if (status != ProgrammerStatus::Ok)
        return status;
    readProbeInfo();
    return ProgrammerStatus::Ok;
}

void StLinkTransport::closeProbe() {
    usbClose();
}

void StLinkTransport::readProbeInfo() {
    uint8_t cmd[stlink::CMD_SIZE] = {};
    uint8_t reply[6] = {};
    cmd[0] = stlink::CMD_GET_VERSION;
    if (usbTransfer(cmd, sizeof(cmd), reply, sizeof(reply)) == ProgrammerStatus::Ok) {
        uint16_t ver = (static_cast<uint16_t>(reply[0]) << 8) | reply[1];
        stlink_version_ = static_cast<uint8_t>((ver >> 12) & 0x0F);
        jtag_version_   = static_cast<uint8_t>((ver >> 6)  & 0x3F);
        pid_            = static_cast<uint16_t>(reply[4]) | (static_cast<uint16_t>(reply[5]) << 8);
    }
}

const char* StLinkTransport::probeName() {
    if (pid_ == stlink::USB_PID_V3 || pid_ == stlink::USB_PID_V3E ||
        pid_ == stlink::USB_PID_V3_MINIE)
        return "ST-Link V3";
    if (pid_ == stlink::USB_PID_V2_1)
        return "ST-Link V2-1";
    if (pid_ == stlink::USB_PID_V2)
        return "ST-Link V2";
    return "ST-Link";
}

int StLinkTransport::probeFirmwareVersion() {
    return (jtag_version_ > 0) ? static_cast<int>(jtag_version_) : -1;
}

float StLinkTransport::targetVoltage() {
    uint8_t cmd[stlink::CMD_SIZE] = {};
    uint8_t reply[8] = {};
    cmd[0] = stlink::CMD_GET_TARGET_VOLTAGE;
    if (usbTransfer(cmd, sizeof(cmd), reply, sizeof(reply)) != ProgrammerStatus::Ok)
        return -1.0f;

    uint32_t adc_ref = static_cast<uint32_t>(reply[0])       |
                       (static_cast<uint32_t>(reply[1]) << 8) |
                       (static_cast<uint32_t>(reply[2]) << 16) |
                       (static_cast<uint32_t>(reply[3]) << 24);
    uint32_t adc_tgt = static_cast<uint32_t>(reply[4])       |
                       (static_cast<uint32_t>(reply[5]) << 8) |
                       (static_cast<uint32_t>(reply[6]) << 16) |
                       (static_cast<uint32_t>(reply[7]) << 24);

    if (adc_ref == 0)
        return -1.0f;

    return 2.4f * static_cast<float>(adc_tgt) / static_cast<float>(adc_ref);
}

// ── ST-Link protocol helpers ────────────────────────────────

ProgrammerStatus StLinkTransport::enterSwd() {
    uint8_t cmd[stlink::CMD_SIZE] = {};
    uint8_t reply[2] = {};

    cmd[0] = stlink::CMD_DEBUG;
    cmd[1] = stlink::DEBUG_ENTER_SWD;
    cmd[2] = 0xA3;  // SWD mode

    return usbTransfer(cmd, sizeof(cmd), reply, sizeof(reply));
}

ProgrammerStatus StLinkTransport::haltCore() {
    uint8_t cmd[stlink::CMD_SIZE] = {};
    uint8_t reply[2] = {};
    cmd[0] = stlink::CMD_DEBUG;
    cmd[1] = stlink::DEBUG_FORCE_HALT;
    return usbTransfer(cmd, sizeof(cmd), reply, sizeof(reply));
}

ProgrammerStatus StLinkTransport::resumeCore() {
    uint8_t cmd[stlink::CMD_SIZE] = {};
    uint8_t reply[2] = {};
    cmd[0] = stlink::CMD_DEBUG;
    cmd[1] = stlink::DEBUG_RUN_CORE;
    return usbTransfer(cmd, sizeof(cmd), reply, sizeof(reply));
}

ProgrammerStatus StLinkTransport::resetTarget() {
    uint8_t cmd[stlink::CMD_SIZE] = {};
    uint8_t reply[2] = {};
    cmd[0] = stlink::CMD_DEBUG;
    cmd[1] = stlink::DEBUG_RESET_SYS;
    return usbTransfer(cmd, sizeof(cmd), reply, sizeof(reply));
}

ProgrammerStatus StLinkTransport::readMemory32(uint8_t* buffer,
                                                uint32_t address,
                                                uint32_t size) {
    while (size > 0) {
        uint32_t chunk = std::min(size, stlink::MAX_TRANSFER);

        uint8_t cmd[stlink::CMD_SIZE] = {};
        cmd[0] = stlink::CMD_DEBUG;
        cmd[1] = stlink::DEBUG_READMEM_32;
        store_le32(&cmd[2], address);
        store_le16(&cmd[6], static_cast<uint16_t>(chunk));

        auto status = usbTransfer(cmd, sizeof(cmd), nullptr, 0);
        if (status != ProgrammerStatus::Ok)
            return status;

        status = usbRead(buffer, chunk);
        if (status != ProgrammerStatus::Ok)
            return status;

        buffer  += chunk;
        address += chunk;
        size    -= chunk;
    }
    return ProgrammerStatus::Ok;
}

ProgrammerStatus StLinkTransport::readMemory8(uint8_t* buffer,
                                               uint32_t address,
                                               uint32_t size) {
    while (size > 0) {
        uint32_t chunk = std::min(size, stlink::MAX_TRANSFER);

        uint8_t cmd[stlink::CMD_SIZE] = {};
        cmd[0] = stlink::CMD_DEBUG;
        cmd[1] = stlink::DEBUG_READMEM_8;
        store_le32(&cmd[2], address);
        store_le16(&cmd[6], static_cast<uint16_t>(chunk));

        auto status = usbTransfer(cmd, sizeof(cmd), nullptr, 0);
        if (status != ProgrammerStatus::Ok)
            return status;

        status = usbRead(buffer, chunk);
        if (status != ProgrammerStatus::Ok)
            return status;

        buffer  += chunk;
        address += chunk;
        size    -= chunk;
    }
    return ProgrammerStatus::Ok;
}

ProgrammerStatus StLinkTransport::writeMemory32(const uint8_t* data,
                                                 uint32_t       address,
                                                 uint32_t       size) {
    while (size > 0) {
        uint32_t chunk = std::min(size, stlink::MAX_TRANSFER);

        uint8_t cmd[stlink::CMD_SIZE] = {};
        cmd[0] = stlink::CMD_DEBUG;
        cmd[1] = stlink::DEBUG_WRITEMEM_32;
        store_le32(&cmd[2], address);
        store_le16(&cmd[6], static_cast<uint16_t>(chunk));

        auto status = usbTransfer(cmd, sizeof(cmd), nullptr, 0);
        if (status != ProgrammerStatus::Ok)
            return status;

        status = usbWrite(data, chunk);
        if (status != ProgrammerStatus::Ok)
            return status;

        data    += chunk;
        address += chunk;
        size    -= chunk;
    }
    return ProgrammerStatus::Ok;
}

ProgrammerStatus StLinkTransport::writeMemory16(const uint8_t* data,
                                                 uint32_t       address,
                                                 uint32_t       size) {
    while (size > 0) {
        uint32_t chunk = std::min(size, stlink::MAX_TRANSFER);

        uint8_t cmd[stlink::CMD_SIZE] = {};
        cmd[0] = stlink::CMD_DEBUG;
        cmd[1] = stlink::DEBUG_WRITEMEM_16;
        store_le32(&cmd[2], address);
        store_le16(&cmd[6], static_cast<uint16_t>(chunk));

        auto status = usbTransfer(cmd, sizeof(cmd), nullptr, 0);
        if (status != ProgrammerStatus::Ok)
            return status;

        status = usbWrite(data, chunk);
        if (status != ProgrammerStatus::Ok)
            return status;

        data    += chunk;
        address += chunk;
        size    -= chunk;
    }
    return ProgrammerStatus::Ok;
}

ProgrammerStatus StLinkTransport::writeMemory8(const uint8_t* data,
                                                uint32_t       address,
                                                uint32_t       size) {
    while (size > 0) {
        uint32_t chunk = std::min(size, stlink::MAX_TRANSFER);

        uint8_t cmd[stlink::CMD_SIZE] = {};
        cmd[0] = stlink::CMD_DEBUG;
        cmd[1] = stlink::DEBUG_WRITEMEM_8;
        store_le32(&cmd[2], address);
        store_le16(&cmd[6], static_cast<uint16_t>(chunk));

        auto status = usbTransfer(cmd, sizeof(cmd), nullptr, 0);
        if (status != ProgrammerStatus::Ok)
            return status;

        status = usbWrite(data, chunk);
        if (status != ProgrammerStatus::Ok)
            return status;

        data    += chunk;
        address += chunk;
        size    -= chunk;
    }
    return ProgrammerStatus::Ok;
}
