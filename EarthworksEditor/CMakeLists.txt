cmake_minimum_required(VERSION 3.13)

project(EarthworksEditor CXX)

# Collect the sources to compile.
#   GLOB             - non-recursive match in this dir; use GLOB_RECURSE to also
#                      descend into subfolders (e.g. "src/**/*.cpp").
#   EDITOR_SRC       - output variable that receives the matched file list.
#   CONFIGURE_DEPENDS - re-run the glob at build time so newly added/removed
#                      files are picked up without manually re-running CMake.
#                      (Drop it if you prefer regenerating CMake explicitly.)
# The remaining args are the match patterns, relative to this CMakeLists.
file(GLOB EDITOR_SRC CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp")

# Prefer an explicit whitelist over globbing when you want full control over the
# build (and to avoid surprise files sneaking in). Then drop the file() above and
# pass EDITOR_SRC straight to add_target_platform_app():
#   set(EDITOR_SRC
#       src/EarthworksEditor.cpp
#       src/EarthworksEditor.hpp)

# add_target_platform_app(TARGET_NAME SOURCE INCLUDE ASSETS)
#   3rd arg INCLUDE - extra header files to show in the IDE project; we keep our
#                     headers in SOURCE above, so this is empty.
#   4th arg ASSETS  - data files to bundle with the app (icons, plists, deployed
#                     content on UWP/Apple); the editor ships none, so empty.
add_target_platform_app(EarthworksEditor "${EDITOR_SRC}" "" "")
earthworksfx_setup_app(EarthworksEditor)
