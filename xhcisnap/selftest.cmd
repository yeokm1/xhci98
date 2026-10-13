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
rem The cases after those drive each decode over canned data, and the last
rem (task 36.47) the system block, which must not carry the computer or user
rem name.
rem
rem A case that expects exit 0 tests for exactly 0: a crash exits with a
rem negative code (an access violation is C0000005h), which IF ERRORLEVEL 1
rem does not see, and the output checks after it could still pass on what
rem was written before the crash.
rem
rem Exit code 0 = every case behaved. Run it after build.cmd.
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
if not "%errorlevel%"=="0" (
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
if not "%errorlevel%"=="0" (
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
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-notes exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("port 13 look:  machine Empty, fed connect, link none; PORTSC CCS PED PLS 0, changes CSC PLC" "port 13 reset: attempt 0 came back enabled; PORTSC low 1203: speed ID 4" "ID 4 is unknown, by no mapping" "attempt 1 failed on the speed (no EP0 size for it); no retry follows" "port 13 end:   machine Failed" "port 13 look:  machine Failed, fed nothing" "ID 4 is SuperSpeed, by default ID, unlisted on a USB 3 table" "port 14 prwait: a reset already running ended after 70 ms" "port 14 rate:  5000 Mbit/s" "port  2 rate:  1.5 Mbit/s" "Enable Slot completion 1 (Success), slot 5" "port 14 end:   machine Present" "port 15 quiet: its budget of 8 bursts is spent") do (
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
if not "%errorlevel%"=="0" (
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
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-tol exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("XhciTolerance    1 (on)" "XhciIntervalCap  1 (AMD controllers and 1002:7316):" "                   applies here, 2 endpoint(s) capped" "XhciAvgTrbEsit   0 (Average TRB Length 1024)" "controller       LATCHED FAILED: the recovery window refused a fourth" "3 of 3 begun inside ten minutes; 1 refused" "begun 300, 200, 100 ticks ago" "BackstopDrains            3" "RetryDiverts              4" "CyclesRefusedCode         2" "    4 USB Transaction Error        5" "   12 Endpoint Not Enabled         2" "  200 vendor-defined error         1" "root port 4        charges 3: re-enumerations 3 of 3" "NOW HELD, re-enumerations spent (powered)" "hub 1 port 2       charges 1" "not served by this driver (an xhci98.sys before 2.2.0.0's" "not a shape this build" "controller: LATCHED FAILED: the recovery window refused a fourth." "controller: CONTAINED: halt and reset did not complete and Bus Master Enable would not clear; DMA not proven stopped, common buffer pinned.") do (
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

rem Task 36.3's interrupt routing record, named from canned HCD headers: the
rem E7250's repair (INTA moved from PIRQA to PIRQF for IRQ 21), a reviewed
rem pair not admitted at value 1, a message grant, IRQ 11 on NT, another vendor, a start
rem refused before the routing step; then a 2.2.0.0 header, an image cut
rem short and a record count the header cannot hold.
"%~dp0XHCISNAP.EXE" -selftest-pirq > "%BASE%.pirq.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-pirq exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("decision         22 WROTE" "D20IR before     00003210 (INTA -> PIRQA)" "PIRQ wanted      PIRQF" "D20IR after      00003215 (INTA -> PIRQF)" "level 21, vector 21, flags 0, share 3" "16-23: PIRQA-H wired direct to I/O APIC inputs 16-23" "a message grant: 1 message(s)," "vector FFFFFFFE, flags 2, share 3" "not read: the routing step has not run for this start" "decision         19 STEERED_NT" "below 16: a PIRQx_ROUT byte steering a PIRQ to it" "PIRQF IRQ 11" "came back 80 bytes" "PIRQA IRQ 11, PIRQB IRQ 10" "PIRQE off" "decision         21 NOT_ADMITTED" "decision         12 MESSAGE" "at a return to D0" "decision         3 NOT_REVIEWED" "00:14.0          id not read" "not set (taken as 1: writes on the verified E7250 only)" "xhci98.sys before 2.3.0.0's" "a record of 40 words") do (
    findstr /C:%%S "%BASE%.pirq.log" > nul
    if errorlevel 1 (
        echo FAIL: the routing decode does not say %%S
        set FAILED=1
    )
)

rem Task 36.21's transfer-size readings from canned HCD headers: a streamed
rem UAS copy at 1 MiB commands over 128 KiB chunks with one class driver
rem record, a value out of range with no clock yet; then a header from before
rem 36.21, an image cut short and a record count the header cannot hold.
"%~dp0XHCISNAP.EXE" -selftest-xstat > "%BASE%.xstat.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-xstat exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("XhciMaxTransferKB           used as set (1024)" "command size in effect      1048576 bytes; asked 1 times" "a chunk spans at most 32 pages" "bulk data URBs              100, 104857600 bytes, the longest 1048576" "<=1M 100" "URBs of more than one 100, the most 8" "3-8: 100" "at 3000.0 counts a" "approximate where their counters disagree" "gap, retired to published   700, mean 90.0 us, longest 200.0 us" "ring empty, URB unfinished  700, mean 80.0 us, longest 180.0 us" "record 1: its device present, slot 2, 9 sends taken, the last 1.0 s before this dump" "command size 1048576 bytes (the bus's answer), streamed, depth 30" "1: 90 2: 14" "record 2: none has come" "out of 64-2048: the default (4096)" "times are not converted" "not measured by this driver" "came back 192 bytes" "9 records of 46 words" "3 records of 1431655795 words" "its device departed, slot 2" "across two processors 3" "older bus 0, out of contract 1, failed 0") do (
    findstr /C:%%S "%BASE%.xstat.log" > nul
    if errorlevel 1 (
        echo FAIL: the transfer-size decode does not say %%S
        set FAILED=1
    )
)

rem Task 36.9's event delivery from canned HCD images: 98 SE forced to
rem poll, a pin-0 controller polled automatically with its periods replaced,
rem a line proven dead and moved, a reserved mode on a header that places no
rem extension words, a driver from before polling, and a block past the image.
"%~dp0XHCISNAP.EXE" -selftest-intr > "%BASE%.intr.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-intr exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("value 2; in effect 2 (polling)" "reason               forced polling (XhciInterruptMode 2)" "XhciImodInterval250ns has no effect" "1234, 56 of them found an event; peeks 78, 9 hits" "49 to 63 ms (2 discarded)" "PollService 1, PollServiceGen 7, LineMoved 0," "Interrupt Pin 0, the controller has no line interrupt" "9000 is above 5000, so the driver held it to 5000." "0 is not allowed, so the driver used its default, 1." "went dead; moved to polling at run time" "mask read back down at the move" "proven by PCI Interrupt Disable; attempts 1" "name different deliveries" "7 is reserved, so it runs as 0." "held to the idle period, 20 ms." "watching the line; last armed 100 ms (a watcher's look)" "not placed by this driver's HCD header (808 bytes)" "predates polled delivery" "does not fit its image" "80 counter words at +816 in 1200 bytes" "no helper and no line offered: the start fails (an earlier 2.3.0.0 build)" "but a message was granted: the start fails" "value 3; in effect 3 (a message interrupt only)" "forced message (XhciInterruptMode 3): a message interrupt" "but no message was granted: the start fails" "is xhcihlpr.sys bound?): the start fails" "this Windows has no message interrupts: the start fails") do (
    findstr /C:%%S "%BASE%.intr.log" > nul
    if errorlevel 1 (
        echo FAIL: the event delivery decode does not say %%S
        set FAILED=1
    )
)

rem Task 36.15's message interrupts from canned HCD images: 7 x86 with the
rem helper bound on MSI, the helper absent and the list stripped, a refused
rem helper, 98 SE with no filter (not applicable), a driver from before MSI
rem and a block between the two sizes.
"%~dp0XHCISNAP.EXE" -selftest-msi > "%BASE%.msi.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-msi exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("the helper xhcihlpr.sys (32-bit; xhcimsi.sys before 2.4)" "4 (the connection's table; one is served); capability MSI" "message descriptors in the grant 1" "capability MSI-X" "MmGetSystemRoutineAddress (64-bit)" "REFUSED (its answer failed a check)" "a message with a line beside it" "BOUND; looks 1" "helper version       2.2.0.0" "not asked (helper bound, or 64-bit, and mode not 1 or 3)" "REPLACED: the line choices stripped, the message left (mode 3)" "not set (mode 0's default)" "no IRP for the query); looks 2, last status C0000034" "REPLACED: the message choices stripped, the line left" "1 configuration(s), 1 message and 1 line member(s); 1 stripped" "mode as read         0" "refused for          the answer's identity" "none has run: not applicable" "messages connected     0 (no message connected)" "this driver predates MSI" "100 counter words - not a shape") do (
    findstr /C:%%S "%BASE%.msi.log" > nul
    if errorlevel 1 (
        echo FAIL: the message interrupt decode does not say %%S
        set FAILED=1
    )
)
rem The 63-word block and the truncated block print nothing of their own:
rem seven sections in all (five decoded, the pre-MSI line, the refused
rem shape).
findstr /C:"[end]" "%BASE%.msi.log" > nul
if errorlevel 1 (
    echo FAIL: the message interrupt selftest did not run to its end
    set FAILED=1
)
set MSICOUNT=0
for /f %%N in ('findstr /C:"message interrupts" "%BASE%.msi.log" ^| find /c /v ""') do set MSICOUNT=%%N
if not "%MSICOUNT%"=="7" (
    echo FAIL: expected 7 message interrupt sections, got %MSICOUNT%
    set FAILED=1
)

rem Every counter word named as the dump prints it (task 36.35's 18-word
rem shift, an MSVC 6.0 /O2 miscompile of an inline two-table lookup): a
rem canned image whose word i holds i + 1, checked word by word against
rem XHCIHC_COUNTERS in src\xhci_counters.h - each field by name and value,
rem any reserved words not printed, the words past the block as
rem "counter N".
"%~dp0XHCISNAP.EXE" -selftest-counters > "%BASE%.counters.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-counters exited %errorlevel%, expected 0
    set FAILED=1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check-counter-names.ps1" -Log "%BASE%.counters.log"
if errorlevel 1 (
    echo FAIL: XHCISNAP's counter names do not match src\xhci_counters.h
    set FAILED=1
)
rem Task 38.11's version line from canned counter words: unwritten, refused
rem before step 0c, QEMU's 1.0, a 0.96 part accepted and refused, the qemu
rem flavour's override, an all-ones register, a 0.95 part, and a block from
rem a driver before 38.11 that prints nothing.
"%~dp0XHCISNAP.EXE" -selftest-version > "%BASE%.version.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-version exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("not read: this start refused before it read XhciAccept096" "HCIVERSION     not read: this start refused before step 0c" "XhciAccept096  absent or not a DWORD, so 1; floor 0.96" "xHCI 1.00: XhciAccept096 not applicable" "xHCI 0.96 (HCIVERSION 0096h): accepted below 1.0; TD Size in 1 KiB units" "XhciAccept096  0; floor 1.00" "xHCI 0.96 (HCIVERSION 0096h): below 1.0, refused at XhciAccept096 0 (Code 10)" "XhciQemuHciVersion decoded 0096h in place of the register's 0100h" "HCIVERSION     FFFFh: not decoding, refused (Code 10)" "xHCI 0.95 (HCIVERSION 0095h): below 0.96, refused at every value (Code 10)" "[short]" "[end]") do (
    findstr /C:%%S "%BASE%.version.log" > nul
    if errorlevel 1 (
        echo FAIL: the version line does not say %%S
        set FAILED=1
    )
)
rem Eight cases print the heading, the short block none: a ninth heading is
rem the length guard printing for a driver from before 38.11.
powershell -NoProfile -Command "if (@(Select-String -SimpleMatch -Path '%BASE%.version.log' -Pattern 'controller version (task 38.11').Count -ne 8) { exit 1 }"
if errorlevel 1 (
    echo FAIL: the version section was not printed exactly once per case, eight
    set FAILED=1
)
rem Task 38.3's finer timer from a canned counter block (design record 15
rem section 7.2.9): a polled 32-bit controller whose helper answered the
rem timer, a barred release, both phases; and a driver from before 38.3,
rem which prints none of it.
"%~dp0XHCISNAP.EXE" -selftest-timer > "%BASE%.timer.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-timer exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("XhciPollFineTimer      not set; in effect ON (the default)" "the helper xhcihlpr.sys (32-bit)" "joined (the controller stopped)" "asks for               10000 (100 ns units); held now no" "holds 12, releases 12 (2 forced by a boundary); holds raced 1" "Windows answered       10032 at the last hold, 156250 at the last release" "a power transition (the controller left D0)" "held in this start     4321 ms" "7 crossed a call" "released  40 interval(s), mean 15.5 ms, 38 late" "held      400 interval(s), mean 1.0 ms, 3 late" "MSI not asked (before Vista); timer accepted") do (
    findstr /C:%%S "%BASE%.timer.log" > nul
    if errorlevel 1 (
        echo FAIL: the timer decode does not say %%S
        set FAILED=1
    )
)
powershell -NoProfile -ExecutionPolicy Bypass -Command "$t = Get-Content -Raw '%BASE%.timer.log'; $i = $t.IndexOf('[pre-38.3]'); if ($i -lt 0 -or $t.Substring($i).Contains('finer system timer')) { exit 1 }; exit 0"
if errorlevel 1 (
    echo FAIL: a counter block from before 38.3 printed the timer section
    set FAILED=1
)

