# xhcisnap - reading this driver's own log off a running machine

`XHCISNAP.EXE` reads `xhci98.sys`'s miniport extension and its raw PORTSC array
from user mode, through usbport's `PassThru` vendor escape, and writes a report
a user can send back.

It ships. The kernel side is in every build flavour of the driver and this tool
is published in `releases/<version>/xhcisnap/`. It began as a bench companion
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

- The channel switched on. `XhciLogVerbosity` defaults to `0` on every machine,
  and `0` is off outright: the driver answers this tool the way a build without
  the channel would. `XHCISNAP -verbosity 2` sets it on every xhci98 controller
  the machine has. Then restart.
- Nothing else. No service, no `regedit`, and no `IOCTL_USB_DIAGNOSTIC_MODE_ON`;
  the route itself is ungated.

## Using it

```text
XHCISNAP -verbosity 2    set the level exactly (0-4), then RESTART
XHCISNAP -dump           dump controller 0 to XHCISNAP.BIN/.PSC/.TXT
XHCISNAP -dump -c 1 -o WEDGED   controller 1 to WEDGED.BIN/.PSC/.TXT
XHCISNAP -disable        exactly -verbosity 0: back to 0, which is off outright
XHCISNAP -probe          check the ROUTE only, with four controls
XHCISNAP -help           the long help; bare XHCISNAP prints the short one
XHCISNAP -force ...      write to a key matched by value NAME alone
```

The ladder: 0 off; 1 the channel with counters only and the ring still off;
2 adds the note ring (this is the log, use this one); 3 adds the PORTSC table
to the `.TXT`; 4 adds everything including kernel addresses.

The published sequence is four steps and none of them is `regedit`:
`-verbosity 2`, restart, reproduce, `-o C:\NAME`. There is one value to set.
`-verbosity N` sets exactly N, up or down, and is the only knob.

`-verbosity N` prints each key's previous level beside the write. A value out
of range is one the driver refuses rather than clamps (a start that reads it
applies the default, which is off), so the tool reports it rather than silently
correcting it. A reported value cannot drift from the driver's policy; a
corrected one can.

The report is about the next start, never about what a running driver did. The
value is read once per start, so a key edited since the last boot has been read
by nothing, and a controller may be running at a level its key no longer names.
The registry cannot tell those apart. The level actually applied travels in a
dump's header, which is where to read it.

There is a fifth step that is not one of the four: `-disable`, once the capture
has been sent. While the channel is on, any local user who can open `\\.\HCD<n>`
can read this driver's diagnostic state through it. That is accepted rather than
overlooked. This driver cannot tighten the door, because usbport owns the device
object, hardcodes the name, completes `IRP_MJ_CREATE` with no work and leaves the
IOCTL `FILE_ANY_ACCESS`. So the registry value is the whole access story:
Administrators on Windows 2000, nothing at all on Windows 98, which has no user
boundary. The content is this driver's own counters, notes, PORTSC and the
miniport extension (raw in the `.BIN` at any level from 1 up), with kernel
addresses in the note ring and the `.TXT` only at level 4; no user data and no
other process's memory.

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
  screen says so at the time.

A capture that fails before publication leaves the previous set alone. Each raw
region is written to `NAME.BIN.TMP` / `NAME.PSC.TMP` and the pair is renamed
over the final names only once both are in hand, with the old `.TXT` retired at
the same moment. So `-c` naming the wrong controller, or a channel that was
never switched on, leaves `NAME.BIN`, `NAME.PSC` and `NAME.TXT` as they were,
rather than truncating the first and leaving the other two beside it looking
like a set. A failure during publication is loud: if either rename fails the
tool deletes both final raw names and says no dump was published, so what is
left is an absence and not a mixture.

The PORTSC decode is printed on screen whatever the level, because that is what
the bench reads on the spot. The headline test is per port: a port reporting a
device connected with `PP` clear is Finding Q read off the register, whatever
the other ports say.

## Three things to know before trusting a dump

It is windowed, so it can tear. usbport refuses `ParameterLength > 0x10000`
before the miniport is ever reached, and the extension is larger than that
(over 90,000 bytes; the `SIZEOF` line of a regenerated `offsets.txt` is the
exact figure), so a dump is several IOCTLs and the driver runs between them. Every window carries a tear detector, the sum of
`CheckCallbacks`, `DpcCount` and both halves of the log's producer accounting,
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
| Device | `\\.\HCD<n>`, from the `\DosDevices\HCD<n>` link the HCD FDO's start path always creates |
| Buffer | one buffer, in length == out length, at least `0x28`; `RequestBufferLength` must equal that length exactly |
| Gate | none |

## What has been executed, and what has not

It works on Windows 98. Observed in the 2a QEMU guest, on NUSB 3.3's own
`USBPORT.SYS` 5.00.2195.5652, the binary the route was derived from.
`\\.\HCD0` opens, the IOCTL round trip completes, `-probe`'s four controls
return 0 / 2 / 4 / 7, the driver's own debug trace carries `cb PassThru` lines
(so the callback was reached rather than inferred), and an 87,592-byte
extension image came back in two windows and decoded against an `offsets.txt`
regenerated from the same tree, with the header's tear detector equal to the
`CheckCallbacks` decoded out of the dump body.

Only three of those four numbers are fixed. The first control reports whether
this driver answered, so it is state-dependent by design: `0` when the channel
is on and the miniport answers (which is what this reading was taken with), and
`6` (`MINIPORT DECLINED`) when it is off, which is where every machine sits by
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
| `the miniport ANSWERED` | the channel is live; take the dump |
| `the request reached a miniport and it DECLINED` | usbport is fine. Two situations and the driver cannot tell you which; see below |
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
```

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
