# The snapshot reader - reading this driver's own state off a running machine

This is the reference for the snapshot route (the door's USBUSER
`PassThru` request, its schema and regions) and for the files it
produces. Since `3.1.0.0` the route is read by the xHCI98 USB Tree:
`XHCITR /snapshot`, `/dump` and `/probe` are what were `XHCISNAP
-report`, `-dump` and `-probe`, and File > Save Snapshot... and the
"Snapshot" part of Save Report and `/report` run `/snapshot` (design
record 25 section 15). Its engine is `xhcitr\tr_snap.c`, moved from
`xhcisnap\xhcisnap.c`; its self-test is `xhcitr\snaptest.cmd`
(`XHCITR /snaptest`), and the counter-name check is
`scripts\check-counter-names.ps1`. `XHCISNAP.EXE` and its `SNAPON.BAT`,
`SNAPGET.BAT` and `SNAPOFF.BAT` were retired in `3.1.0.0` (roadmap-usb4
task 42-A.4); the logging level is set by XHCITR's View > Logging
level... or `XHCITR /setlevel` (record 25 section 7.5). The sections
below keep XHCISNAP's names where they record its history; read
`XHCISNAP -report` as `XHCITR /snapshot` for a current build.

The snapshot reader (`xhcitr\tr_snap.c`; `XHCISNAP.EXE` up to `3.0.0.0`)
reads `xhci98.sys`'s extension, its raw PORTSC array, (from
2.0.0.0) its enabled slots and (from 2.2.0.0) its root ports' enumeration and
counters from user mode, and writes a report a user can send back.

Two drivers answer it. From 2.0.0.0 `xhci98.sys` is the successor host
controller driver (`docs/contributing/roadmap-hcd.md`, design record 13): it
owns its driver object, creates its own `\DosDevices` name (`HCD<n>` on 98 SE
and ME, `XHCI98HC<k>` on NT from 3.3.0.0) and answers the
same USBUSER `PassThru` request through its own door (`src/hcd_door.c`, schema
5 unchanged, the slots and HCD regions added). The 1.x miniport ran under usbport and
was reached through usbport's `PassThru` vendor escape. Most of this document
is the 1.x history; where it says usbport or the miniport, that is the 1.x
route, which the tool still reads.

It ships. The kernel side is in every build flavour of the driver and the
reader is in `XHCITR.EXE` and `XHCITR64.EXE`, published in
`releases/<version>/tools/xhcitr/` (up to `3.0.0.0` `XHCISNAP.EXE` in
`tools/xhcisnap/`; before `2.4.0.0`, `releases/<version>/xhcisnap/`). It began as a bench companion
to a probe build that could not ship either; it is part of the product now.

## Why it exists

On Windows 98 this is the only way to get anything out of the driver, and that
is structural rather than bad luck. Windows 98 has two logging families: ring 3
does the writing through a device object, or ring 0 writes a file. This driver
is locked out of both. `USBPORT_RegisterUSBPortDriver` overwrites
`IRP_MJ_CREATE`, `CLOSE` and `DEVICE_CONTROL` on the miniport's driver object,
so other Windows 98 drivers can log because they own a driver object and this
one runs inside somebody else's. The ring-0 file sink was retired after never
having written a byte outside a virtual machine.

The driver already records everything a maintainer needs: the counters and the
note ring, both inside `XHCI_EXTENSION`. What was missing was a way to read,
not a way to record. The route needs no driver object of our own, because the
port driver this one lives inside already published one and forwards through
it.

## What it needs

- The level. `XhciLogVerbosity` defaults to `2` since `3.0.0.0` (`0`
  before, and an update keeps the value an earlier install wrote); `0` is
  off outright: the driver answers the snapshot the way a build without the
  channel would. XHCITR's View > Logging level... or
  `XHCITR /setlevel xhci98=<n>` sets it on every xhci98 controller the
  machine has (record 25 section 7.5). Then restart.
- Nothing else. No service, no `regedit`, and no `IOCTL_USB_DIAGNOSTIC_MODE_ON`;
  the route itself is ungated.

## Using it

```text
XHCITR /snapshot [NAME] [/c N] [/nosysinfo]
                         the field report: NAME.DEV, then the dump of
                         controller N (default 0) into NAME.TXT/.BIN/.PSC,
                         in the current directory. NAME is an 8.3 base,
                         else made from the clock (mDDhhmmS; see "The field
                         report"); refused (exit 6) rather than overwritten
                         when a file of the set exists. /o NAME is the same
XHCITR /dump [/c N] [/o BASE] [/nosysinfo]
                         the dump alone into BASE.TXT/.BIN/.PSC, replacing
                         an earlier set of that BASE
XHCITR /probe            check the ROUTE only, with four controls
XHCITR /snapshot /?      the long help
/nosysinfo               a dump whose header skips reading the system
                         details (see "The machine it was captured on")
```

With the output redirected these write their text there; otherwise a
message box shows it, with the current directory. A word beginning with `-`
after `/snapshot`, `/dump` or `/probe` is refused before anything is read.
In the window, File > Save Snapshot... runs `/snapshot` in a folder the user
picks, for the first xhci98 controller. Save Report and `XHCITR /report`
end with a "Snapshot" part: the `.DEV` once (from the run for the first
xhci98 controller, or for controller 0 when none is listed) and each
xhci98 controller's dump `.TXT`, whole, withheld when its level is 4 or
cannot be read (record 25 section 15). The raw `.BIN` and `.PSC` come only
from `/snapshot` or `/dump`.

The ladder: 0 off; 1 the channel with counters only and the ring still off;
2 adds the note ring (this is the log, use this one); 3 adds the PORTSC table
to the `.TXT`; 4 adds everything including kernel addresses.

The published sequence: level 2 (the default), restart, reproduce, Save
Report (or `/snapshot` when the files are asked for). Up to `3.0.0.0` this
was `XHCISNAP -verbosity 2` (also `-on`, `-disable` and `-force`), restart,
reproduce, `XHCISNAP -report`, with `SNAPON.BAT`, `SNAPGET.BAT` and
`SNAPOFF.BAT` running the steps; the sections below keep XHCISNAP's names
where they record that history.

The level is about the next start, never about what a running driver did.
The value is read once per start, so a key edited since the last boot has
been read by nothing, and a controller may be running at a level its key no
longer names. The registry cannot tell those apart. The level actually
applied travels in a dump's header, which is where to read it.

Once a capture at level 3 or 4 has been sent, set the level back to 2.
While the level is above 0, any local user who can open the door can
read this driver's diagnostic state through the door. That is accepted
rather than overlooked: under the 1.x miniport this driver could not tighten
the door, because usbport owned the device object, hardcoded the name,
completed `IRP_MJ_CREATE` with no work and left the IOCTL `FILE_ANY_ACCESS`,
so the registry value was the whole access story: Administrators on Windows
2000, nothing at all on Windows 98, which has no user boundary. The content
is this driver's own counters, notes, PORTSC and its extension (raw in the
`.BIN` at any level from 1 up), with kernel addresses in the note ring and
the `.TXT` only at level 4; no user data and no other process's memory.

### Three files, and only one of them is the report

- `.TXT` is the plain-text companion. It is built entirely out of what the
  driver puts on the wire, so it needs no offset table. This is the one to paste
  into an issue, at levels 1 to 3. What goes in is gated by the ladder: the
  header block always, the note ring's text at 2 and above, the PORTSC table at
  3 and above, and kernel addresses at 4, which the driver refuses below that
  rung. So a level-4 `.TXT` is a maintainer's artifact: read it before
  publishing it, or take the report at 2.
- `.BIN` is the raw extension image, the same artifact the QEMU live-counter
  reader produces, so `scripts/local/regen-offsets.cmd`, `offsets.txt` and
  `counters.py` / `readcounters.ps1` decode it. It decodes only against a table
  from the driver's own tree, which the maintainer has and the user does not.
  It is the attachment, not the report.
- `.PSC` is the raw PORTSC array. When the driver reports that the controller
  has no usable register mapping (`SNAP_S_NO_MMIO`), PORTSC was not read and
  the `.PSC` is published as a 0-byte file beside a complete `.BIN`; the
  screen says so at the time. A controller whose root hub reports no ports
  (`PortCount` 0) yields the same 0-byte `.PSC` with no such line: the
  driver had nothing to read, and the empty file is the reading.

A capture that fails before publication leaves the previous set alone. Each raw
region is written to `NAME.BIN.TMP` / `NAME.PSC.TMP` and the pair is renamed
over the final names only once both are in hand, with the old `.TXT` retired at
the same moment. So `-c` naming the wrong controller, or a channel that was
never switched on, leaves `NAME.BIN`, `NAME.PSC` and `NAME.TXT` as they were,
rather than truncating the first and leaving the other two beside it looking
like a set. A failure during publication is loud: if either rename fails the
tool deletes both final raw names **and the old `.TXT`** and says no dump was
published, so what is left is an absence and not a mixture. (The `.TXT` was
left standing on that branch until the 2026-09-16 audit, which made the newest
file in the directory a report of a set that had just been deleted.)