rem Task 38.10's Linux quirks (design record 20 section 6.4) from canned
rem packed words: every normalization, a value listed and not, the policy
rem on, off and overridden, each outcome and its reason, codes past the known
rem ones; the PCI id and revision read and then unread; and a 232-word block
rem (a driver before 38.10), which prints no section. The lines holding a ?
rem are checked outside the FOR, which would take a ? as a file wildcard.
"%~dp0XHCISNAP.EXE" -selftest-quirks > "%BASE%.quirks.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-quirks exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("matched against PCI 1B73:1000; revision 04 (never matched)" "matched against no PCI id (none read); revision unread" "XhciMsiAvoid (XHCI_BROKEN_MSI)" "id listed; policy overridden (XhciInterruptMode 3); last none yet" "read none (absent: the default): mode 1, the listed controllers" "id not listed; policy off; last none yet" "read 2 (as read): mode 2, every controller" "id not listed; policy on; last done" "read 2 (a 2 read as 1): mode 1, the listed controllers" "read none (another type: the default): mode 0, off" "read 7 (an unknown number: the default): mode 1, the listed controllers" "id listed; policy on; last skipped (not in D0)" "read 0 (as read): mode 0, off" "id listed; policy off; last none yet" "XhciAsmediaFlowControl (XHCI_ASMEDIA_MODIFY_FLOWCONTROL)" "id listed; policy on; last failed (read back other than written)" "last failed (reason 15)" "last none yet (no PCI id ever read)" "XhciIsochSpuriousSuccess (XHCI_ETRON_HOST, part)" "id listed; policy on; last skipped (HCH not set)" "1002:7316 (ATI Navi10) has no value: XhciIntervalCap's 1 covers it." "messages XhciMsiAvoid refused or stripped 100" "interrupt endpoints capped at Interval 6  108" "shutdown log flushes not acknowledged     117" "QuirkShutFlushUnacked      117" "[end]") do (
    findstr /C:%%S "%BASE%.quirks.log" > nul
    if errorlevel 1 (
        echo FAIL: the Linux quirks decode does not say %%S
        set FAILED=1
    )
)
findstr /C:"read none (?): mode 3, ?" "%BASE%.quirks.log" > nul
if errorlevel 1 (
    echo FAIL: the Linux quirks decode does not name unknown codes as ?
    set FAILED=1
)
findstr /C:"id not listed; policy off; last ?" "%BASE%.quirks.log" > nul
if errorlevel 1 (
    echo FAIL: the Linux quirks decode does not name an unknown outcome as ?
    set FAILED=1
)
for /f %%N in ('findstr /C:"Linux quirks (task 38.10" "%BASE%.quirks.log" ^| find /c /v ""') do set QKCOUNT=%%N
if not "%QKCOUNT%"=="2" (
    echo FAIL: expected 2 Linux quirks sections, got %QKCOUNT%
    set FAILED=1
)

