@echo off
rem build.cmd - build XHCIQUAL.EXE with Open Watcom (C:\WATCOM).
rem The DOS tool needs the DOS headers (%WATCOM%\H), not the NT ones the
rem xhciqual\SETENV.BAT sets, so the environment is set locally here.

setlocal
if "%WATCOM%"=="" set WATCOM=C:\WATCOM
rem BINW holds dos32a.exe, which wlink embeds as the EXE stub
set PATH=%WATCOM%\BINNT64;%WATCOM%\BINNT;%WATCOM%\BINW;%PATH%
set INCLUDE=%WATCOM%\H

cd /d "%~dp0"

rem Guard: DOS batch wrappers must be CRLF or Win98 COMMAND.COM breaks on them
rem with "Bad command or file name", skipping their :logerr safety branches.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0test\check-bat-eol.ps1"
if errorlevel 1 exit /b 1

rem Guard: host-side unit tests for the pure PCIINFO -> text logic in
rem mmiodiag.c. Seconds, and the QEMU matrix cannot reach any of that code
rem (SeaBIOS always leaves MSE set and no emulated controller exposes a PM
rem capability), so this is its only automated coverage. Skipped by:
rem   build.cmd NOHOSTTEST
rem If this step reports the test binary was "blocked by your organization's
rem Device Guard policy", that is Smart App Control, not a test failure -
rem see docs/contributing/lessons.md.
rem Branch before invoking wmake so NOHOSTTEST is consumed here instead of
rem being interpreted as a make target.
if /i "%1"=="NOHOSTTEST" goto skiphosttest

call "%~dp0test\run-host-tests.cmd"
if errorlevel 1 exit /b 1
wmake -h %*
if errorlevel 1 exit /b 1
goto built

rem NOHOSTTEST is a build.cmd switch, not a wmake target: do not forward it.
:skiphosttest
wmake -h
if errorlevel 1 exit /b 1

:built

echo Built XHCIQUAL.EXE (standalone, DOS/32A extender embedded as the EXE
echo stub). Copy the single XHCIQUAL.EXE to the boot medium.
endlocal
