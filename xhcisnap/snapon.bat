@echo off
rem snapon.bat - turns the xhci98 log channel on: runs XHCISNAP -verbosity 2,
rem which sets XhciLogVerbosity to 2 on every xhci98 controller (roadmap-hcd
rem task 38.1). It is in the download's tools\xhcisnap directory, beside
rem XHCISNAP.EXE, SNAPGET.BAT and SNAPOFF.BAT.
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
echo snapon.bat: the xhci98 log channel on, XHCISNAP -verbosity 2.
echo 2000 and XP: run it as an administrator. Vista and 7: right-click it
echo and choose Run as administrator.
if not exist XHCISNAP.EXE goto nofile
XHCISNAP -verbosity 2
if errorlevel 1 goto refused
echo.
echo The channel is on. Next:
echo   1. Restart Windows, or disable and enable the xHCI controller in
echo      Device Manager.
echo   2. Reproduce the problem.
echo   3. Run SNAPGET.BAT, in this directory.
goto end
:refused
echo The channel was not switched on. Read the lines above: some
echo controllers may already have been changed.
if not "%OS%"=="Windows_NT" goto end
echo Run snapon.bat as an administrator: on Vista and 7, right-click
echo it and choose Run as administrator.
goto end
:nofile
echo XHCISNAP.EXE was not found in this directory. Run snapon.bat from
echo the download's TOOLS\XHCISNAP directory, beside XHCISNAP.EXE: double-
echo click it there, or change to that directory at the prompt first.
:end
echo Nothing to attach yet: SNAPGET.BAT writes the files to attach.
pause
