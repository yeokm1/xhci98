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
for %%S in ("value 2; in effect 2 (polling)" "reason               forced polling (XhciInterruptMode 2)" "XhciImodInterval250ns has no effect" "1234, 56 of them found an event; peeks 78, 9 hits" "49 to 63 ms (2 discarded)" "PollService 1, PollServiceGen 7, LineMoved 0," "Interrupt Pin 0, the controller has no line interrupt" "9000 is above 5000, so the driver held it to 5000." "0 is not allowed, so the driver used its default, 1." "went dead; moved to polling at run time" "mask read back down at the move" "proven by PCI Interrupt Disable; attempts 1" "name different deliveries" "7 is reserved, so it runs as 0." "held to the idle period, 20 ms." "watching the line; last armed 100 ms (a watcher's look)" "not placed by this driver's HCD header (808 bytes)" "predates polled delivery" "does not fit its image" "80 counter words at +816 in 1200 bytes" "no helper and no line offered: the start fails (an earlier 2.3.0.0 build)" "but a message was granted: the start fails" "value 3; in effect 3 (a message interrupt only)" "forced message (XhciInterruptMode 3): a message interrupt" "but no message was granted: the start fails" "is xhcimsi.sys bound?): the start fails" "this Windows has no message interrupts: the start fails") do (
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
for %%S in ("the helper xhcimsi.sys (32-bit)" "4 (the connection's table; one is served); capability MSI" "message descriptors in the grant 1" "capability MSI-X" "MmGetSystemRoutineAddress (64-bit)" "REFUSED (its answer failed a check)" "a message with a line beside it" "BOUND; looks 1" "helper version       2.2.0.0" "not asked (helper bound, or 64-bit, and mode not 1 or 3)" "REPLACED: the line choices stripped, the message left (mode 3)" "not set (mode 0's default)" "no IRP for the query); looks 2, last status C0000034" "REPLACED: the message choices stripped, the line left" "1 configuration(s), 1 message and 1 line member(s); 1 stripped" "mode as read         0" "refused for          the answer's identity" "none has run: not applicable" "messages connected     0 (no message connected)" "this driver predates MSI" "100 counter words - not a shape") do (
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
findstr /C:"WRONG" "%BASE%.sha.log" > nul
if not errorlevel 1 (
    echo FAIL: the SHA-256 self-test printed a WRONG digest
    set FAILED=1
)

rem Task 38.1's -report, end to end in a directory of its own, against a
rem controller index no machine has: the .DEV is written though the device
rem does not open, says why there is no dump, and exits 4; a second run in
rem the same directory takes another name rather than overwriting the
rem first; -nosysinfo is honoured; -o beside -report is refused, writing
rem nothing.
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
"%~dp0XHCISNAP.EXE" -report -o REFUSED > "%BASE%.report3.log"
if not "%errorlevel%"=="2" (
    echo FAIL: -report -o exited %errorlevel%, expected 2
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
for %%S in ("files named " ".DEV written" "Attach " "(no dump was taken: it says why)") do (
    findstr /C:%%S "%BASE%.report1.log" > nul
    if errorlevel 1 (
        echo FAIL: the -report screen does not say %%S
        set FAILED=1
    )
)
findstr /C:"It does not take -o" "%BASE%.report3.log" > nul
if errorlevel 1 (
    echo FAIL: the -report -o refusal does not say it does not take -o
    set FAILED=1
)

rd /s /q "%RUNDIR%" 2> nul
rd out 2> nul

if "%FAILED%"=="1" (
    echo xhcisnap selftest FAILED
    exit /b 1
)
echo xhcisnap selftest: 22 cases, all passed
exit /b 0
