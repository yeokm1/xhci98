@echo off
rem silent-on.bat - turns the xhci98 silent install on: merges SILENT.REG,
rem checks that the list reads back as its six values, and only then sets
rem XhciSilentInstall to 1 on every xhci98 controller through silent.vbs,
rem which is beside it in the download's tools\silent directory (roadmap-hcd
rem task 38.2, design record 13 section 6.11.8).
rem
rem Windows 98 SE and ME only: double-click it, or run it at a prompt in
rem this directory, then restart Windows. Run it only once each listed
rem class's drivers have been installed at least once on this machine.
rem
rem One file for COMMAND.COM and cmd.exe. COMMAND.COM never runs the lines
rem after the nt label: the goto run before them jumps over them. Every line
rem from the run label on is COMMAND.COM syntax. ASCII, CRLF line endings.
if "%OS%"=="Windows_NT" goto nt
goto run
:nt
echo silent-on.bat: Windows 98 SE and ME only; nothing was changed.
goto end
:run
echo silent-on.bat: the silent install on, XhciSilentInstall 1, with the
echo list of SILENT.REG.
if not exist silent.vbs goto nofile
if not exist silent.reg goto nofile
echo Now:
cscript //nologo silent.vbs show
if errorlevel 1 goto nochange
echo Merging SILENT.REG:
regedit /s silent.reg
echo Checking the list, then setting the switch to 1:
cscript //nologo silent.vbs 1
if errorlevel 5 goto unverified
if errorlevel 4 goto nolist
if errorlevel 3 goto partial
if errorlevel 1 goto notset
goto end
:unverified
echo The switch may have changed on some controllers and could not be read
echo back; the lines above say which. Run silent-off.bat to set it back to
echo 0, or check XhciSilentInstall in regedit. Restart only once it reads
echo right.
goto end
:nolist
echo The list did not read back as SILENT.REG's six values; the lines
echo above say what was found. The switch was not changed on any
echo controller. The list may be merged in part. A controller whose switch
echo was already on (the lines under Now say so) stays on over that list:
echo run silent-off.bat if the silent install must stop.
goto end
:partial
echo The switch was set on some controllers and not others; the lines above
echo say which. Run silent-off.bat, or correct it and run silent-on.bat
echo again.
goto end
:notset
echo The switch was not set.
goto end
:nochange
echo Nothing was changed: no xhci98 controller was found, or this is not
echo Windows 98 SE or ME. The list was not merged and the switch not set.
goto end
:nofile
echo silent.vbs or silent.reg was not found in this directory. Run
echo silent-on.bat from the download's TOOLS\SILENT directory, beside them:
echo double-click it there, or change to that directory at the prompt first.
:end
echo.
pause
