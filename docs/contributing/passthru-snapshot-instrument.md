# The PassThru snapshot instrument, as built

This is the as-built record of the driver's read channel: the route a
user-mode tool takes to read `XHCI_EXTENSION` and the PORTSC array out of a
running machine through usbport's `PassThru` escape, the wire contract, the
ordering rules on the kernel side, the tool's behaviour, and what has been
executed on which target. The channel ships in all three flavours, `release`
included, since `0.0.0.6`, and `XHCISNAP.EXE` is the tool that reads it. It is
the only mechanism that has ever carried this driver's evidence off a Windows
98 machine.

Design record 08 (`docs/contributing/design/08-build-flavours-and-the-log-channel.md`)
owns the why: the flavour axis, the verbosity ladder, the retirement of the
ring-0 file sink, the rejected registry write, the security posture. Its
section 13 is the design of this channel. This document owns the what: the
wire contract, the ladder's operational meaning, the tool's behaviour, and the
execution record. Where a fact belongs to both, one states it and the other
points; the one intended duplicate is a warning, which stays in both.

The usbport-side derivation (every address, every length check, every status
code, and the evidence for each) is
`docs/usb-xhci-info/usbport-miniport-abi.md` under "Debug / single-packet",
with its `docs/contributing/legal-provenance.md` section 4 row. That box is
the authority for the route. This document is about the instrument built on
top of it.

Where the code lives:

| Half | Where |
|---|---|
| kernel side | `xhciPassThru` and its two helpers in `src/xhci_dispatch.c`; the wire format in the block after `XHCI_EXTENSION` in `src/xhci.h` |
| host side | `xhcisnap/`: `xhcisnap.c`, `build.cmd`, `README.md` |
| regression | `test_passthru_snapshot` and `test_passthru_snapshot_disabled` in `test/test_init.c`, run on every build |
| the sanctioned PORTSC exception | the bullet in `docs/contributing/implementation-invariants.md` next to "there is one way to read a port" (section 5 rule 8) |

How it is switched on and what a user is told to run: section 8. The four
things a reader should know before decoding a dump:

- It is gated by one registry value, `XhciLogVerbosity`, and by rung 0 of that
  ladder alone: once engaged it serves both regions whole at every level. The
  ladder gates what is recorded into the image and what the tool's plain-text
  companion publishes, never what the engaged channel serves. Why a serving
  ceiling cannot be built on this wire format is design record 08 §13.2's;
  the warning is repeated here on purpose.
- It defaults to `0`, and at `0` the callback does not engage the IOCTL at
  all: it takes section 5 rule 1's existing return, `MP_STATUS_NOT_SUPPORTED`,
  so a switched-off channel answers a caller as a binary built without one
  would. Do not add a "disabled" status code. Section 8's `-probe` table
  carries the consequence: "declined" is two situations, and the tool names
  both. The bootstrap is not circular, because the value is set from ring 3
  without the IOCTL.
- The access control lives in the registry value, not in the device object.
  usbport owns the door (the name, the open semantics and the ACL are all its,
  and Option A leaves this miniport no lever on any of them), so while the
  channel is engaged, any local user who can open that name can take a dump;
  what they get is this driver's own diagnostic state and nothing else.
  Writing the value needs Administrators on Windows 2000 and nothing at all
  on Windows 98. That is a measurement, taken on the one target that has a
  user boundary. The derivation and the posture are design record 08 §13.2's
  amendment; the door itself is `docs/usb-xhci-info/usbport-miniport-abi.md`,
  "Reachability from user mode".
- The wire format is at schema 3, with a 22-ULONG, 88-byte header, and
  `sizeof(XHCI_EXTENSION)` is the `SIZEOF` line of the `offsets.txt`
  regenerated from the tree (over 90,000 bytes). A dump decodes only against
  an `offsets.txt` regenerated from the same tree (section 7).

The tool refuses any driver whose reply signature, schema version or header
size is not the one it was built against, and `make-release.ps1` throws on a
stale `XHCISNAP_VERSION` for the same reason it throws on the qualifier's
`TOOL_VERSION`: the tool prints its version into the header of every report a
user pastes into an issue, and a stale `XHCISNAP.EXE` in a cut is how a user
would meet that refusal.

---

## 1. What it was for

