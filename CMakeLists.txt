cmake_minimum_required(VERSION 3.20)
project(KuragaSTM32Tool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Options ──────────────────────────────────────────────────────────────────

option(KT_TRANSPORT_STLINK "Enable ST-Link transport (requires libusb)" ON)
option(KT_TRANSPORT_BMP    "Enable Black Magic Probe transport"         OFF)
option(KT_TRANSPORT_JLINK  "Enable J-Link transport"                    OFF)
option(KT_BUILD_CLI        "Build CLI executable"                       ON)

# ── Library sources ──────────────────────────────────────────────────────────

# Factory (always included)
set(KT_SOURCES
    stm32_factory.cpp
)

# Flash drivers (always included)
set(KT_FLASH_SOURCES
    flash_drivers/stm32f0_flash.cpp
    flash_drivers/stm32f1_flash.cpp
    flash_drivers/stm32f2_flash.cpp
    flash_drivers/stm32f3_flash.cpp
    flash_drivers/stm32f4_flash.cpp
    flash_drivers/stm32f7_flash.cpp
    flash_drivers/stm32g0_flash.cpp
    flash_drivers/stm32g4_flash.cpp
    flash_drivers/stm32h5_flash.cpp
    flash_drivers/stm32h7_flash.cpp
    flash_drivers/stm32h7ab_flash.cpp
    flash_drivers/stm32l0_flash.cpp
    flash_drivers/stm32l1_flash.cpp
    flash_drivers/stm32l4_flash.cpp
    flash_drivers/stm32l5_flash.cpp
    flash_drivers/stm32u5_flash.cpp
    flash_drivers/stm32wb_flash.cpp
    flash_drivers/stm32wl_flash.cpp
    flash_drivers/nxp/mke_ftmrh.cpp
    flash_drivers/nxp/mke_ftfe.cpp
)
list(APPEND KT_SOURCES ${KT_FLASH_SOURCES})

# Transports (conditional)
if(KT_TRANSPORT_STLINK)
    list(APPEND KT_SOURCES transports/stlink_transport.cpp)
endif()
if(KT_TRANSPORT_BMP)
    list(APPEND KT_SOURCES transports/bmp_transport.cpp)
endif()
if(KT_TRANSPORT_JLINK)
    list(APPEND KT_SOURCES transports/jlink_transport.cpp)
endif()

# ── Static library ───────────────────────────────────────────────────────────

add_library(kt_swd_prog STATIC ${KT_SOURCES})

target_include_directories(kt_swd_prog
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/kt_swd_prog>
)

# Preprocessor defines for available transports
if(KT_TRANSPORT_STLINK)
    target_compile_definitions(kt_swd_prog PUBLIC KT_HAS_STLINK=1)
endif()
if(KT_TRANSPORT_BMP)
    target_compile_definitions(kt_swd_prog PUBLIC KT_HAS_BMP=1)
endif()
if(KT_TRANSPORT_JLINK)
    target_compile_definitions(kt_swd_prog PUBLIC KT_HAS_JLINK=1)
endif()

# ── Dependencies ─────────────────────────────────────────────────────────────

if(KT_TRANSPORT_STLINK)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
    target_link_libraries(kt_swd_prog PUBLIC PkgConfig::LIBUSB)
endif()

# J-Link uses runtime DLL loading (dlopen / LoadLibrary)
if(KT_TRANSPORT_JLINK AND NOT WIN32)
    target_link_libraries(kt_swd_prog PRIVATE ${CMAKE_DL_LIBS})
endif()

# ── CLI executable ───────────────────────────────────────────────────────────

if(KT_BUILD_CLI)
    add_executable(kt_swd_cli main.cpp)
    target_link_libraries(kt_swd_cli PRIVATE kt_swd_prog)
endif()

# ── Install ──────────────────────────────────────────────────────────────────

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Install the library
install(TARGETS kt_swd_prog
    EXPORT kt_swd_progTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Install headers preserving directory structure
set(KT_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/kt_swd_prog)

install(FILES
    stm32_programmer.h
    stm32_chipid.h
    stm32_factory.h
    DESTINATION ${KT_INSTALL_INCLUDEDIR}
)

install(FILES
    flash_drivers/flash_utils.h
    flash_drivers/stm32f0_flash.h
    flash_drivers/stm32f1_flash.h
    flash_drivers/stm32f2_flash.h
    flash_drivers/stm32f3_flash.h
    flash_drivers/stm32f4_flash.h
    flash_drivers/stm32f7_flash.h
    flash_drivers/stm32g0_flash.h
    flash_drivers/stm32g4_flash.h
    flash_drivers/stm32h5_flash.h
    flash_drivers/stm32h7_flash.h
    flash_drivers/stm32h7ab_flash.h
    flash_drivers/stm32l0_flash.h
    flash_drivers/stm32l1_flash.h
    flash_drivers/stm32l4_flash.h
    flash_drivers/stm32l5_flash.h
    flash_drivers/stm32u5_flash.h
    flash_drivers/stm32wb_flash.h
    flash_drivers/stm32wl_flash.h
    DESTINATION ${KT_INSTALL_INCLUDEDIR}/flash_drivers
)

install(FILES
    flash_drivers/nxp/mke_ftmrh.h
    flash_drivers/nxp/mke_ftfe.h
    DESTINATION ${KT_INSTALL_INCLUDEDIR}/flash_drivers/nxp
)

install(FILES
    transports/stlink_transport.h
    transports/bmp_transport.h
    transports/jlink_transport.h
    DESTINATION ${KT_INSTALL_INCLUDEDIR}/transports
)

# Install CLI
if(KT_BUILD_CLI)
    install(TARGETS kt_swd_cli DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ── CMake package config for find_package() ──────────────────────────────────

install(EXPORT kt_swd_progTargets
    FILE kt_swd_progTargets.cmake
    NAMESPACE kt_swd_prog::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kt_swd_prog
)

configure_package_config_file(
    cmake/kt_swd_progConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/kt_swd_progConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kt_swd_prog
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/kt_swd_progConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/kt_swd_progConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/kt_swd_progConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kt_swd_prog
)
