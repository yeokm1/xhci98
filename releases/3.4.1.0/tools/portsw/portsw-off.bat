@echo off
rem portsw-off.bat - turns the Intel port switchover off: sets
rem XhciIntelPortSwitch to 0 machine-wide through portsw.vbs,
rem which is beside it in the download's tools\portsw directory (roadmap-hcd
rem tasks 36.56, 37.5 and 38.9, design record 16 section 5a), in
rem HKLM\System\CurrentControlSet\Services\USB, which every controller
rem reads first; the controllers' own values are left alone (roadmap-usb4
rem task 42-A.17).
rem
rem Windows 98 SE and ME: double-click it, or run it at a prompt in this
rem directory. 2000 and XP: run it as an administrator. Vista and 7:
rem right-click it and choose Run as administrator. Then restart Windows.
rem
rem One file for COMMAND.COM and cmd.exe. COMMAND.COM never runs the line
rem after the nt label: the goto run before it jumps over it. Every line
rem from the run label on is COMMAND.COM syntax. ASCII, CRLF line endings.
if "%OS%"=="Windows_NT" goto nt
goto run
:nt
cd /d "%~dp0"
:run
echo portsw-off.bat: the Intel port switchover off, XhciIntelPortSwitch 0.
echo 2000 and XP: run it as an administrator. Vista and 7: right-click it
echo and choose Run as administrator.
if not exist portsw.vbs goto nofile
echo Now:
cscript //nologo portsw.vbs show
if errorlevel 1 goto end
echo Setting it to 0 machine-wide, for every xhci98 controller:
cscript //nologo portsw.vbs 0
if errorlevel 3 goto elevate
if errorlevel 1 goto refused
goto end
:elevate
echo Windows asked for administrator rights. If you allowed it, the
echo result is shown in the window that opened; if it says the value was
echo set, restart Windows. If you did not, nothing was changed. Next time,
echo right-click portsw-off.bat and choose Run as administrator.
goto end
:refused
echo The change did not complete. Read the lines above: they say what
echo was written and what was read back.
if not "%OS%"=="Windows_NT" goto end
echo Run portsw-off.bat as an administrator: on Vista and 7, right-click
echo it and choose Run as administrator.
goto end
:nofile
echo portsw.vbs was not found in this directory. Run portsw-off.bat from
echo the download's TOOLS\PORTSW directory, beside portsw.vbs: double-click
echo it there, or change to that directory at the prompt first.
:end
echo.
pause
