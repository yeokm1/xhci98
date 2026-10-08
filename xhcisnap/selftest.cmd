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

rem Task 35.3's root port enumeration notes, decoded over a canned ring: 35.0's
rem E460 failure as 2.1.1.0 met it, the same link with 35.1's fallback, and a
rem spent budget.
"%~dp0XHCISNAP.EXE" -selftest-notes > "%BASE%.notes.log"
if errorlevel 1 (
    echo FAIL: -selftest-notes exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("port 13 look:  machine Empty, fed connect, link none; PORTSC CCS PED PLS 0, changes CSC PLC" "port 13 reset: attempt 0 came back enabled; PORTSC low 1203: speed ID 4" "ID 4 is unknown, by no mapping" "attempt 1 failed on the speed (no EP0 size for it); no retry follows" "port 13 end:   machine Failed" "port 13 look:  machine Failed, fed nothing" "ID 4 is SuperSpeed, by default ID, unlisted on a USB 3 table" "port 14 rate:  5000 Mbit/s" "port  2 rate:  1.5 Mbit/s" "Enable Slot completion 1 (Success), slot 5" "port 14 end:   machine Present" "port 15 quiet: its budget of 8 bursts is spent") do (
    findstr /C:%%S "%BASE%.notes.log" > nul
    if errorlevel 1 (
        echo FAIL: the note decode does not say %%S
        set FAILED=1
    )
)
findstr /C:"tol.mode" "%BASE%.notes.log" > nul
if not errorlevel 1 (
    echo FAIL: the note decode printed a record not its own
    set FAILED=1
)

rem Task 35.3's HCD region decoded over a canned image: a port Failed on the
rem speed, a working SuperSpeed port by the fallback, an empty port left out,
rem and the nonzero counters by name.
"%~dp0XHCISNAP.EXE" -selftest-hcd > "%BASE%.hcd.log"
if errorlevel 1 (
    echo FAIL: -selftest-hcd exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("13  Failed      speed (no EP0 size)" "4  unknown (no mapping)" "notes 5 of 8, 2 refused" "14  Present     none" "4  SuperSpeed (default ID, unlisted on a USB 3 table)" "2 of 3 root ports shown" "SlotsEnabled               4" "PortSpeedSuper             1" "2 of 63 nonzero" "not a shape this build reads" "  17  Failed      speed (no EP0 size)" "1 of 2 root ports shown") do (
    findstr /C:%%S "%BASE%.hcd.log" > nul
    if errorlevel 1 (
        echo FAIL: the HCD region decode does not say %%S
        set FAILED=1
    )
)

rem Task 35-T.8's tolerance state, named from a canned extension image at the
rem place a canned HCD header gives: the values in effect, the controller's
rem terminal reason and window, the counters and completion codes by name, a
rem held root port and a re-armed hub port; then a 35.3 header, a state past
rem the image, and no image at all.
"%~dp0XHCISNAP.EXE" -selftest-tol > "%BASE%.tol.log"
if errorlevel 1 (
    echo FAIL: -selftest-tol exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("XhciTolerance    1 (on)" "XhciIntervalCap  1 (AMD controllers only): applies here, 2 endpoint(s) capped" "XhciAvgTrbEsit   0 (Average TRB Length 1024)" "controller       LATCHED FAILED: the recovery window refused a fourth" "3 of 3 begun inside ten minutes; 1 refused" "begun 300, 200, 100 ticks ago" "BackstopDrains            3" "RetryDiverts              4" "CyclesRefusedCode         2" "    4 USB Transaction Error        5" "   12 Endpoint Not Enabled         2" "  200 vendor-defined error         1" "root port 4        charges 3: re-enumerations 3 of 3" "NOW HELD, re-enumerations spent (powered)" "hub 1 port 2       charges 1" "not served by this driver (an xhci98.sys before 2.2.0.0's" "not a shape this build" "controller: LATCHED FAILED: the recovery window refused a fourth." "controller: CONTAINED: halt and reset did not complete and Bus Master Enable would not clear; DMA not proven stopped, common buffer pinned.") do (
    findstr /C:%%S "%BASE%.tol.log" > nul
    if errorlevel 1 (
        echo FAIL: the tolerance decode does not say %%S
        set FAILED=1
    )
)
for %%S in ("2 of 3 begun inside ten minutes; 0 refused" "begun 512, 176 ticks ago" "1 older stamp(s) kept, past the window and not counted") do (
    findstr /C:%%S "%BASE%.tol.log" > nul
    if errorlevel 1 (
        echo FAIL: the expired window decode does not say %%S
        set FAILED=1
    )
)
findstr /C:"QueueHalts" "%BASE%.tol.log" > nul
if not errorlevel 1 (
    echo FAIL: the tolerance decode printed a zero counter
    set FAILED=1
)

rem The diagnostic build 2.2.0.2's parts of the HCD region (roadmap-hcd.md
rem task 36.1): a bulk IN transfer on the ring of a Halted endpoint whose TR
rem Dequeue is that TD, the thread's own EP0 transfer heading its queue with
rem its TD wrapped past the Link TRB, the recovery timeline in order with a
rem stalled CLEAR_FEATURE, a request not taken, one held, a streams status
rem and the commands by name, backstop samples with IP clear and an event
rem waiting and refused insertions counted; then a 2.2.0.0 header, which
rem keeps none of it, and a placement past the image.
"%~dp0XHCISNAP.EXE" -selftest-diag > "%BASE%.diag.log"
if errorlevel 1 (
    echo FAIL: -selftest-diag exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("ISR last entered 2.0 s ago, last claimed 89.9 s ago" "ISR entered 1.0 s ago, claimed never, DPC 0.5 s ago" "waiting with IP CLEAR 130" "the TRB there: Command Completion, cycle 1 - AN EVENT IS WAITING" "the first RESET_PIPE (timeline #9)" "slot 2 DCI 3, EP 81 bulk IN: seq-used" "Endpoint Context: HALTED, TR Dequeue 0000000001000260, DCS 1; slot Configured" "record seq 50, ON THE RING; URB BULK_OR_INTERRUPT_TRANSFER (0009h), TransferFlags 3 (IN, SHORT_OK)" "the controller's TR Dequeue is AT this TD's first TRB" "(3 of 4 open endpoint pipes listed; 2 more with a transfer outstanding" "the THREAD'S OWN EP0 control transfer, ON THE RING, token 7" "SETUP 02 01 00 00 81 00 00 00" "TD 02000FF0 to 02000010 (wrapped past the Link TRB); the controller's TR Dequeue undefined in state Running (stale value)" "TD 03000FE0 to 03000020 (wrapped past the Link TRB); the controller's TR Dequeue is INSIDE this TD" "Endpoint Context: Stopped, TR Dequeue 0000000003000010" "USBD 00010000 (CANCELED), an earlier abort covered it" "done not yet (HELD, not completed)" "streams status 4 (failed)" "7 drain(s) not queued" "commands 2: Stop Endpoint=Success Set TR Dequeue=Context State Error" "CLEAR_FEATURE(ENDPOINT_HALT) STALLED by the device" "(NOT TAKEN YET)" "record seq 50, ON THE RING: marked for the thread" "(7 entries kept of 10 written.)" "IMAN 00000002 (IP 0, IE 1)" "(2 kept of 133 queued.)" "an xhci98.sys before 2.2.0.2" "in records smaller than this build knows; not decoded.") do (
    findstr /C:%%S "%BASE%.diag.log" > nul
    if errorlevel 1 (
        echo FAIL: the diagnostic decode does not say %%S
        set FAILED=1
    )
)
findstr /N /C:"  #7 " /C:"  #8 " /C:"  #9 " /C:"  #10 " "%BASE%.diag.log" > "%BASE%.diag-order.log"
for /f "tokens=1 delims=:" %%L in ('findstr /C:"  #7 " "%BASE%.diag-order.log"') do set "ORD7=%%L"
for /f "tokens=1 delims=:" %%L in ('findstr /C:"  #10 " "%BASE%.diag-order.log"') do set "ORD10=%%L"
if not defined ORD7 (
    echo FAIL: the diagnostic decode lost timeline entry #7
    set FAILED=1
) else if not defined ORD10 (
    echo FAIL: the diagnostic decode lost timeline entry #10
    set FAILED=1
) else if %ORD7% GEQ %ORD10% (
    echo FAIL: the timeline is not printed oldest first
    set FAILED=1
)

rd /s /q "%RUNDIR%" 2> nul
rd out 2> nul

if "%FAILED%"=="1" (
    echo xhcisnap selftest FAILED
    exit /b 1
)
echo xhcisnap selftest: 10 cases, all passed
exit /b 0
