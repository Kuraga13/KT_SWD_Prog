/**
 ******************************************************************************
 * @file           : stm32_chipid.h
 * @brief          : Chip identification via DBGMCU_IDCODE and SIM_SDID
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
 * Constants and types for identifying the connected chip by reading
 * identification registers over SWD.
 *
 * STM32 chips use DBGMCU_IDCODE (address depends on core type):
 * - 0x40015800 for Cortex-M0/M0+ (F0, G0, L0)
 * - 0xE0042000 for Cortex-M3/M4/M7 (F1-F7, G4, L1, L4, WB, WL)
 * - 0x5C001000 for H7 (debug domain)
 * - 0xE0044000 for Cortex-M33 (L5, U5, H5)
 *
 * NXP Kinetis KE chips use SIM_SDID at 0x40048024.
 *
 * Features:
 * - DBGMCU_IDCODE register addresses for all STM32 core types
 * - SIM_SDID register address for NXP Kinetis KE
 * - DEV_ID values for all STM32 families and sub-variants
 * - SDID values for NXP MKE families
 * - ChipFamily enum for family identification
 * - Auto-detection probing order (STM32 first, then NXP)
 *
 * Usage Example:
 * ```cpp
 * ChipFamily family;
 * uint16_t dev_id, rev_id;
 * TargetDriver* driver = chip_factory::detect(transport, family, dev_id, rev_id);
 * ```
 *
 ******************************************************************************
 */

#pragma once

#include <cstdint>

/// Chip family identifier (STM32 + NXP Kinetis KE)
enum class ChipFamily : uint8_t {
    Unknown,

    // STM32
    F0,
    F1,
    F2,
    F3,
    F4,
    F7,
    G0,
    G4,
    H5,
    H7,
    H7AB,   // H7A3, H7B0, H7B3 (RM0455, 128-bit flash word)
    H7RS,   // H7R3, H7R7, H7S3, H7S7 (RM0477, 64 KB boot flash)
    L0,
    L1,
    L4,
    L5,
    U5,
    WB,
    WL,

    // NXP Kinetis KE
    KE02,    // MKE02 (FTMRH flash)
    KE04,    // MKE04 (FTMRH flash)
    KE06,    // MKE06 (FTMRH flash)
    KE14,    // MKE14Z/F (FTFE flash)
    KE15,    // MKE15Z (FTFE flash)
    KE16,    // MKE16F (FTFE flash)
    KE18,    // MKE18F (FTFE flash)
};

// Backward compatibility
using Stm32Family = ChipFamily;

namespace stm32_chipid {

    // ── DBGMCU_IDCODE register addresses (depends on core) ──

    constexpr uint32_t DBGMCU_M0        = 0x40015800;  // Cortex-M0/M0+ (F0, G0, L0)
    constexpr uint32_t DBGMCU_M3_M4_M7  = 0xE0042000;  // Cortex-M3/M4/M7 (F1-F7, G4, L1, L4, WB, WL)
    constexpr uint32_t DBGMCU_H7        = 0x5C001000;  // H7 (debug domain)
    constexpr uint32_t DBGMCU_M33       = 0xE0044000;  // Cortex-M33 (L5, U5, H5)

    /// Probing order for auto-detection (most common first)
    constexpr uint32_t PROBE_ADDRESSES[] = {
        DBGMCU_M3_M4_M7,  // covers F1, F2, F3, F4, F7, G4, L1, L4, WB, WL
        DBGMCU_M0,         // covers F0, G0, L0
        DBGMCU_H7,         // covers H7
        DBGMCU_M33,        // covers L5, U5, H5
    };
    constexpr size_t PROBE_COUNT = sizeof(PROBE_ADDRESSES) / sizeof(PROBE_ADDRESSES[0]);

    // ── DEV_ID values (bits 11:0 of DBGMCU_IDCODE) ─────────

    // STM32F0
    constexpr uint16_t DEV_F030_F070       = 0x440;
    constexpr uint16_t DEV_F030xC_F09x     = 0x442;
    constexpr uint16_t DEV_F03x            = 0x444;
    constexpr uint16_t DEV_F04x            = 0x445;
    constexpr uint16_t DEV_F07x            = 0x448;

    // STM32F1
    constexpr uint16_t DEV_F1_MEDIUM       = 0x410;
    constexpr uint16_t DEV_F1_LOW          = 0x412;
    constexpr uint16_t DEV_F1_HIGH         = 0x414;
    constexpr uint16_t DEV_F1_CONN         = 0x418;
    constexpr uint16_t DEV_F1_VL_LOW       = 0x420;
    constexpr uint16_t DEV_F1_VL_HIGH      = 0x428;
    constexpr uint16_t DEV_F1_XL           = 0x430;

    // STM32F2
    constexpr uint16_t DEV_F2              = 0x411;

    // STM32F3
    constexpr uint16_t DEV_F302_F303_BC    = 0x422;
    constexpr uint16_t DEV_F373            = 0x432;
    constexpr uint16_t DEV_F303_68_F334    = 0x438;
    constexpr uint16_t DEV_F301_F302_68    = 0x439;
    constexpr uint16_t DEV_F302_F303_DE    = 0x446;

