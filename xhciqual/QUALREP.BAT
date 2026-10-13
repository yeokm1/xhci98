@echo off
rem QUALREP.BAT - one XHCIQUAL reading for a field report (roadmap task 38.1).
rem Read-only: XHCIQUAL --probe-only makes no PCI configuration write and
rem takes no controller ownership. Safe on a stock MS-DOS 7.x boot.
rem Run it from the directory that holds it and XHCIQUAL.EXE. XHCIQUAL names
rem the log from the DOS clock, mDDhhmmS.LOG, in this directory, and never
rem overwrites an earlier one. This batch deletes no log.
rem COMMAND.COM built-in commands only: no FIND, no CHOICE.
if not exist XHCIQUAL.EXE goto noexe
if exist QUALREP.FLG del QUALREP.FLG
if exist QUALREP.FLG goto rdonly
echo QUALREP: a read-only XHCIQUAL reading for a field report.
XHCIQUAL --probe-only --no-page --log-stamp --done-flag QUALREP.FLG
rem MS-DOS 7.x COMMAND.COM has no ERRORLEVEL variable: test it at once,
rem highest first, since IF ERRORLEVEL n is true for n or more.
if errorlevel 5 goto other
if errorlevel 4 goto refused
if errorlevel 3 goto other
if errorlevel 2 goto nousb
if errorlevel 1 goto read
goto other
:read
if not exist QUALREP.FLG goto abnormal
del QUALREP.FLG
echo.
echo QUALREP: the reading completed normally. Nothing was changed.
echo Attach the file named on the Log written line above.
goto end
:nousb
if not exist QUALREP.FLG goto abnormal
del QUALREP.FLG
echo.
echo QUALREP: XHCIQUAL found no USB host controller on this machine.
echo Attach the file named on the Log written line above.
goto end
:refused
rem Exit 4 is a refusal before anything was read. With no completion flag
rem either, nothing could be created here: the disk is write-protected or full.
if not exist QUALREP.FLG goto rdonly
del QUALREP.FLG
echo.
echo QUALREP: XHCIQUAL refused before reading anything - see its message.
echo No log was written. Fix what it says, then run QUALREP again.
goto end
:other
if not exist QUALREP.FLG goto abnormal
del QUALREP.FLG
echo.
echo QUALREP: XHCIQUAL ended normally with an unexpected exit code.
echo Attach the file named on the Log written line above.
goto end
:abnormal
echo.
echo ERROR: XHCIQUAL ended abnormally - it wrote no completion flag.
echo Photograph the screen and cold boot. The partial log is kept.
echo Attach the photo and the file named on the Log line near the top.
goto end
:rdonly
echo.
echo ERROR: QUALREP cannot write here (a write-protected or full disk), so no log can be kept.
echo Nothing was read and nothing needs a cold boot.
echo Copy QUALREP.BAT and XHCIQUAL.EXE to a disk with room you can write to, run it there.
goto end
:noexe
echo ERROR: XHCIQUAL.EXE is not in the current directory.
echo Change to the directory holding QUALREP.BAT and XHCIQUAL.EXE, then run it.
:end
pause
