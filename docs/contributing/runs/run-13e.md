# Batch 13-E Run Sheet - the E460 bench trip

The run sheet for roadmap batch 13-E, Phase 13's Windows 98 half, on the
ThinkPad E460. It was written before the trip, for the same reason
`docs/contributing/runs/run-11v.md` was: a clause decided in front of a running
machine is decided by whoever is standing there, under time pressure. The pass
reading for each stage was written down in advance, and the RUN box under each
stage records what was observed.

`docs/contributing/roadmap.md`, "Phase 13 - Final Bare-Metal Validation", is the
authority for what this batch owes. This file is how, and where the two disagree
about a clause, the roadmap wins.

**On `vm\...` paths in this file.** The per-run evidence they name - debugcon
traces, QEMU trace logs, screenshots, counter dumps - was discarded on
2026-08-30 after the RUN boxes and finding tables had transcribed every reading it rests on. A
`vm\` path here says where a reading was taken and what the file was called;
it is not a file a clone, or the maintainer's own tree, still has. `vm/` now
holds only the guest images and transfer disks the harness needs.

## Status

Batch 13-E is closed. Stages E0, E1, E2, E3, E4 and E6 have all been run
against this sheet; E5 is optional and was not run. Stage L3, a batch 13-L stage
run in a later session, sits at the bottom of the file. Each stage's RUN box is
the record; the plan above each box is left as written, because a stage that
turned out to cost two reboots instead of one is worth more written down than
the estimate it replaced.

No Windows 2000 hardware run exists. This sheet was written while a Windows
2000 vehicle on real silicon was still expected (batch 13-T), and it hands
several counter readings forward to that vehicle. It was never obtained: Windows
2000 Setup bugchecks on both fleet machines, and batch 13-T was removed with its
run sheet. Every counter reading described below as a Windows 2000 errand is
therefore a statement of what is not known, not a pending item. Nothing here may
be reported as blocked by that missing vehicle; the batch has no failure branch.
Where a Windows 98 route exists instead (the PassThru snapshot instrument,
`passthru-snapshot-instrument.md`), it is still available.

No E stage on this machine was read from a counter, and DebugView must not be
run on it. Task 12.2 closed with no Windows 98 bare-metal trace channel, so every
E-stage reading is behavioural: Device Manager, a device working or not, a
photograph. The DebugView ban is a measurement from three device classes,
including a Low-Speed mouse (see "Session record - the DebugView ban,
measured"). It is a ban on a trace sink; the PassThru snapshot read, which ships
in every flavour from `0.0.0.6` and which stage L3 used on this machine, prints
nothing and is unaffected.

Two more facts shape the sheet. The machine was reinstalled from scratch on the
morning of bench session 1, so it ran no driver, carried no NUSB and held no SE
CD copy; stage E1.0 is the deployment that resulted. And there is no snapshot on
metal: stage E5 changes the USB stack with no undo, so every clause read
against the plain-NUSB baseline is taken before it.

A clause whose reading was not taken is a clause that did not run. Readings were
written down at the bench and the RUN boxes composed from the notes afterwards,
never from memory.

## The E460's sessions, and what it carries

The sessions this machine has had, in order, are the names the rest of this file
uses; nothing here is dated.

1. Bench session 1: the from-scratch Windows 98 SE reinstall, stage E1.0's
   deployment, and stages E0, E1 and E2.
2. The DebugView check: one boot, no stage clause, which turned the DebugView
   ban from an inference into a measurement.
3. Bench session 2: stages E3 and E4.4, and Findings D to J.
4. The two batch 13-R boots: Findings T and U, on candidate instruments.
5. Bench session 3: batch 13-R's third boot (Finding V), then the `0.0.0.5`
   re-takes: stage E1's four SHA-256s, stage E4's leftovers, stage E6.
6. The enclosure visit: stage E4.2's I/O half, on the replacement enclosure.
7. The stage L3 session: batch 13-L on `0.0.0.6`, the first counters read off
   this machine with `XHCISNAP`.

Three batch 13-R boots happened on this machine: the pair at item 4 and one at
the start of bench session 3 (Finding V, five recipe cycles at position D, five
files home in `run-13e-evidence/p13*`). That third boot ran the 90,600-byte
extension `XHCI_OBS_SNAPSHOT` instrument (83,867 B, sha256 `27180ecc...`), the
last reading that throwaway build took: task 13-R.4 removed it from the tree.
Task 13-L.2 later rebuilt the PassThru channel into every shipping flavour, and
stage L3 read this machine again with `XHCISNAP`.

Read the machine's state off the machine, never off this file. Identify the live
binary with `fc /b` against a reference copy: candidate binary sizes collide, and
so do three published ones (`0.0.0.2`, `0.0.0.3` and `0.0.0.4` are all 81,899
bytes). The reference copies are in the kit at `out\bench-13e-0005\reference\`.
Boot 0 of bench session 3 measured the state the batch 13-R boots had left: the
live `C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS` was 81,899 bytes, `fc /b` reported
no differences against `R0004.SYS` (the published `0.0.0.4` release build), and
`XhciLogDebugView` read 0. The reported state and the measured state agreed.

`C:\XHCI\` holds the candidate binaries `OBS0824.SYS` (Finding T's instrument),
`OBS24B.SYS` (Finding U's), `OBSSNAP.SYS`, and `XHCISNAP.EXE`. It does not hold
an `XHCI98.SAV`; that was checked on the batch 13-R retry, and nobody wrote down
where the reference copy that boot restored from lives. Find it before planning
a restore around it.

Bench session 3 put the shipping `0.0.0.5` binary on the machine by `ren` +
`copy` (item P14), taken from `out\xhci98-0.0.0.5.zip` rather than from that
version's tracked release directory, which carries neither `usbd.sys` nor
`usbhub.sys` and is not complete media (`releases/README.md`). An INF install over an existing one
bugchecks Windows 98 at `0028:C00312EE`, and a user-style install from the zip is
the acceptance run's act on `1.0.0.0`, on this machine. The stage L3 session then left the
debug candidate `L3DBG.SYS` installed; see stage L3's RUN box.

## Finding status

The findings are written newest-last within each session, and several were
refuted or superseded by later ones on the same machine. Citing a retired finding
as live cost this investigation a day and three candidate binaries. This table
is the status; the sections are the evidence.

| Finding | Status |
|---|---|
| V - the repair holds and the poll rate is 36-80 ms | The current reading, from bench session 3. Five recipe cycles at position D on the repaired build: nothing escalated at all, `PollClockMs` 354,364 over 6,461 polls. It refutes S's headline, corrects U's period by one to two orders of magnitude, and closed task 13-R.3.5. Its one open item is Finding U's 971,359 poll count, which is unreconciled |
| S - the Stop Endpoint never completes | Headline refuted by Finding V. Given its proper window the command completes: 123 issued, 123 completed, the abort rung fired 0 times. What it did not do was complete inside the age detector's window, and that window was 2.3-5.1 s rather than the 32 s its author intended. The rest of Finding S (the latch, the missing owner, the census) stands unchanged and is what design record 07 is built on. Phase 14 must not restate the headline in any form |
| U - the repair holds 33 for 33 | Stands as a repair result; its period is withdrawn. The "~1 ms" was 971,359 polls divided by a session nobody timed, an inference corrected by V's direct measurement. Every claim built on it was re-derived |
| T - the recovery works, the cap was a lifetime quota | Superseded in scale by U (3-for-3 became 33-for-33), unchanged in substance. Its cap defect is fixed |
| L - it needs a periodic endpoint's unplug | Confirmed and explained: only a periodic endpoint arms the Stop Endpoint the teardown loses |
| Q - the terminal state is an unpowered bus | Refuted. `PP = 1`, `CCS = 1`, `RhPortsUnpowered 0`, `StopController` never ran. A mouse LED reports enumeration, not VBus |
| P - teardown never reaches PORTSC | Confirmed and irrelevant: it never needed to. The port is untouched; it is the driver that stopped listening |
| N - `W3BOTH` wedged anyway | Explained: slot release and ring reuse are downstream of the latch and could not help |
| M, K, P8 - the excluded variables | Unaffected, all still excluded |
| `W11POLL` / `W10ALL` recovering | Explained: a polled sweep needs no interrupt, so it works in the window before the latch closes |
| `W12EVT`, `W13ACK`, `W7GATE` failing | Explained: all act after the latch, where `xhciRhAdmitted` refuses them |
| `W15SLOW` finding nothing in three minutes | Explained: its sweep is admitted-gated too, so after the latch it never reads PORTSC at all |
| the `XhciRootHubPortEvent` latent defect | Exonerated as this fault: `PortEventsMapped` equals `PortEventChanges`, nothing was dropped. Still a real latent defect |
| the `xhciRhWritePortsc` dropped-ack defect | Untouched by this; still a real latent defect, still not this fault |

One deduction is retired. A review traced `XHCI_EXT_FLAG_RH_CLOSED`, found it
controller-lifecycle only, and concluded the admission gate cannot be reached by
a plug. It was right about that flag and wrong about the gate, which has four
clauses; `ControllerFailed`, the fourth, is reached by a plug through the command
timeout. See `lessons.md`, "Checking one clause of a compound predicate".

---

## At a glance

This section is an index of the sheet. Every row compresses a stage or a
pre-trip item below, and where a row and its stage disagree, the stage wins.

### Decisions taken before booking

| # | Item | Outcome |
|---|---|---|
| 1 | Characterise both hubs and every child in the rig (batch 13-H) | Done before the trip; tree and position D both read. See `docs/contributing/test-equipment.md` (P1, P2) |
| 2 | The audio device's `bInterval` | Read on six devices. It is 1 on all five UAC 1.0 units; the UAC 2.0 Sound Blaster X4 (`041E:3278`) carries isoch `bInterval` 1, 3 and 4. Both targets ship UAC 1.0 drivers and the X4 has no fallback configuration, so clause 3 became "carry it and find out" (P2) |
| 3 | Buy or publish: the Full-Speed 1.1 hub | Publish, no purchase. None held and not reliably purchasable. Recorded in `test-equipment.md` row 5 (P3) |
| 4 | Buy if missing: a single-TT hub | No purchase. `05E3:0608` is held and characterised (P1) |
| 5 | Confirm the Windows 98 SE CD | Second Edition confirmed by the project owner, and its USB components verified by hash: `usbd.sys` 18,912 bytes matches `usbd-sources.expected` exactly, so the disc carries the stack stage E5 installs and is the same media the package's `usbd98.sys` came from (P4) |
| 6 | Confirm the storage enclosure exists | Held: `174C:5106` (ASMedia, "StoreJet Transcend", serial `NB202029162975`), a USB-SATA bridge characterised at both speeds at the enclosure visit. It replaced an earlier enclosure that left the fleet and is not recorded here; E4.2's enumeration half was taken on that departed unit (device table row 9) |
| 7 | The `MTT`/`TTT` print site (the Windows 2000 batch's Q4) | Added before the trip. Nothing in this batch changes: the E460 carries the release flavour and has no counter channel either way |

### The bag

Per the requirements table in `test-equipment.md` ("Requirements the Phase 13
clauses set, and how each was met"): the composite audio device (row 1, E6's
clauses 1-2), the Sound Blaster X4 (`041E:3278`, row 6, the only held device
declaring `bInterval > 1`, and a second unit rather than the same one), the SE
CD, the multi-TT hub (`1A40:0201`) and its barrel-jack PSU (the rig is
characterised self-powered), the single-TT hub (`1A40:0101`, the one-variable
swap partner), the spare hub (`05E3:0608`, rig position H4), the LS HID, the two
flash drives (USB 2.0 and 3.0), the storage enclosure, and the `0b95:7720` ASIX
USB Ethernet adapter (row 10, task 8-V.2's own adapter, for a free
re-observation rather than a clause).

Three hubs, not two. `1A40:0201` and `1A40:0101` are the pair that swap at
position T; `05E3:0608` stays at H4 as the downstream hub stage E3's second tier
needs behind whichever hub is at T. Packing the swap pair alone leaves the rig
unassemblable.

Plus the kit. From bench session 3 that is `out\bench-13e-0005\`, staged by item
P14: the `0.0.0.5` release files taken from the published zip, `XHCIQUAL.EXE` +
`XHCIQUAL.MAP` + the five batch files, and the SHA-256 list (`MANIFEST.txt`),
plus a phone for the photographs and the audio recording. P5's kit at
`out\bench-13e\` carries `0.0.0.3`, which predates the Finding 3 repair, and does
not travel again; it stays on disk as a comparison path. Position T was
physically labelled at stage E0, so the tape rides only as a spare.

### The bench, in one table

| Stage | Boot | Do | Result |
|---|---|---|---|
| E0 | DOS | `XHCIQUAL` no-args, then `1PROBE`..`5XDEV` with cold boots between; map and physically label position T (`XDEV2`/`XDEV3`); read `FSC` off `HCCPARAMS2` | Passed, bench session 1. Every batch reached `Done.`; `XPOLL.LOG` carried the expected `PROVISIONAL` verdict and the full runs qualified the controller; position T = controller port 1, labelled; `FSC=0` |
| E1 | Win98 | the identity row: OS / stack / controller / driver; screenshot Driver File Details | Passed, bench session 1, after the new stage E1.0 deployed NUSB and the driver onto a from-scratch install. Re-taken at bench session 3 on `0.0.0.5`: the four SHA-256s are taken and each matches a known reference byte for byte |
| E2 | Win98 | the composite device on a root port, then behind the hub, on plain NUSB before E5 | Passed, bench session 1: `Code 2` on both topologies, on the C-Media `0D8C:0014`, because the sheet's own unit could not enumerate at all |
| E3 | Win98 | tree at position T: single-TT hub with children H1-H4 and an ordinary child behind the H4 hub, then swap to the multi-TT hub and re-enumerate; photograph the deepest topology. Case B (the FS hub) does not run | Passed on both trees, bench session 2: all five children identical either side of the swap, so the second enumeration, the one `MTT` can get wrong, was not. The E2 carve-out did not apply: the composite bound at H3, task 13-E.1's missing file having been supplied between the two sessions |
| E4 | Win98 | the LS HID at a root port and behind the hub; the enclosure at position D with a file written and read back; the USB 3.0 stick at position D once; the `0b95:7720` Ethernet adapter at position D, task 8-V.2's recipe | E4.4 passed at bench session 2 (DHCP lease plus 748-750/750, the first bulk-IN class traffic through this driver on real silicon). Re-taken at bench session 3 on `0.0.0.5`: E4.1's root half passed, E4.3's void was retaken and passed, a flash-drive round trip passed as E4.2b, the control was clean, and the sequence was Finding G's recipe with nothing wedging (Finding W). E4.2's I/O half closed at the enclosure visit on `174C:5106` at position D. Every reading this row asks for is taken |
| E6 | Win98 | audio: play on a root port and behind the HS hub (the composite), then the X4 as the `bInterval > 1` device. E6.3 runs whatever E5 did | E6.1 and E6.2 play clean at D and behind the multi-TT hub at H3 (Finding X: the published Win98 audio limitation is wrong). E6.3's X4 does not bind, Code 10, no composite parent, so clause 3 is unreadable on this target (Finding Y) |
| E5 | Win98 | one-way: install the SE CD's native USB stack; re-present the composite device; re-take the identity row | Optional, not run. Task 13-E.1 closed without it |

The rows are in stage-id order and the boot order is not: E6 runs before E5. E5
is one-way, E6's clauses 1-2 stopped needing it when Finding D bound the
composite on the current baseline, and clause 3 (the X4) never needed it. The
stage-order table further down carries the same rule with its reason.

Boot 3 of bench session 3 (a drive, a mouse and a drive at position D) was
Finding G's recipe with the repair underneath it, so those three readings were
also the first bench confrontation between that recipe and shipping media.

Never on this machine: DebugView while capturing (except as the free boot below,
which does that with the release flavour on purpose), disabling any USB
controller devnode, or reporting a clause as blocked by the missing Windows 2000
vehicle.

### The free boots

Observations this machine could pick up in minutes or one boot. None blocks
anything, none is a stage, and none has been taken.

| Item | Cost | Where | Owns a task? |
|---|---|---|---|
| Defect B: the published debug flavour of `0.0.0.4` does not load here (`Code 2`, sole import delta `HAL.dll!WRITE_PORT_UCHAR`). Binaries staged at `out/bench-13e-2b/`; decision tree at P6 | one boot | P6 | no task |
| DebugView with the release flavour installed: plug the hub, the audio device and the LS mouse with Capture Kernel on. The debug build's per-line trace is the published attribution for the bugcheck and the release build has none, so this is the discriminator; a survival means an ordinary user is not in danger. It was briefly batch 13-L's task 13-L.1 and was dropped again, because it decides which sink the evidence leaves by rather than whether the diagnostic binary starts. "Dropped" is not evidence in either direction; task 14.0 publishes it as unmeasured | one boot | release-notes "Diagnostics" | no task |
| `usbhub.sys` reverse rename, the last uncontrolled variable in the composite finding | one boot | task 13-E.1's record | nicety; QEMU already supplies that direction |
| The Ethernet counter reading stage E4.4 passed without. `MidTdDeferralsTailedTotal` equal to a nonzero `MidTdDeferralsTotal` is the primary reading, valid only with `MidTdDeferAccountingBroken` at 0 (that flag makes every line in the block unsafe). The `MidTdShortTailsTotal`-against-`MidTdShortRetiresTotal` residual leg is a separate question behind its own two gates, the sticky `MidTdVerdictVoided` 0 and `mid-TD tails still outstanding` 0; a residual taken while the pings are still running can be void, and a voided residual does not discard the partition. `MidTdShortRetiresTotal` alone has been uninterpretable since task 9-0.2. Recipe: DHCP lease, five parallel `ping -n 150 -l 1400`, let them finish, then the `XHCISNAP` dump | one boot | stage E4.4 | no task; the batch that owned the number (13-T) was removed |

Stage L3 at the bottom of this file was the E460 session after this list was
written, and it took none of these. P6's decision tree stays owed on its own:
the flavour split removes the import from every published binary, which makes
the symptom go away without settling whether the cause was the import or the
port, and a repair that makes the question unaskable would be the worse outcome.

Two latent defects are not on this list and must not be chased at the bench:
`XhciRootHubPortEvent` consuming an event without acknowledging it, and
`xhciRhWritePortsc` dropping a refused acknowledgement. Both are real, neither is
this fault, and neither was readable on a machine with no counter channel.

### Reporting back

Every stage ends in a RUN box with fixed fields, one per step id. An empty field
is a step that did not run, and writing that down is honest reporting. The boxes
were filled from the bench notes afterwards, each reading checked against its
stage's pass criteria, and the roadmap's Phase 13 status updated from them.
Nothing was ticked at the bench.

---

## Session record - bench session 1

The machine was reinstalled from scratch on the morning of the session, at the
project owner's decision. That retired three of this sheet's premises at once:
the driver was not installed, NUSB was not installed, and the SE CD copy stage
E5 installs from was gone with the partition. All four items were re-staged
from `tools/` before the session started, and stage E1.0 below is the
deployment that resulted. The reinstall also bought something: the plain-NUSB
baseline stage E2 is read against is documented clean, and the pre-install
check recorded in stage E1.0 proves it.

### What was read, all from verified-clean baselines

| Device | Speed / kind | Root port (D) | Behind powered hub |
|---|---|---|---|
| Logitech mouse `046D:C077` | Low | works | works |
| SanDisk flash `0781:5408` | High | works | works |
| Sound Blaster X4 `041E:3278` | High, composite, IAD | enumerates; no composite node, HID reads Code 10 | not taken |
| C-Media `0D8C:0014` | Full, composite | `USB Composite Device`, Code 2 | `USB Composite Device`, Code 2 |
| Sound Blaster Play! 2 `041E:323D` | Full, composite | does not enumerate: `Unknown Device`, Code 22, no wizard | same |
| Sound Blaster Play! 3 `041E:324D` | Full, composite | does not enumerate: `Unknown Device`, Code 22 | not taken |
| MS Wired Keyboard 600 `045E:0750` | Low, composite, 2 HID interfaces | `USB Composite Device`, Code 2, and it does not type | not taken |

### Finding 1 - task 13-E.1's anchor result, confirmed on real silicon

A real composite USB Audio device reads `USB Composite Device` with Code 2 on
this driver, on a root port and behind a High-Speed hub. That is the reading
stage E2 exists to take; the earlier bare-metal observation was a
multi-interface HID rather than an audio-class composite.

It was taken on a substituted device. The sheet's own composite (row 1, the
Play! 2) cannot enumerate at all on this driver, so the control was taken on the
C-Media `0D8C:0014`, also a Full-Speed UAC 1.0 composite and the
independent-vendor specimen of the six characterised before the trip.

The gap is not about the audio class. The Microsoft Wired Keyboard 600
`045E:0750` (two plain HID interfaces, a different vendor, a different speed and
a different `bcdUSB`) reads `USB Composite Device`, Code 2 on a root port, just
as the C-Media does, and it does not type: with the composite parent refusing to
load, neither HID interface binds. It was plugged after the mechanism was worked
out, so it is a prediction confirmed rather than a case fitted. The statement is
therefore broad: Windows 98 on plain NUSB does not load a composite parent
driver for composite devices, whatever they contain.

The X4 reads differently. It enumerates and the wizard names it, but no `USB
Composite Device` node appears at all and its `USB HID Device` reads Code 10:

| Device | Speed | IAD? | Interfaces | Result |
|---|---|---|---|---|
| C-Media `0D8C:0014` | Full | no | 2 | `USB Composite Device`, Code 2 |
| MS keyboard `045E:0750` | Low | no | 2 | `USB Composite Device`, Code 2 |
| X4 `041E:3278` | High | yes, 2 associations | 7 | no composite node, HID Code 10 |

The two that behave identically are the two without an Interface Association
Descriptor. That is a candidate discriminator, not a conclusion: one specimen
either side of the IAD line, with speed and interface count co-varying with it.

### Finding 1 - RESOLVED at bench session 2. It was a missing file, not a limitation

Task 13-E.1 is answered, and the answer is "fix it". The composite gap is not a
property of Windows 98, of NUSB, or of this driver. It is `usbhub.sys`, Windows
98's own composite parent driver, not being on the machine.

The control was the project owner's ThinkPad X61, which owns no clause in this
phase. It runs Windows 98 SE with the same NUSB 3.3 stack, has no xHCI at all,
and drives both units: the MS keyboard types and appears as four HID devnodes,
and the Play! 2 appears as three HID devnodes plus a working `USB Audio Device`
under Sound, video and game controllers.

The difference was on disk, and stage E1.0's pre-install check had already
recorded it. Windows 98 ships `USB.INF` on every install but only copies the USB
driver files when setup detects a USB controller. An xHCI-only machine looks
empty to Win98 setup:

| | X61 (UHCI/EHCI) | E460 (xHCI only) |
|---|---|---|
| `USB.INF` | present | present |
| `usbhub.sys` | present | absent |
| result | composites bind | devnode named, Code 2 |

That is what Code 2 says: "the NTKERN.VXD device loader(s) for this device could
not load the device driver". Not "no driver matched" (Code 1), but matched, and
the file was not there. The X61's `USB Composite Device` devnode named the file:
Device Manager -> Driver -> Driver File Details reads `usbhub.sys`.

The remedy was one file. `tools\win98se-extracted\usbhub.sys` (35,680 bytes,
`e898b75f2449eb9e5bbcb3fadf7387c9819f1c7d2c6d49bad83d8236f46afc31`, from the
project owner's own SE CD, the same media P4 hash-verified) was copied to
`C:\WINDOWS\SYSTEM32\DRIVERS\`, the devnode removed, cold start. Result: the
keyboard types, the Play! 2 works, and Device Manager on the E460 reads the same
as the X61's. It was safe by construction: `usbhub.sys` imports `HAL.DLL`,
`NTOSKRNL.EXE`, `USBD.SYS` and `WMILIB.SYS`, the identical set `usbhub20.sys`
imports, and that driver already loads here because it enumerates the hubs.

Stage E5 is superseded. It installs the SE CD's whole native USB stack, one-way,
where the variable was one file. Nobody could see that from the 2a VM, where the
entire stack went in at once to satisfy `uhcd.sys`. E5 need not be taken to
close task 13-E.1.

Verified in QEMU by taking the file away from a guest that had it. Batch 9-V
installed the SE CD's USB components in the 2a guest, so composites bind there
and always did. Renaming `C:\WINDOWS\SYSTEM32\DRIVERS\USBHUB.SYS` to `.BAK`
removes it, and QEMU's own `usb-audio` device (which Phase 9 had measured
binding on this guest) then stops at `USB Composite Device`, "The NTKERN.VXD
device loader(s) for this device could not load the device driver. (Code 2.)".
The bench defect reproduced in a VM for the first time. The driver's own side is
clean in the same run: `devices addressed = 1`, `EP0 max packet size corrections
= 1`, and the configuration descriptor read (`declared isochronous endpoints`)
before Windows ran out of anything to bind. Installing the package then puts the
file back and the composite binds:

```
USBHUB20 SYS    50,032   01-16-04    NUSB's 2.0 hub
USBHUB   SYS    35,680   08-23-26    placed by this package's INF
USBHUB   BAK    35,680   04-23-99    the renamed original, untouched
```

`COPYFLG_NO_OVERWRITE` placed ours only because the destination was absent. The
`USB Root Hub` bang the rename caused (the UHCI's hub, not ours) clears at the
same time.

The next thing that breaks is Microsoft's. With the composite bound, the audio
function loads `USBAUDIO.VXD`, which bugchecks: `fatal exception 00 ... in VXD
USBAUDIO(01) + 00002ED4`. That is Phase 9's documented Windows 98 finding,
reproduced four times in batch 9-V including through a UHCI control with this
driver idle-suspended. It is unaffected by anything here. (Finding X later
showed a real device playing on real silicon, so that guest result was a vehicle
artefact.)

One caveat about the install route: Device Manager -> Update Driver on the xHCI
devnode bugchecks 2a, a known property of that guest, so that VM takes
artifacts by file swap. `[DefaultInstall]` (right-click the INF) avoids the
devnode entirely and is the better test anyway, because the gate's `TGT-DEFAULT`
rule guarantees that path carries the same per-target files.

What this owed next, none of it a bench job: ship `usbhub.sys` the way the
package already ships `usbd98.sys` (the same no-overwrite `CopyFiles` mechanism
and the same `legal-provenance.md` treatment); retract the Windows 98 composite
limitation in `docs/using/release-notes.md`; and take stage E6, since a physical
USB Audio device now binds on real xHCI silicon under Windows 98.

The Code 2 readings in stage E2 stand as taken. They were correct observations
of a machine missing a file. What changed is the diagnosis, not the data.

### Finding 2 - two Creative Full-Speed units do not enumerate, and this is new

`041E:323D` and `041E:324D` both produce `Unknown Device` with Code 22 and no
Add New Hardware wizard at all, on a root port and (for the Play! 2) behind a
hub. No devnode is ever classified, so the failure is upstream of driver
matching.

It is not explained by speed, by composite-ness, or by topology, all tested in
the session: Low Speed works on a root port and through a translator, High Speed
works on both, a Full-Speed composite from another vendor (the C-Media)
enumerates on both, and a High-Speed composite (the X4) enumerates. So the
discriminator is a descriptor property these two Creative units share and the
C-Media does not. That comparison was taken the same day on the modern host,
with `scripts\hub-characterise.ps1` extended to report `bMaxPacketSize0`:

| Device | Speed | `bcdUSB` | `bMaxPacketSize0` | On this driver |
|---|---|---|---|---|
| C-Media `0D8C:0014` | Full | 0110 | 8 | enumerates |
| Sound Blaster Play! 3 `041E:324D` | Full | 0200 | 16 | does not enumerate |
| Sound Blaster Play! 2 `041E:323D` | Full | 0200 | 64 | does not enumerate |

The two failing units do not share a value; they share "not 8". A Full-Speed
device is addressed at 8 and then fixed by an Evaluate Context
(`XhciInitialMps0` in `src/xhci_ctx.c`, `WantedMaxPacketSize0` in
`src/xhci_slot.c`); a device already reporting 8 never enters that path, and Low
Speed (always 8) and High Speed (always 64, set up front) never enter it either.
Every device that failed on this driver needs that correction, and every device
that worked does not.

One rival hypothesis survived the same data: `bcdUSB` splits these three just as
well (0200 fails, 0110 works). The experiment that separates them is one device:
`1209:4704` is Full Speed, `bcdUSB` 0200, `bMaxPacketSize0` 8, and the two
hypotheses predict opposite outcomes for it, with `046D:C099` (Full, 0200, 64, a
third vendor) as the control both predict will fail. Finding 2's fix below
settled it without the tie-breaker.

### Finding 2's mechanism - found by a QEMU run that did NOT reproduce it

`scripts\local\qemu-win98-run-13emps0.cmd` boots the 2a guest for this question,
because the E460 has no counter channel and the debug build counts the thing in
dispute (`Mps0Corrections`, `Mps0CorrectionsStale`). The guest ran the debug
build with the devices passed through by `usb-host` using batch 9-V.2's attach
script.

Neither failing device failed here. The Play! 3 (`mps0` 16) was addressed and
the correction ran:

```
devices addressed               = 00000001
EP0 max packet size corrections = 00000001
```

and Windows got far enough to raise the Add New Hardware wizard. The C-Media
control read `devices addressed = 1` with `corrections = 0`, as `mps0`=8
predicts. Both were disowned afterwards by `RH_ClearFeaturePortEnable`, batch
9-V's known passthrough behaviour, so bind outcome is uninformative in this
vehicle and only the enumeration counters are read.

The non-reproduction located the defect, because the trace shows the order of
operations:

```
778:  probe.xfer params+10: 00000000 01000680 00400000   <- 80 06 00 01 | 00 00 40 00
846:  probe.xfer params+10: 00000000 00010500            <- SET_ADDRESS
855:  devices addressed = 00000001
885:  EP0 max packet size corrections = 00000001
```

Line 778 is `GET_DESCRIPTOR(DEVICE)` with `wLength` = 0x0040 = 64 bytes, issued
at address 0, before the address and long before the max-packet correction lands
at 885. At that moment EP0's Max Packet Size is programmed as 8
(`XHCI_EP0_MPS_FULL_INITIAL`, `src/xhci.h`). usbport asks for 64 bytes on an
endpoint declared as 8, and the device answers in packets of its own
`bMaxPacketSize0`:

| Device | `mps0` | Answers in | Endpoint declared | On a conforming xHC |
|---|---|---|---|---|
| C-Media | 8 | 8-byte packets | 8 | fine |
| Play! 3 | 16 | 16-byte packets | 8 | Babble Detected Error |
| Play! 2 | 64 | 64-byte packets | 8 | Babble Detected Error |

A packet larger than the endpoint's declared Max Packet Size is babble. The
transfer dies on the first descriptor read, the descriptor is never obtained,
and no devnode is ever classified, which is `Unknown Device` with no wizard. Low
Speed is always 8 and High Speed always 64 set up front, so only Full Speed can
mismatch, and only when `bMaxPacketSize0` is not 8. QEMU does not reproduce it
because its xHC does not enforce Max Packet Size on IN transfers.

The initial value is the error. The comment at `src/xhci.h` calls 8 "the value
every FS device must accept", which is true of the device and the wrong subject;
what matters is what the controller receives. Declared larger than actual,
packets arrive short, which is harmless; declared smaller, babble, which is
fatal. Spec 4.3's "start at 8" assumes software issues an 8-byte descriptor
request first; usbport asks for 64 and this driver cannot change that. Linux's
xHCI driver sets Full-Speed EP0 to 64 initially for this reason and corrects
downward afterwards, and this driver's correction path already handles a
downward move.

The babble is inferred, not observed: the E460 has no channel that could show a
completion code and QEMU will not produce one, so confirmation had to come from
the fix working.

### Finding 2 - FIXED, and confirmed by remedy at the bench

`XHCI_EP0_MPS_FULL_INITIAL` 8 -> 64 (commit `79f0688`), built as
`out\bench-13e-mps0fix\`, sha256
`d49546616ce3f6261ba7cb9a93e253d7c0fcd33e4bfc7c913bf80d285b4ba54e`. It is
byte-for-byte the same size as release 0.0.0.3 and reports the same version.
Deployed to the E460 by in-place `.sys` overwrite and cold start.

| Device | `mps0` | Before | After |
|---|---|---|---|
| C-Media `0D8C:0014` (control) | 8 | `USB Composite Device`, Code 2 | unchanged, still enumerates |
| Sound Blaster Play! 3 `041E:324D` | 16 | `Unknown Device`, Code 22, no wizard | named by the wizard, then `USB Composite Device`, Code 2 |
| Sound Blaster Play! 2 `041E:323D` | 64 | `Unknown Device`, Code 22, no wizard | named by the wizard, then `USB Composite Device`, Code 2 |
| MS Wired Keyboard 600 `045E:0750` (Low-Speed regression control) | 8, Low Speed | `USB Composite Device`, Code 2 | unchanged |

The wizard naming the device is what makes this decisive. A product string comes
from a string descriptor, which requires working control transfers on EP0. In
bench session 1 not one descriptor byte was read from either unit; in bench
session 2 both are identified by name. The remaining Code 2 is Finding 1, the
separate composite gap.

The Low-Speed path is untouched: `XHCI_EP0_MPS_LOW` is unchanged at 8, so
nothing in this fix can reach `045E:0750`, and it read the same before and after.
The C-Media control is the half that could have gone wrong: the fix moves every
`mps0`=8 Full-Speed device onto a downward correction (64 -> 8) that no such
device had taken on real silicon before, and it enumerated unchanged.

This also refutes the `bcdUSB` hypothesis without the tie-breaker device. The fix
changed EP0's max packet size assumption and nothing about `bcdUSB`; both
failing units declare `bcdUSB` 0200 and now enumerate.

Epistemic status: confirmed by remedy, not directly observed. No babble
completion code was ever seen, because nothing on this vehicle can show one.
The release notes say it that way.

Idle suspend fired on every first plug (defect 11-V.6): nothing appeared until a
Device Manager Refresh, on all three devices. Consistent with the known defect.

### Finding 3 - two unexplained episodes, trigger unknown

Twice in the session, devices that had been working stopped enumerating, and
only a cold boot restored them. Two hypotheses were raised and both refuted: it
is not that a failed enumeration wedges the controller (the Play! 2 later failed
with the hub's children still working), and it is not an address-0 claim leaked
by a failed enumeration (the C-Media enumerated normally with a failed Play! 2
still plugged in). One of the two episodes had a mundane candidate that was
never eliminated: the hub in play was the bus-powered `1A40:0101` carrying three
children. Recorded as observed twice, trigger unknown.

The operational rule it produced: re-verify a known-good device after every
step, because a reading taken after an unnoticed episode is void. Several
readings in this session were discarded for that reason.

### Two socket maps that do not exist, and were needed twice

`H1`-`H4` are defined by hub port number (`build-and-test.md`, "Fixed hub-port
assignment"), but the physical socket-to-port mapping was recorded only for
`05E3:0608` and half-recorded for `1A40:0201`; for `1A40:0101` it was not
recorded at all. The operator asked which socket was `H3`, then which were
`H1`/`H2`, and neither question could be answered from the documents. The
session used counting from the cable end as a stand-in. That is sufficient for
a reading that only asks whether anything works behind a hub, and insufficient
for stage E3, whose readings are per-child and must be comparable across the
hub swap.

Settled at bench session 2. All three maps are taken and
`docs/contributing/build-and-test.md`, "A hub socket is not a hub port", holds
the three tables. On all three units the numbering runs backwards from the cable
end, so `H1` is the socket furthest from the cable. The same physical loading
order therefore gives the same hub-port assignment on both position-T hubs,
which is what makes task 13-E.2's swap comparable. `05E3:0608`'s walk also
confirmed the tier split batch 13-H recorded, by a different method.

`hub-characterise.ps1 -Walk` was added for this: it watches every port and
numbers each arrival, so walking one device down a hub in physical order yields
the map in one pass. Its first three real walks found two defects in it (it
counted the `** DeviceEnumerating **` transient as a socket of its own, and it
counted a failed IOCTL as an arrival); both are fixed, and the summary now prints
a coverage check requiring every watched port to appear exactly once. The
sockets are still unlabelled plastic; label all three hubs with the logical
numbers before the bag travels.

Stages E3, E4, E5 and E6 were not started in this session. The audio clauses
changed shape: E6's clauses 1 and 2 could not use the Play! 2.

---

## Session record - the DebugView ban, measured

One question was asked and answered: the DebugView ban is general, and it is now
a measurement rather than an inference. Two more things fell out of the attempt:
the published debug flavour of 0.0.0.4 does not load on this machine at all, and
the instrument has a limit that would have made the channel useless here even if
it had been safe. No stage clause was taken.

### Why the check was owed

The DebugView ban (see "If something goes wrong") rested on two observations, a
hub plug (`0028:C208D79D`) and one USB audio device on a root port
(`0028:C207B26D`), from which the sheet recorded "the working assumption is that
any device plug will do it." `build-and-test.md` was explicit that this was not
established. Nobody had tried an ordinary mouse or flash drive.

### The correction the check needed before it could mean anything

One boot with the machine as it stood would have tested nothing. `src/xhci_dbg.c`
is entirely inside `#if DBG`, so the standard build makes no live `DbgPrint`
calls, and the hypothesised mechanism (`DbgPrint` from DPC and ISR contexts
meeting the Win9x VxD hook) is never entered by that binary. Both prior
positives were taken on a debug build. So the probe requires the debug flavour,
which makes it a driver swap rather than a boot.

### The swap route, which costs no crash

Both flavours ship a byte-identical `xhci98.inf`, and the three Microsoft files
were already on the machine, so only `xhci98.sys` changes. It was swapped by
file rename from an MS-DOS Prompt, never by INF:

```
ren xhci98.sys xhci98.sav
copy <flavour> xhci98.sys
```

`src/xhci98.inf` and `build-and-test.md` both record that on Windows 98 every
update-over-an-existing-install bugchecks at `0028:C00312EE`, because stopping
the running driver is that fault. The rename leaves the devnode and its driver
key untouched; `NTMPDriver=xhci98.sys` loads whatever binary sits at that name.

### Finding A - the published debug flavour does not load on this machine

The `0.0.0.4` debug binary (155,227 B, `a279144...`) installed by
rename and cold started gives the controller devnode a yellow bang and `Code 2`:
no crash, no trace, nothing loaded.

Diagnosed host-side by import diff. `DUMPBIN /IMPORTS` on both 0.0.0.4 binaries
shows the debug build differs from the standard build by exactly one symbol:

```
HAL.dll!WRITE_PORT_UCHAR
```

Everything else is identical: eight from `ntoskrnl.exe`, four from `HAL.dll`,
two from `USBPORT.SYS`. That row is also the weakest in
`scripts/import-gate/xhci98-imports.allow`: the only `debug`-only entry, marked
`optional`, evidence `w2k-export; ntkern-name` and no `win98-precedent`.

Confirmed by remedy, and by a control taken first. Device Manager was first read
after the swap, so "Code 2 caused by the debug binary" and "Code 2 that was
already there" were indistinguishable until the previous binary was restored and
the bang cleared. A rebuild with `XHCI_EXTRA_DEFINES=-DXHCI_DBG_NO_E9` loads
clean, reports 0.0.0.4, and drives a mouse and a flash drive on real silicon.
That rebuild drops the one import from the trace channel; the binary also
differs by the do-not-deploy marker, because any nonempty `XHCI_EXTRA_DEFINES`
defines `XHCI_DIAGNOSTIC_BUILD`, which is the uncontrolled variable P6 builds a
control for.

