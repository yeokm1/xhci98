@echo off
rem snapoff.bat - turns the xhci98 log channel off: runs XHCISNAP -disable,
rem which sets XhciLogVerbosity to 0 (off; the INF ships 2 since 3.0.0.0) on every
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
rem from the run label on is COMMAND.COM syntax. XHCISNAP runs last and
rem prints the outcome itself, its last line saying there is nothing to
rem attach, so nothing but pause follows it (owner, 2026-10-13). ASCII,
rem CRLF line endings.
if "%OS%"=="Windows_NT" goto nt
goto run
:nt
cd /d "%~dp0"
:run
echo snapoff.bat runs XHCISNAP -disable to turn the xhci98 log channel off.
echo 2000 and XP: run it as an administrator. Vista and 7: right-click it
echo and choose Run as administrator.
if exist XHCISNAP.EXE goto tool
echo XHCISNAP.EXE was not found in this directory. Run snapoff.bat from
echo the download's TOOLS\XHCISNAP directory, beside XHCISNAP.EXE: double-
echo click it there, or change to that directory at the prompt first.
echo Nothing was changed, and there is nothing to attach.
goto end
:tool
XHCISNAP -disable
:end
pause
