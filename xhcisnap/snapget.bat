@echo off
rem snapget.bat - takes the field report: runs XHCISNAP -report, which writes
rem a device report (the .DEV: the xHCI controllers' Device Manager status,
rem problem code and resources, and the installed files' versions and
rem SHA-256) and then the dump (.TXT, .BIN, .PSC), all named from the clock
rem in this directory, never over an earlier run's (roadmap-hcd task 38.1).
rem The log channel is left on; SNAPOFF.BAT turns it off.
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
echo snapget.bat: the device report and the dump, XHCISNAP -report.
echo 2000 and XP: run it as an administrator. Vista and 7: right-click it
echo and choose Run as administrator.
if not exist XHCISNAP.EXE goto nofile
XHCISNAP -report
if errorlevel 8 goto old
if errorlevel 7 goto clock
if errorlevel 6 goto full
if errorlevel 5 goto devbad
if errorlevel 4 goto nodump
if errorlevel 3 goto txtbad
if errorlevel 1 goto old
echo.
echo The device report and the dump were written.
goto attach
:txtbad
echo.
echo The dump's .TXT is not complete: read the lines above, and say so
echo when you attach it.
goto attach
:nodump
echo.
echo No dump was taken; the .DEV says why. If the log channel is off, run
echo SNAPON.BAT, restart Windows, reproduce the problem, then run this again.
goto attach
:devbad
echo.
echo The .DEV could not be written in full: the disk may be full or read-
echo only. Copy this directory somewhere you can write to and run it there.
if not "%OS%"=="Windows_NT" goto attach
echo On Vista and 7, right-click it and choose Run as administrator.
goto attach
:full
echo.
echo All 26 names for this minute are taken here. Wait a minute and run
echo SNAPGET.BAT again; nothing was written or overwritten.
goto end
:clock
echo.
echo The clock does not give a valid date and time. Set it, then run
echo SNAPGET.BAT again; nothing was written.
goto end
:old
echo.
echo XHCISNAP did not take the report. It may be older than SNAPGET.BAT:
echo use the XHCISNAP.EXE from the same download.
goto end
:nofile
echo XHCISNAP.EXE was not found in this directory. Run snapget.bat from
echo the download's TOOLS\XHCISNAP directory, beside XHCISNAP.EXE: double-
echo click it there, or change to that directory at the prompt first.
echo Nothing was written, so there is nothing to attach.
goto stop
:attach
echo Attach the files XHCISNAP named above to your report: the .DEV and
echo the .TXT, and the .BIN and .PSC only if the maintainer asks for them.
echo The log channel stays on until you run SNAPOFF.BAT.
goto stop
:end
echo Attach the files XHCISNAP named above, if it named any. The log
echo channel stays on until you run SNAPOFF.BAT.
:stop
pause