rem Task 36.24's stage 2 counters from canned HCD headers: the switch on
rem with traffic, the switch absent, Windows 98, a header from before 36.24
rem and a count it cannot hold.
"%~dp0XHCISNAP.EXE" -selftest-xpipe > "%BASE%.xpipe.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-xpipe exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("XhciWideMap                 1" "promotion                   on; the register-free check passed" "wide entries                8, 0 in use; claimed 100" "URBs with two TDs at once   100; TDs 900, refills 700" "short 2, error 0, verdict 0; taken off 2" "next TD ready at retirement 650" "not set (the default)" "promotion                   off; the register-free check not run (XhciWideMap off)" "not run (Windows 98 or ME)" "not the register-free 0x80001" "not in this driver" "3 counters in a 948-byte region" "built 2, refused 1; one-TD URBs 300, the largest 513 TRBs" "TDs of 256/257/512/513 TRBs 100 / 101 / 50 / 49" "stage 3 (task 36.25)        not in this driver") do (
    findstr /C:%%S "%BASE%.xpipe.log" > nul
    if errorlevel 1 (
        echo FAIL: the stage 2 decode does not say %%S
        set FAILED=1
    )
)

rem p36-int4: stage B's header words at 237 to 305 after 36.24's stage 2
rem counters, from a canned header whose word i holds i + 1 (each value
rem names its word), and a stage B branch build's 273-word header refused.
"%~dp0XHCISNAP.EXE" -selftest-ssusp > "%BASE%.ssusp.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-ssusp exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("XhciWideMap                 206" "DNCTRL                     0x00EE (N1, FUNCTION_WAKE, enabled)" "DnctrlEnables              0x00EF" "DnctrlReadback             0x00F0" "   243  RECOVERING" "   299  ?" "a 1092-byte header, a stage B branch build's" "4294967295 counters in a 1092-byte region") do (
    findstr /C:%%S "%BASE%.ssusp.log" > nul
    if errorlevel 1 (
        echo FAIL: the stage B header decode does not say %%S
        set FAILED=1
    )
)

rem Task 36.9's review: a header longer than its image is refused by
rem print_hcd, and no other section decodes anything from it.
"%~dp0XHCISNAP.EXE" -selftest-hcd-bad > "%BASE%.hcdbad.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-hcd-bad exited %errorlevel%, expected 0
    set FAILED=1
)
findstr /C:"not a shape this build" "%BASE%.hcdbad.log" > nul
if errorlevel 1 (
    echo FAIL: the malformed header is not refused
    set FAILED=1
)
for %%S in ("controller tolerance" "interrupt routing" "transfer size" "event delivery" "message interrupts" "stage 2") do (
    findstr /C:%%S "%BASE%.hcdbad.log" > nul
    if not errorlevel 1 (
        echo FAIL: a refused header was still decoded: %%S
        set FAILED=1
    )
)


