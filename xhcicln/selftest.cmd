@echo off
rem selftest.cmd - run XHCICLN.EXE's and XHCICL64.EXE's -selftest (the pure
rem half: the id classification, the three-state presence decisions, the
rem service verdict, the door name, the exit codes and the removal table;
rem design record 13 section 10.13.8), and check the switch refusals. Nothing
rem here touches a device or the registry.
rem
rem The executables carry a manifest asking for elevation, which a build
rem host's unelevated shell cannot answer, so the run asks Windows to start
rem them as the invoker (__COMPAT_LAYER=RunAsInvoker, for this process tree
rem only). XHCICL64.EXE is run only where it can run: on 64-bit Windows.
rem
rem A case that expects exit 0 tests for exactly 0, as xhcisnap\selftest.cmd
rem does: a crash exits with a negative code, which IF ERRORLEVEL 1 misses.
rem
rem Exit code 0 = every case behaved. Run it after build.cmd.
setlocal
cd /d "%~dp0"
set "__COMPAT_LAYER=RunAsInvoker"

if not exist XHCICLN.EXE (
    echo ERROR: XHCICLN.EXE is not here; run build.cmd first
    exit /b 1
)

call :one XHCICLN.EXE
if errorlevel 1 exit /b 1

if exist XHCICL64.EXE (
    if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
        call :one XHCICL64.EXE
        if errorlevel 1 exit /b 1
    ) else if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" (
        call :one XHCICL64.EXE
        if errorlevel 1 exit /b 1
    ) else (
        echo XHCICL64.EXE not run: this is not 64-bit Windows
    )
)
echo.
echo OK: xhcicln self-tests passed
exit /b 0

:one
set "OUT=%TEMP%\xhcicln-selftest-%RANDOM%%RANDOM%.txt"
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
rem -yes alone, an unknown switch, and -selftest with company are refused
rem with exit 1 before anything is read.
".\%~1" -yes >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 -yes without -remove was not refused with exit 1
    exit /b 1
)
".\%~1" -frobnicate >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 with an unknown switch was not refused with exit 1
    exit /b 1
)
".\%~1" -selftest -remove >nul 2>&1
if not "%ERRORLEVEL%"=="1" (
    echo ERROR: %~1 -selftest -remove was not refused with exit 1
    exit /b 1
)
echo %~1: switch refusals behaved
exit /b 0