What is not established is why, and two readings survive:

- the import does not resolve here. That would make `ntkern-name` insufficient
  as an evidence tier: the name is in the SE CD's `ntkern.vxd` and this machine
  was installed from that CD. `ZwCreateFile` and `ZwWriteFile` rest on that same
  tier alone and do resolve here, so the tier would be unreliable rather than
  wrong;
- or port `0xE9` is decoded by something on this chipset and the write faults
  during early init. `src/xhci_dbg.c` asserts "on real hardware nothing decodes
  that port and the byte is discarded", an assumption never measured on metal.

A clean `Code 2` rather than a bugcheck favours the first. It does not settle it;
the discriminating test is P6 and was not taken. This is a defect in a published
release and it owns no task.

### Finding B - the ban is general, and a Low-Speed mouse is what proves it

With the `NO_E9` debug binary loaded and verified against real devices, DebugView
v4.64 was started with Capture Kernel active, maximised, logging to
`C:\XHCI\PROBE.LOG`. Sixty seconds on a quiet bus: no lines, no crash, which is
the expected reading (see Finding C).

Plugging a Low-Speed HID mouse (`046D:C077`, `bInterval`=10, 4-byte interrupt
IN) on a root port raised:

```
fatal exception 0E at 0028:C20A3F4D
```

| Observation | Device | Address |
|---|---|---|
| first | hub, both units | `0028:C208D79D` |
| second | USB audio, root port | `0028:C207B26D` |
| third, this check | LS mouse, root port | `0028:C20A3F4D` |

Three device classes, three crashes, and this one is the cheapest stimulus the
bag can offer. Any device plug does it, on the lowest-rate device available.

It also refines the mechanism without establishing the refinement. The standing
explanation is `DbgPrint` from DPC and ISR contexts at real interrupt rates. A
mouse at `bInterval`=10 has no high steady-state rate, and it crashed at the
plug. So the variable may be the enumeration burst (port change to DPC to a
dense run of trace lines), which is much the same for any device. Consistent
with the quiet 60 s above. Hypothesis, not finding.

### Finding C - the instrument could not have served Finding 3 anyway

An attempt was made to test steady state rather than enumeration: attach the
mouse with DebugView closed, confirm it works, then start the capture and move
it. The machine survived and DebugView showed nothing. That null is not evidence
of safety: attaching first guarantees the one dense burst, enumeration, happens
before anything is listening.

The useful result is that this build has no steady-state trace at all. Every
per-transfer site is `XHCI_DBG_VALUE_CHANGED` or `XHCI_DBG_VALUE_LIMITED`, both
capped at `XHCI_DBG_VALUE_LIMIT` = 32 per site and spent within seconds of
driver load; `XHCI_DBG_CB` is capped at 4. The one unbounded macro,
`XHCI_DBG_VALUE`, is documented in `src/xhci_dbg.h` as correct only for a site
that runs "once per start or once per DriverEntry". The budgets are driver-image
statics that no start, stop or resume resets.

So even a perfectly safe DebugView could not have captured Finding 3. An audio
device that enumerates cleanly and stalls three seconds later emits nothing to
capture. And on Windows 98 metal you cannot start a capture before the driver's
only dense trace has already happened: the driver loads at boot, DebugView can
only be started after the desktop. Task 12.2 retired `XhciLogDebugView` because
Windows closes the capture before it stops the driver, the same ordering problem
from the other end of the driver's life.

### What this means for task 12.2 and for Finding 3

Task 12.2's decision stands and is strengthened. It closed on "there is none";
that was an inference from two observations and is now a measurement from three,
with an instrument-design reason on top of the safety reason. It does not
reopen. Finding 3 gains nothing here: the second positive was itself a USB audio
device on a root port, the same class and topology as the Play! 2, so even a
passing result would not have reached it.

### RUN box

```
L0 swap route:  by rename, not by INF   xhci98.sav preserved y
                (INFs byte-identical between flavours; only .sys changes)
L1 debug 0.0.0.4 as published:  Code 2, yellow bang, no crash
                import diff vs standard = HAL.dll!WRITE_PORT_UCHAR, sole delta
L2 control:     previous binary restored -> bang clears  => debug implicated
L3 NO_E9 build: XHCI_EXTRA_DEFINES=-DXHCI_DBG_NO_E9, gates PASSED
                sha256 4d944fa16daa4e355560bee2833a89c3cc3c4a8d5312634105566dc4a21b409c
                loads clean, Driver File Details 0.0.0.4
                mouse moves y   flash drive letter D: y
L4 capture up:  DebugView v4.64, Capture Kernel y, maximised y, log-to-file y
                idle 60 s: 0 lines, no crash   (expected - budgets spent at boot)
L5 LS mouse:    046D:C077, root port
                CRASH  fatal exception 0E at 0028:C20A3F4D
                photographed y
L6 steady state: attach-then-capture, mouse moved 60 s
                survived, 0 lines -> UNREADABLE, not a pass (see Finding C)
L7 restore:     0.0.0.4 standard by rename, Driver File Details 0.0.0.4 y
                flash drive letter y   xhci98.sav deleted y
```

Machine as left: `xhci98.sys` is the 0.0.0.4 standard release, verified by
Driver File Details rather than by size (the 0.0.0.3 bench binary is the same
81,899 bytes). `C:\XHCI\` holds the probe kit (`XHCIDBG.SYS`, `XHCIDBG2.SYS`,
`XHCISTD.SYS`, `DBGVIEW.EXE`) and `PROBE.LOG`, which contains nothing. Host side,
the kit is `out/bench-13e-dbgprobe/` with `MANIFEST.TXT`.

The cost estimate was "one boot". It cost a driver swap, a rebuild, six cold
starts and one bugcheck, because the binary that could answer the question was
not the binary on the machine, and the binary that should have been was broken.

---

## Session record - bench session 2: E3 passed, the composite gap closed itself, and a hot-replug hung the machine

### Finding D - the composite binds now, and stage E2's RUN box is superseded

Stage E2 recorded `USB Composite Device` Code 2 on the C-Media `0D8C:0014`, at a
root port and behind a hub, in bench session 1. In bench session 2, at H3 behind
both hubs of stage E3, the same unit bound with no bang and its USB Audio
function enumerated beneath it.

The cause is `usbhub.sys`, copied onto this machine by hand in this session,
after stage E2 was taken (see "Finding 1 - RESOLVED"). This is the first
bare-metal confirmation of the `usbhub.sys` fix, which until then was verified
only in QEMU and only by removing the file from a guest that had it.

One variable is uncontrolled. Two things changed between stage E2 and this
reading: `usbhub.sys` arrived, and the driver went from the `0.0.0.3` bench
binary to the `0.0.0.4` release. Both are 81,899 bytes and their SHA-256s
differ; that the difference is only version resources is expected but was not
proven. The discriminating test is to rename `usbhub.sys` away and see Code 2
return, one boot, listed under "The free boots".

Downstream: task 13-E.3 clauses 1 and 2 were gated on the composite binding "in
stage E2 or E5", and it bound before stage E5. So the audio clauses are
reachable on the current baseline, and since E5 is one-way they must be taken
before it. That reordered this sheet.

### Finding E - a composite hot-replug hung the machine, with no audio and no load

The C-Media composite was unplugged from H3 (behind the hub tree of stage E3)
and replugged at position D. The machine hung hard: frozen desktop, no error
message and no bugcheck, `Ctrl+Alt+Del` dead. The mouse cursor still moved,
which on a hang means the mouse ISR still runs and is not evidence that USB is
alive.

Finding 3 has the same shape (USB unresponsive, recovery only by cold boot), but
Finding 3 had only ever been provoked by isochronous audio playing, and it hung
the whole machine only under CPU load. Here there was no audio and no load, only
a removal from a hub port and an insertion on a root port. It was not
established that this is Finding 3 rather than a separate defect. The
discriminating split is cheap: cold boot, restore the rig, remove the composite
from H3 and stop; if that hangs it is a removal fault on a hub port, if it
survives plug it into D. Evidence is a photograph and a written note: there was
no bugcheck, so there is no address to compare against the three DebugView
crashes.

### Finding F - Finding 3's defining signature, reproduced with NO audio and NO load

After the cold boot that recovered Finding E, the three constituent events were
each taken separately and all three passed: inserting the composite at H3,
removing it from H3, and inserting it at D. No single topology event reproduces
anything. The wedge arrived only after those were repeated: moving the unit D
-> H3, where it failed to enumerate, then a reseat and a known-good flash drive
at H3.

The end state, measured before the cold boot:

- No device enumerates anywhere. A known-good flash drive in the spare
  right-hand root socket, bypassing the hub entirely, does not appear. So this
  is the controller, not the hub tree.
- Existing hub devnodes persist: `Generic USB Hub` and `USB 2.0 Root Hub` both
  still listed.
- Leaf devices vanished on their own: the H2 flash drive was already gone from
  Device Manager before anyone unplugged it.
- Port-change events have stopped arriving. The hub was physically unplugged
  from position T and after a Refresh `Generic USB Hub` stayed in the tree. That
  is Finding 3's defining reading, taken on purpose.
- The machine stays responsive throughout: Device Manager repaints, internal
  keyboard and trackpad work. Recovery is a cold boot.

Finding 3 had been written up as needing isochronous audio playing, with a UP
Windows 98 interrupt livelock as the named-but-unestablished mechanism. Nothing
isochronous was streaming here and the CPU was idle, so the event-rate argument
cannot be the whole mechanism; "needs audio" was a property of how the fault had
been reached twice, not of the fault. What replaces it as the common factor is
repeated topology change, a cheaper and far more controllable stimulus than
playback.

One hypothesis this may reopen: "a failed enumeration wedges the controller" was
tested and refuted in bench session 1. Here the composite failed to enumerate at
H3 shortly before the machine-wide wedge. That is an ordering, not a causal
chain, but it is new evidence of the kind that clause asked for.

Two symptoms differ from bench session 1's description: there, a removed device
still showed; here, present devices vanished on their own first and the
surviving devnodes were the hubs. Whether that is the same fault caught at a
different stage, or two faults, was not established.

---

## Before the trip - what cannot be done at the bench

The pre-trip items, P1 to P5, are properties Windows' PnP layer will not answer
once you are standing in front of the machine. P6 onward were added during the
investigation as desk work between sessions. The requirements table in
`test-equipment.md` ("Requirements the Phase 13 clauses set, and how each was
met") is the authority for the bag.

P1 and P2 are roadmap batch 13-H's work and task 13-H.1 owns them; they are
restated here because this sheet is what someone reads before travelling.
Everything below assembles the rig of `docs/contributing/build-and-test.md`,
"The bench rig - two connectors, one tree, and nothing moves": positions D (the
mapped left-hand connector) and T (the hub, which never moves), and the fixed
hub ports H1-H4.

### P1. Characterise the hubs, and the mouse, in one run

```
powershell -ExecutionPolicy Bypass -File scripts\hub-characterise.ps1
```

Take the socket maps in the same sitting, one hub at a time, before the rig is
assembled:

```
powershell -ExecutionPolicy Bypass -File scripts\hub-characterise.ps1 -Walk -Hub 1A40:0201
```

All three of `1A40:0201`, `1A40:0101` and `05E3:0608` need one, and stage E3
does not run until they exist. See `build-and-test.md`, "A hub socket is not a
hub port, and the two must be mapped".

Run it on the modern Windows host with the rig assembled: both candidate hubs,
the children in their assigned hub ports, the HID at H1. A child's negotiated
speed is a property of the tree it sits in rather than of the device alone. The
reading: `bDeviceProtocol` 2 with a second interface alternate setting (alt 1,
protocol 2) is the multi-TT hub; 1 is single-TT; 0 is a Full-Speed 1.1 hub. The
mouse's line carries its negotiated speed, which is row 7's answer.

`TTT` does not discriminate. Batch 13-H read six specimens across four models:
`TTT=3` on single-TT hubs, `TTT=3` on multi-TT hubs, and `TTT=0` on the multi-TT
`1A40:0201`.

Both hubs are held and characterised (batch 13-H), so this item was a
re-verification: `1A40:0201` is the multi-TT hub and the standing occupant of
position T; `1A40:0101` is the single-TT swap partner (4 ports on one chip, same
vendor, adjacent product ID, so the swap changes only the TT class); both are
plain USB 2.0 units. `05E3:0608` travels because rig position H4 is its. Two
properties have to be carried to the bench: `1A40:0201`'s socket numbering runs
opposite to its physical order, and `05E3:0608` is one enclosure with two
cascaded chips, so three of its sockets are a tier above the other four. Put
H4's downstream child on a socket whose tier you have read.

### P2. Read the audio device's descriptors

Done by batch 13-H on all six audio specimens; `docs/contributing/test-equipment.md`
is the authority for every value. `scripts\hub-characterise.ps1` reads endpoint
and interface-association descriptors since that batch.

- The streaming endpoint's `bInterval`, which must be > 1 for clause 3 of task
  13-E.3. Every UAC 1.0 unit held reads 1 (five specimens, four of one Creative
  lineage plus an independent C-Media). The one device reading more is the UAC
  2.0 Sound Blaster X4 (`041E:3278`, isoch `bInterval` 1, 3 and 4). Both
  targets ship UAC 1.0 drivers and the X4 has no UAC 1.0 fallback
  configuration, so clause 3 travels as "carry the X4 and find out whether it
  binds" (device-table row 6).
- Whether the device is IAD-grouped, recorded so that the QEMU passthrough rung
  is not confused with this one. The five UAC 1.0 units are not; the X4 is.

The composite device (`041E:323D`) takes clauses 1 and 2; only the X4 can take
clause 3.

### P3. Take the Full-Speed hub decision

The pre-trip inventory found no Full-Speed (USB 1.1) hub. Decision taken before
the trip: publish, no purchase. Task 13-E.2's FS-hub-behind-a-multi-TT-HS-hub
clause is published as a limitation naming the `test\test_init.c` host vectors
and the batch 7b-V0 QEMU measurement as its only evidence, and stage E3 does not
run that case. If a candidate appears later, P1 must first confirm that it is
genuinely Full Speed.

### P4. Confirm the Windows 98 SE CD

Device-table row 2. Closed at bench session 1, both halves, by hash rather than
by eye. The media's own `layout.inf` (out of `PRECOPY1.CAB`) lists the complete
USB stack:

```text
usb.inf=2,,24922        openhci.sys=5,,23632    uhcd.sys=5,,30448
usbd.sys=5,,18912       usbhub.sys=5,,35680     ntkern.vxd=54,,195238
```

and the two files that can be checked against a recorded identity were extracted
from `BASE5.CAB` and matched exactly:

| File | Size | SHA-256 | Against |
|---|---|---|---|
| `usbd.sys` | 18,912 | `0118DB14...F56A` | `scripts\package\usbd-sources.expected`, the Win98 SE 4.10.2222 row |
| `usbhub.sys` | 35,680 | `E898B75F...FC31` | `tools\win98se-extracted\`, the import gate's precedent binary |

It is Second Edition and its USB components are on it, and the identical hashes
also prove this is the same media that produced the `usbd98.sys` the package
carries. The disc's contents were copied onto the E460's own disk, so stage E5
would install from hard disk rather than from the drive.

Verify a different disc the same way: extract `win98\PRECOPY1.CAB` for
`layout.inf`, then `usbd.sys` out of the cab `layout.inf` names, and compare
the hash. Do not verify by disc label or by file date.

### P5. Build, package, hash, and pre-stage

Run at bench session 1, at version `0.0.0.3`; the kit is staged at
`out\bench-13e\` and its `MANIFEST.txt` is the hash list. The tree was commit
`0c3565f` plus the version bump the cut needed. The readings are in the pre-trip
report below.

```
scripts\build-driver.cmd
powershell -ExecutionPolicy Bypass -File scripts\package\make-package.ps1 -Flavor release
```

Carry the release flavour. Nothing on this machine can read a debug build's
output: the only sink `xhciDbgEmit` has on metal is `DbgPrint`, and the only
thing that captures `DbgPrint` here is DebugView, which bugchecks this machine
on a device plug. Record the flavour and build stamp with every result anyway.

Pre-stage generously: this machine has no Windows-side USB until this driver
runs, so files arrive by pulled disk or CD. Carry the package, the previously
known-good package, `xhciqual` (`XHCIQUAL.EXE`, `XHCIQUAL.MAP` and the five
batch files), and the SHA-256 of every binary that travels.

### P6. Build the two binaries that settle defect 2b

Desk work on the modern host. Both binaries are built, gated, and staged at
`out\bench-13e-2b\`. `out\` is git-ignored, so the recipe is here and the
binaries are rebuildable from it. The boot has not been taken.

The question. The `0.0.0.4` debug binary gives the controller devnode
Code 2 on this machine and loads nothing. The sole import delta against the
standard flavour is `HAL.dll!WRITE_PORT_UCHAR`, and a rebuild with
`XHCI_DBG_NO_E9`, which drops that pair, loads clean. Two readings survive that:
either the import does not resolve on Win98 metal, which would make the
`ntkern-name` evidence tier unreliable, or port `0xE9` is decoded on this
chipset and the write faults during init, which would refute `src/xhci_dbg.c`'s
never-measured comment that on real hardware nothing decodes that port.

The separation. `-DXHCI_DBG_E9_NOEXEC` keeps the import and never executes the
write, by putting the call behind a file-scope `volatile` gate that nothing ever
sets. Its import table is byte-identical to the published debug flavour's (18
symbols, `WRITE_PORT_UCHAR` among them, verified with DUMPBIN), so the only
thing that differs at runtime is whether the write happens.

```
set XHCI_EXTRA_DEFINES=-DXHCI_DBG_E9_NOEXEC
scripts\build-driver.cmd debug
set XHCI_EXTRA_DEFINES=-DXHCI_DIAG_CONTROL_UNUSED
scripts\build-driver.cmd debug
set XHCI_EXTRA_DEFINES=
scripts\build-driver.cmd both
```

| Binary | Imports `WRITE_PORT_UCHAR` | Executes the write | Size | SHA-256 (first 16) |
|---|---|---|---|---|
| the `0.0.0.4` debug binary (the defect) | yes | yes | 155,227 | `a279144330220da3` |
| `xhci98-diagcontrol.sys` (the control) | yes | yes | 155,291 | `82f4410a112fb894` |
| `xhci98-e9noexec.sys` (the test) | yes | no | 155,323 | `869d9b48478aebbb` |

Why the control exists. Any nonempty `XHCI_EXTRA_DEFINES` makes `src/sources`
define `XHCI_DIAGNOSTIC_BUILD`, which embeds the do-not-deploy marker, so a
diagnostic build differs from the published one by that marker as well as by
the define being tested. The marker is a string constant and a volatile read in
`DriverEntry` (`src/xhci_dispatch.c`) and cannot plausibly cause or cure a load
failure, but it was uncontrolled in the DebugView-check reading too, where the
`XHCI_DBG_NO_E9` binary that loaded was also a diagnostic build.
`xhci98-diagcontrol.sys` is the published debug flavour plus the marker and
nothing else; it should reproduce Code 2.

Boot order, test first:

1. `xhci98-e9noexec.sys`.
   - Code 2: the import is the problem. Decisive on its own, and the control is
     not needed. Consequence: the `ntkern-name` evidence tier is unreliable on
     this metal, and `ZwCreateFile`/`ZwWriteFile`, which rest on it alone and
     do resolve, need re-reading.
   - Loads: the port write is the problem, pending the control, because both
     binaries that have ever loaded here are diagnostic builds.
2. `xhci98-diagcontrol.sys`, only if the test loaded.
   - Code 2: the marker is exonerated and port `0xE9` is decoded on this
     chipset. That refutes `src/xhci_dbg.c`'s comment and makes the published
     debug flavour unusable on this class of machine by mechanism.
   - Loads: neither reading holds and something about the published binary is
     at fault that no rebuild reproduces. Stop and record it.

All three report version `0.0.0.4`, so Driver File Details cannot tell them
apart. The file size is the discriminator (155,227 / 155,291 / 155,323), and in
the failing branch it is the only one available, because a driver that does
not load has no Driver File Details to read. Check the size after every `copy`
and before every boot. Swap by `ren` + `copy` from a DOS box, never by INF, and
read Device Manager before the swap as well as after.

### P7. The wedge candidates - built for the next E460 session

Finding 3's mechanism, as understood after Finding M, had two independent halves
and the published one-line fix addressed only one of them. These four binaries
are a 2x2 over both halves plus its null cell. All four are the release flavour:
the published debug flavour does not load on this machine (defect 2b), and the
test is behavioural.

| binary | define | what it changes |
|---|---|---|
| `W0CTL.SYS` | `-DXHCI_WEDGE_CONTROL` | nothing. The control |
| `W1GATE.SYS` | `-DXHCI_FIX_QUIESCE_GATE` | `xhciDevDisableCompleted` releases the slot only if the completion code is good and no endpoint failed to quiesce |
| `W2RING.SYS` | `-DXHCI_FIX_NO_RING_REUSE` | `XhciPoolAcquire` rotates instead of first-fitting, so a ring freed at teardown is not handed to the next device |
| `W3BOTH.SYS` | both | both |

W0CTL is not optional. Any nonempty `XHCI_EXTRA_DEFINES` also defines
`XHCI_DIAGNOSTIC_BUILD`, which embeds a marker string and a volatile read in
`DriverEntry`. That is believed inert; W0CTL is what makes it measured.

#### What each candidate tests

W1GATE is the published hypothesis's own fix. Spec 4.6.4 p.97 makes "the
endpoints are stopped" a precondition of Disable Slot; `xhciDevTeardown` issues
the command anyway when the stop could not be shown to have worked, counting
`TeardownsWithoutStop`; and `xhciDevDisableCompleted` then consults only the
completion code. W1 makes it consult both, so a slot whose endpoints were never
shown stopped is abandoned rather than released.

W1 is a conditional no-op, so it is not tested alone. If the Stop
Endpoint never fails on this silicon, `xhciDevHasFailedQuiesce` is false, the
gate never fires, and the binary behaves identically to the control. A "still
wedges" reading from W1 alone cannot distinguish "the fix is insufficient" from
"link 2 of the hypothesis is false". Conversely, W1 behaving differently from
W0CTL is a behavioural reading of `teardowns without a stop` being nonzero,
the one unproven link in the mechanism. That is one-way: a difference proves
the counter is nonzero, no difference proves nothing.

W2RING is a diagnostic, not a proposed fix. Wrapping still reuses, so it narrows
the window rather than closing it: thirty-two rings against a five-plug recipe
taking two or three apiece never wraps. What it buys is a clean answer to "is
immediate ring reuse the mechanism at all".

#### The order to run them in

Minimum two boots, maximum five.

```
0. BASELINE, no swap        the machine already runs 0.0.0.4 standard.
                            Re-confirm the five-plug recipe WEDGES TODAY.
                            *** If it does not reproduce, STOP. ***
                            Every reading after this is unreadable without it.
1. W3BOTH                   wedges  -> the set is EXHAUSTED. Stop. Both halves
                                       are innocent, which redirects the search
                                       away from T7.4's chain. A large result,
                                       and a cheap one - two boots
                            clean   -> go to 2
2. W1GATE                   clean   -> the published hypothesis is right and the
                                       fix is one expression. Go to 4
                            wedges  -> go to 3
3. W2RING                   clean   -> immediate ring reuse is the mechanism and
                                       the quiesce gate is not what matters
                            wedges  -> neither half alone but both together: an
                                       interaction, and the most interesting
                                       outcome in the set. Go to 4
4. W0CTL                    ONLY on a branch where something came back clean.
                            clean   -> *** EVERY READING THAT DAY IS VOID ***
                                       the diagnostic build itself changed
                                       something, not the fix
                            wedges  -> the control holds and the clean readings
                                       above stand