rem Task 36.47's system block: the OS names over canned GetVersionEx answers,
rem the cleaning of registry text, then this host's own block as a dump's
rem header writes it.
"%~dp0XHCISNAP.EXE" -selftest-sysinfo > "%BASE%.sysinfo.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-sysinfo exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("build 950 product 0 arch 65535: Windows 95" "build 1998 product 0 arch 65535: Windows 98" "build 2222 product 0 arch 65535: Windows 98 SE" "4.90 build 3000 product 0 arch 65535: Windows ME" "build 2195 product 1 arch 65535: Windows 2000" "build 2600 product 1 arch 0: Windows XP" "product 1 arch 9: Windows XP x64" "product 3 arch 0: Windows Server 2003" "product 0 arch 65535: Windows Server 2003 or XP x64" "build 6002 product 1 arch 0: Windows Vista" "build 7601 product 1 arch 9: Windows 7" "product 3 arch 9: Windows Server 2008 R2" "Windows 8 or later" "3.51 build 1057 product 0 arch 65535: unknown" "clean [Intel(R) Core(TM) i7-6500U CPU @ 2.50GHz]" "clean [LENOVO 20F5S0??]" "clean 99 chars, ends [x...]" "clean 98 chars, ends [x...]" "clean [Standard PC (i440FX + PIIX, 1996)]" "  captured on - this machine" "    OS                    Windows" "    bitness               " "    CPU                   " "    RAM                   ") do (
    findstr /C:%%S "%BASE%.sysinfo.log" > nul
    if errorlevel 1 (
        echo FAIL: the system block does not say %%S
        set FAILED=1
    )
)
rem Where an NT machine's lines come from (wave 3 matrix, M-3): NT 6 takes
rem the BIOS key first; NT 5 takes SystemBios values and, with none (XP x64),
rem the BIOS key.
for %%S in ("biossrc nt 6 key 1 systembios 0: the BIOS key" "biossrc nt 6 key 1 systembios 1: the BIOS key" "biossrc nt 6 key 0 systembios 1: SystemBiosVersion" "biossrc nt 6 key 0 systembios 0: none" "biossrc nt 5 key 1 systembios 1: SystemBiosVersion" "biossrc nt 5 key 0 systembios 1: SystemBiosVersion" "biossrc nt 5 key 1 systembios 0: the BIOS key" "biossrc nt 5 key 0 systembios 0: none") do (
    findstr /C:%%S "%BASE%.sysinfo.log" > nul
    if errorlevel 1 (
        echo FAIL: the BIOS source choice does not say %%S
        set FAILED=1
    )
)
rem The withholding over canned names (SNAPTEST-PC7, jdoe, CORPDOM, a
rem non-ASCII name, and PC, which is under three characters and not
rem compared): six strings are withheld (the non-ASCII one in both cases), one of them with the name past the
rem 96-character cut, and the QEMU product string is kept. It is deterministic, so no
rem host name is searched for (Codex review of 36.47, round 1: a host search
rem matched the block's own fixed words and missed values under other labels).
set WITHHELD=0
for /f "delims=" %%L in ('findstr /C:"clean [(withheld: it contains" "%BASE%.sysinfo.log"') do set /a WITHHELD+=1
if not "%WITHHELD%"=="6" (
    echo FAIL: the system block withheld %WITHHELD% canned strings, expected 6
    set FAILED=1
)

rem Task 36.47's -nosysinfo (owner, 2026-10-10): the report path with the
rem option says the details were skipped and prints none of them; without it
rem the same report carries the block; on its own the option is refused.
"%~dp0XHCISNAP.EXE" -selftest-report "%BASE%-sys" > "%BASE%.sys-on.log"
if not "%errorlevel%"=="0" (
    echo FAIL: a report with the system block exited %errorlevel%, expected 0
    set FAILED=1
)
findstr /C:"captured on - this machine" "%BASE%-sys.TXT" > nul
if errorlevel 1 (
    echo FAIL: the report does not carry the system block by default
    set FAILED=1
)
"%~dp0XHCISNAP.EXE" -nosysinfo -selftest-report "%BASE%-nosys" > "%BASE%.sys-off.log"
if not "%errorlevel%"=="0" (
    echo FAIL: a report with -nosysinfo exited %errorlevel%, expected 0
    set FAILED=1
)
findstr /C:"system details were skipped by -nosysinfo" "%BASE%-nosys.TXT" > nul
if errorlevel 1 (
    echo FAIL: the -nosysinfo report does not say the details were skipped
    set FAILED=1
)
for %%S in ("captured on - this machine" "    OS " "    CPU " "    RAM ") do (
    findstr /C:%%S "%BASE%-nosys.TXT" > nul
    if not errorlevel 1 (
        echo FAIL: the -nosysinfo report still says %%S
        set FAILED=1
    )
)
rem -selftest-sysinfo honours the option on either side of it: the live
rem block is skipped, the canned decodes still run.
"%~dp0XHCISNAP.EXE" -selftest-sysinfo -nosysinfo > "%BASE%.sys-after.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-sysinfo -nosysinfo exited %errorlevel%, expected 0
    set FAILED=1
)
"%~dp0XHCISNAP.EXE" -nosysinfo -selftest-sysinfo > "%BASE%.sys-before.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -nosysinfo -selftest-sysinfo exited %errorlevel%, expected 0
    set FAILED=1
)
for %%F in ("%BASE%.sys-after.log" "%BASE%.sys-before.log") do (
    findstr /C:"system details were skipped by -nosysinfo" %%F > nul
    if errorlevel 1 (
        echo FAIL: -selftest-sysinfo with -nosysinfo did not skip the block: %%F
        set FAILED=1
    )
    findstr /C:"    RAM " %%F > nul
    if not errorlevel 1 (
        echo FAIL: -selftest-sysinfo with -nosysinfo still read the system: %%F
        set FAILED=1
    )
    findstr /C:"build 2222 product 0 arch 65535: Windows 98 SE" %%F > nul
    if errorlevel 1 (
        echo FAIL: -selftest-sysinfo with -nosysinfo dropped the canned decodes: %%F
        set FAILED=1
    )
)
"%~dp0XHCISNAP.EXE" -nosysinfo > "%BASE%.sys-alone.log"
if not "%errorlevel%"=="2" (
    echo FAIL: -nosysinfo on its own exited %errorlevel%, expected 2
    set FAILED=1
)

rem Task 38.1's clock-stamped name: months 1, 9, 10, 11 and 12 (one
rem character each, 1-9 then A-C), midnight, a taken name moving A to B
rem whichever file of the set holds it (case ignored, the .TMP names
rem included), names outside the set ignored, Z the last, all 26 taken
rem refused, and impossible clock fields refused with no name.
"%~dp0XHCISNAP.EXE" -selftest-stamp > "%BASE%.stamp.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-stamp exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("stamp month 1 day 5 00:00 free: 1050000A" "stamp month 9 day 30 23:59 free: 9302359A" "stamp month 10 day 1 12:34 free: A011234A" "stamp month 11 day 15 07:08 free: B150708A" "stamp month 12 day 31 08:07 free: C310807A" "stamp A.TXT taken: A011234B" "stamp A.BIN.TMP taken: A011234B" "stamp A.DEV and B.PSC.TMP taken: A011234C" "stamp other names only: A011234A" "stamp A to Y taken: A011234Z" "stamp all 26 taken: REFUSED, all 26 names taken" "stamp month 0: REFUSED, the clock is not valid" "stamp month 13: REFUSED, the clock is not valid" "stamp day 0: REFUSED, the clock is not valid" "stamp day 32: REFUSED, the clock is not valid" "stamp hour 24: REFUSED, the clock is not valid" "stamp minute 60: REFUSED, the clock is not valid" "[end]") do (
    findstr /C:%%S "%BASE%.stamp.log" > nul
    if errorlevel 1 (
        echo FAIL: the stamp rule does not say %%S
        set FAILED=1
    )
)
findstr /C:"but a name came back" "%BASE%.stamp.log" > nul
if not errorlevel 1 (
    echo FAIL: a refused stamp still returned a name
    set FAILED=1
)

