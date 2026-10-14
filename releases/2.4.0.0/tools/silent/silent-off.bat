@echo off
rem silent-off.bat - turns the xhci98 silent install off: sets
rem XhciSilentInstall to 0 on every xhci98 controller through silent.vbs,
rem which is beside it in the download's tools\silent directory, and leaves
rem the SilentClasses list in place (roadmap-hcd task 38.2, design record 13
rem section 6.11.8).
rem
rem Windows 98 SE and ME only: double-click it, or run it at a prompt in
rem this directory, then restart Windows.
rem
rem One file for COMMAND.COM and cmd.exe. COMMAND.COM never runs the lines
rem after the nt label: the goto run before them jumps over them. Every line
rem from the run label on is COMMAND.COM syntax. ASCII, CRLF line endings.
if "%OS%"=="Windows_NT" goto nt
goto run
:nt
echo silent-off.bat: Windows 98 SE and ME only; nothing was changed.
goto end
:run
echo silent-off.bat: the silent install off, XhciSilentInstall 0.
if not exist silent.vbs goto nofile
echo Setting the switch to 0:
cscript //nologo silent.vbs 0
if errorlevel 5 goto unverified
if errorlevel 3 goto partial
if errorlevel 1 goto notset
goto list
:unverified
echo The switch may have changed on some controllers and could not be read
echo back. Run silent-off.bat again, or check XhciSilentInstall in regedit.
goto list
:partial
echo The switch was set back to 0 on some controllers and not others. Run
echo silent-off.bat again.
goto list
:notset
echo The switch was not set back to 0.
:list
echo.
echo The list was left in place: another USB stack may read the same key
echo (SweetLow's, whose list SILENT.REG's values are). To delete it too, if
echo nothing else uses it: in regedit, under
echo HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB, delete the
echo SilentClasses key, and restart.
goto end
:nofile
echo silent.vbs was not found in this directory. Run silent-off.bat from
echo the download's TOOLS\SILENT directory, beside silent.vbs: double-click
echo it there, or change to that directory at the prompt first.
:end
echo.
pause
