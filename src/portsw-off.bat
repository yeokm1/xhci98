@echo off
rem portsw-off.bat - turns the Intel port switchover off: sets
rem XhciIntelPortSwitch to 0 on every xhci98 controller through portsw.vbs,
rem which is beside it (roadmap-hcd task 36.56, design record 16 section 5a).
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
echo portsw-off.bat turns the Intel port switchover off (XhciIntelPortSwitch 0).
echo Windows 2000 and later: run it as an administrator. Vista and 7:
echo right-click it and choose Run as administrator.
echo.
if not exist portsw.vbs goto nofile
echo Now:
cscript //nologo portsw.vbs show
if errorlevel 1 goto end
echo Setting it to 0:
cscript //nologo portsw.vbs 0
if errorlevel 3 goto elevate
if errorlevel 1 goto refused
echo RESTART WINDOWS for the change to take effect.
goto end
:elevate
echo Windows asked for administrator rights, and the result is shown in
echo the window that opened. If it says the value was set, restart
echo Windows. Next time, right-click portsw-off.bat and choose Run as
echo administrator.
goto end
:refused
echo Nothing was set. Run portsw-off.bat as an administrator: on Vista
echo and 7, right-click it and choose Run as administrator.
goto end
:nofile
echo portsw.vbs was not found in this directory. Run portsw-off.bat from
echo the package directory that holds portsw.vbs: double-click it there,
echo or change to that directory at the prompt first.
:end
echo.
pause
