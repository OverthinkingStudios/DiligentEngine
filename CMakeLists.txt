cmake_minimum_required (VERSION 3.10)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Generate XCode schema files
set(CMAKE_XCODE_GENERATE_SCHEME TRUE)
# Make malloc write 0xAA to newly allocated memory and 0x55 to deallocated memory
set(CMAKE_XCODE_SCHEME_MALLOC_SCRIBBLE YES)
# Place guard pages on each side of large (4096 bytes or more) buffers
set(CMAKE_XCODE_SCHEME_MALLOC_GUARD_EDGES YES)

project(DiligentEngine)

# --- Shared Dear ImGui (additions-only fork change) -------------------------
# DiligentTools, DiligentFX and the EarthworksFX/Earthworks code must all be
# built against ONE copy of Dear ImGui, otherwise each translation unit set
# links a different GImGui context: the backend (ImGuiImplDiligent/Win32) ends
# up rendering/forwarding input to a context that the Earthworks UI code never
# writes to, so the UI is invisible and ImGui::IsMouseDown() (used by the
# terrain camera) always reads false. Point DILIGENT_DEAR_IMGUI_PATH at the
# OTSCommon copy so the engine compiles the same headers/sources the rest of
# the code includes. Must be set BEFORE add_subdirectory(DiligentTools), where
# the default (DiligentTools/ThirdParty/imgui) is established.
set(EARTHWORKS_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Earthworks/extern/OTSCommon/extern/imgui")
if(EXISTS "${EARTHWORKS_IMGUI_DIR}/imgui.h")
    set(DILIGENT_DEAR_IMGUI_PATH "${EARTHWORKS_IMGUI_DIR}"
        CACHE PATH "Shared Dear ImGui source directory (OTSCommon)" FORCE)
    message(STATUS "EarthworksFX: using shared Dear ImGui at ${DILIGENT_DEAR_IMGUI_PATH}")
else()
    message(WARNING "EarthworksFX: OTSCommon Dear ImGui not found at ${EARTHWORKS_IMGUI_DIR}; "
                    "falling back to DiligentTools' bundled imgui (UI may be invisible due to a split ImGui context).")
endif()

option(DILIGENT_BUILD_TOOLS "Build DiligentTools module" ON)
option(DILIGENT_BUILD_FX "Build DiligentFX module" ON)
option(DILIGENT_BUILD_SAMPLES "Build DiligentSamples module" ON)
option(DILIGENT_BUILD_DOCS "Build documentation" OFF)

add_subdirectory(DiligentCore)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/DiligentCorePro")
    add_subdirectory(DiligentCorePro)
endif()

if(${DILIGENT_BUILD_TOOLS})
    add_subdirectory(DiligentTools)
else()
    if(${DILIGENT_BUILD_FX})
        message("Disabling DiligentFX module as it requires DiligentTools module to be enabled")
        set(DILIGENT_BUILD_FX OFF CACHE BOOL "Build DiligentFX module" FORCE)
    endif()

    if (${DILIGENT_BUILD_SAMPLES})
        message("Disabling DiligentSamples module as it requires DiligentTools module to be enabled")
        set(DILIGENT_BUILD_SAMPLES OFF CACHE BOOL "Build DiligentSamples module" FORCE)
    endif()

    if (${DILIGENT_BUILD_UNITY_PLUGIN})
        message("Disabling unity plugin as it requires DiligentTools module to be enabled")
        set(DILIGENT_BUILD_UNITY_PLUGIN OFF CACHE BOOL "Build Unity plugin" FORCE)
    endif()
endif()

if(${DILIGENT_BUILD_FX})
    add_subdirectory(DiligentFX)
endif()

if(${DILIGENT_BUILD_SAMPLES})
    add_subdirectory(DiligentSamples)
endif()

if(${DILIGENT_BUILD_DOCS})
    add_subdirectory(Doc)
endif()

if(TARGET GLTFViewer)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT GLTFViewer)
elseif(TARGET Asteroids)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Asteroids)
elseif(TARGET Atmosphere)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Atmosphere)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/DiligentCommunity")
    add_subdirectory(DiligentCommunity)
endif()

# --- Earthworks terrain port (additions-only fork change) -------------------
option(DILIGENT_BUILD_EARTHWORKSFX "Build EarthworksFX terrain integration" ON)
if(DILIGENT_BUILD_EARTHWORKSFX AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/EarthworksFX/CMakeLists.txt")
    add_subdirectory(EarthworksFX)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/EarthworksFXSample/CMakeLists.txt")
        add_subdirectory(EarthworksFXSample)
    endif()
endif()

if(TARGET EarthworksFXSample)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT EarthworksFXSample)
endif()