On Windows 98 this driver already records everything a mechanism question
needs, the counter block and the note ring, both inside `XHCI_EXTENSION`, and
none of it could get out. There is no ring-0 file sink (08 §13.0.1), the
DebugView sink has no viewer there, and a flush needs a PASSIVE moment a
wedged session never provides. What was missing was a way to read, not a way
to record.

So the instrument is a read channel and nothing else. It is named `OBS`
rather than `FIX` for that reason: it changes no behaviour, repairs nothing,
and adds no field to `XHCI_EXTENSION`.

That last point matters for decoding. Several candidate defines add a field
and so move `MiniPortExtensionSize`, which invalidates the tracked offset
table, which is what a counter reader must not do. The ones known to do it
are `XHCI_FIX_NO_RING_REUSE`, `XHCI_FIX_ACK_OWED` and the polling/gate
candidates in `src/xhci.h`; others, such as `XHCI_FIX_EVT_REARM`
(`src/xhci_cmd.c`) and `XHCI_FIX_QUIESCE_GATE` (`src/xhci_slot.c`), are
behaviour-only and change no field. This block adds no field, so a shipping
binary decodes against the ordinary `offsets.txt`. Do not reason about a
combination of defines from that list: measure `sizeof(XHCI_EXTENSION)` for
whatever set you build, which is the check section 11 ends on.

## 2. The route, in one table

| | |
|---|---|
| IOCTL | `0x00220438`: `IOCTL_USB_USER_REQUEST`, `METHOD_BUFFERED`, `FILE_ANY_ACCESS` |
| Request | `UsbUserRequest = 3` (`USBUSER_PASS_THRU`) |
| Device | `\\.\HCD<n>`, from the `\DosDevices\HCD<n>` link usbport's HCD FDO start path attempts to create at a fixed index. It can fail silently; see section 9 trap 3 |
| Buffer | one buffer; input length must equal output length; at least `0x28` bytes; `RequestBufferLength` must equal that length exactly |
| Gate | none. `IOCTL_USB_DIAGNOSTIC_MODE_ON` is not a prerequisite |
| Miniport slot | `PassThru`, packet offset 0xE0 |

## 3. The three usbport clauses the design is shaped by

1. The `parameters` block is a non-paged kernel copy usbport made of the
   system buffer and copies back afterwards. There is no user address here,
   nothing to probe, and it is legal to fill under a spin lock at DISPATCH,
   which is what makes "lock, copy, return" possible at all.
2. `PassThru` is entered at PASSIVE_LEVEL holding no usbport lock, so taking
   this driver's own controller lock inside it is safe.
3. usbport refuses `ParameterLength > 0x10000` before the miniport is ever
   reached, and `sizeof(XHCI_EXTENSION)` is larger than that. So one call
   cannot carry the extension: the reader is a window over a region, and the
   tool loops on `Offset` and concatenates. Section 6 is what that costs.

## 4. The wire format

`src/xhci.h` is the wire format's owner. It carries schema 3, an 88-byte,
22-ULONG header, and the field-by-field reasons for each field. Read it, not
this, before writing a decoder.

The header names the signature, schema and header size; the status bits; the
region, offset, region size and payload size of the window;
`ExtensionBytes`, the layout key (section 7); the port count; the tear
detector (section 6); the build flags; and a block a reader can print with no
offset table at all: `Flavour`, `VerbosityRead`/`VerbosityApplied`, each
switch's `MPSTATUS`, `SwitchRead`, and the note ring's offset, capacity, head
and fill.

That last block is what
the plain-text companion is built out of. It is not the gather table design
record 08 §13.2 rejected: every one of those is a scalar or an offset the
compiler derives from the same struct the payload is a copy of, so it cannot
drift from the layout the way a hand-written table would.

Four properties are the contract, and none of them is in the struct:

- Nothing but ULONGs, so the layout is the same under every alignment rule
  MSVC 6.0 has and a host-side reader decodes it with a single unpack. The
  payload follows the header immediately and is raw bytes.
- The request overlays the first three ULONGs of the reply (request
  signature, region, offset). One buffer serves both directions, which is
  usbport's rule, not a choice.
- The format is offset-free on purpose. The header names sizes; nothing in it
  knows where a counter lives. Decoding is the existing host-side machinery
  against `ExtensionBytes`.
- A schema number moves for a shrinking header as well as a growing one. A
  header that loses a field is as much of a decode hazard as one that gains
  one, and the rule in `src/xhci.h` says so.

