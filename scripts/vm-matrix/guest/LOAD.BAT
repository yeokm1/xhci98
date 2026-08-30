@echo off
rem LOAD.BAT - batch 11-V stage F (task 11-V.5), the concurrent-load clause, as
rem ONE command so the whole load starts on a single keystroke rather than two
rem hand-typed command lines racing the measurement window.
rem
rem WHY THIS ARRIVES ON ITS OWN VOLUME.  A QEMU `fat:` volume is a snapshot taken
rem when QEMU OPENS it, so a file written to vm\xfer after launch is invisible to
rem the running guest - that is what left 2d's STAGEF.BAT unrun.  This file rides
rem a SECOND fat: volume attached from the monitor mid-run, whose snapshot is
rem therefore taken with this file already in it.  Copy it to C:\ and the stick
rem can go away again, which matters because the load phase needs all four root
rem ports and this controller only has four.
rem
rem   Usage:  LOAD [U] [S] [IP] [NOAUDIO]
rem             U  = the USB disk's drive letter, no colon   (default F)
rem             S  = the source/transfer volume letter        (default E)
rem             IP = an address on the LAN to ping            (default 192.168.1.100)
rem             NOAUDIO = skip the isochronous half entirely
rem
rem NOAUDIO EXISTS FOR THE 2d SMP GUEST, and it is not a convenience.  2d's
rem launcher (qemu-win2k-smp-run-11v.cmd) declares no `-audiodev` and no sound
rem hardware AT ALL, so `device_add usb-audio` would be refused outright there
rem ("no default audio driver available", measured guestlessly on 2b) and a guest
rem player has nothing to play through either way.
rem
rem BE PRECISE ABOUT WHY, because the shorthand in circulation is wrong: the
rem *measured Phase 9 reason* (that OS's audio stack failing five of five) is why
rem audio is excluded on **2a**, not on 2d.  2d is Windows 2000 like 2b.  Audio
rem is absent here because the roadmap's 2d clause is "the SMP guest under
rem concurrent transfer load" - 2d carries the CONCURRENCY half - and the
rem isochronous clause was placed on 2b, where the wav-capture backend is the
rem outside witness for it.  So this is a SCOPING decision baked into the
rem vehicle, not a measurement that forbids audio on 2d.
rem
rem On that guest mplay32 EXISTS - it ships with
rem Windows 2000 - so the `if exist` probe below passes and playback starts
rem against a machine with no sound device, which fails in the worst available
rem shape: a MODAL ERROR BOX, four of them, arriving during the measurement
rem window.  Stage E measured that a modal dialog blocks the PnP install queue on
rem both targets, and this is also the window in which the operator is typing
rem into a console through the monitor.  So the skip is explicit rather than left
rem to a probe that cannot see the difference between "the player is installed"
rem and "the player has something to play through".
rem
rem WHAT IT STARTS, and why both halves are needed:
rem   STAGEF.BAT   bulk OUT (disk writes) + interrupt/bulk (five ping streams).
rem   TONE120.WAV  ISOCHRONOUS, which nothing else in stage F reaches.  Measured
rem                on 2b in batch 11-V: twenty usb-audio replug cycles opened ONE
rem                endpoint per device and moved no cancels at all, because
rem                Windows opens an iso pipe when an application STREAMS, not
rem                when the device binds.  Cycling an audio device is not
rem                isochronous traffic.
rem
rem The tone is 120 s and plays four times, which covers an 8-minute window
rem without anyone re-starting it.

set U=%1
set S=%2
set IP=%3
set NOAUDIO=%4
if "%U%"=="" set U=F
if "%S%"=="" set S=E
if "%IP%"=="" set IP=192.168.1.100

if not exist %S%:\STAGEF.BAT (
  echo ERROR: %S%:\STAGEF.BAT not found - is %S%: the transfer volume?
  goto end
)
if not exist %U%:\ (
  echo ERROR: %U%: does not exist - is the USB disk attached, and on that letter?
  goto end
)

rem THE DRIVE EXISTING IS NOT THE DRIVE BEING WRITABLE, and the difference cost
rem a whole load window on 2b.  usb8v-2b.img arrives carrying batch
rem 8-V's payload - 32 files, 260,048,090 bytes used, 8,101,888 free - so every
rem 8 MB copy failed instantly, STAGEF.BAT's ten passes completed in about a
rem second, its window closed, and the run reported four classes under load while
rem the trace showed ZERO usb_msd_cmd_submit.  The first version of this file
rem checked `if not exist %U%:\` and passed.
rem
rem So the guard PERFORMS THE OPERATION whose failure it is trying to catch: a
rem real write of a real file.  Same rule as `info usb` proving a port is occupied
rem without proving the device is attached - test the property you actually
rem depend on.
rem
rem AND THE CANARY IS 8 MB, NOT 5 KB.  Corrected, while transcribing
rem this file into its Windows 98 variant: the first version copied STAGEF.BAT
rem itself, and 5 KB fits comfortably in the 8,101,888 bytes free that defeated
rem the run above - so this guard would have PASSED the very failure it was
rem written for, and only the medium being *even fuller* next time would have
rem caught it.  "A real write of a real file" was not the property depended on;
rem an 8 MB write was.  It costs about four seconds against the throttled backend.
rem
rem The pre-delete is what makes this sound without trusting COPY's errorlevel,
rem which DOS has never set dependably: a stale CANARY.TMP from an earlier run
rem would otherwise satisfy the `if exist` after a copy that failed.
if not exist %S%:\BIG8MB.BIN (
  echo ERROR: %S%:\BIG8MB.BIN not found - is %S%: the transfer volume?
  goto end
)
if exist %U%:\CANARY.TMP del %U%:\CANARY.TMP
echo Checking %U%: can take an 8 MB write...
copy /Y %S%:\BIG8MB.BIN %U%:\CANARY.TMP >NUL
if not exist %U%:\CANARY.TMP goto nospace
del %U%:\CANARY.TMP >NUL
goto writable

:nospace
echo.
echo ERROR: %U%: cannot be written to.  The load would report as running while
echo        moving no data at all.  Free space and re-run:
echo.
dir %U%:\ | find "bytes free"
echo.
echo        del %U%:\*.bin      (the batch 8-V payload is the usual occupant)
goto end

:writable

echo.
echo ============================================================
echo  batch 11-V stage F - concurrent load, all four classes
echo    USB disk    : %U%:
echo    source      : %S%:
echo    ping target : %IP%
echo ============================================================
echo.

rem The isochronous half is checked BEFORE anything is launched, so a missing
rem player or tone refuses the whole run rather than starting the traffic and
rem then reporting both halves running with one absent.  The skip is announced,
rem not silent: an unrun isochronous half must never be mistaken later for one
rem that ran and moved nothing.
set PLAYER=
if /i "%NOAUDIO%"=="NOAUDIO" goto preflightdone

rem mplay32 is Windows 2000's command-line-drivable player; sndrec32 is the
rem fallback.  Neither is assumed.
if exist %SystemRoot%\system32\mplay32.exe set PLAYER=mplay32
if "%PLAYER%"=="" if exist %SystemRoot%\system32\sndrec32.exe set PLAYER=sndrec32
if "%PLAYER%"=="" goto noplayer
if not exist %S%:\TONE120.WAV goto notone

:preflightdone
echo Starting disk writes and five ping streams...
start "stage F - traffic" /min cmd /c "%S%:\STAGEF.BAT %U% %S% %IP%"

if /i "%NOAUDIO%"=="NOAUDIO" goto noaudio
echo Starting isochronous playback - 4 x 120 s tone (%PLAYER%)...
start "stage F - audio" /min cmd /c "for /L %%i in (1,1,4) do %PLAYER% /play /close %S%:\TONE120.WAV"
echo.
echo Both halves launched.  Tell the agent they are running.
goto tail

:noaudio
echo.
echo ISOCHRONOUS HALF SKIPPED BY REQUEST (NOAUDIO) - this guest has no audio
echo device, and audio is a 2b-only leg of stage F.  Three classes, not four.
echo Traffic half launched.  Tell the agent it is running WITHOUT audio.
goto tail

:noplayer
echo.
echo ERROR: neither mplay32 nor sndrec32 found - the ISOCHRONOUS half cannot run.
echo        Nothing was launched.  Install a player and re-run, or re-run with
echo        NOAUDIO as the fourth argument (LOAD %U% %S% %IP% NOAUDIO) to run
echo        the three-class form on purpose.
goto end

:notone
echo.
echo ERROR: %S%:\TONE120.WAV is missing - the ISOCHRONOUS half cannot run.
echo        Nothing was launched.  Re-make the media, or re-run with NOAUDIO
echo        as the fourth argument (LOAD %U% %S% %IP% NOAUDIO) to run the
echo        three-class form on purpose.
goto end

:tail
echo On a disk error box when the device is pulled, press A for Abort -
echo that error IS the test.
:end
