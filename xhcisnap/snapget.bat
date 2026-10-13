@echo off
rem snapget.bat [NAME] - takes the field report: runs XHCISNAP -report,
rem which writes a device report (the .DEV: the xHCI controllers' Device
rem Manager status, problem code and resources, and the installed files'
rem versions and SHA-256) and then the dump (.TXT, .BIN, .PSC), in this
rem directory, never over an earlier run's (roadmap-hcd task 38.1). With no
rem NAME the files are named from the clock; with one (SNAPGET MYPC, run as
rem XHCISNAP -report -o MYPC) they are MYPC.DEV and so on: 1 to 8 letters,
rem digits, _ or -, with no path, drive or extension, and refused, not
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
if "%1"=="" goto stamp
XHCISNAP -report -o %1
goto ran
:stamp
XHCISNAP -report
:ran
if errorlevel 9 goto old
if errorlevel 8 goto clash
if errorlevel 7 goto clock
if errorlevel 6 goto full
if errorlevel 5 goto devbad
if errorlevel 4 goto nodump
if errorlevel 3 goto txtbad
if errorlevel 2 goto badname
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
if not "%1"=="" goto taken
echo All 26 names for this minute are taken here. Wait a minute and run
echo SNAPGET.BAT again; nothing was written or overwritten.
goto end
:taken
echo A file of that name is here already. It was left as it was and
echo nothing was written: run SNAPGET.BAT with another name, or none.
goto end
:clash
echo.
echo Another run made a file of this set while it ran. That file was left
echo as it was and no dump was kept under this name.
if not "%1"=="" goto clashn
echo Run SNAPGET.BAT again: it takes the next letter.
goto end
:clashn
echo Run SNAPGET.BAT again with another name, or none.
goto end
:clock
echo.
echo The clock does not give a valid date and time. Set it, then run
echo SNAPGET.BAT again; nothing was written.
goto end
:badname
if "%1"=="" goto old
echo.
echo That name was refused; the line above says why. Give 1 to 8 letters,
echo digits, _ or -, with no path, drive or extension (SNAPGET MYPC), or
echo no name. Nothing was written.
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