That replacing is `-dump`'s and `-o`'s, where the name is the user's own.
**`-report` never replaces or deletes a file it did not create** (task
38.1): its temporaries are created with `CREATE_NEW`, its
`.TXT` with an exclusive create, and its two regions are renamed with a bare
`MoveFileA`, which refuses a name that exists, with nothing deleted first. A
failure removes only this run's own files. See "The field report" for what
happens when another run takes one of its names.

### The field report: `-report`, the `.DEV` and the three batch files (2.4.0.0)

Roadmap-hcd task 38.1 (owner, 2026-10-12 and 2026-10-13): a reporter runs one
batch file and attaches what it names, rather than following a list. The tool
itself reads what the field reports ask for, so every target reports the same.

**The name.** `-report` names its files from the local clock, because a batch
file cannot (COMMAND.COM on 98 SE and ME has no `%DATE%` or `%TIME%`, and
cmd.exe's follow the locale). The base is eight characters, `mDDhhmmS`:

- `m`, the month as one character: `1`-`9`, then `A` (October), `B`, `C`;
- `DD`, `hh` and `mm`, the day, the hour (24-hour) and the minute;
- `S`, a letter `A`-`Z`: the first for which **no** file of the set this run
  could write already exists in the current directory - `.DEV`, `.TXT`,
  `.BIN`, `.PSC`, and each of those with `.TMP` after it.

So 13 October 2026 at 14:19 is `A131419A`, and a second run in the same minute
is `A131419B`. Nothing is ever overwritten; the `.DEV` is created with an
exclusive create, so a name that appeared since the letter was chosen is
refused too. With all 26 letters taken the tool writes nothing and exits 6;
a clock whose month, day, hour or minute is out of range is refused, exit 7.
The full date and time, with the year, is written inside the `.DEV` and the
`.TXT` ("local time"; every dump's `.TXT` carries the line since 2.4.0.0).
The files always go to the current directory. The screen says "the current
directory" and never prints its path, which is often the user's profile and
so carries the user's name.

**A name of the reporter's own: `-report NAME`** (owner, 2026-10-13;
`SNAPGET.BAT NAME` runs it). `NAME` is the argument after `-report` unless
that is `-o`, `-c` or `-nosysinfo`, so a `-X`, a `/?` or a `-help` there is a
name, and refused; `-report -o NAME` gives the same name the older way, and
the two at once are refused (exit 2). `NAME` replaces the clock's base and
is held to an 8.3 base in the current directory, by the tool and not only by the batch:
1 to 8 characters, each a letter, a digit, `_` or `-` (`-` not first, so it
never reads as an option), with no path, no drive, no dot and no extension,
and not a DOS device name (`CON`, `PRN`, `AUX`, `NUL`, `COM0`-`COM9`,
`LPT0`-`LPT9`). The set is narrower than 8.3 allows on purpose: a name a
reporter types is one the maintainer types back, and `&`, `^`, `%`, `!` and
the parentheses mean something to cmd.exe. A name outside the rule is
refused with the reason and exit 2, writing nothing. A name any file of whose
set (`.DEV`, `.TXT`, `.BIN`, `.PSC`, or one of those with `.TMP`) already
exists is refused, not overwritten: exit 6, nothing written, the existing
file as it was. Past that check the run is the clock-named one exactly -
the same exclusive creates and the same exit 8 when a file of the set
appears meanwhile (run it again under another name, or none). The clock is
not needed for the name, so a given name is never refused with exit 7; the
local time is still written inside. Beside `-dump`, `-o` keeps its old
meaning, a basename or path the dump replaces.

**Another run in the same directory.** The letter is chosen as one no file
has, but another run - a second `-report`, or a `-dump -o` with that
basename - can make one of the set between the choice and the write. Every
file `-report` makes is made exclusively, so the other file is never
replaced: if the `.DEV` itself is taken nothing is written; if a dump file
(`.TXT`, `.BIN`, `.PSC` or a temporary) is, before the dump or at its
publication, the dump is not kept under the name, this run's own files of it
are removed, the other file is left as it was, the screen and the `.DEV` name
it, and the screen lists only the files this run made. Exit 8. Run it again:
it takes the next letter (with `-o NAME`, give another name, or none).

**What it writes, in order.** First `BASE.DEV`, the device report, then the
ordinary dump into `BASE.TXT`, `.BIN` and `.PSC`. The `.DEV` is written and
closed before the controller is opened, so it exists whatever the dump does -
the driver not loaded, the controller at a Code, the channel off - and the
dump's result, with the reason when there is none, is appended to it
afterwards. A controller at a Code does not by itself mean no dump: on 98
SE and ME a disabled one (Code 22) keeps its `\\.\HCD0` open and the dump is
taken (the p38-1 legs, 2026-10-13), so when a PCI xHCI controller `xhci98`
drives is reported by Device Manager as disabled, at a Code or not started,
the `.DEV`'s dump result and the screen say "dump taken from a controller
Device Manager reports as disabled (Code 22)" (or its Code), naming the
device; with more than one such controller, as one that may be it. No dump
comes from a controller whose driver is not loaded (as before the restart
after an install) or whose channel is off. The screen lists every file
written, says what the exit code means and ends with the attach line: the
`.DEV` and the `.TXT`; the `.BIN` and `.PSC` only if the maintainer asks.

**The `.DEV`** is plain ASCII text with CRLF line endings:

- the tool's version and build, the local time, the controller index asked
  for, and the same "captured on" block as the dump's header (below), under
  the same privacy rule and the same `-nosysinfo`;
- every devnode that is a PCI xHCI controller (a hardware or compatible id
  carrying class code `CC_0C0330`), whatever drives it or if nothing does,
  and every devnode `xhci98.sys` or `xhciuas.sys` drives (on NT by its
  service; on 98 SE and ME by `NTMPDriver` in its driver key, the signal
  `-verbosity` uses to find the driver's keys there). For each: the device
  id, its description, its service, its driver key with `NTMPDriver`,
  `InfSection`, `DriverVersion` and `DriverDate`, the status flags and the
  problem code (`CM_Get_DevNode_Status`, with a short name for the common
  codes: 10, 28, 31, 39, 52 and others), and the allocated resources
  (memory, I/O, IRQ, DMA, bus numbers), or the boot configuration when
  nothing is allocated;
- the installed files - `xhci98.sys`, `xhciuas.sys`, `xhcihlpr.sys` (and
  `xhcimsi.sys`, the `2.3.x` helper a `2.4` update retires, so a leftover
  shows) and `usbd.sys` in the Windows directory's `System32\Drivers` (where both INFs
  copy them, dirid 10, on every target), `usbui.dll` in the system directory
  (dirid 11: `SYSTEM` on 9x, `system32` on NT) - each present or not, its
  size, file version and SHA-256. WOW64's redirection is turned off for the
  read, so a 64-bit Windows reports its native files - only around each
  call that opens an installed file, and restored straight after it.
  `version.dll` is loaded before any of them, with the redirection on: off,
  the load would find the native 64-bit DLL, which an x86 process cannot
  load.

**Where the device readings come from.** `cfgmgr32.dll`, loaded at run time
(the import table still names only `KERNEL32.dll` and `ADVAPI32.dll`): the
tree is walked with `CM_Locate_DevNode`, `CM_Get_Child` and `CM_Get_Sibling`,
the properties are `CM_Get_DevNode_Registry_Property`'s (or the devnode's
`Enum` key where that routine is missing), and the resources are
`CM_Get_First_Log_Conf`, `CM_Get_Next_Res_Des` and `CM_Get_Res_Des_Data`'s,
decoded from either the NT layout or the 9x one by their size, and printed
as raw bytes when neither fits. 98 SE's and ME's `cfgmgr32.dll` hands back a
memory descriptor in NT's layout - the 32-byte header, then 36-byte ranges -
with the range-size field left at 20, 9x's own range size; that is decoded
too (the p38-1 legs, 2026-10-13: 68 bytes, `FEBF0000-FEBF3FFF`, as Device
Manager showed it; `-selftest-res` holds it). Raw bytes are printed for a
memory, I/O, DMA, IRQ or bus-number
descriptor; any other type - class-specific, device-private - is named with
its size and never printed, since it can carry anything). Under WOW64 the
x86 `cfgmgr32.dll` answers
the log configuration calls with `CR_CALL_NOT_IMPLEMENTED` (read on a 64-bit
Windows 11 build host, 2026-10-13), so the resources then come from
`HKLM\HARDWARE\RESOURCEMAP\PnP Manager\PnpManager`, the PnP manager's raw
list under the device's PDO name, which also carries the message-signalled
flag and the message count. On 98 SE and ME, if `cfgmgr32.dll` does not
answer, the devnodes come from `HKEY_DYN_DATA\Config Manager\Enum` with
their status and problem but no resources. Any routine missing or failing is
written as "not read: <why>", never as a silence. On 98 SE and ME (the
p38-1 legs, 2026-10-13) the devnodes, their status and problem code came
through `cfgmgr32.dll`, and the IRQ and, since that leg, the memory range
were decoded. On 2000, XP, XP x64 and 7 (the same legs) the allocated
descriptors came back with a count of 0 and one range still after the
header (memory 68 bytes, 32 + 36; IRQ 32 bytes on 2000, 20 + 12, and 36 on
XP and later, 24 + 12), which the decode now takes, the fields being in the
header: `FEBF0000-FEBF3FFF` everywhere, IRQ 11 on 2000, 10 on XP and XP x64,
and on 7 the message-signalled number Device Manager shows (`-selftest-res`
holds every one of these images). On NT a disabled controller has no
`\\.\HCD0`, so it gives no dump; the `.DEV` says Code 22 and why. **None of
it has yet been read on Vista.**