rem Task 38.1's SHA-256, on the real /O2 binary (the miscompile lesson of
rem task 36.35): the FIPS 180-2 examples and the empty message in memory,
rem then a million 'a' written to a file and read back through the path the
rem installed files are hashed by, and the file once deleted.
"%~dp0XHCISNAP.EXE" -selftest-sha256 "%BASE%" > "%BASE%.sha.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-sha256 exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("sha256 empty: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 ok" "sha256 abc: ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad ok" "sha256 448-bit: 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1 ok" "sha256 million a, 1000 x 1000: cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0 ok" "sha256 million a, 1000000 x 1: cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0 ok" "sha256 file 1000000 bytes: cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0 ok" "sha256 file deleted: not present" "[end]") do (
    findstr /C:%%S "%BASE%.sha.log" > nul
    if errorlevel 1 (
        echo FAIL: the SHA-256 self-test does not say %%S
        set FAILED=1
    )
)
rem The padding boundaries (55, 56, 63, 64, 65 bytes; 127 and 128 fed in
rem uneven pieces), the length carry across the low word driven through the
rem count and bit-length routines, and the file path's failures: a read
rem that fails, one cut short, a file held exclusively, a directory.
for %%S in ("sha256 55 a: 9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318 ok" "sha256 56 a: b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a ok" "sha256 63 a: 7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34 ok") do (
    findstr /C:%%S "%BASE%.sha.log" > nul
    if errorlevel 1 (
        echo FAIL: the SHA-256 self-test does not say %%S
        set FAILED=1
    )
)
for %%S in ("sha256 64 a: ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb ok" "sha256 64 a, 1+63: ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb ok" "sha256 65 a: 635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0 ok") do (
    findstr /C:%%S "%BASE%.sha.log" > nul
    if errorlevel 1 (
        echo FAIL: the SHA-256 self-test does not say %%S
        set FAILED=1
    )
)
for %%S in ("sha256 127 a, 1+62+64: c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a ok" "sha256 128 a, 63+1+63+1: 6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e ok") do (
    findstr /C:%%S "%BASE%.sha.log" > nul
    if errorlevel 1 (
        echo FAIL: the SHA-256 self-test does not say %%S
        set FAILED=1
    )
)
for %%S in ("sha256 count no carry: 00000000 FFFFFFFF ok" "sha256 count wrap to 0: 00000001 00000000 ok" "sha256 count wrap past: 00000001 00000010 ok" "sha256 count second carry: 00000002 00000000 ok") do (
    findstr /C:%%S "%BASE%.sha.log" > nul
    if errorlevel 1 (
        echo FAIL: the SHA-256 self-test does not say %%S
        set FAILED=1
    )
)
for %%S in ("sha256 bits 512 MiB - 1: 00000000 FFFFFFF8 ok" "sha256 bits 512 MiB: 00000001 00000000 ok" "sha256 bits 4 GiB - 1: 00000007 FFFFFFF8 ok" "sha256 bits 4 GiB: 00000008 00000000 ok" "sha256 length block after the wrap: 00000008 00000000 ok") do (
    findstr /C:%%S "%BASE%.sha.log" > nul
    if errorlevel 1 (
        echo FAIL: the SHA-256 self-test does not say %%S
        set FAILED=1
    )
)
for %%S in ("sha256 file read fails: not read, error 30 ok" "sha256 file read cut short: not read, error 0 ok" "sha256 file held: not read, error 32 ok" "sha256 file directory: not read, error 5 ok") do (
    findstr /C:%%S "%BASE%.sha.log" > nul
    if errorlevel 1 (
        echo FAIL: the SHA-256 self-test does not say %%S
        set FAILED=1
    )
)
findstr /C:"WRONG" "%BASE%.sha.log" > nul
if not errorlevel 1 (
    echo FAIL: the SHA-256 self-test printed a WRONG digest
    set FAILED=1
)

