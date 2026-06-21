# Deploy EarthworksFX/hlsl to $ACSMP_GAMEROOT/hlsl at build time (not configure time).
# Invoked from EarthworksFX POST_BUILD via: cmake -DSOURCE_DIR=... -P DeployHLSL.cmake

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "DeployHLSL.cmake: SOURCE_DIR is not set")
endif()

if(NOT DEFINED ENV{ACSMP_GAMEROOT} OR "$ENV{ACSMP_GAMEROOT}" STREQUAL "")
    message(STATUS "EarthworksFX: ACSMP_GAMEROOT is not set; skipping HLSL deploy")
    return()
endif()

set(_dst "$ENV{ACSMP_GAMEROOT}/hlsl")
file(MAKE_DIRECTORY "${_dst}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SOURCE_DIR}" "${_dst}"
    RESULT_VARIABLE _result
)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "EarthworksFX: failed to copy HLSL from ${SOURCE_DIR} to ${_dst}")
endif()

message(STATUS "EarthworksFX: deployed HLSL to ${_dst}")
