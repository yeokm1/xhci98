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

rem Task 36.10: the xHCI verdict, C4's outcome, the C6/C8 gate and the exit
rem code (verdict.c), with the classifier it consults (mmiodiag.c).
if exist test_verdict.exe del test_verdict.exe
wcl386 -bt=nt -q -zq -wx -za -otexan -fe=test_verdict.exe ^
    test_verdict.c ..\verdict.c ..\mmiodiag.c
if errorlevel 1 goto builderr
if not exist test_verdict.exe goto builderr
"%~dp0test_verdict.exe"
if errorlevel 1 goto testfail

rem Task 36.11: the routing report, with the driver's reviewed-pair table
rem (src\xhci_pirq.c) built as the driver's own host suites build it.
if exist test_route.exe del test_route.exe
wcl386 -bt=nt -q -zq -wx -za -otexan -DXHCI_HOST_TEST -fe=test_route.exe ^
    test_route.c ..\route.c ..\..\src\xhci_pirq.c
if errorlevel 1 goto builderr
if not exist test_route.exe goto builderr
"%~dp0test_route.exe"
if errorlevel 1 goto testfail

rem Task 36.48: the system description - SMBIOS entry points and tables
rem (valid, 3.0, truncated, garbled, strings missing), CPUID text, and no
rem serial, UUID, asset tag, SKU or part number in any printed line.
if exist test_sysinfo.exe del test_sysinfo.exe
wcl386 -bt=nt -q -zq -wx -za -otexan -fe=test_sysinfo.exe ^
    test_sysinfo.c ..\sysinfo.c
if errorlevel 1 goto builderr
if not exist test_sysinfo.exe goto builderr
"%~dp0test_sysinfo.exe"
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
if exist test_route.exe del test_route.exe
if exist test_route.obj del test_route.obj
if exist route.obj del route.obj
if exist test_verdict.exe del test_verdict.exe
if exist test_verdict.obj del test_verdict.obj
if exist verdict.obj del verdict.obj
if exist xhci_pirq.obj del xhci_pirq.obj
if exist test_sysinfo.exe del test_sysinfo.exe
if exist test_sysinfo.obj del test_sysinfo.obj
if exist sysinfo.obj del sysinfo.obj
goto :eof