rem Task 38.1's -report, end to end in a directory of its own, against a
rem controller index no machine has: the .DEV is written though the device
rem does not open, says why there is no dump, and exits 4; a second run in
rem the same directory takes another name rather than overwriting the
rem first; -nosysinfo is honoured; a name beside -report that is not an 8.3
rem base is refused, writing nothing.
mkdir "%RUNDIR%\rep"
pushd "%RUNDIR%\rep"
"%~dp0XHCISNAP.EXE" -report -c 99 > "%BASE%.report1.log"
if not "%errorlevel%"=="4" (
    echo FAIL: -report with no controller exited %errorlevel%, expected 4
    set FAILED=1
)
"%~dp0XHCISNAP.EXE" -report -nosysinfo -c 99 > "%BASE%.report2.log"
if not "%errorlevel%"=="4" (
    echo FAIL: -report -nosysinfo with no controller exited %errorlevel%, expected 4
    set FAILED=1
)
"%~dp0XHCISNAP.EXE" -report -o REFUSED.X > "%BASE%.report3.log"
if not "%errorlevel%"=="2" (
    echo FAIL: -report -o REFUSED.X exited %errorlevel%, expected 2
    set FAILED=1
)
set DEVCOUNT=0
for %%F in (*.DEV) do set /a DEVCOUNT+=1
if not "%DEVCOUNT%"=="2" (
    echo FAIL: two -report runs left %DEVCOUNT% .DEV files, expected 2
    set FAILED=1
)
for %%F in (REFUSED.*) do (
    echo FAIL: -report -o wrote %%F
    set FAILED=1
)
copy /b *.DEV "%BASE%.devs.txt" > nul
popd
for %%S in ("xhci98 device report - xhcisnap -report" "  local time         " "  devices - every PCI xHCI controller" "  installed files - size, file version and SHA-256" "    xhci98.sys   " "    usbui.dll    " "  dump result" "NOT taken: cannot open" "HCD99, error" "end of device report" "captured on - this machine" "skipped by -nosysinfo") do (
    findstr /C:%%S "%BASE%.devs.txt" > nul
    if errorlevel 1 (
        echo FAIL: the .DEV files do not say %%S
        set FAILED=1
    )
)
for %%S in ("files named " ".DEV written" "No dump was taken; " "The log channel stays on until you run SNAPOFF.BAT" "Attach: ") do (
    findstr /C:%%S "%BASE%.report1.log" > nul
    if errorlevel 1 (
        echo FAIL: the -report screen does not say %%S
        set FAILED=1
    )
)
findstr /C:"it carries a dot" "%BASE%.report3.log" > nul
if errorlevel 1 (
    echo FAIL: the -report -o REFUSED.X refusal does not say it carries a dot
    set FAILED=1
)
rem Codex review of 38.1, round 1. The screen never names the directory (it
rem is often the user's profile); -nosysinfo before -report is accepted too;
rem and with -nosysinfo the names are not read, so the .DEV prints no free
rem text unscreened - it says so, and the Windows directory is named, not
rem shown.
for %%I in ("%RUNDIR%") do set "RUNNAME=%%~nxI"
for %%F in ("%BASE%.report1.log" "%BASE%.report2.log") do (
    findstr /I /C:"%RUNNAME%" %%F > nul
    if not errorlevel 1 (
        echo FAIL: the -report screen names the current directory: %%F
        set FAILED=1
    )
)
mkdir "%RUNDIR%\rep3"
pushd "%RUNDIR%\rep3"
"%~dp0XHCISNAP.EXE" -nosysinfo -report -c 99 > "%BASE%.report4.log"
if not "%errorlevel%"=="4" (
    echo FAIL: -nosysinfo -report with no controller exited %errorlevel%, expected 4
    set FAILED=1
)
copy /b *.DEV "%BASE%.nosys.txt" > nul
popd
for %%S in ("-nosysinfo: this machine's names were not read, so no free text" "in the Windows directory's System32\Drivers" "skipped by -nosysinfo") do (
    findstr /C:%%S "%BASE%.nosys.txt" > nul
    if errorlevel 1 (
        echo FAIL: the -nosysinfo .DEV does not say %%S
        set FAILED=1
    )
)
findstr /I /C:"%SystemRoot%" "%BASE%.nosys.txt" > nul
if not errorlevel 1 (
    echo FAIL: the -nosysinfo .DEV prints the Windows directory unscreened
    set FAILED=1
)

rem A file of the set made by another run after the name was chosen
rem (XHCISNAP_FAULT=clash makes the .TXT): it survives as it was, the run
rem says so, takes no dump under the name, and exits 8.
mkdir "%RUNDIR%\rep2"
pushd "%RUNDIR%\rep2"
set "XHCISNAP_FAULT=clash"
"%~dp0XHCISNAP.EXE" -report -c 99 > "%BASE%.clash.log"
set CLASHRC=%errorlevel%
set "XHCISNAP_FAULT="
if not "%CLASHRC%"=="8" (
    echo FAIL: -report with a clashing .TXT exited %CLASHRC%, expected 8
    set FAILED=1
)
set TXTCOUNT=0
for %%F in (*.TXT) do set /a TXTCOUNT+=1
if not "%TXTCOUNT%"=="1" (
    echo FAIL: the clash left %TXTCOUNT% .TXT files, expected the other run's 1
    set FAILED=1
)
copy /b *.TXT "%BASE%.clash-txt.txt" > nul
copy /b *.DEV "%BASE%.clash-dev.txt" > nul
for %%F in (*.BIN *.PSC *.TMP) do (
    echo FAIL: the clash left %%F
    set FAILED=1
)
popd
findstr /C:"made by XHCISNAP_FAULT=clash, standing in for another run" "%BASE%.clash-txt.txt" > nul
if errorlevel 1 (
    echo FAIL: the other run's .TXT did not survive the clash as it was
    set FAILED=1
)
for %%S in ("appeared after" "NOT taken: " "end of device report") do (
    findstr /C:%%S "%BASE%.clash-dev.txt" > nul
    if errorlevel 1 (
        echo FAIL: the clash .DEV does not say %%S
        set FAILED=1
    )
)
for %%S in ("appeared after this run chose its name" "It was left alone and is not this" "Attach: ") do (
    findstr /C:%%S "%BASE%.clash.log" > nul
    if errorlevel 1 (
        echo FAIL: the clash screen does not say %%S
        set FAILED=1
    )
)

rem A name given to -report with -o (owner, 2026-10-13: SNAPGET.BAT NAME).
rem A valid name is used as the base and the run exits 4 as the clock-named
rem one does; a second run under the same name exits 6 and leaves the first
rem run's .DEV byte for byte; a name whose .PSC.TMP alone exists is taken too
rem and that file is left as it was; a path, a drive, a dot, 9 characters,
rem an empty name, a DOS device name and a leading '-' each exit 2 and write
rem nothing.
mkdir "%RUNDIR%\rep4"
pushd "%RUNDIR%\rep4"
"%~dp0XHCISNAP.EXE" -report -o MyRep_1 -c 99 > "%BASE%.named1.log"
if not "%errorlevel%"=="4" (
    echo FAIL: -report -o MyRep_1 with no controller exited %errorlevel%, expected 4
    set FAILED=1
)
if not exist MyRep_1.DEV (
    echo FAIL: -report -o MyRep_1 wrote no MyRep_1.DEV
    set FAILED=1
)
findstr /C:"files named MyRep_1 (the name given)" "%BASE%.named1.log" > nul
if errorlevel 1 (
    echo FAIL: the named -report screen does not say the name was given
    set FAILED=1
)
findstr /X /C:"Attach: MyRep_1.DEV" "%BASE%.named1.log" > nul
if errorlevel 1 (
    echo FAIL: the named -report screen does not name MyRep_1.DEV to attach
    set FAILED=1
)
copy /b MyRep_1.DEV "%BASE%.named1-dev.txt" > nul
"%~dp0XHCISNAP.EXE" -report -o myrep_1 -c 99 > "%BASE%.named2.log"
if not "%errorlevel%"=="6" (
    echo FAIL: -report -o under a taken name exited %errorlevel%, expected 6
    set FAILED=1
)
fc /b MyRep_1.DEV "%BASE%.named1-dev.txt" > nul
if errorlevel 1 (
    echo FAIL: a taken name's .DEV was changed by the refused run
    set FAILED=1
)
findstr /C:"Nothing was written, and nothing was overwritten." "%BASE%.named2.log" > nul
if errorlevel 1 (
    echo FAIL: the taken-name refusal does not say nothing was written
    set FAILED=1
)
> TAKEN.PSC.TMP echo theirs
"%~dp0XHCISNAP.EXE" -report -o TAKEN -c 99 > "%BASE%.named3.log"
if not "%errorlevel%"=="6" (
    echo FAIL: -report -o TAKEN beside TAKEN.PSC.TMP exited %errorlevel%, expected 6
    set FAILED=1
)
findstr /B /C:"theirs" TAKEN.PSC.TMP > nul
if errorlevel 1 (
    echo FAIL: TAKEN.PSC.TMP did not survive the refused run as it was
    set FAILED=1
)
for %%N in ("sub\X" "C:X" "A.B" "ABCDEFGHI" "" "CON" "com1" "-X" "A&B") do (
    "%~dp0XHCISNAP.EXE" -report -o %%N -c 99 > "%BASE%.badname.log"
    if errorlevel 3 (
        echo FAIL: -report -o %%N exited past 2, expected 2
        set FAILED=1
    ) else if not errorlevel 2 (
        echo FAIL: -report -o %%N exited below 2, expected 2
        set FAILED=1
    )
    findstr /C:"That" "%BASE%.badname.log" > nul
    if errorlevel 1 (
        echo FAIL: -report -o %%N was not refused by the name rule
        set FAILED=1
    )
)
set NAMEDCOUNT=0
for %%F in (*) do set /a NAMEDCOUNT+=1
if not "%NAMEDCOUNT%"=="2" (
    echo FAIL: the named -report cases left %NAMEDCOUNT% files, expected 2: MyRep_1.DEV and TAKEN.PSC.TMP
    dir /b
    set FAILED=1
)
popd

rem Codex review of 38.1, round 3: SNAPON.BAT runs XHCISNAP -on %1 and
rem SNAPGET.BAT XHCISNAP -report %1, with no if on %1, so the default and
rem the check are the tool's. -on's parse (-selftest-on, no registry): none
rem is 2, 1 to 4 and a quoted "1" are taken, 0 (pointing at SNAPOFF.BAT),
rem 5, 00, 01, signs, letters, an empty word and two words are refused with
rem exit 2; -on itself refuses the same before the registry is opened.
"%~dp0XHCISNAP.EXE" -selftest-on > "%BASE%.on.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-on with no level exited %errorlevel%, expected 0
    set FAILED=1
)
findstr /C:"-on: level 2 accepted" "%BASE%.on.log" > nul
if errorlevel 1 (
    echo FAIL: -on with no level did not take level 2
    set FAILED=1
)
for %%L in (1 2 3 4 "4") do (
    "%~dp0XHCISNAP.EXE" -selftest-on %%L > "%BASE%.on.log"
    if errorlevel 1 (
        echo FAIL: -selftest-on %%L exited nonzero, expected 0
        set FAILED=1
    ) else if not errorlevel 0 (
        echo FAIL: -selftest-on %%L exited below 0, expected 0
        set FAILED=1
    )
    findstr /C:"-on: level %%~L accepted" "%BASE%.on.log" > nul
    if errorlevel 1 (
        echo FAIL: -selftest-on %%L did not take level %%~L
        set FAILED=1
    )
)
for %%L in (0 5 9 00 01 02 10 +1 -1 x 2x "" "1 2" "A&B") do (
    "%~dp0XHCISNAP.EXE" -selftest-on %%L > "%BASE%.on.log"
    if errorlevel 3 (
        echo FAIL: -selftest-on %%L exited past 2, expected 2
        set FAILED=1
    ) else if not errorlevel 2 (
        echo FAIL: -selftest-on %%L exited below 2, expected 2
        set FAILED=1
    )
    findstr /C:"Nothing was" "%BASE%.on.log" > nul
    if errorlevel 1 (
        echo FAIL: -selftest-on %%L was not refused by -on's rule
        set FAILED=1
    )
)
"%~dp0XHCISNAP.EXE" -selftest-on 1 2 > "%BASE%.on.log"
if not "%errorlevel%"=="2" (
    echo FAIL: -selftest-on 1 2 exited %errorlevel%, expected 2
    set FAILED=1
)
findstr /C:"one level at most" "%BASE%.on.log" > nul
if errorlevel 1 (
    echo FAIL: -on with two levels does not say it takes one at most
    set FAILED=1
)
"%~dp0XHCISNAP.EXE" -on 0 > "%BASE%.on.log"
if not "%errorlevel%"=="2" (
    echo FAIL: -on 0 exited %errorlevel%, expected 2
    set FAILED=1
)
findstr /C:"SNAPOFF.BAT" "%BASE%.on.log" > nul
if errorlevel 1 (
    echo FAIL: -on 0 does not point at SNAPOFF.BAT
    set FAILED=1
)
for %%A in ("5" "00" "1 2" "x") do (
    "%~dp0XHCISNAP.EXE" -on %%~A > "%BASE%.on.log"
    if not errorlevel 2 (
        echo FAIL: -on %%~A was not refused with exit 2
        set FAILED=1
    )
    findstr /C:"level" "%BASE%.on.log" > nul
    if errorlevel 1 (
        echo FAIL: -on %%~A was not refused by -on's rule
        set FAILED=1
    )
)
"%~dp0XHCISNAP.EXE" -force -on 2 > "%BASE%.on.log"
if not "%errorlevel%"=="2" (
    echo FAIL: -force -on 2 exited %errorlevel%, expected 2: -on stands alone
    set FAILED=1
)

