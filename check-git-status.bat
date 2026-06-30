@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM  check-git-status.bat
REM  Reports checkout state of the three nested repositories:
REM    1) DiligentEngine (root)
REM    2) Earthworks (submodule)
REM    3) Earthworks/extern/OTSCommon (submodule)
REM  Highlights detached HEADs in RED as an issue.
REM ============================================================

REM --- Enable ANSI escape sequences (VT) for coloured output ---
for /f %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"
set "C_RED=%ESC%[91m"
set "C_GREEN=%ESC%[92m"
set "C_YELLOW=%ESC%[93m"
set "C_CYAN=%ESC%[96m"
set "C_BOLD=%ESC%[1m"
set "C_RESET=%ESC%[0m"

set "ROOT=%~dp0"
set "LOG=%ROOT%check-git-status.log"

REM --- Fresh log file ---
> "%LOG%" echo ============================================================
>>"%LOG%" echo  Git status report  -  %DATE% %TIME%
>>"%LOG%" echo ============================================================

echo %C_BOLD%============================================================%C_RESET%
echo %C_BOLD% Git status report  -  %DATE% %TIME%%C_RESET%
echo %C_BOLD%============================================================%C_RESET%

set "ISSUES=0"

call :checkRepo "DiligentEngine (root)"        "%ROOT%."
call :checkRepo "Earthworks (submodule)"       "%ROOT%Earthworks"
call :checkRepo "OTSCommon (submodule)"        "%ROOT%Earthworks\extern\OTSCommon"

echo.
echo ------------------------------------------------------------
>>"%LOG%" echo ------------------------------------------------------------
if "%ISSUES%"=="0" (
    echo %C_GREEN%RESULT: All repositories are on a named branch. No detached HEADs.%C_RESET%
    >>"%LOG%" echo RESULT: All repositories are on a named branch. No detached HEADs.
) else (
    echo %C_RED%RESULT: %ISSUES% repository^(ies^) in a DETACHED HEAD state ^(see above^).%C_RESET%
    >>"%LOG%" echo RESULT: %ISSUES% repository^(ies^) in a DETACHED HEAD state ^(see above^).
)
echo.
echo Log written to: %LOG%

endlocal
exit /b 0

REM ============================================================
REM  :checkRepo  <label>  <path>
REM ============================================================
:checkRepo
set "LABEL=%~1"
set "RPATH=%~2"

echo.
echo %C_CYAN%%C_BOLD%[ %LABEL% ]%C_RESET%
echo   path: %RPATH%
>>"%LOG%" echo.
>>"%LOG%" echo [ %LABEL% ]
>>"%LOG%" echo   path: %RPATH%

REM --- Is it a git working tree? ---
git -C "%RPATH%" rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
    echo   %C_RED%status: NOT a git repository / not checked out%C_RESET%
    >>"%LOG%" echo   status: NOT a git repository / not checked out
    set /a ISSUES+=1
    goto :eof
)

REM --- Determine branch / detached state ---
for /f "delims=" %%b in ('git -C "%RPATH%" rev-parse --abbrev-ref HEAD 2^>nul') do set "BRANCH=%%b"

REM --- Last commit (short hash, subject, author, relative date) ---
for /f "delims=" %%c in ('git -C "%RPATH%" log -1 "--format=%%h  %%s  (%%an, %%ar)" 2^>nul') do set "LASTCOMMIT=%%c"

if /i "%BRANCH%"=="HEAD" (
    REM Detached HEAD - try to show any branches/tags pointing at it
    for /f "delims=" %%d in ('git -C "%RPATH%" describe --all --always 2^>nul') do set "DESCRIBE=%%d"
    echo   %C_RED%%C_BOLD%state: DETACHED HEAD  ^<-- ISSUE%C_RESET%
    echo   %C_RED%branch: ^(none - detached at !DESCRIBE!^)%C_RESET%
    >>"%LOG%" echo   state: DETACHED HEAD  ^<-- ISSUE
    >>"%LOG%" echo   branch: ^(none - detached at !DESCRIBE!^)
    set /a ISSUES+=1
) else (
    echo   %C_GREEN%state: on branch%C_RESET%
    echo   %C_GREEN%branch: %BRANCH%%C_RESET%
    >>"%LOG%" echo   state: on branch
    >>"%LOG%" echo   branch: %BRANCH%
)

echo   last commit: !LASTCOMMIT!
>>"%LOG%" echo   last commit: !LASTCOMMIT!

goto :eof