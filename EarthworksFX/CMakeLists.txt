cmake_minimum_required(VERSION 3.13)

project(EarthworksFX CXX)

option(EARTHWORKSFX_ENABLE_OPENJPH "Enable JP2 elevation decode via OpenJPH (through Earthworks)" ON)
option(EARTHWORKSFX_ENABLE_ASSIMP "Enable Assimp mesh IO for terrafector import (through Earthworks)" ON)

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

# legacy/ (an older renderer: its own src/core, src/compat, hlsl, Falcor.h) is
# deliberately EXCLUDED from the build - reference and salvage material only.
# The live tree is src/gpu (the ew:: native layer), src/core and hlsl/.
file(GLOB EFX_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")

file(GLOB EFX_GPU_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/*.h")

file(GLOB EFX_HLSL CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/*.hlsli"
    "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/**/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/**/*.hlsli"
    "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/**/*.inc")

set(EFX_APP_SOURCES
    src/app/EarthworksFXApplicationBase.cpp
    src/app/EarthworksFXWindowBase.cpp
    src/app/FirstPersonCamera.cpp
    src/app/TestFlightController.cpp
)

set(EFX_INPUT_SOURCES "")

set(EFX_APP_INCLUDES
    interface/EarthworksFXApplicationBase.hpp
    interface/EarthworksFXWindowBase.hpp
    interface/FirstPersonCamera.hpp
    interface/InputController.hpp
    interface/TestFlightController.hpp
)

if(PLATFORM_WIN32)
    list(APPEND EFX_INPUT_SOURCES src/input/Win32/InputControllerWin32.cpp)
    list(APPEND EFX_APP_INCLUDES interface/Win32/InputControllerWin32.hpp)
elseif(PLATFORM_LINUX)
    list(APPEND EFX_INPUT_SOURCES src/input/Linux/InputControllerLinux.cpp)
    list(APPEND EFX_APP_INCLUDES interface/Linux/InputControllerLinux.hpp)
endif()

add_library(EarthworksFX STATIC
    ${EFX_GPU_SOURCES}
    ${EFX_CORE_SOURCES}
    ${EFX_HLSL}
    ${EFX_APP_SOURCES}
    ${EFX_INPUT_SOURCES}
    ${EFX_APP_INCLUDES})

add_library(EarthworksFX::EarthworksFX ALIAS EarthworksFX)

target_link_libraries(EarthworksFX PUBLIC Earthworks::Earthworks)

target_compile_features(EarthworksFX PUBLIC cxx_std_17)

target_include_directories(EarthworksFX
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/interface"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core")

# Committed testflight scenario jsons (see TESTFLIGHTS.md). Dev-machine builds
# read/write them straight in the source tree so flights are versioned with the
# code; deployed builds fall back to <cwd>/testflights.
target_compile_definitions(EarthworksFX PRIVATE
    "EFX_TESTFLIGHTS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/testflights\"")

# Every glm clip-space helper the core calls (glm::unProject for mouse picking,
# glm::orthoLH/orthoRH for the bake projections) has to be compiled for the
# D3D/Vulkan [0,1] depth range. Without this define glm defaults to GL's
# [-1,1] and those helpers silently change meaning. PUBLIC so that app targets
# compile glm identically - a mismatch here is an ODR violation.
target_compile_definitions(EarthworksFX PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)

if(NOT EARTHWORKS_WITH_OPENJPH OR NOT EARTHWORKS_WITH_ASSIMP)
    message(FATAL_ERROR
        "EarthworksFX requires OpenJPH and Assimp from Earthworks "
        "(EARTHWORKSFX_ENABLE_OPENJPH / EARTHWORKSFX_ENABLE_ASSIMP).")
endif()

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

if(PLATFORM_WIN32 OR PLATFORM_LINUX)
    if(NOT TARGET Diligent-NativeAppBase)
        if(NOT TARGET Diligent-TextureLoader)
            add_subdirectory(
                ${CMAKE_SOURCE_DIR}/DiligentTools/ThirdParty
                ${CMAKE_BINARY_DIR}/EarthworksFX/DiligentTools/ThirdParty)
            add_subdirectory(
                ${CMAKE_SOURCE_DIR}/DiligentTools/TextureLoader
                ${CMAKE_BINARY_DIR}/EarthworksFX/DiligentTools/TextureLoader)
        endif()
        add_subdirectory(
            ${CMAKE_SOURCE_DIR}/DiligentTools/NativeApp
            ${CMAKE_BINARY_DIR}/EarthworksFX/DiligentTools/NativeApp)
    endif()

    get_supported_backends(ENGINE_LIBRARIES)

    target_link_libraries(EarthworksFX
    PUBLIC
        Diligent-NativeAppBase
        Diligent-TargetPlatform
        Diligent-TextureLoader
        ${ENGINE_LIBRARIES})

    if(PLATFORM_LINUX)
        find_package(X11 REQUIRED)
        find_package(OpenGL REQUIRED)
        target_link_libraries(EarthworksFX PUBLIC XCBKeySyms OpenGL::GL OpenGL::GLX X11::X11)
    endif()
endif()

set_common_target_properties(EarthworksFX)

if(MSVC)
    target_compile_options(EarthworksFX PRIVATE /bigobj /FS /wd4201)
    # _CRT_SECURE_NO_WARNINGS: the core reads the terrain streaming manifests
    # with fopen/fscanf/fread (terrain.cpp) - silence C4996 rather than rewrite
    # the hot IO paths.
    target_compile_definitions(EarthworksFX PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

set_source_files_properties(${EFX_HLSL} PROPERTIES VS_TOOL_OVERRIDE "None")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/hlsl" PREFIX "hlsl" FILES ${EFX_HLSL})
source_group("src/core" FILES ${EFX_CORE_SOURCES})
source_group("src/gpu" FILES ${EFX_GPU_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src/app" PREFIX "src/app" FILES
    src/app/EarthworksFXApplicationBase.cpp
    src/app/EarthworksFXWindowBase.cpp
    src/app/FirstPersonCamera.cpp
    src/app/TestFlightController.cpp)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src/input" PREFIX "src/input" FILES ${EFX_INPUT_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/interface" PREFIX "interface" FILES ${EFX_APP_INCLUDES})
set_target_properties(EarthworksFX PROPERTIES FOLDER "EarthworksFX")

add_custom_command(TARGET EarthworksFX POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/hlsl"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployHLSL.cmake"
    COMMENT "Deploying EarthworksFX HLSL to ACSMP_GAMEROOT/hlsl"
    VERBATIM)

add_custom_target(EarthworksFX_DeployHLSL
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/hlsl"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployHLSL.cmake"
    DEPENDS EarthworksFX
    COMMENT "Deploying EarthworksFX HLSL to ACSMP_GAMEROOT/hlsl"
    VERBATIM)
set_target_properties(EarthworksFX_DeployHLSL PROPERTIES FOLDER "EarthworksFX")

# Wire an executable up as an EarthworksFX app. Links the library and applies the
# per-exe Win32 bits that can't live on the static library itself (post-build dll
# copy, debugger working dir), so each app's CMakeLists stays a couple of lines.
function(earthworksfx_setup_app TARGET_NAME)
    target_link_libraries(${TARGET_NAME} PRIVATE EarthworksFX::EarthworksFX)
    set_common_target_properties(${TARGET_NAME})
    set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "EarthworksFX")

    if(PLATFORM_WIN32)
        # SM6 Earthworks shaders need DXC; without these dlls next to the exe,
        # Diligent silently falls back to FXC (D3D12) / glslang (Vulkan).
        copy_required_dlls(${TARGET_NAME} DXC_REQUIRED YES)
    endif()

    if(DEFINED ENV{ACSMP_GAMEROOT} AND NOT "$ENV{ACSMP_GAMEROOT}" STREQUAL "")
        set_target_properties(${TARGET_NAME} PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$ENV{ACSMP_GAMEROOT}")
    endif()
endfunction()