rem -report NAME, the name as -report's own argument: a valid name is the
rem base (exit 4), the same name again is taken (exit 6), both spellings at
rem once are refused, and every bad name - a leading '-', '/?', '-help', a
rem DOS device name, a cmd.exe character - is refused by the name rule
rem with exit 2, writing nothing.
mkdir "%RUNDIR%\rep5"
pushd "%RUNDIR%\rep5"
"%~dp0XHCISNAP.EXE" -report MyRep_2 -c 99 > "%BASE%.pos1.log"
if not "%errorlevel%"=="4" (
    echo FAIL: -report MyRep_2 with no controller exited %errorlevel%, expected 4
    set FAILED=1
)
if not exist MyRep_2.DEV (
    echo FAIL: -report MyRep_2 wrote no MyRep_2.DEV
    set FAILED=1
)
findstr /C:"files named MyRep_2 (the name given)" "%BASE%.pos1.log" > nul
if errorlevel 1 (
    echo FAIL: the -report NAME screen does not say the name was given
    set FAILED=1
)
"%~dp0XHCISNAP.EXE" -report myrep_2 -c 99 > "%BASE%.pos2.log"
if not "%errorlevel%"=="6" (
    echo FAIL: -report NAME under a taken name exited %errorlevel%, expected 6
    set FAILED=1
)
"%~dp0XHCISNAP.EXE" -report A1 -o B1 -c 99 > "%BASE%.pos3.log"
if not "%errorlevel%"=="2" (
    echo FAIL: -report A1 -o B1 exited %errorlevel%, expected 2
    set FAILED=1
)
"%~dp0XHCISNAP.EXE" -report /? > "%BASE%.pos4.log"
if not "%errorlevel%"=="2" (
    echo FAIL: -report /? exited %errorlevel%, expected 2: a name, refused
    set FAILED=1
)
for %%N in ("sub\X" "C:X" "A.B" "ABCDEFGHI" "" "CON" "nul" "Aux" "prn" "COM0" "com9" "LPT0" "lpt9" "-X" "-help" "-dump" "A&B" "A^B" "A B" "A%%B") do (
    "%~dp0XHCISNAP.EXE" -report %%N -c 99 > "%BASE%.posbad.log"
    if errorlevel 3 (
        echo FAIL: -report %%N exited past 2, expected 2
        set FAILED=1
    ) else if not errorlevel 2 (
        echo FAIL: -report %%N exited below 2, expected 2
        set FAILED=1
    )
    findstr /C:"That name is refused" "%BASE%.posbad.log" > nul
    if errorlevel 1 (
        echo FAIL: -report %%N was not refused by the name rule
        set FAILED=1
    )
)
set NAMEDCOUNT=0
for %%F in (*) do set /a NAMEDCOUNT+=1
if not "%NAMEDCOUNT%"=="1" (
    echo FAIL: the -report NAME cases left %NAMEDCOUNT% files, expected 1: MyRep_2.DEV
    dir /b
    set FAILED=1
)
popd

