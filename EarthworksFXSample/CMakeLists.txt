cmake_minimum_required(VERSION 3.13)

project(EarthworksFXSample CXX)

if(NOT TARGET EarthworksFX)
    message(FATAL_ERROR "EarthworksFXSample requires the EarthworksFX library target")
endif()

# SampleBase bootstrap when DiligentSamples is not built.
if(NOT TARGET Diligent-SampleBase)
    if(NOT TARGET Diligent-NativeAppBase)
        if(NOT DILIGENT_TOOLS_FOUND)
            set(DILIGENT_TOOLS_FOUND TRUE CACHE INTERNAL "DiligentTools module is found")
        endif()
        if(NOT TARGET Diligent-TextureLoader)
            add_subdirectory(
                ${CMAKE_SOURCE_DIR}/DiligentTools/ThirdParty
                ${CMAKE_BINARY_DIR}/EarthworksFXSample/DiligentTools/ThirdParty)
            add_subdirectory(
                ${CMAKE_SOURCE_DIR}/DiligentTools/TextureLoader
                ${CMAKE_BINARY_DIR}/EarthworksFXSample/DiligentTools/TextureLoader)
            add_subdirectory(
                ${CMAKE_SOURCE_DIR}/DiligentTools/Imgui
                ${CMAKE_BINARY_DIR}/EarthworksFXSample/DiligentTools/Imgui)
        endif()
        add_subdirectory(
            ${CMAKE_SOURCE_DIR}/DiligentTools/NativeApp
            ${CMAKE_BINARY_DIR}/EarthworksFXSample/DiligentTools/NativeApp)
    endif()
    add_subdirectory(
        ${CMAKE_SOURCE_DIR}/DiligentSamples/SampleBase
        ${CMAKE_BINARY_DIR}/EarthworksFXSample/SampleBase)
endif()

set(SAMPLE_SOURCES src/EarthworksFXSample.cpp)
set(SAMPLE_INCLUDES src/EarthworksFXSample.hpp)

if(COMMAND add_sample_app)
    add_sample_app(EarthworksFXSample
        IDE_FOLDER EarthworksFX
        SOURCES ${SAMPLE_SOURCES}
        INCLUDES ${SAMPLE_INCLUDES}
    )
else()
    add_target_platform_app(EarthworksFXSample "${SAMPLE_SOURCES}" "${SAMPLE_INCLUDES}" "")

    if(PLATFORM_WIN32)
        copy_required_dlls(EarthworksFXSample)
        append_sample_base_win32_source(EarthworksFXSample)
    elseif(PLATFORM_UNIVERSAL_WINDOWS)
        append_sample_base_uwp_source(EarthworksFXSample)
        package_required_dlls(EarthworksFXSample)
    endif()

    target_include_directories(EarthworksFXSample PRIVATE src)

    target_link_libraries(EarthworksFXSample
    PRIVATE
        Diligent-NativeAppBase
        Diligent-BuildSettings
        Diligent-SampleBase)

    set_common_target_properties(EarthworksFXSample)

    if(MSVC)
        target_compile_options(EarthworksFXSample PRIVATE /wd4201)
    endif()

    set_target_properties(EarthworksFXSample PROPERTIES FOLDER EarthworksFX)
endif()

target_link_libraries(EarthworksFXSample PRIVATE EarthworksFX::EarthworksFX)
add_dependencies(EarthworksFXSample EarthworksFX_DeployHLSL)

if(MSVC)
    target_compile_definitions(EarthworksFXSample PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(DEFINED ENV{ACSMP_GAMEROOT} AND NOT "$ENV{ACSMP_GAMEROOT}" STREQUAL "")
    set_target_properties(EarthworksFXSample PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$ENV{ACSMP_GAMEROOT}")
endif()