```

The stimulus is the five-plug recipe, unchanged (Finding G), at position D,
from a cold boot, nothing else attached:

```
1. High-Speed flash drive -> D                 enumerates    unplug
2. Low-Speed mouse OR Full-Speed device -> D   enumerates    unplug
3. High-Speed flash drive -> D            *** does it enumerate? ***
```

Cold boot between candidates, and re-verify a known-good device after every
step: a flash drive left in a spare socket is this sheet's live wedge detector.

Two operational cautions, both consequences of a fix working. W1GATE and W3BOTH
leak a device record per failed-quiesce teardown, and there are only
`XHCI_MAX_SLOTS` = 32; a five-plug recipe costs two or three, so do not run
dozens of plugs on these without a cold boot. W2RING and W3BOTH add a field to
`XHCI_RING_POOL`, so their `MiniPortExtensionSize` does not match the tracked
offset table: never read counters from those two, and never put them on a QEMU
vehicle expecting a matrix run.

#### Deploying them

By `ren` + `copy` from an MS-DOS Prompt, never by INF:

```
ren xhci98.sys xhci98.sav
copy <candidate> xhci98.sys
```

All four report version `0.0.0.4`, so Driver File Details cannot tell them
apart, and `dir` separates only three of the four: the linker pads to section
alignment, so `W0CTL.SYS` and `W1GATE.SYS` are both 81,963 bytes with different
hashes, while W2RING and W3BOTH land 32 and 64 bytes higher. Identify the
installed binary with `fc /b`, which ships with Windows 98. Stage all four on
the machine under their own names in `C:\XHCI\`, then:

```
fc /b C:\WINDOWS\SYSTEM32\DRIVERS\xhci98.sys C:\XHCI\W1GATE.SYS
```

`no differences encountered` is a positive identification. Run it after every
swap and before every recipe, and record the answer beside the reading. This
rule outlived the experiment.

All four carry `XHCI98_PROBE_BUILD_DO_NOT_DEPLOY` and are refused by
`make-package.ps1`.

#### The binaries

Staged `out\bench-13e-wedge\`. All four went through `scripts\build-driver.cmd
release`, so all four are import-gated and INF-gated. Built by
`scripts\local\build-wedge-candidates.cmd`, which is git-ignored like the rest
of `scripts\local\`; the defines it passes are recorded above.

| binary | bytes | sha256 |
|---|---|---|
| `W0CTL.SYS` | 81,963 | `3956e1f9cfd548182eeed554c8f9041c1f3de0105ca1b036a4e1e0c659d8db7b` |
| `W1GATE.SYS` | 81,963 | `0dcea5d7bab27838fcca85d826531c7e4b4d2b7c6446c2ea0d589d816eb6fea3` |
| `W2RING.SYS` | 81,995 | `e4adf2ef28ca7242a5486c7b61a22db0288760a6c100d246204eedc9b2b748c4` |
| `W3BOTH.SYS` | 82,027 | `66a77550c218c22a9f99e485d864caaa20140013d2b096df7c14f73c82853812` |

The shipping flavours are unchanged, and that is measured. Every edit these four
rest on is inside `#ifdef`, so a plain `build-driver.cmd release` with
`XHCI_EXTRA_DEFINES` empty must reproduce the published binary. It does: 81,899
bytes, the same size as the `0.0.0.4` release binary, differing in 14
bytes that are all build artefacts (the COFF `TimeDateStamp` at PE+8, the
optional header's `CheckSum` at opt+64, three `IMAGE_DEBUG_DIRECTORY`
`TimeDateStamp`s 28 bytes apart, one byte of `IMAGE_DEBUG_MISC`'s `Reserved[3]`
padding, and the CodeView `NB10` signature's stamp). No executable code, data,
resource or relocation byte differs. Re-run that comparison if the defines are
ever extended.

The baseline already on the machine, the `0.0.0.4` release binary, is
81,899 bytes, sha256 `aca4d4cc...`. It is 64 bytes smaller than `W0CTL.SYS` and
the difference is the `XHCI98_PROBE_BUILD_DO_NOT_DEPLOY` marker.

#### Round two: `W7GATE`, a recovery candidate

Added after the first four were exhausted in two boots. `W7GATE.SYS`, 82,107
bytes, sha256
`7d00d8f37eb182c67fd32cc7810f7d162792d0da9f2eec5dafac9c7611404531`,
`-DXHCI_FIX_RH_GATE`.

The first four were prevention candidates, so their negatives were quiet. This
one is a recovery candidate: a success is the machine coming back by itself,
about ten seconds after the failed plug, with no cold boot.

What it does: a watchdog on the health poll. If an announcement is owed while
the root-hub notification gate is shut for 20 consecutive polls (~10 s at
usbport's nominal 500 ms), force the gate open and re-drive the announcement.
`xhciRhAnnounce` (`src\xhci_rh.c`) calls `UsbPortInvalidateRootHub` only while
`XHCI_EXT_FLAG_RH_IRQ` is set, and counts `RootHubInvalidatesGated` when it is
not. That file's own comment states the hazard: usbport closes the gate from
`USBPORT_InvalidateRootHub`, and the single enable site is the status-change
scan's no-changes exit, so "a close is not guaranteed a matching open". The
case it does not cover is a change latched after that scan has finished.

It is a mitigation, not a repair, and the causal link from "a periodic device
was unplugged" to "the gate stuck" was not established. `RhIrqGateCloses`
against `RhIrqGateOpens`, with `RootHubInvalidatesGated` beside them, would
settle it in one counter reading.

#### Round three: `W10ALL`

`W10ALL.SYS`, 82,251 bytes, sha256
`7183a95b0b8e0719a6c802c8d83f1e2fe845ec1538238e69e73119a9dce2ee71`,
`-DXHCI_FIX_PORT_POLL -DXHCI_FIX_EVT_REARM -DXHCI_FIX_RH_GATE`. `W9BOTH.SYS`
(82,139, `191656f0...`) is a strict subset of it and was not given a boot of its
own.

W7GATE was tested and was inert, and the reason produced this candidate. That
watchdog fires only when a change is owed; if no event ever arrives, nothing is
latched, nothing is owed, and it has nothing to force out. So the failure is
upstream of the announcement.

The hazard is this project's own, stated in `src\xhci_rh.c` itself: a PORTSC
change bit that nobody clears suppresses the controller's next Port Status
Change Event for that port (`lessons.md`, "hot-plug operations are not hot-plug
events"). A port whose change bit is stranded stops raising events for good.
Plug a device in and the controller says nothing, the driver never learns of the
arrival, and nothing appears anywhere in Device Manager, not even a failed
devnode, which is the measured signature on both machines. It also
retro-explains both earlier negatives: `W7GATE` was inert because no event meant
nothing owed, and `W3BOTH` was irrelevant because the slot and ring subsystem is
never reached if the arrival is never seen.

What it does: every managed port, every health poll, through `xhciRhRefresh`,
the one PORTSC reader here that folds what it reads into the shadow and
acknowledges the change bits it observes. So the sweep both notices the missed
arrival and unsticks the port. The reading is fast: the drive should appear
within about half a second of being plugged in, and nothing in five seconds
means it has not worked.

| outcome | what it buys |
|---|---|
| appears in ~0.5 s | a port was silenced by a stranded change bit; the sweep noticed the arrival and unstuck the port. `RhPolledLatches` says how often the fallback was needed |
| nothing in 5 s | the whole "usbport was never told" family is exhausted (gate, interrupter and port state alike) and the fault moves downstream into enumeration |

It is a polled fallback, not a repair, and the causal link from "a periodic
device was unplugged" to "a change bit was stranded" was not established.

#### RUN box

```
P7.0 baseline    0.0.0.4 standard, already installed, no swap
                 five-plug recipe at D, cold boot, nothing else attached
                 *** WEDGED *** step 3 flash drive did not enumerate
                 (project owner)
                 => the recipe reproduces TODAY and the positive control holds,
                    so every candidate reading below is readable
P7.1 W3BOTH      *** WEDGED *** (project owner)
                 the wedge arrives between step 2 and step 3, as on the baseline
                 devnode after Refresh: NOT RECORDED - owed, and now the most
                 valuable free observation available
                 => W1GATE and W2RING NOT RUN, and correctly so: both are
                    strict subsets of W3BOTH and cannot succeed where the
                    combination failed. Two boots, set exhausted
P7.2 W1GATE      not run
P7.3 W2RING      not run
P7.4 W0CTL       not run - only on a clean branch, and there was none
P7.5 W7GATE      *** INERT *** waited over a minute, drive never enumerated
                 (project owner)
                 => the failure is UPSTREAM of the announcement: with no event,
                    nothing is ever latched, so nothing is owed and the watchdog
                    has nothing to force out
P7.6 W10ALL      *** WORKS *** (project owner)
                 the first candidate in this investigation to repair anything
                 how fast: ~0.5 s, which is ONE HEALTH POLL INTERVAL - usbport's
                    timer is nominally 500 ms and the sweep runs on that
                    callback, so the recovery lands on the first poll after the
                    plug. That is what a working polled fallback looks like
                 machine after: NORMAL across several mouse-then-flash cycles
                 fc /b: confirmed
                 NOTE "normal" most likely means INVISIBLE rather than ABSENT.
                    Two readings fit and RhPolledLatches separates them, which
                    is a counter and not readable here:
                      climbing per cycle -> the stranding still happens every
                        time and the sweep rescues it inside one poll, a delay
                        too small to notice
                      frozen after the first -> acknowledging the change bit
                        unstuck that port for good, which is the better news
                 => the "usbport was never told" family is the right family:
                    the failure is in getting the ARRIVAL to the stack, not in
                    enumeration and not in the slot or ring machinery
                 => but W10ALL carries THREE defines, so which one repaired it
                    is unknown, and they imply different repairs. RH_GATE is
                    already excluded (P7.5 inert), leaving PORT_POLL and
                    EVT_REARM. Bisect with P7.7 and P7.8
P7.7 W11POLL     *** WORKS *** (project owner)
                 -DXHCI_FIX_PORT_POLL alone, 82,107 B, 120dff7099885 8ed
                 => THE POLLED SWEEP IS THE ACTIVE INGREDIENT. EVT_REARM is not
                    needed and the interrupter is fine; RH_GATE was already
                    excluded. The fault is a STRANDED PORTSC CHANGE BIT
                 (this attribution was too quick - see P7.9 and Finding P)
P7.8 W12EVT      *** FAILS *** (project owner) - wedges
                 -DXHCI_FIX_EVT_REARM alone, 81,995 B, 5301a5e2660c0df6
                 => AND IT IS A DISCRIMINATOR, not just another negative.
                    Re-establishing event delivery does NOT recover the port,
                    while a direct PORTSC read-and-ack (W11POLL) does. That
                    argues AGAINST a machine-wide dead interrupter and toward a
                    PER-PORT stuck condition
                 => the hot-plug-events finding was observed PER-PORT in QEMU
                    and says nothing about the interrupter globally, so
                    "stranded CSC" and "interrupter dead" are different-SCOPED
                    failures that W11POLL alone cannot separate. This is what
                    separates them
P7.9 W13ACK      *** WEDGES *** (project owner)
                 the repair from Finding O alone, no polled sweep
                 81,995 B, da777176dceed2e5
                 => FINDING O'S MECHANISM IS REFUTED. Carrying the refused
                    acknowledgement forward and retrying it changes nothing,
                    which says PortscAckOwed is never set - the ack was never
                    refused, because Port Power was never in flight. That is
                    exactly the hole the independent review flagged and could
                    not close
                 => AND THE "NEVER ATTEMPTED" INFERENCE IS A NON-SEQUITUR,
                    retracted after an independent read. W13ACK adds
                    retry logic only INSIDE xhciRhWritePortsc's refusal branch,
                    so a null result is equally explained by "the write always
                    succeeded when tried" and by "xhciRhRefresh was never invoked
                    for this port at all" - in which case the branch never runs
                    either way and W13ACK is dead code. What it DOES establish is
                    narrower: the Port-Power-refusal path is not involved
                 => AND THE P7.7 ATTRIBUTION WAS TOO QUICK. The polled sweep
                    does TWO independent things: it acknowledges change bits,
                    AND it bypasses event delivery entirely by reading PORTSC on
                    a timer. W11POLL working is equally consistent with "the
                    port was stranded" and "no events are being delivered at
                    all". W12EVT was the control that separates them and it was
                    skipped - a success attributed without excluding the
                    alternative
```

Record the devnode question too, not only wedged y/n. Finding 3's signature is
that port-change events stop arriving: a physically removed device still shows
in Device Manager after a Refresh. A candidate that leaves the enumeration
failing but restores the port-change flow has changed the end state without
fixing the fault, which is a reading about which half it touched rather than a
null result.

---

### P8. The P14s replication check - the control this batch never had

Added at the project owner's request after the first candidate round was
exhausted, and taken before any further candidate: it costs no build, and it
can invalidate the premise every candidate rests on. Every reproduction of
Finding 3 came from one machine, at one connector. The P14s Gen 1 is the second
vehicle, it has Windows 98 SE on it, and it had never been asked.

| outcome | what it means |
|---|---|
| P14s also wedges | the fault is in the driver, reproduced on two different Intel generations. Software, definitively |
| P14s stays clean | the fault is specific to the E460 (its silicon, its install, or its port), and "build a fix" is the wrong frame |

The two machines are mirror images, and the rig maps by controller port rather
than by side:

| | E460 | P14s Gen 1 |
|---|---|---|
| D | left USB-A, this machine's Always On connector | left USB-A, controller port 4, an ordinary port |
| T | right | right USB-A, this machine's Always On connector |

Every E460 reproduction was at its D, which is an Always On port. Testing the
P14s at "position D" therefore varies two things at once, the silicon and the
Always-On-ness, so the recipe was to be run at both connectors on the P14s.
There is no spare socket on that machine (two USB-A against the E460's three),
so the live wedge detector is unavailable; re-verify by plugging instead.

Before a result counts: Driver File Details must report `0.0.0.4` on the xHCI
devnode (not the file size; the two 81,899-byte binaries settled that), and a
flash drive must enumerate at each connector. Install the plain `0.0.0.4`
release binary (81,899 B, sha256
`aca4d4cc...`), never a candidate. A replication check may skip stages E0 and
E1 but the result is then labelled provisional.

#### RUN box

```
*** RESULT: THE P14s GEN 1 REPRODUCES IT. Same issue, same recipe. ***
    => FINDING 3 IS A DRIVER DEFECT, NOT A MACHINE. Two different Intel xHCI
       generations, one binary, same five plugs. Silicon is EXCLUDED, and every
       candidate built against this fault was aimed at something real.
    => a user reaches this by plugging a mouse and then a flash drive into one
       socket, on two of two machines tried.
    CONNECTOR: the LEFT USB-A = controller port 4 = an ORDINARY port.
    => THE ALWAYS ON CONFOUND IS RETIRED TOO. Every E460 reproduction is at that
       machine's Always On connector; this one is not, on a different machine.
       So the fault is neither the silicon nor the port class, and it is
       reachable at ANY root port. The E460's spare right-hand socket does not
       need a boot spent on it.
    THEN THE RIGHT USB-A (this machine's Always On port) WAS TRIED AND IT IS THE
    SAME. So BOTH connectors wedge on the P14s, and the E460's D wedges, and the
    port class is irrelevant on either machine. Three ports, two machines, two
    Intel generations, one binary, one recipe.

P8.0 prerequisites   Driver File Details = 0.0.0.4 y
                     flash drive enumerates at LEFT y   at RIGHT y
P8.1 recipe at LEFT  (ordinary port, controller port 4 = position D)  WEDGED
P8.2 recipe at RIGHT (Always On port = position T)                     WEDGED
P8.3 read as         both wedge -> driver fault, silicon-independent
```

---

### P9. W15SLOW - the latency instrument

Built from a source-level constraint analysis. Staged as
`out\bench-13e-wedge\W15SLOW.SYS`, 82,139 bytes, the same size as W9BOTH, so
`fc /b` is not optional. Built by `scripts\local\build-wedge-w15.cmd`;
host-suite regression 10,830 checks with defines off, 10,843 with them on, 0
failures both ways. `src/xhci_hw.h`, `src/xhci_rh.c` and `test/test_init.c`
name this item as the description of `W15SLOW`, whose
`XHCI_RH_SWEEP_SLOW_POLLS` is still in the driver.

#### Why this and not W14TELL

A proposed W14TELL ("force the announcement with no PORTSC read and no
acknowledgement") could not split its two hypotheses. `RH_GetPortStatus`
performs a live `xhciRhRefresh` (a PORTSC read and a full change-bit
acknowledgement) whenever the controller is admitted (`src\xhci_rh.c:639`), and
a forced `UsbPortInvalidateRootHub` makes usbport walk every port through
`RH_GetPortStatus`, so the announce performs the read-and-ack one layer down as
a side effect. Both rows of W14TELL's interpretation table collapse into
W11POLL.

What needed splitting was one soft observation. The no-event claim rested on
W11POLL's recovery taking "about 0.5 s, one health-poll interval", which no
operator can distinguish from instant against OS enumeration noise. Detection
at poll latency means the sweep found it and no event arrived, and since
W11POLL's sweep had been acknowledging every change bit on every port every
~0.5 s since boot, no stranded change bit can have been suppressing the event:
the hardware has stopped generating port events. Detection instant means an
event announced it, so the sweep's earlier acknowledgement unstuck the port and
the stranded-change-bit mechanism is confirmed.

#### What W15SLOW is

`-DXHCI_FIX_PORT_POLL -DXHCI_FIX_PORT_POLL_SLOW`: W11POLL's sweep unchanged,
behind a divider. It runs on every 16th health poll (~8 s) instead of every
poll (~0.5 s). An event-driven detection stays at ordinary enumeration latency
(a second or two); a sweep-mediated one lands uniformly in 0-8 s, mean ~4 s. A
stopwatch is the whole instrument: any single detection taking >= 3 s is
conclusive on its own, and three repetitions cannot all land under 2 s by phase
luck ((1/4)^3, under 2%). Time from the physical plug to the first visible
reaction; treat 2-3 s as ambiguous and repeat.

The mandatory pause: at the recipe's step 3, wait >= 20 s (two full sweep
periods) between the sub-High-Speed unplug and the High-Speed replug. That
guarantees any change bit the teardown could have stranded has been swept and
acknowledged before the replug, which is what makes a slow step-3 detection
refute the stranded-bit mechanism rather than merely repeat W11POLL. Finding
H's localisation already showed a pause does not prevent the wedge.

#### RUN box

```
P9.0 prerequisites   fc /b C:\WINDOWS\SYSTEM32\DRIVERS\xhci98.sys C:\XHCI\W15SLOW.SYS
                     -> "no differences encountered" y/n     (size collides with
                     W9BOTH - fc /b is the only identification)
                     cold boot after install y/n
P9.1 baseline, pos D HS drive plug/unplug x3 from cold boot, stopwatch each:
                     ___ s   ___ s   ___ s     expect ALL <= 2 s
P9.2 baseline, spare HS drive plug x2:  ___ s   ___ s     expect <= 2 s
     If any baseline row is >= 3 s: STOP, record, do not run the recipe -
     detections are sweep-driven even on a healthy boot, which is its own
     finding and voids the rows below.
P9.3 THE READING     recipe at D: HS -> unplug, mouse -> unplug,
                     *** WAIT >= 20 SECONDS ***, HS -> plug, stopwatch:
                     ___ s      devnode ever appears y/n
P9.4 persistence     same drive, same port, unplug -> replug x3:
                     ___ s   ___ s   ___ s
P9.5 scope           spare socket, HS plug x2:  ___ s   ___ s
P9.6 teardown        restore xhci98.sav (pristine 0.0.0.4, 81,899 B), verify by
                     fc /b, cold boot
```

#### Read as

| P9.3 | P9.4 | meaning |
|---|---|---|
| >= 3 s | >= 3 s every time | hardware stopped generating port events, and stays stopped. The stranded-bit mechanism is refuted; the fault is silicon state provoked by the periodic teardown's command sequence; the sweep is a per-plug crutch |
| >= 3 s | <= 2 s after the first recovery | events are dead until one sweep touches the port, then generation resumes: a transient per-port suppression something clears on the first post-wedge read/ack |
| <= 2 s | (any) | the stranded-change-bit mechanism is confirmed: the sweep's earlier acknowledgement is what un-silenced the port, the replug's event arrived normally, and the strander is in software-reachable territory |
| nothing after 30+ s | - | the sweep itself failed to recover, which W11POLL never showed. Record everything and stop; the two binaries need `fc /b` re-verification before anything else is concluded |

P9.5 reads the same way for scope: slow spare-socket detections mean the state
is machine-wide and persistent; instant means per-port.

Cautions: while the wedge state is in force, every detection W15SLOW mediates
takes seconds, which is the instrument working. W15SLOW adds a field to
`XHCI_EXTENSION`, so its `MiniPortExtensionSize` does not match the tracked
offset table; never read counters from it. It carries
`XHCI98_PROBE_BUILD_DO_NOT_DEPLOY` and is refused by `make-package.ps1`.

#### The run - E460, bench session 2: the fourth row fired

Run by the project owner the same day. `fc /b` positive at P9.0.

- P9.1 / P9.2 baselines: all five plugs under one second. Every baseline
  detection was event-driven; nothing in the run demonstrated the divided sweep
  firing.
- Recipe: mouse plug instant, devnode vanished cleanly on unplug.
- P9.3, with the mandatory >= 20 s pause: nothing. Not at 30 s, not at 3
  minutes, roughly 22 nominal sweep crossings. Machine responsive throughout.

Probes taken in the wedged state, in order:

| probe | result |
|---|---|
| Device Manager Refresh | nothing (weak evidence: a Win98 refresh may never reach the hub port scan at all) |
| DevMgr USB controller + root hub nodes | no yellow bang, no error code, unchanged |
| mouse plugged into wedged position D | sensor LED does not light; not detected |
| drive into the spare socket | nothing (LED not checked there) |
| `HKLM\System\CCS\Services\USB\DisableSelectiveSuspend` | present, = 1 |
| warm restart | not available on this machine at all: a pre-existing ACPI issue, it has never been able to warm-restart |

Finding S later explained the null: W15SLOW's sweep is admitted-gated, so after
the `ControllerFailed` latch it never reads PORTSC at all.

### Finding Q - the wedged port has NO VBUS (REFUTED by Findings R and S)

This finding was refuted by Findings R and S below and is kept because the
reasoning that produced it is instructive. Two of its claims are known false:
the terminal state is not an unpowered bus (`PP = 1`, `CCS = 1`,
`RhPortsUnpowered 0`, `StopController` never ran), and usbport does not stop the
controller after `ResetController` (the batch 13-R census: usbport reaches
`StopController` only on a PnP or power transition). Do not act on the LED
measurement it proposes.

Observed, the first electrical observation of the failed state this
investigation had: in the fully-formed wedge, a mouse plugged into position D
gets no power; the sensor LED that lights on insertion into any healthy port
stays dark. Detection is also dead at the spare socket. The machine is
otherwise responsive, and usbport shows no error of any kind. (Finding R showed
the LED is an enumeration indicator, not a VBus indicator.)

What was inferred from it at the time:

1. W15SLOW behaved correctly: a sweep over electrically-empty ports latches
   nothing.
2. Every recovery candidate's failure was overdetermined: nothing W7GATE,
   W12EVT, W13ACK or the sweep does can restore VBus.
3. The stranded-change-bit mechanism cannot be the terminal state: a stale RW1C
   bit does not remove port power.
4. "Recovered only by a cold boot" was never a depth measurement, because the
   E460 cannot warm-restart at all.
5. The suspend family is dead: `DisableSelectiveSuspend = 1` is present on the
   E460, and baseline events were instant.

The two-stage model it inferred: `W11POLL`/`W10ALL` recovered something in
~0.5 s, and a PORTSC read-and-ack cannot re-power a bus, so either the wedge has
an early, still-powered stage they caught, or their 0.5 s cadence prevented the
terminal stage from forming. The variable separating those runs from this one
was the >= 20 s pause and the sweep cadence. Finding S later named the two
stages: a stranded change bit on a live controller, then the `ControllerFailed`
latch.

The mechanism it proposed, the driver's own `XhciStopController` teardown
reached through the recovery ladder (~5 s command timeout, ~20 s abort-ladder
end, ~32 s age-poll backstop), was wrong; the LED-fuse measurement written to
fingerprint which watchdog fired has nothing to time, because the LED never
lights on the failing plug. What survived is the observation that the driver's
breadcrumbs (`ctrl.failed.here`, `FatalStatusDetected`, `ResetControllerCalls`,
`RhPortsUnpowered`, `PortTeardown*`) were all being recorded in RAM with no
working sink on Win98. What was missing is a way to read, not a way to record.
That is what P10 built.

---

### Finding V - the poll-rate repair is CONFIRMED, the wedge is GONE, and Finding U's ~1 ms is corrected

Read off the E460 at bench session 3, one boot, five recipe cycles at position
D. Five files came home in `run-13e-evidence/`: three extension dumps at 90,600
B (`p13h`, the healthy control; `p13a3`, after cycle 3; `p13a5`, final) plus
`p13p1.txt` / `p13p2.txt`, the probe reads, which are text records rather than
dumps. `Log.BytesDropped` 0 in both ladder dumps, so the ring never wrapped and
the ordering is intact.

#### The recipe passes outright, and nothing escalated

```
CommandsIssued        123  ==  CommandsCompleted  123
CommandAgeAborts        0      CommandAgeResets     0
ResetControllerCalls    0      RecoveryAttempts     0
EndpointQuiesceLost     0      DevicesStalledOut    0
SlotsEnabled           18      DevicesTornDown     15
```

Eighteen devices addressed (three internal plus fifteen recipe plugs), fifteen
torn down, 123 commands and every one completed. No wedge, no latch, no
recovery, no abort. The operator's report was again "almost instant"; Finding U
read identically from the operator's chair through 33 controller resets. There
were none here, and this time the counters are what make the report mean
something.

#### Finding S's headline is refuted, not merely qualified

Finding U downgraded "a Stop Endpoint that never completes" to "it did not
complete inside a window three orders of magnitude smaller than its author
believed". This boot closes it the rest of the way: given its proper window the
Stop Endpoint completes. `EndpointQuiesceLost` 0, every command completed, and
the abort rung, which fired 4-for-4 in Finding T and 33-for-33 in Finding U on
this same machine and recipe, fired 0 times in 123 commands. Phase 14 must not
restate Finding S's headline in any form. What was real was the escalation;
what caused it was this driver aborting commands that were going to finish.

#### The correction to Finding U's number

This is the first dump that could measure the poll period instead of inferring
it, because `PollClockMs` is an elapsed time sitting beside `HealthPolls` in the
same file:

| segment | ms | polls | ms/poll |
|---|---|---|---|
| boot -> `p13h` | 122,135 | 3,377 | 36.2 |
| `p13h` -> `p13a3` | 148,812 | 1,875 | 79.4 |
| `p13a3` -> `p13a5` | 83,417 | 1,209 | 69.0 |
| whole boot | 354,364 | 6,461 | 54.9 |

The clock is independently checked against wall time: 232,229 ms between the
healthy control and the final dump, against file timestamps four minutes apart.
`PollClockStalls` 0 throughout.

The E460's `CheckController` period is 36-80 ms, not ~1 ms. Finding U's figure
came from dividing 971,359 polls by a session nobody timed. It was wrong by one
to two orders of magnitude, and every document that repeated it has been
corrected.

The defect and the repair are unchanged. At 36-80 ms the old counts were:

| | intended | actually, on this machine |
|---|---|---|
| `XHCI_COMMAND_AGE_POLLS` 64 | 32 s | 2.3-5.1 s |
| `XHCI_DEV_AGE_POLLS` 128 | 64 s | 4.6-10.2 s |
| `XHCI_DEV_STALL_POLLS` 10 | 5 s | 0.36-0.8 s |
| `XHCI_PORT_AGE_POLLS` 16 | 8 s | 0.6-1.3 s |
| `XHCI_EP_RESTART_POLLS` 2 | 1 s | 72-160 ms |

The first row is the whole defect. `XHCI_COMMAND_TIMEOUT_MS` is 5,000. A
backstop at 2.3-5.1 s is at or under the watchdog it was sized to sit twelve
seconds behind, so it pre-empted that watchdog on every hung command, which is
why `CommandsTimedOut` read 0 across 76, then 635, then 123 commands on three
separate boots.

Two claims made on the ~1 ms figure are withdrawn. The port age was not "a 16 ms
deadline below TDRSTR's own minimum"; it was 0.6-1.3 s, a margin of 1.2-2.6x
over the 500 ms deadline where 16x was intended, a real narrowing but not a
guaranteed misfire. The endpoint-restart net's margin over usbport's one-frame
abort gate was not "gone"; at 72-160 ms it was about seventyfold, narrowed by
an order of magnitude from the thousandfold intended. Both conversions stand on
the units argument, which never depended on the factor.

#### The one thing left open: Finding U's poll count

971,359 polls is irreconcilable with 36-80 ms: at this boot's rate that many
polls is fifteen hours, and that session was about one. Either the rate under
the pre-repair build was an order of magnitude higher again (plausibly because
33 recovery cycles drive callbacks the healthy path does not), or the figure
means something other than what was assumed. It is recorded as open. What can
be said without guessing is that the poll rate is not a constant, on one
machine or across machines: this boot alone spans 36 to 79 ms, the 2a guest runs
at 465 ms (P13-CLOCK-0), and the Finding U session was faster again. A
threshold counted in polls therefore has a size the host decides and a size
that moves while the driver runs, and the busiest, sickest moments are the ones
with the most polls in them.

#### What this does and does not close

Task 13-R.3.5's stated pass was `CommandsTimedOut` nonzero with
`CommandAgeResets` at 0, and it was not met: both read 0, because no command
hung and no watchdog had anything to time. That clause was written expecting a
fault the repair has removed. What stands in its place is the same claim shown
negatively: `CommandTimeoutArrivals` 123 == `CommandStaleCallbacks` 123 ==
`CommandsIssued` 123 (every command's watchdog armed, arrived, and found its
command already complete) with `CommandAgeAborts` 0. The poll no longer reaches
a command before its own timer does, on the machine where it previously always
did. The project owner closed the task on that reading.

---

### Finding U - the repair is CONFIRMED at scale, and the health poll runs far faster than the thresholds assumed

Superseded in one particular by Finding V. The ~1 ms period below is an
inference (971,359 polls over an untimed session) and the direct measurement is
36-80 ms. Everything else in this finding stands: the counters, the 33
recoveries, and the conclusion that the age detector was pre-empting the
watchdog ladder.

Read off the E460 on the second batch 13-R boot, P11-RETRY. Five reads, all at
`ExtensionBytes = 87644`: `run-13e-evidence/p12h.txt` (healthy) and `p12a3` /
`p12a5` / `p12a10` / `p12a15`. Fifteen recipe cycles at position D.

#### The pass, and it closes task 13-R.3

| | cycle 3 | cycle 5 | cycle 10 | cycle 15 |
|---|---|---|---|---|
| `ResetControllerCalls` | 6 | 10 | 23 | 33 |
| `RecoveryAttempts` | 6 | 10 | 23 | 33 |
| `RecoveryCompletions` | 6 | 10 | 23 | 33 |
| `RecoveryFailures` | 0 | 0 | 0 | 0 |
| `RecoveryFailuresConsecutive` | 0 | 0 | 0 | 0 |
| `ControllerFailed` (final) | | | | 0 |

Thirty-three latches, thirty-three complete recoveries, no failures, fifteen
cycles, no wedge. `RecoveryLastStep 22` (`XHCI_INIT_STEP_DONE`) every time. That
confirms task 13-R.1 at a scale Finding T could not, and it proves the Finding T
cap fix outright: 33 recoveries against a cap of 3 is possible only because the
budget bounds consecutive failures rather than lifetime attempts.

The bench report was "the flash drive and mouse enumerated and worked almost
immediately each time", through 33 controller resets. The whole ladder, abort
through reinitialization, completes faster than a person can notice.

#### The poll count

`HealthPolls` went 17,228 -> 988,587 across the run: 971,359 polls. At the 500
ms period every threshold in this driver is sized against, that is 5.6 days; the
session was well under an hour. As a bound needing no timestamp, the
`CheckController` period on this machine was inferred to be at most ~3.7 ms,
with the observed dump spacing putting it nearer 0.5-1.2 ms. (Withdrawn; see
Finding V.)

The age detector counts polls on the reasoning that "the miniport has no clock
it may read at DISPATCH without a new import, and the error direction of a poll
count is the safe one: usbport's timer is nominally 500 ms, so a slower poll
only makes this fire later, never sooner." The premise is false on this machine
and the error direction is therefore the unsafe one. The comment beside the
threshold says it "clears the whole watchdog ladder by construction ... 20 s of
legitimate lateness against 32 s here"; on this machine it pre-empted the
working ladder every time.

#### Which answers Finding S's and Finding T's open question

`CommandTimeoutArrivals` 633 against `CommandsIssued` 635, and exactly equal at
every interim dump: 125/125, 201/201, 445/445. Every command watchdog arrives;
usbport's timer service is not dropping them. `CommandStaleCallbacks 624` plus
`CommandsAfterFailure 9` is 633: every arrival accounted for, and not one
finding its command live, because the poll reached the command sooner and had
already moved the generation on. `CommandsTimedOut 0` across three boots and
76 + 635 commands has that one cause.

#### What this means for the diagnosis

This driver had been escalating to a controller reset on commands that were
merely slow. A Stop Endpoint on a Low-Speed periodic endpoint may legitimately
take up to a service interval (the ring shows `ep.open.rate=00200004`); the age
detector gave it far less than intended and then aborted it. So Finding S's "a
Stop Endpoint that never completes" was not established; what was established
is that it did not complete within the age detector's window. The 4-for-4 and
33-for-33 abort failures in Findings T and U are consistent with a command that
was going to finish being aborted before it could.

Nobody noticed until task 13-R.1 because before the repair one such escalation
was terminal and produced Finding 3; after it, thirty-three of them are
invisible. A repair that makes a defect survivable also makes it silent, which
is the argument for reading counters rather than watching a port.

#### The fix this pointed at

The objection the poll-count design rests on, no clock at DISPATCH without a new
import, was no longer true. `XhciFrameNumber` is MFINDEX-derived, DISPATCH-safe,
already in the extension, already sampled by this very poll (`XhciFrameSample`),
and costs no import. A threshold expressed in frames is a threshold in
milliseconds, and the sizing is the ladder's own arithmetic: the watchdog
ladder's legitimate worst case is `XHCI_COMMAND_TIMEOUT_MS +
(XHCI_COMMAND_ABORT_WAITS + 1) * XHCI_COMMAND_ABORT_MS` = 20 s, and the backstop
belongs beyond it. Simply raising the poll count would re-parameterise a
quantity whose units are wrong: the 2a and 2b guests, and this same E460 in the
Finding S boot, all show different `CheckCallbacks`-to-`HealthPolls` ratios.

#### Done on the host: task 13-R.3.5, five thresholds rather than three

The clock is `XHCI_EXTENSION.PollClockMs`, advanced once per health poll by
`XhciPollClockAdvance` (`src/xhci_init.c`) from the same MFINDEX register
`XhciFrameSample` reads, one tick per frame and one frame per millisecond. It is
a second axis rather than the published frame number, because that one advances
per call on its stall path and carries a congruence claim an isochronous Frame
ID depends on.

| was | is | on the E460, before |
|---|---|---|
| `XHCI_COMMAND_AGE_POLLS` 64 | `XHCI_COMMAND_AGE_MS` 32,000 | 2.3-5.1 s, not 32 s |
| `XHCI_DEV_AGE_POLLS` 128 | `XHCI_DEV_AGE_MS` 64,000 | 4.6-10.2 s, not 64 s |
| `XHCI_DEV_STALL_POLLS` 10 | `XHCI_DEV_STALL_MS` 5,000 | 0.36-0.8 s, not 5 s |
| `XHCI_PORT_AGE_POLLS` 16 | `XHCI_PORT_AGE_MS` 8,000 | 0.6-1.3 s, not 8 s |
| `XHCI_EP_RESTART_POLLS` 2 | `XHCI_EP_RESTART_MS` 1,000 | 72-160 ms, not 1 s |

The third column is Finding V's, at the measured 36-80 ms per poll. The task
named the first three; the last two were found by reading the rest of the tree
for the same shape. The first is the defect: a command backstop at 2.3-5.1 s
sits at or under `XHCI_COMMAND_TIMEOUT_MS` = 5,000, the watchdog it was sized to
sit 12 s behind. The port age had 0.6-1.3 s over the 500 ms this driver allows a
root-port reset, a margin of 1.2-2.6x where 16x was intended. The
endpoint-restart net (`xhciEpRestartIfStopped` clears `XHCI_EPQ_PAUSED` and
rings the doorbell) had about seventyfold over usbport's one-frame abort gate
where a thousandfold was intended.

Two relationships are now `XHCI_C_ASSERT`s rather than prose (the command
backstop clearing the watchdog ladder, the port age clearing the reset
deadline), and the host suite drives every detector against a model clock, with
a vector per detector asserting that polling harder does not spend the budget.
`sizeof(XHCI_EXTENSION)` moved 87,644 -> 90,600, so a fourth instrument
generation exists; see `run-13e-evidence/README.md`. The one bench reading this
owed is P13-CLOCK, taken as Finding V.

#### Ring accounting

`p12a3` is the only dump with an intact ring: `Log.Used 12,560`,
`Log.BytesDropped` 0. By `p12a5` it had dropped 3,625 bytes, by `p12a10` 27,651,
and by `p12a15` 46,308; the ring turned over roughly four times. Thirty-three
recoveries each write the whole initialization note block, which P11-RETRY's
estimate did not account for. Take the ordering out of `p12a3`; the later dumps
carry counters that are still exact and a ring that is not.

---

### Finding T - the repair WORKS, three times out of three, and the wedge that is left is this project's own bound

Read off the E460 on the first batch 13-R boot, P11-BENCH. The wedged read is
`run-13e-evidence/p11w.txt`, its note ring `run-13e-evidence/p11w-notering.txt`
(7,532 bytes, 323 records, no wrap), the healthy control
`run-13e-evidence/p11h.txt`, and both probes. `ExtensionBytes = 87636` on both,
so the reading is against the repaired tree. The raw dumps those were decoded
from are gone (task 14.1.11); the counters below are the whole of what they
held.

The recipe wedged at cycle 3, not cycle 1 (two clean cycles where the pre-repair
build died twice out of two), and the note ring says why.

#### The ladder, in order, four times

```
cmd.age.abort=00070004   ctrl.failed.here=00000001   ctrl.recover.begin=00000001   ctrl.recovered=00000001
cmd.age.abort=00070004   ctrl.failed.here=00000002   ctrl.recover.begin=00000002   ctrl.recovered=00000002
cmd.age.abort=00070001   ctrl.failed.here=00000003   ctrl.recover.begin=00000003   ctrl.recovered=00000003
cmd.age.abort=00070004   ctrl.failed.here=00000004   *** nothing, ever ***
```

| | |
|---|---|
| `RecoveryAttempts` | 3 |
| `RecoveryCompletions` | 3 |
| `RecoveryFailures` | 0 |
| `RecoveryLastStep` / `RecoveryLastStatus` | 22 (`XHCI_INIT_STEP_DONE`) / 0 |
| `ResetControllerCalls` | 4 |
| `CommandAgeAborts` / `CommandAgeResets` | 4 / 4 |
| `CommandsIssued` / `CommandsCompleted` | 76 / 76 |
| `CommandsTimedOut` | 0 |
| `InterruptCount` = `InterruptsClaimed` = `DpcCount` | 332: the interrupter is alive, unlike the wedged dumps of Finding S |

#### Task 13-R.1 is confirmed on the machine that reproduces the fault

Each `ctrl.recover.begin` is followed in the ring by the whole initialization
sequence re-running (`gate.busmaster.command`, `hc.version`, the full `map.port`
list, `hc.pagesize`) and then `ctrl.recovered`. After each one the bus comes
back: `port.connect` on 6, 7 and 8, all three internal devices re-addressed, and
then the mouse's own port enumerating again with its periodic endpoint
(`ep.open.rate=00200004`). The `ControllerFailed` latch is no longer terminal,
and that is read rather than inferred. It is also the first time this project
has watched its own driver reinitialize a controller in place, from a DPC, on
real silicon.

#### Task 13-R.2 does not repair this fault

`CommandAgeAborts 4` equals `CommandAgeResets 4`: `CRCR.CA` was written four
times and the command ring never stopped once. Every incident went abort ->
still not stopped -> escalate. The rung buys ~32 s per incident and changes
nothing else. It is kept, because 4.6.1.2 asks for that order and it costs one
register write, but it must not be reported as the Finding 3 repair. On this
controller, in this state, the xHC does not answer a Command Abort.

#### The wedge that remains is the attempt cap, this project's own defect

`ResetControllerCalls` 4 against `RecoveryAttempts` 3, with `RecoveryFailures`
0. The fourth latch armed no recovery at all; the ring ends at
`ctrl.failed.here=00000004` with nothing after it.

`XHCI_RECOVERY_MAX_ATTEMPTS` was bounding a lifetime total. Three recoveries
that all succeeded spent the entire budget, so an ordinary fourth incident found
nothing left. The cap's own documentation said "a controller that will not come
back after that is left latched", a statement about consecutive failures; the
code counted attempts of any outcome. Fixed the same day:
`RecoveryFailuresConsecutive` is what the arming predicate reads, a refusal
increments it and a success clears it, and `RecoveryAttempts` stays the lifetime
reading. Host regression: recover more times than the cap allows and then fail.

The note ring shows the port was not deaf: `port.connect=00000302` appears
before `ctrl.failed.here=00000004`. The replug was seen and latched; the driver
had no recovery left to arm.

#### The 5 s watchdog produced no verdict, four times

`cmd.timeout` appears nowhere in 7,532 bytes of ring. `CommandsTimedOut 0`
across 76 commands. `CommandTimerFailures 0`, so every submit had the service.
`CommandStaleCallbacks 74` plus `CommandsAfterFailure 2` is 76: one callback
accounted for per command issued, and not one of them ever found its own command
live. Four of those commands were outstanding for more than thirty seconds each,
with a 5,000 ms timer armed, so `CommandsTimedOut` should have been 4. It is 0,
and the entire ladder was being carried by the health poll's age detector.

Two explanations fit and no counter in this dump separates them: the callback
never arrived, or it arrived far too late. `CommandTimeoutArrivals` was added
the same day, incremented immediately after the signature and epoch bracket and
before every branch, as the discriminator. Finding U read it.

#### What the port looked like, against Finding S

```
port 3   PORTSC 000006E1    PP 1  CCS 1  PED 0  PR 0  PLS 7  spd 1   CSC 0
                            (Finding S read 00020AE1 - same shape, but CSC=1)
```

`CSC` is clear this time. The change bit was raised, mapped and acknowledged
(`PortEventsMapped 62` equals `PortEventChanges 62`), which is the recovered
interrupter doing its job. The port sits connected in Polling with nothing owed
to it and no driver willing to touch it, which is what a spent attempt cap looks
like from the register side. Ports 6/7/8 are connected and enabled, and
`RhPortsUnpowered 0`: the bus was never unpowered, on either reading.

#### What this run does not settle

The recipe reached the fault at cycle 3, where every pre-repair reading reached
it at cycle 1. Two clean cycles is unexplained; nothing in either repair can
make a Stop Endpoint complete. It may be ordinary variance the two-out-of-two
reading never had the sample size to show. It is not to be written down as the
repair having partly prevented the fault.

---

### Finding S - ROOT CAUSE. The driver marks its own controller failed, masks interrupts, and waits for a stop/start that never comes

Half of this finding is refuted by Finding V; read this before citing any of it.

What stands, and it is the root cause: the driver latches `ControllerFailed` on
itself, masks its own interrupt enables, and names a stop/start that nothing
anywhere performs, established by the batch 13-R census of both shipping
`usbport.sys` builds. That is what task 13-R.1 repaired and what design record
07 is built on. The latch, the missing owner, the census, and the ring ending at
`ctrl.failed.here` are all unchanged.

What is refuted is the mechanism this finding gave for how the latch is reached:
"a Stop Endpoint that never completes". It does complete. Finding U showed the
abort rung firing 33-for-33, which is what aborting a command that was going to
finish looks like, and Finding V gave the command its proper window and read 123
commands issued, 123 completed, the abort rung 0 times, the whole escalation
ladder at 0. What was really happening is that the health poll's age backstop,
counted in polls and sized against a 500 ms period the E460 does not have, stood
at 2.3-5.1 s and pre-empted the 5 s watchdog on every boot (task 13-R.3.5).
Phase 14 must not restate the headline in any form.

Read out of the note ring, on the second E460 run with `XhciLogDebugView = 1`.
89 records, 2,070 bytes, no wrap. The recipe reproduced the wedge identically to
the first run (same ladder counters, same PORTSC), so this is a mechanism, not
an incident. Ring: `run-13e-evidence\wedge2-notering.txt`.

The tail of the ring is the entire fault, in order:

```
port.connect=00000301          the mouse arrives at port 3
port.reset.begin=00000003
port.reset.done=00000301
slot.enabled=00000503          slot 5
port.reset.begin=00000003
port.reset.done=00000301
slot.addressed=00050401        slot 5, address 4
slot.route=00000000
slot.parenthub=00000000
ep.open=00050303               ONE endpoint - the interrupt endpoint
ep.open.rate=00200004          with an interval: it is PERIODIC
port.disconnect=00000300       the mouse is unplugged
ctrl.failed.here=00000001      <-- and the ring STOPS. Forever.
```

Nothing is recorded after that line. No `port.connect` for the replug, no reset,
nothing. The driver did not drop the replug's event; it never saw one, which
closes the link Finding R's counters could not: `PortEventsMapped` equals
`PortEventChanges` because after this point no events are delivered at all.

`ctrl.failed.here` is `xhciResetController` (`src\xhci_dispatch.c`):
`ResetController` was called, and this driver's own handler masks interrupts and
marks the controller failed:

```c
    if ((ext->Flags & XHCI_EXT_FLAG_INITIALIZED) != 0) {
        XhciMaskInterrupts(ext);
    }
    ext->ControllerFailed = 1;
    XhciLogNoteLocked(ext, "ctrl.failed.here", ext->ResetControllerCalls);
```

and then says in its own trace text what it is waiting for: "ResetController:
controller marked failed - a miniport cannot reinitialize from a DPC holding
usbport's lock; recovery is a stop/start". That stop/start never comes. usbport
does not issue one, so the driver sits with its interrupts masked and its
controller marked failed for the life of the boot. That is why the only
recovery anybody had found was a cold boot.

Both halves of that were read out of the binaries. `StartController` and
`StopController` have exactly three call sites each in each shipping
`usbport.sys`, and everything that can reach one of them is entered through the
`IRP_MJ_PNP` or `IRP_MJ_POWER` handlers (plus, on SP4 only, a USBUSER IOCTL
that asks for a power transition). None of the reset DPC, the 500 ms timer, the
ISR/DPC pair or the async-callback DPC can, so "usbport does not issue one" is
a census rather than an inference.

And usbport never asked for the reset either: the only producer of
`UsbPortInvalidateController(RESET)` is a miniport, and usbport's one internal
call site of that routine passes `SURPRISE_REMOVE`. The request came from
`XhciRequestControllerReset` in `src\xhci_cmd.c`, on the command timeout. Every
address is in `docs/usb-xhci-info/usbport-miniport-abi.md`, in the two
subsections after the `UsbPortInvalidateController(RESET)` box.

The interrupter really did go dark: `InterruptCount 182` and `InterruptsClaimed
182` frozen, against `HealthPolls 8485` still climbing.

#### The whole chain, end to end

| # | What happens | Evidence |
|---|---|---|
| 1 | a Low-Speed mouse, one periodic endpoint, is unplugged from port 3 | `ep.open.rate=00200004`, then `port.disconnect` |
| 2 | its teardown owes a Stop Endpoint, and the quiesce is lost | `EndpointQuiesceLost 1`, `AbortsBeforeStopped 2` |
| 3 | the command ages out | `CommandAgeResets 1` |
| 4 | this driver asks usbport to invalidate the controller, and usbport's only answer is a DPC that calls this driver's `ResetController` back | `ResetControllerCalls 1`; the batch 13-R census: usbport's own single `InvalidateController` call site passes `SURPRISE_REMOVE`, never `RESET` |
| 5 | this driver masks its own interrupts and marks the controller failed | `ctrl.failed.here=00000001`, and the ring ends |
| 6 | it waits for a stop/start, as its own comment says. usbport never sends one | nothing after that record; the census says nothing in usbport can, short of a PnP or power IRP |
| 7 | the replug raises `CSC` in hardware; no event is delivered, so nothing acknowledges it and nothing resets the port | `PORTSC 00020AE1`: powered, connected, Polling, `CSC` standing |
| 8 | only a cold boot re-initialises | which is the whole of Finding 3 |

Every earlier finding fits. Finding L: the fault needs a periodic endpoint's
unplug, because only that arms the Stop Endpoint step 2 loses. Finding P:
teardown never touches PORTSC, and it never needed to; the port is untouched and
the driver stopped listening. Finding Q: the bus was never unpowered
(`RhPortsUnpowered 0`, `StopController` never ran). Finding N: `W3BOTH` changed
slot release and ring reuse, which are downstream of step 3. And `W11POLL`: a
polled sweep reads PORTSC without needing an interrupt, and that is what made
it the one thing that ever recovered the port.

#### The two defects this names

1. The lost quiesce (steps 2-3) is what starts it. (Finding V later showed the
   command completes when given its proper window.)
2. `xhciResetController` is a dead end (steps 5-6), the more serious one because
   it converts a recoverable stall into a permanent one. The handler is honest
   about needing a stop/start it cannot perform itself, but it masks interrupts
   and returns with nothing arranging for that stop/start to happen. A failure
   path whose recovery step has no owner is not a recovery path, and it is
   reachable by any command timeout.

The ring also carries the controller describing itself into a durable record for
the first time: `hc.pci=9D2F8086` (Intel Sunrise Point-LP), `maxslots 0x40`,
`maxports 0x12` (18), `contextsize 0x20` (64-byte contexts), `scratchpad 0x22`,
`map.managed 0x0C` (12 USB 2.0 ports), six USB2-only, six companion, six USB3.

#### `ControllerFailed` is the latch, and it closes the W15SLOW tension

`xhciRhAdmitted` (`src\xhci_rh.c:88`) is the admission gate every root-hub path
that must reach hardware passes through, and it has four clauses:

```c
    if (ext->HcInfoStatus != XHCI_HC_OK)              return 0;
    if ((ext->Flags & XHCI_EXT_FLAG_INITIALIZED) == 0) return 0;
    if ((ext->Flags & XHCI_EXT_FLAG_RH_CLOSED) != 0)   return 0;
    if (ext->ControllerFailed)                         return 0;   /* <-- */
```

Once step 5 sets `ControllerFailed`, every root-hub operation that would touch
PORTSC refuses, the polled sweep included. That resolves the tension Finding R
left standing: `W15SLOW`'s sweep runs every ~8 s from the health poll, which
does keep running, but the sweep is admitted-gated, so after the latch it never
reads PORTSC, never acknowledges, never announces. P9's "nothing ever appeared
in three minutes" was this gate.

It explains `W11POLL` in the same stroke. Its sweep is the same code at a 0.5 s
cadence, so the only way it ever recovered anything is by acting in the window
before the latch closed. That is the two-stage model P9 inferred without being
able to name the stages: stage one is a stranded change bit on a live
controller, which a fast enough sweep still fixes; stage two is
`ControllerFailed`, after which nothing in the root hub is reachable at all.

One earlier deduction is retired here. A review traced `XHCI_EXT_FLAG_RH_CLOSED`,
found it set only in `XhciStopController` and cleared only in the start
sequence, and concluded that the admission gate is controller-lifecycle only.
The trace was right about that flag and wrong about the gate: the same
predicate has a fourth clause, and a plug and an unplug reach `ControllerFailed`
through the command timeout.

#### The repair, and it moved the second defect

Both halves are implemented (roadmap tasks 13-R.1 and 13-R.2; design record
`docs/contributing/design/07-controller-recovery-in-place.md`) and were read
off the E460 as Findings T, U and V.

Steps 5-6, the latch, now have an owner. `xhciResetController` still masks and
latches, which is all that is legal inside usbport's reset-DPC spin lock, but it
also raises a recovery request. The 500 ms health poll, the one periodic context
that survives the latch (`HealthPolls 8485` against `InterruptCount 182` is the
measurement that says so), arms one `UsbPortRequestAsyncCallback`; that callback
runs at DISPATCH_LEVEL holding no usbport lock, and reinitializes the controller
from HCRST in place. Step 7's admission gate re-opens because
`XhciInitController` clears `ControllerFailed` the moment HCRST completes.

Steps 2-3, the lost quiesce, turned out to be a different defect from the one
this section named, found by reading three counters the offset table did not
carry (`scripts/local/offsets.c` now carries the whole command-engine block
for that reason). Decoded from the second wedge capture, written out here
because the dump itself is gone (task 14.1.11):

```
  CommandState          1   PENDING          CommandsIssued        22
  CommandType        0x0F   Stop Endpoint    CommandsCompleted     21
  CommandAgePolls      64   the crossing     CommandsTimedOut       0   <--
  CommandAgeResets      1                    CommandAbortWaits      0   <--
  ResetControllerCalls  1                    CommandAbortsNotWritten 0  <--
  CommandOwnerLost      1                    CommandTimerFailures   0
  CommandsAfterFailure  1                    CommandStaleCallbacks 21
```

`CRCR.CA` was never written once. The health poll's age detector, whose premise
is that it fires only when the watchdog was never armed, asked for a controller
reset outright, skipping the specification's own remedy. That is now rung 1: an
over-age `PENDING` command is aborted (`CommandAgeAborts`, `cmd.age.abort` in
the ring) and only an over-age `ABORTING` one, a ring told to stop that has not,
which is 4.6.1.2's own trigger for HCRST, reaches `CommandAgeResets`.

Two corrections to the chain above. The order is not quite as tabled:
`EndpointQuiesceLost` came after the reset, from `XhciSlotPoll`'s 128-poll
recovery, not before it. And `CommandsAfterFailure 1` says the Stop Endpoint's
own 5 s watchdog did arrive, but only after the latch had closed; why it was
that late was answered by Findings U and V (the age detector's window was far
shorter than 5 s).

Host regressions (`test_controller_recovery`, and the age detector's vector
rewritten for two rungs) plus a QEMU smoke run on both targets, `vm/13r-qemu/`.
QEMU does not reproduce Finding 3 (Finding L), so that run is a regression test:
the driver loads, initializes, enumerates a hot-plugged mouse, tears it down
and re-enumerates on the same port, with every new counter at 0.

---

### Finding R - the wedged port is POWERED, the device IS connected, and CSC is standing unacknowledged. Finding Q is REFUTED

The first direct reading of PORTSC in the failed state, taken on the E460
through the `PassThru` channel, on `OBSSNAP.SYS` (plain release behaviour plus
the reader, no `XHCI_FIX_*` define, so this is the shipping driver's own
conduct). Position D is xHCI port 3. The companion report is
`run-13e-evidence/wedge2.txt`.

```
  port  PORTSC    PP CCS PED  PR PLS spd | CSC PEC WRC OCC PRC PLC CEC
     3  00020AE1   1   1   0   0   7   2 |  1   0   0   0   0   0   0
```

Every other port reads its healthy value, the six SuperSpeed ports included.

| Bit | Value | Meaning |
|---|---|---|
| `PP` | 1 | the port is powered |
| `CCS` | 1 | a device is connected and the controller sees it |
| speed | 2 | Low Speed: it identified the mouse correctly |
| `PED` | 0 | the port was never enabled |
| `PLS` | 7 | Polling: the port saw the connect and is waiting for software to reset it |
| `CSC` | 1 | the connect change is standing, unacknowledged |

#### What this refutes

Finding Q said the terminal state is an unpowered bus. `PP = 1`, corroborated
independently by `CCS = 1`: a USB device asserts its D+/D- pull-up only when it
has VBus, so a port that reads a connection is a port delivering power.

The mouse LED is an enumeration indicator, not a VBus indicator. The operator's
observation is unchanged (first plug lights instantly, second plug stays dark)
but the inference drawn from it was wrong. `PED = 0`: the port was never
enabled, so the device was never reset, addressed or configured, so it never
turned its sensor on.

It is not machine-wide. Ports 6, 7 and 8, the three internal devices, still read
`CCS = 1, PED = 1` in the wedged dump, and every empty USB 2.0 port still reads
powered. Only port 3 is affected, at least in this reproduction. The channel
itself still works in the wedged state: `probe3.txt` is identical to
`probe2.txt`, so usbport is responsive, the miniport answers, the controller is
not dead. And there is no LED-fuse number, because there is no fuse: the LED
never lights on the failing plug, so the recovery ladder is not involved in
producing this state.

#### What it puts back

The stranded change bit is the terminal state after all. `lessons.md`'s
"hot-plug operations are not hot-plug events" entry (an unacknowledged PORTSC
change bit silences the port) was demoted by Finding Q on the reasoning that a
stale RW1C bit does not remove VBus. It does not, and nothing removed VBus. The
register shows the shape that entry describes: connect seen, `CSC` raised,
nothing acknowledged it, nothing announced it, and therefore nothing issued the
port reset that would move `PLS` out of Polling. This is also the first
explanation of `W11POLL` that does not require a coincidence: the sweep reads
PORTSC, sees `CSC`, acknowledges it, latches it, announces it, usbport scans,
usbhub resets the port, the mouse enumerates.

A tension this did not resolve: `W15SLOW` runs that same sweep every ~8 s, and
P9 reported nothing appearing in three minutes with a `CSC` that, on this
reading, was sitting there the whole time. Finding S resolved it (the sweep is
admitted-gated).

#### Finding R, the counter half - a Disable Slot owed forever, and a port claimed by a device that is GONE

Decoded from the first wedge capture against the `offsets.txt` of the tree that
took it, with `scripts\local\readsnap.py`. The dump itself is gone (task
14.1.11) and this table is what it held. The device record for root port 3 is
the whole story:

```
  Dev3_State           6   XHCI_DEV_STATE_GONE
  Dev3_PendingOp       5   XHCI_DEV_OP_DISABLE_SLOT      <-- owed, forever
  Dev3_SlotId          5
  Dev3_RootPort        3   position D
  Dev3_DeviceAddress   4
  Dev3_Flags           4   DCBAA_SET only - EP0_OPEN and ADDRESS_VALID gone
```

against `SlotsEnabled 5` / `SlotsDisabled 1`: four slots are still enabled and
one of them belongs to a device the driver already knows is gone.

| Step | Reading |
|---|---|
| the Low-Speed mouse's unplug starts a teardown, and its periodic endpoint owes a Stop Endpoint | Finding L measured that only periodic endpoints arm one |
| the quiesce is lost | `EndpointQuiesceLost 1`, `AbortsBeforeStopped 2`, `TransfersAborted 2`, `EndpointRemovesHeld 3` |
| a command ages out and the controller is reset | `CommandAgeResets 1`, `ResetControllerCalls 1` |
| but the stop path never runs, so the port stayed powered | `FatalStatusDetected 0`, `RhPortsUnpowered 0`, every `PortTeardown*` 0 |
| the record is left in GONE still owing its Disable Slot, and a Disable Slot must wait for every endpoint's Stop Endpoint (`src\xhci.h`, `XHCI_DEV_OP_STOP_EP`) | `Dev3_PendingOp 5`, the slot never released |
| the replug's connect arrives at a port a dead record still claims, and its reset is suppressed | `EnumResetSuppressed 1`, `EnumClaimSpent 1` |
| so the port waits at Polling with `CSC` standing | `PORTSC = 00020AE1` |

This was the first mechanism in the investigation read rather than inferred,
and it fits every prior finding. Finding L: only periodic endpoints arm the Stop
Endpoint this hangs on. Finding P: teardown never touches PORTSC, and it never
needed to; the claim is what blocks. Finding N: `W3BOTH` gated slot release on
quiesce success and wedged anyway, because holding the slot is what already
happens.

What is exonerated: `PortEventsMapped 19` equals `PortEventChanges 19`, so not
one event was mapped to a port and dropped without acknowledging.
`XhciRootHubPortEvent`'s uncounted early return, the latent defect this sheet
had dismissed as unreachable, did not fire. The interrupter is alive too
(`InterruptCount 194` = `InterruptsClaimed 194`, `DpcCount 194`, `EventsTotal
206`).

A safety net designed for this case was measured failing. `EnumResetSuppressed`'s
own declaration says the bound exists because that state "can get stuck", and
that "at most one reset is suppressed per claim: the second one re-arms whatever
the record says, so the cost of being wrong is one enumeration round rather than
a dead port". The counter reads 1, and the port is dead. The assumption the
bound rests on, that a second reset will come, is false here: nothing re-drives
it, so one suppression is permanent.

One link this did not close: `CSC` stands on port 3 while `RhChangesCleared 19`
says nineteen changes were folded and acknowledged. Either the final connect's
event never reached the driver, or it did, was folded, and the port raised `CSC`
again afterwards with nothing left to service it. The note ring separated those
(Finding S: the event was never delivered).

#### The note ring was OFF, and 91 notes were thrown away

```
  Log.Enabled          0        Log.Appends       0
  Log.SwitchRead       1        Log.Suppressed   91
  Log.FileRequested    0        Log.Used          0
  Log.DebugViewEnabled 0
```

The switch was read (`SwitchRead 1`); nobody had set either value, so every
append site tested `Enabled`, found it zero, and produced nothing, 91 times. The
dump carried an empty ring, a fact about the channel rather than about the
fault.

The fix is one registry value. On Windows 98 the file sink is force-disabled by
design and DebugView has no viewer, so the instinct is that the ring is
unreachable there. It is not: `XhciLogDebugView` does not have to reach
anywhere. Setting it makes `Log.Enabled` nonzero, which is the only thing the
append sites test, and the ring then fills inside `XHCI_EXTENSION`, where the
`PassThru` dump reads it straight out of memory. Before the next run: Regedit,
Edit -> Find, `XhciLogDebugView` (the INF put it in the driver's own software
key), set it to 1, cold boot, run the recipe, dump. That produced Finding S.

---

### P10. The reading channel: the `PassThru` route, answered by disassembly

The channel this item built, `XHCI_OBS_SNAPSHOT`, did its work (Findings R, S,
T, U and V) and was then removed from the tree by task 13-R.4, because it was an
unofficial escape hatch on an undocumented vendor IOCTL. Task 13-L.2 later
rebuilt the same channel into every shipping flavour, switched off by default
and engaged by the `XhciLogVerbosity` registry value; `xhcisnap/` is tracked
again and `0.0.0.6` publishes it. So P10, P10-BENCH, P11-BENCH, P11-RETRY and
P13-CLOCK below are the record of what was run with the throwaway instrument.
Taking another reading today needs no rebuild: an installed shipping binary,
`XHCISNAP -verbosity N` and a restart, which is what stage L3 did on this
machine. `docs/contributing/passthru-snapshot-instrument.md` carries the
contract, the ordering rules and the operating traps.

Why a reading channel rather than another candidate binary: Finding Q ended
five boots of remedies with the first electrical observation of the failed
state, and the mechanism question had a named suspect whose breadcrumbs the
driver was already recording (`ctrl.failed.here`, `FatalStatusDetected`,
`ResetControllerCalls`, `RhPortsUnpowered`, `PortTeardown*`, and the task
11-V.9 note ring). All of it sat in RAM on a machine with no sink: the file
sink is force-disabled on Windows 98 (the measured `ZwCreateFile` hang), the
DebugView sink has no viewer there (Finding B), and a flush needs a PASSIVE
moment the session never provides. What was missing was a way to read, not a
way to record.

The plan was usbport's own vendor escape, `PassThru`, then a stub in this
driver (`src\xhci_dispatch.c`, `xhciPassThru`, returning
`MP_STATUS_NOT_SUPPORTED`). Four things were unknown: which IOCTL reaches it,
what the buffer contract is, whether the route is user-reachable, and whether
Win98's usbport creates an openable device name.

#### Step 1: the route, read statically off the shipping binaries

All four questions were answered from the binaries. The full derivation, with
every address in both builds, is the box under "Debug / single-packet" in
`docs\usb-xhci-info\usbport-miniport-abi.md`, with a `legal-provenance.md`
section 4 row tagged static. In brief:

| Question | Answer |
|---|---|
| Which IOCTL | `0x00220438`, `METHOD_BUFFERED`, `FILE_ANY_ACCESS` (`IOCTL_USB_USER_REQUEST`), read off usbport's own comparison chain (NUSB `0002E00C`, SP4 `0002E8E6`) |
| Which request | `UsbUserRequest` = 3, off the 8-entry jump table (NUSB `00027E9E`, SP4 `00028528`) |
| Buffer | one buffer, in length == out length, >= 0x28; 0x10-byte header, GUID at +0x10, `ParameterLength` at +0x20, parameters at +0x24; `RequestBufferLength` must equal the input length exactly |
| Openable name | yes. The HCD FDO's start path always `IoCreateSymbolicLink`s `\DosDevices\HCD<n>` onto `\Device\USBFDO-<n>` (NUSB `00011D19`), and `IRP_MJ_CREATE`/`CLOSE` on it succeed with no work. So `\\.\HCD0` |
| Gated? | no. The test-mode gate only fires for requests with `0x30000000` set; request 3 skips it, so `IOCTL_USB_DIAGNOSTIC_MODE_ON` is not a prerequisite |

Three findings from the same read changed the implementation, and each would
have cost a boot to discover at the bench:

1. The buffer handed to the callback is a non-paged kernel copy of the system
   buffer, copied back afterwards. The miniport never sees a user address,
   needs no probe, and may fill the block with the controller lock held at
   DISPATCH_LEVEL.
2. `PassThru` is entered at PASSIVE_LEVEL holding no usbport lock, so it is
   free to take this driver's own lock.
3. `xhciPassThru` must keep returning 6 for a GUID it does not recognise.
   usbport's other call site is its own internal port-status probe (fixed GUID
   `{022252A1-ED5D-4E3F-976F-B2D9DB3D2BD3}`), and it falls back to
   `RH_GetPortStatus` only when the return is exactly 6. Answering with
   `MP_STATUS_FAILURE` would suppress that fallback and hand back a zeroed
   port status. That site is reachable only through a test-mode request, but
   the rule costs nothing and the failure would be silent.

Also settled on the way: the `0xF0`/`0xF4` displacements batch 6-0 recorded for
these sites are interface offsets, not packet offsets. Both are packet slot
`0xE0`, through the wrapper prefix this project already knew about (0x10 on
NUSB, 0x14 on Win2000/XP). Nothing in `src\` was wrong; the note was ambiguous.

#### The kernel side

Built behind `XHCI_OBS_SNAPSHOT`, not an `XHCI_FIX_*` name, because it repairs
nothing. The contract is the block after `XHCI_EXTENSION` in `src\xhci.h`, the
implementation `xhciPassThru` in `src\xhci_dispatch.c`, the regression
`test_passthru_snapshot` in `test\test_init.c`. Two measured facts decided the
shape:

1. `sizeof(XHCI_EXTENSION)` was 87,592 and usbport refuses
   `ParameterLength > 0x10000` before the miniport is reached. One call cannot
   carry the extension, so the reader is a window over a region and the tool
   loops on an offset: two windows for the extension as it stood. Each window's
   header carries a `TearDetector` (`CheckCallbacks`, which usbport advances
   about twice a second, read inside the lock). Equal across every window
   means nothing serviced the controller while the dump was taken; unequal
   means the dump is torn. It detects tearing, it does not prevent it.
2. The define moves nothing. `sizeof(XHCI_EXTENSION)` measured 87,592 with the
   define on and off, because this candidate adds no field to the extension.
   Every other candidate define moves `MiniPortExtensionSize` and invalidates
   the tracked offset table. A binary built with this define alone decodes
   against the ordinary `offsets.txt`.

The wire format, one window: a 48-byte, twelve-`ULONG` header followed by raw
payload bytes. The header carries the reply signature, a schema version, its
own size, a status word, the echoed region and offset, the region's whole size
(so the caller can size its loop), the payload size, `ExtensionBytes` (the
layout key the host-side offset table is matched against), the port count, the
tear detector, and build flags. Region 0 is the extension's raw bytes; region 1
is the raw PORTSC array. The caller writes a three-`ULONG` request (signature,
region, offset) into the same block first, so the driver reads all of it before
writing any of it.

Because usbport collapses every nonzero `MPSTATUS` into one
`UsbUserStatusCode`, every refusal is reported inside the header and the call
still returns success; the single exception is a block too small to hold a
header. `ExtensionBytes` is filled even on a refusal, because a dump decoded
against the wrong offset table is a wrong reading rather than a failed one.

The PORTSC region is read with `XhciReadPortsc` and the change bits are not
acknowledged, nothing is latched into the port shadow. Every other PORTSC read
in this driver must fold and ack, because a read that drops a change bit
silences the port (`lessons.md`, "hot-plug operations are not hot-plug
events"). This is the documented observation-mode exception: an instrument
that acknowledged what it came to measure would destroy the evidence on the
one boot that mattered. The regression sets a connect change, reads the
region, and requires the register to be byte-for-byte unchanged afterwards.

Gates, all run: 10,830 host checks with the define off (unchanged), 10,888 with
it on (58 new checks, 0 failures); `/W3 /WX /Za` clean both ways; the DDK
release build with the define passes the import gate with no new imports
(82,475 bytes, carrying the probe marker like every candidate); and a plain
release build with `XHCI_EXTRA_DEFINES` empty still byte-reproduces the
published `0.0.0.4` at 81,899 bytes.

The 18 differing bytes are all build stamps (the COFF `TimeDateStamp`, the
optional-header `CheckSum`, the three `IMAGE_DEBUG_DIRECTORY` stamps, one byte
inside `IMAGE_DEBUG_MISC`, and the CodeView `NB10` stamp, each verified by
mapping the offset to its field). The count is 18 here and 14 in the W15SLOW
record; a timestamp that differs in three of its four bytes counts three, so
the number tracks the clock rather than the build.

#### Step 2: the user-side tool

`xhcisnap\` (tracked, with its own `build.cmd` and `README.md`), built with the
in-repo MSVC 6.0 as a Win32 console EXE that runs on Windows 98 SE. It opens
`\\.\HCD<n>`, loops the windows for both regions, writes `BASENAME.BIN` (the
raw extension, the same artifact the QEMU live-counter reader produces, so the
existing offsets machinery decodes it) and `BASENAME.PSC` (the raw PORTSC
array), and prints the PORTSC decode. `PP` clear on every port is called out
in those words, since it is the number the instrument was built to fetch. The
tear-detector verdict is the last line, so it reaches the operator at the
bench. `-probe` separates the two causes of "nothing came back": a route that
does not work on this machine versus a route that works and a driver without
the define.

On the Windows 11 development host, `\\.\HCD0` opens and every one of
`-probe`'s controls returns the predicted status: 6 for a miniport that
declines the GUID (Microsoft's own xHCI miniport), 2 for an unknown request
code, 4 for a `RequestBufferLength` that disagrees with the input length, and
7 for a buffer below the 0x28 floor. That host runs a modern descendant of
usbport, not NUSB's 5652 and not SP4's 6681, so it confirmed the derivation
without saying anything about Windows 98.

#### Step 3: the chain runs end to end on Windows 98

Two boots of the 2a guest, both with the `XHCI_OBS_SNAPSHOT` debug build
(DriverEntry "built Aug 23 2026 22:33:39" in the trace). Launcher:
`scripts\local\qemu-win98-run-13psnap.cmd`. The three archived traces:

| Trace | DriverEntry | `cb PassThru` lines |
|---|---|---|
| `vm\win98-debugcon.13psnap-preswap-old-driver.log` | 12:27:20, the pre-swap binary | 0 |
| `vm\win98-debugcon.13psnap-uhci-no-passthru.log` | 22:33:39, the candidate | 0 |
| `vm\win98-debugcon.13psnap-clean-4-passthru.log` | 22:33:39, the candidate | 4 |

Four is `XHCI_DBG_CALL_LIMIT`, so the trace says "the callback was reached and
kept being reached" rather than counting IOCTLs. That is the driver's own
evidence, independent of the tool, and it separates rows 2 and 3. The dump is
`vm\SNAP_BIN.out` / `vm\SNAP_PSC.out`.

Boot 2 worked, and every clause of the derived contract held on the 5652 build:

```text
xhcisnap: opening \\.\HCD0
route probe - IOCTL 0x00220438 on this controller:
  PassThru, our GUID                 status  0 (success)
    the miniport ANSWERED - this driver has snapshot support
  unknown request code 15            status  2 (invalid request code)
  RequestBufferLength disagreeing    status  4 (invalid header parameter)
  a 0x20-byte buffer                 status  7 (buffer too small)
```

```text
extension:
  C:\SNAP.BIN: 87592 bytes in 2 window(s) (region is 87592)
  ExtensionBytes = 87592
  build flags    = 00000003  debug diagnostic
PORTSC:
  C:\SNAP.PSC: 16 bytes in 1 window(s) (region is 16)
  port  PORTSC    PP CCS PED  PR PLS spd | CSC PEC WRC OCC PRC PLC CEC
     1  000002A0   1   0   0   0   5   0 |   0   0   0   0   0   0   0
     ... ports 2-4 identical ...
coherence: tear detector 217, unchanged across every window - the dump is coherent
```

So Win98's ntkern honours the `\DosDevices\HCD<n>` link, a Win32 `CreateFile`
reaches the FDO, and the round trip completes into this driver's `PassThru`.
The healthy baseline is on record: `PP = 1` on every port, `PLS = 5`
(RxDetect), no change bits. `SNAP.BIN` was carried out on the floppy image and
decoded against an `offsets.txt` regenerated from the tree; the header's tear
detector (217) equals `CheckCallbacks` decoded out of the dump body (217).
Healthy-machine ladder reading: `HealthPolls` 217, and `CommandAgeResets`,
`FatalStatusDetected`, `ResetControllerCalls`, `RhPortsUnpowered`,
`PortTeardownFailures`, `PortTeardownSkipped`,
`PortTeardownSkippedSuspended`, `HostControllerEventResets` all 0.

Boot 1 produced a separate finding. It ran with the guest's UHCI controller
present and failed in a way that looked like the channel not working at all:

```text
xhcisnap: opening \\.\HCD0
  DeviceIoControl failed, error 317        (all four controls)
```

`HCD1`, `HCD2` and `HCD3` refused to open, and there was no `cb PassThru` line
anywhere in the driver's trace on a boot where the driver was demonstrably
alive (`PollController`, health polls, RH port status queries all climbing).
Whatever `HCD0` was on that boot, it was not this miniport.

The mechanism is structural. This guest is not a plain NUSB install (batch 9-V
let the wizard install Win98's own USB stack), and that stack creates its own
`\DosDevices\HCD0` for the UHCI controller. usbport builds its link at a fixed
index from its own controller number with no retry and no fallback (NUSB
`00011C90`, the `'0' + controllerIndex` patch at `00011CF7`), so a name already
taken is a link that never appears, and the failure is silent. Removing the
UHCI device removed the competitor and boot 2 worked first time.

The E460 is xHCI-only and was installed from plain NUSB, so nothing should
hold `HCD0` there, but `-probe` is what settles it, in five seconds, before
anything is wedged:

| `-probe` says | It means |
|---|---|
| `the miniport ANSWERED` | the channel is live; take the dump |
| `the ROUTE WORKS, this driver has no snapshot support` | usbport is fine, the wrong binary is installed: `fc /b` and re-copy |
| `cannot open \\.\HCD0` | no usbport HCD link on this machine at all |
| opens, but `DeviceIoControl failed` | boot 1's signature: something else owns that name. Try `-c 1`, `-c 2`; if none answer, this machine has the collision |

One gap this run found and closed: `ResetControllerCalls`, `RhPortsUnpowered`
and the three `PortTeardown*` counters, four of the breadcrumbs the mechanism
question is named after, were missing from `offsets.txt`. They and
`CheckCallbacks` were added to `scripts\local\offsets.c`. Regenerate
`offsets.txt` from the same tree as the deployed binary before decoding
anything; `ExtensionBytes` in the tool's output is what must match.

The E460 boot that followed (wedge it, dump, decode) is P10-BENCH.

### P10-BENCH. The E460 boot, step by step: three boots, and boot B does the work

This ran on a batch 13-R boot, with the instrument that task 13-R.4 later
removed (see the note at the head of P10). It is a record, not a procedure.

The session produces one healthy control dump, one LED-fuse number, and one
dump of the fully formed wedge. Together they answer whether the recovery
ladder fired and whether this driver's own `XhciStopController` unpowered the
bus, which was the open question of Finding 3.

Carry: `out\bench-13e-wedge\OBSSNAP.SYS` (82,475 B) and `XHCISNAP.EXE`
(49,152 B), hashes in that directory's `MANIFEST.TXT`, a stopwatch, the
Low-Speed mouse whose sensor LED is the Finding Q probe, a High-Speed flash
drive, and a second flash drive to carry the dumps home.

Two rules that ruin the session if missed:

1. In the wedged state the USB bus is dead machine-wide, so the dump cannot be
   copied onto a flash drive while the machine is wedged. Write it to `C:\`;
   it survives the cold boot, and boot C retrieves it.
2. Never install by INF. Update-over-install on Windows 98 bugchecks this
   machine at `0028:C00312EE`. Installation here is `ren` + `copy`, always.

#### Boot A: find out what is installed, then install the instrument

The P9 teardown's restore was never confirmed, so W15SLOW (82,139 B) may still
be on the machine. Five candidate binaries share sizes with each other and all
report version `0.0.0.4`, so `dir` and Driver File Details cannot tell them
apart.

```bat
cd \XHCI
fc /b C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.SAV
```

`no differences encountered` means the pristine `0.0.0.4` (81,899 B) is
installed. Anything else means a candidate is still in place; write down which
one, since it qualifies every observation made since that install.

```bat
copy C:\XHCI\OBSSNAP.SYS C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS
copy C:\XHCI\XHCISNAP.EXE C:\
fc /b C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS C:\XHCI\OBSSNAP.SYS
```

The third line checks that the copy landed on a file the loader had open. Then
shut down properly and cold boot; this machine cannot warm-restart at all.

#### Boot B: the whole session, with no reboot in the middle

B1. Is the channel live? Before touching any USB device:

```bat
C:\XHCISNAP -probe
```

`the miniport ANSWERED` is a positive identification of the running binary,
and a better one than `fc /b`: only an `XHCI_OBS_SNAPSHOT` build answers that
GUID. The other outcomes:

| It says | Do this |
|---|---|
| `the miniport ANSWERED` | go to B2 |
| `the ROUTE WORKS, this driver has no snapshot support` | the copy did not take. Redo boot A |
| `cannot open \\.\HCD0` | try `-c 1`, `-c 2`. If none open, usbport published no link at all: stop and record it |
| opens, but `DeviceIoControl failed` | something else owns that name (2a hit this with a UHCI present). Try `-c 1`, `-c 2` |

B2. The healthy control. Redirect the output to a file: on real silicon the
root-hub port count is large enough that the PORTSC table scrolls off the top
of a DOS box, and there is no scrollback worth trusting (found at the bench).
The captured text also travels home in boot C with the dumps.

```bat
C:\XHCISNAP -probe > C:\PROBE1.TXT
C:\XHCISNAP -o C:\HEALTHY > C:\HEALTHY.TXT
more < C:\HEALTHY.TXT
```

The `more` is how you still see it; redirection leaves the screen blank, which
on a wedged machine looks like the tool hanging. Expected, from the 2a guest's
healthy reading: `PP = 1` on every port, `PLS = 5`, no change bits, every
ladder counter 0, and `the dump is coherent` on the last line. On the E460
expect `build flags = 00000002` (diagnostic with no `debug` bit, since this is
the release flavour). Without this control every number in the wedged dump is
uncalibrated.

Do not take a separate healthy LED control here. Plugging the mouse in for a
control spends its first plug, and Finding K established that a Low-Speed
mouse alone fails on its second, so the teardown that starts the fuse would
happen with no stopwatch on it. The mouse enumerating at step 2 below is the
same observation, taken inside the run. The general shape: on a machine whose
fault is triggered by a count of plug cycles, no "just check this first" step
that touches the port under test is free. Fold such controls into the run, or
take them on a different port.

B3. The recipe, with the stopwatch running (the LED fuse). At position D (the
left USB-A socket), nothing else attached:

```text
1. High-Speed flash drive -> D      enumerates       unplug
2. Low-Speed mouse       -> D      its LED lighting here IS the healthy control;
                                   enumerates    *** START THE STOPWATCH AS YOU UNPLUG IT ***
3. ~2 s later, plug the mouse back into D and LEAVE IT THERE
```

Write down the stopwatch time at which the mouse's LED dies:

| LED dies at | What it fingerprints |
|---|---|
| ~5 s | the command watchdog |
| ~20 s | the abort ladder |
| ~32 s | the age-poll backstop |
| immediately, or never lights | something below the ladder; the ladder is excluded |
| still lit at 60 s with no detection | the wedge has a stage that is not the power loss |

Also note whether the mouse was ever detected (a devnode appearing), and check
the spare socket's LED with the second flash drive, which says machine-wide
versus per-port.

B4. The reading, taken while the machine is still wedged. Wait until the LED
has died and the port is fully dead, then:

```bat
C:\XHCISNAP -probe > C:\PROBE2.TXT
C:\XHCISNAP -o C:\WEDGED > C:\WEDGED.TXT
more < C:\WEDGED.TXT
```

Run `-probe` again first: the IOCTL path runs through usbport and a wedged
controller is where it might not answer; if it does not, that is itself a
result. Then shut down cleanly. The bus is dead until the cold boot.

#### The E460's healthy baseline, and it is not "PP = 1 everywhere"

The first direct reading of this controller's root-port array: 18 ports, 72
bytes.

| Ports | PORTSC | State |
|---|---|---|
| 1-5, 9-12 | `000002A0` | powered, empty, `PLS = 5` (RxDetect): USB 2.0 ports with nothing in them |
| 6, 7, 8 | `00000603` / `00000E03` / `00000603` | connected and enabled, `PLS = 0` (U0), speeds FS / HS / FS: the three internal devices, which cannot be unplugged |
| 13-18 | `00000080` | `PP = 0`, `PLS = 4` (Disabled): the six SuperSpeed ports |

Ports 13-18 are unpowered by this driver's own design (Phase 4 task 5): PP
comes up asserted on every port after HCRST, and a driver that ignored them
would leave SuperSpeed devices trained onto ports nothing services instead of
falling back to the USB 2.0 companion. `PP is clear on 6 of 18 ports` is the
expected reading on this machine.

This is a trap for the tool: `xhcisnap`'s headline only fires the "this is
Finding Q read off the register" line when every port is unpowered, which can
never happen here. On real silicon, read the per-port table, not the headline,
and specifically port 3, which is position D (the left-hand Always On
connector; the map is in `build-and-test.md`, "The two positions").

The socket map corroborated itself from a new direction. Ports 1, 2 and 3
empty-and-powered are the three external connectors, and 6, 7 and 8 occupied
are the internal devices, which is what `xhciqual/results/e460-2026-08-22/`
recorded by moving a flash drive around. The internal devices on 6, 7 and 8
are also free machine-wide instrumentation: if the wedge unpowers the bus
machine-wide, they lose `PP` and `CCS` in the dump.

`HEALTHY2` was identical to `HEALTHY` in every PORTSC bit after one clean
Low-Speed mouse cycle: no stranded change bits, nothing owed. A
periodic-endpoint teardown that works leaves the port array as it found it.
Tear detectors read 22305 then 51247; `CheckCallbacks` advances fast on real
silicon, so "unchanged across every window" is a tight coherence check here.

#### Boot C: retrieve, restore, and leave the machine as you found it

```bat
copy C:\HEALTHY.* <the flash drive, which works again now>
copy C:\WEDGED.*  <same>
copy C:\PROBE?.TXT <same>
copy C:\XHCI\XHCI98.SAV C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS
fc /b C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS C:\XHCI\XHCI98.SAV
```

Send that last `fc /b` line with the results. The P9 session did not, which is
why nobody knew what was installed at the start of this one.

#### Back on the host: decoding

```bat
scripts\local\regen-offsets.cmd
```

Regenerate `offsets.txt` from the same tree the deployed binary was built
from. `ExtensionBytes` in the tool's output must match `SIZEOF` in the table;
a dump decoded against a mismatched table is a wrong reading, not a failed
one. Then read the ladder out of `WEDGED.BIN` against `HEALTHY.BIN`:

| Counter | What a nonzero says |
|---|---|
| `CommandAgeResets` | the age backstop fired |
| `FatalStatusDetected` | the driver decided the controller was fatally sick |
| `ResetControllerCalls` | this driver requested a controller reset and usbport called `ResetController` back (usbport never escalates on its own, per batch 13-R) |
| `RhPortsUnpowered` | this driver's own port-power-off pass ran. It read 0 in both wedged dumps, one of the two readings that refuted Finding Q |
| `PortTeardownFailures` / `PortTeardownSkipped` / `PortTeardownSkippedSuspended` | the stop path ran but the ports would not take it |
| `HostControllerEventResets` | a Host Controller Event drove it |

`WEDGED.PSC` did not show what this step was written to look for. The wedged
port came back `PORTSC 00020AE1`: `PP = 1`, `CCS = 1`, `PED = 0`, Polling,
with `CSC` standing. The bus was never unpowered, and Finding Q was refuted.
The pairing that matters after that is Finding S's: `ResetControllerCalls`
nonzero with `ctrl.failed.here` in the note ring, and `InterruptCount` frozen
while `HealthPolls` climbs. The note ring is in the extension dump too, so
`ctrl.failed.here` and its neighbours are readable from the same file.

### P11-BENCH. Reading the 13-R repair off the E460, step by step (task 13-R.3)

This ran on a batch 13-R boot (Finding U) and again in the shape P13-CLOCK
describes at bench session 3 (Finding V). The instrument it depends on was
removed at task 13-R.4; see the note at the head of P10.

Written after the repair was committed and smoke-tested in QEMU. It replaces
P10-BENCH for the trip: P10-BENCH was written to find the mechanism and its
boot B ends in a wedge on purpose. This one asks whether the wedge still
happens, so its shape is different: five cycles instead of one, a dump at the
end instead of a dump taken while dead, and a pass condition stated in advance.
The two standing rules from P10-BENCH hold: never install by INF on this
machine (`ren` + `copy`, always), and every reboot is a cold boot.

#### P11-0. On the host, before the trip

```bat
scripts\local\build-obs-snapshot.cmd
scripts\local\regen-offsets.cmd
```

Both, in that order, from the same tree. Batch 13-R moved
`sizeof(XHCI_EXTENSION)` from 87,592 to 87,636. `readsnap.py` refuses to
decode across a mismatch rather than decoding wrongly, so a stale table means
a trip whose dumps cannot be read. Record the staged binary's size and SHA-256
in `out\bench-13e-wedge\MANIFEST.TXT`. It is a different size from the
82,475-byte `OBSSNAP.SYS` P10-BENCH carried, so for once `dir` distinguishes
the new instrument from the old one.

Then prove the instrument still works before spending a bench boot on it: the
extension grew and `xhcisnap` sizes its windows from what the driver reports.
One QEMU boot on the 2a guest, which already carries `C:\XHCISNAP.EXE`:

```bat
XHCISNAP -probe
XHCISNAP -o C:\PRE > C:\PRE.TXT
```

Expect `the miniport ANSWERED` and `ExtensionBytes = 87636`. Copy the `.BIN`
out and check `readsnap.py PRE.BIN --ladder` decodes. If this fails, the trip
is off until it does.

Carry: the new `OBSSNAP.SYS`, `XHCISNAP.EXE`, the Low-Speed mouse, a
High-Speed flash drive, and a second flash drive for the dumps.

#### P11-A. Boot A: identify, install, and turn the note ring on

Confirm what is installed rather than trusting the record:

```bat
cd \XHCI
fc /b C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.SAV
```

`no differences encountered` identifies pristine `0.0.0.4`. Anything else
means something is still installed from an earlier session; write down which.

Install the instrument, keeping the old one under a name of its own:

```bat
copy C:\XHCI\OBSSNAP.SYS C:\XHCI\OBS0824.SYS
ren C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.PRE
copy C:\XHCI\OBS0824.SYS C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS
fc /b C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS C:\XHCI\OBS0824.SYS
copy C:\XHCI\XHCISNAP.EXE C:\
```

Turn the note ring on. Its absence threw away 91 records on the first wedge
capture (Finding R). Regedit, Edit -> Find, `XhciLogDebugView` (the INF put it
in the driver's own software key), set it to 1. It does not have to reach
anywhere; setting it makes `Log.Enabled` nonzero, which is all the append
sites test, and the ring then fills inside the extension where the dump reads
it out of memory.

**Never set `XhciLogFile`.** It reaches `ZwCreateFile` on the boot path, where
task 11-V.7 measured an open that never returns.

Then shut down properly and cold boot.

#### P11-B. Boot B: the whole session, with no reboot in the middle

B1. Is the channel live? Before touching any USB device:

```bat
C:\XHCISNAP -probe > C:\P11P1.TXT
more < C:\P11P1.TXT
```

`the miniport ANSWERED` identifies the running binary. The other outcomes are
P10-BENCH's table. `-probe` does not print `ExtensionBytes`; the size
appears only in the `-o` dump's header at B2.

B2. The healthy control, before any USB device is touched:

```bat
C:\XHCISNAP -o C:\P11H > C:\P11H.TXT
more < C:\P11H.TXT
```

Read `ExtensionBytes` first. It must be 87636; 87,592 means the machine booted
the pre-repair instrument whatever `fc /b` said on disk. Expect the recorded
baseline: ports 1-5 and 9-12 `000002A0`, ports 6/7/8 connected and enabled,
ports 13-18 `PP = 0` by design, every ladder counter 0, `the dump is
coherent` on the last line.

B3. The recipe, five times, at position D (the left-hand Always On connector,
xHCI port 3), nothing else attached:

```text
per cycle:
  1. High-Speed flash drive -> D    enumerates    unplug
  2. Low-Speed mouse        -> D    enumerates    unplug
  3. plug the mouse back into D                   <-- the old build died HERE
     confirm it enumerates, then unplug it
```

Cycle 1 is the whole experiment: the old build wedged at step 3 of the first
cycle, twice out of two. Cycles 2-5 turn one non-event into a result. Watch
the clock at step 3 of cycle 1, because if a repair fires the delay says which
one:

| What you see at step 3 | What it fingerprints |
|---|---|
| the mouse enumerates immediately | nothing escalated; the Stop Endpoint completed |
| it enumerates after a pause of roughly 30 s | task 13-R.2: the command hung, the poll aborted it, the ring stopped |
| every USB device on the machine disappears and comes back, then the mouse enumerates, roughly 60-90 s in | task 13-R.1: it latched, and the in-place recovery reinitialized the controller |
| nothing, ever | the wedge survives. Take the dump anyway |

The third row looks like a crash and is not one. The in-place recovery
performs an HCRST, which returns every slot and every port to its default
state, so the three internal devices on ports 6, 7 and 8 drop and
re-enumerate too, and Windows may pop "new hardware" dialogs. Do not
power-cycle out of alarm; if you do, the reading is gone. A real failure here
is a bugcheck, or the devices going away and not coming back within about
30 s.

B4. The reading, with the machine still up. Write it to `C:\` and copy it in
boot C; that is the only version of this step that also works when the repair
did not.

```bat
C:\XHCISNAP -probe > C:\P11P2.TXT
C:\XHCISNAP -o C:\P11A5 > C:\P11A5.TXT
more < C:\P11A5.TXT
```

Then shut down cleanly.

#### P11-RETRY. The second run, after Finding T

The procedure above is unchanged. Four deltas:

1. A different binary, and a third generation of it. The corrected build is
   83,611 bytes, `MiniPortExtensionSize` 87,644. `dir` separates all three:

   | file | bytes | extension | what it is |
   |---|---|---|---|
   | `OBSSNAP-0823.SYS` | 82,475 | 87,592 | pre-repair. Findings R and S were read with it |
   | `OBSSNAP-0824a.SYS` | 83,579 | 87,636 | the first 13-R build. Finding T was read with it |
   | `OBSSNAP.SYS` | 83,611 | 87,644 | the corrected cap + `CommandTimeoutArrivals` |

   A dump decodes only against the `offsets.txt` built from its own tree, so
   keep the older binaries while a dump is still worth decoding: `p11w.BIN`
   was uninterpretable without the middle one. (This is also why task 14.1.11
   stopped keeping the dumps: the trees they decode against are past ones, and
   every reading they held is transcribed here.)

2. Name the retry's files `P12*` rather than `P11*`, or the first run's
   evidence is overwritten on `C:\` before it has been copied off.

3. Take an interim dump after cycle 3. Finding T's three cycles produced 323
   records / 7,532 bytes of a 16,384-byte ring, and each recovery emits the
   whole initialization note block (`hc.version` through `hc.pagesize`, twelve
   `map.port` lines) on top of the per-cycle records. Five cycles with a
   recovery on each lands around 12-13 KB, and a wrap eats the earliest
   cycles, which establish the pattern.

   ```text
   after cycle 3:   C:\XHCISNAP -o C:\P12A3 > C:\P12A3.TXT
   after cycle 5:   C:\XHCISNAP -o C:\P12A5 > C:\P12A5.TXT
   ```

   Check `Log.Used` and `Log.BytesDropped` in each: a nonzero `BytesDropped`
   in the second means read the ordering out of the first.

4. Expect the recovery to fire on every cycle, and expect it to be slow.
   Finding T measured the abort rung failing four times out of four, so every
   incident goes ~32 s to the abort, ~32 s to the latch, then the
   reinitialization. Budget about 90 seconds per cycle.

What the retry has to show: five cycles, no wedge; `RecoveryCompletions`
equal to `ResetControllerCalls`; `RecoveryFailuresConsecutive` 0 in the final
dump; and the ring's last `ctrl.recovered` followed by the replug's
`port.connect`. The same boot settles a second question: `CommandsIssued`
minus `CommandTimeoutArrivals` is the number of command watchdogs armed and
never returned. Roughly one per hung command means usbport's timer service is
dropping them; ~0 means they arrive late. Those need opposite investigations.

#### P11-C. Boot C: retrieve and restore

```bat
copy C:\P11*.* <the flash drive>
ren C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.OBS
copy C:\XHCI\XHCI98.SAV C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS
fc /b C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS C:\XHCI\XHCI98.SAV
```

Put `XhciLogDebugView` back to 0 in Regedit, and send the final `fc /b` line
and the registry value with the results.

#### What a pass is, decided in advance

A working replug is not the reading; that is the error this investigation made
three times.

```bat
scripts\local\regen-offsets.cmd
scripts\local\readsnap.py P11A5.BIN --ladder
```

The pass, all four clauses: the recipe ran five times with no wedge;
`CommandAgeResets` and `ResetControllerCalls` are 0; no `ctrl.failed.here`
appears in the note ring; the replug's `port.connect` does appear in it. The
new counters say which half of the repair carried it:

| Reading | What happened |
|---|---|
| `CommandAgeAborts` 0, `CommandAgeResets` 0, `ResetControllerCalls` 0 | nothing escalated. The Stop Endpoint completed on its own, and neither repair was exercised, so this pass proves nothing about either of them |
| `CommandAgeAborts` >= 1, `CommandAgeResets` 0 | 13-R.2 carried it: the command hung, the poll wrote CA, the ring stopped, no controller reset was asked for |
| `ResetControllerCalls` == `RecoveryCompletions`, both >= 1 | 13-R.1 carried it: it still latched, and it came back. Clause 2 fails and that is still a pass on the batch's terms (the roadmap asks for "no longer reaches `ControllerFailed` or the latch is no longer terminal") |
| `ResetControllerCalls` >= 1, `RecoveryCompletions` 0 | the latch is still terminal. `RecoveryAttempts`, `RecoveryFailures` and `RecoveryLastStep`/`RecoveryLastStatus` say where the reinitialization refused |
| `RecoveryStaleCallbacks` >= 1 | a recovery callback found nothing to do. Expected if something else recovered the controller first; unexplained otherwise |
| `DmaFailClosedDeferred` >= 1 | a recovery attempt could not prove the controller had stopped. Not a bugcheck on this path by design, but the evidence a later corruption would need; report it |

The note ring is the ordering evidence. Read it for `cmd.age.abort`,
`ctrl.failed.here`, `ctrl.recover.begin`, `ctrl.recovered` and
`ctrl.recover.refused`, in order, against the `port.connect` /
`port.disconnect` pairs around them. The ring is 16 KB and holds roughly 700
records; one full cycle produced 89, so five cycles fit without wrapping, but
check `Log.Used` and the wrap accounting anyway.

One question this boot was also owed, and it had to come home as a question:
why the E460's Stop Endpoint did not complete, and why its 5 s watchdog
produced no verdict. The wedged dump reads `CommandsTimedOut 0` with
`CommandsAfterFailure 1`: the watchdog callback arrived, but only after the
32 s age detector had already latched, more than 32 s after a 5,000 ms timer
was armed. If the repair means the wedge never forms, this boot cannot answer
it either, and the honest outcome is to record it as still open.

### The pre-trip report

P1, P2 and P3 are batch 13-H's readings, filled from
`docs/contributing/test-equipment.md`, which is the authority for every value
below; this box is the trip's copy.

```text
P1 multi-TT hub:    VID:PID=1A40:0201   protocol=2   alt1/protocol2 y
P1 single-TT hub:   VID:PID=1A40:0101   protocol=1
P1 H4 hub:          VID:PID=05E3:0608   protocol=1   two cascaded chips
                    downstream child socket=          chip/tier=
P1 mouse speed:     LS  (046D:C077, Low 1.5 Mbps; int IN 4 B, bInterval=10)
P2 audio device:    bInterval=1    IAD-grouped n    VID:PID=041E:323D
P2 X4 (clause 3):   bInterval=1,3,4   UAC 2.0   High Speed   VID:PID=041E:3278
                    carry it and find out whether a UAC 1.0 target binds it
P3 FS 1.1 hub:      DECIDED before the trip - published as untested ground, no purchase
P4 SE CD:           confirmed SE y (project owner)
                    USB components on the disc: y - VERIFIED BY HASH;
                    usbd.sys 18912 = 0118DB14..F56A, the same
                    build the package's usbd98.sys came from
                    CD contents also copied onto the E460's own disk, so
                    stage E5 installs from hard disk, not from the drive
P5 version:         0.0.0.3, cut before the trip so that this trip and the
                    the Windows 2000 trip would have carried one version. DriverVer=08/22/2026,0.0.0.3
P5 build:           flavour=release   stamp=2026-08-22 07:25:44 UTC
                    (15:25:44 +08 - the PE link stamp, because a release
                    build carries no "DriverEntry (built ...)" banner)
                    build passed y - both flavours, every gate, and the
                    host suite at 18,068 checks / 0 failures
P5 package:         passed y          package path=out\pkg-release
                    release cut: releases\0.0.0.3\ and out\upload-0.0.0.3.zip
P5 pre-stage:       current package y      known-good package y
                    XHCIQUAL.EXE + XHCIQUAL.MAP + five batch files y
                    (qualifier rebuilt at TOOL_VERSION 0.0.0.3 - NOT the
                    build that ran E0; see MANIFEST.txt)
                    kit=out\bench-13e\ - driver-release\ (install this),
                    known-good\ (the published 0.0.0.2 package), and
                    known-good-7bm\ (what the machine runs today)
P5 hashes:          list path=out\bench-13e\MANIFEST.txt   every travelling
                    binary covered y
E0 qualifier:       RUN in bench session 1 - QUALIFIED, T=port 1, FSC=0
                    (xhciqual/results/e460-2026-08-22/, not a P-item but the
                    trip cites it)
P14 kit:            RE-STAGED at 0.0.0.5 for the SECOND session,
                    kit=out\bench-13e-0005\ - see item P14, which supersedes
                    the P5 lines above for anything that travels from now on.
                    P5's kit is 0.0.0.3, pre-repair, and stays home
```

P4's second clause, that the disc's USB components are on it, closed at bench
session 1 by hash rather than by a directory listing (see P4). P5 ran the same
day: both flavours built and gated, the release flavour packaged, and the kit
staged at `out\bench-13e\` with `MANIFEST.txt` carrying the SHA-256 of every
binary that travels. Release `0.0.0.3` was cut the same day, upload asset
`out\upload-0.0.0.3.zip`.

Two things P5 turned up. A release build has no build banner: `DriverEntry
(built ...)` is inside `XHCI_DBG_TEXT` and compiles out, so stage E1.4's stamp
is the PE link timestamp recorded above and its evidence is the Device Manager
screenshot plus the SHA-256. And `0.0.0.3` is `0.0.0.2`'s release code: the
only source commits since that cut are the `MTT`/`TTT` context-field
`XHCI_DBG_VALUE_CHANGED` sites, which are debug-only, and two comments, so the
travelling `xhci98.sys` differs from the `0.0.0.2` release binary in
21 bytes (four of version number, one build timestamp repeated at five places,
and the PE checksum). The bump was cut for consistency across the two planned
bench trips, at the project owner's direction; the debug flavour did change.

The qualifier moved with the version. `TOOL_VERSION` in `xhciqual\qual.h`
tracks the package version, so `XHCIQUAL.EXE` was rebuilt before the cut, and
the kit's qualifier is not the binary that ran stage E0. E0's own
`XHCIQUAL.MAP`, archived in `xhciqual/results/e460-2026-08-22/`, is the
0.0.0.2-era build's. This matters only if the qualifier is re-run at the
bench: say which build produced the log.

### P13-CLOCK-0. The instrument check on the 2a guest, and a poll rate on a second machine

P11-0's "prove the instrument still works before spending a bench boot on it",
at the new extension size. One cold boot of the 2a guest,
`scripts\local\qemu-win98-run-13psnap.cmd`, with the debug `XHCI_OBS_SNAPSHOT`
build. The swap is identified from the driver's own trace:

```text
xhci98: DriverEntry (built Aug 24 2026 23:59:30)
xhci98: MiniPortExtensionSize=000161E8          (= 90,600)
```

The channel answers and all four controls hold at the new size, the same
0 / 2 / 4 / 7 the derivation predicted:

```text
route probe - IOCTL 0x00220438 on this controller:
  PassThru, our GUID                 status  0 (success)
    the miniport ANSWERED - this driver has snapshot support
  unknown request code 15            status  2 (invalid request code)
  RequestBufferLength disagreeing    status  4 (invalid header parameter)
  a 0x20-byte buffer                 status  7 (buffer too small)
```

The windowed read works at 90,600, which is what the boot existed to check;
that arithmetic had only run at 87,592 and 87,644:

```text
  C:\pre.BIN: 90600 bytes in 2 window(s) (region is 90600)
  ExtensionBytes = 90600
coherence: tear detector 294, unchanged across every window - the dump is coherent
```

The header's tear detector is 294 and `CheckCallbacks` decoded out of the dump
body is 294. The healthy PORTSC baseline is unchanged: all four ports
`000002A0`, `PP = 1`, `PLS = 5` (RxDetect), no change bits. Dump and
screenshots in `vm\13r35-qemu\`; the pre-swap control trace, where the route
worked and the driver correctly answered status 6, is
`vm\win98-debugcon.13r35-preswap-old-driver.log`.

#### The measurement it gives away for free

| | |
|---|---|
| `PollClockMs` | 136,285 |
| `HealthPolls` = `CheckCallbacks` | 294 (equal, so no poll was declined) |
| `PollClockStalls` | 0 |
| intervals | 293 |
| ms per poll | 465 |

136 seconds is the driver's real uptime (the guest clock read 4:12 at the boot
screenshot and 4:14 at the dump), so the clock is measuring time and not
calls, which is the property the poll-count design lacked and the repair rests
on. And 465 ms is essentially usbport's nominal 500 ms.

The same instrument has therefore measured the `CheckController` period on
two machines: 465 ms on the 2a guest and 36-80 ms on the E460 (Finding V; an
earlier inference of ~1 ms from Finding U's poll count was withdrawn by that
direct measurement). A spread of 6-13x is task 13-R.3.5's premise stated as a
measurement: the poll rate is not a constant, so a threshold counted in polls
is a threshold whose size the host decides. It is also why "just raise the
count" was rejected. The old 64-poll budget was a correct 30 s on this guest
while it was 2.3-5.1 s on the bench machine.

The ladder is quiet, as a healthy control should be: `CommandAgeAborts`,
`CommandAgeResets`, `CommandAgeEscalated`, `DevicesStalledOut`,
`EndpointRestartsByPoll`, `ResetControllerCalls` and the whole `Recovery*`
family all 0, with `CommandsIssued` 1 / `CommandsCompleted` 1 (the No Op
self-test) and `CommandAgeStamp` equal to `PollClockMs`, an idle engine
re-stamping every poll.

This does not close task 13-R.3.5: nothing here made a command hang, so the
ladder never ran. That is P13-CLOCK.

### P13-CLOCK. The reading task 13-R.3.5 was owed

This reading was taken at bench session 3 and is Finding V above, which
includes the correction to the ~1 ms period this plan assumed. The procedure
is a record. Task 13-R.4 then removed the `XHCI_OBS_SNAPSHOT` channel, and task
13-L.2 put it back in every shipping flavour behind a registry value, so
re-running it today needs an installed binary and `XHCISNAP -verbosity N`
rather than a rebuild.

One boot, the same recipe, looking for the inverse of every dump taken so
far: Finding U closed the diagnosis, this closes the repair. The two standing
rules for this machine hold: never install by INF (`ren` + `copy`), and every
reboot is a full power-off cold boot.

#### P13-0. On the host, before the trip

```bat
scripts\local\build-obs-snapshot.cmd
scripts\local\regen-offsets.cmd
```

Both, in that order, from the same tree. The instrument is
`out\bench-13e-wedge\OBSSNAP.SYS`, 83,867 bytes, sha256 `27180ecc...`,
`MiniPortExtensionSize` 90,600. `dir` separates it from every earlier
generation (82,475 / 83,579 / 83,611) but not from a stale build: one staged
late in the repair session, two minutes before the fifth threshold
(`XHCI_EP_RESTART_MS`) was converted, carried `sizeof(XHCI_EXTENSION)` =
89,960 at the same 83,867 bytes. Nothing was read with it, and `readsnap.py`
would have refused it, but it would have cost a bench boot. Check the hash,
not the size.

The instrument check is P13-CLOCK-0 above. Carry: `OBSSNAP.SYS`,
`XHCISNAP.EXE`, the Low-Speed mouse, a High-Speed flash drive, and a second
flash drive for the dumps.

#### P13-A. Boot A: identify, install, turn the note ring on

```bat
cd \XHCI
fc /b C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.SAV
```

`no differences encountered` identifies pristine `0.0.0.4`. Anything else:
write down which build is there.

```bat
copy C:\XHCI\OBSSNAP.SYS C:\XHCI\OBS0825.SYS
ren C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.PRE
copy C:\XHCI\OBS0825.SYS C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS
fc /b C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS C:\XHCI\OBS0825.SYS
copy C:\XHCI\XHCISNAP.EXE C:\
```

Regedit, Edit -> Find, `XhciLogDebugView`, set it to 1, so the ring fills
inside the extension. **Never set `XhciLogFile`**: it reaches `ZwCreateFile`
on the boot path, where task 11-V.7 measured an open that never returns. Shut
down properly and cold boot.

#### P13-B. Boot B: the whole session, with no reboot in the middle

B1. Before touching any USB device:

```bat
C:\XHCISNAP -probe > C:\P13P1.TXT
more < C:\P13P1.TXT
```

`the miniport ANSWERED` identifies the running binary.

B2. The healthy control, before any USB device is touched:

```bat
C:\XHCISNAP -o C:\P13H > C:\P13H.TXT
more < C:\P13H.TXT
```

Read `ExtensionBytes` first. It must be 90600; 87,644 means the machine booted
the P11-RETRY binary whatever `fc /b` said on disk. Check the last line says
`the dump is coherent`. Expect the recorded baseline: ports 1-5 and 9-12
`000002A0`, ports 6/7/8 connected and enabled, ports 13-18 `PP = 0` by design,
every ladder counter 0.

B3. The recipe, five times, at position D, nothing else attached:

```text
per cycle:
  1. High-Speed flash drive -> D    enumerates    unplug
  2. Low-Speed mouse        -> D    enumerates    unplug
  3. plug the mouse back into D                   <-- the old build died HERE
     confirm it enumerates, then unplug it
```

Time step 3 with a stopwatch on every cycle and write the number down. Before
task 13-R.3.5 the whole ladder ran in tens of milliseconds; now every rung is
a real interval, so the delay names which rung fired:

| Delay at step 3 | What it fingerprints |
|---|---|
| immediate | nothing escalated; the Stop Endpoint completed on its own |
| ~5 s | the command's own watchdog fired (`XHCI_COMMAND_TIMEOUT_MS`), the rung that had never once been observed |
| ~20 s | the full ladder: watchdog, then `XHCI_COMMAND_ABORT_WAITS + 1` abort intervals |
| ~32 s | the age backstop still beat the watchdog: the repair did not take, and `PollClockMs` against `HealthPolls` is the first thing to read |
| every USB device drops and returns, ~60-90 s | task 13-R.1: it latched and the in-place recovery reinitialized the controller |
| nothing, ever | the wedge survives. Take the dump anyway |

The fifth row looks like a crash and is not one (see P11-B). Do not
power-cycle out of alarm. Take an interim dump after cycle 3, because a wrap
eats the earliest cycles:

```text
after cycle 3:   C:\XHCISNAP -o C:\P13A3 > C:\P13A3.TXT
```

B4. The reading, with the machine still up:

```bat
C:\XHCISNAP -probe > C:\P13P2.TXT
C:\XHCISNAP -o C:\P13A5 > C:\P13A5.TXT
more < C:\P13A5.TXT
```

Check `Log.Used` and `Log.BytesDropped` in both dumps: a nonzero
`BytesDropped` in the second means take the ordering out of the first. Then
shut down cleanly.

#### P13-C. Boot C: retrieve and restore

```bat
copy C:\P13*.* <the flash drive>
ren C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.OBS
copy C:\XHCI\XHCI98.SAV C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS
fc /b C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS C:\XHCI\XHCI98.SAV
```

Put `XhciLogDebugView` back to 0, and send the final `fc /b` line and the
registry value with the results.

#### What the reading has to show

One ordering claim, both halves needed:

| Reading | What it means |
|---|---|
| `CommandsTimedOut` >= 1 with `CommandAgeAborts` 0 and `CommandAgeResets` 0 | the pass. The command's own 5 s watchdog reached it first, the order the ladder was designed in and which no dump had ever shown |
| `CommandAgeAborts` >= 1 with `CommandsTimedOut` >= 1 | also a pass, and a better one: the watchdog fired, the abort did not resolve it, and the backstop caught what was left. The rungs ran in order |
| `CommandAgeAborts` >= 1 with `CommandsTimedOut` 0 | the failure this boot is looking for. The backstop is still beating the watchdog; report `PollClockMs`, `PollClockStalls`, `HealthPolls` and `CheckCallbacks` together |
| `PollClockStalls` climbing against a live bus | the clock could not read MFINDEX. Expected around a recovery; a large number is a finding of its own |

Two counters come free with it. `CommandTimeoutArrivals` should now be
roughly `CommandsTimedOut` plus the stale ones, rather than equal to
`CommandsIssued` with `CommandsTimedOut` at 0. `RhAgeRetires` should be 0 on a
healthy boot; a nonzero value is a real lost timer. (With the measured poll
period the port age stands at 0.6-1.3 s against a 500 ms reset deadline, a
margin of 1.2-2.6x where 16x was intended.)

The number to write down whatever else happens:

```text
PollClockMs / (HealthPolls - 1)  =  this machine's CheckController period, in ms
```

Finding U derived ~1 ms for the E460 indirectly, from a poll count against a
session length nobody timed. This measures it directly: `PollClockMs` is a
real elapsed time and `HealthPolls` is beside it in the same dump. Report
both, plus `CheckCallbacks` and `PollClockStalls`. The 2a guest measured
465 ms by this arithmetic. The E460 came back at 36-80 ms (Finding V), not
~1 ms; the 6-13x spread still makes the argument, and the 971,359 poll count
the inference came from is carried as an open question in Finding V.

The boot also re-opens Finding S's headline. "A Stop Endpoint that never
completes" was established only inside a window three orders of magnitude
smaller than its author believed. If a Stop Endpoint on a Low-Speed periodic
endpoint now completes where it used to be aborted, the headline is refuted
rather than qualified; if it still does not complete inside 5 s, Finding S
stands with its window corrected. Neither answer may be inferred from the
machine working. (Finding V: it completes, 123 of 123.)

### P14. Re-stage the kit at `0.0.0.5`, and settle how it gets onto the machine

Desk work on the modern host. The kit is staged at `out\bench-13e-0005\` and
its `MANIFEST.txt` is the hash list. Nothing was rebuilt: `driver-release\` is
a copy of the published asset's `release\` directory, and each of its five
SHA-256s was checked against the entry inside `out\xhci98-0.0.0.5.zip`.

P5's kit cannot travel again. It carries `0.0.0.3`, which predates the
Finding 3 repair, and everything batch 13-E still owed at the time (stage E1's
four hashes, E4.1's root half, E4.2's I/O half, E4.3's retake, and stage E6)
is plug-and-unplug work at position D. Finding G's five-event recipe is that
work written out: a flash drive, then the Low-Speed mouse, then the flash
drive again, at one root port. A session run on the old kit would not risk the
wedge; it would perform it, and every reading after it would be void rather
than negative, which is what happened to stage E4.3 at bench session 2.
`out\bench-13e\` stays on disk as a comparison path.

The kit has a fifth file. P5's kit had four (`xhci98.sys`, `xhci98.inf`,
`usbd98.sys`, `usbd2k.sys`); this one adds `usbhub98.sys`, Windows 98 SE's own
composite parent driver, the file Finding D found missing and the shipping
answer to task 13-E.1. The INF copies it to `usbhub.sys` with
`COPYFLG_NO_OVERWRITE`, and the E460 already has that file by hand-copy since
bench session 2, byte-identical to the kit's (`e898b75f...`, the same SE CD
file), so the composite stays bound whatever route is taken.

The kit also carries a `reference\` directory, because boot 0 could not
otherwise be run. There is no published reference on the machine: `C:\XHCI\`
holds the three instrument candidates and nothing else, and no `XHCI98.SAV`
was ever found. Three published release builds are all 81,899 bytes
(`0.0.0.2`, `0.0.0.3` and `0.0.0.4`, different code each time), so `dir`
cannot name which is live. The directory carries all three plus the `0.0.0.4`
debug build as `R0002/R0003/R0004/D0004.SYS`, with `IDENTIFY.TXT` holding the
size table and the `fc /b` sequence. `0.0.0.5` is the one release size that
is distinguishable at a glance (83,419).

#### The install route, and it is not an install

Swap the `.sys`. Do not install the INF. Every INF update over an existing
install on Windows 98 bugchecks at `0028:C00312EE`, because stopping the
running driver is that fault (`lessons.md`, "Swap a Win98 driver binary by
rename, never by INF"). From a DOS box:

```bat
ren  C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS  XHCI98.OLD
copy <kit>\driver-release\XHCI98.SYS         C:\WINDOWS\SYSTEM32\DRIVERS\
```

then a cold start. Nothing else in `driver-release\` needs to move: the
`0.0.0.5` INF differs from `0.0.0.4`'s in `DriverVer` and one comment block
only (diffed), and the other three files are already in place.

The identity evidence changes with the route. The driver key still names the
INF that installed the machine, so Device Manager -> Driver File Details reads
the old version after a successful swap. The witnesses this time are:

| Check | Reading |
|---|---|
| `dir` | 83,419 bytes. `0.0.0.3` and the `0.0.0.4` release are both 81,899, and the repair grew the binary. Two builds of the same version are the same size, so this is a screen, not the proof |
| `fc /b` against the kit copy | the actual check, used for every candidate swap on this machine |
| SHA-256 | taken on the host, off the copy that travelled: `44d3edb0...4df4f` |

A user-style install from the zip is the acceptance run's act, on `1.0.0.0`,
through `docs/using/release-acceptance-test.md`, on this same machine.
Spending it on `0.0.0.5` would risk the bugcheck above and consume the one act
that run exists to observe. The zip rides in the kit as the identity of what
`driver-release\` came from, not as an installer.

RUN box, filled at bench session 3:

```text
P14 kit:           out\bench-13e-0005\    MANIFEST.txt y
                   driver-release\ = the asset's release\, five files,
                   hashes checked entry-by-entry inside the zip y
P14 version:       0.0.0.5, cut at commit 6e209cd
                   DriverVer=08/25/2026,0.0.0.5
P14 build:         flavour=release   83,419 B
                   stamp=2026-08-25 08:00:33 UTC (= 16:00:33 +08, PE link)
                   sha256=44d3edb002c73f7717dab55bba2ee9df140d9b56dd9c22b8218113b22ec4df4f
P14 route:         DOS-box ren + copy of XHCI98.SYS alone, cold start
                   INF install REFUSED - 0028:C00312EE on Win98
                   identity by dir 83,419 + fc /b, NOT Driver File Details
P14 fallback:      XHCI98.OLD, left in place by the ren. No revert package
                   travels: 0.0.0.4 and 0.0.0.3 both carry the Finding 3 wedge
P14 qualifier:     XHCIQUAL.EXE/.MAP at TOOL_VERSION 0.0.0.5 + the five batch
                   files, riding as a spare - stage E0 is DONE and owes nothing
P14 boot 0:        RUN before anything was touched.
                   live XHCI98.SYS = 81,899 B
                   fc /b -> NO DIFFERENCES against reference\R0004.SYS
                   => the machine was carrying PRISTINE 0.0.0.4, confirming
                      what had only been reported since the 13-R boots
                   XhciLogDebugView = 0
                   dir C:\XHCI listing NOT TAKEN - declined at the bench, and
                   it stopped being worth taking the moment the fc /b named
                   the live binary outright. The candidates in that directory
                   are still recorded by name only
```

---

## Stage order, and why it is not the task order

| Stage | Task | Why here |
|---|---|---|
| E0 | 13-E.4 | The qualifier is a DOS tool needing cold boots between stages, so it goes first, before any Windows session. It is also the free `FSC` ride-along for task 12.1. |
| E1 | 13-E.4 | The identity row every later result is attributed to. Cheap, and worthless if taken afterwards. |
| E2 | 13-E.1 | The composite device on plain NUSB. This is the control stage E5 is read against, and E5 destroys the baseline it is taken from. |
| E3 | 13-E.2 | Hub topology and its deepest-tree photograph on the documented baseline. |
| E4 | 13-E.4 | The free re-observations, still on the baseline. |
| E6 | 13-E.3 | Audio, before E5. Finding D bound the composite on the current baseline, so E6's clauses 1-2 do not wait on E5, and E5 is one-way, so anything reachable without it precedes it. Clause 3 is the X4 and never needed E5. |
| E5 | 13-E.1 | The SE CD install: one-way, and optional. Task 13-E.1 closed at bench session 2 without it. Run it last or not at all. |
| L3 | 13-L.3 | A different batch and a different session. It swaps the installed binary four times, so a machine part-way through it is not the machine an E stage is read against. It carries its own boot order and its own restore. If stage E5 is ever run, L3 must come first. |

The roadmap's original rule was to run task 13-E.1's cheap branch first and
only then attempt 13-E.3, because their device is one physical unit and the
audio clauses needed the composite bound, which only E5 was expected to
deliver. At bench session 2 the composite bound on the plain-NUSB baseline as
soon as `usbhub.sys` was copied onto the machine by hand, so E6.1 and E6.2
have a device to listen to without E5. Since E5 is one-way, "reachable
without it" means "taken before it".

13-E.1's cheap branch is only interpretable against a same-session control on
this machine, because no real composite audio device had ever been presented
to it. The earlier bare-metal `Code 2` was a multi-interface HID, itself a
composite by row 1's definition (`build-and-test.md` records it coming up as
`USB Composite Device` on a root port and behind a hub), so what was missing
was that reading on the class of device stage E6 needs. The control is stage
E2, taken while the stack was still plain NUSB.

---

## Stage E0 - the qualifier pass, and this controller's fact sheet

Task 13-E.4, third bullet. Nothing binds in this stage; the machine boots DOS.

Boot clean MS-DOS 7.1 into a writable directory holding `XHCIQUAL.EXE`,
`XHCIQUAL.MAP` and the five batch files, with external USB test devices
disconnected. `xhciqual/hardware-testing.md` is the procedure; this is its
short form.

1. `XHCIQUAL` with no arguments, the read-only quick scan. Three outcomes:
   `LOOKS QUALIFIED`, `DISQUALIFIED`, `CANNOT SAY`.
2. `1PROBE` -> `PROBE.LOG`. Archive, power off completely, cold boot.
3. `2XPOLL` -> `XPOLL.LOG`. Expect C4 `SKIP` and verdict `PROVISIONAL`. Archive, cold boot.
4. `3XIRQ` -> `XIRQ.LOG`. Require `IRQ SELF-TEST PASS` and `Done.`. Archive, cold boot.
5. `4XEMPTY` -> `XEMPTY.LOG`. Expect xHCI C4 `PASS`. Archive, cold boot.
6. Attach the USB 2.0 flash drive to position D (this machine's left-hand
   connector, controller port 3); `5XDEV` -> `XDEV.LOG`.
7. Map position T. This machine has three USB-A connectors: one on the left
   (the Always On one, controller port 3) and two on the right, identical to
   look at. Move the same device into one right-hand socket and repeat the
   attached-device run into `XDEV2.LOG`, then do the other into `XDEV3.LOG`.
   Label the socket chosen as position T physically. Without this, every tree
   result in stage E3 names a hub on a port nobody can identify.

   Done: position T is the right-hand screen-side socket, controller port 1,
   physically labelled. All three external sockets were mapped: left = port 3
   (D), right screen-side = port 1 (T), right user-side = port 2 (spare). The
   screen-side one was chosen because T holds the hub and its PSU cable all
   session, and a knocked hub latches `DeviceFailedEnumeration` on everything
   behind it.

   `5XDEV.BAT` deletes `XDEV.LOG` on every run (`if exist XDEV.LOG del
   XDEV.LOG`), so rename between runs. This stage's logs were renamed after
   the fact, so `XDEV2.LOG` and `XDEV3.LOG` both end `Report copied to
   XDEV.LOG.`; read the socket off the `DEV port=` line, never off the
   filename. Read the port off the VID/PID, not off position in the list:
   three internal devices answer on every run (ports 6, 7, 8), and the drive
   is the `vid=0781 pid=5408` line.

The pass: every batch reaches `Done.`, the run's verdict is recorded, and
position T has a controller port number. **A `DISQUALIFIED` from the quick
scan ends the session for that controller**; nothing in the staged runs can
overturn it.

On this machine `4XEMPTY` reports three connects, not `C6 SKIP`: ports 6, 7
and 8 are internal and cannot be unplugged. The generic "expect C6 SKIP with
nothing connected" advice in `hardware-testing.md` is about an empty
controller, and this one is never empty. Three connects there is a pass.

Log the quick scan with `--quick`, not with `--log` alone. The scan triggers
on `argc == 1`, so `XHCIQUAL --log QUICK.LOG` would perform the full active
bring-up instead (BIOS handoff, `HCRST`, DMA and port resets). The read-only
spelling is `XHCIQUAL --quick --no-page --log QUICK.LOG`.

Take the `FSC` ride-along while here: read `HCCPARAMS2` off the capability
sheet, or `fsc=` in the `FACT` line. It is not a clause of this batch (task
12.1 closed on its publish branch), but it is the only thing that could ever
identify a machine able to exercise the successful `CSS`/`CRS` path.

This is a single-controller pass. The one-screen claim, the extender-line
budget and the multi-controller overflow case need a machine with more than
one USB controller, which no fleet machine has; they are published as
unreachable.

Keep all logs with the `XHCIQUAL.MAP` of the exact build that produced them,
under `xhciqual/results/e460-<date>/`. The `.EXE` itself is not archived: the
MAP travels with the logs and the build is pinned by the version and build
stamp the log banners carry (the quick scan prints the version without the
stamp, so that log is pinned by the version and the run record), per the
`e460-2026-08-22/` precedent. It does not rebuild byte-identically, because
the stamp embeds the build time.

RUN box, filled in bench session 1 (this stage is done and is not repeated on
a later visit):

```text
session:             bench session 1   logs: xhciqual/results/e460-2026-08-22/
build stamp:         XHCIQUAL 0.0.0.2 (build Aug 21 2026 23:59:58)
E0.1 quick scan:     LOOKS QUALIFIED            (QUICK.LOG, --quick --log)
E0.2 PROBE.LOG:      archived y
E0.3 XPOLL.LOG:      verdict=PROVISIONAL (C4 SKIP, expected)   archived y
E0.4 XIRQ.LOG:       IRQ SELF-TEST PASS y      Done. y      (ISR on IRQ 11)
E0.5 XEMPTY.LOG:     xHCI C4=PASS               archived y
E0.6 XDEV.LOG:       archived y  - holds the LAST run (drive on port 2, the
                     right user-side socket); step 6's position-D run is
                     XDEV2.LOG (DEV port=3), position T's is XDEV3.LOG
                     (DEV port=1) - read DEV port=, never the filename
E0.7 position T:     socket physically labelled y    controller port=1
     also mapped:    left (D) = port 3, right user-side (spare) = port 2
     stickers:       D and T both labelled; the spare is the unlabelled one
FSC ride-along:      HCCPARAMS2=00000000        fsc=0
BIOS:                R00ET65W (1.40); "USB UEFI BIOS Support"
                     Enabled is the ONLY USB option this BIOS offers
verdict:             CONTROLLER QUALIFIED for cross-target driver development
```

Three things this run settled that the sheet did not ask for, all in
`xhciqual/results/e460-2026-08-22/README.md`:

- All three external sockets were mapped, not just T. Each extra socket costs
  one cold boot and removes a question permanently.
- Port 6 carries an `8087:0A2B` Intel Bluetooth radio: internal, unpluggable,
  absent in the earlier qualification run and present in every stage here. So
  this machine has three permanent internal devices, and `4XEMPTY` can never
  report `C6 SKIP` here.
- The BIOS has no USB knob that can vary between sessions: no Legacy USB, no
  handoff, no routing option. Unlike an Intel 7/8-series machine, where a
  setting such as `USB 3.0 Mode = Auto` makes a working driver look dead, no
  BIOS setting on the E460 can be blamed for a bad result.

---

## Stage E1.0 - deploying onto a from-scratch install

Added in bench session 1. No task owns this stage; it exists because the sheet
assumed a machine that already ran the driver, and the reinstall made that
false. Skip it only on a machine that demonstrably already carries NUSB and
the current build.

Nothing here is a clause. Every reading is a precondition for stage E1's
identity row meaning anything, and the pre-install check in step 1 is
irrecoverable: it stops being observable the moment anything is installed,
and stage E5's meaning depends on it.

Order matters: NUSB before the driver, because `xhci98.sys` is a
`usbport.sys` miniport and its imports cannot resolve without one. A `Code 2`
on the controller devnode is what skipping this looks like.

1. Record the untouched state, before installing anything. In a DOS box:

   ```bat
   dir C:\WINDOWS\SYSTEM32\DRIVERS\usbd.sys
   dir C:\WINDOWS\SYSTEM32\DRIVERS\usbhub.sys
   dir C:\WINDOWS\SYSTEM32\DRIVERS\uhcd.sys
   dir C:\WINDOWS\SYSTEM32\DRIVERS\usbport.sys
   ```

   All four absent is the expected reading on a machine whose only USB
   controller is xHCI: Windows 98 installs `usbd.sys` only with the USB 1.1
   stack, and setup cannot have detected a controller it has never heard of.
   If `uhcd.sys` or `usbhub.sys` are present, stop: setup installed the native
   stack anyway, stage E5 has effectively already happened, and stage E2's
   plain-NUSB control is not one.

2. Record the unclaimed devnode. The xHCI device should sit under Other
   devices as `Universal Serial Bus (USB) Controller` or `PCI Device`, yellow
   `?`, Code 1, PnP ID `PCI\CC_0C0330`. Photograph it; it is the "before" of
   the whole session.

3. Confirm the four staged items are on the machine's own disk, because after
   this the transfer route may not survive its own outcome: `nusb33e.exe`
   (`tools/`), the `driver-release\` files, the `\WIN98` CAB directory, and
   the ASIX `AX88772` driver for stage E4.4. On an xHCI-only machine with no
   USB stack yet, the pulled disk is the only guaranteed route; the storage
   enclosure in this batch's bag is what it is pulled into.

4. Install NUSB 3.3 in full, as-is (project owner's decision;
   `build-and-test.md`, "Installing the usbport USB 2.0 Stack"). Reboot. Then
   verify placement in `SYSTEM32\DRIVERS`, not `C:\WINDOWS\SYSTEM`; the wrong
   directory once produced a false negative that stood in these docs for three
   days:

   ```text
   usbport.sys    expect 135,920 bytes
   usbhub20.sys   expect  50,032 bytes
   ```

   The xHCI device is still Code 1 at this point, correctly: NUSB's `USB2.inf`
   binds EHCI only and leaves `PCI\CC_0C0330` alone.

5. Install the driver by INF, not by copying the `.sys`. Every file of the
   flavour's directory in one directory, then Device Manager -> the yellow
   `?` device -> Update Driver -> that directory. This step also supplies
   `usbd.sys` (the INF copies `usbd98.sys` to it, no-overwrite):
   `usbhub20.sys` imports it, NUSB does not ship it, and step 1 proved the OS
   did not install it either.

   It was four files when this stage ran and it is five from `0.0.0.4` on:
   `xhci98.inf`, `xhci98.sys`, `usbd98.sys`, `usbd2k.sys` and `usbhub98.sys`,
   the last copied to `usbhub.sys`, also no-overwrite. That fifth file is
   Finding D's fix and it is why a machine installed from current media binds
   composites without anything being copied by hand. This applies to a first
   install only: an INF install over an existing one bugchecks Windows 98 at
   `0028:C00312EE`, so a later version reaches this machine by the
   `ren`+`copy` swap of item P14.

6. Cold start. Full power off, not the reboot Windows offers.

7. One Device Manager -> Refresh per boot, before plugging anything in. The
   controller idle-suspends and will not notice a newly attached device until
   refreshed; this is open defect 11-V.6 and it looks like a dead plug.

The pass: all four files absent in step 1, the devnode at Code 1 in step 2,
both NUSB files at the expected sizes in step 4, and the controller named
without a bang after step 6, which is stage E1.5.

RUN box, filled in bench session 1:

```text
E1.0.1 pre-state:    usbd.sys ABSENT   usbhub.sys ABSENT   uhcd.sys ABSENT
                     usbport.sys ABSENT
                     -> the SE CD's USB 1.1 stack was NOT installed by setup,
                        so stage E5 remains a genuine one-way change
E1.0.2 devnode:      Other devices, yellow ?, Code 1 y     photo y
E1.0.3 staged:       nusb33e.exe y   driver-release\ (4 files) y
                     \WIN98 CAB directory y   AX88772 ASIX driver y
E1.0.4 NUSB:         3.3 installed in full y   rebooted y
                     usbport.sys 135,920 B y   usbhub20.sys 50,032 B y
E1.0.5 driver:       installed by INF from the four-file directory y
E1.0.6 cold start:   y
E1.0.7 refresh rule: in force for the session
```

---

## Stage E1 - the identity row

Task 13-E.4, first two bullets.

Boot Windows 98 SE and record, as one row, the four facts every later result
in this session is attributed to:

- OS: Windows 98 SE, and whether this is the install batch 7b-M used or a
  later restore.
- Stack build: NUSB 3.3 present, with `usbport.sys` and `usbhub20.sys` in
  `C:\WINDOWS\SYSTEM32\DRIVERS`, version and, if a copy-out path exists, the
  SHA-256. NUSB's `_NUSB.INF` places them unconditionally, so no EHCI is
  involved and none exists here.
- Controller ID: from stage E0's fact sheet, tied to the same
  `xhciqual/results/e460-<date>/` directory.
- Driver identity: flavour, build stamp, and the SHA-256 of the deployed
  `xhci98.sys`. On this machine the `DriverEntry (built ...)` banner is
  unreadable, so Device Manager -> Driver -> Driver File Details is the
  identity evidence, and a screenshot of it is the record.

The reading: the controller's Device Manager entry is present, named, and
without a bang, and the identity row is written down. If it has a bang, stop
and read "If something goes wrong"; a `Code 2` on the controller devnode
means NUSB is absent, not anything about composite devices.

RUN box, filled in bench session 1. This row records a stack and a driver
deployed during the session rather than found in place; see stage E1.0.

```text
E1.1 OS:             Win98 SE, install= FRESH, reinstalled this session
                     (NOT batch 7b-M's install, which this one replaced)
E1.2 stack:          NUSB 3.3 y
                     usbport.sys  135,920 B - the expected NUSB 3.3 build
                     usbhub20.sys  50,032 B - likewise
                     usbd.sys supplied by THIS package's INF (from usbd98.sys),
                     because E1.0.1 proved the OS had none
                     ver= and sha256= OUTSTANDING: the copy-out of the three
                     files was asked for and not taken before the session paused
E1.3 controller ID:  xhciqual/results/e460-2026-08-22/
E1.4 driver:         flavour=release   stamp=2026-08-22 07:25:44 UTC (PE link)
                     sha256 expected 9e53ac2e..d700e6 - OUTSTANDING, the
                     deployed XHCI98.SYS was not copied out either
                     Driver File Details reads 0.0.0.3 y  <- the identity
                     evidence on this machine, since a release build carries no
                     "DriverEntry (built ...)" banner at all
E1.5 Device Manager: controller named, no bang y    File Details screenshot y
```

Stage E1 passed, with one loose end at the time: the four SHA-256s were not
taken off the machine, and the version-and-size evidence stood in the
meantime. The re-take below closed it.

RUN box, re-taken at bench session 3 on `0.0.0.5` (the P14 swap boot). Only
the driver row changed; the OS, stack and controller are the install stage
E1.0 made at bench session 1. Because the binary arrived by `ren`+`copy`
rather than by an INF install, the driver key still names the INF that
installed the machine and Driver File Details reads the old version; the
identity evidence for this row is `dir` 83,419 bytes, `fc /b` against the kit
copy, and the host-side SHA-256. Every reading taken after that boot belongs
to `0.0.0.5`, and the first two bench sessions' results belong to `0.0.0.3`.

```text
E1.1 OS:             Win98 SE, the bench session 1 from-scratch install, unchanged
E1.2 stack:          NUSB 3.3, unchanged - and now HASHED rather than sized:
                     usbport.sys  135,920 B  eec79b5a..3f9b55
                     usbhub20.sys  50,032 B  fe9de344..82e0d5
                     usbd.sys      18,912 B  0118db14..f56a
E1.3 controller ID:  xhciqual/results/e460-2026-08-22/, unchanged
E1.4 driver:         0.0.0.5 release   83,419 B
                     sha256 44d3edb0..4df4f - the published release build,
                     byte for byte, read off the machine's own copy
                     stamp=2026-08-25 08:00:33 UTC (PE link)
                     arrived by P14's ren+copy swap, NOT by an INF install
                     Driver File Details still reads 0.0.0.4 - BY CONSTRUCTION,
                     the driver key naming the INF that installed the machine.
                     The fc /b and the hash are the identity evidence here
E1.5 Device Manager: controller named, no bang y
```

All four SHA-256s are taken, and each matches a known reference byte for
byte, which authenticates the whole stack on this machine:

| File | SHA-256 | Matches |
|---|---|---|
| `USBPORT.SYS` | `eec79b5a...3f9b55` | `tools\nusb-extracted\USBPORT.SYS` exactly |
| `USBHUB20.SYS` | `fe9de344...82e0d5` | `tools\nusb-extracted\USBHUB20.SYS` exactly |
| `USBD.SYS` | `0118db14...f56a` | the SE CD's own `usbd.sys`, and `usbd-sources.expected`'s `usbd98.sys` row |
| `XHCI98.SYS` | `44d3edb0...4df4f` | the `0.0.0.5` release binary exactly |

Until then the NUSB stack here was evidenced by size alone (135,920 and
50,032), and this project's recurring trap is that the wrong build loads
rather than failing; it is why `usbd.sys` ships under two names checked by
hash (`usbd-sources.expected`), and why `lessons.md` says a restore verified
by size verifies nothing. The `usbd.sys` row closes the same loop from the
other end: the file this package's INF placed is the authenticated SE CD
build, on the machine where stage E1.0 proved the OS had none.

The binaries themselves are not kept. Three of the four are third-party and
`AGENTS.md` forbids tracking them; the hashes are the record, re-derivable by
repeating the copy-out. The working copies sat in the git-ignored `temp\`.

---

## Stage E2 - the composite device on plain NUSB, which is the control

Task 13-E.1, first half. Taken before stage E5 changes the stack.

Present the owner's composite device (USB Audio class and a USB keyboard in
one unit) on a root port, then behind a hub. Both, because the root port is
the control that places any fault in the stack rather than in `xhci98.sys`
or the hub path.

Steps:

1. E2.1 Plug the composite device into position D and let it enumerate.
2. E2.2 Read Device Manager (what appeared, under what name, with what code)
   and photograph a `Code 2` dialog.
3. E2.3 Note the keyboard half separately: does the keyboard type, and which
   of the device's functions appear at all.
4. E2.4 Unplug it, plug it into hub port H3, and take the same reading.

Both outcomes are the measurement. `USB Composite Device` with Code 2 ("the
NTKERN.VXD device loader(s) for this device could not load the device
driver") on the root port and behind the hub reproduces the earlier
bare-metal finding on a real composite audio device rather than on a
multi-interface HID; the audio class is what is new. If it binds instead,
NUSB 3.3 is not the discriminator anybody thought it was and stages E5 and E6
change shape. A single-interface HID binds and works normally under NUSB, so
which functions appear is itself information.

RUN box, filled in bench session 1. Read the substitution line before citing
any of it: this stage was taken on a different unit than the sheet names.

```text
E2.1/E2.2 root port:   name shown=USB Composite Device    code=2     photo y
E2.3 HID half:         NO function surfaced at all - the composite parent
                       failed to load, so nothing beneath it was ever offered
                       ANSWERED on a second substituted unit: the MS Wired
                       Keyboard 600 045E:0750 (Low Speed, two HID interfaces)
                       also reads USB Composite Device Code 2 at D, and IT DOES
                       NOT TYPE - the gap's user-visible consequence, and a
                       broader result than the sheet's audio-only device could
                       have given
E2.4 behind hub:       name shown=USB Composite Device    code=2     photo y
                       same reading either side of the hub
SUBSTITUTION:          the sheet's device (row 1, Play! 2 041E:323D) CANNOT
                       ENUMERATE on this driver at all - see "Finding 2" in the
                       Session record - so the composite control was taken on
                       the C-Media 0D8C:0014: also a Full-Speed UAC 1.0
                       composite, and the independent-vendor specimen of the six
                       characterised by batch 13-H
also read, not E2:     Sound Blaster X4 041E:3278 (High Speed, composite,
                       IAD-grouped) enumerates and the wizard names it, but NO
                       USB Composite Device node appears and its USB HID Device
                       reads Code 10 - a second, different failure shape
```

Stage E2 passed, on the substituted unit, and it is task 13-E.1's anchor
reading: `Code 2` on a real composite audio device, on both topologies, on
real xHCI silicon.

---

## Stage E3 - hub behaviour on real translators

Task 13-E.2. Behavioural only: the `MTT`/`TTT` context-field numbers need a
counter channel and cannot be read on this machine.

The original plan had two cases, both with children attached; P3 closed Case
B on the published-limitation branch, so only Case A runs. The hub under test
is at position T and the children keep their assigned hub ports: the HID at
H1, a flash drive at H2, the composite audio device at H3, a second hub at
H4, and an ordinary child behind H4 on a socket whose chip and tier P1
recorded. Keep that socket and child fixed across the swap, so the two trees
are comparable with the characterisation run.

Case A, the single-TT to multi-TT replacement: enumerate the children behind
the single-TT hub, confirm they work, then replace it with the multi-TT hub in
position T and re-enumerate. `MTT` follows the currently enabled alternate
setting rather than being latched at enumeration, so the second enumeration
is the one that can be wrong.

Case B, a Full-Speed 1.1 hub behind the multi-TT High-Speed hub (where `MTT`
has both of its independent causes at once and the marking must OR rather
than assign), does not run. No hub was bought and none is held, so this
clause ships as untested ground, with the `test/test_init.c` host vectors and
the batch 7b-V0 QEMU measurement as its only evidence. If a Full-Speed hub ever turns
up (a late-1990s keyboard or monitor with built-in USB ports contains one),
`bDeviceProtocol = 0` on `scripts\hub-characterise.ps1` is what makes it
genuine, and this case reopens.

Steps:

1. E3.1 The single-TT hub at position T, children at H1-H4; confirm every
   child, including the one behind H4.
2. E3.2 Swap the hub at T for the multi-TT one (children keep their ports and
   the downstream child keeps its H4 socket), re-enumerate, confirm every
   child again, and photograph the deepest topology.
3. E3.3 Case B: does not run.

The reading: every child device reaches Device Manager named, without a bang,
and working (a drive letter that mounts and reads, a HID that responds). A
wrong `MTT` surfaces here as a device that does not work, which is what a
user would see.

One carve-out, decided by stage E2: if the composite device read `Code 2` in
stage E2, it reads `Code 2` at H3 here too. That is task 13-E.1's finding
repeating behind a hub, not a hub failure, and this stage's pass is read on
the other children. Record its presentation on both sides of the swap anyway.

RUN box, filled at bench session 2. The E2 carve-out did not apply; read the
composite line before citing it.

```text
E3.1 single-TT tree:   hub at T = 1A40:0101, sockets assigned from the map
                       taken at bench session 2 (numbering runs backwards from the
                       cable end, so H1 is the socket furthest from it)
                       H1 name=Low-Speed mouse 046D:C077   code=none  works y
                       H2 name=USB 2.0 flash drive         code=none  works y
                       H3 name=C-Media 0D8C:0014           code=NONE  works y
                            *** BOUND - did NOT read Code 2, no yellow bang,
                            and the USB Audio Device beneath it enumerated ***
                       H4 name=05E3:0608 hub               code=none  works y
                       H4 child name=USB 3.0 flash drive   code=none  works y
                                socket=4 from cable end    chip/tier=B port 4 / 2
E3.2 multi-TT tree:    hub at T = 1A40:0201, self-powered, same hub ports,
                       different physical sockets per that hub own map
                       H1 name=Low-Speed mouse 046D:C077   code=none  works y
                       H2 name=USB 2.0 flash drive         code=none  works y
                       H3 name=C-Media 0D8C:0014           code=none  works y
                       H4 name=05E3:0608 hub               code=none  works y
                       H4 child name=USB 3.0 flash drive   code=none  works y
                                socket=4 from cable end    chip/tier=B port 4 / 2
                       all five identical to E3.1 - the second enumeration,
                       which is the one MTT can get wrong, did not
E3.3 FS-hub case:      DOES NOT RUN (published per P3, decided before the trip)
```

Stage E3 passed on both trees. The socket maps are what made it runnable:
every child sat in a named hub port on both hubs, so the two trees are
comparable rather than merely both working.

The carve-out inherited from E2 did not apply, and that is the session
finding rather than an E3 result. The composite at H3 bound on both trees,
with its USB Audio function enumerated beneath it. See the session record
(Finding D) for what changed on the machine between the two readings; in
particular, task 13-E.3 clauses 1 and 2 became reachable without stage E5.

---

## Stage E4 - the free re-observations

Task 13-E.4, last three bullets, plus the USB 3.0 stick, which this sheet
adds. Still on the plain-NUSB baseline.

- E4.1 Low-Speed device, behaviourally. An LS keyboard or mouse on a root
  port and behind a High-Speed hub. The reading is Device Manager showing the
  correct VID/PID and the device working. This does not close the Low-Speed
  leg's trace half, which needs the `RH first decode of a speed` trace line
  too; record it as a re-observation.
- E4.2 A physical BOT or UAS storage enclosure, which no vehicle in this
  project has ever had (device-table row 9). Position D, on the root port,
  not behind a bus-powered hub, where it can brown the hub out and change a
  negotiated speed. The reading: a drive letter, a file written and read
  back, and a clean unplug. Phase 8's `+0` finding is a property of two QEMU
  device models and transfers to nothing here.

  The I/O half was deferred at bench session 3 by operator decision (the
  enclosure was not at the bench, and the project owner's direction was not
  to let it block the phase). What was deferred was narrow: the bench session
  2 reading already had the enclosure enumerating at position D (drive
  letter, `USB Mass Storage` devnode, the first real USB-to-SATA bridge this
  project has met), and E4.2b's flash-drive round trip the same day was not a
  substitute, because a bridge chip is a different class of thing from a
  flash controller. The requirement recorded for closing it was a disk
  Windows 98 can mount: FAT32 on an MBR partition, since a drive letter can
  appear for a volume the OS cannot read.

  It was discharged at the enclosure visit. The unit that took the
  enumeration reading left the fleet and is not recorded here; `174C:5106`
  (ASMedia, "StoreJet Transcend") replaced it, reads BOT+UAS at both speeds,
  and its disk was already MBR with an active FAT32 first partition (a
  second, NTFS partition is present and Windows 98 letters it without
  reading it). The half was taken at position D on Windows 98 SE, shipping
  `0.0.0.5`: a drive letter, and a file written and read back with the
  contents matching. The reading is in the third RUN box below.

- E4.3 The USB 3.0 flash drive in position D, once. Everywhere else it is an
  ordinary High-Speed child at H2; on a root blue connector it exercises
  something different. This driver leaves USB 3.x root ports unpowered by
  design, so the device should fall back to the USB 2.0 companion port and
  enumerate at High Speed. A device that does not appear here belongs to the
  port-power design rather than to any Phase 13 clause.

- E4.4 The `0b95:7720` ASIX USB Ethernet adapter at position D, task
  13-E.4's last bullet. No bulk-IN class traffic had ever gone through this
  driver on real xHCI silicon: every storage and Ethernet result in this
  project came from QEMU, and the driver carries compensation for a QEMU
  non-conformance (QEMU's xHC withholds the second Short Packet Event p.175
  mandates), so that path had never met a controller that behaves correctly.
  It is the same `0b95:7720` ASIX AX88772A task 8-V.2 used, on the same OS
  with the same drivers, so real silicon is the only variable changed. Repeat
  that task's recipe: take a DHCP lease, then five parallel `ping -n 150 -l
  1400` streams. 748/750 and 749/750 is a pass (Phase 10's device-matrix
  figures for 2a and 2b; the missing replies are the opening ARP). The
  reading is whether it passes; the counter number needed a channel this
  machine did not have.

RUN box, filled at bench session 2. Read the VOID line on E4.3 before citing
it; a null there is not a negative.

```text
E4.1 LS HID:       behind hub: VID/PID correct Y  works Y  (046D:C077 at H1,
                   on BOTH hubs, taken free during stage E3)
                   root: NOT TAKEN - the controller wedged before it was reached
                   NOT a closure of the Low-Speed leg's trace half, which needs a trace line this
                   machine cannot give
E4.2 storage:      enumerates Y - drive letter present, USB Mass Storage device
                   I/O NOT VERIFIED - the enclosure was unplugged before a file
                   was written and read back, so the half of this row that
                   distinguishes "enumerates" from "does I/O correctly" is
                   unread. Five-minute job on a later boot
E4.3 USB 3.0 at D: *** VOID, NOT NEGATIVE *** the drive did not appear, but a
                   known-good USB 2.0 drive did not appear at D either, so the
                   controller was already wedged and this is an artefact of that
                   rather than a reading about port power. RETAKE on a healthy
                   machine
E4.4 ASIX 0b95:7720 Ethernet at D:  *** PASS ***  DHCP lease taken, five
                   parallel ping -n 150 -l 1400 at or near 748-750/750, which is
                   Phase 10's QEMU figure. FIRST bulk-IN class traffic ever
                   put through this driver on real xHCI silicon - every prior
                   storage and Ethernet result in this project comes from QEMU,
                   whose xHC withholds the second Short Packet Event p.175
                   mandates and which this driver carries compensation for. That
                   path had never met a conforming controller until now
```

RUN box, re-taken at bench session 3 on `0.0.0.5` (the P14 swap boot). It
supersedes E4.1's root line and E4.3 above; E4.4 stands as taken.

```text
E4.1 LS HID root:  *** TAKEN *** 046D:C077 at position D, VID/PID correct,
                   pointer moves. With the bench session 2 behind-hub half, both
                   halves of E4.1 are now read.
                   STILL NOT a closure of the Low-Speed leg's trace half, which needs the trace
                   half and says so explicitly
E4.2 storage:      I/O half DEFERRED, not retaken - the enclosure was not at
                   the bench (operator). The bench session 2
                   enumeration half stands. See the deferral note below
                   -- SUPERSEDED at the enclosure visit, next box: I/O is TAKEN
E4.2b flash I/O:   *** PASS *** a USB 2.0 flash drive at position D took a
                   file WRITTEN AND READ BACK. Not a substitute for E4.2 -
                   recorded under its own name, because it is the first
                   DATA-VERIFIED bulk round trip this driver has done on real
                   silicon. E4.4's Ethernet leg was the first bulk-IN class
                   traffic; every drive before today was enumerate-only
E4.3 USB 3.0 at D: *** PASS, and the void is retaken *** the drive appears at
                   position D. The USB 3.x root port is left unpowered by
                   design, so the device fell back to its USB 2.0 companion
                   and enumerated - which is what this row was always for
control:           *** the known-good USB 2.0 drive still enumerates at the
                   end *** so none of the readings above is void
```

RUN box, the enclosure visit: E4.2's I/O half, a short session on the E460
rather than a bench trip. It supersedes the E4.2 line in both boxes above;
E4.2b stands under its own name as the flash-drive reading.

```text
E4.2 storage I/O:  *** PASS *** 174C:5106 (ASMedia USB-to-SATA bridge,
                   "StoreJet Transcend", serial NB202029162975) at position D
                   on Windows 98 SE, shipping 0.0.0.5. Drive letter present,
                   a file WRITTEN AND READ BACK with the CONTENTS MATCHING.
                   Both halves of E4.2 are now read - the bench session 2
                   enumeration half plus this one
                   FIRST data-verified I/O round trip this project has taken
                   through a REAL BRIDGE CHIP. E4.2b did the same thing on a
                   flash drive and was never a substitute: a flash controller
                   IS the storage, where a bridge translates to SATA with its
                   own firmware, queueing and error paths
                   Disk is MBR, partition 1 FAT32 and ACTIVE - which is what
                   made it mountable. Partition 2 is NTFS and Win98 letters it
                   without reading it, the trap the deferral note named
```

The two halves of E4.2 are two units: the enumeration half belongs to the
departed enclosure and the I/O half to `174C:5106`
(`docs/contributing/test-equipment.md`, "Mass storage"). Nothing rests on the
two reading alike.

What this closes upward: roadmap carve-out 1 of batch 13-E is discharged;
device-table row 9 is fully discharged, the enclosure having been exercised
rather than merely held; and Phase 8's `+0` bullet, which said its finding
"transfers to nothing on real silicon" and pointed forward to this trip, has
the reading it pointed at. It does not change UAS: Windows 98 ships a
BOT-only storage driver, so alt 1 sat in the descriptor unselected and this
was a BOT round trip, as row 9 predicted.

### Finding W - the repair meets its own recipe, through shipping media, and nothing wedges

(A finding, not one of the `W<n>` wedge-candidate binaries, which are all `W`
followed by a digit.)

The four readings in the bench session 3 box were taken in this order, at
position D, on one boot: a High-Speed flash drive, then the Low-Speed mouse,
then the USB 3.0 drive, then the High-Speed flash drive again. That is
Finding G's recipe: drive, mouse, drive at one root port, with the speed
alternation as the operative variable, and on `0.0.0.3` it killed the port on
the first alternation every time it was tried. This time it ran through to a
clean control.

Findings T, U and V also showed the repair working, but all three were read
on candidate instrument builds (`OBS0824.SYS`, `OBS24B.SYS`, `OBSSNAP.SYS`,
each carrying `XHCI_OBS_SNAPSHOT`, a 90,600-byte extension and a reading
channel that task 13-R.4 then removed). This is the shipping `0.0.0.5`
release binary, hash-confirmed off the machine's own copy, with no instrument
in it. The repair had never before met the fault in the form a user would get.

It is one pass of the recipe, not a count (Finding U's 33 cycles and Finding
V's five are the scale results), and it is an absence: `0.0.0.5` carries no
counter channel on this machine, so nobody watched `RecoveryAttempts` while
it happened. What it retires is the last form of the doubt, that the repair
worked only in builds instrumented to watch it work.

### How the recipe was found

Stage E4 at bench session 2 stood at one clean pass, one half-read, one void,
one half-taken. E4.4 passed on the number the QEMU runs set. E4.3 had to be
retaken: a reading taken after a stall is void rather than negative, and only
the known-good drive revealed the stall.

The second wedge of that session happened during this stage, and its sequence
differed from the first. All three plug and unplug cycles were on root port D
with no hub involved, so the hub tree present for Finding F was not required.
It also followed immediately on the Ethernet adapter pushing 750 pings of bulk
traffic and then being removed, which suggested "removing a device that has
been doing heavy traffic" as a candidate, one that fit Finding 3's audio
stream dying as well. The obvious control, heavy traffic with no removal, had
not been tried.

A third reproduction later the same session refuted that candidate. After a
cold boot, three retakes were attempted at position D (the USB 3.0 drive, a
known-good USB 2.0 drive, and the Low-Speed mouse), each plugged and
unplugged. The machine wedged between unplugging the mouse and plugging the
storage enclosure. A Low-Speed HID at `bInterval`=10 and two flash drives
that were only enumerated are not heavy traffic, so the Ethernet ping run
before the second wedge was a coincidence.

What survived all three was the plainest reading: topology events on a root
port, and enough of them. The count looked similar each time, roughly six to
eight plug-or-unplug events on position D since the last cold boot, with no
traffic, no audio, no load and no hub. That earned a counted protocol: cold
boot, then plug and unplug one known-good flash drive in one root socket,
counting each event and testing enumeration each time, until it stops
enumerating. All three wedges were at position D, which is the Always On
connector and not electrically identical to the others, so the protocol was
to run first at D and then at the spare right-hand socket.

### Finding G - the recipe, and it is five events long

The counted protocol produced a negative. One known-good High-Speed flash
drive, plugged and unplugged in position D, fifteen cycles (thirty events),
hub off, nothing else attached: no wedge at all. Raw plug count on one port
with one device is not the trigger, and the "six to eight events" reading
above is wrong as a count of events.

That negative removed two variables at once, the hub and the device variety
of the sequences that had wedged. Device variety was added back, and it
wedged on the first alternation:

```text
1. High-Speed flash drive -> position D   enumerates, drive letter   unplug
2. Low-Speed mouse 046D:C077 -> position D   enumerates              unplug
3. High-Speed flash drive -> position D   *** NOTHING ENUMERATES ***
```

Five events. Confirmed as the same machine-wide wedge as the earlier three:
nothing enumerates anywhere afterwards, the spare root socket included, and
only a cold boot recovers it.

This is the minimal reproduction the project had never had. Finding 3 had
been reachable only by playing isochronous audio and waiting, across two
sessions. It became: plug in a drive, plug in a mouse, plug in the drive. No
hub, no traffic, no audio, no CPU load, no composite device.

The two devices differ in more than identity; they differ in speed. The clean
fifteen-cycle run used one High-Speed device throughout, and the wedge needed
a Low-Speed device to have used the same root port in between. So the
candidate became a speed transition on a root port, or per-slot state not
released when a device of one speed is replaced by one of another. The
control that separates speed from identity is one boot: alternate two
different High-Speed flash drives with different VID/PIDs in the same
position D. Clean means speed transition; a wedge means device identity.

Not established: that this is the same fault as bench session 1's audio
observation. It shares the terminal signature (nothing enumerates,
port-change events stopped, cold boot only), but the audio case had an
isochronous stream dying first and no device swap.

### Finding H - it is the speed transition, and the control that says so was clean

The speed-versus-identity control came back clean. Three conditions at
position D, one variable between them:

| Sequence at position D | Events | Result |
|---|---|---|
| one High-Speed flash drive, plugged and unplugged repeatedly | 30 | clean |
| two different High-Speed flash drives, alternating | 16 | clean |
| High-Speed drive -> Low-Speed mouse -> High-Speed drive | 5 | WEDGE |

Device identity and plug count are exonerated, and the port is the same in
all three. What distinguishes the wedge from both clean runs is that a
Low-Speed device used that root port in between. This was the first time
Finding 3's family of failures had been reduced to a single named variable,
and it took a bench afternoon rather than instrumentation, because the two
clean runs are what give the positive its meaning.

Where to look, as inference from the shape: the driver's per-slot and
per-port state is speed-dependent in several places at once (the Slot Context
speed field, the EP0 max packet size, and the TT fields a root-port device
does not use). A Low-Speed device giving the port back and a High-Speed
device then failing to enumerate on it is the shape of state not being reset
on disconnect and inherited by the next enumeration. The teardown path is
where to start, because the enumeration that fails is correct in isolation:
the same drive on the same port enumerated thirty times in a row. A near
neighbour not to read as a cause: `XHCI_EP0_MPS_FULL_INITIAL` was changed
from 8 to 64 at bench session 1 for a Full-Speed babble, a different defect on
a different path.

The next control was the same sequence with a Full-Speed device in the
middle (the C-Media `0D8C:0014`). It wedged too, on the first cycle, so the
answer is the general one: any speed change on a root port, not Low Speed
specifically.

| Sequence at position D | Events | Result |
|---|---|---|
| one High-Speed drive, plugged and unplugged repeatedly | 30 | clean |
| two different High-Speed drives, alternating | 16 | clean |
| High-Speed -> Low-Speed mouse -> High-Speed | 5 | WEDGE |
| High-Speed -> Full-Speed C-Media -> High-Speed | 5 | WEDGE |

Two of two non-High-Speed speeds reproduce it; two of two High-Speed-only
runs do not. A Low-Speed-only quirk is out. Low Speed and Full Speed share
what High Speed does not on this path: their Slot Context, EP0 packet size
and TT-related fields are set differently, and on a root port they need no
transaction translator while still being marked sub-High-Speed.

The localisation was run too, and the damage is latent. The sequence was
stopped after the sub-High-Speed device was unplugged, and Device Manager
read before anything else was plugged in: the mouse devnode disappeared
cleanly, the rest of the tree was intact and repainting normally, and the
High-Speed flash drive plugged in afterwards still failed with the same
machine-wide wedge. So teardown completes its visible job correctly, and what
it leaves behind poisons the next enumeration on that port. A control came
free with this: the pause to read Device Manager sat between the unplug and
the next plug and did not prevent the failure, so timing is not part of the
trigger.

### What the session establishes about Finding 3, in one place

The trigger, from four conditions with device identity, plug count and the
port held constant:

| Sequence at position D | Events | Result |
|---|---|---|
| one High-Speed drive, repeated | 30 | clean |
| two different High-Speed drives, alternating | 16 | clean |
| High-Speed -> Low-Speed mouse -> High-Speed | 5 | WEDGE |
| High-Speed -> Full-Speed C-Media -> High-Speed | 5 | WEDGE |

The shape: a sub-High-Speed device disconnecting from a root port leaves
state behind; teardown looks clean and is not; the next enumeration on that
port fails and wedges the controller machine-wide, and only a cold boot
recovers it. Not established: that this is the same fault as bench session
1's audio observation. What to audit first, as inference: whatever the
enumeration path reads that the previous device wrote (slot state, device
address, or per-port context) for a port whose last occupant was not High
Speed.

### Finding K - the speed reading is confounded, and two more bench observations say so

Reported by the project owner at bench session 2, at position D, with devices
already in the bag:

- The Low-Speed mouse `046D:C077`, alone, fails on the second plug. Plug in,
  works. Unplug, the devnode disappears cleanly. Plug the same mouse back in,
  it no longer works. Three events, no speed change, the same device on both
  plugs.
- The `0B95:7720` ASIX Ethernet adapter lasts only two plug/unplug cycles
  before wedging.

The first observation removes the word "transition" from Finding H: that
table's only repeat-plug rows used a High-Speed flash drive, so
repeat-plugging a sub-High-Speed device had never been run. The trigger is
something the disconnect itself leaves behind. The second removes the word
"speed": batch 13-H characterised that adapter (device table row 10,
`docs/contributing/test-equipment.md`) as High Speed, vendor class `FF/FF`,
one interface, an interrupt IN for link status at `bInterval=11` plus bulk IN
and bulk OUT at 512. The speed reading predicts it cannot wedge, and it
wedges in three cycles.

Speed and endpoint type were perfectly confounded in the bench data: every
sub-High-Speed device tried carried a periodic endpoint, and every High-Speed
device tried was bulk-only.

| device at D | speed | periodic endpoint | result |
|---|---|---|---|
| High-Speed flash drive, repeated | High | none, bulk IN/OUT only | clean, 30 events |
| two different High-Speed flash drives | High | none | clean, 16 events |
| `046D:C077` Logitech mouse | Low | interrupt IN, `bInterval`=10 | fails on plug 2 |
| `0B95:7720` ASIX AX88772A | High | interrupt IN, `bInterval`=11 | wedges in 3 cycles |
| `0D8C:0014` C-Media | Full | isochronous | WEDGE |

The variable that survives all five rows is a periodic endpoint, again as
inference from the shape, on one more row of evidence and one fewer confound.
It also re-unifies bench session 1's audio route: isochronous is periodic, and
a stream dying three to seven seconds in and taking the controller with it is
the same shape as a periodic endpoint's state not being released.

A rival formulation fits every row equally well: not "the device has a
periodic endpoint" but "the device had a transfer in flight at the moment of
disconnect". A periodic endpoint always has one queued; an idle flash drive
has none. The test that separates them is one plug: unplug a flash drive
during a file copy, so a bulk-only device is torn down with a transfer in
flight. A wedge means teardown with a transfer outstanding; clean means the
periodic endpoint itself. (Finding M below took it.)

The audit target moves accordingly: "for a port whose last occupant was not
High Speed" becomes "for a port whose last occupant had a periodic endpoint",
which points at endpoint teardown and the periodic ring rather than at the
Slot Context speed field, the EP0 packet size and the TT fields. None of
those distinguishes the ASIX from a flash drive, and the ASIX is the row that
decides it. A counter reading of this, had one been available, would add the
endpoint open and close accounting (`endpoints opened` against the close
side, and the nine-term open-accounting identity `src\xhci.h` states) to the
slot, address and port-change counters.

### Finding L - QEMU does not reproduce it, and that is the recorded result

`scripts\vm-matrix\wedge-observe.ps1` on target 2a, debug build
(`MiniPortExtensionSize` 87592). Evidence in `out\obs-13q\`. The negative is
a result and is written as one.

The `hid` group was re-run first, so the negative rests on a measurement
rather than on a roadmap line about an older binary: 4 PASS, 2 EXCLUDED,
including four High<->Full alternations on the pinned DUT port with a
`device_del` between each. Then four purpose-made arms, one boot each, all on
root port 2:

| arm | sequence | verdict |
|---|---|---|
| `repeat-int` | mouse/HS x3 | NEGATIVE |
| `repeat-bulk` | storage/HS x3 | NEGATIVE |
| `fs-mid` | storage/HS -> mouse/FS -> storage/HS | NEGATIVE |
| `hs-mid` | storage/HS -> mouse/HS -> storage/HS | printed REPRODUCED; a false positive, see below |

So QEMU does not reproduce the wedge. The vehicle tolerates whatever real
silicon halts on, and the traces rather than the verdict are what the run was
for.

The `hs-mid` verdict was wrong and is recorded so it is not cited. The final
screenshot shows a modal Add New Hardware Wizard for `USB Mass Storage
Device`. The counters agree: that arm's first attach reached `devices
addressed` +1 and `slots enabled` +1 but `endpoints opened` 0, so this driver
enumerated the device and the OS never claimed it, the matrix's `NODRIVER`
outcome. `repeat-bulk` and `fs-mid` bound theirs (`endpoints opened` +2). A
blocked bind also silently kills the keep-alive pump, because `mouse_move`
only reaches the wire once a function driver holds the pointer's interrupt
endpoint, so every later hot-plug in that arm was invisible to the stack, and
attaches 2 and 3 and both their pulls registered only `port event changes` +1
and `RH changes cleared` +1.

That is the opposite of the E460's signature. There, port-change events stop
arriving; here the driver saw every connect and cleared every change while
the guest's PnP stack was stalled behind a dialog. Both defects were in the
harness's verdict logic and both are fixed: it accepted a baseline the OS had
never bound, and it compared the first attach against the last so it named
attach 3 when the break was at attach 2. It now voids an unbound baseline,
names the earliest failing attach, and says so when port changes are still
being announced at the failure.

What the run did establish is Finding K's variable, measured. Three pulls of
a periodic device armed three Stop Endpoints; three pulls of a bulk-only
device armed none; and in `fs-mid`, of three pulls on one port in one boot,
only the middle one, the periodic device, entered the quiesce path at all.

---

### Finding M - the in-flight unplug is clean, so "a transfer outstanding" is not the variable

Executable item 3e2, taken by the project owner. ThinkPad E460, Windows 98 SE,
`xhci98.sys` `0.0.0.4` standard release. Cold boot, position D, nothing else
attached, re-plugged at D. No instrumentation.

The stimulus: a High-Speed flash drive at D, a large file copied from the
drive to the internal disk, and the drive pulled while the copy was running.
The reading: clean. The re-plug at D enumerated normally (`USB Mass Storage
Device` in Device Manager, drive letter in Explorer).

The copy was genuinely in flight, and Windows testified to it: a full-screen
advisory ("the volume you removed had open files on it") followed by a
"cannot read from source file or disk" dialog when the copy died. That is the
OS stating there was an open handle and an incomplete read at the instant of
removal, which is stronger than watching a progress bar. Both alarms are
expected and neither is a fault; the copy read from the drive, so nothing was
being written and no filesystem was at risk. (That this was not said in
advance is a run-sheet defect, recorded in `lessons.md`.) The machine was
healthy afterwards, which makes this a negative rather than a void reading:
the re-plug enumerating is the wedge detector this sheet requires after every
step.

#### What Finding M establishes, and the one thing it does not

Refuted as the variable: hypothesis (2), "had a transfer in flight at the
disconnect". A bulk-only device with a demonstrably outstanding transfer was
torn down and the next enumeration on that port was clean. No other vehicle
could have produced it; every arm of the QEMU observatory detaches an idle
device, stated as a limit in `wedge-observe.ps1`'s own report. What survives
is a periodic endpoint, hypothesis (1), unchanged since Finding K.

The discrimination rests on an assumption nobody measured: that a busy bulk
endpoint arms a Stop Endpoint, which the sheet took from `xhciEpArmIfBusy`
(`src\xhci_slot.c`), where a non-empty queue or an unsettled ring arms one.
Whether it did here is unknown, because this machine cannot say:

| If, on a mid-transfer bulk unplug, the driver | then | and Finding K's variable is |
|---|---|---|
| armed a stop and it completed | arming a stop is not sufficient to wedge | genuinely periodicity, for a reason beyond arming a stop (bandwidth or interval state) |
| never armed one (usbport cancelled the transfers before teardown looked) | the clean result is explained trivially | "arms a Stop Endpoint", which a periodic device always does and an idle or cancelled bulk one never does; periodicity is a proxy |

Hypothesis (2) is refuted on both branches. What the branches disagree about
is the name of the surviving variable. It is settleable at the desk: an arm
of `wedge-observe.ps1` that starts a large read in the guest and issues
`device_del` mid-transfer would read `endpoint stops` and `transfers
cancelled` for this case. That asks what this driver does, not what the
silicon does with it, so the vehicle's negative on the wedge itself does not
disqualify it. The question a counter read would then ask is "does a stop on
a periodic endpoint fail where a stop on a busy bulk endpoint succeeds",
with `teardowns without a stop` read per teardown class rather than as a
single total.

---

### Finding N - the mechanism is refuted as a sufficient cause, and it cost two boots

Task P7.1, taken by the project owner immediately after the baseline
re-confirmation. `W3BOTH.SYS` (both candidate fixes at once) on the E460,
five-plug recipe at position D, cold boot, nothing else attached.

The reading: it still wedges. Step 3's flash drive does not enumerate; the
wedge arrives between steps 2 and 3 as on the baseline.

`W1GATE` and `W2RING` were not run, correctly. Both are strict subsets of
what `W3BOTH` contains, so neither can succeed where the combination failed.
The whole four-binary set was exhausted in two boots.

#### What this refutes

The teardown-without-a-stop chain, as a sufficient cause. The hypothesis was:

1. only a device with a posted transfer arms a Stop Endpoint, so only a
   periodic device reaches the quiesce path at all;
2. the stop is not shown to have worked;
3. the Disable Slot goes out anyway, counting `TeardownsWithoutStop`;
4. the release decision consults only the completion code, so a Success
   answers the queued transfers and returns the transfer rings to a first-fit
   pool that hands the same index straight to the next device.

`W3BOTH` blocks both of the acting links: it refuses the release when the
quiesce failed (link 4's gate) and stops the freed ring being handed to the
next device (link 4's consequence). The wedge is unaffected, so that chain is
not what wedges this machine.

#### What survives

Finding K's variable is untouched. A periodic endpoint's teardown still takes
a structurally different path from a bulk one, and that is measured: Finding
L counted three Stop Endpoints armed across three pulls of a High-Speed
interrupt device against none across three pulls of a High-Speed bulk-only
device. Finding M then refuted the rival reading. Something in a periodic
teardown still wedges the controller; it is not the transfer rings and it is
not the slot release.

One observation was owed and free: whether the devnode survives a Refresh
after the failure. Finding 3's signature is that port-change events stop
arriving (a physically removed device still shows in Device Manager). Devnode
stays: event delivery or the controller itself is dead. Devnode goes: the
controller is alive and clearing port changes, and only enumeration is
failing. Those point at disjoint bodies of code.

#### The sharper question this run raised

This driver already detects a fatal controller and asks for a reset, so why
does the machine still need a cold boot? `xhciCheckController`
(`src\xhci_cmd.c`) polls `USBSTS` on every invocation, nominally every
500 ms, and on `HCE` or `HSE` latches `ControllerFatal`, counts
`FatalStatusDetected` and escalates, asking usbport to queue a controller
reset. A plain Host System Error should therefore self-recover here. It does
not. Three readings survive:

1. the health poll is not running: usbport has stopped calling the miniport
   at all. `xhci.h`'s comment on `HealthPolls` names the ambiguity: a frozen
   count reads either as "usbport stopped calling this miniport" or as
   "usbport is calling it and it declines";
2. it escalates and the reset does not recover the controller;
3. it is not a fatal-bit condition at all: the xHC believes itself healthy
   and simply stops delivering.

`HealthPolls`, `HealthPollsDead`, `FatalStatusDetected`, `ControllerFatal`
and `LastCheckStatus` separate all three in one reading. None was readable on
the E460 at the time. (P10 built the channel that read them; Finding S is the
answer, and it was none of the three as written: the driver marked its own
controller failed on a command timeout and waited for a stop/start that never
came.)

#### What this says about the method

The superset was tested first and it saved three boots. Had W1GATE been run
first it would have wedged, W2RING would have wedged, and only then would
W3BOTH have shown the set was exhausted. And a conditional no-op would have
been unreadable alone: W1GATE only acts if the stop fails, so "still wedges"
by itself could not distinguish "the fix is insufficient" from "the stop
never fails here". A candidate whose action is conditional on an unmeasured
premise must not be the only candidate in a run.

---

### Finding O - refuted by its own repair, kept because the error in it is instructive

Do not cite the mechanism below. `W13ACK`, the repair this finding proposes,
built alone with no polled sweep, wedges exactly as the baseline does (P7.9).
Carrying the refused acknowledgement forward and retrying it changes nothing,
which says `PortscAckOwed` is never set: the acknowledgement was never
refused, because Port Power was never in flight. That was the hole the
independent review named and could not close.

The attribution that produced it was too quick. `W11POLL`'s polled sweep does
two independent things: it acknowledges change bits, and it bypasses event
delivery entirely by reading PORTSC on a timer. A working poll is therefore
equally consistent with "the port was stranded" and "no events are being
delivered at all". `W12EVT` (the interrupter re-arm) was the control that
separates them, and it was skipped on the strength of `W11POLL` succeeding: a
success attributed without excluding the alternative.

The surviving hypothesis at that point was the one the control tests: if the
interrupter is silenced during a sub-High-Speed teardown (`EHB` left set, or
`IMAN.IE` lost), then no port change events and no transfer completions
arrive, machine-wide; the health poll keeps running because usbport drives it
on its own timer; and a polled sweep works while everything event-driven is
dead. (Finding S later found the actual latch: the driver marking its own
controller failed and masking its interrupts.)

### Finding O (as written, now refuted) - a PORTSC acknowledgement refused while Port Power is in flight is dropped

Attributed by bisection on the bench (`W11POLL` alone works, P7.7), then
located in the source.

#### The site

`xhciRhWritePortsc` (`src\xhci_rh.c`), the one function that writes PORTSC on
the root-hub path:

```c
if (!XhciPortShadowPpSettled(shadow, portsc)) {
    ext->RhPortPowerPending++;
    XHCI_DBG_VALUE_CHANGED("RH: holding a PORTSC write back - a Port Power "
                           "change is still in flight on port", xhciPort);
    return 0;                  /* <- the acknowledgement is DROPPED, not queued */
}
```

#### The chain

1. A Port Power write is in flight on a port, so the shadow's `PpPending` is
   set.
2. Hardware sets a change bit, `CSC` on the disconnect.
3. `xhciRhRefresh` latches the change into the shadow and computes `ackBits`,
   then calls `xhciRhWritePortsc` to clear the hardware bit.
4. The write is refused, because PP has not settled. `ackBits` is a local
   variable, so the debt is dropped: not remembered, not queued, not retried.
5. `XhciPortShadowPpAge` later gives up and clears `PpPending`.
6. Nothing re-runs a refresh for that port. The only block that would is
   gated on `PpPending != 0` (`src\xhci_rh.c`, the deferred-work sweep), and
   `PpPending` is now zero.
7. The change bit stays set for the life of the driver. A change bit nobody
   clears suppresses the controller's next Port Status Change Event for that
   port (`lessons.md`, "hot-plug operations are not hot-plug events"). The
   port is silent from then on.

Why it produces the observed signature: a connect raises no event, so the
driver never learns of the arrival and nothing appears in Device Manager, not
even a failed devnode (measured on both machines). The shadow still holds the
change, so `RH_GetPortStatus` would report it if usbport asked, but usbport
only asks when told, and telling requires the event the hardware will no
longer raise. Cold-boot-only, because nothing in steady state acknowledges
that bit again. It also explained the two candidate negatives: `W7GATE` was
inert because with no event nothing is owed for its watchdog to force out,
and `W3BOTH` was irrelevant because the slot and ring subsystem is never
reached if the arrival is never seen.

Why `W11POLL` works on this account: the polled sweep refreshes every port,
every health poll, unconditionally. Once `PpPending` has cleared, the next
sweep's `xhciRhRefresh` recomputes `ackBits` and the write is not refused, so
the hardware bit is cleared. Recovery lands on the first poll after the plug,
the measured ~0.5 s.

#### The repair proposed, which is not the polled sweep

When an acknowledgement is refused because a Port Power change is in flight,
the debt must be remembered and retried when PP settles.
`XhciPortShadowLatchChange` already stores the change bits in the shadow,
which keeps `RH_GetPortStatus` correct; what has no owed-and-retried path is
the hardware acknowledgement. The reviewed minimal shape, with the review's
warning that the debt must belong to the acknowledgement caller because
`xhciRhWritePortsc` also serves reset, resume and disable writes whose stale
full values must never be replayed:

- add `ULONG PortscAckOwed;` to `XHCI_PORT_SHADOW`, initialised with the rest
  of each rebuilt shadow (`src\xhci_port.c:219`);
- in `xhciRhRefresh`, `ackBits |= shadow->PortscAckOwed;` after
  `XhciPortShadowUpdate`, then use the write's return value: clear
  `PortscAckOwed` when it wrote, set it to `ackBits` when it refused;
- widen the health poll's gate to
  `if (shadow->PpPending != 0 || shadow->PortscAckOwed != 0)`.

Every retry then composes from a fresh PORTSC read, natural PP settlement
drains the debt in the same refresh, and a PP give-up leaves the debt as the
next poll's reason to act. A host regression vector exists to build on:
`test/test_init.c:11448` constructs a stuck PP plus a change. `W11POLL`'s
sweep remains defensible as a fallback (one PORTSC read per port per 500 ms),
but a fallback that hides a dropped debt makes the next dropped debt
invisible too.

The counter that would close it: `RhPortPowerPending` increments on this
refusal, and `RhPolledLatches` (added with `XHCI_FIX_PORT_POLL`) says how
often the fallback was needed.

#### Corrected by an independent source review: the trigger story above was wrong

The mechanism survives as a latent defect. The causal direction did not.

The acknowledgement is attempted before teardown is entered. `xhciRhRefresh`
issues the ack at `src\xhci_rh.c:298`, and only afterwards, at
`src\xhci_rh.c:387`, does `XhciSlotPortConnectChanged` reach `xhciDevTeardown`
(`src\xhci_slot.c:9421`). Teardown runs after the ack attempt and cannot be
what blocks the ack for that same CSC.

No teardown path arms `PpPending` at all. A repository-wide search finds the
only runtime arm at `src\xhci_rh.c:850`, reached solely from
`XhciRhSetFeaturePortPower` / `XhciRhClearFeaturePortPower`
(`src\xhci_rh.c:1097`), a usbhub-initiated callback. `XhciPortShadowPpArm`
(`src\xhci_port.c:607`) is the sole non-initialisation `PpPending = 1`.
`xhciDevArmTeardownStops` (`src\xhci_slot.c:3364`) has no speed and no
transfer-type branch; the only periodic distinction in it is
`xhciEpArmIfBusy`'s "is the endpoint busy".

So the pre-existing Port Power operation would have to come from above this
driver, and why usbport or usbhub would issue one around a sub-High-Speed
unplug is not established by anything in this repository. The proposed link
is incompatible with the local call ordering.

Two further corrections. "Nothing re-runs a refresh for that port" is too
strong: `RH_GetPortStatus` also calls `xhciRhRefresh` (`src\xhci_rh.c:610`), so
an invalidation would re-acknowledge, but the announcement that provokes it is
suppressed while the notification gate is closed (`src\xhci_rh.c:1884`), so it
is not a guaranteed retry. The correct statement is that no unconditional
steady-state sweep exists in any shipping flavour, which is the hole
`XHCI_FIX_PORT_POLL` fills.

And `RhPortPowerPending` is not exclusive proof: it increments both for a
refused PORTSC write (`src\xhci_rh.c:140`) and for a whole port operation
refused at the precheck (`src\xhci_rh.c:742`). To identify a real pre-CSC Port
Power source, capture `RhPortsPowered`, `RhPortsUnpowered`, the port number,
`PpWanted` and the preceding callback at the refusal.

What stands: the dropped acknowledgement is a genuine latent defect whatever
puts PP in flight. What must not be repeated is the claim that this driver's
own teardown is what puts PP in flight.

---

### Finding P - teardown cannot reach PORTSC at all, so no candidate that assumes it can is worth building

Independent source read, after `W12EVT` and `W13ACK` both failed. A
structural result: it does not name the fault, it removes a whole family of
candidates for it.

`xhciDevTeardown` and everything it reaches never read or write PORTSC, never
touch `PpPending`, and never touch the port shadow. `xhciDevTeardown`,
`xhciDevArmTeardownStops`, `xhciEpArmIfBusy`, `xhciDevRelease`,
`xhciDevFailRecord` and the `XhciSlotPortConnectChanged` / `Disowned` /
`Disabled` family were read end to end. Teardown is a command-ring sequence
(Stop Endpoint, then an owed Disable Slot) with zero contact with port state.
The call direction is root-hub into slot, never the reverse: `xhciRhRefresh`
calls `XhciSlotPortConnectChanged` and `XhciSlotPortReset`, and nothing in
the slot layer calls back into the root-hub layer. The periodic/bulk
difference lives entirely on the command ring: `xhciEpArmIfBusy` differs
between the two classes only in whether a Stop Endpoint is armed.

Consequence: no mechanism in which this driver's own teardown strands a
change bit can be true. Finding O was one such; the admission-gate theory
below was another.

#### `XHCI_EXT_FLAG_RH_CLOSED`, traced

Set only in `XhciStopController` (`src\xhci_init.c:3783`) and cleared only in
the controller start sequence (`src\xhci_init.c:4241`, paired with
`XHCI_EXT_FLAG_INITIALIZED` going up). It is a controller-lifecycle admission
gate, and no per-device teardown path sets or clears it. It cannot silence
one port after an ordinary unplug.

So the `xhciRhAdmitted` early return in `XhciRootHubPortEvent`
(`src\xhci_rh.c:2711`) is a real latent defect and not this fault. It
consumes a Port Status Change Event and returns without acknowledging the
change bit, and the drop is uncounted (`PortEventsMapped` increments before
it, `PortEventChanges` after). Worth fixing and counting on its own merits.

(This deduction about the flag was right and the conclusion about the gate
was wrong: `xhciRhAdmitted` has four clauses, and `ControllerFailed`, the
fourth, is reached by a plug through the command timeout. See the finding
status table at the top, and Finding S.)

#### The retraction

"The acknowledgement was never refused, therefore it was never attempted" is
a non-sequitur, and it carried the reasoning that produced `W13ACK`. That
binary adds retry logic only inside `xhciRhWritePortsc`'s single refusal
branch. Its failure is equally explained by "the write always succeeded when
tried" and by "`xhciRhRefresh` was never invoked for this port at all", in
which case the added code is inert. What `W13ACK` establishes is only that
the Port-Power-refusal path is not involved.

What survives, as inference: `W11POLL`'s sweep announces only when
`xhciRhRefresh` returns nonzero, which happens only when a change bit is set
in the PORTSC it just read, so CSC was probably still set at sweep time. At
this point nobody had read PORTSC directly in the wedged state. (Finding R
did: `00020AE1`, `CSC` standing.)

#### The discriminator that was nearly lost

`W12EVT` failing while `W11POLL` works is a result. Re-establishing event
delivery does not recover the port; a direct PORTSC read-and-ack does. That
argues against a machine-wide dead interrupter and toward a per-port stuck
condition. `lessons.md`'s hot-plug-events finding was observed per-port in
QEMU and says nothing about the interrupter globally; "stranded CSC on one
port" and "interrupter dark machine-wide" are different-scoped failures, and
this is what separates them.

#### The method rule this run earned

Every P7 result to that point was "confirmed by remedy", including the two
later refuted. A remedy that works says a class of intervention helps; it
does not say which mechanism it acted on, and this sheet twice attributed
one and was wrong. Observation of the failed state outranks remedy, and after
five bench boots and five remedies the investigation had no readings: not
PORTSC, not a counter, not a register.

What to read, in one break-in rather than a sixth candidate, with a plain
debug build carrying no wedge-candidate defines: run the recipe to the step-3
failure and then, before any recovery action, take PORTSC itself, raw, for
the affected port, and `InterruptCount`, `InterruptsClaimed`, `DpcCount` and
`EventsTotal` against `HealthPolls` (`src\xhci.h`). `HealthPolls` climbing
while the other four are frozen from before the failing teardown is direct
proof the interrupter has gone dark machine-wide; all five climbing normally
excludes that family and puts the fault in port-level signalling. This was
written for a Windows 2000 counter vehicle that never existed; P10 built the
Windows 98 channel that took it, and Findings R and S are the reading.

---

## Stage E5 - the SE CD install, and it is one-way

Optional since bench session 2, and it runs last or not at all. The question
this stage was built to answer, whether the SE CD's stack supplies what plain
NUSB lacks, was answered without it. Finding D bound the composite on the
plain-NUSB baseline the moment `usbhub.sys` was copied onto the machine by hand,
and task 13-E.1 closed on that at bench session 2 with the fix being one file
rather than a stack. The X61 control settled it: same Windows 98 SE, same NUSB
3.3, no xHCI, every composite device works, because Setup found its UHCI
controller and placed the file.

So run this only if somebody wants the SE stack on the machine for its own sake.
It still ends the plain-NUSB baseline permanently, and stage E6 comes before it
(see "Stage order" above). The procedure below is kept as written.

Task 13-E.1, the cheap branch. Everything above is taken on the plain-NUSB
baseline; this stage ends that baseline.

Install Windows 98's native USB stack from the SE CD, reboot, and re-present the
composite device on a root port.

The install source is already on the E460's own disk (CD contents copied there
at bench session 1), so this stage does not need the drive and does not cost a
disc swap on a machine that has no Windows-side USB until this driver runs. P4
verified that media by hash (`usbd.sys` 18,912 bytes, `0118DB14...F56A`), so
what is staged there is the genuine SE stack.

That convenience cuts both ways. The stage is still one-way, and it is now one
double-click away. Everything readable on the plain-NUSB baseline, stages E1 to
E4 and above all E2 (the control), must be taken before it. Having the installer
on the hard disk removes the disc swap that used to stand in front of an
irreversible step.

Why this stage existed: on the 2a VM a composite audio device binds with no
yellow bang, but that guest is not a plain NUSB install. The SE CD's own USB
components had been installed to satisfy `uhcd.sys`, and every successful
composite bind observed there happened afterwards. So the difference between
that guest and this machine could have been the SE CD's stack rather than
metal-versus-QEMU or the device class. It was neither. Copying `usbhub.sys` onto
the E460 by hand bound the composite on the plain-NUSB baseline (Finding D), and
the X61 (same OS, same NUSB, no xHCI, composite devices working) says why the
guest had it and this machine did not.

The reading, and both branches are results:

- It binds. Expected, and it confirms nothing new: the composite already binds
  on the plain-NUSB baseline with `usbhub.sys` in place.
- Still `Code 2`. That would be a new finding and worth reporting. It would mean
  the SE CD install displaced or shadowed the `usbhub.sys` this package
  delivers, on a machine where the composite was working an hour earlier.
  Record what the stack build changed to (E5.2) before concluding anything.

Re-take the identity row afterwards. The stack build in stage E1's row is no
longer what is installed, and every result after this point names the new one.

RUN box (not yet run):

```
E5.1 native stack installed + rebooted:  y/n
E5.2 identity row re-taken:              new stack build=
     (the two file rows from E1.2 again - these are what E5 changed:)
                     usbport.sys ver=            sha256=
                     usbhub20.sys ver=           sha256=
E5.3 composite re-presented at D:        binds / still Code 2:        photo y/n
```

---

## Stage E6 - audio, heard

Task 13-E.3, the only genuine blocker left in batch 13-E. Clauses 1 and 2 need
the composite device bound, and it is bound already: Finding D (bench session 2)
has it binding on the plain-NUSB baseline with `usbhub.sys` in place, which is
what the shipping package now carries. So this stage runs before stage E5, which
is one-way and optional (see "Stage order" above). Clause 3 is a different
physical unit and is not gated on any of this; run E6.3 regardless.

Three clauses on two devices:

1. A physical USB Audio device on real xHCI silicon. Never once run in this
   project.
2. The same device behind a High-Speed hub. No High-Speed hub model exists in
   QEMU at all, so the split-transaction isochronous path has never been
   exercised anywhere.
3. A device declaring `bInterval > 1` (the Sound Blaster X4, per P2). The
   Endpoint Context Interval derivation has only ever been exercised at one
   value.

Steps:

1. E6.1: play through the device at position D; phone-record; note any stutter
   or silent window and for how long it played.
2. E6.2: the same behind the High-Speed hub, at H3.
3. E6.3: clause 3 is the Sound Blaster X4's (`041E:3278`), the only held device
   declaring `bInterval > 1` (isoch 1, 3 and 4, per P2); the composite device
   reads 1 and cannot take it. The X4 is UAC 2.0 with no UAC 1.0 fallback
   configuration and this target's audio driver is UAC 1.0, so whether it binds
   at all is the finding. If it binds, play through it as in E6.1. Record the
   bind outcome (or its Code) and what was heard. Take this step whatever the
   other composite did, and whether or not stage E5 is ever run: it is a
   separate unit with its own reading, and a `Code 2` on the other composite
   says nothing about it.

The reading is whether it plays: a clean capture with no silent windows.
"Presented and bound" discharges none of the three clauses. A device that
enumerates and then stutters is the outcome this task most needs to be able to
report, and it is a pass of the task even though it is a failure of the device.
Record what was heard, for how long, and on which of the three clauses.

There is no capture path on this machine, so the artefact is a phone recording
of the speakers plus a written note. That is legitimate evidence here, and the
absence of a `.wav` is not a reason to leave the clause unread.

Both directions are useful. A real device failing the way the emulated one did
makes `docs/using/release-notes.md`'s Windows 98 audio limitation real and
shippable. A real device working makes the QEMU result a vehicle artefact and
the published limitation wrong, which is a result this trip is equally entitled
to produce.

The counter half (`iso packets` against `missed service`) was a Windows 2000
counter clause. No Windows 2000 vehicle ever existed, and the audible half was
taken here regardless.

RUN box (E6.1 and E6.2 filled at bench session 3, on the `0.0.0.5` swap boot):

```
E6.1 root port:    *** PLAYS CLEAN *** no stutter, no silent window
                   played for= ~7 s (the clip's own length, played to its end)
                   position D, the composite UAC 1.0 unit
E6.2 behind hub:   *** PLAYS CLEAN *** no stutter, no silent window
                   played for= (not separately recorded)
                   multi-TT 1A40:0201 at position T, device at H3, hub on its
                   barrel-jack PSU
E6.3 clause 3 (X4): *** DOES NOT BIND *** position D, 0.0.0.5
                   ONE new devnode: an HID device, yellow bang, *** Code 10 ***
                   NO "USB Composite Device" parent devnode at all
                   no audio devnode, no CDC devnode, nothing under Other
                   devices, no bugcheck
                   plays: N/A - nothing to play through
                   => CLAUSE 3 IS UNREADABLE ON THIS TARGET, and that is the
                      recorded outcome rather than a blank row
                   OWED: the Enum\USB attribution check - see Finding Y
```

### Finding X - USB Audio plays on real xHCI silicon under Windows 98, and the published limitation is wrong

Clauses 1 and 2 of task 13-E.3 are discharged, both clean. A physical USB Audio
device played through this driver on a real xHCI controller under Windows 98 SE,
which this project had never done, and it played again behind a High-Speed hub,
the split-transaction isochronous path that had never been exercised in any
vehicle (QEMU has no High-Speed hub model).

It contradicts the prediction, and the prediction was well-evidenced. Phase 9
reproduced `USBAUDIO.VXD` bugchecking on bind four times, including through a
UHCI control with this driver idle-suspended, and `docs/using/release-notes.md`
carries a Windows 98 audio limitation built on it. Every one of those runs used
one emulated device in QEMU, and this repository separately records that QEMU's
emulated devices never produce the error paths real ones do. The limitation was
a statement about the test vehicle, as task 13-E.3 said it might be. This
reading turns it into a statement about Windows 98, in the direction that makes
the published text false.

What follows is a release-notes correction rather than a code change.
`release-notes.md`'s Windows 98 audio limitation carries task 13-E.3 as its
`OWED` marker, and Phase 14's task 14.1 is where that table must come out empty.
The correction is not "audio works on Windows 98" either; see the bound at the
end of Finding Y.

### Finding Y - the X4 does not bind, clause 3 is unreadable on this target, and the composite parent never appeared

Observed at bench session 3, position D, on the shipping `0.0.0.5` build. The
Sound Blaster X4 (`041E:3278`) produced one new devnode, an HID device with a
yellow bang at Code 10, and no `USB Composite Device` parent, no audio devnode,
no CDC devnode, nothing under Other devices, and no bugcheck.

Task 13-E.3's clause 3 is therefore unreadable on this target, and that is the
result. An Endpoint Context is built only when a class driver selects an alt
setting carrying the endpoint. Nothing selected one, so the isochronous
`bInterval` 3 and 4 the X4 carries (Interval 2 and 3 through
`XhciIsochIntervalFromBInterval`) were never presented to the derivation. Every
isochronous endpoint this project has ever built a context for is still Interval
0, in every vehicle, and the one held device that could have changed that could
not get far enough. The clause is recorded as read-and-unreadable rather than
left blank, which is what stage E6.3's template asks for.

The bind question the device was carried to answer is answered, and the answer
is narrower than "no". Row 6 of the device table converted from "buy or publish"
into "carry it and find out" on the specific question of whether `usbaudio.inf`
matches a UAC 2.0 device at all. It never got that far: the failure is one level
above the audio driver, at composite parsing, so this run says nothing about
whether a UAC 1.0 `usbaudio.inf` would match protocol 32. That question is still
open and this device cannot answer it here.

The absence of the composite parent is a reading with a control attached. On
this machine, this boot, this driver, a different composite (the UAC 1.0 unit)
bound and played, at a root port and behind a hub (E6.1, E6.2). So composite
support is present and working, and `usbhub.sys` has been in place since Finding
D. What differs about the X4 is that it is IAD-grouped: seven interfaces in two
interface associations. Windows 98's `usbhub.sys` is a 1999 binary and the
Interface Association Descriptor ECN is from 2005, so it cannot group interfaces
into functions.

That is a candidate explanation, not an observation, and it must not harden into
one. `test-equipment.md` gives the same warning about this device: "That is
reasoning about INF matching, not an observation - do not record it as one."
What is measured is one HID child at Code 10, no parent, on a machine where
another composite works. The differential is strong and the mechanism is
inferred. This project has met IAD-grouping as a real obstacle once before, in
another layer (task 9-V.2 measured the QEMU passthrough rung shut for
IAD-grouped multi-interface functions), which makes the candidate plausible
without making it measured.

#### The attribution check - TAKEN, and the driver is exonerated

Whether this driver enumerated the device correctly is a separate question from
whether Windows could bind what was enumerated, and a Code 10 child sits on the
Windows side of that line. The precedent for not assuming it is Finding 2, where
two other Creative units did not enumerate at all and the cause turned out to be
`bMaxPacketSize0`, a driver-side defect that first looked like a device-side
one. Two Regedit screenshots settled it.

`HKLM\Enum\USB\VID_041E&PID_3278` exists, with two instance keys under it. The
first reads:

```
Class          "HID"
ClassGUID      "{745a17a0-74d3-11d0-b6fe-00a0c90f57da}"
CompatibleIDs  "USB\CLASS_03&SUBCLASS_00&PROT_00,USB\CLASS_03&SUBCLASS_00,USB\CLASS_03"
DeviceDesc     "USB Human Interface Device"
Driver         "HID\0002"
HardwareID     "USB\VID_041E&PID_3278&REV_1070,USB\VID_041E&PID_3278"
Mfg            "(Standard device)"
instance key   29F7657FFDE2C119
```

The instance key is the device's own serial number, and it is right.
`test-equipment.md`'s device table row 6 records the X4's `Serial` as
`29F7657FFDE2C119`, read on the modern Windows host by `hub-characterise.ps1`
during batch 13-H. Windows 98 uses a device's `iSerialNumber` string as its
instance ID, and the string this driver delivered on real xHCI silicon matches
the one a different machine read off the same device five days earlier,
character for character. So the miniport enumerated the X4 correctly (device
descriptor, `bcdDevice` as `REV_1070`, and the string descriptors), and the
failure above it is Windows'. That is a cross-vehicle confirmation of an
enumeration, about as clean as attribution gets here.

The same screenshot carries the control that upgrades the IAD reading from
inference to differential. The `Enum\USB` tree shows, on this machine:

| Device | Keys present |
|---|---|
| `VID_041E&PID_323D` | plus `&MI_00` and `&MI_03` |
| `VID_0D8C&PID_0014` | plus `&MI_00` and `&MI_03` |
| `VID_045E&PID_0750` | plus `&MI_00` and `&MI_01` |
| `VID_041E&PID_3278` | no `&MI_` key at all |

`&MI_nn` is what Windows creates for the interfaces of a composite device. Three
composites on this machine have them; the X4, which has seven interfaces, has
none, and Windows instead generated whole-device compatible IDs of
`USB\CLASS_03...`, binding the entire device as one HID. Windows 98 did not
merely fail to load a driver for the X4's functions: it never saw it as a
composite. The IAD explanation now has a same-machine, same-boot, same-registry
control behind it rather than resting on the 1999-versus-2005 dating alone. It
is still not measured, since nobody read what Windows did with the device
descriptor, but it is no longer bare inference.

#### One thing in those screenshots is unexplained, and it is not being written off

There are two instance keys under `VID_041E&PID_3278`, not one:

```
29F7657FFDE2C119   Driver "HID\0002"   Serial blob 0a 00 00 ...
657FC119F765E2C1   Driver "HID\0001"   Serial blob 09 00 00 ...
```

The first is the device's true serial. The second is sixteen hex characters
built from the same material in a different order: `657F` and `C119` are groups
2 and 4 of the real serial, and `F765`/`E2C1` straddle the boundaries of groups
1-2 and 3-4. It is not a Windows duplicate-instance suffix, which would be
`&0`/`&1`.

Two readings, not close in seriousness:

- Benign: the device was enumerated twice at different addresses (the binary
  `Serial` values differ, `0a` and `09`, which look like device address or
  port), and the odd key is a one-off artefact of an interrupted or repeated
  enumeration.
- Not benign: a string descriptor came back scrambled from a control IN
  transfer. That would be data corruption on the enumeration path, in this
  driver, on the one device in the bag whose descriptors are unusual. This
  driver carries compensation for a QEMU short-packet non-conformance that,
  until stage E4.4 at bench session 2, had never met a conforming controller.

The discriminator was run the same session, and it points at the benign reading
without reaching it. Two facts came back:

- The X4 was plugged in more than once before the screenshots were taken, so
  two instance keys from two enumerations is expected arithmetic. Only the name
  of the second one was ever the anomaly.
- Two further replugs produced no third key. Windows matched the device to an
  instance it already had, both times, which it can only do by reading the
  serial and finding it familiar.

So the scramble did not reproduce in two attempts, and it is not a live defect.
It is also not explained: across the enumerations that happened, one produced an
ID that is a re-ordering of this device's true serial, and nothing here says
which one or why. Two replugs is a weak negative, the same weak negative Finding
G's fifteen clean cycles were, right before device variety wedged the port on
the first alternation.

Recorded as an open question that gates nothing. It owns no task, it blocks no
clause, and it must not be written up as either a defect or a non-event. The
experiment that would close it: delete both instance keys under
`VID_041E&PID_3278`, then replug the X4 a counted number of times and see how
many keys come back and what they are named. That converts "it did not happen
twice" into a rate, which two replugs against a registry that already knew the
device cannot give.

The bound on this result, stated because the clause's own standard is duration:
E6.1 played for about seven seconds, the clip's length, played to its end rather
than stopped. E6.2's duration was not separately recorded. Isochronous faults
are the kind that appear over time (drift, ring wrap, a starved buffer), so a
clean minute would have been worth much more than a clean seven seconds, and
nobody should read these two rows as a soak test. What they establish is solid
and narrower than "audio works": the device binds, the isochronous path carries
real audio on real silicon, and it does so through a real transaction
translator. None of that was true of this project an hour before. A future
session wanting the stronger claim needs minutes, not seconds, and should say so
when it takes them.

---

## Stage L3 - the shipping binaries on the machine that refused one (task 13-L.3)

Task 13-L.3, which is batch 13-L and not 13-E. The plan was written into this
sheet before the session, for the reason the top of this file gives: a clause
decided in front of a running machine is decided by whoever is standing there.
`docs/contributing/roadmap.md`, task 13-L.3, is the authority for what this
owes; this stage is how, and where the two disagree about a clause the roadmap
wins. The session has since run; its RUN box and decode are at the end of the
stage.

Why a batch 13-E sheet carries a batch 13-L stage: one machine, one set of
standing rules, one bag, and one operator. A second file would have duplicated
the three rules that have each already cost a boot here and left the two copies
to drift.

What this stage is not: it is not the release-flavour DebugView boot. That one
asks which sink the bugcheck evidence leaves by, it owns no task, it was
dropped, and it is still a free boot in the table above. This stage asks the
opposite and much simpler question: do the two binaries batch 13-L is about to
publish install, load and drive devices here (the `debug` one being the file
that gave this machine `Code 2` at `0.0.0.4`), and can a shipping binary report
off it?

### Three standing rules for this machine, and one new consequence

1. Never install by INF here. An update-over-install bugchecks this machine at
   `0028:C00312EE` (P10-BENCH; item P14 corrected the sheet for it). Every
   binary below goes on by `ren` + `copy`. That is not in tension with the
   roadmap's "both binaries, from built media, never a hand-copied `.sys` and
   `.inf`": what that clause forbids is a `.sys` picked out of a build tree
   with no packaging gate behind it and an INF that never met `check-inf.ps1`.
   The binaries below come out of `make-package.ps1`, so they are media
   artifacts; only the act that puts one on this machine changes. The
   per-target `usbd.sys` reason the clause exists for is already discharged on
   this machine (`usbd98.sys` and `usbhub98.sys` were placed by the P14 /
   `0.0.0.5` deployment and neither flavour changes them), and a user-style
   install from a zip is the acceptance run's act, on `1.0.0.0`, which
   spending it here would consume.
2. This machine cannot warm-restart. Every restart below is a full power-off
   cold boot. A swap therefore never costs a boot of its own: restart to MS-DOS
   mode, swap, power off, come back up.
3. Do not run DebugView. Three device classes, three bugchecks; see "If
   something goes wrong". The new consequence: `XhciLogDebugView` must read 0
   for the whole of this stage, not because it is dangerous with no viewer
   running but because it is the confound. Task 13-L.2's claim is that the ring
   records with no sink selected at all, and a machine with the DebugView
   switch set records for the old reason too. The 2a guest carried a stale
   `XhciLogDebugView = 1` from the 11-V.9 work and its first level-2 dump
   proved nothing. This machine's was measured 0 at bench session 3, which is
   reported state and gets checked anyway. `XHCISNAP` cannot clear that value;
   only `regedit` can.

### L3-0. On the host, before the trip

Everything in this section is a host act. The bench has no `dumpbin`, no
hasher, no offset table and no way to build any of the three.

```
scripts\build-driver.cmd both
powershell -File scripts\package\make-package.ps1 -Flavor release
powershell -File scripts\package\make-package.ps1 -Flavor debug
xhcisnap\build.cmd
scripts\local\regen-offsets.cmd
```

In that order, from one tree, with nothing else built in between. `both` is
the two shipping flavours. The packager runs the import and INF gates and the
flavour-marker check on each image. `regen-offsets.cmd` must run last and from
the same tree, because a dump decodes only against its own tree's `offsets.txt`,
and a table regenerated from a different checkout gives plausible wrong numbers
rather than an error.

- `qemu` is not built for this trip and does not travel. It carries the
  port-`0xE9` mirror and `HAL.dll!WRITE_PORT_UCHAR` (the sole import delta of
  the build that gave this machine its `Code 2`, cause still open per P6), and
  it is never published. If it is ever installed here it is as a deliberate
  probe under a name that cannot be mistaken for either candidate, and this
  stage does not ask for one.
- Read both import tables here and write the delta down. The roadmap wants a
  pass that says why it passed, and Windows 98 cannot read an import table:

  ```
  tools\MSVC600\VC98\BIN\dumpbin.exe /imports out\pkg-release\xhci98.sys
  tools\MSVC600\VC98\BIN\dumpbin.exe /imports out\pkg-debug\xhci98.sys
  ```

  The expected reading is that neither carries `HAL.dll!WRITE_PORT_UCHAR`, and
  that the `debug` table is otherwise the `release` table. Record both
  module/symbol lists in the RUN box. This is a static reading and must be
  tagged as one; what the bench adds is the runtime half, that the file whose
  table this is actually loads.
- Hash both, and hash the tool: `certutil -hashfile <file> SHA256`.
  Identification is this machine's oldest trap. `0.0.0.2`, `0.0.0.3` and
  `0.0.0.4` are all 81,899 bytes, and a candidate instrument has already
  collided with a stale build at an identical size; `dir` identifies nothing
  here. Hash the copy that actually travels, and hash it last. A re-link moves
  the PE timestamp with no source change at all: the stage L3 session found
  `vm\xfer*\CHECKED\xhci98.sys` byte-different from `src\objchk\i386\xhci98.sys`
  at identical sources, same size, different hash, because `build-driver.cmd`
  had re-linked after the staging. Build, then package, then copy into the kit,
  then hash. Anything built after that invalidates the manifest.
- Rebuild the tool rather than trusting a staged copy, and hash the result.
  `XHCISNAP.EXE` was rebuilt five times for that session (task 13-L.6) and
  every rebuild moves the bytes. The tool refuses any driver whose reply
  signature, schema version or header size is not the one it was built
  against, so a stale tool fails safely, but it costs a boot.
- Check `XHCISNAP_VERSION` before travelling, and do not bump it here. It read
  `0.0.0.5` at the time of writing and prints into the header of every report;
  the bump to `0.0.0.6` is task 13-L.4's, after this bench, along with the
  other six copies. A tool headed `0.0.0.5` at this session is correct: it is
  the version the machine is coming from.

Stage the kit at `out\bench-13l\`, with a `MANIFEST.txt` in the shape item P14
established: the two `xhci98.sys` candidates renamed so a DOS prompt can tell
them apart, `XHCISNAP.EXE`, the `0.0.0.5` reference, and the SHA-256 list.

| In the kit as | What it is |
|---|---|
| `L3REL.SYS` | `out\pkg-release\xhci98.sys`, the candidate `release` flavour |
| `L3DBG.SYS` | `out\pkg-debug\xhci98.sys`, the candidate `debug` flavour, the file this stage exists for |
| `R0005.SYS` | `out\bench-13e-0005\driver-release\xhci98.sys`, shipping `0.0.0.5`, the binary this machine is carrying and the control it is read against |
| `XHCISNAP.EXE` | freshly built, hashed, `XHCISNAP_VERSION 0.0.0.5` |
| `MANIFEST.txt` | every hash above, both import tables, and the tree's commit |

Also in the bag (`test-equipment.md`'s requirements table is the authority;
this stage asks for a subset of it): the High-Speed flash drive, the Low-Speed
mouse (`046D:C077`), a second flash drive for the dumps, the multi-TT hub
`1A40:0201` and its barrel-jack PSU with one ordinary child, and, if the P6
rider is taken, `out\bench-13e-2b\`'s two binaries.

### The boot order, and why the batch's own clause goes first

Seven Windows sessions, each short. The ordering rule is this sheet's usual one
with one addition: the clause the whole batch turns on is not put behind five
boots of ladder work. If the session ends early it must end having answered
whether the diagnostic binary starts on this machine, because that is what
`0.0.0.6` is about to claim.

| Boot | Binary | Level | What it answers | Owed by |
|---|---|---|---|---|
| 0 | `0.0.0.5` as found | as found | the machine's state, and the route control: does `-probe` reach a miniport here at all | this sheet's read-the-machine rule |
| 1 | `L3DBG` | default (off) | does the debug binary load, and does it drive devices. Plus its level-0 reading | 13-L.3 clause 3 |
| 2 | `L3REL` | default (off) | release loads and drives, no regression against `0.0.0.5`. Plus the level-0 reading that counts | clauses 2 and 6, ladder rung 1 |
| 3 | `L3REL` | 1 | the dump comes back with counters and an empty ring | ladder rung 2 |
| 4 | `L3REL` | 2 | reproduce, then the headline dump: a non-empty ring with no sink selected | clauses 5 and 7, ladder rung 3 |
| 5 | `L3DBG` | 2 | the same channel out of the other shipping flavour | clause 5's "take `debug` as well" |
| 6 | whatever is being left | 0 | retrieve, switch the channel off, and write down what is on the machine | this sheet's restore rule |

The minimum viable session is boots 0, 1 and 2. They answer the batch's
question and the control that attributes a failure to the tree rather than to
the flavour split. Boots 3-5 are the log channel's own readings and are worth
the trip on their own, but a session that has to stop stops after boot 2 and
says so.

`debug` is installed first and `release` second, which inverts the roadmap's
reading order for the dumps: the dump clause says take `release` first, and
this stage does (boot 4 is before boot 5). What moves is the load check. A
`Code 2` on boot 1 changes the rest of the session (the release control at boot
2 becomes the attribution reading and the ladder is cancelled), while a `Code
2` discovered at boot 5 has cost six boots to find. Neither order is free:
taking `debug` first means the first `-probe` under a candidate binary is taken
on the flavour that has failed here before, so if it declines, boot 2 is what
says whether that was the flavour or the default.

### L3-A. Boot 0 - read the machine as found, and take the route control

Windows 98. Touch no USB device until the two readings below are taken. Copy
the kit onto the machine from the transfer drive first:

```
md C:\XHCI13L
copy <the flash drive>\*.* C:\XHCI13L
copy C:\XHCI13L\XHCISNAP.EXE C:\
```

A0. What is actually installed.

```
fc /b C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS C:\XHCI13L\R0005.SYS
```

`no differences encountered` identifies shipping `0.0.0.5` and is the expected
answer; the enclosure visit left it there. Anything else means something is
installed that nobody wrote down, which retrospectively qualifies every
observation since. Write down the size and work out which build it is before
continuing.

A1. The two registry values, in `regedit`. Edit -> Find, `XhciLogDebugView`,
and read every key it finds:

- `XhciLogDebugView` must be 0. If it is 1, set it to 0 and record that it was
  found set (rule 3 above; the whole level-2 reading depends on it).
- `XhciLogVerbosity` is expected to be absent entirely, not 0. It is a value
  the INF writes at install time and this machine's install predates it; the
  `.sys` swaps since did not re-run the INF. Absent and 0 mean the same thing
  to the driver (the default is off), and the distinction matters only so the
  operator is not surprised into thinking they have found the wrong key.

A2. The route control, the cheapest reading of the session:

```
C:\XHCISNAP -probe > C:\L3P0.TXT
more < C:\L3P0.TXT
```

The expected answer is `the request reached a miniport and it DECLINED`.
`0.0.0.5` has no snapshot channel at all (task 13-R.4 removed it before that
cut), so this is a binary answering as a switched-off one would, which is
section 5 rule 1 working as designed. What it establishes is the route:
usbport's HCD link exists on this machine, `\\.\HCD0` opens, and the dispatcher
parses this tool's buffer. Check the four controls read 0/2/4/7 as the probe
expects. If the probe cannot open anything, try `-c 1` and `-c 2` before
concluding anything (`passthru-snapshot-instrument.md` section 9, trap 3);
nothing later in this stage is interpretable until it does.

This is also the reading that makes boot 2's `-probe` mean something. Two
different binaries decline for two different reasons and the tool cannot tell
them apart; what separates them is that the operator knows which file is
installed, by `fc /b`, on both occasions.

Then: restart to MS-DOS mode, swap in the debug candidate, power off.

```
ren C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.P05
copy C:\XHCI13L\L3DBG.SYS C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS
fc /b C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS C:\XHCI13L\L3DBG.SYS
```

Keep `XHCI98.P05`. It is the way back to a known machine, one copy and one
boot, and the previous session's `XHCI98.SAV` could not be found when a restore
was planned around it.

### L3-B. Boot 1 - the debug flavour, and this is the clause the batch turns on

Windows 98. `L3DBG.SYS` is installed and the channel is at its default, which
is off.

B1. The devnode. Device Manager -> USB controllers. A pass is: the controller
is named, there is no yellow bang, and there is no `Code 2`. Photograph it
either way. Then Driver -> Driver File Details and photograph that too, and
read the file it names rather than the version beside it. Device Manager's
Driver Version is the INF's `DriverVer` recorded at install time, so after a
`.sys` swap it names `0.0.0.5` and is stale by construction. It is not evidence
about the running binary.

B2. It drives real devices. The clause says loads and drives, and a devnode
with no bang is only half of it. The same classes this machine already
exercises, at position D (the left-hand Always On connector, xHCI port 3):

1. the High-Speed flash drive: a drive letter, and a file written and read back
   with the contents matching;
2. the Low-Speed mouse: correct VID/PID, pointer moves;
3. the multi-TT hub at position T on its PSU with one ordinary child behind it:
   the child named and working.

B3. The level-0 reading, on this flavour:

```
C:\XHCISNAP -probe > C:\L3P1.TXT
```

`DECLINED` is the pass, the same sentence boot 0 produced from a binary with no
channel in it. This reading is the one most likely to be misread at the bench:
it is not a fault, it is not a missing channel, and nothing observable through
the IOCTL separates it from one. What separates them is the `fc /b` above.

If it answers instead, the channel is engaged at the default and that is a
defect against task 13-L.2. Record the `XhciLogVerbosity` found in A1 first,
because a key that already carried a nonzero level explains it without a
defect.

If the devnode shows `Code 2`, this is the batch's failure and the session
changes shape. Do not debug it at the bench. Go straight to boot 2, get the
release control, and file it. The two things that make the report worth
anything are the import tables recorded at L3-0 and the confirmation that the
installed file is the one whose table that is.

Then: DOS mode, swap in `L3REL.SYS`, power off.

```
ren C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.DBG
copy C:\XHCI13L\L3REL.SYS C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS
fc /b C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS C:\XHCI13L\L3REL.SYS
```

### L3-C. Boot 2 - the release flavour: the control, and the level-0 reading that counts

Windows 98. `L3REL.SYS` installed, channel still at its default.

C1, C2 and C3 are B1, B2 and B3 again, on this flavour, with identical pass
conditions. Take them in the same order and write them into their own rows.
This half is the control: if `release` also misbehaves, the reading is about
the tree and not about the flavour split, which is a materially different
report.

C4. No regression against `0.0.0.5`. The comparison is behavioural, because
this machine has no counter channel until the ladder is engaged. What `0.0.0.5`
does here is written down: stage E4's boxes and Finding W, Finding G's
five-event recipe at position D on shipping media with nothing wedging. Run
that recipe (flash drive, mouse, flash drive, all at D, unplugging between) and
record whether the port survives it. An escalation, a device that stops
enumerating, or a machine that needs a cold boot before the port notices
anything is a regression and is the finding.

C5. Engage the ladder's first rung:

```
C:\XHCISNAP -verbosity 1 > C:\L3V1.TXT
more < C:\L3V1.TXT
```

Read what it printed, per key. The ordinary case is one line per key,
`USB\000n  XhciLogVerbosity  was <n> -> 1`, with prose only where something is
anomalous. Every key on this machine must be written; the tool sets all of them
on purpose, because setting one of two and reporting success is how a user
restarts, reproduces on the other controller and dumps an empty ring. A key it
would not open, or a refused write, is recorded and is a defect against the
tool. `was` speaks about the next start and never about what the running driver
did; the value is read once per start.

Cold boot.

### L3-D. Boot 3 - level 1: the rung that must come back EMPTY

Windows 98, `L3REL`, `XhciLogVerbosity = 1`.

```
C:\XHCISNAP -probe > C:\L3P2.TXT
C:\XHCISNAP -o C:\L3H
```

Do not redirect a dump command. Earlier procedures in this sheet write
`XHCISNAP -o C:\NAME > C:\NAME.TXT` (P13-B, P11-B), and that pattern predates
the plain-text companion: since task 13-L.2 the tool writes `NAME.TXT` itself,
so the shell redirect and the tool open the same filename and the tool loses.
It says so plainly (`*** cannot create c:\NAME.TXT - the report will be on
screen only`) and then prints the report to the screen, which the redirect
captures, so nothing is lost and the collision is invisible unless that line is
read. It fired twice on the stage L3 session, at boots 4 and 5, and both
reports survived only because the redirect caught what the companion could not
write. A `-probe` still redirects safely: it writes no file of its own.

`-probe` must now say `the miniport ANSWERED`. That is the transition this
whole batch is about: the same binary that declined on boot 2 serves the
channel after one registry value and one restart, with nothing rebuilt.

Read the dump's summary. It is the last thing on screen, and on this controller
the header block will already have scrolled away. Four fields decide the rung:

| Summary line | Required |
|---|---|
| `driver` | `release`, extension 90272 bytes, schema 3 |
| `verbosity` | `read 1, APPLIED 1` |
| `note ring` | `0 of 16384 bytes   EMPTY - the report says why` |
| `coherence` | either answer is a reading; see below |

The empty ring is the pass, not a fault. This is the configuration the naive
merge of the two registry values would have deleted (channel open, ring off),
and it is why the ladder has five rungs. A level-1 dump that arrives with a
full ring means the recording default did not apply, which is a defect against
task 13-L.2 and the thing this rung exists to catch.

An `ExtensionBytes` that is not 90,272 voids the reading, and a wrong
`offsets.txt` produces a wrong decode rather than a failed one. The tool prints
the driver's own value for this reason; if it disagrees with the table
regenerated at L3-0, something other than the intended binary booted, whatever
`fc /b` said on disk.

On `coherence`: `*** TORN` means the counters may be a mixture across the
dump's windows. It fired on an idle 2a guest and not on a busy one, the
opposite of the intuition, so do not read it as a load signal. It does not
touch this rung's claim; an empty ring is empty in every window.

```
C:\XHCISNAP -verbosity 2 > C:\L3V2.TXT
```

Cold boot.

### L3-E. Boot 4 - level 2 on release: the headline dump

Windows 98, `L3REL`, `XhciLogVerbosity = 2`. This is the boot the batch's
headline claim rests on: a shipping binary carrying this driver's evidence off
a Windows 98 machine, which has never been done. Every previous reading of this
channel here (the batch 13-R boots, Findings R and S; bench session 3, Finding
V) was taken with an `XHCI_OBS_SNAPSHOT` build nobody could install.

E1. `C:\XHCISNAP -probe > C:\L3P3.TXT`, expecting `ANSWERED`.

E2. Give the ring something to record. This is the same work as B2 and C2: the
High-Speed flash drive at D with a file round trip, the Low-Speed mouse at D,
and the multi-TT hub at T with its child. Then Finding G's recipe once more at
D.

E3. An interim dump after the device work and before the recipe:

```
C:\XHCISNAP -o C:\L3A1
```

The ring is 16,384 bytes and it wraps. The `p12` set lost its earliest cycles
this way and only the interim dump carried intact ordering. Take ordering from
the interim dump and counters from the final one.

E4. The final dump, with the machine still up:

```
C:\XHCISNAP -o C:\L3A2
```

| Summary line | Required |
|---|---|
| `driver` | `release`, 90272, schema 3 |
| `verbosity` | `read 2, APPLIED 2` |
| `note ring` | non-empty, with `XhciLogDebugView` still 0 |
| `send` | the resolved absolute path; check it is on `C:` and not a transfer volume |

This is the reading nothing else in this project can produce: a ring that
filled with no sink selected at all, on the operating system where neither sink
can honestly be selected. Check `Log.BytesDropped` in both dumps; nonzero in
the second means take the ordering out of the first.

E5. Copy `C:\L3*.*` to the second flash drive now, before any swap. The `.TXT`
files are the reports; the `.BIN` files are what decode against `offsets.txt`;
the `.PSC` files are the PORTSC arrays.

Then: DOS mode, swap `L3DBG.SYS` back in, power off. Do not change the
verbosity value. It is per key, not per binary, so it carries across the swap,
and boot 5 needs it.

### L3-F. Boot 5 - level 2 on the other shipping flavour

Windows 98, `L3DBG`, `XhciLogVerbosity = 2` inherited.

```
C:\XHCISNAP -probe > C:\L3P4.TXT
C:\XHCISNAP -o C:\L3D2
```

Plug the flash drive and the mouse at D so the ring has content, then dump. The
summary's `driver` line must read `debug`, and that is the clause: the flavour
marker read back off the machine rather than inferred from what was staged.
Everything else matches boot 4's table.

Both regions whole at every engaged level is the property to confirm across
boots 3, 4 and 5 together: the `.BIN` is the full extension at level 1 as much
as at level 2. The ladder gates what is recorded and what the `.TXT` publishes,
never what the channel serves. A `.BIN` that is short at a lower level is a
serving ceiling, which this wire format cannot express and nobody intended to
build.

### L3-G. Boot 6 - retrieve, switch the channel off, and say what is left

```
copy C:\L3*.* <the flash drive>
C:\XHCISNAP -verbosity 0 > C:\L3V0.TXT
copy C:\L3V0.TXT <the flash drive>
```

`-verbosity 0` and `-disable` are one code path. Run it: while the channel is
on, any local user of this machine can read this driver's diagnostic state
through it. Confirm in `regedit` that every key reads 0, and that
`XhciLogDebugView` still reads 0.

What binary to leave installed was decided here rather than at the bench. Leave
`L3REL.SYS` in place, a departure from this sheet's leave-it-as-you-found-it
rule. Task 13-L.4 cuts `0.0.0.6` from this tree immediately afterwards, so the
machine ends up carrying the newest release rather than a superseded one, and
restoring `0.0.0.5` would spend a DOS trip to make the machine less current.
The rule the departure keeps is the one that matters: not that the state is
unchanged, but that it is written down.

```
fc /b C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS C:\XHCI13L\L3REL.SYS
```

Send that line and both registry values with the results. The P9 session did
not, and nobody knew what was installed at the start of the next one.
`XHCI98.P05` (shipping `0.0.0.5`) and `XHCI98.DBG` stay in
`C:\WINDOWS\SYSTEM32\DRIVERS\`; `C:\XHCI13L\` keeps the kit. If anything in
boots 1-5 failed, restore `XHCI98.P05` instead and say so. A machine left on a
candidate that did not pass is a machine whose next session starts confounded.

### What the reading has to show

The batch's own clause is one row:

| Reading | What it means |
|---|---|
| `debug` loads, no bang, no `Code 2`, and drives all three device classes | the pass the whole batch turns on. The flavour split repaired the `Code 2`, on the machine that produced it, and the recorded import tables say why |
| `debug` loads and `release` does not, or both fail | the reading is about the tree, not the flavour split. Neither publishes; task 13-L.4's release-notes row says so |
| `debug` shows `Code 2` again | the split did not repair it. The import delta is not the cause, or is not the whole cause, which is the question P6's two binaries were built to separate, and this is the session to take them |

The ladder, all four readings:

| Rung | Required | A failure means |
|---|---|---|
| 0 (default) | `-probe` says `DECLINED`, on both flavours | an engaged channel at the default, a defect against task 13-L.2 |
| 1 | counters present, ring empty | a full ring means the recording default did not apply |
| 2 | ring non-empty, `XhciLogDebugView` = 0 throughout | an enabled channel that still declines is the failure this task is looking for |
| every engaged level | the `.BIN` is the whole extension, both regions | a short `.BIN` at a low level is a serving ceiling that was never built |

And the identification clause, the one a bench forgets: every reading above is
void unless the RUN box also says which binary produced it (by `fc /b` against
the kit and by the `driver` line of the dump's own summary) and which level was
applied, from the dump header rather than from what was staged.

### The riders - taken if the session has room, reported separately either way

- P6's two binaries, `out\bench-13e-2b\`. They answer whether defect 2b's cause
  was the import or the port, which the flavour split does not: it removes the
  import from every published binary, making the symptom go away without
  settling the mechanism. Do not fold a green boot 1 into this question; the
  decision tree at P6 stays owed on its own.
- The release-flavour DebugView boot. Still a free boot with no task. If it is
  taken, it is taken after everything above, because it can bugcheck the
  machine.
- `HCCPARAMS2.FSC` rides free on any `XHCIQUAL xhci --probe-only` run; it was
  read `FSC=0` here at stage E0 in bench session 1 and needs no retake.

### Deviations

Filed, never fixed at the bench from memory: batch 13-E's rule, and it binds
here for the same reason. A defect against the driver goes to the issue tracker
with what the form asks for; a defect against this procedure goes back into
this stage afterwards.

### RUN box template

```
DATE:              ____________  operator: ____________
kit:               out\bench-13l\, commit ____________
                   L3REL.SYS  sha256 ____________  ______ bytes
                   L3DBG.SYS  sha256 ____________  ______ bytes
                   XHCISNAP.EXE sha256 ____________  version ________
import delta:      release vs debug = ____________________________
                   HAL.dll!WRITE_PORT_UCHAR present in: ____________
                   (STATIC - read with dumpbin on the host, nothing executed)

BOOT 0  as found
  fc /b vs R0005.SYS:      ____________________
  XhciLogDebugView:        ____   XhciLogVerbosity: ____ (absent expected)
  -probe:                  ____________________  controls: __/__/__/__

BOOT 1  L3DBG, default
  fc /b vs L3DBG.SYS:      ____________________
  devnode:                 ____________________  Code: ______
  HS drive at D:           ______  file round trip: ______
  LS mouse at D:           ______  VID/PID: ____________
  multi-TT hub + child:    ______
  -probe:                  ____________________   (DECLINED expected)

BOOT 2  L3REL, default
  fc /b vs L3REL.SYS:      ____________________
  devnode:                 ____________________  Code: ______
  HS drive / mouse / hub:  ______ / ______ / ______
  -probe:                  ____________________   (DECLINED expected)
  Finding G recipe at D:   ____________________   (no wedge expected)
  -verbosity 1, per key:   ____________________

BOOT 3  L3REL, level 1
  -probe:                  ____________________   (ANSWERED expected)
  driver / ext / schema:   ______ / ______ / ______   (release/90272/3)
  verbosity read/APPLIED:  ______ / ______
  note ring:               ______ of 16384          (EMPTY expected)
  coherence:               ____________________
  -verbosity 2, per key:   ____________________

BOOT 4  L3REL, level 2   ** the headline **
  -probe:                  ____________________
  interim dump L3A1:       ring ______  dropped ______
  final dump   L3A2:       ring ______  dropped ______
  driver / ext / schema:   ______ / ______ / ______
  verbosity read/APPLIED:  ______ / ______
  XhciLogDebugView:        ____  (0 REQUIRED for this reading to mean anything)
  send path:               ____________________
  devices exercised:       ____________________

BOOT 5  L3DBG, level 2
  fc /b vs L3DBG.SYS:      ____________________
  -probe:                  ____________________
  driver line:             ____________________   (debug expected)
  note ring:               ______ of 16384
  .BIN size at each level: L1 ______  L2 ______   (both 90272 expected)

BOOT 6  restore
  -verbosity 0, per key:   ____________________
  XhciLogDebugView:        ____   XhciLogVerbosity: ____
  LEFT INSTALLED:          ____________________
  final fc /b:             ____________________
  files retrieved:         ____________________

RIDERS
  P6 decision tree:        ____________________ (taken / not taken)
  DebugView + release:     ____________________ (taken / not taken)

DEVIATIONS FILED: ____________________________________________
```

RUN box, the stage L3 session. It ran, and every clause of task 13-L.3 was
observed. Filled from the bench readings as they were taken, one boot at a
time; the plan above is left intact.

```
SESSION:           stage L3   operator: project owner
kit:               out\bench-13l\, working tree at c0361ef (+ docs edits)
                   L3REL.SYS  sha256 BAAD49CF...  82,203 bytes
                   L3DBG.SYS  sha256 84708F2C...  82,811 bytes
                   XHCISNAP.EXE sha256 B5E2DEA5...  version 0.0.0.5
import delta:      NONE - the two tables are IDENTICAL, eleven module/symbol
                   pairs each, same order, same three modules
                   HAL.dll!WRITE_PORT_UCHAR present in: NEITHER
                   (STATIC - dumpbin on the host, nothing executed)

BOOT 0  as found
  fc /b vs R0005.SYS:      no differences encountered - pristine 0.0.0.5
  XhciLogDebugView:        0      XhciLogVerbosity: ABSENT (as expected)
  driver keys found:       ONE
  -probe:                  DECLINED    controls: 6 / 2 / 4 / 7

BOOT 1  L3DBG, default
  fc /b vs L3DBG.SYS:      no differences encountered
  devnode:                 NAMED, NO YELLOW BANG      Code: NONE
  HS drive at D:           drive letter  file round trip: fc /b MATCHED
  LS mouse at D:           enumerated    VID/PID: correct
  multi-TT hub + child:    both named and working
  -probe:                  DECLINED  (the shipped default is off)

BOOT 2  L3REL, default
  fc /b vs L3REL.SYS:      no differences encountered
  devnode:                 NAMED, NO YELLOW BANG      Code: NONE
  HS drive / mouse / hub:  pass / pass / pass  (round trip fc /b matched)
  -probe:                  DECLINED
  Finding G recipe at D:   CLEAN PASS, all three events, no delay
                           -- but see THE ONE-OFF below, which happened on an
                              earlier attempt at the same recipe in this boot
  -verbosity 1, per key:   one key, was ABSENT -> 1

BOOT 3  L3REL, level 1
  -probe:                  the miniport ANSWERED
  driver / ext / schema:   release / 90272 / 3
  verbosity read/APPLIED:  1 / 1
  note ring:               0 of 16384    EMPTY  <-- the pass
  -verbosity 2, per key:   one key, was 1 -> 2

BOOT 4  L3REL, level 2   ** the headline **
  -probe:                  the miniport ANSWERED
  interim dump L3A1:       ring NON-EMPTY, well under 16384, no wrap
  final dump   L3A2:       ring NON-EMPTY, coherence: NO TEARING
  driver / ext / schema:   release / 90272 / 3
  verbosity read/APPLIED:  2 / 2
  XhciLogDebugView:        0  - checked again at this boot, still 0
  Finding G recipe at D:   CLEAN PASS, with the ring recording underneath it
  devices exercised:       HS flash drive (round trip), LS mouse, multi-TT hub
                           with one child, all at D / T

BOOT 5  L3DBG, level 2 (inherited across the binary swap)
  fc /b vs L3DBG.SYS:      no differences encountered
  -probe:                  the miniport ANSWERED  <-- the level carried across
                           the swap, which is what "the value is per KEY, not
                           per image" means when it is observed rather than
                           asserted
  driver line:             debug / 90272 / 3
  note ring:               non-empty
  .BIN size at each level: ALL 90,272 - level 1 and level 2 alike

BOOT 6  close-out
  -verbosity 0, per key:   one key, was 2 -> 0; regedit agrees
  XhciLogDebugView:        0        XhciLogVerbosity: 0
  LEFT INSTALLED:          L3DBG.SYS - the DEBUG candidate, 82,811 bytes,
                           sha256 84708F2C...   ** NOT a release build **
  final fc /b:             no differences encountered
  files retrieved:         L3P0-L3P5, L3H, L3A1, L3A2, L3D2, L3V0-L3V2
                           (.TXT, .BIN and .PSC), copied off before any swap

RIDERS
  P6 decision tree:        NOT TAKEN - still owed, see below
  DebugView + release:     NOT TAKEN - still a free boot with no task

DEVIATIONS FILED: none against the driver. One against the HOST TOOLING, found
                  and fixed before the trip - see "What L3-0 caught" below.
                  One bench OBSERVATION with no owner yet - see THE ONE-OFF.
```


#### The decode, taken on the host - and the level-1 dump is the strongest reading of the session

Every `.BIN` decodes against the `offsets.txt` regenerated at L3-0, which
reports `SIZEOF 90272` and matches the `ExtensionBytes` every dump header
printed at the bench. The raw `.BIN` and `.PSC` dumps were discarded once
decoded, as the earlier sessions' were; the companion reports and probe reads
(`l3h.txt`, `l3a1.txt`, `l3a2.txt`, `l3d2.txt`, `l3p0`-`l3p5`, `l3v0`-`l3v2`)
are in `run-13e-evidence/`. The readings below are the citable record, per this
file's rule that a decoded reading is written out rather than left in a file a
clone does not have.

The level-1 rung, stated as a measurement rather than as an absence. This is
the reading that could not have been taken anywhere else:

| `l3h.BIN` | value |
|---|---|
| `Log.Verbosity` / `VerbosityRead` | 1 / 1 |
| `Log.Enabled` | 0 |
| `Log.Used` / `Log.Appends` | 0 / 0 |
| `Log.Suppressed` | 62 |
| `Log.DebugViewEnabled` | 0 |

Sixty-two appends were offered and every one was turned away, while the
counter block came back whole. That is channel open, ring off, the
configuration the naive one-value merge would have deleted, demonstrated with a
number rather than an empty file. An empty ring on its own is consistent with a
channel that was never reached; `Suppressed` at 62 says the producers ran and
the ladder refused them.

The level-2 rung proves the batch's second clause out of the driver's own
counters rather than out of `regedit`:

| `l3a2.BIN` | value |
|---|---|
| `Log.Verbosity` / `VerbosityRead` / `VerbosityRefused` | 2 / 2 / 0 |
| `Log.Enabled` | 1 |
| `Log.Used` / `Head` | 3,729 / 3,729, no wrap |
| `Log.Appends` | 159 |
| `Log.Truncated` / `Log.BytesDropped` | 0 / 0 |
| `Log.DebugViewEnabled` | 0 |
| `Log.DebugViewEmits` / `DebugViewBytes` | 0 / 0 |

`Log.Enabled` is 1 while every DebugView field is 0. The ring recorded 159
records and the sink emitted nothing, because there was no sink. That is the
whole of what task 13-L.2 changed and the thing that cannot be checked on any
other operating system. Before that task `Log.Enabled` was
`FileEnabled || DebugViewEnabled`, so this configuration recorded zero.

`Log.Suppressed` is 2 at level 2. That is not a defect; since task 13-L.2 the
field counts two different refusals, and an ADDRESS record is refused at every
level below 4. Two address records offered at level 2 is the ordinary case.

The recovery ladder never fired at all, which is the strongest form "no
regression" can take:

| counter | value |
|---|---|
| `RecoveryAttempts` / `Completions` / `Failures` | 0 / 0 / 0 |
| `ResetControllerCalls`, `FatalStatusDetected`, `HostControllerEventResets` | 0 |
| `CommandAgeAborts` / `CommandAgeResets` / `CommandAgeEscalated` | 0 / 0 / 0 |
| `CommandsIssued` / `CommandsCompleted` / `CommandsTimedOut` | 63 / 63 / 0 |
| `CommandFailures`, `CommandsAbandoned`, `CommandOwnerLost` | 0 |
| `RhPortsUnpowered`, `PortTeardownFailures` | 0 / 0 |

Finding G's recipe ran at position D underneath this and nothing escalated by
a single rung. Findings T, U and V all showed the repair working on
instrumented candidate builds; Finding W showed it on shipping media as an
absence, with nothing watching. This is the first time the repair has been met
by the recipe with the counters running, on a binary about to be published.

The latent defect did not fire, and this is the first time anyone could have
seen it either way on this machine:

```
PortEventsMapped     33
PortEventChanges     33     -> Mapped == Changes: every mapped event reached the fold
PortEventsUnmapped    0
RootHubInvalidatesOwed 0
```

`XhciRootHubPortEvent` consuming an event without acknowledging it would show
as `Mapped > Changes`. It is clean here. That says nothing about the one-off
below, which happened at boot 2 with the channel switched off. That is why
the reading is named for the next session rather than treated as settled.

A poll-rate measurement fell out for free, and it corroborates Finding V:

```
PollClockMs      530,521      HealthPolls  16,381      PollClockStalls  0
530,521 / 16,380  =  32.4 ms per CheckController poll
```

Finding V measured 36-80 ms on this machine at bench session 3 and withdrew
Finding U's inferred ~1 ms. 32.4 ms is the same order and the same story,
taken independently five sessions later on a different binary. `HealthPolls`
equals `CheckCallbacks` (16,381), so the poll declined zero times.

#### One STALL, fully recovered - and a teardown code worth a decision

The note ring carries a single transfer error and its recovery. The label
encodings are read out of the tree: `src/xhci_slot.c:8083` packs `xfer.error`
as `slotId << 16 | dci << 8 | completionCode`, and `src/xhci_slot.c:2889` packs
`ep.recovery` as `Dci << 16 | op << 8 | completionCode`, with the ops at
`src/xhci.h:3811` and the codes beside them:

```
ep.open=00070302 / ep.open=00070402     slot 7 - the device behind the hub
                                        (slot.route 01000005, parenthub slot 5)
xfer.error=00070306                     slot 7, DCI 3, CC 6  = STALL_ERROR
ep.halted=00000003                      DCI 3 halted
ep.recovery=00030801                    DCI 3, op 8 RESET_EP, CC 1 = SUCCESS
```

That is a stall handled correctly, and an ordinary USB event: a device
rejecting a request. The counters bound it: `EndpointHalts` 1,
`TransfersOnHaltedEndpoint` 1, `EndpointResetsNotHalted` 0, `DevicesStalledOut`
0. One halt, one transfer that arrived on it, one reset, done.

The other code is worth more attention. It appears four times and always beside
a disconnect:

```
port.disconnect=00000300
ep.recovery=00030713                    DCI 3, op 7 STOP_EP, CC 19 =
                                        CONTEXT_STATE_ERROR
slot.disabled=...
```

A Stop Endpoint returning Context State Error means the endpoint was not in
the state the command required. On a disconnect, where the device is already
gone and the context has left Running, that is the expected answer. Nothing
downstream misbehaved: every teardown completed, `PortTeardownFailures` and
`PortTeardownSkipped` are both 0, and `CommandsCompleted` equals
`CommandsIssued`.

It is recorded here because it is new information rather than because it is a
fault. This is the first time this project has watched its own teardown path
on Windows 98 bare metal, and it lands next to Finding S's territory, whose
headline ("a Stop Endpoint that never completes") Finding V already refuted.
This corroborates that refutation from a third direction: the command completes
promptly, with a non-Success code.

The driver already treats the code as expected, to the letter of the spec.
`src/xhci_slot.c:2906` exempts it from the failure path outright, then does
what xHCI 4.6.9 p.123 tells software to do ("Software may verify that this
case occurred by inspecting the EP State ... when a Stop Endpoint Command
results in a Context State Error"): it reads the hardware state and branches
four ways. Halted resets first, because Set TR Dequeue is refused from Halted
(4.6.10 p.126). Error places a Set TR Dequeue and does not drain, because the
xHC may still own prefetched TRBs. Stopped is treated as stopped. Disabled is
marked `XHCI_EPQ_NO_CONTEXT` so no restart path rings a doorbell 4.8.3 p.150
forbids. Only Running, which would contradict the code the xHC just gave, is a
failure.

The counters say every branch behaved: `EndpointStops` 6,
`EndpointStopFailures` 0, `EndpointDequeueSets` 4 with 0 failures,
`EndpointResetsNotHalted` 0. All four Context State Errors were counted as
stops.

What is open is the log gate and nothing else. `ep.recovery` fires on
`completionCode != SUCCESS || HALTED`, so an outcome the driver treats as
normal still writes a record carrying the raw code with no hint that it
resolved cleanly, which is what cost a reader twenty minutes on the evening
this was decoded. There is a real argument for leaving it as it is: that record
is the only reason the teardown path is visible at all, and the site's own
comment says the tier exists so that "a chain that will not converge shows up
as repeated records with the same code - a reading rather than a flood".
Silence the expected case and a stuck chain looks like nothing until it
floods. The present cost is negligible: four records out of 159, in 3,729
bytes of a 16,384-byte ring.

Deferred at the project owner's direction; it is not a defect. No driver change
may land before `0.0.0.6`, because the value of this session is that the
binary being cut is the one that ran on this machine, and a change to `src/`
now would put task 13-L.4's release-notes row back to "expected to work". This
paragraph is the record of it; no phase owns it.

#### The controller's own facts, for the record

The ring's opening block is the controller sheet, read at last from the driver
rather than from the qualifier: `hc.pci=9D2F8086`, `hc.version=00000100`, 64
slots, 18 ports, 8 interrupters, 34 scratchpad pages, 32-byte contexts. The
port map: 12 managed, 6 USB2-only, 6 USB2 companions, 6 USB3 left unpowered by
design, which is what the PORTSC table's six `PP = 0` rows are. The tool now
says so in as many words rather than staying silent, the section 8 defect task
13-L.2 fixed.

#### What L3-0 caught, and it would have cost the trip

`scripts\local\offsets.c` did not compile against the tree, and
`regen-offsets.cmd` refused rather than producing a table. It still named
`Log.FileRequested`, `Log.FileEnabled` and `Log.FileStatus`, the ring-0 file
sink's fields, which task 13-L.2 removed from the driver. The generator is
git-ignored per-host tooling, so nothing gated it when the struct moved: no
host test reads it, no driver gate sees it, and the packager has no opinion
about it.

It was fixed on the host and the comment above the block rewritten for the
verbosity ladder. `offsets.txt` now regenerates at 90,272, which was the
tree-head size at the time of the L3 session.

The failure was the good kind. A refusal costs minutes; a table that built from
a stale struct would have decoded every dump that night into plausible wrong
numbers, a wrong reading rather than a failed one, which is how this
investigation lost time before. It was caught on the host because L3-0 exists
and says to run it there.

#### THE ONE-OFF - a Low-Speed miss at D that did not reproduce

It happened once, on boot 2, and it is recorded rather than explained. On the
first counted attempt at Finding G's recipe under the `release` candidate, step
2, the Low-Speed mouse at position D immediately after the High-Speed flash
drive had been plugged and unplugged there, did not enumerate.

What was established immediately, with no reboot:

| Check | Result | What it rules out |
|---|---|---|
| HS flash drive back into D | letters normally | the port is not deaf. This is not Finding 3's signature |
| the mouse in a different port | works | the device is not dead |
| the mouse into D again | works | it does not reproduce |
| a clean counted recipe pass afterwards | all three events, no delay | the recipe itself passes on this binary |

It is an anecdote and it is being called one. A single non-repeating miss,
against a port proven alive seconds later and a device proven alive in another
socket, is not a finding and no mechanism is claimed for it. The first reading
taken at the bench leaned toward "the speed transition", which is Finding H's
claim; that reading was withdrawn the moment it failed to reproduce, and it is
recorded here as a withdrawn inference because the temptation to promote one
plug into a mechanism is one this sheet keeps catching.

What makes it worth carrying is that it has the shape of two latent defects
this project already has on the books and has never been able to read on this
machine: `XhciRootHubPortEvent` consuming an event without acknowledging it,
and `xhciRhWritePortsc` dropping a refused acknowledgement. Both are real, both
were exonerated as Finding 3's cause, and both have been unreadable here for
want of a counter channel.

That channel now exists on this machine. The next session that sees this has
an instrument for it, and the reading to take is `PortEventsMapped` against
`PortEventChanges` in a dump taken without a reboot in between. `Mapped`
exceeding `Changes` is an event mapped to a port and then dropped
unacknowledged, the first defect caught in the act. That reading had never been
available on Windows 98 bare metal before this session, and at the time it was
available only on this host: `PortEventsMapped` had no print site, so
`gen-offsets.ps1` never derived it and a clone's `offsets.txt` could not name
it. The 33 above was decoded through the git-ignored `scripts/local/offsets.c`.
It and `RootHubInvalidatesOwed` have print sites now.

It owns no task and blocks nothing. Whether it earns an issue is a decision for
the project owner: one non-reproducing miss is thin for the tracker, and the
argument the other way is that an unfiled observation is one nobody will
recognise the second time.

#### What the session establishes, in one place

1. The `debug` flavour loads on the E460, drives High-Speed bulk with a
   data-verified round trip, a Low-Speed HID, and a multi-TT hub with a child.
   This is the machine that gave the `0.0.0.4` debug binary a `Code 2`
   and loaded nothing, and that failure has never been reproducible on a
   guest, so no number of green 2a boots could have said this.
2. The static half says why: the two flavours' import tables are identical and
   `HAL.dll!WRITE_PORT_UCHAR` (the sole import delta of the build that gave the
   `Code 2`, cause still open per P6) is in neither. Read with `dumpbin` on the
   host, because Windows 98 cannot read an import table. Static, not runtime;
   the bench supplied the runtime half, that the file whose table that is
   actually starts.
3. `release` is no regression. It loads, drives the same three classes, and
   met Finding G's five-event recipe at position D twice with nothing
   escalating and no delay, once at level 0 and once with the ring recording
   underneath it. Finding W's bar, cleared on a candidate rather than on a
   shipped binary.
4. A shipping binary carried this driver's own log off a Windows 98 machine,
   which this project had never done. Findings R, S and V were all read with an
   `XHCI_OBS_SNAPSHOT` build that could not be packaged and that no user could
   have installed. What was unmeasured was a shipping binary doing it, and it
   is measured now: on `release` first, because that is what an ordinary user
   is running when something goes wrong, and then on `debug`.
5. The ring recorded with no sink selected at all. `XhciLogDebugView` read 0 at
   boot 0 and was re-checked at boot 4, and the ring filled anyway. That is the
   property task 13-L.2 created and the thing that cannot be checked anywhere
   else on this operating system, because Windows 98 is where neither sink can
   honestly be selected.
6. All four ladder readings came back, across the default: level 0 declines as
   a channel-less binary does; level 1 serves the counter block with an empty
   ring, the configuration the naive merge would have deleted; level 2 records;
   and every `.BIN` is 90,272 bytes at both engaged levels, so the ladder gates
   recording and publication and never what the channel serves.
7. The flavour marker and the applied tier were both read back off the machine
   rather than inferred from what was staged: `release` at boots 3 and 4,
   `debug` at boot 5, each with its extension size and schema beside it.
8. The verbosity level lives in the driver key and not in the image, observed
   rather than asserted: level 2 survived the binary swap between boots 4 and 5
   untouched.

#### What it does NOT establish

- Nothing about Windows 2000. No vehicle exists on real hardware, and this
  batch's Windows 2000 evidence is the 2b guest boot in task 13-L.1 and nothing
  else. This session must not be written up as if it covered both targets.
- Defect 2b's mechanism. The flavour split removes the import from every
  published binary, so the symptom is gone without it being settled whether the
  cause was the import or the port. P6's two binaries stay owed, not folded
  into this task's pass condition, because a repair that makes the question
  unaskable would be the worse outcome.
- Which sink the DebugView bugcheck leaves by. Still unmeasured, still a free
  boot with no task, not taken.
- The decode was outstanding when the RUN box was written. `ExtensionBytes` was
  confirmed as 90,272 in every dump summary at the bench, which is the clause
  that protects against a wrong decode; the `.BIN` files were then decoded on
  the host against the `offsets.txt` regenerated at L3-0, and the results are
  the tables above.

---

## If something goes wrong

- Do not run DebugView. Measured on this machine: with Capture Kernel active,
  plugging any hub raises `fatal exception 0E at 0028:C208D79D`; with DebugView
  not running the same hub on the same connector enumerates and works. A second
  observation widened it: a USB Audio device on a root port with no hub in the
  machine bugchecked at `0028:C207B26D`, the same region. A third, at the
  DebugView check: a Low-Speed HID mouse (`046D:C077`, `bInterval`=10) on a
  root port bugchecked at `0028:C20A3F4D`. Three device classes, three crashes,
  and the third is the lowest-interrupt-rate stimulus at the bench. There is no
  trace to be had here; that is what task 12.2 decided, and the decision does
  not reopen. See "Session record - the DebugView ban, measured", which also
  records why the channel could not have served Finding 3 even if it had been
  safe.
- A bugcheck leaves nothing behind. `C:\XHCI.LOG` does not reach a file system
  on Windows 98 at all (`\??\` does not resolve there and the two roots
  `ntkern.vxd` contains refuse), so the evidence is a photograph of the screen
  and a written note of what was plugged in where.
- Recovery: the Win98 Startup Menu and Safe Mode keep working input, because
  the boot menu runs in real mode with BIOS legacy-USB emulation fully active
  and Safe Mode never loads `xhci98.sys`, so its handoff never runs. The
  `BootMenu=1` path in `docs/contributing/build-and-test.md`, "Recovering from a
  driver that crashes at boot", works on this machine.
- Expect to lose the keyboard if the driver initialises and then fails.
  Firmware SMM emulation dies the moment the handoff runs. Plan every step so a
  hard reboot is an acceptable recovery.
- Do not disable the controller in Device Manager. Disabling any USB host
  controller devnode bugchecks Windows 98, `usbehci.sys` included, so it is not
  this driver.
- A `Code 2` on the controller devnode is not a composite-device finding. It
  means NUSB is absent and this driver's imports cannot resolve; that is how a
  base Windows 98 restore reads.

---

## Asset capture

A screenshot taken during this trip is free; recreating it costs the trip
again. Capture as you go, for the result boxes and for anything published
later:

- the controller's Device Manager entry, named and without a bang;
- the Driver tab and Driver File Details, which are also stage E1's evidence;
- the `Code 2` dialog, in whichever stage produces it;
- the hub tree at stage E3's deepest topology, with children bound;
- a device a viewer recognises working: a flash drive with a drive letter, the
  storage enclosure mounted, audio playing.

---

## Recording results

One directory for the session, plus `xhciqual/results/e460-<date>/` for the
qualifier logs and the `XHCIQUAL.MAP` of the exact build that produced them
(the `.EXE` itself is not archived). The session note names: the OS, the stack
build before and after stage E5, the controller ID, the driver's flavour and
build stamp, the devices actually presented, and which stages were attempted.

Batch 13-E's roadmap checkbox is not ticked on "it did not crash". Each stage
above has a reading, and a stage whose reading was not taken is a stage that
did not run.

Then report the filled templates back per "Reporting back" at the top of this
sheet. That is what turns bench notes into RUN boxes and ticked clauses.

---

## The second machine: the ThinkPad P14s Gen 1

Windows 98 SE is installed on it (project owner), which retires the one thing
that used to keep it out of this batch. It is Phase 0-qualified
(`xhciqual/results/p14s-gen1-2026-07-25/`, xhciqual v0.9, PM decode
cross-checked against `lspci` and bit-identical to the E460's PM words), its
Comet Lake xHCI is clean silicon with no quirk-table row, and Windows 2000
Setup bugchecks on it. So it is a Windows 98 vehicle only, and it belongs to
this batch or to nothing.

It still owns no clause, and that is a project-owner decision rather than
something to settle at a bench. The argument either way is short: every clause
in this batch is about the driver and the stack above it rather than about the
silicon, and two clean Intel generations are already covered, so a third
confirms rather than extends. What a second machine would buy is a control.
This batch has no failure branch and no second vehicle, so a result that turns
out to be about the E460 rather than about Windows 98 has nothing to be checked
against.

The rig fits it without modification. Vendor documentation gives this machine
two USB-A connectors: the left one, which this project mapped to controller
port 4 and which is therefore position D, and one on the right, which is the
Always On port and is position T once mapped. Its three USB-C connectors are
out of the rig. Note the mirror image of the E460, where the Always On port is
the left one and there are two spare sockets on the right; on this machine
there is no spare, so a staging flash drive occupies a rig position.

If it is taken, take it as stages E0 to E4 repeated after the E460 trip, not as
a new batch and not interleaved. E0 is in that range and is not optional here,
for the reason the first proviso gives. Two provisos:

- What is on that machine is unrecorded here. Only the install is reported;
  whether NUSB 3.3 is on it, and whether `xhci98.sys` has ever been bound
  there, are unknown. So a session there starts at stage E0 (the qualifier,
  which also ties the run to `xhciqual/results/p14s-gen1-<date>/`) and stage
  E1 (the identity row), and neither is optional the way it might be on the
  E460.
- Stage E5 is one-way on this machine too, and a machine that has not had the
  SE CD stack installed is worth more as a plain-NUSB control than as a second
  copy of the same measurement. If both machines are available, take the
  composite control (E2) on both and the SE CD install (E5) on one.
