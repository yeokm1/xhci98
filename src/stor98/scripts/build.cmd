@echo off
rem SPDX-License-Identifier: BSD-3-Clause
rem Copyright (c) 2026, Yeo Kheng Meng
rem
rem build.cmd - build stor98 for every target from the single src\ tree.
rem
rem   scripts\build.cmd [target] [flavor]
rem
rem   target  9x | x86 | amd64 | all            (default: all)
rem   flavor  free | checked | both             (default: free)
rem
rem   9x     stor98.mpd  Windows 2000 DDK + MSVC 6.0, NVME2K_W9X,
rem                      SCSI.H/SRB.H from tools\w98ddk\inc
rem   x86    stor98.sys  Windows 2000 DDK + MSVC 6.0   setenv <ddk> <fl> w2k x86
rem   amd64  stor98.sys  WDK 7.1 7600.16385.1          setenv <wdk> <fl> x64 WNET no_oacr
rem
rem Toolchains are found in tools\ next to this script's parent directory
rem (see docs\BUILD.md); nothing is installed machine-wide. Results are copied
rem to out\<flavor>\{win9x,x86,amd64}\ together with the .pdb. Each build runs
rem in its own child cmd because setenv.bat is not idempotent.
rem
rem BUILD_ALT_DIR is overridden after setenv.bat so the three targets never
rem share an object tree: 9x uses obj{fre,chk}9x\i386, x86 obj{fre,chk}\i386,
rem amd64 obj{fre,chk}\amd64 (WDK 7.1 would otherwise say fre_wnet_AMD64).
rem
rem STOR98_DBGPORT=1 in the environment builds the test-only debug-port
rem instrumentation (src\dbgport.h) into out\<flavor>-dbgport\ instead.
rem
rem Exit code 0 when every requested build produced its image without errors.

setlocal
for %%I in ("%~dp0..") do set "REPO=%%~fI"
set "MSVC=%REPO%\tools\MSVC600"
set "W2KDDK=%REPO%\tools\ntddk"
set "WDK71=%REPO%\tools\WinDDK71"
set "W98INC=%REPO%\tools\w98ddk\inc"

set "TARGETS=%~1"
set "FLAVORS=%~2"
if "%TARGETS%"=="" set "TARGETS=all"
if "%FLAVORS%"=="" set "FLAVORS=free"
if /i "%TARGETS%"=="all" set "TARGETS=9x x86 amd64"
if /i "%FLAVORS%"=="both" set "FLAVORS=free checked"

for %%T in (%TARGETS%) do (
    if /i not "%%T"=="9x" if /i not "%%T"=="x86" if /i not "%%T"=="amd64" (
        echo ERROR: unknown target "%%T" - use 9x, x86, amd64 or all
        exit /b 1
    )
)
for %%F in (%FLAVORS%) do (
    if /i not "%%F"=="free" if /i not "%%F"=="checked" (
        echo ERROR: unknown flavor "%%F" - use free, checked or both
        exit /b 1
    )
)

rem setenv.bat takes BASEDIR verbatim and cannot be quoted.
for /f "tokens=2 delims= " %%S in ("%REPO%") do (
    echo ERROR: the repository path contains a space: "%REPO%"
    echo The DDK's setenv.bat cannot take a quoted path. Clone it elsewhere.
    exit /b 1
)

set "FAILED="
for %%F in (%FLAVORS%) do for %%T in (%TARGETS%) do call :build %%T %%F || set "FAILED=1"
if defined FAILED (
    echo.
    echo *** BUILD FAILED ***
    exit /b 1
)
echo.
echo All builds succeeded. Output in "%REPO%\out".
exit /b 0

rem ------------------------------------------------------------------
rem :build <9x|x86|amd64> <free|checked>
rem ------------------------------------------------------------------
:build
setlocal
set "TGT=%~1"
set "FL=%~2"
set "ALT=fre"
if /i "%FL%"=="checked" set "ALT=chk"
set "ARCHDIR=i386"
set "STOR98_TARGET=nt"
set "DDKROOT=%W2KDDK%"
set "SETENVARGS=w2k x86"
set "OUTSUB=%TGT%"
set "IMAGE=stor98.sys"

if /i "%TGT%"=="9x" (
    set "ALT=%ALT%9x"
    set "STOR98_TARGET=9x"
    set "OUTSUB=win9x"
    set "IMAGE=stor98.mpd"
)
if /i "%TGT%"=="amd64" (
    set "ARCHDIR=amd64"
    set "DDKROOT=%WDK71%"
    set "SETENVARGS=x64 WNET no_oacr"
)

if not exist "%DDKROOT%\bin\setenv.bat" (
    echo ERROR: "%DDKROOT%\bin\setenv.bat" not found - see docs\BUILD.md
    exit /b 1
)
if /i not "%TGT%"=="amd64" if not exist "%MSVC%\VC98\BIN\CL.EXE" (
    echo ERROR: MSVC 6.0 not found at "%MSVC%" - see docs\BUILD.md
    exit /b 1
)
if /i "%TGT%"=="9x" if not exist "%W98INC%\SCSI.H" (
    echo ERROR: "%W98INC%\SCSI.H" not found - see docs\BUILD.md
    exit /b 1
)
if /i "%TGT%"=="9x" if not exist "%W98INC%\SRB.H" (
    echo ERROR: "%W98INC%\SRB.H" not found - see docs\BUILD.md
    exit /b 1
)

set "OBJ=%REPO%\src\obj%ALT%\%ARCHDIR%"
set "LOGBASE=%REPO%\src\build%ALT%"
set "OUTDIR=%REPO%\out\%FL%\%OUTSUB%"
if "%STOR98_DBGPORT%"=="1" set "OUTDIR=%REPO%\out\%FL%-dbgport\%OUTSUB%"

echo.
echo === %TGT% %FL% ===
if exist "%OBJ%\stor98.sys" del "%OBJ%\stor98.sys"
if exist "%LOGBASE%.err" del "%LOGBASE%.err"
if exist "%LOGBASE%.wrn" del "%LOGBASE%.wrn"

cmd /c "call %DDKROOT%\bin\setenv.bat %DDKROOT% %FL% %SETENVARGS% >nul && set "BUILD_ALT_DIR=%ALT%" && cd /d "%REPO%\src" && build -cZ"
if errorlevel 1 (
    echo ERROR: build.exe failed for %TGT% %FL%
    if exist "%LOGBASE%.err" type "%LOGBASE%.err"
    exit /b 1
)
rem build.exe can exit 0 and still have written errors.
if exist "%LOGBASE%.err" (
    echo ERROR: errors in "%LOGBASE%.err":
    type "%LOGBASE%.err"
    exit /b 1
)
if not exist "%OBJ%\stor98.sys" (
    echo ERROR: "%OBJ%\stor98.sys" was not produced
    exit /b 1
)
if exist "%LOGBASE%.wrn" (
    echo WARNINGS in "%LOGBASE%.wrn":
    type "%LOGBASE%.wrn"
)

if not exist "%OUTDIR%" mkdir "%OUTDIR%"
copy /y "%OBJ%\stor98.sys" "%OUTDIR%\%IMAGE%" >nul || exit /b 1
if exist "%OBJ%\stor98.pdb" copy /y "%OBJ%\stor98.pdb" "%OUTDIR%\stor98.pdb" >nul
echo Built "%OUTDIR%\%IMAGE%"
endlocal
exit /b 0
