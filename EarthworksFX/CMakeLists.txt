cmake_minimum_required(VERSION 3.13)

project(EarthworksFX CXX)

# ============================================================================
# EarthworksFX - Diligent-side terrain integration.
#
# Layer:  OTSCommon <- Earthworks <- EarthworksFX <- DiligentEngine <- Game
#
# This is the ONLY layer that links Diligent targets. The GPU-agnostic core
# lives in the Earthworks submodule (a top-level sibling in this fork); the seam
# is earthworks/IGpuSinks.hpp.
# ============================================================================

# Earthworks is checked out as a top-level submodule of the fork (see
# .gitmodules). Pull it in if a parent project has not already done so.
if(NOT TARGET Earthworks)
    add_subdirectory(${CMAKE_SOURCE_DIR}/Earthworks ${CMAKE_BINARY_DIR}/Earthworks)
endif()

file(GLOB EFX_SHADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.hlsli")

add_library(EarthworksFX STATIC
    src/TerrainSystem.cpp
    src/TerrainInit.cpp
    src/TileBakePipeline.cpp
    src/TerrainDrawPass.cpp
    src/GpuSinksImpl.cpp
    ${EFX_SHADERS})

target_compile_features(EarthworksFX PUBLIC cxx_std_17)

target_include_directories(EarthworksFX
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/interface"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# GUARDRAIL: Diligent links live here and ONLY here.
target_link_libraries(EarthworksFX
    PUBLIC
        Earthworks
        Diligent-GraphicsEngine
        Diligent-GraphicsTools
        Diligent-Common
        Diligent-GraphicsAccessories)

set_common_target_properties(EarthworksFX)

# Shaders are runtime assets, not compiled by the build.
set_source_files_properties(${EFX_SHADERS} PROPERTIES VS_TOOL_OVERRIDE "None")
source_group("shaders" FILES ${EFX_SHADERS})
set_target_properties(EarthworksFX PROPERTIES FOLDER "EarthworksFX")

# Phase-1 sample (needs the DiligentSamples SampleBase framework).
if(TARGET Diligent-SampleBase)
    add_subdirectory(sample)
endif()
