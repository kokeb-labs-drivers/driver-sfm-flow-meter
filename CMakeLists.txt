#===============================================================================
# SFM_FLOW_METER driver: root CMake
#===============================================================================
# One file, two build systems:
#
# - ESP-IDF (ESP_PLATFORM): this folder is an ESP-IDF component. Point
#   EXTRA_COMPONENT_DIRS at it, check it out under components/, or add it as a
#   component-manager git dependency. menuconfig reads _config/Kconfig.
# - Any other CMake build (host, STM32, RP2040, nRF, ...): one INTERFACE target,
#   sfm_flow_meter, consumed as sfm_flow_meter::sfm_flow_meter both through add_subdirectory() and
#   through find_package(sfm_flow_meter) after install.
#
#   cmake -S . -B build -DSFM_FLOW_METER_BUILD_TESTS=ON
#   cmake --build build && ctest --test-dir build --output-on-failure
#===============================================================================
cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/sfm_flow_meter_build_settings.cmake)

#===============================================================================
# ESP-IDF component
#===============================================================================
if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${SFM_FLOW_METER_SOURCE_FILES}
        INCLUDE_DIRS ${SFM_FLOW_METER_PUBLIC_INCLUDE_DIRS}
        REQUIRES ${SFM_FLOW_METER_IDF_REQUIRES}
        KCONFIG ${CMAKE_CURRENT_LIST_DIR}/_config/Kconfig
    )
    if(SFM_FLOW_METER_SOURCE_FILES)
        target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
    else()
        target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_20)
    endif()
    return()
endif()

project(sfm_flow_meter_driver
    VERSION ${SFM_FLOW_METER_VERSION}
    DESCRIPTION "C++20 driver for the Sensirion SFM family of I2C mass flow meters (SFM3000 to SFM6000D)."
    LANGUAGES CXX)

include(GNUInstallDirs)

# ISO C++20 for every target this project builds (tests, fuzzers, examples), never the GNU dialect
# that CMake picks by default (handbook KB-CPP-001, KB-CPP20-001). CXX_EXTENSIONS does not travel
# through an INTERFACE target, so each folder that builds a target sets it as well.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The installed layout keeps the source tree's inc/ + src/ pair, because sfm_flow_meter.hpp includes
# "../src/sfm_flow_meter.ipp" relative to itself:
#   <prefix>/include/sfm_flow_meter/inc/*.hpp, sfm_flow_meter_version.h
#   <prefix>/include/sfm_flow_meter/src/sfm_flow_meter.ipp
set(SFM_FLOW_METER_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/sfm_flow_meter/inc")

#===============================================================================
# Target
#===============================================================================
add_library(${SFM_FLOW_METER_TARGET_NAME} INTERFACE)
target_include_directories(${SFM_FLOW_METER_TARGET_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/inc>
        $<BUILD_INTERFACE:${SFM_FLOW_METER_VERSION_HEADER_DIR}>
        $<INSTALL_INTERFACE:${SFM_FLOW_METER_INSTALL_INCLUDEDIR}>
)
target_compile_features(${SFM_FLOW_METER_TARGET_NAME} INTERFACE cxx_std_20)

# sfm_flow_meter::sfm_flow_meter everywhere: the alias for add_subdirectory(), EXPORT_NAME for the installed package.
add_library(sfm_flow_meter::sfm_flow_meter ALIAS ${SFM_FLOW_METER_TARGET_NAME})
set_target_properties(${SFM_FLOW_METER_TARGET_NAME} PROPERTIES EXPORT_NAME sfm_flow_meter)

option(SFM_FLOW_METER_ENABLE_WARNINGS "Add -Wall -Wextra -Wpedantic to every target that links sfm_flow_meter::sfm_flow_meter" OFF)
if(SFM_FLOW_METER_ENABLE_WARNINGS)
    target_compile_options(${SFM_FLOW_METER_TARGET_NAME} INTERFACE -Wall -Wextra -Wpedantic)
endif()

#===============================================================================
# Host tests, documentation snippets, install check (tests/)
#===============================================================================
option(SFM_FLOW_METER_BUILD_TESTS "Build the host tests in tests/" OFF)
option(SFM_FLOW_METER_BUILD_FUZZERS "Also build the libFuzzer targets in tests/fuzz/ (Clang only)" OFF)
if(SFM_FLOW_METER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

#===============================================================================
# Install and package export
#===============================================================================
include(CMakePackageConfigHelpers)

install(TARGETS ${SFM_FLOW_METER_TARGET_NAME} EXPORT sfm_flow_meterTargets
    INCLUDES DESTINATION ${SFM_FLOW_METER_INSTALL_INCLUDEDIR})

install(DIRECTORY inc/ DESTINATION ${SFM_FLOW_METER_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(FILES "${SFM_FLOW_METER_VERSION_HEADER}" DESTINATION ${SFM_FLOW_METER_INSTALL_INCLUDEDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sfm_flow_meter/src
    FILES_MATCHING PATTERN "*.ipp")

install(EXPORT sfm_flow_meterTargets
    FILE sfm_flow_meterTargets.cmake
    NAMESPACE sfm_flow_meter::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sfm_flow_meter)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/sfm_flow_meterConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/sfm_flow_meterConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sfm_flow_meter)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/sfm_flow_meterConfigVersion.cmake"
    VERSION ${SFM_FLOW_METER_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/sfm_flow_meterConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/sfm_flow_meterConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sfm_flow_meter)
