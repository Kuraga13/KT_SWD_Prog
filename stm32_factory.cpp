/**
 ******************************************************************************
 * @file           : stm32_factory.cpp
 * @brief          : Auto-detection and target driver factory implementation
 * @author         : Kuraga Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Kuraga Tech.
 * Licensed under the MIT License. See LICENSE file for details.
 *
 ******************************************************************************
 */

#include "stm32_factory.h"
#include <cctype>

// STM32 target drivers
#include "flash_drivers/stm32f0_flash.h"
#include "flash_drivers/stm32f1_flash.h"
#include "flash_drivers/stm32f2_flash.h"
#include "flash_drivers/stm32f3_flash.h"
#include "flash_drivers/stm32f4_flash.h"
#include "flash_drivers/stm32f7_flash.h"
#include "flash_drivers/stm32g0_flash.h"
#include "flash_drivers/stm32g4_flash.h"
#include "flash_drivers/stm32h5_flash.h"
#include "flash_drivers/stm32h7_flash.h"
#include "flash_drivers/stm32h7ab_flash.h"
#include "flash_drivers/stm32l0_flash.h"
#include "flash_drivers/stm32l1_flash.h"
#include "flash_drivers/stm32l4_flash.h"
#include "flash_drivers/stm32l5_flash.h"
#include "flash_drivers/stm32u5_flash.h"
#include "flash_drivers/stm32wb_flash.h"
#include "flash_drivers/stm32wl_flash.h"

// NXP target drivers
#include "flash_drivers/nxp/mke_ftmrh.h"
#include "flash_drivers/nxp/mke_ftfe.h"

// ── Static driver instances ─────────────────────────────────

// STM32
static Stm32F0FlashDriver  driver_f0;
static Stm32F1FlashDriver  driver_f1;
static Stm32F2FlashDriver  driver_f2;
static Stm32F3FlashDriver  driver_f3;
static Stm32F4FlashDriver  driver_f4;
static Stm32F7FlashDriver  driver_f7;
static Stm32G0FlashDriver  driver_g0;
static Stm32G4FlashDriver  driver_g4;
static Stm32H5FlashDriver  driver_h5;
static Stm32H7FlashDriver   driver_h7;
static Stm32H7abFlashDriver driver_h7ab;
static Stm32L0FlashDriver  driver_l0;
static Stm32L1FlashDriver  driver_l1;
static Stm32L4FlashDriver  driver_l4;
static Stm32L5FlashDriver  driver_l5;
static Stm32U5FlashDriver  driver_u5;
static Stm32WbFlashDriver  driver_wb;
static Stm32WlFlashDriver  driver_wl;

// NXP Kinetis KE
static MkeFtmrhTargetDriver driver_ke_ftmrh;
static MkeFtfeTargetDriver  driver_ke_ftfe;

// ── Helpers ─────────────────────────────────────────────────

static uint32_t read_id_register(Transport& transport, uint32_t address) {
    uint8_t buf[4] = {};
    auto status = transport.readMemory(buf, address, 4);
    if (status != ProgrammerStatus::Ok)
        return 0;
    return static_cast<uint32_t>(buf[0])
         | (static_cast<uint32_t>(buf[1]) << 8)
         | (static_cast<uint32_t>(buf[2]) << 16)
         | (static_cast<uint32_t>(buf[3]) << 24);
}

/// Try to detect an STM32 chip by probing DBGMCU_IDCODE addresses
static TargetDriver* detectStm32(Transport& transport,
                                  ChipFamily& family,
                                  uint16_t& dev_id,
                                  uint16_t& rev_id) {
    for (size_t i = 0; i < stm32_chipid::PROBE_COUNT; i++) {
        uint32_t idcode = read_id_register(transport, stm32_chipid::PROBE_ADDRESSES[i]);

        uint16_t candidate_dev = static_cast<uint16_t>(idcode & 0xFFF);
        uint16_t candidate_rev = static_cast<uint16_t>(idcode >> 16);

        if (candidate_dev == 0x000 || candidate_dev == 0xFFF)
            continue;

        ChipFamily candidate_family = stm32_chipid::identifyFamily(candidate_dev);
        if (candidate_family != ChipFamily::Unknown) {
            family = candidate_family;
            dev_id = candidate_dev;
            rev_id = candidate_rev;
            return chip_factory::createDriver(family);
        }
    }
    return nullptr;
}

