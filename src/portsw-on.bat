@echo off
rem portsw-on.bat - turns the Intel port switchover on: sets
rem XhciIntelPortSwitch to 1 on every xhci98 controller through portsw.vbs,
rem which is beside it in the download's portsw directory (roadmap-hcd
rem tasks 36.56 and 37.5, design record 16 section 5a).
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
echo portsw-on.bat: the Intel port switchover on, XhciIntelPortSwitch 1.
echo 2000 and XP: run it as an administrator. Vista and 7: right-click it
echo and choose Run as administrator.
if not exist portsw.vbs goto nofile
echo Now:
cscript //nologo portsw.vbs show
if errorlevel 1 goto end
echo Setting it to 1:
cscript //nologo portsw.vbs 1
if errorlevel 3 goto elevate
if errorlevel 1 goto refused
goto end
:elevate
echo Windows asked for administrator rights. If you allowed it, the
echo result is shown in the window that opened; if it says the value was
echo set, restart Windows. If you did not, nothing was changed. Next time,
echo right-click portsw-on.bat and choose Run as administrator.
goto end
:refused
echo The change did not complete. Read the lines above: some controllers
echo may already have been changed.
if not "%OS%"=="Windows_NT" goto end
echo Run portsw-on.bat as an administrator: on Vista and 7, right-click
echo it and choose Run as administrator.
goto end
:nofile
echo portsw.vbs was not found in this directory. Run portsw-on.bat from
echo the download's PORTSW directory, beside portsw.vbs: double-click it
echo there, or change to that directory at the prompt first.
:end
echo.
pause
