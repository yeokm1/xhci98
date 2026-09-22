@echo off
rem IMOD.BAT - set, show or clear XhciImodInterval250ns, the interrupt
rem moderation interval the driver reads at every start (roadmap task 23.3;
rem task 23.2 swept it), on an NT target.  Run it IN THE GUEST or on the bench
rem machine, not on the build host.
rem
rem   IMOD SHOW              print the value in force, and the key it is in
rem   IMOD <0-65535>         set XhciImodInterval250ns to that many 250 ns units
rem   IMOD CLEAR             delete the value - the driver then runs at 4000
rem
rem THE CONTRACT IS THE DRIVER'S, NOT THIS FILE'S.  Since 1.1.1.0 every build
rem reads this value at every start and writes the result to IMOD: 10 to 4000
rem is used as given, and anything else - absent, 0, 5000 - is REPLACED by
rem 4000, the hardware reset value (1 ms) and what every release before 1.1.1.0
rem ran at.  Not clamped, so 0 is not moderation off.  An INF install writes
rem 500.  This file still accepts 0 to 65535, so the fallback can be set on
rem purpose and read back.
rem
rem SO CLEAR IS NOT "PUT THE PACKAGE BACK".  It deletes the value, and the
rem driver then runs at 4000 - the control arm of a sweep, and NOT what the
rem package installs.  To go back to the package's own setting, set 500.
rem Leave a bench machine at 500 when the session ends, or say in the run
rem sheet that it was not.  (Until 2026-09-21 the experiment read
rem XhciImodExperiment.  That name is not read by anything.)
rem
rem REQUIRES REG.EXE, which is in the base install from Windows XP onwards.
rem Windows 2000 SP4 carries it only with the Support Tools; on a plain Win2000
rem use IMOD98.BAT's regedit route, which needs nothing extra.  Windows 2000 is
rem a VM-only target for this project anyway, so it cannot take this task's
rem reading - see AGENTS.md, "Project Purpose".
rem
rem THE INSTANCE NUMBER UNDER THE CLASS KEY IS NOT FIXED by anything on either
rem target (run-11v.md, run 2b: it was 0002 there and 0000 on the NT guests of
rem task 23.1.5).  So this file finds the key by CONTENT - the DriverDesc this
rem package writes, which carries "xhci98" - and never by the number.

setlocal
set CLASSKEY=HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}
set VALNAME=XhciImodInterval250ns
set DEVKEY=
set MULTI=

if "%1"=="" goto usage

rem The argument is checked BEFORE the machine is searched, so that a typo is
rem reported as a typo everywhere rather than as "no devnode" on any machine
rem that has not got the driver installed - which includes the build host,
rem where somebody sanity-checking this file will run it first.
if /i "%1"=="SHOW" goto findkey
if /i "%1"=="CLEAR" goto findkey

rem Numeric, and this test also refuses a leading zero rather than accepting it:
rem set /a reads 010 as OCTAL 8, so the round trip through VCHK stops agreeing
rem with what was typed.  Silently writing 8 for a typed 010 would put a wrong
rem interval against a right-looking row in the run sheet.
set /a VCHK=%1 2>nul
if not "%VCHK%"=="%1" goto badvalue
if %1 LSS 0 goto range
if %1 GTR 65535 goto range

:findkey
reg /? >nul 2>&1
if errorlevel 1 goto noreg

rem Every subkey of the USB class key, asked one at a time whether its
rem DriverDesc is ours.  The lines reg query prints that are not keys - its
rem banner, the blank ones - fail the inner query and are skipped by the
rem errorlevel test in :probe rather than by parsing, which is the part that
rem would have to change between reg.exe versions.
for /f "delims=" %%K in ('reg query "%CLASSKEY%" 2^>nul') do call :probe "%%K"

if "%DEVKEY%"=="" goto nodev
if not "%MULTI%"=="" goto toomany

if /i "%1"=="SHOW" goto show
if /i "%1"=="CLEAR" goto clear

reg add "%DEVKEY%" /v %VALNAME% /t REG_DWORD /d %1 /f >nul
if errorlevel 1 goto writefail
echo.
echo Set %VALNAME% = %1  (250 ns units)
if %1 LSS 10 echo   NOTE: below 10, so the driver will use 4000 instead - not 10.
if %1 GTR 4000 echo   NOTE: above 4000, so the driver will use 4000.
if "%1"=="500" echo   500 is what the package's INF installs.
goto restart

