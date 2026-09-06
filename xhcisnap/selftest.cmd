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
rem step fail deterministically. This script runs the three cases and checks
rem the exit codes and the summary lines.
rem
rem Exit code 0 = all three cases behaved. Run it after build.cmd.
setlocal
cd /d "%~dp0"

if not exist XHCISNAP.EXE (
    echo ERROR: XHCISNAP.EXE is not here; run build.cmd first
    exit /b 1
)

set "BASE=%TEMP%\xhcisnap-selftest-%RANDOM%"
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

del /q "%BASE%.TXT" "%BASE%.no-fault.log" "%BASE%.write-fault.log" "%BASE%.close-fault.log" "%BASE%.readonly.log" 2> nul

if "%FAILED%"=="1" (
    echo xhcisnap selftest FAILED
    exit /b 1
)
echo xhcisnap selftest: 4 cases, all passed
exit /b 0
