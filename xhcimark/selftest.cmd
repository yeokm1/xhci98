@echo off
rem selftest.cmd - run XHCIMARK.EXE's and XHCIMK64.EXE's -selftest (the pure
rem half: the answers' parsers, the verifiable domain, the write's checks,
rem the restart's wish, the reconciliation, the multi-string, the record and
rem the confirmation; design record 21 section 7.4, revision 3), check the
rem switch refusals and the import table (KERNEL32 and ADVAPI32 alone).
rem Nothing here touches a device, a service or the registry: -selftest and
rem a refused switch read nothing. XHCIMK64.EXE is run only on 64-bit
rem Windows.
rem
rem Exit code 0 = every case behaved. Run it after build.cmd.
setlocal
cd /d "%~dp0"

if not exist XHCIMARK.EXE (
    echo ERROR: XHCIMARK.EXE is not here; run build.cmd first
    exit /b 1
)

call :one XHCIMARK.EXE
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File check-imports.ps1 -Path XHCIMARK.EXE
if errorlevel 1 exit /b 1

if exist XHCIMK64.EXE (
    powershell -NoProfile -ExecutionPolicy Bypass -File check-imports.ps1 -Path XHCIMK64.EXE
    if errorlevel 1 exit /b 1
    if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
        call :one XHCIMK64.EXE
        if errorlevel 1 exit /b 1
    ) else if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" (
        call :one XHCIMK64.EXE
        if errorlevel 1 exit /b 1
    ) else (
        echo XHCIMK64.EXE not run: this is not 64-bit Windows
    )
)
echo.
echo OK: xhcimark self-tests passed
exit /b 0

:one
set "OUT=%TEMP%\xhcimark-selftest-%RANDOM%%RANDOM%.txt"
".\%~1" -selftest > "%OUT%" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%"
if not "%RC%"=="0" (
    echo ERROR: %~1 -selftest exited %RC%
    del /q "%OUT%" 2>nul
    exit /b 1
)
findstr /r /c:"^[0-9][0-9]* checks, 0 failures$" "%OUT%" >nul
if errorlevel 1 (
    echo ERROR: %~1 -selftest printed no clean summary line
    del /q "%OUT%" 2>nul
    exit /b 1
)
del /q "%OUT%" 2>nul
rem An unknown switch and -selftest with company are refused with exit 1
rem before anything is read.
".\%~1" -frobnicate >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 with an unknown switch was not refused with exit 1
    exit /b 1
)
".\%~1" -selftest -once >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 -selftest -once was not refused with exit 1
    exit /b 1
)
echo %~1: switch refusals behaved
exit /b 0
