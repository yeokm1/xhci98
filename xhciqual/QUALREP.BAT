@echo off
rem QUALREP.BAT - one XHCIQUAL reading for a field report (roadmap task 38.1).
rem Read-only: XHCIQUAL --probe-only makes no PCI configuration write and
rem takes no controller ownership. Safe on a stock MS-DOS 7.x boot.
rem Run it from the directory that holds it and XHCIQUAL.EXE. XHCIQUAL names
rem the log from the DOS clock, mDDhhmmS.LOG, in this directory, and never
rem overwrites an earlier one. This batch deletes no log.
rem COMMAND.COM built-in commands only: no FIND, no CHOICE.
rem The last line printed names the file to attach (owner, 2026-10-13).
rem XHCIQUAL prints its own ending - Log written, or why it refused - so
rem after it this batch prints only for what XHCIQUAL cannot report: a run
rem stopped by DOS (Abort) and a run that never finished (no flag).
if not exist XHCIQUAL.EXE goto noexe
if exist QUALREP.FLG del QUALREP.FLG
if exist QUALREP.FLG goto stale
XHCIQUAL --probe-only --no-page --log-stamp --done-flag QUALREP.FLG
rem MS-DOS 7.x COMMAND.COM has no ERRORLEVEL variable: test it at once,
rem highest first, since IF ERRORLEVEL n is true for n or more. A DOS/32A
rem fault ends with 255; an Abort answer to DOS's Abort, Retry, Fail ends
rem the program with 0, which --probe-only never returns itself.
if errorlevel 5 goto checked
if errorlevel 4 goto refused
if errorlevel 1 goto checked
goto stopped
:refused
rem Exit 4: refused before anything was read, in XHCIQUAL's own words. A
rem directory it cannot write to has no flag either, so none is required.
if exist QUALREP.FLG del QUALREP.FLG
goto end
:checked
if not exist QUALREP.FLG goto abnormal
del QUALREP.FLG
goto end
:stopped
echo.
echo QUALREP: XHCIQUAL was stopped before it finished, as by an Abort
echo answer to a DOS disk error. Nothing needs a cold boot. If DOS said
echo the disk is write-protected or full, copy QUALREP.BAT and XHCIQUAL.EXE
echo to a disk you can write to and run QUALREP there; else run it again.
echo Attach the newest .LOG in this directory, if there is one.
goto end
:abnormal
echo.
echo ERROR: XHCIQUAL ended abnormally - it wrote no completion flag.
echo Photograph the screen and cold boot. The partial log is kept.
echo Attach the photo and the newest .LOG in this directory, if any.
goto end
:stale
echo ERROR: QUALREP cannot write here: it could not delete QUALREP.FLG,
echo so the disk is write-protected. Nothing was read. Copy QUALREP.BAT
echo and XHCIQUAL.EXE to a disk you can write to and run QUALREP there.
echo No log was written, so there is no file to attach.
goto end
:noexe
echo ERROR: XHCIQUAL.EXE is not in the current directory. Change to the
echo directory holding QUALREP.BAT and XHCIQUAL.EXE, then run QUALREP.
echo No log was written, so there is no file to attach.
:end
pause
