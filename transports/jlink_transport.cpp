/**
 ******************************************************************************
 * @file           : jlink_transport.cpp
 * @brief          : SEGGER J-Link debug probe transport layer implementation
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "jlink_transport.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

// ── Library loading helpers ─────────────────────────────────

static void* load_lib(const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(LoadLibraryA(name));
#else
    return dlopen(name, RTLD_NOW);
#endif
}

static void unload_lib(void* handle) {
    if (!handle) return;
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

static void* get_sym(void* handle, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(
        GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

// ── Probe scanning ──────────────────────────────────────────

std::vector<ProbeInfo> JLinkTransport::listProbes() {
    std::vector<ProbeInfo> result;

    void* lib = load_lib(jlink::LIB_NAME);
    if (!lib)
        return result;

    auto fn_open  = reinterpret_cast<jlink::FnOpen>(get_sym(lib, "JLINKARM_Open"));
    auto fn_close = reinterpret_cast<jlink::FnClose>(get_sym(lib, "JLINKARM_Close"));
    auto fn_sn    = reinterpret_cast<jlink::FnGetSN>(get_sym(lib, "JLINKARM_GetSN"));

    if (fn_open && fn_close) {
        if (fn_open() == 0) {
            ProbeInfo info;
            info.name = "J-Link";
            if (fn_sn) {
                uint32_t sn = fn_sn();
                info.serial = std::to_string(sn);
            }
            result.push_back(std::move(info));
            fn_close();
        }
    }

    unload_lib(lib);
    return result;
}

// ── Construction / Destruction ──────────────────────────────

JLinkTransport::JLinkTransport(uint32_t speed_khz, jlink::Interface iface)
    : speed_khz_(speed_khz), interface_(iface) {}

JLinkTransport::~JLinkTransport() {
    disconnect();
}

// ── Transport interface ─────────────────────────────────────

ProgrammerStatus JLinkTransport::connect() {
    auto status = loadLibrary();
    if (status != ProgrammerStatus::Ok)
        return status;

    // Open J-Link connection
    if (fn_open_() != 0) {
        m_error_ = "JLINKARM_Open failed";
        unloadLibrary();
        return ProgrammerStatus::ErrorConnect;
    }

    // Select interface (SWD or JTAG)
    fn_select_(static_cast<int>(interface_));

    // Set speed
    fn_set_speed_(speed_khz_);

    // Connect to target
    if (fn_connect_() != 0) {
        m_error_ = "JLINKARM_Connect failed";
        fn_close_();
        unloadLibrary();
        return ProgrammerStatus::ErrorConnect;
    }

    // Halt the core
    fn_halt_();

    connected_ = true;
    return ProgrammerStatus::Ok;
}

void JLinkTransport::disconnect() {
    if (!connected_)
        return;

    fn_go_();
    fn_close_();
    connected_ = false;
    unloadLibrary();
}

bool JLinkTransport::isConnected() const {
    return connected_;
}

ProgrammerStatus JLinkTransport::readMemory(uint8_t* buffer,
                                             uint32_t address,
                                             uint32_t size) {
    if (!connected_) {
        m_error_ = "J-Link not connected";
        return ProgrammerStatus::ErrorConnect;
    }

    int result = fn_read_mem_(address, size, buffer);
    if (result != 0) {
        char buf[96];
        snprintf(buf, sizeof(buf), "J-Link read failed at 0x%08X, size %u (error %d)", address, size, result);
        m_error_ = buf;
        return ProgrammerStatus::ErrorRead;
    }
    return ProgrammerStatus::Ok;
}

ProgrammerStatus JLinkTransport::writeMemory(const uint8_t* data,
                                              uint32_t       address,
                                              uint32_t       size) {
    if (!connected_) {
        m_error_ = "J-Link not connected";
        return ProgrammerStatus::ErrorConnect;
    }

    int result = fn_write_mem_(address, size, data);
    if (result != 0) {
        char buf[96];
        snprintf(buf, sizeof(buf), "J-Link write failed at 0x%08X, size %u (error %d)", address, size, result);
        m_error_ = buf;
        return ProgrammerStatus::ErrorWrite;
    }
    return ProgrammerStatus::Ok;
}

// ── Library loading ─────────────────────────────────────────

ProgrammerStatus JLinkTransport::loadLibrary() {
    lib_handle_ = load_lib(jlink::LIB_NAME);
    if (!lib_handle_) {
        m_error_ = std::string("Failed to load J-Link library: ") + jlink::LIB_NAME;
        return ProgrammerStatus::ErrorConnect;
    }

    // Resolve all function pointers
    fn_open_         = reinterpret_cast<jlink::FnOpen>(get_sym(lib_handle_, "JLINKARM_Open"));
    fn_close_        = reinterpret_cast<jlink::FnClose>(get_sym(lib_handle_, "JLINKARM_Close"));
    fn_connect_      = reinterpret_cast<jlink::FnConnect>(get_sym(lib_handle_, "JLINKARM_Connect"));
    fn_select_       = reinterpret_cast<jlink::FnSelect>(get_sym(lib_handle_, "JLINKARM_TIF_Select"));
    fn_set_speed_    = reinterpret_cast<jlink::FnSetSpeed>(get_sym(lib_handle_, "JLINKARM_SetSpeed"));
    fn_read_mem_     = reinterpret_cast<jlink::FnReadMem>(get_sym(lib_handle_, "JLINKARM_ReadMem"));
    fn_write_mem_    = reinterpret_cast<jlink::FnWriteMem>(get_sym(lib_handle_, "JLINKARM_WriteMem"));
    fn_reset_        = reinterpret_cast<jlink::FnReset>(get_sym(lib_handle_, "JLINKARM_Reset"));
    fn_halt_         = reinterpret_cast<jlink::FnHalt>(get_sym(lib_handle_, "JLINKARM_Halt"));
    fn_go_           = reinterpret_cast<jlink::FnGo>(get_sym(lib_handle_, "JLINKARM_Go"));
    fn_is_connected_ = reinterpret_cast<jlink::FnIsConnected>(get_sym(lib_handle_, "JLINKARM_IsConnected"));
    fn_get_sn_       = reinterpret_cast<jlink::FnGetSN>(get_sym(lib_handle_, "JLINKARM_GetSN"));

    // Check required functions
    if (!fn_open_ || !fn_close_ || !fn_connect_ || !fn_select_ ||
        !fn_set_speed_ || !fn_read_mem_ || !fn_write_mem_ ||
        !fn_halt_ || !fn_go_) {
        m_error_ = "Missing required function symbols in J-Link library";
        unloadLibrary();
        return ProgrammerStatus::ErrorConnect;
    }

    return ProgrammerStatus::Ok;
}

void JLinkTransport::unloadLibrary() {
    if (lib_handle_) {
        unload_lib(lib_handle_);
        lib_handle_ = nullptr;
    }

    fn_open_         = nullptr;
    fn_close_        = nullptr;
    fn_connect_      = nullptr;
    fn_select_       = nullptr;
    fn_set_speed_    = nullptr;
    fn_read_mem_     = nullptr;
    fn_write_mem_    = nullptr;
    fn_reset_        = nullptr;
    fn_halt_         = nullptr;
    fn_go_           = nullptr;
    fn_is_connected_ = nullptr;
    fn_get_sn_       = nullptr;
}