/// Try to detect an NXP Kinetis KE chip by reading SIM_SDID
static TargetDriver* detectNxpKe(Transport& transport,
                                  ChipFamily& family,
                                  uint16_t& dev_id,
                                  uint16_t& rev_id) {
    uint32_t sdid = read_id_register(transport, nxp_chipid::SIM_SDID);

    if (sdid == 0x00000000 || sdid == 0xFFFFFFFF)
        return nullptr;

    ChipFamily candidate_family = nxp_chipid::identifyFamily(sdid);
    if (candidate_family != ChipFamily::Unknown) {
        family = candidate_family;
        dev_id = static_cast<uint16_t>(sdid & 0xFFFF);
        rev_id = static_cast<uint16_t>(sdid >> 16);
        return chip_factory::createDriver(family);
    }

    return nullptr;
}

// ── Public interface ────────────────────────────────────────

TargetDriver* chip_factory::detect(Transport& transport,
                                    ChipFamily& family,
                                    uint16_t&   dev_id,
                                    uint16_t&   rev_id) {
    family = ChipFamily::Unknown;
    dev_id = 0;
    rev_id = 0;

    // Try STM32 first (most common)
    TargetDriver* driver = detectStm32(transport, family, dev_id, rev_id);
    if (driver)
        return driver;

    // Try NXP Kinetis KE
    driver = detectNxpKe(transport, family, dev_id, rev_id);
    if (driver)
        return driver;

    return nullptr;
}

TargetDriver* chip_factory::createDriver(ChipFamily family) {
    TargetDriver* driver = nullptr;
    switch (family) {
        // STM32
        case ChipFamily::F0: driver = &driver_f0; break;
        case ChipFamily::F1: driver = &driver_f1; break;
        case ChipFamily::F2: driver = &driver_f2; break;
        case ChipFamily::F3: driver = &driver_f3; break;
        case ChipFamily::F4: driver = &driver_f4; break;
        case ChipFamily::F7: driver = &driver_f7; break;
        case ChipFamily::G0: driver = &driver_g0; break;
        case ChipFamily::G4: driver = &driver_g4; break;
        case ChipFamily::H5: driver = &driver_h5; break;
        case ChipFamily::H7:   driver = &driver_h7; break;
        case ChipFamily::H7AB: driver = &driver_h7ab; break;
        case ChipFamily::L0: driver = &driver_l0; break;
        case ChipFamily::L1: driver = &driver_l1; break;
        case ChipFamily::L4: driver = &driver_l4; break;
        case ChipFamily::L5: driver = &driver_l5; break;
        case ChipFamily::U5: driver = &driver_u5; break;
        case ChipFamily::WB: driver = &driver_wb; break;
        case ChipFamily::WL: driver = &driver_wl; break;

        // NXP Kinetis KE — FTMRH flash controller
        case ChipFamily::KE02:
        case ChipFamily::KE04:
        case ChipFamily::KE06: driver = &driver_ke_ftmrh; break;

        // NXP Kinetis KE — FTFE flash controller
        case ChipFamily::KE14:
        case ChipFamily::KE15:
        case ChipFamily::KE16:
        case ChipFamily::KE18: driver = &driver_ke_ftfe; break;

        default: return nullptr;
    }
    driver->clearError();
    return driver;
}

/// Case-insensitive string comparison
static bool str_eq_ci(const char* a, const char* b) {
    while (*a && *b) {
        if (std::toupper(static_cast<unsigned char>(*a)) !=
            std::toupper(static_cast<unsigned char>(*b)))
            return false;
        ++a; ++b;
    }
    return *a == *b;
}

