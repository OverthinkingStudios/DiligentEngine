@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ============================================================
REM  DiligentEngine full reconfigure + rebuild - one-click button
REM
REM  RECONFIGURES CMake from scratch (deletes the build dir so a
REM  stale CMakeCache can never leak in) and then builds EVERY
REM  target.
REM
REM  Uses the SAME layout as a hand-rolled Visual Studio 2022
REM  build and as VS Code's CMake Tools (.vscode/settings.json):
REM    solution   = build\Win64\DiligentEngine.sln
REM    build dir  = build\Win64           (multi-config)
REM    exe        = build\Win64\<project>\<config>\<name>.exe
REM
REM  Usage:
REM    rebuild-all.bat [config] [generator]
REM
REM    config     - Debug | Release | RelWithDebInfo | MinSizeRel
REM                 (default: RelWithDebInfo)
REM    generator  - optional CMake generator override
REM                 (default: "Visual Studio 17 2022")
REM
REM  Options match .vscode/settings.json:
REM    DILIGENT_BUILD_TESTS=TRUE
REM    DILIGENT_NO_FORMAT_VALIDATION=FALSE
REM    CMAKE_INSTALL_PREFIX=install
REM ============================================================

REM Always operate from the repo root (folder this script lives in)
cd /d "%~dp0"

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=RelWithDebInfo"

set "GENERATOR=%~2"
if "%GENERATOR%"=="" set "GENERATOR=Visual Studio 17 2022"

set "BUILD_DIR=build\Win64"

echo.
echo === DiligentEngine rebuild-all ===
echo   config    : %CONFIG%
echo   generator : %GENERATOR%
echo   build dir : %BUILD_DIR%
echo.

REM --- [1/3] Wipe the existing build tree for a true reconfigure --------------
echo === [1/3] Removing previous build dir (forcing a fresh configure) ===
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
    if errorlevel 1 goto :fail
)
mkdir "%BUILD_DIR%"
if errorlevel 1 goto :fail

REM --- [2/3] Configure --------------------------------------------------------
echo.
echo === [2/3] Configuring CMake ===
cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -A x64 ^
    -DDILIGENT_BUILD_TESTS=TRUE ^
    -DDILIGENT_NO_FORMAT_VALIDATION=FALSE ^
    -DCMAKE_INSTALL_PREFIX=install
if errorlevel 1 goto :fail

REM --- [3/3] Build everything -------------------------------------------------
echo.
echo === [3/3] Building all targets (%CONFIG%) ===
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 goto :fail

echo.
echo Done. %CONFIG% build complete in "%BUILD_DIR%".
echo.
endlocal
exit /b 0

:fail
echo.
echo ERROR: a command failed. See output above.
echo.
endlocal
exit /b 1
