cmake_minimum_required(VERSION 3.13)

project(TextureSplitTool CXX)

file(GLOB TOOL_SRC CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp")

add_target_platform_app(TextureSplitTool "${TOOL_SRC}" "" "")
earthworksfx_setup_app(TextureSplitTool)
