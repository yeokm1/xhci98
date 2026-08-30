# Guest-side load scripts

These run inside the guest, not on the host. They are the half of the
stability matrix's concurrent-load clause that cannot be driven from the QEMU
monitor: bulk storage writes, Ethernet traffic, and isochronous playback all
have to originate in the guest's own I/O stack.

## There are two pairs, and they are not interchangeable

`LOAD.BAT`/`STAGEF.BAT` are `cmd.exe` programs, for Windows 2000 and later
only. They were written for 2b and reused unchanged on 2d, which is also
Windows 2000.

| pair | target | shell |
|---|---|---|
| `LOAD.BAT` + `STAGEF.BAT` | 2b, 2d (Windows 2000) | `cmd.exe` |
| `LOAD98.BAT` + `STGF98.BAT` | 2a (Windows 98 SE) | `COMMAND.COM` |

**Do not run the Windows 2000 pair on 2a.** It fails in the dangerous shape:
`cmd /c`, `%~f0` and `for /L` are the lines that start traffic, so the load
reports as running while nothing moves. Measured: 28 `usb_msd_cmd_submit` in
the window, against 2d's 10,837. A tool validated on one target is validated
on one target.

What the Windows 98 pair replaces, and why:

| cmd.exe | COMMAND.COM | |
|---|---|---|
| `cmd /c` | `command /c` | the shell is `COMMAND.COM` |
| `start "title" /min` | `start /m` | Win98's `START.EXE` takes no title argument; it would read the title as the program name |
| `%~f0` | an explicit `C:\STGF98.BAT` | no `%~` expansions exist |
| `for /L` | (nothing) | 2a runs no isochronous half; see below |
| `set /a`, `if /i`, `if ( )` blocks | avoided outright | none of the three exist |

The Windows 98 pair sets no environment variable at all. COMMAND.COM's
environment block is small and already carrying `PATH`, `COMSPEC`, `TEMP`,
`PROMPT` and `winbootdir`; an "Out of environment space" mid-start would
half-launch the load. That is why `LOAD98.BAT` requires all three arguments
where `LOAD.BAT` defaults them, and why its disk stream is unbounded rather
than counted.

## On 2a they must be copied to `C:\` and run from there

Windows 98's `COMMAND.COM` can `dir` and `type` a batch file on the QEMU `fat:`
transfer volume but cannot execute one from it: "Bad command or file name",
with the bus idle and nothing running. This was measured against a negative
control on `C:`, after CRLF, 8.3 alias generation, volume size, `COMSPEC` and
internal-vs-external commands had each been ruled out in turn. Reading from
the volume is unaffected, so `BIG8MB.BIN` is still read off it.

In the guest:

```text
copy d:\*.bat c:\
c:
cd \
load98 F D 192.168.1.100
```

Measured 2a drive letters: `D:` is the transfer volume, `F:` the USB disk, and
`C:` the hard disk. The ping target `192.168.1.100` is the gateway; the ASIX
gets 192.168.1.235.

## They are tracked here and copied to the transfer volume

