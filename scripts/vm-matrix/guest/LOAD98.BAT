@echo off
rem LOAD98.BAT - the WINDOWS 98 variant of LOAD.BAT (batch 11-V stage F, task
rem 11-V.5), as ONE command so the whole load starts on a single keystroke rather
rem than several hand-typed command lines racing the measurement window.
rem
rem WHY A SECOND FILE.  See STGF98.BAT's header for the full account.  In short:
rem LOAD.BAT and STAGEF.BAT are cmd.exe programs (`cmd /c`, `%~f0`, `for /L`) and
rem Windows 98's shell is COMMAND.COM, which has none of the three; and a batch
rem file on the QEMU `fat:` transfer volume can be listed and typed but NOT
rem EXECUTED on this target.  Both facts are Windows-98-only, which is why 2b and
rem 2d never met them.
rem
rem   HOW TO RUN IT, and the copy step is not optional:
rem
rem       copy d:\*.bat c:\
rem       c:
rem       cd \
rem       load98 F D 192.168.1.100
rem
rem   Usage:  LOAD98 U S IP
rem             U  = the USB disk's drive letter, no colon     (2a: F)
rem             S  = the source/transfer volume letter, no colon   (2a: D)
rem             IP = an address on the LAN to ping              (2a: 192.168.1.100)
rem
rem ALL THREE ARGUMENTS ARE REQUIRED, unlike LOAD.BAT, which defaults them.
rem Defaulting costs three `set`s, and COMMAND.COM's environment block is small
rem and already carrying PATH, COMSPEC, TEMP, PROMPT and winbootdir - an "Out of
rem environment space" here would half-start the load.  Neither this file nor
rem STGF98.BAT sets a single variable.  The measured 2a values are in the usage
rem line above and in docs\contributing\runs\run-11v.md's stage F block.
rem
rem THERE IS NO ISOCHRONOUS HALF ON THIS TARGET, and that is a measured exclusion
rem rather than an omission.  Windows 98's own audio stack failed five of five in
rem this vehicle in Phase 9, in a way two independent controls place outside this
rem driver, so an audio churn here would re-measure somebody else's defect and
rem read as a failure of this stage.  The 2b leg carries the isochronous clause,
rem with QEMU's wav capture backend as the outside witness.  Recorded as excluded,
rem not ticked off a substitute - which is also why this file has no NOAUDIO
rem switch to forget to pass.
rem
rem WHAT IT STARTS:
rem   STGF98.BAT   bulk OUT (disk writes) + interrupt/bulk (five ping streams),
rem                called rather than started, so its output stays in this window.
rem The HID half is driven from the host - scripts\local\hidpump-11v.ps1 - and
rem needs `-NoRepin` on the soak, or the pump moves the PS/2 mouse while the USB
rem mouse sits on one parked interrupt IN reporting as attached throughout.

if "%1"=="" goto usage
if "%2"=="" goto usage
if "%3"=="" goto usage

rem The worker re-entry in STGF98.BAT names C:\STGF98.BAT explicitly, because
rem COMMAND.COM has no %~f0.  So this must be true before anything starts, not
rem discovered by five ping windows opening and closing again.
if not exist C:\STGF98.BAT goto nostgf

rem The payload the disk stream reads.  It has gone missing before:
rem a mistyped `del *.bin` at an E:\> prompt removed BIG8MB.BIN from the guest's
rem view of the transfer volume mid-run.
if not exist %2:\BIG8MB.BIN goto nosrc

rem THE DRIVE EXISTING IS NOT THE DRIVE BEING WRITABLE, and the difference cost a
rem whole load window on 2b: usb8v-2b.img arrived carrying batch
rem 8-V's payload - 260,048,090 bytes used, 8,101,888 free - so every 8 MB copy
rem failed instantly, the disk stream's ten passes completed in about a second,
rem its window closed, and the run reported four classes under load while the
rem trace showed ZERO usb_msd_cmd_submit.
rem
rem SO THE CANARY IS 8 MB, NOT 5 KB, in both pairs.  An earlier LOAD.BAT copied
rem STAGEF.BAT itself as the canary, and 5 KB fits in the 8,101,888 bytes free
rem that defeated it, so that guard would have PASSED the very run it was
rem written for; LOAD.BAT copies BIG8MB.BIN now, as this file does.  Test the
rem property you actually depend on - an 8 MB write - with the file the load
rem actually copies.  It costs about four seconds against the throttled backend.
rem
rem The pre-delete is what makes this sound without trusting COPY's errorlevel,
rem which DOS has never set dependably: a stale CANARY.TMP from an earlier run
rem would otherwise satisfy the `if exist` after a copy that failed.
if exist %1:\CANARY.TMP del %1:\CANARY.TMP
echo Checking %1: can take an 8 MB write...
copy /Y %2:\BIG8MB.BIN %1:\CANARY.TMP >NUL
if not exist %1:\CANARY.TMP goto nospace
del %1:\CANARY.TMP
goto writable

:nospace
echo.
echo ERROR: %1: cannot take an 8 MB write.  The load would report as running
echo        while moving no data at all.  Free space and re-run:
echo.
dir %1:\ | find "bytes free"
echo.
echo        del %1:\*.bin      (the batch 8-V payload is the usual occupant)
goto end

:writable
echo.
echo ============================================================
echo  batch 11-V stage F - concurrent load, Windows 98 (2a)
echo    USB disk    : %1:
echo    source      : %2:
echo    ping target : %3
echo    audio       : NOT RUN - excluded on this target, see header
echo ============================================================
echo.
echo Starting disk writes and five ping streams...
echo.
call C:\STGF98.BAT %1 %2 %3
echo.
echo Tell the agent the load is running.
echo On a disk error box when the device is pulled, press A for Abort -
echo that error IS the test.
goto end

:nostgf
echo.
echo ERROR: C:\STGF98.BAT not found.
echo.
echo        Windows 98 cannot EXECUTE a batch file off the transfer volume -
echo        it can list and read one, but running it gives "Bad command or
echo        file name".  Both scripts must be copied to C:\ first:
echo.
echo            copy d:\*.bat c:\
echo.
goto end

:nosrc
echo.
echo ERROR: %2:\BIG8MB.BIN not found - is %2: the transfer volume?
echo        Reading from that volume works even though executing does not,
echo        so this is a missing file rather than the execute blocker.
goto end

:usage
echo Usage: LOAD98 U S IP    (U = USB drive letter, S = source drive letter, no colons)
echo Example on 2a: LOAD98 F D 192.168.1.100
echo.
echo All three are required - this file sets no environment variables.
echo Run it from C:\, after: copy d:\*.bat c:\
:end
