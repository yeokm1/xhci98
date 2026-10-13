@echo off
rem snapoff.bat - turns the xhci98 log channel off: runs XHCISNAP -disable,
rem which sets XhciLogVerbosity back to 0, what the INF ships, on every
rem xhci98 controller (roadmap-hcd task 38.1). Run it once the report from
rem SNAPGET.BAT has been sent. It is in the download's tools\xhcisnap
rem directory, beside XHCISNAP.EXE.
rem
rem Windows 98 SE and ME: double-click it, or run it at a prompt in this
rem directory. 2000 and XP: run it as an administrator. Vista and 7:
rem right-click it and choose Run as administrator.
rem
rem One file for COMMAND.COM and cmd.exe. COMMAND.COM never runs the line
rem after the nt label: the goto run before it jumps over it. Every line
rem from the run label on is COMMAND.COM syntax. ASCII, CRLF line endings.
if "%OS%"=="Windows_NT" goto nt
goto run
:nt
cd /d "%~dp0"
:run
echo snapoff.bat: the xhci98 log channel off, XHCISNAP -disable.
echo 2000 and XP: run it as an administrator. Vista and 7: right-click it
echo and choose Run as administrator.
if not exist XHCISNAP.EXE goto nofile
XHCISNAP -disable
if errorlevel 1 goto refused
echo.
echo The channel is off from the next start: restart Windows, or disable
echo and enable the xHCI controller in Device Manager, for it to take effect.
goto end
:refused
echo The channel was NOT switched off. Read the lines above: some
echo controllers may already have been changed.
if not "%OS%"=="Windows_NT" goto end
echo Run snapoff.bat as an administrator: on Vista and 7, right-click
echo it and choose Run as administrator.
goto end
:nofile
echo XHCISNAP.EXE was not found in this directory. Run snapoff.bat from
echo the download's TOOLS\XHCISNAP directory, beside XHCISNAP.EXE: double-
echo click it there, or change to that directory at the prompt first.
:end
echo Nothing to attach: snapoff.bat writes no file.
pause