A driver file 98 SE or ME copied while the old one was in use sits under a
`.TMP` name until the next restart renames it (ME right after an install had
`XHCI98.TMP` and no `xhci98.sys`); a file not present whose `.TMP` is there
is listed as "not present; its .TMP is, pending rename at the next restart".
The 98 SE build's letter (`2222 A`) follows the build number with a space,
so the OS line is not wrapped with the letter on a line of its own.

**Privacy.** A USB device's instance id can be its serial number, so the
instance part of every id not under `PCI\` or `ROOT\` is printed as
`(instance withheld)`. Every string goes through the same cleaning and
name-withholding as the "captured on" block, and the rule does not depend on
`-nosysinfo` (task 38.1). With `-nosysinfo` this
machine's names are not read, so nothing can be screened for them: every
free-text string (a description, `InfSection`, `DriverVersion`,
`DriverDate`, the Windows directory) is then printed as "withheld: not
screened", the installed files are named by their directory's role rather
than its path, and the `.DEV` says so. Identifiers the system generates -
the device id with its instance part withheld as above, the service name,
the driver key - are still printed, but only in the letters, digits and
punctuation an id is made of. The same holds on a machine whose computer
name could not be read.

**Exit codes of `-report`:**

| Exit | Meaning |
|---|---|
| 0 | the `.DEV` and the dump's `.TXT` complete |
| 2 | a usage error (a `-o NAME` outside the 8.3 rule, for one); nothing written |
| 3 | the `.DEV` complete; the dump taken, but its `.TXT` incomplete or the extension's size disagreed (the dump's own 3) |
| 4 | the `.DEV` complete; no dump (the device did not open or did not serve it; the `.DEV` says which) |
| 5 | the `.DEV` could not be written in full |
| 6 | no free name: all 26 for this minute taken, or with `-o NAME` a file of that name's set already here; nothing written or overwritten. Wait a minute, or give another name (or none) |
| 7 | the clock is not a valid date and time; nothing written (never with `-o NAME`). Set the clock |
| 8 | a file of this run's set appeared after its name was chosen (another run made it): it was left as it was, and no dump is kept under the name; the `.DEV`, when it was written, says which file. Run it again: it takes the next letter, or give another name |

**The three batch files**, `snapon.bat`, `snapget.bat` and `snapoff.bat` in
this directory, shipped as `SNAPON.BAT`, `SNAPGET.BAT` and `SNAPOFF.BAT`
beside `XHCISNAP.EXE` in the release's `tools\xhcisnap\`. Each is one file
for COMMAND.COM (98 SE and ME) and cmd.exe (2000 to 7), in the shape
`src\portsw-on.bat` has and its gate holds: `@echo off`, the pinned
`%OS%` split, only COMMAND.COM built-ins and `XHCISNAP`, ASCII and CRLF,
lines of 127 characters or fewer, no `%DATE%` or `%TIME%`, and `pause`
last. Each first checks `XHCISNAP.EXE` is beside it (`IF EXIST XHCISNAP.EXE
GOTO TOOL`) and says so if not; otherwise **`XHCISNAP` is the last thing it
runs, and nothing but `pause` follows** (owner, 2026-10-13: the last line
of output names the files to attach). The tool prints the outcome the
batches' `IF ERRORLEVEL` ladders used to - the advice for each exit code,
Run as administrator where a registry write was refused on 2000 to 7
(`-on` and `-disable` exit 1 for any key not written, an access refusal
included), and what to do next - and its last line is the attach line:
`Attach: A131554A.DEV A131554A.TXT (the .BIN and .PSC only if asked)`,
`Attach: A131554A.DEV` with no dump, `Nothing to attach: nothing was
written.` when nothing was, `Nothing to attach yet: restart Windows,
reproduce, then run SNAPGET.BAT.` after `-on`, and `Nothing to attach.`
after `-disable`. The gate holds the shape: the tool line is reached by
that one `IF EXIST` with its label just above it, only `rem` and `echo`
come between the split and the test, only labels and the last `pause`
follow the tool line, and no line reads the exit code. Each batch says
first that 2000 and XP need an administrator and Vista and 7 Run as
administrator.

Two of them take one optional argument (owner, 2026-10-13), passed on as
`%1` at the end of the batch's one tool line and read nowhere else: no `IF`
tests it (task 38.1: `SNAPGET "A&B"` broke cmd.exe's
`IF "%1"==""` before the tool could refuse it, and `SNAPON "1"` failed the
batch's compare though the C runtime hands the tool a plain `1`), so the
default and the check are the tool's. The gate admits `%1` only on that
line, which must be exactly `XHCISNAP -on %1` or `XHCISNAP -report %1`, and
no other percent variable (`%2`, `%0`, `%*`, `%~1`, `%DATE%`) at all. A
word the user escapes for the prompt (`A^&B`) arrives unescaped and splits
the line; that is the user's own command, and no form of `%1` both
survives a quoted word and protects an escaped one.

- `SNAPON.BAT [LEVEL]` runs `XHCISNAP -on LEVEL`: no `LEVEL` is 2, the log,
  and `LEVEL` must be exactly `1`, `2`, `3` or `4` (quotes the C runtime has
  stripped, but no sign, no leading zero, nothing after it); `0` is refused
  with a pointer to `SNAPOFF.BAT` (it is off), anything else is refused
  naming the levels, all exit 2 with nothing changed. The batch passes on
  its first word only, so a second is ignored (`SNAPON 1 4` is level 1). The
  write is `-verbosity`'s, with the same key matching and exit 1 for any key
  not written. `XHCISNAP` then says, at 2, what `SNAPON.BAT` takes; at 1,
  that the channel was not switched on and (on 2000 to 7) to run it as an
  administrator; at 0, the channel is on: restart Windows (or disable and
  enable the controller in Device Manager), reproduce the problem, run
  `SNAPGET.BAT`. Nothing to attach yet.
- `SNAPGET.BAT [NAME]` runs `XHCISNAP -report NAME` (`-report` alone with no
  word). `-report` says what its exit code means - at 2 the name was refused
  and why; at 3 the `.TXT` is not complete, say so; at 4 no dump, the `.DEV`
  says why, and SNAPON, restart, reproduce, SNAPGET; at 5 copy the directory
  somewhere writable (Run as administrator on Vista and 7); at 6 the name is
  taken or, with none, all 26 are; at 8 run it again, under another name if
  one was given - then that the channel stays on until `SNAPOFF.BAT`, and
  last the attach line.
- `SNAPOFF.BAT` runs `XHCISNAP -disable`, which says the change takes
  effect at the next start (or, refused, to run it as an administrator), and
  last: Nothing to attach.

The self-tests (`selftest.cmd`) hold the name rule (`-selftest-stamp`:
months 1, 9, 10, 11 and 12, midnight, a taken letter moving to the next
whichever file of the set holds it, Z the last, all 26 taken refused,
impossible clock fields refused), the SHA-256 on the real `/O2` binary
(`-selftest-sha256 BASE`: FIPS 180-2's one-block, two-block and million-`a`
examples, the empty message, the padding boundaries at 55, 56, 63, 64 and
65 bytes and at 127 and 128 fed in uneven pieces, the length carry across
the low 32-bit word driven through the count and bit-length routines
directly, and the file path: a million `a` read back, a read that fails, a
read cut short, a file held exclusively, a directory, a deleted file),
`-report`'s publication on its own (`-selftest-publish BASE`: clean, a
`.TXT` taken first, a `.PSC` taken after the `.BIN` was renamed, a temporary
taken first - the other file keeps its contents, this run's are gone), and
`-report` end to end against a controller index no machine has: exit 4 with
the `.DEV` written, a second run taking the next letter, `-o REFUSED.X`
refused for its dot, the screen never naming the directory, `-nosysinfo
-report` printing no unscreened text, and with `XHCISNAP_FAULT=clash` (a
`.TXT` made after the name is chosen) exit 8 with that file intact; and
`-report -o NAME`: a valid name used as the base (exit 4, `NAME.DEV`), the
same name again (in other case) refused with exit 6 and the first `.DEV`
byte for byte as it was, a name whose `.PSC.TMP` alone exists refused with
that file intact, and a path, a drive, a dot, 9 characters, an empty name,
`CON`, `com1`, a leading `-` and `A&B` each refused with exit 2, writing
nothing; `-report NAME` the same way, with `-X`, `/?`, `-help`, `-dump`,
`COM0`, `LPT9` and `A%B` among the refused names and `-report A -o B`
refused; `-selftest-on` (the `-on` parse, no registry): none is 2, `1` to
`4` and a quoted `"4"` taken, `0`, `5`, `00`, `01`, `+1`, `x`, an empty word
and two words refused with exit 2, and `-on 0` itself pointing at
`SNAPOFF.BAT`; and the real `SNAPGET.BAT` and `SNAPON.BAT` run through
cmd.exe beside a copy of the build, with `"A&B"`, `"-X"` and `CON` reaching
`XHCISNAP`'s refusal with no cmd.exe error first, and with
`XHCISNAP_FAULT=noreg` (the level parsed, nothing written) a quoted `"1"`
taken as level 1 and no word as 2, each batch's last line before `pause`'s
prompt being `XHCISNAP`'s attach line; and `-selftest-res`, the memory
descriptors 98 SE and ME and 2000 to 7 hand back decoded as
`FEBF0000-FEBF3FFF`, one of neither layout left raw, and the IRQ
descriptors of 2000, XP and 7 decoded as 11, 10 and a message-signalled
interrupt.

### The machine it was captured on (2.3.0.0)

From 2.3.0.0 (roadmap-hcd.md task 36.47) the `.TXT`'s header block, which
is written at every level and is the part users paste into an issue, also
names the system the dump was taken on, under "captured on":

- **OS**: `GetVersionEx`'s platform, version, build and service pack
  (`szCSDVersion`), with a name: Windows 95, 98, 98 SE, ME, 2000, XP,
  XP x64 or Server 2003 (told apart by the product type and the native
  processor), Vista or Server 2008, 7 or Server 2008 R2, "Windows 8 or
  later" (every later Windows reports 6.2 to a program with no
  compatibility manifest), or "unknown".
- **Bitness**: x86, or 64-bit Windows with this x86 tool under WOW64
  (`IsWow64Process` and `GetNativeSystemInfo`, looked up at run time so the
  EXE still loads on 98 SE, ME and 2000). The tool is x86 on every target.
- **The machine**, only where the OS records it in the registry: on Vista
  SP1 and later `HKLM\HARDWARE\DESCRIPTION\System\BIOS`'s
  `SystemManufacturer`, `SystemProductName`, `SystemFamily`, `SystemVersion`,
  `BaseBoardManufacturer`, `BaseBoardProduct`, `BIOSVendor`, `BIOSVersion`
  and `BIOSReleaseDate`; on 2000 and XP (and a Vista without that key)
  `HKLM\HARDWARE\DESCRIPTION\System`'s `SystemBiosVersion` and
  `SystemBiosDate`, with "make/model: not available on this OS"; on XP x64,
  which has neither of those two values and keeps the SMBIOS strings in the
  `BIOS` key as Vista SP1 does (2026-10-10 matrix guest export), that key's
  values through the same allow list - 2000, XP and XP x64 read the `BIOS`
  key only when `SystemBiosVersion` and `SystemBiosDate` are both absent; on
  98 SE and ME `HKLM\Enum\Root\*PNP0C01\0000`'s `BIOSName`, `BIOSVersion` and
  `BIOSDate`, which is what the system board's devnode carries there. Each
  value is printed only where it exists: on QEMU's SeaBIOS, ME's devnode
  holds `BIOSVersion` and `BIOSDate` and no `BIOSName`, and 2000's key holds
  `SystemBiosDate` and no `SystemBiosVersion` (2026-10-10 guest exports).
- **CPU**: `ProcessorNameString`, else `Identifier`, from
  `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0`, and the processor
  count (`GetSystemInfo`).
- **RAM**: the physical memory, from `GlobalMemoryStatusEx` where it exists
  and `GlobalMemoryStatus` before it.

**It never prints the computer name, the user name or the domain as a
field, and never reads a serial number, UUID, asset tag or SKU value.** The
values printed are a fixed allow list named in the source, never a key's
whole contents, and each one names a make, a model, a firmware or a
processor rather than one machine. Those are still the firmware's free text,
which an integrator (or QEMU's `-smbios`) can fill with anything, so a
second guard stands behind the list: the tool reads the computer name, the
user's name, the domain (`USERDOMAIN`, the domain or workgroup joined, the
DNS domain) and on 98 SE and ME the workgroup, for this comparison only,
and prints any registry string that contains one of them (without case,
before and after cleaning) as "withheld". A name of one or two characters is
not compared, so it is not withheld. What no tool can catch is
an identifier that is none of those, typed into a product string; on a
machine whose firmware strings were customised, read the block before
posting it. Registry text is printed as plain ASCII (any other byte becomes
`?`) and cut at 96 characters with `...`. `-selftest-sysinfo` drives the
withholding over canned names.

`-nosysinfo` beside `-report` or a dump (`XHCISNAP -nosysinfo -report`) skips all
of it, for a machine on which the reading itself causes a problem: no
`GetVersionEx`, no registry read for the BIOS, the CPU or the names, no
processor or memory query. The header then says "captured on - not read:
the system details were skipped by -nosysinfo", and the rest of the report
is unchanged. Reading is on unless the option is given; on its own, or
beside `-probe`, `-verbosity` or `-disable`, the option is refused with
exit 2.

It is read from the running machine when the dump is taken, never from the
dump: the driver does not record it and the `.BIN` does not carry it. This
tool does not decode a saved `.BIN` (that is the maintainer's offset-table
route), so the block always describes the machine the driver answered on.
`-selftest-sysinfo` prints the names over canned `GetVersionEx` answers, the
NT source choice above over canned source-presence combinations, and then
the block for the machine it runs on.

The `.TXT` also carries, from a 2.0.0.0 driver and at every level, one line per
device the driver holds a slot for: its Slot ID, root port, Route String and
tier, the Output Slot Context's state and Speed field (the raw PSIV, xHCI
Table 6-4), that PSIV decoded on the root port's protocol - Low, Full, High,
SuperSpeed or SuperSpeedPlus - and for a SuperSpeed link the rate it trained
at and its Gen and lane count. It is the witness of the speed a device's slot
actually carries (roadmap-hcd.md 29-E.1). It is read from a third snapshot
region that is not saved as a raw file; a 1.x driver does not serve it, and
the report says so. `-selftest-slots` prints the decode over canned records.

From 2.2.0.0 (roadmap-hcd.md task 35.3) the `.TXT` also carries, at every
level, the driver's own view of each root port and its counter block, read
from a fourth region, the HCD region, which is not saved as a raw file either.
Neither is in the extension, so neither is in the `.BIN`: each root port's
enumeration machine - its state, why it failed, the attempts it used, its slot,
the raw speed ID its last reset read with the class and meaning the driver
gave it, its USB 3 link's warm resets and give-up, and its enumeration notes'
budget and refusals - and `XHCIHC_COUNTERS`, the counters the device matrix
reads (63 words up to 2.2.0.2, 96 from 2.2.0.3). A port is printed only when it has something to say, the counters only
when nonzero, by name. A driver before 2.2.0.0 does not serve the region and
the report says so. `-selftest-hcd` prints the decode over a canned image.

The region is versioned on its own, inside the unchanged schema 5: a header
(version, header bytes, port count, record bytes, the records' offset, counter
count, the counters' offset, the notes' budget, and from 35-T.8 the twelve
tolerance words below), then one sixteen-word record per root port
(`src/xhci.h`, `XHCI_SNAPSHOT_HCD_PORT_*`), then the counters. The tool walks
it by the sizes its header gives, so a later driver that appends to the
header, a record or the counters is still read; a version past the one the
tool knows is refused, since a version bump means a field changed meaning.
35.3's driver sent an eight-word header; 35-T.8's appended twelve words with
the version left at 1, and an older tool reads the region as before.

### Controller tolerance (2.2.0.0)

From 2.2.0.0 (roadmap-hcd.md task 35-T.8, design record 17 section 4.8) the
`.TXT` also names, at every level, what the driver's controller tolerance saw
and did. That state lives in the extension (`XHCI_EXTENSION.Tol`,
`src/xhci_tol.h`), so it is in the `.BIN` and inside the tear detector's
cover; the HCD region's header says where it lies in the extension image
(`XHCI_SNAPSHOT_HCD_TOL_*`: its offset and size, the counters' word count, the
recovery window's and the clock's offsets, and each kind of location's
offset, count and record size), so the tool names it from the `.BIN`'s own
bytes with no offset table, and checks each figure against the image before
believing it. The last header word, `XHCI_SNAPSHOT_HCD_TERMINAL`, is a value
rather than a place: the controller's terminal reason, cut under the
controller lock with the window.

What the report carries:

- **The values in effect**: `XhciTolerance` (1 on, 0 off - `2.1.1.0`'s
  handling, but for what applies at every value: an incompatible device's
  re-enumeration, terminal reason 5's containment, and on a latched-failed
  controller the release of its held transfers on HCH's proof, no resume
  clearing a spent run of failures (a change from `2.1.1.0` at 0: only a
  stop and a start bring it back), and no Save or Restore State; the
  counters of what was seen still count - the codes, the queues' sums and
  halts, root port PED faults and over-currents - and so do those of what
  applies at every value (an incompatible device's charges and `Holds`,
  `Contained`); the counters of the behaviours switched off stay at 0),
  `XhciIntervalCap` (value 1: AMD controllers and, from task 38.10, the
  ATI Navi10 USB-C controller `1002:7316`, Linux's
  `XHCI_LIMIT_ENDPOINT_INTERVAL_9`; printed "AMD controllers and
  1002:7316") with whether
  the cap applies on this controller and how many endpoints it lowered, and
  `XhciAvgTrbEsit`.
- **The controller**: running; failed with an in-place recovery still owed;
  latched failed after three recoveries failed in a row; latched failed
  because the recovery window refused a fourth inside it (at least ten
  minutes, about 22 at the timer's nominal rate); contained as unreadable
  (USBSTS read all ones); or, terminal reason 5,
  "CONTAINED: halt and reset did not complete and Bus Master Enable would
  not clear; DMA not proven stopped, common buffer pinned" - an in-place
  recovery or a reinitializing resume whose halt and controller reset never
  completed, on a controller that still reads, and whose Bus Master Enable
  clear was not proven either (at every `XhciTolerance` value, 0 included;
  the containment below then reads pinned). The two latched states and
  reason 5 end only at a stop and start, and the report says so beside them.
- **The recovery window**: how many of the three recoveries it allows were
  begun inside it, how long ago each began in tolerance-clock ticks (100 ms
  nominal, credited at no more than 45 ms each), and how many it refused.
  The driver drops an old stamp only when it next admits a recovery, so the
  tool judges each stamp against the dump's own clock: one past the window
  is reported as kept but not counted.
- **The containment** and its branch: released (Bus Master Enable read back
  clear, the devices dropped) or pinned (no proof DMA stopped, the common
  buffer and transfers kept), and the all-ones episodes begun.
- **The counters, nonzero only, by name** (`XHCI_TOL_STATS`): the backstop's
  drains; the queues' `Errors`, `BadCodes`, `UnmatchedEvents` and
  `ForeignEvents` summed, and halting completions; the soft retry's diverts,
  Reset Endpoints, recoveries, exhaustions, replays and refused resets; the
  device cycles by reason (a code nothing claims, a halt with no TD, a root
  port found disabled), those before a PDO, those dropped as stale and those
  a budget refused; the context reads for a halt with no TD and the stale
  ones; root port PED faults, over-currents, repowers, holds; HCH recoveries
  and recoveries the window refused. A counter a later driver appends before
  the histogram prints by number.
- **The completion codes**, every Transfer Event's code by count with its
  xHCI name. The note ring's `xfer.error` records (slot `<< 16` \| DCI `<< 8`
  \| code) carry the first few of each error code, a budget per code so a
  storm of one cannot crowd out the first of another.
- **Each location charged, held or re-armed**: a root port by number, an
  external hub's port as hub object and port (the hub numbered from 0 in the
  order the driver brought hubs up), with its charges, its re-enumerations
  and repowers against their budget of three, its re-arms, how often it was
  held, and the hold standing now - powered (ended by a stable disconnect or
  a controller start) or unpowered (ended only by a controller start).

A driver before 35-T.8 sends the eight-word header and the report says it
does not serve the tolerance state; an extension image that came back short
or another size than the driver declared is not decoded, and only the
terminal reason is printed. `-selftest-tol` prints the decode over a canned
image.

From 2.3.0.0 the HCD region's header carries the interrupt routing quirk's
record (`XhciIntelIrqRoute`, roadmap task 36.3) after the terminal reason,
and the report names it in a section of its own, "interrupt routing":

- **The value** as found, or "not set", and how the driver took it: 0 reports
  and writes nothing, 1 writes on the verified E7250 only, 2 on any reviewed
  xHCI and LPC pair.
- **The counts** since the driver loaded: runs, D20IR writes, and writes
  that failed (the write refused, its read-back failed, or the read-back
  disagreed).
- **The last run**: at the start or at a return to D0, its decision by name
  and meaning (the run stops at the first check that fails), the delivery
  (a line, a message interrupt, polled, none), and the raw interrupt
  resource the OS gave (descriptors, level, vector, flags, share).
- **What it read**, each "not read" past the check that ended the run: the
  xHCI's id, subsystem and pin; 00:14.0's id; the LPC bridge's id, RCBA and
  `PIRQA-H_ROUT` routes; D20IP's pin; D20IR before, the PIRQ that delivers
  the IRQ the OS assigned, and D20IR read back after a write.

A header from before 2.3.0.0 is reported as not serving the record, and a
record count the header cannot hold is not decoded. `-selftest-pirq` prints
the decode over canned headers.

From the same release the header also carries the transfer-size readings
(roadmap task 36.21) after the routing record, in a section of their own,
"transfer size", since the controller's start:

- **The value** `XhciMaxTransferKB` as the start took it (task 36.22), the
  command size in effect and how often a class driver asked for it, and the
  map grant (registers, and the pages one chunk may span).
- **Bulk data URBs** by length, and the chunks each became.
- **The gap between two chunks of one URB**, in two parts: software's time
  from a chunk's retirement to the next one's publication, and the time the
  ring stood empty in software's view with the URB unfinished; and the
  processor time a publication, a completion pass and a next chunk's
  mapping take. These are time-stamp counter readings converted with the
  rate the start's and the dump's (counter, system time) pairs give, so a
  dump under a second after the start prints counts only; an interval
  whose two readings came from two processors is not measured but counted
  ("across two processors"), and on a machine whose processors' counters
  disagree the rate and ages are approximate. Hardware starvation itself
  is not measured.
- **Each class driver record** (`xhciuas.sys` sends one at most once a
  second, when anything in it changed): whether its device is present or
  departed, the command size it advertises and where it came from,
  streamed or not, its queue depth and the most commands in flight, its
  commands by length and by the number in flight as each started, its
  counters, how often its size question fell back to 64 KiB, and how old
  the record is. A record slot nothing has written reads "none has come",
  never zeros.

A header from before 36.21 is reported as not measuring it. `-selftest-xstat`
prints the decode over canned headers.

From 2.2.0.3 (roadmap tasks 36.6 to 36.9, design record 15) the controller
may take its events by polling instead of the line interrupt, and the report
says which, in a section of its own, "event delivery", read from the counter
block's words 63 to 95 (named in the counter list too):

- **The three values** `XhciInterruptMode`, `XhciPollIdleMs` and
  `XhciPollActiveMs`, as `XhciImodInterval250ns` is reported: found or not,
  the value as read, the value in effect, and in words the rule that made
  them differ (a reserved mode run as 0, a period of 0 replaced by its
  default, an idle period above 5000 ms held to it, an active period held to
  the idle one).
- **The delivery in effect and its reason**: the line interrupt, polling,
  MSI or MSI-X; and why - automatic with the line there, automatic with
  Interrupt Pin 0 or no interrupt resource, forced line, forced polling, or
  the line gone dead at run time and moved to polling; or why a start
  failed: `XhciInterruptMode` 1 given a message, or 3 given no message, a
  message not connected, or a Windows with no message interrupts (design
  record 15 revision 7). So a machine that
  polls automatically and one forced to poll read apart. Under polling the
  report says that `XhciImodInterval250ns` has no effect.
- **The Interrupt Pin** and whether it could be read.
- **The poller**: its role (watching the line, or serving the event ring),
  the period it last armed and the bus state that chose it, its runs and the
  runs that found an event, the opportunistic peeks and their hits,
  promotions, parks, kicks, arms displaced, and the shortest and longest
  interval observed between two runs.
- **The watcher and the dead-line move**: looks, asserted looks, stale pairs;
  whether the line was moved and whether the interrupter's mask was proven
  down; the line's retirement and its attempts; and a start that found PCI
  Interrupt Disable set and cleared it.
- **The extension's six delivery words** (`EventDelivery` to
  `LineRetireState`), which the HCD header's words 202 and 203 place in the
  extension image as the tolerance words place `Tol`; a mismatch between
  the extension's delivery and the counter block's is said, since the two
  regions are read at different times.

A counter block of 63 words is a driver from before polling, reported as
taking the line interrupt; a header shorter than 204 words places no
extension words. `-selftest-intr` prints the decode over canned images.

**Message interrupts** (2.2.0.3 on, task 36.15; design records 15 section
4.3 and 18 section 9). Counter words 96 to 113 are named
(`MsiRoutines` to `MsiFilterStripped`) and decoded in their own section:
where the `IoConnectInterruptEx` pair came from (the 64-bit
`MmGetSystemRoutineAddress`, the 32-bit helper - `xhcihlpr.sys` from
`2.4.0.0`, `xhcimsi.sys` before - or not
found), the messages connected and granted, the capability (MSI or MSI-X)
and the connection's status; the 32-bit helper's state (ABSENT, REFUSED
with the check that failed, BOUND with its version); and the last
`IRP_MN_FILTER_RESOURCE_REQUIREMENTS`'s record: the list's class, the
outcome ("not asked", "REPLACED" when the message choices were stripped so
the line is granted, "REPLACED ... the message left" when `XhciInterruptMode`
3 stripped the line choices, or why the list was kept), the `XhciInterruptMode`
it read, and the list's members. The filter runs on Vista and 7 only, so on
Windows 98, ME, 2000 and XP its sequence is 0 and the section says "not
applicable" (the raw `MsiFilterMode` word there reads FFFFFFFF, no value).
A block of 96 words is a driver from before MSI. `-selftest-msi` prints
the decode over canned images.

After the MSI words come selective suspend's (114 to 141, `SuspSwitchRead`
to `SuspNotesSuppressed`), its stage B's eighteen (142 to 159,
`SsSuspArms` to `SsSuspCapSettled`, reserved and unprinted before task
36.38 filled them), the composite parent's two (160 and 161,
`CompositeParents` and `CompositeRepresented`) and issue 18's five (162 to
166, `Usb2HubSpeedSwitchRead` to `Usb2HubSpeedFallbackSlots`; roadmap-hcd
36.49). Those five also get a section of their own, **"USB 2.0 speed IDs
behind a hub"**: whether `XhciUsb2HubSpeedFallback` was read and the
default IDs are on, the placements refused for want of an ID and those
given the default ID, and the Slot IDs holding such a device (the slots
list's speed column decodes their speed by the same rule). A block shorter
than 167 words is a driver from before it, and the section is not printed.
Task 36.54's six follow (167 to 172, `StreamRetryResets`,
`StreamRingRestarts`, `QemuStreamQuirk`, `QemuStreamResyncs`,
`QemuStreamResyncFailed`, `QemuStreamRestops`; design record 17 section
4.12), with a section of their own, **"stream endpoints"**: the soft
retries on a UAS stream, the stream rings a port reset restarted at their
base, and whether the QEMU stream-context quirk is armed - on QEMU's xHCI,
PCI `1B36:000D`, alone - with the Set TR Dequeue commands it completed,
the resyncs that could not finish and the extra stops QEMU's own restart
of an endpoint cost. A block shorter than 173 words is a driver from
before it, and the section is not printed.
Task 38.11's three follow (173 to 175, `HciVersions`, `HciAccept096Value`
and `HciVersionPolicy`; design record 17 section 4.14.7), each FFFFFFFF
until the start writes it, with a section of their own, **"controller
version"**: `XhciAccept096` as read and the floor it set (0.96, or 1.00 at
`0`), and the controller's HCIVERSION with what the driver did with it -
`xHCI 0.96 (HCIVERSION 0096h): accepted below 1.0; TD Size in 1 KiB
units`, refused at `XhciAccept096 0` or below 0.96 (Code 10), or `xHCI
1.00: XhciAccept096 not applicable`. Bit 21 of the policy word, not the
versions word, says whether the version was read, so a register that
answered all ones is told from a start refused before it; the `qemu`
flavour's `XhciQemuHciVersion` is named beside the register's own value
whenever it applied. A block shorter than 176 words is a driver from
before it, and the section is not printed. Whether `XHCISNAP` can open a
controller whose start failed is not read on any target; such a start's
counter dump goes to DebugView with `XhciLogDebugView` `1`.
`-selftest-version` prints the section over canned words.
Task 38.3's twenty-three follow (176 to 198, `TmrSwitchRead` to
`HlprTimerVerdict`; design record 15 section 7.2.9, record 18 section
15.3), with a section of their own, **"the finer system timer while
polling"**: `XhciPollFineTimer` as read and in effect (a value present
prints its number and "in effect off" or "ON", so the install's `0` reads
"0; in effect off" after the name; "not set; in effect off (the
default)" is printed only when the value is absent), where
`ExSetTimerResolution` came from (the helper, `MmGetSystemRoutineAddress`,
not found, or none on Windows 98 SE and ME), the timer thread's state, the
value asked and whether a request is held now, the holds and releases
(those a boundary forced, the holds a boundary raced), what Windows
answered, the last release's cause, the time held in this start, the poll
intervals of short requests by the request's phase (count, mean, late),
and the 32-bit helper's two service verdicts. A block shorter than 199
words is a driver from before it, and the section is not printed;
`-selftest-timer` prints the decode over a canned block.
Task 38.10's thirty-four follow (199 to 232; design record 20 section
6.4): one packed word per Linux quirk value, `QuirkMsiAvoid` to
`QuirkAmdSuspendBit` (199 to 212, in `src\xhci_quirk.h`'s order), then
`QuirkPciRevision` (213) and `QuirkIdentity` (214, the PCI vendor and
device the quirk lists were matched against), then the counts,
`QuirkMsiRefused` to `QuirkShutFlushUnacked` (215 to 232). They get a
section of their own, **"Linux quirks"**: the PCI id the lists were
matched against ("no PCI id" when none was read) and the revision
("unread" when none was; it is logged, never matched), then for each of
the fourteen registry values its name and its Linux flag, and the four
things record 20 keeps apart:

- **the value read and what became of it**: the number read (its low 16
  bits; "none" when nothing was read) and its normalization - "as read",
  "absent", "another type" or "an unknown number" (each taken as the
  default), or "a 2 read as 1" (a vendor-register quirk offers no 2) -
  with the mode in effect (0 off, 1 the listed controllers, 2 every
  controller);
- **whether this controller's id is on the quirk's list** ("id listed" or
  "id not listed");
- **the policy in effect**: on, off, or overridden - `XhciMsiAvoid` alone,
  when `XhciInterruptMode` 3 asks for a message whatever the quirk says;
- **the last outcome**: none yet, done, skipped or failed, with its reason
  when one was recorded (the precondition that was not met, the access
  that failed, or for `XhciMsiAvoid` that no PCI id was read or the one
  read before was kept).

Then the counts in plain words: the messages `XhciMsiAvoid` refused or
stripped, the suspend halts past 32 ms, the pauses before a suspend's
halt, the suspends that did not save, the stuck USB 3 links warm-reset,
the padded stream blocks, the No Op TRBs placed, the isochronous
Successes dropped, the interrupt endpoints `XhciTiIntervalCap` capped,
the ASMedia flow-control writes made and given up, the AMD bit 17 clears
made and skipped, the shutdowns that routed ports to EHCI, the controller
resets at shutdown, and the shutdown's log flushes drained, acknowledged
with the sink off, and not acknowledged. The fifteenth id, `1002:7316`
(ATI Navi10), has no value of its own: it joined `XhciIntervalCap`'s
value 1, and the section says so. A block shorter than 233 words is a
driver from before 38.10, and the section is not printed.
`-selftest-quirks` prints the section over canned packed words (every
normalization, listed and not, on, off and overridden, each outcome, codes
this build does not know printed as `?`), with the PCI id and revision
read and unread, and a 232-word block that prints nothing.
Task 38.10's eight follow (233 to 240, `PoolRingsInUse`, `RingsRetained`,
`RingsPinned`, `RingsReleased`, `SelectsReattached`,
`SelectStallSuccesses`, `SelectRollbacks`, `SelectRollbacksReopened`;
design record 20 section 5), with a section of their own, **"endpoint
rings"**: the pool rings held at the last poll (of 32), the rings held
for an endpoint the controller may still have enabled - stale, or pinned
after a command whose completion was never seen - and those a later proof
released, and how interface switches ended: the old pipes put back after a
refused command, the one-setting STALLs carried out by clearing the
endpoints' halts, the rollbacks after a STALL and those that reopened the
old setting. A block shorter than 241 words is a driver from before it,
and the section is not printed.
Task 38.12's five follow (241 to 245, `CompositeSelects`,
`CompositeSelectRefused`, `CompositeRewrites`, `CompositeMismatches`,
`CompositeIntervals`; design record 13 section 10.14), with a section of
their own, **"composite selects"**: the composite devnode's own
`SELECT_CONFIGURATION`s the parent recorded, and those it refused or whose
own sequence failed; the endpoints a filter on the composite devnode gave
another `bInterval`, and those it changed in more than `bInterval` (not
taken); and the function endpoints built at a recorded rate. A block
shorter than 246 words is a driver from before it, and the section is not
printed.
Task 44.35's six follow (246 to 251, `XportUas`, `XportUasOnly`,
`XportUasOnlyForced`, `XportBotForced`, `XportBotNoStreams`,
`XportNoStreams`; roadmap-usb4 44.35, design record 13 section 10.7,
"The decision kept"): the storage transport decisions the bus made, one
per reason, in the counter list by name. Until `3.3.0.0` they were
`hc->XportDecisions`, outside the block and never zeroed; in the block
they are zeroed at each start with it. A block shorter than 252 words is
a driver from before it.
`-selftest-counters`
prints every word of a canned block whose word i holds i + 1, and
`selftest.cmd` checks it word by word against `src\xhci_counters.h`
(`check-counter-names.ps1`), on the built EXE: an inline lookup over two
of these tables was once compiled by MSVC 6.0 to name the selective
suspend words 18 places off.

Two more sections come from the HCD header of a `2.3.0.0` driver (amended
in roadmap-hcd 36.20). **"stage 2, two TDs of one URB on the ring"**
(task 36.24, design record 13 section 11.6; `-selftest-xpipe`):
`XhciWideMap` as read ("not set (the default)" when absent), whether
promotion is on and what the register-free check found (or why it did not
run: the value off, Windows 98 or ME, a grant that is not register-free),
and the wide mapper's and the pipelined record's counts; with task 36.25,
stage 3's big stream blocks and their TD sizes. A driver from before it
says "not in this driver". **"selective suspend, stage B"** (task 36.38,
design record 14 section 8.10): `DNCTRL` as read when the window was cut
("not read" unless `XhciSelectiveSuspend` is 1), the enables written and
read back, and the records of the first eight devices in slot order, of
any speed (a USB 2.0 device takes a record too, so a later SuperSpeed
device can be left out; the `Ss` column marks SuperSpeed) - slot, link state,
the window, armed functions and the last step's outcome.

From `3.3.0.0` the HCD header ends with **"storage transport"** (roadmap-usb4
task 44.35; design record 13 section 10.7, "The decision kept", and design
record 25 section 19; `-selftest-xport`): at words 306 to 308 the record
count (32), a record's words (10) and the first record's word (309), then
one record per device holding a mass-storage decision, in Slot ID order -
Slot ID, root port, route string, interface (`0xFF` for a device PDO),
transport, why, alternate, the inputs (`XHCI_XPORT_F_*`), the class triple
and the count of storage interfaces decided - all 0 past the last. The
header is 629 words, the region's version still 1: a reader walks it by
the header's size. The section prints each record with the words
`XHCITR`'s details pane uses ("UAS (the device offers Bulk-Only and UAS;
UAS chosen)"); a header shorter than 629 words is a driver from before
it, and the section says so. The same records reach `XHCITR`'s window
through the query request's version 2 (design record 13 section 8.12),
at every level, which also carries the driver's version (design record
25 section 18).

The PORTSC decode is printed on screen whatever the level, because that is what
the bench reads on the spot. The headline test is per port: a port reporting a
device connected with `PP` clear is Finding Q read off the register, whatever
the other ports say.

The exit code says whether the `.TXT` is the report. There are four, not two.

`1` is **anything the run attempted and could not finish**: `\\.\HCD0` would
not open, `-probe` found that the request reached no miniport at all (a
miniport that answered, or declined the request, is a route and exits 0;
`-probe` never publishes a dump), the driver refused the window or answered
with a schema this build does not know, a publish rename failed, or a
`-verbosity` / `-disable` registry write failed.

`2` is a usage error, and it covers more than the flag combinations: no
arguments at all (which prints the short usage), a `-c` or `-verbosity`
argument that is not a whole number, a `-verbosity` above the ladder's top, an
`-o` basename too long for the three names built from it, a
`-selftest-report` basename too long, an unrecognised flag, `-force` with
nothing to modify, and the refused combinations - `-verbosity` with `-disable`,
which are opposites, or either of those with `-probe` or `-dump`, which read
the driver rather than setting it, or `-probe` with `-dump`, since the probe
returns before a dump would be taken. **`-dump` is implied by `-c` and by
`-o`**, so those combinations are refused under the implied spelling too.

`0` means the file was
created and every write and the close reached the volume. `3` means it was
not created (the summary line reads `NOT CREATED`; the report went to the
screen), or it was created but not completed (a full or removed destination;
`INCOMPLETE`), or the extension window came back a different size from the
one the driver declared (`MISMATCH`, `DO NOT DECODE`, printed whatever
happened to the `.TXT`): in every case the `.BIN` and `.PSC` are still the
raw evidence and are still named, but the `.TXT` must not be sent as the
report. Until the
2026-09-05 audit (roadmap Phase 20, F5, F17) `fopen` succeeding was the whole of
"written", and a truncated report exited 0 with a "send this" underneath it.
`xhcisnap -selftest-report BASE` drives the report path with no controller,
and the `XHCISNAP_FAULT` environment variable (`write` or `close`) makes the
named step fail; `xhcisnap\selftest.cmd` runs four cases - no fault, write
fault, close fault, and a read-only destination.

The help is `-help`, `-?` or `/?`; all three print the long text, and a bare
invocation prints the short usage and exits 2 rather than taking a dump.

### Reading the note ring: the endpoint, slot and virtual-hub records

From level 2 the `.TXT` carries the note ring, one record a line as
`label=XXXXXXXX`: a fixed label and one 32-bit value in hexadecimal, packed
as the table says. The records are written in every build flavour
(`XhciLogNote` / `XhciLogNoteLocked` call sites, `src/xhci_log.h`), so a
`release` machine's dump carries them too. No other document listed them
until `1.2.0.0`'s cut; this table covers the records an endpoint, a device
address and the virtual High-Speed hub (design record 12, the
`XhciVirtualHSHub` switch) leave, read from their call sites. Every other
label is described where it is written.

Speeds are this driver's classes (`src/xhci.h`): 0 unknown, 1 Low, 2 Full,
3 High. A virtual hub is named by its root port, numbered from 1.

| Label | Written when | Value |
|---|---|---|
| `ep.open` | usbport opens a device endpoint other than the default pipe (`src/xhci_slot.c`, `xhciSlotOpenNonDefault`) | Slot ID `<< 16` \| DCI `<< 8` \| usbport's transfer type (0 isochronous, 1 control, 2 bulk, 3 interrupt) |
| `ep.open.rate` | the same open, next record | `Period` `<< 16` \| maximum packet size, as usbport asked |
| `ep.open.ival` | the same open, next record (roadmap 24.1) | speed `<< 16` \| floored `<< 8` \| Interval. Speed is the one usbport bucketed `Period` with, so a root-port device reads 3 with the switch off; floored is 1 when the 1 ms floor for Full and Low Speed moved the value; Interval is what the Endpoint Context was given, a period of 2^Interval x 125 us (6 is 8 ms, 3 is 1 ms) |
| `slot.addressed` | a device's Address Device completed | Slot ID `<< 16` \| USB address `<< 8` \| the speed this driver decoded |
| `slot.route` | the same, next record | tier `<< 24` \| Route String; 0 for a device on a root port |
| `slot.parenthub` | the same, next record | parent hub's USB address `<< 8` \| its downstream port; 0 for a device on a root port |
| `vhub.switch` | every start, before the controller is initialised (`src/xhci_dispatch.c`) | the registry read's status `<< 16` \| `XhciVirtualHSHub` as read (low 16 bits; 0 when the read failed) |
| `vhub.applied` | the same | why refused `<< 8` \| mode applied (0 off, 1 on demand, 2 always). Why refused: 0 nothing refused, 1 the switch held a value other than 0, 1 or 2, 2 `XhciVirtualHSHubVid` failed, 3 `XhciVirtualHSHubPid` failed |
| `vhub.ids` | the same | vendor id `<< 16` \| product id as applied; 0 when the ids were not read (the switch at 0) |
| `vhub.create` | at 1, a root-port reset decoded a Full or Low Speed device and stood a hub up (`src/xhci_rh.c`). Not written for the hubs value 2 stands up at start | root port `<< 8` \| decoded speed |
| `vhub.drop` | a hub retired: its device left, or usbport's root-port disable retired it | root port |
| `vhub.gone` | the retirement was the port's own disconnect being read; a `vhub.drop` with no `vhub.gone` beside it is a hub retired some other way | root port |
| `vhub.flip` | at 1, the decision flipped across a reset (another speed class on the same port), and a connect change was forced on the root port | root port |
| `vhub.open` | usbport opens one of a hub's two pipes (`src/xhci_slot.c`) | root port `<< 16` \| USB address `<< 8` \| endpoint address (0 the default pipe, `0x81` the status-change pipe) |
| `vhub.address` | the hub's `SET_ADDRESS` was answered | root port `<< 8` \| USB address; a second record for one port is the hub enumerated again |
| `vhub.prc.late` | a reset completion nothing armed, on a port carrying a hub or whose hub retired after a port-1 reset's deadline: the late end of a reset already given up on | root port |
| `vhub.redisable` | a reset ended with the port enabled while a disable was still owed, and the disable was written again | root port |
| `vhub.p1.pednoccs` | a port-1 status answer read the physical port enabled but not connected | root port `<< 24` \| the operation armed on the port `<< 16` \| PORTSC bits 15:0 |
| `vhub.p1.prc` | a port-1 status answer carried `C_PORT_RESET` | root port `<< 24` \| change bits `<< 16` \| status bits 15:0 |
| `vhub.lost` | at 1, a controller reinitialisation found a hub whose device left meanwhile, and latched the root port's connect change | root port |

So `ep.open.ival=00020006` is a Full-Speed endpoint at Interval 6, 8 ms -
what a stock mouse behind a virtual hub reads - and `00030005` the same
mouse on a root port with the switch off, reported High Speed and polled
every 4 ms.

### The root port enumeration records (2.2.0.0)

`xhci98.sys` from 2.2.0.0 (roadmap-hcd.md task 35.3) notes how each root
port's enumeration went, so a dump names its own cause. 35.0's E460 dumps
showed a SuperSpeed link trained and no slot ever made, and nothing in the
log said why; these records would have said "speed ID 4, no mapping, failed
on the speed, no retry follows". The `.TXT` prints them in the ring as
written and, after it, once more decoded a line each ("root port
enumeration notes, decoded"). `-selftest-notes` prints that decode over a
canned ring. The layouts are `src/xhci_enum.h`'s; the port is the xHCI port
number, as in the PORTSC table.

| Label | Written when | Value |
|---|---|---|
| `enum.port.look` | an inspection of the port fed its machine a connect or disconnect, asked for a warm reset or gave the link up, or found the machine Failed (once, until something is fed again) | port `<< 24` \| machine state before it `<< 20` \| link action `<< 16` \| feed `<< 14` (bit 14 disconnect, bit 15 connect) \| PORTSC change bits 23:17 `<< 7` \| PLS `<< 3` \| PR `<< 2` \| PED `<< 1` \| CCS |
| `enum.port.reset` | the machine reset the port | port `<< 24` \| ok `<< 23` \| attempt `<< 16` (0 the first) \| PORTSC bits 15:0 as the reset left it (speed ID at 13:10) |
| `enum.port.speed` | the same, the reset ok | port `<< 24` \| raw speed ID `<< 16` \| class `<< 8` (0 unknown, 1 Low, 2 Full, 3 High, 4 SuperSpeed) \| where the meaning came from (0 none, 1 listed in the protocol's PSI table, 2 the default IDs of a protocol with no table, 3 the default ID for an ID 4 to 7 a USB 3 table does not list - task 35.1) |
| `enum.port.rate` | the same | port `<< 24` \| SuperSpeedPlus `<< 23` \| the rate in units of 100 kbit/s (0 when the protocol names no rate for the ID, 7FFFFFh for one past the field), printed in Mbit/s |
| `enum.port.slot` | Enable Slot completed, or never did | port `<< 24` \| completion code `<< 16` (0 never completed) \| attempt `<< 8` \| Slot ID the controller gave |
| `enum.port.fail` | an attempt failed | port `<< 24` \| cause `<< 16` \| attempt `<< 8` \| 1 when no retry follows |
| `enum.port.end` | a run of the machine that reset the port ended | port `<< 24` \| state `<< 16` \| cause `<< 8` \| retries used |
| `enum.port.quiet` | the port's budget was spent | port `<< 24` \| the budget |

Machine states: 0 Empty, 1 Debounce, 2 Reset, 3 Enable Slot, 4 Address, 5
the 8-byte descriptor, 6 Evaluate, 7 the device descriptor, 8 the
configuration's head, 9 the whole configuration, 10 Present, 11 Bound, 12
Gone, 13 Failed, 14 and 15 the BOS. Causes: 0 none, 1 the reset, 2 no slot,
3 Address Device, 4 the device descriptor, 5 the configuration, 6 the PDO,
7 the speed (no EP0 packet size for the class it decoded to).

They are bounded. A look, or one run of the machine (both its attempts), is
one burst, and a port has eight between enumerations: one that enumerates
gets them all back, so a working port replugged any number of times keeps its
records, and one that fails or flaps writes `enum.port.quiet` and then
nothing until it enumerates. The bursts refused are counted per port, in the
HCD region above. Ports behind a hub are not noted.

## Three things to know before trusting a dump

It is windowed, so it can tear. usbport refuses `ParameterLength > 0x10000`
before the miniport is ever reached, and the extension is larger than that
(over 90,000 bytes; the `SIZEOF` line of a regenerated `offsets.txt` is the
exact figure), so a dump is several IOCTLs and the driver runs between them. Every window carries a tear detector, the sum of
`CheckCallbacks`, `DpcCount` (every event drain, interrupt or polled) and both halves of the log's producer accounting,
read inside the driver's lock, and the tool reports whether they all agreed. A
torn dump is not wrong, but any counter in it may be a mixture, and the last
line of output says which you have. A torn one prints the pair, `first ->
last`, and the step between them; the detector is monotonic, so the pair is
also a magnitude.

`ExtensionBytes` is the layout key. Decode a dump only against an `offsets.txt`
regenerated from the same tree. The tool prints the driver's own
`ExtensionBytes` for this reason: a dump decoded against the wrong table is a
wrong reading, not a failed one, and wrong readings are how this investigation
has lost time before.

One known wart, left alone. Every line of the note ring in the `.TXT` ends
`0D 0D 0A`: the driver stores `CRLF` in the ring, the tool emits it character
by character through a text-mode `FILE*`, and the runtime translates the `\n`
again. Harmless in a viewer and in a GitHub paste, visible only in a hex dump.

## The route, and where it came from

usbport's vendor escape was read out of both shipping `USBPORT.SYS` builds:
NUSB 3.3's 5.00.2195.5652 (the Windows 98 binary) and Windows 2000 SP4's
5.00.2195.6681. The full derivation, with every address, is under "Debug /
single-packet" in `docs/usb-xhci-info/usbport-miniport-abi.md`, and
`docs/contributing/legal-provenance.md` section 4 has a row for it.

| | |
|---|---|
| IOCTL | `0x00220438` - `IOCTL_USB_USER_REQUEST`, `METHOD_BUFFERED`, `FILE_ANY_ACCESS` |
| Request | `UsbUserRequest = 3` (`USBUSER_PASS_THRU`) |
| Device | the controller's door: `\\.\HCD<n>` on 98 SE and ME, `\\.\XHCI98HC<k>` on NT from 3.3.0.0 (`HCD<n>` on every target before), the `\DosDevices` link the HCD FDO's start path creates |
| Buffer | one buffer, in length == out length, at least `0x28`; `RequestBufferLength` must equal that length exactly |
| Gate | none |

## What has been executed, and what has not

It works on Windows 98. Observed in the 2a QEMU guest, on NUSB 3.3's own
`USBPORT.SYS` 5.00.2195.5652, the binary the route was derived from.
`\\.\HCD0` opens, the IOCTL round trip completes, `-probe`'s four controls
return 0 / 2 / 4 / 7, the driver's own debug trace carries `cb PassThru` lines
(so the callback was reached rather than inferred), and an 87,592-byte
extension image came back in two windows and decoded against an `offsets.txt`
regenerated from the same tree, with the header's tear detector agreeing across both windows and equal to the
`CheckCallbacks` decoded out of the dump body. The tear detector has since
become a sum of four counters rather than that single one, so the equality in
that reading is a property of the version it was taken on; what the tool
checks now, and reports, is only that every window's detector agreed.

Only three of those four numbers are fixed. The first control reports whether
this driver answered, so it is state-dependent by design: `0` when the channel
is on and the driver answers (which is what this reading was taken with), and
`6` (`DRIVER DECLINED`; `MINIPORT DECLINED` before 2.0.0.0) when it is off, which is where every machine sits by
default. A `6` there is the ordinary shipping reading and not a fault. The
other three are properties of the route rather than of the driver's consent,
and do not move.

The same contract was separately exercised on the Windows 11 development host,
where all four controls also matched.

Windows 2000 has run it too. In the 2b guest, against SP4's own `usbport.sys`
6681 rather than the NUSB 5652 build the route was read out of, `\\.\HCD0`
opens, the round trip completes, and `-probe`'s four controls return
6 / 2 / 4 / 7: the same three fixed controls as the Windows 98 reading above,
with the first at `6` because this guest's channel was off (the shipping
default, not a difference between the targets). So the route is an observation
on both targets, and the two usbport builds answer this escape identically at
run time as well as in their comparison chains.

On 32-bit Windows Vista only the registry half has run. An install through
Device Manager records `InfSection = Xhci.Dev6` in the driver key, which the
tool did not recognise until that name was added to the ones it matches
(roadmap task 22.5, 2026-09-17): the earlier build skipped the key as matched
by a value name alone, and the current one matches it and sets
`-verbosity`. The `\\.\HCD0` route has not been run on Vista or Windows 7.

## The one way this route can be missing, and how to tell

usbport builds its symbolic link at a fixed index from its own controller
number, with no retry and no fallback. On a machine where something else
already owns that name (Windows 98's own USB stack does, for a UHCI or OHCI
controller it drives), `IoCreateSymbolicLink` fails and no usbport link appears
at all. The failure is silent: the driver starts, binds and runs perfectly, and
only the reading channel is missing.

Measured: with the 2a guest's UHCI present, `\\.\HCD0` opened but every IOCTL
failed and the trace showed no `cb PassThru` at all, while `HCD1`-`HCD3` did
not exist. Removing the UHCI made it work first time.

So run `-probe` before trusting anything, and read it this way:

| `-probe` says | It means |
|---|---|
| `the driver ANSWERED` (`the miniport ANSWERED` before 2.0.0.0) | the channel is live; take the dump |
| `the request reached the controller's driver and it DECLINED` (`... a miniport ...` before 2.0.0.0) | the route is fine. Two situations and the driver cannot tell you which; see below |
| `cannot open` | no usbport HCD link on this machine at all |
| opens, but `DeviceIoControl failed` | something else owns that name; try `-c 1`, `-c 2` |

