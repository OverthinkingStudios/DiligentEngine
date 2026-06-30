cmake_minimum_required(VERSION 3.13)

project(EarthworksFXSample CXX)

if(NOT TARGET EarthworksFX::EarthworksFX)
    message(FATAL_ERROR "EarthworksFXSample requires the EarthworksFX library target")
endif()

set(SAMPLE_SOURCES src/EarthworksFXSample.cpp)
set(SAMPLE_INCLUDES src/EarthworksFXSample.hpp)

add_target_platform_app(EarthworksFXSample "${SAMPLE_SOURCES}" "${SAMPLE_INCLUDES}" "")

if(PLATFORM_WIN32)
    # DXC_REQUIRED copies dxcompiler.dll + dxil.dll (D3D12) and spv_dxcompiler.dll
    # (Vulkan) next to the exe. The Earthworks shaders are SM6 and the compat
    # layer requests SHADER_COMPILER_DXC; without these dlls Diligent silently
    # falls back to FXC (SM 5.1 cap) on D3D12 and glslang on Vulkan (which
    # rejects the HLSL constant-buffer layout, e.g. FogAtmosphericParams).
    copy_required_dlls(EarthworksFXSample DXC_REQUIRED YES)
endif()

target_include_directories(EarthworksFXSample PRIVATE src)
target_link_libraries(EarthworksFXSample PRIVATE EarthworksFX::EarthworksFX)
set_common_target_properties(EarthworksFXSample)
set_target_properties(EarthworksFXSample PROPERTIES FOLDER EarthworksFX)

add_dependencies(EarthworksFXSample EarthworksFX_DeployHLSL)

if(DEFINED ENV{ACSMP_GAMEROOT} AND NOT "$ENV{ACSMP_GAMEROOT}" STREQUAL "")
    set_target_properties(EarthworksFXSample PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$ENV{ACSMP_GAMEROOT}")
endif()
