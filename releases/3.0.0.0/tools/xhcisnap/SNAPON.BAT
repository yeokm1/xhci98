@echo off
rem snapon.bat [LEVEL] - turns the xhci98 log channel on: runs XHCISNAP
rem -on LEVEL, which sets XhciLogVerbosity on every xhci98 controller as
rem -verbosity does (roadmap-hcd task 38.1). LEVEL is 1 to 4, and 2 when none
rem is given - 2 is the log, and what to use unless the maintainer asks for
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
rem from the run label on is COMMAND.COM syntax. The word given is passed on
rem as it came and tested by XHCISNAP alone, never by an if here, so a
rem quoted word or one with a cmd.exe character in it reaches the tool to
rem be refused (Codex review of 38.1, round 3). XHCISNAP runs last and
rem prints the outcome itself, its last line what to attach, so nothing
rem but pause follows it (owner, 2026-10-13). ASCII, CRLF line endings.
if "%OS%"=="Windows_NT" goto nt
goto run
:nt
cd /d "%~dp0"
:run
echo snapon.bat runs XHCISNAP -on to turn the xhci98 log channel on.
echo 2000 and XP: run it as an administrator. Vista and 7: right-click it
echo and choose Run as administrator.
if exist XHCISNAP.EXE goto tool
echo XHCISNAP.EXE was not found in this directory. Run snapon.bat from
echo the download's TOOLS\XHCISNAP directory, beside XHCISNAP.EXE: double-
echo click it there, or change to that directory at the prompt first.
echo Nothing was changed, and there is nothing to attach.
goto end
:tool
XHCISNAP -on %1
:end
pause
