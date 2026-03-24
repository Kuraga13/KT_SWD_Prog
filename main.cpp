/**
 ******************************************************************************
 * @file           : main.cpp
 * @brief          : CLI entry point for KuragaSTM32Tool
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
 * Command-line interface for the KuragaSTM32Tool universal programmer.
 * Supports detect, read, write, erase, and RDP check operations
 * across all supported debug probes and chip families.
 *
 ******************************************************************************
 */

#include "stm32_programmer.h"
#include "stm32_chipid.h"
#include "stm32_factory.h"

#ifdef KT_HAS_STLINK
#include "transports/stlink_transport.h"
#endif
#ifdef KT_HAS_BMP
#include "transports/bmp_transport.h"
#endif
#ifdef KT_HAS_JLINK
#include "transports/jlink_transport.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

// ── Helpers ─────────────────────────────────────────────────────────────────

static void print_usage() {
    printf(
        "Usage: kt_swd_cli <command> [options]\n"
        "\n"
        "Commands:\n"
        "  probe                               Show debug probe info and target voltage\n"
        "  detect                              Auto-detect connected chip\n"
        "  read flash <addr> <size> -o <file>  Read flash memory to file\n"
        "  read ob <addr> <size> -o <file>     Read option bytes to file\n"
        "  read otp <addr> <size> -o <file>    Read OTP memory to file\n"
        "  write flash <file> <addr>           Write flash from file\n"
        "  write ob <file> <addr> [--unsafe]    Write option bytes from file\n"
        "  write otp <file> <addr>             Write OTP from file\n"
        "  erase                               Mass erase flash\n"
        "  rdp                                 Check RDP level\n"
        "\n"
        "Options:\n"
        "  --probe stlink|bmp|jlink  Debug probe type (default: stlink)\n"
        "  --port <path>             Serial port for BMP (e.g. COM3)\n"
        "  --speed <kHz>             SWD speed for J-Link (default: 4000)\n"
        "  --chip <family>           Override auto-detect (e.g. F4, G0, KE02)\n"
        "  --unsafe                  Skip option byte sanitization (write as-is)\n"
        "  --no-stub                 Disable flash loader stub (use slow SWD writes)\n"
        "  --help                    Show this help message\n"
    );
}

static const char* statusToString(ProgrammerStatus s) {
    switch (s) {
        case ProgrammerStatus::Ok:            return "OK";
        case ProgrammerStatus::ErrorConnect:   return "connection error";
        case ProgrammerStatus::ErrorProtected: return "protected";
        case ProgrammerStatus::ErrorErase:     return "erase error";
        case ProgrammerStatus::ErrorRead:      return "read error";
        case ProgrammerStatus::ErrorWrite:     return "write error";
        case ProgrammerStatus::ErrorVerify:    return "verify error";
        case ProgrammerStatus::ErrorAborted:   return "aborted";
        default:                               return "unknown error";
    }
}

static const char* rdpToString(RdpLevel r) {
    switch (r) {
        case RdpLevel::Level0:  return "Level 0 (no protection)";
        case RdpLevel::Level1:  return "Level 1 (flash read blocked, mass erase to revert)";
        case RdpLevel::Level2:  return "Level 2 (permanently locked)";
        case RdpLevel::Unknown: return "Unknown";
        default:                return "Unknown";
    }
}

/// Print error detail from transport and/or driver (whichever has a message)
static void printErrorDetail(Transport* transport, TargetDriver* driver = nullptr) {
    const std::string* detail = nullptr;
    if (driver && !driver->getLastError().empty())
        detail = &driver->getLastError();
    else if (transport && !transport->getLastError().empty())
        detail = &transport->getLastError();
    if (detail)
        fprintf(stderr, "  Detail: %s\n", detail->c_str());
}

static uint32_t parseNumber(const char* s) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return static_cast<uint32_t>(strtoul(s, nullptr, 16));
    return static_cast<uint32_t>(strtoul(s, nullptr, 10));
}

static std::string toUpper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return result;
}

