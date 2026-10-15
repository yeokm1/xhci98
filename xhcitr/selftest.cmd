@echo off
rem selftest.cmd - XHCITR.EXE's and XHCITR64.EXE's import tables, their
rem /selftest and their switch refusals (design record 25 sections 7.3 and
rem 9). Nothing here opens a device, reads the device tree or writes the
rem registry: /selftest checks the pure half inside the built image, and a
rem refused command line ends before anything is read. The pure half's
rem full suite is test\test_tr.c, which test\run-host-tests.cmd runs.
rem
rem THE IMPORT GATE FOR THE TOOL: each executable's import table must name
rem KERNEL32, USER32, GDI32, ADVAPI32, SHELL32, COMCTL32 and COMDLG32 and
rem nothing else - every SetupAPI, configuration-manager and security
rem routine is looked up at run time, which is what lets the one image
rem start on Windows 98 SE. check-imports.ps1 reads the PE import directory.
rem
rem The tool is a Windows program: with its standard output redirected it
rem writes its answers there instead of showing a message box, and a batch
rem file waits for it. XHCITR64.EXE is run only on 64-bit Windows.
rem
rem Exit code 0 = every case behaved. Run it after build.cmd.
setlocal
cd /d "%~dp0"

if not exist XHCITR.EXE (
    echo ERROR: XHCITR.EXE is not here; run build.cmd first
    exit /b 1
)

call :imports XHCITR.EXE
if errorlevel 1 exit /b 1
if exist XHCITR64.EXE (
    call :imports XHCITR64.EXE
    if errorlevel 1 exit /b 1
)

call :one XHCITR.EXE
if errorlevel 1 exit /b 1
if exist XHCITR64.EXE (
    if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
        call :one XHCITR64.EXE
        if errorlevel 1 exit /b 1
    ) else if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" (
        call :one XHCITR64.EXE
        if errorlevel 1 exit /b 1
    ) else (
        echo XHCITR64.EXE not run: this is not 64-bit Windows
    )
)
echo.
echo OK: xhcitr self-tests passed
exit /b 0

:imports
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check-imports.ps1" -Path "%~1"
if errorlevel 1 exit /b 1
exit /b 0

:one
set "OUT=%TEMP%\xhcitr-selftest-%RANDOM%%RANDOM%.txt"
".\%~1" /selftest > "%OUT%" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%"
if not "%RC%"=="0" (
    echo ERROR: %~1 /selftest exited %RC%
    del /q "%OUT%" 2>nul
    exit /b 1
)
findstr /r /c:"^[0-9][0-9]* checks, 0 failures" "%OUT%" >nul
if errorlevel 1 (
    echo ERROR: %~1 /selftest printed no clean summary line
    del /q "%OUT%" 2>nul
    exit /b 1
)
del /q "%OUT%" 2>nul
rem An unknown switch, /report without a file, /eject without id=, and a
rem one-shot mode with company are refused with exit 1.
".\%~1" /frobnicate >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 with an unknown switch was not refused with exit 1
    exit /b 1
)
".\%~1" /report >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 /report without a file was not refused with exit 1
    exit /b 1
)
".\%~1" /eject foo >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 /eject foo was not refused with exit 1
    exit /b 1
)
".\%~1" /tray /report x.txt >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 /tray /report was not refused with exit 1
    exit /b 1
)
if exist x.txt (
    echo ERROR: %~1 /tray /report wrote a report
    del /q x.txt
    exit /b 1
)
echo %~1: switch refusals behaved
exit /b 0