    // STM32F4
    constexpr uint16_t DEV_F405_F407       = 0x413;
    constexpr uint16_t DEV_F42x_F43x       = 0x419;
    constexpr uint16_t DEV_F446            = 0x421;
    constexpr uint16_t DEV_F401_BC         = 0x423;
    constexpr uint16_t DEV_F411            = 0x431;
    constexpr uint16_t DEV_F401_DE         = 0x433;
    constexpr uint16_t DEV_F469_F479       = 0x434;
    constexpr uint16_t DEV_F412            = 0x441;
    constexpr uint16_t DEV_F410            = 0x458;

    // STM32F7
    constexpr uint16_t DEV_F74x_F75x       = 0x449;
    constexpr uint16_t DEV_F76x_F77x       = 0x451;
    constexpr uint16_t DEV_F72x_F73x       = 0x452;

    // STM32G0
    constexpr uint16_t DEV_G05x_G06x       = 0x456;
    constexpr uint16_t DEV_G07x_G08x       = 0x460;
    constexpr uint16_t DEV_G03x_G04x       = 0x466;
    constexpr uint16_t DEV_G0Bx_G0Cx       = 0x467;

    // STM32G4
    constexpr uint16_t DEV_G431_G441       = 0x468;
    constexpr uint16_t DEV_G47x_G48x       = 0x469;
    constexpr uint16_t DEV_G49x_G4Ax       = 0x479;

    // STM32H5
    constexpr uint16_t DEV_H503            = 0x474;
    constexpr uint16_t DEV_H562_H563       = 0x478;
    constexpr uint16_t DEV_H523_H533       = 0x484;

    // STM32H7
    constexpr uint16_t DEV_H74x_H75x       = 0x450;
    constexpr uint16_t DEV_H7Ax_H7Bx       = 0x480;
    constexpr uint16_t DEV_H72x_H73x       = 0x483;
    constexpr uint16_t DEV_H7Rx_H7Sx       = 0x485;

    // STM32L0
    constexpr uint16_t DEV_L05x_L06x       = 0x417;
    constexpr uint16_t DEV_L031_L041       = 0x425;
    constexpr uint16_t DEV_L07x_L08x       = 0x447;
    constexpr uint16_t DEV_L01x_L02x       = 0x457;

    // STM32L1
    constexpr uint16_t DEV_L1_CAT1         = 0x416;
    constexpr uint16_t DEV_L1_CAT3         = 0x427;
    constexpr uint16_t DEV_L1_CAT2         = 0x429;
    constexpr uint16_t DEV_L1_CAT4_5_6     = 0x436;
    constexpr uint16_t DEV_L1_CAT5_6       = 0x437;

    // STM32L4
    constexpr uint16_t DEV_L47x_L48x       = 0x415;
    constexpr uint16_t DEV_L43x_L44x       = 0x435;
    constexpr uint16_t DEV_L496_L4A6       = 0x461;
    constexpr uint16_t DEV_L45x_L46x       = 0x462;
    constexpr uint16_t DEV_L41x_L42x       = 0x464;
    constexpr uint16_t DEV_L4Rx_L4Sx       = 0x470;
    constexpr uint16_t DEV_L4Px_L4Qx       = 0x471;

    // STM32L5
    constexpr uint16_t DEV_L552_L562       = 0x472;

    // STM32U5
    constexpr uint16_t DEV_U575_U585       = 0x455;
    constexpr uint16_t DEV_U535_U545       = 0x476;
    constexpr uint16_t DEV_U595_U5A5       = 0x481;
    constexpr uint16_t DEV_U5Fx_U5Gx       = 0x482;

    // STM32WB
    constexpr uint16_t DEV_WB55_WB35       = 0x495;
    constexpr uint16_t DEV_WB50_WB30       = 0x496;

    // STM32WB/WL (shared ID — needs additional heuristics)
    constexpr uint16_t DEV_WBx5_WLx5       = 0x497;

