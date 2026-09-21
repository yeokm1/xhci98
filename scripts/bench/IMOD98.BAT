@echo off
rem IMOD98.BAT - the WINDOWS 98 SE / WINDOWS ME variant of IMOD.BAT (roadmap
rem task 23.2's moderation sweep).  Sets the interval the EXPERIMENTAL build
rem reads.  Run it on the bench machine, not on the build host.
rem
rem   IMOD98 FIND                   find the instance number, and print it
rem   IMOD98 step NNNN              set the value under that instance
rem   IMOD98 CLEAR NNNN             delete the value - the CONTROL arm
rem   IMOD98 HEX xxxxxxxx NNNN      set any value, as eight hex digits
rem
rem   step is one of the ladder steps, by its DECIMAL name:
rem       0   10   40   100   200   500   1000   2000   4000
rem   NNNN is the instance under ...\Services\Class\USB.  Run FIND to get it.
rem
rem WHY THE STEPS ARE A FIXED LIST AND NOT ANY NUMBER.  Windows 98's shell is
rem COMMAND.COM, which has no reg.exe, no `for /f` and no `set /a`, so the only
rem route to the registry is to write a REGEDIT4 file and import it - and a
rem REGEDIT4 dword is written in HEX.  Nothing here can convert 4000 into 0fa0.
rem The ladder task 23.2 actually sweeps is nine values, so they are tabulated
rem below, and HEX takes the digits as their own argument because COMMAND.COM
rem has no substring syntax either and so cannot strip a prefix off one.
rem
rem THREE MORE COMMAND.COM LIMITS ARE WRITTEN INTO THE SHAPE OF THIS FILE, and
rem each of them is a thing that fails SILENTLY or WEIRDLY rather than loudly:
rem
rem   IF HAS NO /I.  That switch is cmd.exe's.  `if /i "%1"=="FIND"` does not
rem   compare case-insensitively here, it fails to parse - so every keyword is
rem   tested in the three spellings a person actually types.
rem
rem   THE CARET IS NOT AN ESCAPE CHARACTER.  It is cmd.exe's.  So the angle
rem   brackets that would mark a placeholder in a usage line cannot be
rem   escaped, and an unescaped one IS a redirection: a usage line that puts
rem   the word step between angle brackets tries to read a file called step.
rem   There is not a single angle bracket in this file's output for that
rem   reason.  LOAD98.BAT's usage line is written the same way, beside a
rem   LOAD.BAT that can afford them.
rem
rem   AND NOT IN ITS COMMENTS EITHER.  COMMAND.COM performs redirection on a
rem   REM line: the angle brackets and pipes quoted in this header until
rem   2026-09-22 created junk files named after their targets in the current
rem   folder, and printed File not found for the input one, on every run
rem   (runs/run-23.md, task 23.2, measured on the P14s Gen 1).  So the
rem   redirection syntax below is spelled out in words, never written.
rem
rem   THERE IS NO NUMBERED-HANDLE REDIRECTION.  Sending handle 2 to NUL does
rem   nothing useful here - that is cmd.exe's too - so nothing below tries to
rem   suppress an error stream, and every failure is caught with `if exist` or
rem   `if not exist` on the thing that was supposed to be produced.
rem
rem   *(An earlier draft of this header claimed the trailing digit of a dword
rem   written with an append-redirection straight after it would be read as a
rem   handle number by cmd.exe, and was safe only because COMMAND.COM cannot
rem   parse one.  That is wrong, and it was measured wrong rather than
rem   reasoned wrong: cmd.exe treats a digit before the redirection as a handle
rem   only when the digit is its own token, so a value ending in 0 appended
rem   straight to a file comes through intact, while the same 0 standing
rem   alone redirects stdin and writes an empty file.  All nine of the values
rem   this file writes come through intact.  The writes are safe on both
rem   shells; only the reasons above make this file 9x-only.)*
rem
rem This is the same reason LOAD98.BAT exists beside LOAD.BAT rather than one
rem file serving both targets: see its header.
rem
rem CLEAR IS NOT "SET IT TO ZERO".  With the value absent the start writes no
rem IMOD at all, which is the driver exactly as it ships, so the same .sys with
rem nothing set IS the baseline and no rebuild separates it from the readings.
rem Zero is a real and different arm - moderation OFF - and the driver tells
rem them apart by the read's status, not by the value.  Do not use one where
rem the run sheet says the other.
rem
rem IT ONLY DOES ANYTHING IN AN EXPERIMENTAL BUILD, one built with
rem XHCI_EXTRA_DEFINES=-DXHCI_IMOD_EXPERIMENT.  A shipping binary ignores this
rem value completely and always will - that is what makes the experiment safe
rem to leave in the tree.  So a value set here against a shipping .sys reads
rem back as "no effect", correctly, and is not a fault to chase.
rem
rem THE NAME IS 23.3's, AND SO CLEAR THE VALUE WHEN THE SESSION ENDS.  If task
rem 23.3 ships, it reads this same value under its own contract: default 4000,
rem 10 to 4000 accepted, 4000 substituted for anything outside that.  A 0 left
rem behind falls back to 4000, but 10 to 4000 left behind stays in force under
rem the shipping build.  (Until 2026-09-21 the experiment read
rem XhciImodExperiment, named apart so a leftover was inert.  That name is no
rem longer read by anything.)
rem
rem THE INSTANCE NUMBER IS NOT FIXED by anything on this target - it was 0002
rem on the 2a machine (runs/run-11v.md) - and COMMAND.COM cannot search for it,
rem which is why FIND is a separate step run once per machine rather than
rem something this file does on every call.
rem
rem AND ON WINDOWS 98 THE RESTART IS A REBOOT, not a Device Manager disable:
rem disabling any USB host controller devnode bugchecks this target before the
rem teardown completes (build-and-test.md, "Do not disable the controller in
rem Device Manager").  One reboot per value, and the sweep is planned around
rem that rather than against it.
rem
rem ONE ENVIRONMENT VARIABLE, and that is a budget rather than a style.
rem COMMAND.COM's environment block is small and already carries PATH, COMSPEC,
rem TEMP, PROMPT and winbootdir; LOAD98.BAT sets none at all for that reason.
rem The HEX form needs its key argument in a different position, and the cost
rem of a second variable is paid as a duplicated write branch instead.

if "%1"=="" goto usage

if "%1"=="FIND" goto find
if "%1"=="find" goto find
if "%1"=="Find" goto find

if "%1"=="HEX" goto hex
if "%1"=="hex" goto hex
if "%1"=="Hex" goto hex

if "%1"=="CLEAR" goto clear
if "%1"=="clear" goto clear
if "%1"=="Clear" goto clear

if "%2"=="" goto usage

set IMODHEX=
if "%1"=="0" set IMODHEX=00000000
if "%1"=="10" set IMODHEX=0000000a
if "%1"=="40" set IMODHEX=00000028
if "%1"=="100" set IMODHEX=00000064
if "%1"=="200" set IMODHEX=000000c8
if "%1"=="500" set IMODHEX=000001f4
if "%1"=="1000" set IMODHEX=000003e8
if "%1"=="2000" set IMODHEX=000007d0
if "%1"=="4000" set IMODHEX=00000fa0
if "%IMODHEX%"=="" goto badstep

echo REGEDIT4>C:\IMODSET.REG
echo.>>C:\IMODSET.REG
echo [HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\USB\%2]>>C:\IMODSET.REG
echo "XhciImodInterval250ns"=dword:%IMODHEX%>>C:\IMODSET.REG
echo.>>C:\IMODSET.REG
regedit /s C:\IMODSET.REG
echo.
echo Set XhciImodInterval250ns = %1  (250 ns units, written as dword:%IMODHEX%)
echo   under ...\Services\Class\USB\%2
if "%1"=="0" echo   NOTE: 0 is moderation OFF, not the control arm.  See CLEAR.
goto restart

:hex
if "%2"=="" goto usage
if "%3"=="" goto usage
set IMODHEX=%2
echo REGEDIT4>C:\IMODSET.REG
echo.>>C:\IMODSET.REG
echo [HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\USB\%3]>>C:\IMODSET.REG
echo "XhciImodInterval250ns"=dword:%IMODHEX%>>C:\IMODSET.REG
echo.>>C:\IMODSET.REG
regedit /s C:\IMODSET.REG
echo.
echo Set XhciImodInterval250ns = dword:%IMODHEX%
echo   under ...\Services\Class\USB\%3
echo.
echo NOT CHECKED HERE: that those are eight hex digits, and that the value fits
echo IMODI.  regedit takes a short or malformed dword quietly, so read the
echo value back in XHCISNAP rather than trusting this line.  A value above
echo ffff is REFUSED by the driver, not masked - imod.exp.refused goes to 1
echo and nothing at all is written to the register.
goto restart

:clear
if "%2"=="" goto usage
echo REGEDIT4>C:\IMODSET.REG
echo.>>C:\IMODSET.REG
echo [HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\USB\%2]>>C:\IMODSET.REG
echo "XhciImodInterval250ns"=->>C:\IMODSET.REG
echo.>>C:\IMODSET.REG
regedit /s C:\IMODSET.REG
echo.
echo Cleared XhciImodInterval250ns under ...\Services\Class\USB\%2
echo The start will write no IMOD at all - the driver exactly as it ships.
echo This is the CONTROL arm of the sweep.
goto restart

:restart
echo.
echo NOT IN FORCE YET.  The value is read once per start.
echo.
echo REBOOT.  Do NOT disable the controller in Device Manager to restart it -
echo on this target that bugchecks the machine before the teardown completes.
echo.
echo After the reboot, confirm it took before running any workload:
echo   XHCISNAP -verbosity 2
echo then check imod.exp.written = 1 and imod.exp.readback = what you asked
echo for.  A figure taken without that check is not attributable to an
echo interval.
goto end

:find
echo.
echo Exporting the USB class key to C:\USBCLASS.REG ...
regedit /e C:\USBCLASS.REG HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\USB
if not exist C:\USBCLASS.REG goto noexport
echo.
echo Looking for this package in it:
echo.
find /i "xhci98" C:\USBCLASS.REG
echo.
echo If nothing is listed above, this package is not installed on this machine
echo and there is no key to set anything in.
echo.
echo If it IS listed, open the file and read the instance number off the
echo [...\Services\Class\USB\NNNN] line ABOVE the xhci98 line:
echo.
echo     edit C:\USBCLASS.REG
echo.
echo Then use that NNNN in every later call, for example:
echo     IMOD98 4000 0002
echo.
echo The number is not fixed by anything - it was 0002 on the 2a machine - so
echo read it once per machine rather than assuming it.
goto end

:noexport
echo.
echo ERROR: regedit wrote no C:\USBCLASS.REG.  Is C: writable, and is there
echo        room on it?
goto end

:badstep
echo.
echo ERROR: "%1" is not one of the ladder steps.
echo        Write 40, not 040 and not 0x28.  For a value off the ladder:
echo            IMOD98 HEX 0000012c %2
goto usage

:usage
echo.
echo Usage:  IMOD98 FIND
echo         IMOD98 step NNNN
echo         IMOD98 CLEAR NNNN
echo         IMOD98 HEX xxxxxxxx NNNN
echo.
echo   step: 0  10  40  100  200  500  1000  2000  4000
echo   NNNN: the instance under ...\Services\Class\USB.  Run FIND once per
echo         machine to get it - it is not fixed.
echo.
echo   Units are 250 ns, so 4000 = 1 ms = the hardware reset default.
echo   Task 23.2's sweep: CLEAR, 4000, 2000, 1000, 500, 200, 100, 40, 10,
echo   taking CLEAR first and again last.
echo   CLEAR is the control arm; 0 is moderation off.  Not the same thing.
echo.
echo   Only an experimental build reads this value - one built with
echo   XHCI_EXTRA_DEFINES=-DXHCI_IMOD_EXPERIMENT.
echo.
echo   Windows 2000 and later: use IMOD.BAT instead.
:end