The two regions are the extension (`XHCI_SNAPSHOT_REGION_EXTENSION`) and the
PORTSC array (`XHCI_SNAPSHOT_REGION_PORTSC`). A caller puts a request
signature in the block before the call, so a GUID match against uninitialised
memory is refused rather than answered.

## 5. The kernel side, and the ordering rules that are not obvious

The whole callback is: validate, fill a truthful header, take the controller
lock, copy a window, release, return. The rules that make it correct, in the
order they bite:

1. Return exactly `MP_STATUS_NOT_SUPPORTED` (6) for any GUID that is not
   ours, and never any other nonzero value. usbport's own `PassThru` call
   site, its root-hub port-status probe, retries through `RH_GetPortStatus`
   only when the return is exactly 6 (NUSB `00028603`). A miniport answering
   `MP_STATUS_FAILURE` there would suppress that fallback silently and leave
   usbport reporting a zeroed port status.

   That site is reachable only through a test-mode USBUSER request so it
   cannot fire in ordinary use, but the rule costs nothing and the failure
   would be invisible.

   This rule is
   live in every published binary, so it has a host regression rather than
   trust: `test_passthru_snapshot` and `test_passthru_snapshot_disabled` in
   `test/test_init.c`. The switched-off channel (verbosity 0) takes this same
   return.
2. Read the request out before writing a byte of the reply, because request
   and header overlay each other in the caller's block.
3. Once the GUID matches, report every refusal in the header, not through
   the return. usbport collapses every nonzero `MPSTATUS` to one
   `UsbUserStatusCode`, so a status returned that way is indistinguishable
   from usbport's own errors. The single exception is a block too small to
   hold a header (`parameterLength < sizeof(XHCI_SNAPSHOT_HEADER)`), where
   there is nowhere to write it; that one returns `MP_STATUS_FAILURE`.
4. Fill every header field before any early return. A refused window still
   comes back with a header that is true about what it can be true about,
   `ExtensionBytes` above all (section 7).
5. Take `TearDetector` inside the lock, so it belongs to the window and not
   to the moment before it.
6. Copy byte at a time. `RtlCopyMemory`/`memcpy` resolve to a compiler
   intrinsic or an `ntoskrnl` import depending on build flags, and this
   driver decides its import list on purpose rather than by build-flag
   accident, the same reason `xhciZeroPacket` exists. A full window is up to
   65,488 iterations under the controller lock, on the order of 100 us at
   DISPATCH. That is the right trade: the instrument runs on a wedged, idle
   machine and is in no hot path, and the ISR does not take this lock, so
   what the hold delays is the command engine and the DPC, not interrupts.
7. Refuse a PORTSC `Offset` that is not ULONG-aligned (`S_BAD_REQUEST`)
   rather than rounding it. A PORTSC window is an array of ULONGs; a byte
   offset that does not land on one has no honest answer, and rounding would
   hand the caller a shifted array to reassemble.
8. The PORTSC region is read with `XhciReadPortsc` (a bare read) and neither
   acknowledges the change bits nor folds them into the port shadow. This is
   the one sanctioned exception to "there is one way to read a port". The
   reason: Finding Q established that nobody had ever read PORTSC in the
   wedged state, and an instrument that acknowledged what it came to measure
   would destroy the evidence on the one boot that mattered. The conditions
   the exception is granted on belong to the rule, not to the instrument, so
   they are in `docs/contributing/implementation-invariants.md` next to the
   rule itself and are not restated here. The first of them is that the
   channel is shut by default and reads no register at rung 0.
9. Port numbering is 1-based at the register. The array index `i` maps to
   `XhciReadPortsc(ext, first + i + 1)`.

Section 5 is a specification, not the source: it names the ordering rules and
the reasons for them. The exact validation order, the unlock path on every
branch, the window and PORTSC capacity arithmetic, and the regression matrix
in `test/test_init.c` (null arguments, short and header-only buffers,
past-end, a corrupted extension, a misaligned PORTSC offset, and
PORTSC-and-shadow preservation) live in the source.

## 6. Windowing, and the tear detector

A dump is several IOCTLs and the driver runs between them, so a dump can
tear. Every window carries `TearDetector`, read inside the driver's lock. It
is `CheckCallbacks + DpcCount + Log.Appends + Log.Suppressed`
(`src/xhci_dispatch.c`), so it moves for the DPC and for every producer call
as well as for usbport's health check. The tool prints the pair and the step
(`tear detector 4431 -> 4438 (+7)`), because reporting a change and then
printing only the value it started at made one idle tick and a thousand DPCs
read identically. The detector is monotonic, so the step is a magnitude.

