@echo off
rem selftest.cmd - drive XHCISNAP.EXE's report path with no controller present.
rem
rem The 2026-09-05 audit (roadmap Phase 20, F5, F17) found the tool reporting a
rem text report as written on the strength of fopen alone: every fprintf and
rem the fclose were unchecked, so a full or removed destination left a
rem truncated .TXT that the summary told the user to send, exit 0. The tool now
rem tracks write and close failures and answers exit 3 with an INCOMPLETE line.
rem A real full disk is not a repeatable test, so `-selftest-report BASE` opens
rem BASE.TXT through the same writers and the same finish as the dump, and the
rem XHCISNAP_FAULT environment variable ("write" or "close") makes the named
rem step fail deterministically. This script runs four cases - a clean report,
rem a write fault, a close fault and a .TXT that cannot be created at all - and
rem checks the exit codes and the summary lines. A fifth case checks that
rem `-probe` beside an implied `-dump` is refused before any device is opened
rem (the 2026-09-17 audit's C4), which needs no controller either.
rem
rem Exit code 0 = all five cases behaved. Run it after build.cmd.
setlocal
cd /d "%~dp0"

if not exist XHCISNAP.EXE (
    echo ERROR: XHCISNAP.EXE is not here; run build.cmd first
    exit /b 1
)

rem A directory of this run's own under xhcisnap\out\ (ignored), not a name
rem in %TEMP%: two builds started in the same second draw the same %RANDOM%,
rem so a shared %TEMP% name is one name, and mkdir is the claim on it. Only a
rem name that exists is a collision: any other mkdir failure fails at once,
rem and twenty collisions in a row fail too.
if not exist out mkdir out
if not exist out\ (
    echo ERROR: could not create %~dp0out
    exit /b 1
)
set CLAIMTRIES=0
:claimrundir
set /a CLAIMTRIES+=1
if %CLAIMTRIES% GTR 20 (
    echo ERROR: 20 run directory names under %~dp0out were already taken
    exit /b 1
)
set "RUNDIR=%~dp0out\s%RANDOM%%RANDOM%"
mkdir "%RUNDIR%" 2>nul && goto claimedrundir
if exist "%RUNDIR%" goto claimrundir
echo ERROR: could not create %RUNDIR% (not a name collision: check the
echo directory's permissions and free space)
exit /b 1
:claimedrundir
set "BASE=%RUNDIR%\report"
set FAILED=0

set "XHCISNAP_FAULT="
"%~dp0XHCISNAP.EXE" -selftest-report "%BASE%" > "%BASE%.no-fault.log"
if errorlevel 1 (
    echo FAIL: a report with no fault injected exited %errorlevel%, expected 0
    set FAILED=1
)
if not exist "%BASE%.TXT" (
    echo FAIL: a report with no fault injected left no .TXT
    set FAILED=1
)
findstr /C:"written and closed" "%BASE%.no-fault.log" > nul
if errorlevel 1 (
    echo FAIL: the no-fault summary does not say the report was written and closed
    set FAILED=1
)

set "XHCISNAP_FAULT=write"
"%~dp0XHCISNAP.EXE" -selftest-report "%BASE%" > "%BASE%.write-fault.log"
if not errorlevel 3 (
    echo FAIL: a report whose writes fail exited %errorlevel%, expected 3
    set FAILED=1
)
findstr /C:"INCOMPLETE" "%BASE%.write-fault.log" > nul
if errorlevel 1 (
    echo FAIL: the write-fault summary does not say INCOMPLETE
    set FAILED=1
)

set "XHCISNAP_FAULT=close"
"%~dp0XHCISNAP.EXE" -selftest-report "%BASE%" > "%BASE%.close-fault.log"
if not errorlevel 3 (
    echo FAIL: a report whose close fails exited %errorlevel%, expected 3
    set FAILED=1
)
findstr /C:"INCOMPLETE" "%BASE%.close-fault.log" > nul
if errorlevel 1 (
    echo FAIL: the close-fault summary does not say INCOMPLETE
    set FAILED=1
)
set "XHCISNAP_FAULT="

rem A .TXT that cannot be created: the dump's own fopen-failure branch used to
rem exit 0 with the report on screen (Phase 20 review, finding 6).
attrib +R "%BASE%.TXT"
"%~dp0XHCISNAP.EXE" -selftest-report "%BASE%" > "%BASE%.readonly.log"
if not errorlevel 3 (
    echo FAIL: a report whose .TXT could not be created exited %errorlevel%, expected 3
    set FAILED=1
)
findstr /C:"NOT CREATED" "%BASE%.readonly.log" > nul
if errorlevel 1 (
    echo FAIL: the read-only summary does not say NOT CREATED
    set FAILED=1
)
attrib -R "%BASE%.TXT"

rem -probe with an implied -dump: refused as a usage error, no device opened,
rem no file named after the -o argument created.
"%~dp0XHCISNAP.EXE" -probe -o "%BASE%-probe" > "%BASE%.probe-dump.log"
if not "%errorlevel%"=="2" (
    echo FAIL: -probe beside -o exited %errorlevel%, expected 2
    set FAILED=1
)
findstr /C:"Run them as two commands" "%BASE%.probe-dump.log" > nul
if errorlevel 1 (
    echo FAIL: the -probe -o refusal does not say to run them as two commands
    set FAILED=1
)
if exist "%BASE%-probe.TXT" (
    echo FAIL: -probe beside -o created a .TXT
    set FAILED=1
)

rem The slots region's decode (2.0.0.0): the Slot Context Speed in words over
rem canned records - SuperSpeed with its rate and lanes, SuperSpeedPlus by its
rem rate, High and Low Speed, and a slot whose context could not be read.
"%~dp0XHCISNAP.EXE" -selftest-slots > "%BASE%.slots.log"
if errorlevel 1 (
    echo FAIL: -selftest-slots exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("SuperSpeed, 5 Gbit/s, Gen 1x1" "SuperSpeedPlus, 10 Gbit/s, Gen 2x1" "configured     3  High Speed" "addressed      2  Low Speed" "(controller not started)" "5 device(s) hold a slot") do (
    findstr /C:%%S "%BASE%.slots.log" > nul
    if errorlevel 1 (
        echo FAIL: the slot decode does not say %%S
        set FAILED=1
    )
)

rd /s /q "%RUNDIR%" 2> nul
rd out 2> nul

if "%FAILED%"=="1" (
    echo xhcisnap selftest FAILED
    exit /b 1
)
echo xhcisnap selftest: 6 cases, all passed
exit /b 0
