@echo off
rem IMOD.BAT - set, show or clear the interrupt-moderation interval that roadmap
rem task 23.2's EXPERIMENTAL build reads, on an NT target.  Run it IN THE GUEST
rem or on the bench machine, not on the build host.
rem
rem   IMOD SHOW              print the value in force, and the key it is in
rem   IMOD <0-65535>         set XhciImodInterval250ns to that many 250 ns units
rem   IMOD CLEAR             delete the value - the CONTROL arm of the sweep
rem
rem THE LADDER task 23.2 sweeps: CLEAR (control), 4000, 2000, 1000, 500, 200,
rem 100, 40, 10.  4000 is the hardware reset default, 1 ms.  Take the control
rem first and again last; a machine that has drifted between them has
rem invalidated the run, and that is the cheapest way to find out.
rem
rem CLEAR IS NOT "SET IT TO ZERO".  With the value absent the start writes no
rem IMOD at all, which is the driver exactly as it ships, so the same .sys with
rem nothing set IS the baseline and no rebuild separates it from the readings.
rem Zero is a real and different arm of the sweep - moderation OFF - and the
rem driver tells the two apart by the read's status, not by the value.  Do not
rem use one where the run sheet says the other.
rem
rem IT ONLY DOES ANYTHING IN AN EXPERIMENTAL BUILD.  The value is read behind
rem XHCI_IMOD_EXPERIMENT, which only XHCI_EXTRA_DEFINES sets:
rem     set XHCI_EXTRA_DEFINES=-DXHCI_IMOD_EXPERIMENT
rem     scripts\build-driver.cmd release
rem A shipping binary ignores this value completely and always will - that is
rem what makes the experiment safe to leave in the tree.  So a value set here
rem against a shipping .sys reads back as "no effect", correctly, and is not a
rem fault to chase.
rem
rem THE NAME IS 23.3's, AND SO CLEAR THE VALUE WHEN THE SESSION ENDS.  If task
rem 23.3 ships, it reads this same value under its own contract: default 4000,
rem 10 to 4000 accepted, 4000 substituted for anything outside that.  A 0 left
rem behind falls back to 4000, but 10 to 4000 left behind stays in force under
rem the shipping build.  (Until 2026-09-21 the experiment read
rem XhciImodExperiment, named apart so a leftover was inert.  That name is no
rem longer read by anything.)
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
if "%1"=="0" echo   NOTE: 0 is moderation OFF, not the control arm.  See CLEAR.
goto restart

:clear
reg delete "%DEVKEY%" /v %VALNAME% /f >nul 2>&1
echo.
echo Cleared %VALNAME% - the start will write no IMOD at all.
echo This is the CONTROL arm: the driver exactly as it ships.
goto restart

:show
echo.
echo Key: %DEVKEY%
reg query "%DEVKEY%" /v %VALNAME% 2>nul
if errorlevel 1 echo   %VALNAME% is not set - the start writes no IMOD (control arm).
echo.
echo What the driver actually did with it is in XHCISNAP, not here:
echo   imod.exp.status    0 = the value was read
echo   imod.exp.value     what was asked for
echo   imod.exp.refused   1 = too large for IMODI, and NOTHING was written
echo   imod.exp.written   1 = the start wrote IMOD
echo   imod.exp.readback  what the register read straight after the write
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
echo After the reboot, confirm it took before running any workload:
echo   XHCISNAP -verbosity 2
echo then check imod.exp.written = 1 and imod.exp.readback = what you asked for.
echo A figure taken without that check is not attributable to an interval.
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
echo        IMODI is bits 15:0 of IMOD (xHCI 1.2c Table 5-39, p.392); bits
echo        31:16 are IMODC, an interrupt COUNTER and not an interval.  The
echo        driver refuses a larger value rather than masking it, so a typed
echo        70000 would leave the register alone rather than quietly writing
echo        4464 - but there is no reason to make it prove that here.
goto usage

:usage
echo.
echo Usage:  IMOD SHOW ^| IMOD ^<0-65535^> ^| IMOD CLEAR
echo.
echo   Task 23.2's sweep: CLEAR (control), 4000, 2000, 1000, 500, 200, 100, 40, 10
echo   Units are 250 ns, so 4000 = 1 ms = the hardware reset default.
echo   CLEAR is the control arm; 0 is moderation off.  They are not the same.
echo.
echo   Only an experimental build reads this value:
echo     set XHCI_EXTRA_DEFINES=-DXHCI_IMOD_EXPERIMENT
echo     scripts\build-driver.cmd release
echo.
echo   Windows 98 and ME: use IMOD98.BAT instead.
:end
endlocal