static ChipFamily parseChipFamily(const std::string& name) {
    std::string u = toUpper(name);

    // Strip common prefixes
    if (u.size() > 5 && u.substr(0, 5) == "STM32") u = u.substr(5);
    if (u.size() > 3 && u.substr(0, 3) == "MKE")   u = "KE" + u.substr(3);

    if (u == "F0")   return ChipFamily::F0;
    if (u == "F1")   return ChipFamily::F1;
    if (u == "F2")   return ChipFamily::F2;
    if (u == "F3")   return ChipFamily::F3;
    if (u == "F4")   return ChipFamily::F4;
    if (u == "F7")   return ChipFamily::F7;
    if (u == "G0")   return ChipFamily::G0;
    if (u == "G4")   return ChipFamily::G4;
    if (u == "H5")   return ChipFamily::H5;
    if (u == "H7")   return ChipFamily::H7;
    if (u == "L0")   return ChipFamily::L0;
    if (u == "L1")   return ChipFamily::L1;
    if (u == "L4")   return ChipFamily::L4;
    if (u == "L5")   return ChipFamily::L5;
    if (u == "U5")   return ChipFamily::U5;
    if (u == "WB")   return ChipFamily::WB;
    if (u == "WL")   return ChipFamily::WL;
    if (u == "KE02") return ChipFamily::KE02;
    if (u == "KE04") return ChipFamily::KE04;
    if (u == "KE06") return ChipFamily::KE06;
    if (u == "KE14") return ChipFamily::KE14;
    if (u == "KE15") return ChipFamily::KE15;
    if (u == "KE16") return ChipFamily::KE16;
    if (u == "KE18") return ChipFamily::KE18;
    return ChipFamily::Unknown;
}

static std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

static std::unique_ptr<Transport> createTransport(const std::string& probe_raw,
                                                   const std::string& port,
                                                   uint32_t speed) {
    std::string probe = toLower(probe_raw);

    if (probe == "stlink") {
#ifdef KT_HAS_STLINK
        return std::make_unique<StLinkTransport>();
#else
        fprintf(stderr, "Error: ST-Link transport not enabled in this build\n");
        return nullptr;
#endif
    }

    if (probe == "bmp") {
#ifdef KT_HAS_BMP
        if (port.empty()) {
            fprintf(stderr, "Error: --port required for BMP transport\n");
            return nullptr;
        }
        return std::make_unique<BmpTransport>(port);
#else
        fprintf(stderr, "Error: BMP transport not enabled in this build\n");
        return nullptr;
#endif
    }

    if (probe == "jlink") {
#ifdef KT_HAS_JLINK
        return std::make_unique<JLinkTransport>(speed);
#else
        fprintf(stderr, "Error: J-Link transport not enabled in this build\n");
        return nullptr;
#endif
    }

    fprintf(stderr, "Error: unknown probe '%s' (use stlink, bmp, or jlink)\n", probe.c_str());
    return nullptr;
}

// ── Progress display ────────────────────────────────────────────────────────

