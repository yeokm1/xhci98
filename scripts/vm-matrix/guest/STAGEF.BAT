@echo off
rem STAGEF.BAT - batch 11-V stage F (roadmap task 11-V.5): the SUSTAINED
rem CONCURRENT multi-class load.
rem
rem WHY THIS FILE EXISTS.  Phases 5 to 9 each exercised ONE device class at a
rem time.  Task 11-V.5's "sustained concurrent HID + storage + Ethernet load"
rem has never run in any phase, and it is one of the two highest-information
rem clauses left in batch 11-V - which is why the run strategy refused to drop
rem it when the batch's true cost became visible.  Concurrency is the point:
rem control, bulk IN, bulk OUT and interrupt IN all live on the ring at once,
rem across several slots, with the driver's one controller lock between them.
rem
rem DIVISION OF LABOUR.  The HID half is driven from the HOST - the agent pumps
rem mouse_move through the QEMU monitor, which costs the guest nothing to set
rem up and cannot raise a dialog.  This file drives the two halves that can only
rem come from inside the guest: bulk storage traffic and Ethernet traffic.
rem
rem   Usage:  STAGEF U S IP
rem             U  = the USB disk's drive letter, no colon
rem             S  = the source drive letter (the VVFAT transfer volume), no colon
rem             IP = an address on the LAN to ping (the default gateway will do)
rem
rem   Example: STAGEF F E 192.168.1.1
rem
rem WRITES, NOT READS, AND THAT IS MEASURED RATHER THAN CHOSEN.  This guest runs
rem -m 512 against a 256 MB medium, so the whole volume fits in the file cache
rem and a read clause cannot reach the device at all: measured on 2b in batch 8-V,
rem the cache saturated around 170 MB and the guest reported reading 192 MB
rem while usb_msd_cmd_submit moved ZERO.  Writes must be flushed, so they reach
rem the device whatever the cache does.  The source is the VVFAT volume, which
rem is an IDE disk, so every USB command this generates is a write.
rem
rem THE BACKEND IS THROTTLED to iops-total=30 by the stage launcher.  That is
rem not a mistake and must not be "fixed": QEMU completes about 99% of transfers
rem synchronously inside the doorbell kick, so without the throttle an unplug
rem essentially never catches a transfer in flight - two pulls at 215 and 597
rem writes deep caught nothing, and at 30 iops the same pull catches one 3 times
rem out of 3.  It makes this load slower per byte and that is the intent.
rem
rem FOUR ROTATING NAMES keep the 256 MB medium from filling.
rem
rem THE ERRORS WHEN THE DISK IS PULLED ARE THE TEST, not a failure of this
rem script.  On Abort/Retry/Fail press A, and tell the agent what the console
rem said.

if /i "%1"=="DISK" goto disk
if /i "%1"=="NET"  goto net

if "%1"=="" goto usage
if "%2"=="" goto usage
if "%3"=="" goto usage

echo.
echo ============================================================
echo  batch 11-V stage F - concurrent load
echo    USB disk    : %1:
echo    source      : %2:
echo    ping target : %3
echo.
echo  Starting the storage stream and five Ethernet streams in
echo  parallel.  Leave them running; the agent holds the bus
echo  population and takes the readings around this window.
echo ============================================================
echo.

rem Five parallel ping streams is batch 9-0.2's shape: 5 x `ping -n 150 -l 1400`
rem per leg gave 748/750 and 749/750 replies, the two losses being the opening
rem ARP.  Several streams rather than one because a single ping is one interrupt
rem IN outstanding at a time and that is not a load.
start "stage F - disk"  /min cmd /c "%~f0" DISK %1 %2
start "stage F - net 1" /min cmd /c "%~f0" NET %3
start "stage F - net 2" /min cmd /c "%~f0" NET %3
start "stage F - net 3" /min cmd /c "%~f0" NET %3
start "stage F - net 4" /min cmd /c "%~f0" NET %3
start "stage F - net 5" /min cmd /c "%~f0" NET %3

echo All streams launched.  Tell the agent they are running.
goto end

:disk
rem 8 MB x 4 files, rewritten in place, UNTIL THE WINDOW ENDS - there is no pass
rem count, and removing it was a fix rather than a tidy-up.
rem
rem MEASURED ON 2b, AND THE ORIGINAL CLAIM HERE WAS WRONG: this said
rem ten passes was "comfortably longer than the load window", so the traffic
rem could not stop halfway and leave the tail measuring an idle bus.  It is
rem about FOUR MINUTES (9,976 usb_msd_cmd_submit at data-len 65536, ~340 MB),
rem against an eight-minute window - so it finished at t+250s and the window ran
rem three-class until it was restarted by hand.
rem
rem THE BOUND IS GONE AS OF STAGE G.  It is why "storage was idle at
rem the pull" is recorded against 2d, and why the Windows 98 pair - which never
rem had one - is the only leg that caught a write in flight, raising that guest's
rem Disk Write Error box at the teardown.  Stage G's whole clause is a stop that
rem lands on traffic, and a stream that has already finished cannot supply one.
rem REMOVE THE BOUND RATHER THAN ADDING A MECHANISM: no timer, no restart
rem prompt, no pass arithmetic to get wrong - the four files are rewritten in
rem place, so an unbounded loop consumes no additional space on the medium and
rem the operator's Ctrl+C or the shutdown is what ends it.
rem
rem A FALLING TRANSFER RATE IS NOT EVIDENCE THIS DIED: on the same run a normal
rem ten-pass completion and a silent failure (full medium) both showed up as the
rem rate dropping.  What separates them is the trace - count
rem usb_msd_cmd_submit, and compare the total against ~5,000 per ten passes.
set N=0
:diskloop
set /a N=%N%+1
copy /Y %3:\BIG8MB.BIN %2:\W1.BIN >NUL
copy /Y %3:\BIG8MB.BIN %2:\W2.BIN >NUL
copy /Y %3:\BIG8MB.BIN %2:\W3.BIN >NUL
copy /Y %3:\BIG8MB.BIN %2:\W4.BIN >NUL
echo --- disk pass %N% done
goto diskloop

:net
rem -l 1400 keeps each echo inside one Ethernet frame, so a reply is one bulk IN
rem completion rather than a reassembly.
:netloop
ping -n 150 -l 1400 %2
goto netloop

:usage
echo Usage: STAGEF U S IP    (U = USB drive letter, S = source drive letter, no colons)
echo Example: STAGEF F E 192.168.1.1
:end
