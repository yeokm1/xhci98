@echo off
rem run-host-tests.cmd - build and run the host-side unit tests.
rem
rem These run on the Windows build host, not in DOS or QEMU: they cover the
rem pure PCIINFO -> text logic in xhciqual\mmiodiag.c, none of which the QEMU
rem matrix can reach (SeaBIOS always leaves MSE set and the BAR assigned, and
rem no emulated USB controller has a PM capability). Seconds, no VM.
rem
rem Uses the same Open Watcom the xhciqual build already requires, targeting
rem NT instead of DOS. Any C89 compiler works - the code under test has no
rem DOS dependency, which is the point of the mmiodiag.c split.

setlocal
if "%WATCOM%"=="" set WATCOM=C:\WATCOM
set PATH=%WATCOM%\BINNT64;%WATCOM%\BINNT;%PATH%
set INCLUDE=%WATCOM%\H;%WATCOM%\H\NT

cd /d "%~dp0"

if exist test_mmiodiag.exe del test_mmiodiag.exe
wcl386 -bt=nt -q -zq -wx -za -otexan -fe=test_mmiodiag.exe ^
    test_mmiodiag.c ..\mmiodiag.c
if errorlevel 1 goto builderr
if not exist test_mmiodiag.exe goto builderr

rem Absolute path: this host sets NoDefaultCurrentDirectoryInExePath, so a
rem bare exe name in the working directory is not found.
"%~dp0test_mmiodiag.exe"
if errorlevel 1 goto testfail

echo.
echo Host tests PASSED.
call :cleanup
endlocal
exit /b 0

:builderr
echo.
echo ERROR: could not build the host tests (Watcom at %WATCOM%?).
call :cleanup
endlocal
exit /b 1

:testfail
echo.
echo Host tests FAILED - see the FAIL lines above.
call :cleanup
endlocal
exit /b 1

:cleanup
if exist test_mmiodiag.exe del test_mmiodiag.exe
if exist test_mmiodiag.obj del test_mmiodiag.obj
if exist mmiodiag.obj del mmiodiag.obj
goto :eof