:clear
reg delete "%DEVKEY%" /v %VALNAME% /f >nul 2>&1
echo.
echo Cleared %VALNAME% - the driver will run at its own default, 4000.
echo That is the CONTROL arm of a sweep, NOT the package's setting: the INF
echo installs 500.
goto restart

:show
echo.
echo Key: %DEVKEY%
reg query "%DEVKEY%" /v %VALNAME% 2>nul
if errorlevel 1 echo   %VALNAME% is not set - the driver runs at 4000.
echo.
echo What the driver actually did with it is in XHCISNAP's .TXT, not here: the
echo XhciImodInterval250ns lines under "registry values" give the value read,
echo the interval in force and what the register read straight after the write.
goto end

:restart
echo.
echo Key: %DEVKEY%
echo.
echo NOT IN FORCE YET.  The value is read once per start, so the controller has
echo to be restarted before it means anything.  REBOOT rather than disable and
echo re-enable in Device Manager: on the E460 under 32-bit Windows 7 the FIRST
echo Disable of this controller never finished (runs/run-22.md, task 22.9), and
echo a sweep that restarts the controller once per value will meet it.
echo.
echo If the log channel is off, run XHCISNAP -verbosity 1 before the reboot as
echo well: it is read once per start too, and without it no dump is answered.
echo.
echo After the reboot, confirm it took before running any workload: run XHCISNAP
echo and read "interval in force" and "register reads" in the .TXT.  A figure
echo taken without that check is not attributable to an interval.  XHCISNAP
echo -disable when the session ends.
goto end

:probe
rem Called per candidate key.  No setlocal here on purpose: DEVKEY and MULTI
rem have to survive back into the loop.
reg query %1 /v DriverDesc 2>nul | find /i "xhci98" >nul
if errorlevel 1 goto :eof
if not "%DEVKEY%"=="" set MULTI=1
set DEVKEY=%~1
goto :eof

:nodev
echo.
echo ERROR: no devnode under the USB class key has a DriverDesc naming xhci98.
echo.
echo        Either this package is not installed on this machine, or the
echo        controller is not started.  Check Device Manager first - a devnode
echo        showing a yellow bang still has its key, so "not found" here means
echo        the install never happened rather than that it failed.
echo.
echo        Class key searched:
echo        %CLASSKEY%
goto end

:toomany
echo.
echo ERROR: more than one devnode names xhci98, so this file will not guess
echo        which one you meant.  That is a real configuration - a machine with
echo        two xHCI controllers - and not a fault.
echo.
echo        Set the value by hand in regedit under the right instance:
echo        %CLASSKEY%\NNNN
echo        The value is %VALNAME%, REG_DWORD, in 250 ns units.
goto end

:noreg
echo.
echo ERROR: reg.exe is not on this machine.  It is in the base install from
echo        Windows XP onwards; Windows 2000 SP4 has it only with the Support
echo        Tools.  Use IMOD98.BAT, which goes through regedit and needs
echo        nothing extra, or set the value by hand.
goto end

:writefail
echo.
echo ERROR: the write failed.  On Vista and Windows 7 this needs an elevated
echo        command prompt - run as administrator and try again.
goto end

:badvalue
echo.
echo ERROR: "%1" is not a plain decimal number.
echo        Write 40, not 040 and not 0x28 - a leading zero is read as octal.
goto usage

:range
echo.
echo ERROR: %1 is outside 0-65535.
echo.
echo        IMODI is bits 15:0 of IMOD (xHCI 1.2c Table 5-39, p.392).  The
echo        driver would use 4000 for any such value anyway - it accepts 10 to
echo        4000 only - so there is nothing to test above 65535.
goto usage

:usage
echo.
echo Usage:  IMOD SHOW ^| IMOD ^<0-65535^> ^| IMOD CLEAR
echo.
echo   Units are 250 ns, so 4000 = 1 ms = the hardware reset default.
echo   The INF installs 500.  CLEAR deletes the value, and the driver then runs
echo   at 4000.  Outside 10 to 4000 the driver uses 4000 instead, not clamped.
echo.
echo   Windows 98 and ME: use IMOD98.BAT instead.
:end
endlocal
