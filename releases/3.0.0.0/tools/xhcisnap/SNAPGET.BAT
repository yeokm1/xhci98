@echo off
rem snapget.bat [NAME] - takes the field report: runs XHCISNAP -report,
rem which writes a device report (the .DEV: the xHCI controllers' Device
rem Manager status, problem code and resources, and the installed files'
rem versions and SHA-256) and then the dump (.TXT, .BIN, .PSC), in this
rem directory, never over an earlier run's (roadmap-hcd task 38.1). With no
rem NAME the files are named from the clock; with one (SNAPGET MYPC, run as
rem XHCISNAP -report MYPC) they are MYPC.DEV and so on: 1 to 8 letters,
rem digits, _ or -, not starting with -, with no path, drive or extension,
rem not a DOS device name (CON PRN AUX NUL COM0-9 LPT0-9), and refused, not
rem overwritten, when a file of that name is here already. The log channel
rem is left on; SNAPOFF.BAT turns it off.
rem
rem Windows 98 SE and ME: double-click it, or run it at a prompt in this
rem directory. 2000 and XP: run it as an administrator. Vista and 7:
rem right-click it and choose Run as administrator, or run it at an
rem administrator's prompt to give a name.
rem
rem One file for COMMAND.COM and cmd.exe. COMMAND.COM never runs the line
rem after the nt label: the goto run before it jumps over it. Every line
rem from the run label on is COMMAND.COM syntax. The word given is passed on
rem as it came and tested by XHCISNAP alone, never by an if here, so a
rem quoted word or one with a cmd.exe character in it reaches the tool to
rem be refused (Codex review of 38.1, round 3). XHCISNAP runs last and
rem prints the outcome itself, its last line the files to attach, so
rem nothing but pause follows it (owner, 2026-10-13). ASCII, CRLF line
rem endings.
if "%OS%"=="Windows_NT" goto nt
goto run
:nt
cd /d "%~dp0"
:run
echo snapget.bat runs XHCISNAP -report for the device report (.DEV) and the
echo dump (.TXT, .BIN and .PSC). 2000 and XP: run it as an administrator.
echo Vista and 7: right-click it and choose Run as administrator.
if exist XHCISNAP.EXE goto tool
echo XHCISNAP.EXE was not found in this directory. Run snapget.bat from
echo the download's TOOLS\XHCISNAP directory, beside XHCISNAP.EXE: double-
echo click it there, or change to that directory at the prompt first.
echo Nothing was written, so there is nothing to attach.
goto end
:tool
XHCISNAP -report %1
:end
pause