rem The batches themselves, through cmd.exe, beside a copy of this build: a
rem quoted word, one with a cmd.exe character in it and a bad one each reach
rem XHCISNAP and are refused there, with no cmd.exe error first; with
rem XHCISNAP_FAULT=noreg SNAPON.BAT's levels are parsed and nothing is
rem written, so a quoted "1" is level 1 and no word is 2. SNAPGET.BAT is not
rem run with a good name: that would open this host's own controller. A
rem host shell that sets NoDefaultCurrentDirectoryInExePath would not find
rem the batches' bare XHCISNAP in their own directory, as no 98 SE to 7
rem prompt fails to, so it is cleared for these runs (setlocal holds it).
set "NoDefaultCurrentDirectoryInExePath="
mkdir "%RUNDIR%\bat"
copy /b XHCISNAP.EXE "%RUNDIR%\bat\" > nul
copy /b snapon.bat "%RUNDIR%\bat\" > nul
copy /b snapget.bat "%RUNDIR%\bat\" > nul
pushd "%RUNDIR%"
call "%RUNDIR%\bat\snapget.bat" "A&B" < nul > "%BASE%.bat1.log" 2>&1
call "%RUNDIR%\bat\snapget.bat" "-X" < nul > "%BASE%.bat3.log" 2>&1
call "%RUNDIR%\bat\snapget.bat" CON < nul > "%BASE%.bat4.log" 2>&1
call "%RUNDIR%\bat\snapget.bat" -c < nul > "%BASE%.bat11.log" 2>&1
call "%RUNDIR%\bat\snapget.bat" -o < nul > "%BASE%.bat12.log" 2>&1
popd
for %%F in ("%BASE%.bat1.log" "%BASE%.bat3.log" "%BASE%.bat4.log" "%BASE%.bat11.log" "%BASE%.bat12.log") do (
    findstr /C:"That name is refused" %%F > nul
    if errorlevel 1 (
        echo FAIL: SNAPGET.BAT did not reach XHCISNAP's name refusal: %%F
        type %%F
        set FAILED=1
    )
    findstr /C:"SNAPGET.BAT takes such a name, or none" %%F > nul
    if errorlevel 1 (
        echo FAIL: SNAPGET.BAT's XHCISNAP did not say what it takes: %%F
        set FAILED=1
    )
    findstr /I /C:"unexpected" /C:"not recognized" /C:"syntax" %%F > nul
    if not errorlevel 1 (
        echo FAIL: cmd.exe stopped SNAPGET.BAT before XHCISNAP ran: %%F
        type %%F
        set FAILED=1
    )
)
set "XHCISNAP_FAULT=noreg"
pushd "%RUNDIR%"
call "%RUNDIR%\bat\snapon.bat" < nul > "%BASE%.bat5.log" 2>&1
call "%RUNDIR%\bat\snapon.bat" "1" < nul > "%BASE%.bat6.log" 2>&1
call "%RUNDIR%\bat\snapon.bat" 4 < nul > "%BASE%.bat7.log" 2>&1
call "%RUNDIR%\bat\snapon.bat" "A&B" < nul > "%BASE%.bat8.log" 2>&1
call "%RUNDIR%\bat\snapon.bat" 0 < nul > "%BASE%.bat9.log" 2>&1
call "%RUNDIR%\bat\snapon.bat" 00 < nul > "%BASE%.bat10.log" 2>&1
popd
set "XHCISNAP_FAULT="
for %%C in ("bat5:2" "bat6:1" "bat7:4") do (
    for /f "tokens=1,2 delims=:" %%P in (%%C) do (
        findstr /C:"-on: level %%Q accepted" "%BASE%.%%P.log" > nul
        if errorlevel 1 (
            echo FAIL: SNAPON.BAT did not run XHCISNAP -on at level %%Q: %%P
            type "%BASE%.%%P.log"
            set FAILED=1
        )
        findstr /C:"The channel is on." "%BASE%.%%P.log" > nul
        if errorlevel 1 (
            echo FAIL: SNAPON.BAT did not say the channel is on: %%P
            set FAILED=1
        )
    )
)
for %%F in ("%BASE%.bat8.log" "%BASE%.bat9.log" "%BASE%.bat10.log") do (
    findstr /C:"SNAPON.BAT takes the same" %%F > nul
    if errorlevel 1 (
        echo FAIL: SNAPON.BAT did not reach XHCISNAP's level refusal: %%F
        type %%F
        set FAILED=1
    )
    findstr /C:"The channel is on." %%F > nul
    if not errorlevel 1 (
        echo FAIL: SNAPON.BAT said the channel is on for a refused level: %%F
        set FAILED=1
    )
    findstr /I /C:"unexpected" /C:"not recognized" /C:"syntax" %%F > nul
    if not errorlevel 1 (
        echo FAIL: cmd.exe stopped SNAPON.BAT before XHCISNAP ran: %%F
        set FAILED=1
    )
)
findstr /C:"SNAPOFF.BAT (XHCISNAP -disable)" "%BASE%.bat9.log" > nul
if errorlevel 1 (
    echo FAIL: SNAPON.BAT 0 does not point at SNAPOFF.BAT
    set FAILED=1
)
rem Owner, 2026-10-13: the last line before pause's prompt names what to
rem attach - XHCISNAP's, since the batch prints nothing after it.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$bad = 0; foreach ($c in @(@('bat1','Nothing to attach: nothing was written.'), @('bat3','Nothing to attach: nothing was written.'), @('bat4','Nothing to attach: nothing was written.'), @('bat11','Nothing to attach: nothing was written.'), @('bat12','Nothing to attach: nothing was written.'), @('bat5','Nothing to attach yet: restart Windows, reproduce, then run SNAPGET.BAT.'), @('bat6','Nothing to attach yet: restart Windows, reproduce, then run SNAPGET.BAT.'), @('bat8','Nothing to attach: nothing was changed.'), @('bat9','Nothing to attach: nothing was changed.'))) { $l = @(Get-Content -LiteralPath ($env:BASE + '.' + $c[0] + '.log') | Where-Object { $_.Trim() -ne '' -and $_ -notmatch '^Press any key' }); if ($l.Count -eq 0 -or $l[$l.Count - 1] -cne $c[1]) { Write-Output ('FAIL: ' + $c[0] + ' ends with: ' + $(if ($l.Count) { $l[$l.Count - 1] } else { '(nothing)' })); $bad = 1 } }; exit $bad"
if errorlevel 1 set FAILED=1
set BATCOUNT=0
for %%F in ("%RUNDIR%\bat\*") do set /a BATCOUNT+=1
if not "%BATCOUNT%"=="3" (
    echo FAIL: the batch cases left %BATCOUNT% files beside the batches, expected 3
    dir /b "%RUNDIR%\bat"
    set FAILED=1
)

rem The memory descriptor 98 SE's and ME's cfgmgr32.dll hands back (the
rem p38-1 legs, 2026-10-13): NT's 32-byte header and 36-byte range with the
rem range-size field left at 9x's 20. It decodes as Device Manager showed it,
rem as do NT's (2000 to 7: count 0 with one range after the header); a size
rem neither has stays raw. The IRQ descriptors 2000 (32 bytes), XP and XP x64
rem (36) and 7 (36, message-signalled) handed back decode as 11, 10 and MSI.
"%~dp0XHCISNAP.EXE" -selftest-res > "%BASE%.res.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-res exited %errorlevel%, expected 0
    set FAILED=1
)
findstr /R /C:"memory  *FEBF0000-FEBF3FFF (16 KB)$" "%BASE%.res.log" | find /c "FEBF0000" | findstr /X "3" > nul
if errorlevel 1 (
    echo FAIL: the 9x and NT memory descriptors did not all decode to FEBF0000-FEBF3FFF
    type "%BASE%.res.log"
    set FAILED=1
)
findstr /C:"type 1, 68 bytes in a layout this tool does not" "%BASE%.res.log" > nul
if errorlevel 1 (
    echo FAIL: a memory descriptor of neither layout was not left raw
    set FAILED=1
)
for %%S in ("IRQ          11, shared, level" "IRQ          10, shared, level" "-2 as a signed number: a" "message-signalled interrupt (MSI)") do (
    findstr /C:%%S "%BASE%.res.log" > nul
    if errorlevel 1 (
        echo FAIL: the IRQ descriptors do not decode to %%S
        set FAILED=1
    )
)
findstr /C:"[end]" "%BASE%.res.log" > nul
if errorlevel 1 (
    echo FAIL: -selftest-res did not finish
    set FAILED=1
)

rem -report's publication on its own (report_publish, region_create): clean,
rem a .TXT taken first, a .PSC taken after the .BIN was renamed, a temporary
rem taken first. The other file keeps its contents and this run's are gone.
"%~dp0XHCISNAP.EXE" -selftest-publish "%BASE%-pub" > "%BASE%.publish.log"
if not "%errorlevel%"=="0" (
    echo FAIL: -selftest-publish exited %errorlevel%, expected 0
    set FAILED=1
)
for %%S in ("publish clean: ok" "publish .TXT taken: ok" "publish .PSC taken: ok" "publish .BIN.TMP taken: ok" "[end]") do (
    findstr /C:%%S "%BASE%.publish.log" > nul
    if errorlevel 1 (
        echo FAIL: the publication self-test does not say %%S
        set FAILED=1
    )
)

rd /s /q "%RUNDIR%" 2> nul
rd out 2> nul

if "%FAILED%"=="1" (
    echo xhcisnap selftest FAILED
    exit /b 1
)
echo xhcisnap selftest: 33 cases, all passed
exit /b 0