Read it in the direction it works. Unequal across a dump's windows proves the
dump is torn and any counter in it may be a mixture. Equal proves only that
none of the four counters it sums moved between the windows. It is not
evidence that nothing else happened, because there is no counter behind every
field in the extension. So equal is "no evidence of tearing", not "quiescent".

It is not a lock and it does not prevent tearing. It detects the things it
counts, and the tool's last line of output says which kind of dump you have.

Measured on the 2a guest: an idle machine with nothing plugged in produced a
torn dump, and a busy one moving bulk traffic produced an untorn one. Two
dumps is not a rate, but it is the opposite of the intuition, so do not reason
about the likelihood of tearing from how busy the machine looks.

## 7. `ExtensionBytes` is the layout key, and getting it wrong is worse than failing

Decode a dump only against an `offsets.txt` regenerated from the same tree.
The tool prints the driver's own `ExtensionBytes` for this reason: a dump
decoded against the wrong table is a wrong reading, not a failed one, and
wrong readings are how the Finding 3 investigation lost time. The decoder
refuses on a size mismatch.

The decode chain:

```
scripts\local\regen-offsets.cmd          (from the same tree)
scripts\local\readsnap.py NAME.BIN --ladder
```

`.BIN` is the raw extension image, the same artifact the QEMU live-counter
reader produces, so `offsets.txt`, `counters.py` and `readcounters.ps1` decode
it unchanged. `.PSC` is the raw PORTSC array; its decode is also printed on
screen, because that is what a bench reads on the spot.

## 8. The host side

```
XHCISNAP -verbosity 2    set the level exactly (0-4), then RESTART
XHCISNAP -dump           dump controller 0 to XHCISNAP.BIN/.PSC/.TXT
XHCISNAP -dump -c 1 -o WEDGED   controller 1 to WEDGED.BIN/.PSC/.TXT
XHCISNAP -disable        exactly -verbosity 0: back to 0, which is off outright
XHCISNAP -probe          check the ROUTE only, with four controls
XHCISNAP -help           the long help; bare XHCISNAP prints the short one
XHCISNAP -force ...      write to a key matched by value NAME alone
```

The ladder, in the tool's own words: `0` off, `1` counters, `2` + the note
ring (this is the log), `3` + the PORTSC table, `4` + everything including
kernel addresses. Each rung is the one below plus one thing, so a level is
always a superset and never a different report. A value outside `0`-`4` is
refused rather than clamped, and a start that reads one applies the default,
which is off. Why there are five rungs and not four, why consent and depth
are one value, and what kind of line the 3 / 4 boundary is, are design record
08 §13.2's; the driver's own `XHCI_LOG_VERBOSITY_*` in `src/xhci_log.h` owns
the numbers.

The published sequence is four steps and `regedit` appears in none of them:
`-verbosity 2`, restart, reproduce, `-o C:\NAME`. There is one value to set.
`-verbosity N` sets exactly N, up or down, and is the only knob. The tool
prints each key's previous level beside the write, so an out-of-range value
(which the driver refuses rather than clamps, so that a start reading it
applies the default, off) is reported rather than silently corrected. The
tool already duplicates the driver's wire format for want of a shared header;
correcting the value would have made the driver's policy a second thing that
can drift, and a reported value cannot drift.

That report speaks about the next start and never about what a running driver
did, because the value is read once per start: a key edited since the last
boot has been read by nothing, and a controller may be running at a level its
key no longer names. The applied level is in a dump's header, which is the
only place it is known. The tool finds the driver's per-machine software key
itself, on every xhci98 controller the machine has: setting one of two and
reporting success would be the worst outcome, since the user then reproduces
on the other and dumps an empty ring.

Three files, and only one of them is the report. `NAME.BIN` is the raw
extension image and decodes only against an `offsets.txt` regenerated from
the driver's own tree, which the maintainer has and the user does not, so it
is the right thing to attach and the wrong thing to be the only output.
`NAME.TXT` is built entirely out of what the driver puts on the wire and
needs no offset table; that is the one to paste into an issue, at levels 1 to
3. What goes into it is gated by the ladder: the header block always, the
note ring's text at `>= 2`, the PORTSC table at `>= 3`, and kernel addresses
at 4, which `XhciLogAppendAddress` refuses below that rung so that the driver
enforces the line rather than this document promising it. That is a
publication line rather than a transport one: the engaged channel serves both
regions whole at every level.

