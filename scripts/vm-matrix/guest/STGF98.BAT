@echo off
rem STGF98.BAT - the WINDOWS 98 variant of STAGEF.BAT (batch 11-V stage F, task
rem 11-V.5): the sustained concurrent multi-class load, driven from inside the
rem guest.
rem
rem WHY A SECOND FILE EXISTS AT ALL.  STAGEF.BAT and LOAD.BAT were written for 2b
rem and reused unchanged on 2d.  Both are Windows 2000 guests, and
rem the two scripts are cmd.exe programs: they use `cmd /c`, `%~f0` and `for /L`,
rem and those are EXACTLY the lines that start traffic.  2a is the first Windows
rem 98 target to need them, and Windows 98's shell is COMMAND.COM, which has none
rem of the three.  The symptom was the dangerous kind - the load
rem reported as running while the QEMU trace carried 28 usb_msd_cmd_submit
rem against 2d's 10,837.  A tool validated on one target is validated on ONE
rem target.
rem
rem THE FOUR THINGS COMMAND.COM DOES NOT HAVE, and what replaces each:
rem
rem   cmd /c        ->  command /c        (COMMAND.COM is the shell here)
rem   %~f0          ->  an explicit C:\ path, hard-coded below
rem   for /L        ->  not needed; 2a runs no isochronous half (see LOAD98.BAT)
rem   set /a        ->  not used at all; the disk stream is unbounded instead
rem   if /i         ->  both spellings tested explicitly
rem
rem AND ONE THING THE VOLUME DOES NOT HAVE.  Windows 98's COMMAND.COM can `dir`
rem and `type` a batch file on the QEMU `fat:` transfer volume but CANNOT EXECUTE
rem one from it - "Bad command or file name", with the bus idle.  Measured
rem in batch 11-V stage G against a negative control on C:, after CRLF, 8.3 aliases, volume
rem size, COMSPEC and internal-vs-external commands had each been ruled out.
rem READING from the volume is unaffected, which is why BIG8MB.BIN is still read
rem off it below.  So this file must be COPIED TO C:\ AND RUN FROM THERE, and the
rem worker re-entry below therefore names C:\STGF98.BAT rather than deriving its
rem own path.  In the guest:
rem
rem       copy d:\*.bat c:\
rem       c:
rem       cd \
rem       load98 F D 192.168.1.100
rem
rem DIVISION OF LABOUR is unchanged from STAGEF.BAT.  The HID half is driven from
rem the HOST (scripts\local\hidpump-11v.ps1 through the QEMU monitor), which costs
rem the guest nothing and cannot raise a dialog.  This file drives the two halves
rem that can only originate inside the guest: bulk storage writes and Ethernet
rem traffic.
rem
rem   Usage:  STGF98 U S IP        (the launcher role - normally via LOAD98.BAT)
rem             U  = the USB disk's drive letter, no colon    (2a: F)
rem             S  = the source/transfer volume letter, no colon  (2a: D)
rem             IP = an address on the LAN to ping             (2a: 192.168.1.100)
rem
rem   Example: STGF98 F D 192.168.1.100
rem
rem WRITES, NOT READS, and that is measured rather than chosen.  Measured on 2b
rem Batch 8-V: the whole medium fits in the guest's file cache, which saturated
rem around 170 MB while the guest reported reading 192 MB and usb_msd_cmd_submit
rem moved ZERO.  Writes must be flushed, so they reach the device whatever the
rem cache does.  The source is the transfer volume, which is an IDE disk, so every
rem USB command this generates is a write.
rem
rem THE BACKEND IS THROTTLED to iops-total=30 by soak-11v.ps1 when it attaches the
rem storage device.  That is not a mistake and must not be "fixed": QEMU completes
rem about 99% of transfers synchronously inside the doorbell kick, so without the
rem throttle an unplug essentially never catches a transfer in flight.  It makes
rem this load slower per byte and that is the intent.
rem
rem FOUR ROTATING NAMES keep the medium from filling; the resident cost is 32 MB
rem whatever the pass count.
rem
rem THE ERRORS WHEN THE DISK IS PULLED ARE THE TEST, not a failure of this script.
rem On Abort/Retry/Fail press A, and tell the agent what the console said.
rem
rem THIS LOAD CANNOT BE USED FOR A SHUTDOWN TEST ON THIS TARGET.  Measured on
rem Batch 11-V stage G: Windows 98 raises a modal "You must quit this
rem program before you quit Windows" box for EVERY running DOS program, and this
rem file leaves eight of them.  Worse, :diskloop below is unbounded, so the box
rem Windows marks "Closing - MS-DOS Prompt" never yields and the shutdown STALLS
rem INDEFINITELY - a stop fired into a bus doing 1,909 transfers/s produced no
rem `cb StopController` in 420 seconds.
rem
rem The irony is exact and it is the lesson: the ten-pass bound was removed from
rem STAGEF.BAT (see :disk) BECAUSE a finished stream cannot supply a stop that
rem lands on traffic - true on Windows 2000.  Here the OPPOSITE holds, and an
rem unfinished stream cannot supply a shutdown at all.  Do not "fix" this by
rem re-adding a bound: a bounded program still blocks the shutdown while it runs,
rem and one that has finished supplies no traffic.  The traffic for a Windows 98
rem lifecycle stop must come from something Windows closes BY ITSELF - an
rem Explorer file copy against a THROTTLED backend, plus hidpump-11v.ps1 driving
rem HID from the host.  docs\contributing\runs\run-11v.md stage G's WIN98
rem SHUTDOWN BLOCK box is normative; this file is for stage F's sustained load
rem only.

