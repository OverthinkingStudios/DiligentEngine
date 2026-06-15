cmake_minimum_required(VERSION 3.13)

project(EarthworksSample CXX)

set(SOURCE
    src/EarthworksSample.cpp
)
set(INCLUDE
    src/EarthworksSample.hpp
)

add_sample_app(EarthworksSample
    IDE_FOLDER  EarthworksFX
    SOURCES     ${SOURCE}
    INCLUDES    ${INCLUDE}
)

target_link_libraries(EarthworksSample PRIVATE EarthworksFX)

message(STATUS "GAMEROOT=$ENV{ACSMP_GAMEROOT}")

# The terrain HLSL lives in EarthworksFX/shaders (single source of truth). Copy
# it next to the executable and run from there so the shader source stream
# factory ("shaders") resolves at runtime.
add_custom_command(TARGET EarthworksSample POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../shaders"
        "$ENV{ACSMP_GAMEROOT}/shaders")

set_target_properties(EarthworksSample PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$ENV{ACSMP_GAMEROOT}" )
    
    #"$<TARGET_FILE_DIR:EarthworksSample>"