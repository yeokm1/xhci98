@echo off
rem snapon.bat [LEVEL] - turns the xhci98 log channel on: runs XHCISNAP
rem -verbosity LEVEL, which sets XhciLogVerbosity on every xhci98
rem controller (roadmap-hcd task 38.1). LEVEL is 1 to 4, and 2 when none is
rem given - 2 is the log, and what to use unless the maintainer asks for
rem another. 0 is refused: it is off, and SNAPOFF.BAT is what turns the
rem channel off. It is in the download's tools\xhcisnap directory, beside
rem XHCISNAP.EXE, SNAPGET.BAT and SNAPOFF.BAT.
rem
rem Windows 98 SE and ME: double-click it, or run it at a prompt in this
rem directory (SNAPON 3 for level 3). 2000 and XP: run it as an
rem administrator. Vista and 7: right-click it and choose Run as
rem administrator, or run it at an administrator's prompt for a level.
rem
rem One file for COMMAND.COM and cmd.exe. COMMAND.COM never runs the line
rem after the nt label: the goto run before it jumps over it. Every line
rem from the run label on is COMMAND.COM syntax. The level is checked here
rem against 1 to 4 one by one, so that 00 or 2x never reaches the tool as
rem a level. ASCII, CRLF line endings.
if "%OS%"=="Windows_NT" goto nt
goto run
:nt
cd /d "%~dp0"
:run
echo snapon.bat: the xhci98 log channel on, XHCISNAP -verbosity.
echo 2000 and XP: run it as an administrator. Vista and 7: right-click it
echo and choose Run as administrator.
if not exist XHCISNAP.EXE goto nofile
if "%1"=="" goto deflt
if "%1"=="0" goto zero
if "%1"=="1" goto level
if "%1"=="2" goto level
if "%1"=="3" goto level
if "%1"=="4" goto level
goto badlvl
:deflt
XHCISNAP -verbosity 2
goto ran
:level
XHCISNAP -verbosity %1
:ran
if errorlevel 1 goto refused
echo.
echo The channel is on. Next:
echo   1. Restart Windows, or disable and enable the xHCI controller in
echo      Device Manager.
echo   2. Reproduce the problem.
echo   3. Run SNAPGET.BAT, in this directory.
goto end
:zero
echo.
echo Level 0 is off, so snapon.bat does not take it: run SNAPOFF.BAT to
echo turn the channel off. Nothing was changed.
goto end
:badlvl
echo.
echo That is not a level snapon.bat takes. Give 1, 2, 3 or 4, or nothing
echo for 2, the log: SNAPON or SNAPON 3. Nothing was changed.
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
