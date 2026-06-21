@echo off
setlocal EnableExtensions

REM ============================================================
REM  DiligentEngine submodule setup - one-click win button
REM
REM  Safe to run any number of times (idempotent). Works on a
REM  fresh clone or an already-configured tree. No detached HEAD
REM  for the actively-developed repos (Earthworks + OTSCommon).
REM
REM  Third-party Diligent* submodules are intentionally left at
REM  their pinned commits so the build stays reproducible.
REM ============================================================

REM Always operate from the repo root (folder this script lives in)
cd /d "%~dp0"

set "EW_URL=https://github.com/OverthinkingStudios/Earthworks.git"
set "OTS_PATH=Earthworks/extern/OTSCommon"

echo.
echo === [1/5] Ensure Earthworks submodule is registered ===
git config --file .gitmodules --get submodule.Earthworks.url >nul 2>&1
if errorlevel 1 (
    echo Earthworks not registered - adding it...
    git submodule add --force "%EW_URL%" Earthworks
    if errorlevel 1 goto :fail
) else (
    echo Earthworks already registered - skipping add.
)

echo.
echo === [2/5] Configure branch tracking ^(for 'submodule update --remote'^) ===
git config -f .gitmodules submodule.Earthworks.branch main

echo.
echo === [3/5] Sync URLs + init/fetch submodules ===
git submodule sync --recursive
if errorlevel 1 goto :fail

REM Pinned third-party deps: checkout the SHA recorded in this repo.
git submodule update --init DiligentCore DiligentTools DiligentSamples DiligentFX
if errorlevel 1 goto :fail

REM Dev repos: never force the parent-recorded SHA when already cloned ^(would
REM wipe uncommitted work^). Fresh clones still use submodule update --init.
call :init_dev_repo Earthworks
call :init_otscommon

echo.
echo === [4/5] Put dev repos on 'main' ^(no detached HEAD^) ===
call :on_main Earthworks
call :on_main "%OTS_PATH%"

echo.
echo === [5/5] Pull latest on the dev repos ===
call :pull_main Earthworks
call :pull_main "%OTS_PATH%"

echo.
echo Done. Submodules initialized; Earthworks + OTSCommon are on main and up to date.
echo Diligent* dependencies remain pinned at their recorded commits.
echo.
pause
endlocal
exit /b 0

REM ------------------------------------------------------------
:init_dev_repo
REM %~1 = dev submodule path. Clone if missing; otherwise fetch only.
if not exist "%~1\.git" (
    echo   [init] %~1
    git submodule update --init "%~1"
    if errorlevel 1 exit /b 1
) else (
    echo   [fetch] %~1 ^(keeping current checkout^)
    git -C "%~1" fetch origin
    if errorlevel 1 exit /b 1
)
goto :eof

REM ------------------------------------------------------------
:init_otscommon
REM OTSCommon is registered in Earthworks, not in this repo.
if not exist "Earthworks\.git" goto :eof
if not exist "%OTS_PATH%\.git" (
    echo   [init] %OTS_PATH%
    git -C Earthworks submodule sync extern/OTSCommon
    if errorlevel 1 exit /b 1
    git -C Earthworks submodule update --init extern/OTSCommon
    if errorlevel 1 exit /b 1
) else (
    echo   [fetch] %OTS_PATH% ^(keeping current checkout^)
    git -C "%OTS_PATH%" fetch origin
    if errorlevel 1 exit /b 1
)
goto :eof

REM ------------------------------------------------------------
:on_main
REM %~1 = submodule path. Checkout main (DWIM creates local main
REM tracking origin/main if needed). Falls back to master.
if not exist "%~1\.git" (
    echo   [skip] %~1 not present
    goto :eof
)
git -C "%~1" checkout main 2>nul || git -C "%~1" checkout master 2>nul
if errorlevel 1 (
    echo   [warn] %~1: could not checkout main/master
) else (
    echo   [ok]   %~1 on branch
)
goto :eof

REM ------------------------------------------------------------
:pull_main
if not exist "%~1\.git" goto :eof
git -C "%~1" pull --ff-only
if errorlevel 1 echo   [warn] %~1: pull skipped ^(no upstream / not fast-forward^)
goto :eof

REM ------------------------------------------------------------
:fail
echo.
echo ERROR: a git command failed. See output above.
echo.
pause
endlocal
exit /b 1
