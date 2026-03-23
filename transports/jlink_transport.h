/**
 ******************************************************************************
 * @file           : jlink_transport.h
 * @brief          : SEGGER J-Link debug probe transport layer
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
 * Transport implementation for SEGGER J-Link debug probes.
 * Loads the SEGGER JLinkARM library at runtime (DLL on Windows,
 * shared object on Linux/macOS) and calls its API through resolved
 * function pointers. This allows the tool to compile and link
 * without the J-Link SDK installed.
 *
 * Features:
 * - Runtime dynamic loading of JLinkARM library (no link-time dependency)
 * - Cross-platform support (Windows DLL / Linux .so / macOS .dylib)
 * - SWD and JTAG interface selection
 * - Configurable interface speed
 * - Automatic function pointer validation on load
 *
 * Usage Example:
 * ```cpp
 * JLinkTransport transport(4000, jlink::Interface::SWD);
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
#include <string>

/// J-Link communicates via SEGGER's proprietary library (JLinkARM.dll / libjlinkarm.so).
/// We load the library at runtime so the tool can still compile without it installed.
namespace jlink {
    /// Default J-Link interface speed (kHz)
    constexpr uint32_t DEFAULT_SPEED_KHZ = 4000;

    /// J-Link interface type
    enum class Interface : uint8_t {
        SWD  = 1,
        JTAG = 0,
    };

    /// J-Link library function signatures (loaded at runtime)
    using FnOpen              = int       (*)();
    using FnClose             = void      (*)();
    using FnConnect           = int       (*)();
    using FnSelect            = void      (*)(int interface);
    using FnSetSpeed          = void      (*)(uint32_t speed_khz);
    using FnReadMem           = int       (*)(uint32_t addr, uint32_t size, void* buffer);
    using FnWriteMem          = int       (*)(uint32_t addr, uint32_t size, const void* data);
    using FnReset             = void      (*)();
    using FnHalt              = int       (*)();
    using FnGo                = void      (*)();
    using FnIsConnected       = bool      (*)();
    using FnGetSN             = uint32_t  (*)();

#ifdef _WIN32
    constexpr const char* LIB_NAME = "JLinkARM.dll";
#elif __APPLE__
    constexpr const char* LIB_NAME = "libjlinkarm.dylib";
#else
    constexpr const char* LIB_NAME = "libjlinkarm.so";
#endif
}

/// J-Link transport — communicates with a SEGGER J-Link probe
/// via the J-Link SDK library (loaded at runtime).
class JLinkTransport : public Transport {
public:
    explicit JLinkTransport(uint32_t         speed_khz = jlink::DEFAULT_SPEED_KHZ,
                            jlink::Interface iface     = jlink::Interface::SWD);
    ~JLinkTransport() override;

    JLinkTransport(const JLinkTransport&)            = delete;
    JLinkTransport& operator=(const JLinkTransport&) = delete;

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
    // ── Library loading ─────────────────────────────────────

    /// Load JLinkARM library and resolve function pointers
    ProgrammerStatus loadLibrary();

    /// Unload the library
    void unloadLibrary();

    // ── State ───────────────────────────────────────────────

    uint32_t         speed_khz_;
    jlink::Interface interface_;
    bool             connected_ = false;

    /// Library handle (HMODULE on Windows, void* on Linux/macOS)
    void* lib_handle_ = nullptr;

    // ── Resolved function pointers ──────────────────────────

    jlink::FnOpen         fn_open_         = nullptr;
    jlink::FnClose        fn_close_        = nullptr;
    jlink::FnConnect      fn_connect_      = nullptr;
    jlink::FnSelect       fn_select_       = nullptr;
    jlink::FnSetSpeed     fn_set_speed_    = nullptr;
    jlink::FnReadMem      fn_read_mem_     = nullptr;
    jlink::FnWriteMem     fn_write_mem_    = nullptr;
    jlink::FnReset        fn_reset_        = nullptr;
    jlink::FnHalt         fn_halt_         = nullptr;
    jlink::FnGo           fn_go_           = nullptr;
    jlink::FnIsConnected  fn_is_connected_ = nullptr;
    jlink::FnGetSN        fn_get_sn_       = nullptr;
};