`vm\` is gitignored, so a script that lives only in `vm\xfer\` has no source to
restore from. That is not hypothetical: a mistyped `del *.bin` issued at an
`E:\>` prompt removed `BIG8MB.BIN` from the guest's view of the transfer volume
mid-run. A pattern of `*.bat` would have taken the whole harness with it, and
until this directory existed there was no copy to put back.

Before a stage F run (the two volumes are different directories):

```text
copy scripts\vm-matrix\guest\*.BAT vm\xfer\        (2b, 2d)
copy scripts\vm-matrix\guest\*.BAT vm\xfer98\      (2a)
```

`qemu-win98-run-11v.cmd` mounts `vm\xfer98`; the Windows 2000 launchers mount
`vm\xfer`. Copying all four `.BAT`s to both is harmless and is what the
commands above do; the wrong pair simply is not run.

**These files must reach the guest with CRLF endings.** `.gitattributes` pins
`*.BAT eol=crlf` because COMMAND.COM's label scanner can fail to find a `goto`
target in an LF-only batch file, which turns every error path in these scripts
into a fall-through. A fresh clone gets it right; a working tree where a file
was written after the attribute existed can still hold LF. Check with
`git ls-files --eol scripts/vm-matrix/guest/`; the `w/` column must say `crlf`.

## A `fat:` volume is snapshotted when QEMU opens it

Copying a file into `vm\xfer98` while the guest is running does not make it
visible to that guest; the directory table was built at launch. A new or
changed script needs a relaunch, which is also the only way to undo a deletion
the guest made (those live in the `snapshot=on` layer and vanish on relaunch).
2d's leg recorded `STAGEF.BAT` as never having run for this reason.

## The files

| file | what it does |
|---|---|
| `STAGEF.BAT` | Win2000: the storage + Ethernet load, rotating 8 MB writes and five parallel `ping` streams |
| `LOAD.BAT` | Win2000: one-command wrapper. Starts `STAGEF.BAT` and the isochronous playback, and refuses to start either if the target medium cannot actually be written |
| `STGF98.BAT` | Win98: the same storage + Ethernet load, in COMMAND.COM |
| `LOAD98.BAT` | Win98: one-command wrapper. No isochronous half exists; see below |

## Why the Windows 98 pair has no audio half at all

This is not an omission and not a `NOAUDIO` switch someone might forget to
pass. Windows 98's own audio stack failed five of five in this vehicle in
Phase 9, in a way two independent controls place outside this driver. An audio
churn on 2a would re-measure somebody else's defect and read as a failure of
this stage. The isochronous clause is placed on 2b, where QEMU's wav capture
backend is the outside witness for it. `LOAD98.BAT` says so on screen rather
than staying silent, so an unrun half is never mistaken later for one that ran
and moved nothing.

`LOAD.BAT`'s `NOAUDIO` switch remains, and is for 2d, whose launcher declares
no audio hardware. 2d is Windows 2000 like 2b; audio is absent there as a
scoping decision (2d carries the concurrency half), not because of the Phase 9
measurement.

## Why `LOAD*.BAT` write-tests instead of checking the drive exists

The first version checked `if not exist F:\` and passed happily onto a volume
with 8 MB free, whereupon every copy failed, the window closed, and the run
looked like it was loading four classes when it was loading three. The medium
arrives carrying batch 8-V's payload: 32 files, 260 MB of a 256 MB volume.

Checking that a drive exists is not checking that it can be written to, the
same way `info usb` proves a device occupies a port without proving it is
attached. So the guard is a real write, checked by the file appearing: the
test performs the operation whose failure it is trying to catch.

The canary is 8 MB, not 5 KB, in both pairs. An earlier version copied
`STAGEF.BAT` itself, and 5 KB fits comfortably inside the 8,101,888 bytes free
that defeated the run it was written for. It would have passed that very
failure, and only a still-fuller medium would have caught it. An 8 MB write of
`BIG8MB.BIN` is the property that matters, so that is what it writes. The
pre-delete beside it is what makes the check sound without trusting `COPY`'s
errorlevel, which DOS has never set dependably; a stale `CANARY.TMP` would
otherwise satisfy an `if exist` after a copy that failed.

## Why both disk streams are unbounded

`STAGEF.BAT` used to stop after ten passes, and on both Windows 2000 legs that
was measured to be shorter than the window it loads: about 4 minutes against
2b's eight-minute window (restarted by hand mid-run), and on 2d it had
finished again by t+584 of 600. That is why "storage was idle at the pull" is
recorded against that leg rather than 2b's "actively transferring devices torn
down". A load that stops halfway leaves the tail of the window measuring an
idle bus while still reporting as a three-class load.

The bound was removed for stage G. `STGF98.BAT` never had one, and that
asymmetry is what separated the two targets' teardown readings; stage G's
clause is a stop that lands on traffic, so a stream that has already finished
cannot supply one on either target. The fix was a deletion: no timer, no
restart prompt, no pass arithmetic. Both pairs now loop until the window ends
and the operator closes the box, and the pass number is still echoed so
progress is visible.

The four rotating names make the medium cost constant (32 MB, whatever the
pass count), so a pass count bought nothing here in the first place. The cost
of an unbounded loop is that a full medium makes it spin instead of exiting,
which is the case the 8 MB canary refuses before the window starts.
