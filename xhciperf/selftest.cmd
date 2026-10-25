@echo off
rem selftest.cmd - XHCIPERF's gated self-test (design record 29 section
rem 10.3), run by scripts\build-driver.cmd once the tool is built: the
rem import table of each image (check-imports.ps1), the source rule of
rem section 4.2 (no physical drive is ever opened; only the two volume
rem openings of sections 5.4 and 6.2), and the built image's /selftest,
rem which renders every report at its longest and touches no disk and opens
rem no window, then a refusal of an unknown switch with exit code 2.
setlocal
cd /d "%~dp0"

if not exist XHCIPERF.EXE (
    echo ERROR: XHCIPERF.EXE is not here; run build.cmd first
    exit /b 1
)

call :imports XHCIPERF.EXE
if errorlevel 1 exit /b 1
if exist XHCIPF64.EXE (
    call :imports XHCIPF64.EXE
    if errorlevel 1 exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "$t = [IO.File]::ReadAllText('xhciperf.c'); if ($t -match 'PhysicalDrive') { Write-Output 'ERROR: xhciperf.c names a physical drive'; exit 1 }; $n = ([regex]::Matches($t, [regex]::Escape('\\.\'))).Count; if ($n -ne 1) { Write-Output ('ERROR: xhciperf.c opens a device name in ' + $n + ' places; only volume_name may'); exit 1 }; exit 0"
if errorlevel 1 exit /b 1

call :one XHCIPERF.EXE
if errorlevel 1 exit /b 1
if exist XHCIPF64.EXE (
    if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
        call :one XHCIPF64.EXE
        if errorlevel 1 exit /b 1
    ) else if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" (
        call :one XHCIPF64.EXE
        if errorlevel 1 exit /b 1
    ) else (
        echo XHCIPF64.EXE not run: this is not 64-bit Windows
    )
)
echo.
echo OK: xhciperf self-tests passed
exit /b 0

:imports
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check-imports.ps1" -Path "%~1"
if errorlevel 1 exit /b 1
exit /b 0

:one
set "OUT=%TEMP%\xhciperf-selftest-%RANDOM%%RANDOM%.txt"
".\%~1" /selftest > "%OUT%" 2>&1
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    type "%OUT%"
    echo ERROR: %~1 /selftest exited %RC%
    del /q "%OUT%" 2>nul
    exit /b 1
)
findstr /r /c:"^[0-9][0-9]* checks, 0 failures" "%OUT%"
if errorlevel 1 (
    echo ERROR: %~1 /selftest printed no clean summary line
    del /q "%OUT%" 2>nul
    exit /b 1
)
del /q "%OUT%" 2>nul
".\%~1" /frobnicate >nul 2>&1
if not "%ERRORLEVEL%"=="2" (
    echo ERROR: %~1 with an unknown switch was not refused with exit 2
    exit /b 1
)
exit /b 0