if "%1"=="DISK" goto disk
if "%1"=="disk" goto disk
if "%1"=="NET"  goto net
if "%1"=="net"  goto net

if "%1"=="" goto usage
if "%2"=="" goto usage
if "%3"=="" goto usage

echo.
echo ============================================================
echo  batch 11-V stage F - concurrent load (Windows 98 variant)
echo    USB disk    : %1:
echo    source      : %2:
echo    ping target : %3
echo.
echo  Starting the storage stream and five Ethernet streams in
echo  parallel.  Leave them running; the agent holds the bus
echo  population and takes the readings around this window.
echo ============================================================
echo.

rem Five parallel ping streams is batch 9-0.2's shape - 5 x `ping -n 150 -l 1400`
rem per leg gave 748/750 and 749/750 replies, the two losses being the opening
rem ARP.  Several streams rather than one because a single ping has one interrupt
rem IN outstanding at a time and that is not a load.
rem
rem `start /m command /c` rather than `start "title" /min cmd /c`: Windows 98's
rem START.EXE takes no title argument - it would read the title as the program
rem name - and its minimise switch is /m.
start /m command /c C:\STGF98.BAT DISK %1 %2
start /m command /c C:\STGF98.BAT NET %3
start /m command /c C:\STGF98.BAT NET %3
start /m command /c C:\STGF98.BAT NET %3
start /m command /c C:\STGF98.BAT NET %3
start /m command /c C:\STGF98.BAT NET %3

echo All streams launched.  Tell the agent they are running.
goto end

:disk
rem UNBOUNDED, AS STAGEF.BAT IS NOW.  An earlier STAGEF.BAT stopped after ten
rem passes, and on both Windows 2000 targets that was measured to be shorter
rem than the window it is loading:
rem ~4 minutes against 2b's eight-minute window (it had to be restarted by hand),
rem and on 2d it had finished again by t+584 of 600, so "storage was idle at the
rem pull" is recorded against that leg.  A load that stops halfway leaves the tail
rem of the window measuring an idle bus while still reporting as a three-class
rem load.  The four rotating names make the medium cost constant, so there is
rem nothing a pass count buys here: the window ends the stream, and the operator
rem closes this box.
rem
rem A FALLING TRANSFER RATE IS NOT EVIDENCE THIS DIED, and neither is this box
rem still being open: on 2b a normal completion and a silent failure (full medium)
rem both showed up as the rate dropping.  What separates them is the trace - count
rem usb_msd_cmd_submit.  If the medium IS full every copy fails instantly and this
rem loop spins; that is the case LOAD98.BAT's canary write exists to refuse
rem before the window starts.
:diskloop
copy /Y %3:\BIG8MB.BIN %2:\W1.BIN >NUL
copy /Y %3:\BIG8MB.BIN %2:\W2.BIN >NUL
copy /Y %3:\BIG8MB.BIN %2:\W3.BIN >NUL
copy /Y %3:\BIG8MB.BIN %2:\W4.BIN >NUL
echo --- disk pass done
goto diskloop

:net
rem -l 1400 keeps each echo inside one Ethernet frame, so a reply is one bulk IN
rem completion rather than a reassembly.  Windows 98's PING.EXE takes -n and -l
rem exactly as Windows 2000's does.
:netloop
ping -n 150 -l 1400 %2
goto netloop

:usage
echo Usage: STGF98 U S IP    (U = USB drive letter, S = source drive letter, no colons)
echo Example: STGF98 F D 192.168.1.100
echo.
echo Normally started by LOAD98.BAT, and only from C:\ - see the header.
:end
