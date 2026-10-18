@echo off
rem selftest.cmd - the installer's four executables' /selftest, their switch
rem refusals and their import tables (design record 23 sections 10 and 11,
rem record 26 section 11).
rem The console tool's /selftest renders every screen of an interactive and
rem a listing run with made-up devices and fails on a line wider than 79
rem columns, or on questions asked out of record 23's order
rem (XHCIINST_SCREENS=1 shows them; owner, 2026-10-16). The window's
rem renders every page's fixed text through its own dialog templates off
rem screen and fails on a text its control does not hold whole.
rem Nothing here touches a device, the registry or a file outside this
rem directory. The pure half's full suite is test\test_inst.c, which
rem test\run-host-tests.cmd runs.
rem
rem The executables carry a manifest asking for elevation, which a build
rem host's unelevated shell cannot answer, so the run asks Windows to start
rem them as the invoker (__COMPAT_LAYER=RunAsInvoker, for this process tree
rem only). The x64 pair is run only where it can run: on 64-bit Windows.
rem
rem THE IMPORT GATE FOR THE TOOL: the console tool's import table must name
rem KERNEL32.dll and ADVAPI32.dll and nothing else, the window's those and
rem USER32, GDI32, COMCTL32, COMDLG32 and SHELL32 - every SetupAPI,
rem newdev, configuration-manager and version routine is looked up at run
rem time, which is what lets one image start on Windows 98 SE.
rem check-imports.ps1 reads the PE import directory of each image itself (a
rem linker's dump, filtered by a pattern, once let names with other
rem characters through).
rem
rem Exit code 0 = every case behaved. Run it after build.cmd.
setlocal
cd /d "%~dp0"
set "__COMPAT_LAYER=RunAsInvoker"
set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
if "%MSVC6%"=="" set "MSVC6=%REPO%\tools\MSVC600"
if "%WDK71%"=="" set "WDK71=%REPO%\tools\WinDDK71"
set "IS64=0"
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set "IS64=1"
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set "IS64=1"

if not exist XHCIINST.EXE (
    echo ERROR: XHCIINST.EXE is not here; run build.cmd first
    exit /b 1
)
if not exist XHCIWIZ.EXE (
    echo ERROR: XHCIWIZ.EXE is not here; run build.cmd first
    exit /b 1
)

call :imports XHCIINST.EXE console
if errorlevel 1 exit /b 1
call :imports XHCIWIZ.EXE window
if errorlevel 1 exit /b 1
if exist XHCIIN64.EXE (
    call :imports XHCIIN64.EXE console
    if errorlevel 1 exit /b 1
)
if exist XHCIWZ64.EXE (
    call :imports XHCIWZ64.EXE window
    if errorlevel 1 exit /b 1
)

call :one XHCIINST.EXE
if errorlevel 1 exit /b 1
call :wiz XHCIWIZ.EXE
if errorlevel 1 exit /b 1
if "%IS64%"=="1" (
    if exist XHCIIN64.EXE (
        call :one XHCIIN64.EXE
        if errorlevel 1 exit /b 1
    )
    if exist XHCIWZ64.EXE (
        call :wiz XHCIWZ64.EXE
        if errorlevel 1 exit /b 1
    )
) else (
    echo The x64 pair not run: this is not 64-bit Windows
)
echo.
echo OK: xhciinst self-tests passed
exit /b 0

:imports
rem check-imports.ps1 reads the image's import directory itself: every
rem module name, whatever its characters, and no delay-load table.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check-imports.ps1" -Path "%~1" -Profile %2
if errorlevel 1 exit /b 1
exit /b 0

:selftest
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
exit /b 0

:refused
rem %1 the image, then its arguments: refused with exit 1 before anything
rem is read (the window says so on standard output, which goes to NUL).
".\%~1" %2 %3 %4 %5 >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 %2 %3 %4 %5 was not refused with exit 1
    exit /b 1
)
exit /b 0

:one
call :selftest %1
if errorlevel 1 exit /b 1
rem An unknown switch, an unknown /set name, /list with /yes and /selftest
rem with company are refused with exit 1 before anything is read.
call :refused %1 /frobnicate
if errorlevel 1 exit /b 1
rem The installer never runs XHCICLN (owner, 2026-10-15): /clean is gone.
call :refused %1 /clean
if errorlevel 1 exit /b 1
call :refused %1 /set Bogus=1
if errorlevel 1 exit /b 1
call :refused %1 /list /yes
if errorlevel 1 exit /b 1
call :refused %1 /selftest /list
if errorlevel 1 exit /b 1
rem /to names the install directory of an install, nothing else; /from the
rem uninstall's relaunch.
call :refused %1 /uninstall /to C:\XHCI98
if errorlevel 1 exit /b 1
call :refused %1 /to
if errorlevel 1 exit /b 1
call :refused %1 /from C:\XHCI98
if errorlevel 1 exit /b 1
echo %~1: switch refusals behaved
exit /b 0

:wiz
call :selftest %1
if errorlevel 1 exit /b 1
rem The window takes record 23's switches where they mean something to a
rem window and refuses the console's (record 26 section 10).
call :refused %1 /frobnicate
if errorlevel 1 exit /b 1
call :refused %1 /yes
if errorlevel 1 exit /b 1
call :refused %1 /list
if errorlevel 1 exit /b 1
call :refused %1 /console
if errorlevel 1 exit /b 1
call :refused %1 /devices
if errorlevel 1 exit /b 1
call :refused %1 /dir release-x86
if errorlevel 1 exit /b 1
call :refused %1 /from C:\XHCI98
if errorlevel 1 exit /b 1
echo %~1: switch refusals behaved
exit /b 0