`-probe` exists for the case that happens at a bench, nothing came back, and
separates its causes in one command:

| `-probe` says | It means |
|---|---|
| `the miniport ANSWERED` | the channel is live; take the dump |
| `the request reached a miniport and it DECLINED` | usbport is fine. Two situations, and the driver cannot tell you which; see below |
| `cannot open` | no usbport HCD link on this machine at all |
| opens, but `DeviceIoControl failed` | something else owns that name; try `-c 1`, `-c 2` |

The second row is two situations because the ordinary case on every machine
is "an xhci98 with the channel switched off". A switched-off channel answers
a caller as a binary built without one would, since section 5's rule 1 makes
6 the only honest nonzero value at that slot, so no probe can separate them,
and saying which it is would be a guess. The tool names both and offers the
fix for the one that is fixable from where the user is standing: `-verbosity
2`, then restart.

The tool duplicates the wire format rather than including it from
`src/xhci.h`, which is a kernel header. That duplication is checked at run
time: the tool refuses any driver whose reply signature, schema version or
header size is not the one this build knows, and says to rebuild from the
same tree.

The PORTSC decode on screen tests per port: a port reporting a device
connected with `PP` clear is Finding Q read off the register, whatever the
other ports say. A second line covers the all-ports case, because on a
controller that really did lose power everywhere it is the more useful
sentence, and a third says plainly that unpowered SuperSpeed ports are
expected rather than a fault. (An earlier version announced the unpowered
case only when every port read `PP = 0`, which cannot happen on a controller
with SuperSpeed ports; the E460 leaves six of its eighteen unpowered by
design, so the headline stayed silent while the per-port table said the
thing it was meant to shout.)

It is built with MSVC 6.0 in place from `tools\MSVC600` via
`xhcisnap\build.cmd`. `/Za` is not used here even though the driver and the
host tests both use it: that compiler's own SDK headers are full of anonymous
unions, which `/Za` rejects, so `windows.h` does not compile under it. `/WX`
still makes a warning a build failure, and the source keeps the same C89
rules by hand; it has to run on Windows 98 SE.

## 9. The three operating traps, each of which cost a boot

The first two are closed by the tool as shipped; they are kept because a
rebuilt tool that quietly reintroduced either would be the same boot spent
twice. The third is usbport's and stands.

1. Recording used to be gated by a sink. The trap was that the ring filled
   only when `XhciLogDebugView` was set to 1, because setting it made
   `Log.Enabled` nonzero, which was the only thing the append sites tested;
   that meant switching on a sink known to be dead so that a ring would fill,
   which is not an instruction a user can be given. Recording is now gated by
   `XhciLogVerbosity` and by nothing else, so the ring fills at level `>= 2`
   with no sink named anywhere. `XHCISNAP -verbosity 2` sets it. The ring-0
   file sink (`XhciLogFile`), which reached `ZwCreateFile` on the boot path
   where task 11-V.7 measured an open that never returns, no longer exists.
2. The tool's output used to need redirecting to a file. The PORTSC table
   scrolls off a DOS box on real silicon. The tool now writes `NAME.TXT`
   beside `NAME.BIN` and `NAME.PSC`, a plain-text companion carrying
   everything it can decode with no offset table, so there is nothing to
   redirect. That file is also the one a stranger pastes into an issue; the
   `.BIN` is the attachment.
3. usbport publishes its HCD link at a fixed index with no retry. On a
   machine where Windows 98's own USB stack already owns that name (it does,
   for any UHCI or OHCI controller it drives), `IoCreateSymbolicLink` fails
   and no link for the usbport controller ever appears. The failure is
   silent: the driver starts, binds and runs, and only the reading channel is
   missing. Measured: with the 2a guest's UHCI present, `\\.\HCD0` opened but
   every IOCTL failed and the trace showed no `cb PassThru` at all, while
   `HCD1`-`HCD3` did not exist; removing the UHCI made it work first time.
   Run `-probe` before trusting an absence.

## 10. What was executed, and what was not

Read the two eras apart. Everything up to batch 13-R was taken with a
candidate build (`XHCI_OBS_SNAPSHOT` behind `XHCI_EXTRA_DEFINES`, which
carried the do-not-deploy marker and could not be packaged), so "it worked"
was a statement about a binary nobody could install. Everything from batch
13-L was taken with a shipping build.