That second row is two situations wearing one answer, and the ordinary one on
every machine is "switched off". A disabled channel answers the way a binary
built without one would, on purpose: `MP_STATUS_NOT_SUPPORTED` is the only
honest nonzero value at that slot, because usbport's own root-hub port-status
probe retries only on that code, and any other would suppress its fallback
silently. No probe can separate the two cases, and saying which it is would be
a guess. The tool names both and offers the fix for the one that is fixable
from here: `-verbosity 2`, then restart. The bootstrap is not circular; the
value is set from ring 3 without needing the IOCTL at all.

## Building

```bat
xhcisnap\build.cmd
xhcisnap\selftest.cmd
```

The second runs the report path four times with no controller present (no
fault, a failing write, a failing close, a `.TXT` that cannot be created) and
checks the exit codes and summary lines; see "Three files" above.

It also drives every `-selftest-*` decode over canned data, and from 2.3.0.0
`-selftest-sysinfo`, which withholds canned names, and the report path with
and without `-nosysinfo`.

MSVC 6.0 in place from `tools\MSVC600`; nothing is installed machine-wide and
`MSVC6` overrides the location. `/Za` is not used here even though the driver
and the host tests both use it: that compiler's own SDK headers are full of
anonymous unions, which `/Za` rejects, so `windows.h` does not compile under it.
`/WX` still makes a warning a build failure, and the source keeps the same C89
rules by hand, since it has to run on Windows 98 SE.

The wire format is written out again in `xhcisnap.c` rather than included from
`src/xhci.h`, which is a kernel header. That duplication is checked at run time:
the tool refuses any driver whose reply signature, schema version or header size
is not the one this build knows, and says to rebuild from the same tree.
