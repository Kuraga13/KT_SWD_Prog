/**
 ******************************************************************************
 * @file           : probe_info.h
 * @brief          : Debug probe identification for enumeration/scanning
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Identifies a connected debug probe found during USB/serial scanning.
/// Used by transport listProbes() methods to report available hardware.
struct ProbeInfo {
    std::string name;       // "ST-Link V2", "ST-Link V3", "J-Link", "BMP (COM3)"
    std::string serial;     // serial number string
    uint16_t    vid = 0;    // USB VID (0 if not applicable)
    uint16_t    pid = 0;    // USB PID (0 if not applicable)
};