/// Parse a family name string to ChipFamily enum.
/// Uses full names only (e.g. "STM32G0", "MKE15") to avoid collisions.
ChipFamily chip_factory::familyFromString(const char* name) {
    if (!name || !*name)
        return ChipFamily::Unknown;

    // STM32 — full names only to avoid collisions
    if (str_eq_ci(name, "STM32F0"))   return ChipFamily::F0;
    if (str_eq_ci(name, "STM32F1"))   return ChipFamily::F1;
    if (str_eq_ci(name, "STM32F2"))   return ChipFamily::F2;
    if (str_eq_ci(name, "STM32F3"))   return ChipFamily::F3;
    if (str_eq_ci(name, "STM32F4"))   return ChipFamily::F4;
    if (str_eq_ci(name, "STM32F7"))   return ChipFamily::F7;
    if (str_eq_ci(name, "STM32G0"))   return ChipFamily::G0;
    if (str_eq_ci(name, "STM32G4"))   return ChipFamily::G4;
    if (str_eq_ci(name, "STM32H5"))   return ChipFamily::H5;
    if (str_eq_ci(name, "STM32H7AB")) return ChipFamily::H7AB;  // before H7
    if (str_eq_ci(name, "STM32H7"))   return ChipFamily::H7;
    if (str_eq_ci(name, "STM32L0"))   return ChipFamily::L0;
    if (str_eq_ci(name, "STM32L1"))   return ChipFamily::L1;
    if (str_eq_ci(name, "STM32L4"))   return ChipFamily::L4;
    if (str_eq_ci(name, "STM32L5"))   return ChipFamily::L5;
    if (str_eq_ci(name, "STM32U5"))   return ChipFamily::U5;
    if (str_eq_ci(name, "STM32WB"))   return ChipFamily::WB;
    if (str_eq_ci(name, "STM32WL"))   return ChipFamily::WL;

    // NXP Kinetis KE — full names only
    if (str_eq_ci(name, "MKE02"))     return ChipFamily::KE02;
    if (str_eq_ci(name, "MKE04"))     return ChipFamily::KE04;
    if (str_eq_ci(name, "MKE06"))     return ChipFamily::KE06;
    if (str_eq_ci(name, "MKE14"))     return ChipFamily::KE14;
    if (str_eq_ci(name, "MKE15"))     return ChipFamily::KE15;
    if (str_eq_ci(name, "MKE16"))     return ChipFamily::KE16;
    if (str_eq_ci(name, "MKE18"))     return ChipFamily::KE18;

    return ChipFamily::Unknown;
}

const char* chip_factory::familyName(ChipFamily family) {
    switch (family) {
        // STM32
        case ChipFamily::F0: return "STM32F0";
        case ChipFamily::F1: return "STM32F1";
        case ChipFamily::F2: return "STM32F2";
        case ChipFamily::F3: return "STM32F3";
        case ChipFamily::F4: return "STM32F4";
        case ChipFamily::F7: return "STM32F7";
        case ChipFamily::G0: return "STM32G0";
        case ChipFamily::G4: return "STM32G4";
        case ChipFamily::H5: return "STM32H5";
        case ChipFamily::H7:   return "STM32H7";
        case ChipFamily::H7AB: return "STM32H7AB";
        case ChipFamily::L0: return "STM32L0";
        case ChipFamily::L1: return "STM32L1";
        case ChipFamily::L4: return "STM32L4";
        case ChipFamily::L5: return "STM32L5";
        case ChipFamily::U5: return "STM32U5";
        case ChipFamily::WB: return "STM32WB";
        case ChipFamily::WL: return "STM32WL";

        // NXP
        case ChipFamily::KE02: return "MKE02 (FTMRH)";
        case ChipFamily::KE04: return "MKE04 (FTMRH)";
        case ChipFamily::KE06: return "MKE06 (FTMRH)";
        case ChipFamily::KE14: return "MKE14 (FTFE)";
        case ChipFamily::KE15: return "MKE15 (FTFE)";
        case ChipFamily::KE16: return "MKE16 (FTFE)";
        case ChipFamily::KE18: return "MKE18 (FTFE)";

        default: return "Unknown";
    }
}