With the candidate build:

- Windows 98, in the 2a QEMU guest on NUSB 3.3's own `USBPORT.SYS`
  5.00.2195.5652, the binary the route was derived from. `\\.\HCD0` opens,
  the IOCTL round trip completes, `-probe`'s four controls return 0 / 2 / 4 /
  7, the driver's own trace carries `cb PassThru` lines (so the callback was
  reached rather than inferred from a status), and an 87,592-byte extension
  came back in exactly two windows and decoded against an `offsets.txt`
  regenerated from the same tree, with the header's tear detector equal to
  the `CheckCallbacks` decoded out of the body.
- Real silicon, on the E460 in batch 13-R, where it read Finding 3's root
  cause off a wedged machine (`run-13e.md` Findings R and S).
- The same contract was separately exercised on the Windows 11 development
  host, where all four controls also matched, which raises confidence in the
  derivation without being an observation on either target.

With a shipping build, all at stage L3 (the guest readings below are the
record for the guests, and `run-13e.md`, "Stage L3", is the record for the
E460):

- Windows 98, 2a guest, `debug` flavour. All four ladder readings: level 0
  declines with the route intact, level 1 returns the counter block with a
  ring 0 of 16384 bytes used, level 2 returns a 981-byte ring, and both
  regions come back whole at every engaged level (`schema 3, 88-byte header`,
  `ExtensionBytes 90272`, flavour read off the wire). The level-2 reading was
  taken twice: the first ran with a stale `XhciLogDebugView = 1` this guest
  carried from the 11-V.9 work; the second, with `log.debugview=00000000`
  beside a 981-byte ring, is the one that proves a driver recording with no
  sink selected at all.
- Windows 2000, 2b guest, `debug` flavour, against SP4's own `usbport.sys`
  5.00.2195.6681 rather than the NUSB 5652 build the route was derived from:
  the same four control answers, the same `read 2, APPLIED 2` and a 981-byte
  ring with `log.debugview=00000000`. So the structural reading of the SP4
  binary is an observation on both targets;
  `docs/contributing/legal-provenance.md` §4's row for this route records
  runtime on both. The same session took the non-administrator measurement
  design record 08 §13.2's posture rests on.
- Real silicon, the E460, with both shipping flavours (task 13-L.3, seven
  cold boots, `run-13e.md` stage L3). `release` first and then `debug`, each
  reading back its own flavour, extension 90,272 and schema 3 off the machine
  rather than from what was staged; every `.BIN` 90,272 bytes at both engaged
  levels, which is the ladder gating recording and publication and never what
  the channel serves; `XhciLogDebugView` read 0 at boot 0 and again at boot
  4, so the ring filled with no sink selected. Every dump decoded against a
  table regenerated from the same tree. This is the first time a shipping
  binary carried this driver's own log off a Windows 98 machine.

What is still not observed: nobody outside this project has run any of it.
Every reading above is a maintainer's, on a machine this project controls,
and the sentence the channel exists to make true (a stranger sends a capture)
is still an expectation.

## 11. Rebuilding it

Everything the channel consists of is tracked at the tip (the table at the
top of this document), so a rebuild after a removal starts from the source,
not from this prose. What this document is for is the part the source does
not give you: why each rule exists, which usbport clauses the design is
pinned to, what the three operating traps cost, and what was and was not
executed on which target.

Two rules for anyone who removes and restores it:

- Restore the invariants bullet (the sanctioned PORTSC exception) in its
  existing text, rewriting only a condition that has genuinely changed.
  Restoring an exception by paraphrase is how the conditions it was granted
  on quietly become something else, and an exception standing with nothing
  behind it is how a rule erodes.
- Before trusting a dump from any build, measure `sizeof(XHCI_EXTENSION)`
  and confirm the offset table you are decoding against came from the same
  tree. The extension size has moved between generations (87,592 when the
  instrument was first built, 90,600 and 90,280 in between, 90,272 in the
  tree that ships it), and a dump taken before a move does not decode
  against a later tree.

A guest's driver, its extension size and its `XHCISNAP.EXE` are three things
that go stale independently. Confirm what is on a guest with `fc /b` against
a reference copy before concluding anything from it; one staging boot found
a stale `XhciLogDebugView = 1` restored by a snapshot rollback, which would
have made a level-2 reading prove nothing. `qemu-img snapshot -l
vm\win98.img` lists the safety snapshots.