    /// Identify the STM32 family from the DEV_ID field
    inline ChipFamily identifyFamily(uint16_t dev_id) {
        switch (dev_id) {
            // F0
            case DEV_F030_F070:
            case DEV_F030xC_F09x:
            case DEV_F03x:
            case DEV_F04x:
            case DEV_F07x:
                return ChipFamily::F0;

            // F1
            case DEV_F1_MEDIUM:
            case DEV_F1_LOW:
            case DEV_F1_HIGH:
            case DEV_F1_CONN:
            case DEV_F1_VL_LOW:
            case DEV_F1_VL_HIGH:
            case DEV_F1_XL:
                return ChipFamily::F1;

            // F2
            case DEV_F2:
                return ChipFamily::F2;

            // F3
            case DEV_F302_F303_BC:
            case DEV_F373:
            case DEV_F303_68_F334:
            case DEV_F301_F302_68:
            case DEV_F302_F303_DE:
                return ChipFamily::F3;

            // F4
            case DEV_F405_F407:
            case DEV_F42x_F43x:
            case DEV_F446:
            case DEV_F401_BC:
            case DEV_F411:
            case DEV_F401_DE:
            case DEV_F469_F479:
            case DEV_F412:
            case DEV_F410:
                return ChipFamily::F4;

            // F7
            case DEV_F74x_F75x:
            case DEV_F76x_F77x:
            case DEV_F72x_F73x:
                return ChipFamily::F7;

            // G0
            case DEV_G05x_G06x:
            case DEV_G07x_G08x:
            case DEV_G03x_G04x:
            case DEV_G0Bx_G0Cx:
                return ChipFamily::G0;

            // G4
            case DEV_G431_G441:
            case DEV_G47x_G48x:
            case DEV_G49x_G4Ax:
                return ChipFamily::G4;

            // H5
            case DEV_H503:
            case DEV_H562_H563:
            case DEV_H523_H533:
                return ChipFamily::H5;

            // H7 (RM0433 / RM0468)
            case DEV_H74x_H75x:
            case DEV_H72x_H73x:
                return ChipFamily::H7;

            // H7AB (RM0455 — different flash controller)
            case DEV_H7Ax_H7Bx:
                return ChipFamily::H7AB;

            // H7RS (RM0477 — 64 KB boot flash, single-bank)
            case DEV_H7Rx_H7Sx:
                return ChipFamily::H7RS;

            // L0
            case DEV_L05x_L06x:
            case DEV_L031_L041:
            case DEV_L07x_L08x:
            case DEV_L01x_L02x:
                return ChipFamily::L0;

            // L1
            case DEV_L1_CAT1:
            case DEV_L1_CAT3:
            case DEV_L1_CAT2:
            case DEV_L1_CAT4_5_6:
            case DEV_L1_CAT5_6:
                return ChipFamily::L1;

            // L4
            case DEV_L47x_L48x:
            case DEV_L43x_L44x:
            case DEV_L496_L4A6:
            case DEV_L45x_L46x:
            case DEV_L41x_L42x:
            case DEV_L4Rx_L4Sx:
            case DEV_L4Px_L4Qx:
                return ChipFamily::L4;

            // L5
            case DEV_L552_L562:
                return ChipFamily::L5;

            // U5
            case DEV_U575_U585:
            case DEV_U535_U545:
            case DEV_U595_U5A5:
            case DEV_U5Fx_U5Gx:
                return ChipFamily::U5;

            // WB
            case DEV_WB55_WB35:
            case DEV_WB50_WB30:
                return ChipFamily::WB;

            // WB/WL shared — default to WL
            case DEV_WBx5_WLx5:
                return ChipFamily::WL;

            default:
                return ChipFamily::Unknown;
        }
    }

} // namespace stm32_chipid

namespace nxp_chipid {

    // ── SIM_SDID register (System Integration Module) ───────

    constexpr uint32_t SIM_SDID         = 0x40048024;

    // ── Subfamily ID values (bits 27:24 of SIM_SDID for KE) ─

    // Older KE (FTMRH flash controller)
    constexpr uint8_t SUBFAM_KE02       = 0x02;
    constexpr uint8_t SUBFAM_KE04       = 0x04;
    constexpr uint8_t SUBFAM_KE06       = 0x06;

    // Newer KE (FTFE flash controller)
    constexpr uint8_t SUBFAM_KE14       = 0x04;  // distinguished by FAMID
    constexpr uint8_t SUBFAM_KE15       = 0x05;
    constexpr uint8_t SUBFAM_KE16       = 0x06;
    constexpr uint8_t SUBFAM_KE18       = 0x08;

    // Family ID field (bits 23:20 of SIM_SDID)
    constexpr uint8_t FAMID_KE_OLD      = 0x00;  // KE02/04/06
    constexpr uint8_t FAMID_KE_NEW      = 0x01;  // KE14/15/16/18

    /// Try to identify an NXP Kinetis KE family from the SIM_SDID value.
    /// Returns ChipFamily::Unknown if the SDID doesn't match any known KE part.
    inline ChipFamily identifyFamily(uint32_t sdid) {
        uint8_t famid  = static_cast<uint8_t>((sdid >> 20) & 0x0F);
        uint8_t subfam = static_cast<uint8_t>((sdid >> 24) & 0x0F);

        if (famid == FAMID_KE_OLD) {
            switch (subfam) {
                case 0x02: return ChipFamily::KE02;
                case 0x04: return ChipFamily::KE04;
                case 0x06: return ChipFamily::KE06;
            }
        } else if (famid == FAMID_KE_NEW) {
            switch (subfam) {
                case 0x04: return ChipFamily::KE14;
                case 0x05: return ChipFamily::KE15;
                case 0x06: return ChipFamily::KE16;
                case 0x08: return ChipFamily::KE18;
            }
        }

        return ChipFamily::Unknown;
    }

} // namespace nxp_chipid
