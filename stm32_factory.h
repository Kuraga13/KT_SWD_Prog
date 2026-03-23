/**
 ******************************************************************************
 * @file           : stm32_factory.h
 * @brief          : Auto-detection and target driver factory
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
 * Provides auto-detection of the connected chip by probing identification
 * registers over SWD, and a factory function that returns the correct
 * TargetDriver for the detected family.
 *
 * Detection strategy:
 * 1. Probe STM32 DBGMCU_IDCODE at 4 addresses (covers all STM32 core types)
 * 2. Probe NXP SIM_SDID at 0x40048024 (covers Kinetis KE)
 *
 * Features:
 * - Auto-detection of STM32 and NXP Kinetis KE families
 * - Returns the matching TargetDriver instance for the detected family
 * - Reports detected family, DEV_ID, and REV_ID via output parameters
 * - Manual driver creation for known families
 *
 * Usage Example:
 * ```cpp
 * StLinkTransport transport;
 * transport.connect();
 *
 * ChipFamily family;
 * uint16_t dev_id, rev_id;
 * TargetDriver* driver = chip_factory::detect(transport, family, dev_id, rev_id);
 *
 * if (driver) {
 *     Stm32Programmer programmer(transport, *driver);
 *     programmer.readFlash(buffer, 0x08000000, size);
 * }
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include "stm32_programmer.h"
#include "stm32_chipid.h"

namespace chip_factory {

    /// Probe the connected chip and return the matching TargetDriver.
    /// Tries STM32 DBGMCU_IDCODE first, then NXP SIM_SDID.
    /// Returns nullptr if no chip is detected or the family is unsupported.
    TargetDriver* detect(Transport& transport,
                         ChipFamily& family,
                         uint16_t&   dev_id,
                         uint16_t&   rev_id);

    /// Create a TargetDriver for a known family (no auto-detection).
    /// Returns nullptr if the family is Unknown or unsupported.
    TargetDriver* createDriver(ChipFamily family);

    /// Get a human-readable name for a family.
    const char* familyName(ChipFamily family);

} // namespace chip_factory

// Backward compatibility
namespace stm32_factory = chip_factory;
