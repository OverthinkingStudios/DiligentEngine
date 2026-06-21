cmake_minimum_required(VERSION 3.13)

project(EarthworksFX CXX)

option(EARTHWORKSFX_ENABLE_OPENJPH "Enable JP2 elevation decode via OpenJPH (through Earthworks)" ON)
option(EARTHWORKSFX_ENABLE_ASSIMP "Enable Assimp mesh IO for terrafector import (through Earthworks)" ON)

# OpenJPH and Assimp are resolved inside Earthworks (extern/, find_package, or FetchContent).
# EarthworksFX must not duplicate that logic with find_package alone.
if(EARTHWORKSFX_ENABLE_OPENJPH)
    set(EARTHWORKS_WITH_OPENJPH ON CACHE BOOL "Enable OpenJPH via Earthworks" FORCE)
endif()
if(EARTHWORKSFX_ENABLE_ASSIMP)
    set(EARTHWORKS_WITH_ASSIMP ON CACHE BOOL "Enable Assimp via Earthworks" FORCE)
endif()

if(NOT TARGET Earthworks::Earthworks)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../Earthworks
        ${CMAKE_BINARY_DIR}/Earthworks)
endif()

file(GLOB EFX_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")

file(GLOB EFX_HLSL CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/*.hlsli"
    "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/**/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/**/*.hlsli"
    "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/**/*.inc")

add_library(EarthworksFX STATIC
    src/compat/FalcorCompat.cpp
    interface/FalcorCompat.hpp
    ${EFX_CORE_SOURCES}
    ${EFX_HLSL})

add_library(EarthworksFX::EarthworksFX ALIAS EarthworksFX)

# PUBLIC: inherit OpenJPH / Assimp + compile defs from Earthworks when enabled.
target_link_libraries(EarthworksFX PUBLIC Earthworks::Earthworks)

target_compile_features(EarthworksFX PUBLIC cxx_std_17)

target_include_directories(EarthworksFX
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/interface"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core")

if(NOT EARTHWORKS_WITH_OPENJPH OR NOT EARTHWORKS_WITH_ASSIMP)
    message(FATAL_ERROR
        "EarthworksFX requires OpenJPH and Assimp from Earthworks "
        "(EARTHWORKSFX_ENABLE_OPENJPH / EARTHWORKSFX_ENABLE_ASSIMP).")
endif()

# GUARDRAIL: Diligent links live here and ONLY here.
target_link_libraries(EarthworksFX
    PUBLIC
        Diligent-GraphicsEngine
        Diligent-GraphicsTools
        Diligent-Common
        Diligent-GraphicsAccessories)

if(NOT TARGET Diligent-Imgui)
    if(NOT DILIGENT_TOOLS_FOUND)
        set(DILIGENT_TOOLS_FOUND TRUE CACHE INTERNAL "DiligentTools module is found")
    endif()
    if(NOT TARGET Diligent-TextureLoader)
        add_subdirectory(
            ${CMAKE_SOURCE_DIR}/DiligentTools/ThirdParty
            ${CMAKE_BINARY_DIR}/EarthworksFX/DiligentTools/ThirdParty)
        add_subdirectory(
            ${CMAKE_SOURCE_DIR}/DiligentTools/TextureLoader
            ${CMAKE_BINARY_DIR}/EarthworksFX/DiligentTools/TextureLoader)
    endif()
    add_subdirectory(
        ${CMAKE_SOURCE_DIR}/DiligentTools/Imgui
        ${CMAKE_BINARY_DIR}/EarthworksFX/DiligentTools/Imgui)
endif()
target_link_libraries(EarthworksFX PUBLIC Diligent-Imgui)

set_common_target_properties(EarthworksFX)

if(MSVC)
    target_compile_options(EarthworksFX PRIVATE /bigobj /FS)
    target_compile_definitions(EarthworksFX PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Shaders and HLSL-included struct headers are runtime / IDE assets, not compiled by CMake.
set_source_files_properties(${EFX_HLSL} PROPERTIES VS_TOOL_OVERRIDE "None")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/hlsl" PREFIX "hlsl" FILES ${EFX_HLSL})
source_group("src/core" FILES ${EFX_CORE_SOURCES})
source_group("src/compat" FILES src/compat/FalcorCompat.cpp interface/FalcorCompat.hpp)
set_target_properties(EarthworksFX PROPERTIES FOLDER "EarthworksFX")

# HLSL source of truth: EarthworksFX/hlsl/
# Deploy to $ACSMP_GAMEROOT/hlsl at build time so runtime paths like hlsl/terrain/*.hlsl resolve.
# Uses a cmake script (not $ENV{...} baked at configure time) so VS builds pick up the env var
# that is set when MSBuild runs, and skip cleanly when ACSMP_GAMEROOT is unset.
add_custom_command(TARGET EarthworksFX POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/hlsl"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployHLSL.cmake"
    COMMENT "Deploying EarthworksFX HLSL to ACSMP_GAMEROOT/hlsl"
    VERBATIM)
# Re-run HLSL deploy whenever the sample exe is built, even if the static lib is up-to-date.
add_custom_target(EarthworksFX_DeployHLSL
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/hlsl"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployHLSL.cmake"
    DEPENDS EarthworksFX
    COMMENT "Deploying EarthworksFX HLSL to ACSMP_GAMEROOT/hlsl"
    VERBATIM)
set_target_properties(EarthworksFX_DeployHLSL PROPERTIES FOLDER "EarthworksFX")

