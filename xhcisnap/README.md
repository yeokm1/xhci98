# xhcisnap - reading this driver's own log off a running machine

`XHCISNAP.EXE` reads `xhci98.sys`'s extension, its raw PORTSC array, (from
2.0.0.0) its enabled slots and (from 2.2.0.0) its root ports' enumeration and
counters from user mode, and writes a report a user can send back.

Two drivers answer it. From 2.0.0.0 `xhci98.sys` is the successor host
controller driver (`docs/contributing/roadmap-hcd.md`, design record 13): it
owns its driver object, creates `\DosDevices\HCD<n>` itself and answers the
same USBUSER `PassThru` request through its own door (`src/hcd_door.c`, schema
5 unchanged, the slots and HCD regions added). The 1.x miniport ran under usbport and
was reached through usbport's `PassThru` vendor escape. Most of this document
is the 1.x history; where it says usbport or the miniport, that is the 1.x
route, which the tool still reads.

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
XHCISNAP -force ...      write to a key matched by value NAME alone. It is a
                         modifier on -verbosity / -disable only: on its own it
                         asks for nothing and is refused with exit 2
```

The ladder: 0 off; 1 the channel with counters only and the ring still off;
2 adds the note ring (this is the log, use this one); 3 adds the PORTSC table
to the `.TXT`; 4 adds everything including kernel addresses.

The published sequence is four steps and none of them is `regedit`:
`-verbosity 2`, restart, reproduce, `-o C:\NAME` (`-o` and `-c` each imply
`-dump`, so the last step needs no flag of its own). There is one value to set.
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
budget and refusals - and `XHCIHC_COUNTERS`, the 63 counters the device matrix
reads. A port is printed only when it has something to say, the counters only
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
  handling, with the counters still counting), `XhciIntervalCap` with whether
  the cap applies on this controller and how many endpoints it lowered, and
  `XhciAvgTrbEsit`.
- **The controller**: running; failed with an in-place recovery still owed;
  latched failed after three recoveries failed in a row; latched failed
  because the recovery window refused a fourth inside ten minutes; or
  contained as unreadable (USBSTS read all ones). The two latched states end
  only at a stop and start.
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
