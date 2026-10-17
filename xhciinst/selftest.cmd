@echo off
rem selftest.cmd - XHCIINST.EXE's and XHCIIN64.EXE's -selftest, their switch
rem refusals and their import tables (design record 23 sections 10 and 11).
rem -selftest also renders every screen of an interactive and a listing run
rem with made-up devices and fails on a line wider than 79 columns
rem (XHCIINST_SCREENS=1 shows them; owner, 2026-10-16).
rem Nothing here touches a device, the registry or a file outside this
rem directory. The pure half's full suite is test\test_inst.c, which
rem test\run-host-tests.cmd runs.
rem
rem The executables carry a manifest asking for elevation, which a build
rem host's unelevated shell cannot answer, so the run asks Windows to start
rem them as the invoker (__COMPAT_LAYER=RunAsInvoker, for this process tree
rem only). XHCIIN64.EXE is run only where it can run: on 64-bit Windows.
rem
rem THE IMPORT GATE FOR THE TOOL: each executable's import table must name
rem KERNEL32.dll and ADVAPI32.dll and nothing else - every SetupAPI, newdev,
rem configuration-manager and version routine is looked up at run time, which
rem is what lets the one image start on Windows 98 SE. check-imports.ps1
rem reads the PE import directory of either image itself (a linker's dump,
rem filtered by a pattern, once let names with other characters through).
rem
rem Exit code 0 = every case behaved. Run it after build.cmd.
setlocal
cd /d "%~dp0"
set "__COMPAT_LAYER=RunAsInvoker"
set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
if "%MSVC6%"=="" set "MSVC6=%REPO%\tools\MSVC600"
if "%WDK71%"=="" set "WDK71=%REPO%\tools\WinDDK71"

if not exist XHCIINST.EXE (
    echo ERROR: XHCIINST.EXE is not here; run build.cmd first
    exit /b 1
)

call :imports XHCIINST.EXE
if errorlevel 1 exit /b 1
if exist XHCIIN64.EXE (
    call :imports XHCIIN64.EXE
    if errorlevel 1 exit /b 1
)

call :one XHCIINST.EXE
if errorlevel 1 exit /b 1
if exist XHCIIN64.EXE (
    if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
        call :one XHCIIN64.EXE
        if errorlevel 1 exit /b 1
    ) else if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" (
        call :one XHCIIN64.EXE
        if errorlevel 1 exit /b 1
    ) else (
        echo XHCIIN64.EXE not run: this is not 64-bit Windows
    )
)
echo.
echo OK: xhciinst self-tests passed
exit /b 0

:imports
rem check-imports.ps1 reads the image's import directory itself: every
rem module name, whatever its characters, and no delay-load table.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check-imports.ps1" -Path "%~1"
if errorlevel 1 exit /b 1
exit /b 0

:one
set "OUT=%TEMP%\xhciinst-selftest-%RANDOM%%RANDOM%.txt"
".\%~1" /selftest > "%OUT%" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%"
if not "%RC%"=="0" (
    echo ERROR: %~1 /selftest exited %RC%
    del /q "%OUT%" 2>nul
    exit /b 1
)
findstr /r /c:"^[0-9][0-9]* checks, 0 failures$" "%OUT%" >nul
if errorlevel 1 (
    echo ERROR: %~1 /selftest printed no clean summary line
    del /q "%OUT%" 2>nul
    exit /b 1
)
del /q "%OUT%" 2>nul
rem An unknown switch, an unknown /set name, /list with /yes and /selftest
rem with company are refused with exit 1 before anything is read.
".\%~1" /frobnicate >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 with an unknown switch was not refused with exit 1
    exit /b 1
)
rem The installer never runs XHCICLN (owner, 2026-10-15): /clean is gone.
".\%~1" /clean >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 /clean was not refused with exit 1
    exit /b 1
)
".\%~1" /set Bogus=1 >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 /set Bogus=1 was not refused with exit 1
    exit /b 1
)
".\%~1" /list /yes >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 /list /yes was not refused with exit 1
    exit /b 1
)
".\%~1" /selftest /list >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 /selftest /list was not refused with exit 1
    exit /b 1
)
rem /to names the install directory of an install, nothing else.
".\%~1" /uninstall /to C:\XHCI98 >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 /uninstall /to was not refused with exit 1
    exit /b 1
)
".\%~1" /to >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 /to with no path was not refused with exit 1
    exit /b 1
)
echo %~1: switch refusals behaved
exit /b 0