static bool cliProgressCallback(uint32_t bytes_done, uint32_t bytes_total, void*) {
    if (bytes_total == 0) return true;

    constexpr int BAR_WIDTH = 30;
    int filled = static_cast<int>(static_cast<uint64_t>(bytes_done) * BAR_WIDTH / bytes_total);
    int percent = static_cast<int>(static_cast<uint64_t>(bytes_done) * 100 / bytes_total);

    printf("\r  [");
    for (int i = 0; i < BAR_WIDTH; i++)
        putchar(i < filled ? '#' : '.');
    printf("] %3d%%  %u / %u bytes", percent, bytes_done, bytes_total);
    fflush(stdout);

    if (bytes_done >= bytes_total)
        printf("\n");

    return true;
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // ── Parse arguments ─────────────────────────────────────
    std::vector<std::string> positional;
    std::string probe = "stlink";
    std::string port;
    uint32_t    speed = 4000;
    std::string chip_override;
    std::string output_file;
    bool        unsafe = false;
    bool        no_stub = false;

    for (int i = 1; i < argc; ) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--probe" && i + 1 < argc) {
            probe = argv[++i]; ++i;
        } else if (arg == "--port" && i + 1 < argc) {
            port = argv[++i]; ++i;
        } else if (arg == "--speed" && i + 1 < argc) {
            speed = parseNumber(argv[++i]); ++i;
        } else if (arg == "--chip" && i + 1 < argc) {
            chip_override = argv[++i]; ++i;
        } else if (arg == "-o" && i + 1 < argc) {
            output_file = argv[++i]; ++i;
        } else if (arg == "--unsafe") {
            unsafe = true; ++i;
        } else if (arg == "--no-stub") {
            no_stub = true; ++i;
        } else {
            positional.push_back(arg);
            ++i;
        }
    }

    if (positional.empty()) {
        print_usage();
        return 1;
    }

    // Normalize probe name so all comparisons are case-insensitive
    probe = toLower(probe);

    // ── Create transport ────────────────────────────────────
    auto transport = createTransport(probe, port, speed);
    if (!transport) return 1;

    std::string cmd = positional[0];

    // ── probe ───────────────────────────────────────────────
    if (cmd == "probe") {
        auto status = transport->openProbe();
        if (status != ProgrammerStatus::Ok) {
            fprintf(stderr, "Error: no debug probe found on USB\n");
            printErrorDetail(transport.get());
            return 1;
        }

        printf("Probe: %s\n", transport->probeName());
        int fw = transport->probeFirmwareVersion();
        if (fw > 0) printf("Firmware: JTAG/SWD v%d\n", fw);

        float voltage = transport->targetVoltage();
        if (voltage >= 0.0f) {
            printf("Target voltage: %.2f V\n", voltage);
            if (voltage < 0.5f)
                printf("WARNING: target not powered or not connected\n");
        }

        transport->closeProbe();
        return 0;
    }

    // ── detect ──────────────────────────────────────────────
    if (cmd == "detect") {
        auto status = transport->connect();
        if (status != ProgrammerStatus::Ok) {
            fprintf(stderr, "Error: failed to connect (%s)\n", statusToString(status));
            printErrorDetail(transport.get());
            return 1;
        }

        ChipFamily family = ChipFamily::Unknown;
        uint16_t dev_id = 0, rev_id = 0;
        auto* driver = chip_factory::detect(*transport, family, dev_id, rev_id);

        if (driver) {
            printf("Detected: %s", chip_factory::familyName(family));
            if (dev_id) printf(" (DEV_ID: 0x%03X, REV_ID: 0x%04X)", dev_id, rev_id);
            printf("\n");
        } else {
            fprintf(stderr, "Error: no chip detected\n");
        }

        transport->disconnect();
        return driver ? 0 : 1;
    }

    // ── Commands that require a target driver ───────────────

    // Phase 1: Connect raw transport for chip detection
    auto status = transport->connect();
    if (status != ProgrammerStatus::Ok) {
        fprintf(stderr, "Error: failed to connect (%s)\n", statusToString(status));
        printErrorDetail(transport.get());
        return 1;
    }

    ChipFamily family = ChipFamily::Unknown;
    uint16_t dev_id = 0, rev_id = 0;
    TargetDriver* driver = nullptr;

    if (!chip_override.empty()) {
        family = parseChipFamily(chip_override);
        if (family == ChipFamily::Unknown) {
            fprintf(stderr, "Error: unknown chip family '%s'\n", chip_override.c_str());
            transport->disconnect();
            return 1;
        }
        driver = chip_factory::createDriver(family);
    } else {
        driver = chip_factory::detect(*transport, family, dev_id, rev_id);
    }

    if (!driver) {
        fprintf(stderr, "Error: no chip detected or unsupported family\n");
        transport->disconnect();
        return 1;
    }

    printf("Chip: %s", chip_factory::familyName(family));
    if (dev_id) printf(" (DEV_ID: 0x%03X, REV_ID: 0x%04X)", dev_id, rev_id);
    printf("\n");

    // Close detection session
    transport->disconnect();

    // Phase 2: Reconnect through Stm32Programmer for full driver control
    Stm32Programmer programmer(*transport, *driver);
    programmer.setProgressCallback(cliProgressCallback);
    if (!no_stub) programmer.setUseStub(probe == "stlink");

    status = programmer.connect();
    if (status != ProgrammerStatus::Ok) {
        fprintf(stderr, "Error: failed to connect (%s)\n", statusToString(status));
        if (!programmer.getLastError().empty())
            fprintf(stderr, "  Detail: %s\n", programmer.getLastError().c_str());
        return 1;
    }

    int result = 0;

    // ── read ────────────────────────────────────────────────
    if (cmd == "read") {
        if (positional.size() < 4) {
            fprintf(stderr, "Usage: kt_swd_cli read <flash|ob|otp> <addr> <size> -o <file>\n");
            result = 1;
        } else if (output_file.empty()) {
            fprintf(stderr, "Error: -o <file> required for read\n");
            result = 1;
        } else {
            std::string region = positional[1];
            uint32_t addr = parseNumber(positional[2].c_str());
            uint32_t size = parseNumber(positional[3].c_str());

            if (region != "flash" && region != "ob" && region != "otp") {
                fprintf(stderr, "Error: unknown region '%s' (use flash, ob, or otp)\n",
                        region.c_str());
                result = 1;
            } else {
                std::vector<uint8_t> buffer(size);
                printf("Reading %u bytes from 0x%08X (%s)...\n", size, addr, region.c_str());

                if (region == "ob")
                    status = programmer.readOptionBytes(buffer.data(), addr, size);
                else if (region == "otp")
                    status = programmer.readOtp(buffer.data(), addr, size);
                else
                    status = programmer.readFlash(buffer.data(), addr, size);

                if (status != ProgrammerStatus::Ok) {
                    fprintf(stderr, "Error: read failed (%s)\n", statusToString(status));
                    if (!programmer.getLastError().empty())
                        fprintf(stderr, "  Detail: %s\n", programmer.getLastError().c_str());
                    result = 1;
                } else {
                    FILE* f = fopen(output_file.c_str(), "wb");
                    if (!f) {
                        fprintf(stderr, "Error: cannot open '%s' for writing\n",
                                output_file.c_str());
                        result = 1;
                    } else if (fwrite(buffer.data(), 1, size, f) != size) {
                        fprintf(stderr, "Error: failed to write '%s'\n",
                                output_file.c_str());
                        fclose(f);
                        result = 1;
                    } else {
                        fclose(f);
                        printf("Written %u bytes to %s\n", size, output_file.c_str());
                    }
                }
            }
        }
    }
    // ── write ───────────────────────────────────────────────
    else if (cmd == "write") {
        if (positional.size() < 4) {
            fprintf(stderr, "Usage: kt_swd_cli write <flash|ob|otp> <file> <addr>\n");
            result = 1;
        } else {
            std::string region    = positional[1];
            std::string input_file = positional[2];
            uint32_t addr         = parseNumber(positional[3].c_str());

            if (region != "flash" && region != "ob" && region != "otp") {
                fprintf(stderr, "Error: unknown region '%s' (use flash, ob, or otp)\n",
                        region.c_str());
                result = 1;
            } else {
                FILE* f = fopen(input_file.c_str(), "rb");
                if (!f) {
                    fprintf(stderr, "Error: cannot open '%s' for reading\n",
                            input_file.c_str());
                    result = 1;
                } else {
                    fseek(f, 0, SEEK_END);
                    long fsize = ftell(f);
                    if (fsize < 0) {
                        fprintf(stderr, "Error: cannot determine size of '%s'\n",
                                input_file.c_str());
                        fclose(f);
                        result = 1;
                        goto cleanup;
                    }
                    fseek(f, 0, SEEK_SET);

                    std::vector<uint8_t> data(fsize);
                    if (fread(data.data(), 1, fsize, f) != static_cast<size_t>(fsize)) {
                        fprintf(stderr, "Error: failed to read '%s'\n",
                                input_file.c_str());
                        fclose(f);
                        result = 1;
                        goto cleanup;
                    }
                    fclose(f);

                    uint32_t size = static_cast<uint32_t>(fsize);
                    printf("Writing %u bytes to 0x%08X (%s)...\n", size, addr, region.c_str());

                    if (region == "flash")
                        status = programmer.writeFlash(data.data(), addr, size);
                    else if (region == "ob")
                        status = programmer.writeOptionBytes(data.data(), addr, size, unsafe);
                    else
                        status = programmer.writeOtp(data.data(), addr, size);

                    if (status != ProgrammerStatus::Ok) {
                        fprintf(stderr, "Error: write failed (%s)\n", statusToString(status));
                        if (!programmer.getLastError().empty())
                            fprintf(stderr, "  Detail: %s\n", programmer.getLastError().c_str());
                        result = 1;
                    } else {
                        printf("Write complete\n");
                    }
                }
            }
        }
    }
    // ── erase ───────────────────────────────────────────────
    else if (cmd == "erase") {
        printf("Erasing flash...\n");
        status = programmer.eraseFlash();
        if (status != ProgrammerStatus::Ok) {
            fprintf(stderr, "Error: erase failed (%s)\n", statusToString(status));
            if (!programmer.getLastError().empty())
                fprintf(stderr, "  Detail: %s\n", programmer.getLastError().c_str());
            result = 1;
        } else {
            printf("Flash erased successfully\n");
        }
    }
    // ── rdp ─────────────────────────────────────────────────
    else if (cmd == "rdp") {
        RdpLevel rdp = programmer.readRdpLevel();
        printf("RDP: %s\n", rdpToString(rdp));
    }
    // ── unknown command ─────────────────────────────────────
    else {
        fprintf(stderr, "Error: unknown command '%s'\n", cmd.c_str());
        print_usage();
        result = 1;
    }

cleanup:
    programmer.disconnect();
    return result;
}
