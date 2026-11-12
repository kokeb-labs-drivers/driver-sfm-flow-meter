#===============================================================================
# SFM_FLOW_METER driver: build settings
#===============================================================================
# Read by the root CMakeLists.txt in both of its modes (plain CMake and the
# ESP-IDF component): target name, version, include directories, sources and
# ESP-IDF requirements. This file is the single source
# of the version; tests/CMakeLists.txt checks every other copy against it.
#===============================================================================

include_guard(GLOBAL)

set(SFM_FLOW_METER_TARGET_NAME "sfm_flow_meter")

#===============================================================================
# Version (single source of truth)
#===============================================================================
set(SFM_FLOW_METER_VERSION_MAJOR 0)
set(SFM_FLOW_METER_VERSION_MINOR 1)
set(SFM_FLOW_METER_VERSION_PATCH 0)
set(SFM_FLOW_METER_VERSION "${SFM_FLOW_METER_VERSION_MAJOR}.${SFM_FLOW_METER_VERSION_MINOR}.${SFM_FLOW_METER_VERSION_PATCH}")

#===============================================================================
# Generated version header (build tree, never the source tree)
#===============================================================================
set(SFM_FLOW_METER_VERSION_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/../inc/sfm_flow_meter_version.h.in")
set(SFM_FLOW_METER_VERSION_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/sfm_flow_meter_generated")
set(SFM_FLOW_METER_VERSION_HEADER "${SFM_FLOW_METER_VERSION_HEADER_DIR}/sfm_flow_meter_version.h")

# The directory must exist before ESP-IDF's idf_component_register() checks the include paths.
file(MAKE_DIRECTORY "${SFM_FLOW_METER_VERSION_HEADER_DIR}")
configure_file("${SFM_FLOW_METER_VERSION_TEMPLATE}" "${SFM_FLOW_METER_VERSION_HEADER}" @ONLY)

#===============================================================================
# Public include directories, sources, ESP-IDF requirements
#===============================================================================
set(SFM_FLOW_METER_PUBLIC_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../inc"
    "${SFM_FLOW_METER_VERSION_HEADER_DIR}"
)

# Header-only: the template implementation is src/sfm_flow_meter.ipp, included by inc/sfm_flow_meter.hpp.
set(SFM_FLOW_METER_SOURCE_FILES)

set(SFM_FLOW_METER_IDF_REQUIRES)
