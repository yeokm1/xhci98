# Development Roadmap - The Host Controller Driver, Phases 25 Onward

The third roadmap file, opened on 2026-10-02 at the project owner's request,
the day Phase 24 closed on the `1.2.0.0` cut. It carries every phase of the
miniport's successor: a monolithic USB host controller driver, `xhci98h.sys`,
that replaces `usbport.sys`, the hub driver and the composite parent with code
of this project's own and so can drive USB 3.x SuperSpeed devices, hubs and
UAS storage on the same targets the miniport serves. The first two files are
unchanged by it: [`roadmap.md`](roadmap.md) keeps the status, the conventions
and Phases 0-16, and [`roadmap-phases-17-on.md`](roadmap-phases-17-on.md)
keeps Phases 17-24, the whole of the miniport's life after `1.0.0.0`. Every
convention of theirs holds here: the batching convention and the task-id
rules, "observed on both" as `AGENTS.md` qualifies it, and the rule that no
phase advances past an unobserved checkpoint.

**The miniport is frozen.** `1.2.0.0` is the last release of `xhci98.sys`
(owner, 2026-10-02): no further miniport cut, and a defect reported against
it is answered by the successor rather than by a `1.2.x.0`. The miniport
stays the shipping download until `2.0.0.0` is cut, and its two roadmap files
are closed records.

## The decisions this roadmap rests on

All taken by the owner on 2026-10-02, when the phases below were drawn up.
Each is restated where it binds a task; this table is the index.

| Decision | Taken |
|---|---|
| Pure HCD, not a registry switch | The successor is a second binary in a second package, chosen at install time through Update Driver against the same PCI class id. The miniport imports `USBPORT_RegisterUSBPortDriver`, a load-time gate, so one binary could never both serve a stock Windows 98 SE and remain the miniport; and a switch would owe every HCD change a miniport regression matrix under the "switch off is byte-identical" rule |
| Binary and package name | `xhci98h.sys`, `xhci98h.inf` and `xhci98h-amd64.inf`, package `xhci98h-<version>.zip`. It coexists with `xhci98.sys` on disk, so a user swaps back with Update Driver |
| Version and cut point | One cut, `2.0.0.0`, after Phase 31: USB 2.0 parity on every target, SuperSpeed on root ports and behind SuperSpeed hubs, and UAS. No intermediate release |
| The miniport | Frozen at `1.2.0.0`, no further cuts (above) |
| Hub class and composite splitting | Inside the bus driver. External hubs are objects of the bus, not PDOs; a composite device is split by the bus into per-function PDOs. No hub-to-port contract to design, no second INF binding path, no per-target matching of `usbccgp.sys` or Windows 98's composite parent |
| Stock Windows 98 SE | A goal: the HCD loads, starts and binds devices on a Windows 98 SE install with no USB 2.0 stack. NUSB's and SweetLow's host-stack halves become inert under it. What a stock install still lacks is a mass-storage class driver, which NUSB's `usbstor.sys` and its `ntmap` layer supply and Windows ME ships; the HCD does not replace that. Phase 26 takes the reading (26-V.3); whether it is a checkpoint clause is the owner's call when that phase opens |
| Mass storage on Windows 98 SE | NUSB's, kept (owner, 2026-10-02, choosing this over an own storage driver). A drive letter on 9x comes from the IOS layer, and a WDM storage driver's disk objects reach it only through the NTMAP port driver (`NTMAPHLP.PDR`, `NTMAP.SYS`, `USBNTMAP.SYS`), Microsoft files of the 98 SE hotfix line (4.10.0.2223 to 2227, hotfixes 242975 and 267304) that NUSB carries and this project does not redistribute. An own BOT driver would still sit under NTMAP, and an own IOS port driver is a VxD project with no use elsewhere. So `usbstor.sys` stays the BOT driver on every target, and on Windows 98 SE mass storage, UAS included, needs NUSB installed; on a stock install it cannot work and the release notes say so |
| Pool and DMA | The HCD allocates pool and its own common buffer. The miniport's "allocate no pool" rule stands for `src/`; for the HCD it is replaced by import-allowlist rows with Windows 98 export evidence, read in task 25.3, and a rule naming the sites that may allocate |
| The Advanced tab | Kept. The HCD answers the `USBUSER` request set `usbui.dll` sends, read per target in task 25.4, so the controller's property page and the root hub's keep appearing. `XHCISNAP` reaches the HCD through the same door |
| USB Audio 2.0 | Not in this repository and not on this roadmap. A class driver of generic design, so that it serves machines on EHCI and vendor stacks as well as this one, built in its own repository; it needs nothing from xHCI, since UAC 2.0 runs at High Speed and binds by class through the standard URB contract |

## The shape of the successor

One bus driver. The controller FDO owns the hardware, enumerates every device
on the bus and creates a PDO per device, or per function of a composite
device, with the id strings each target's class INFs match. Reused unchanged:
the leaf class drivers on each target (`hidusb.sys`, `usbstor.sys`,
`usbaudio.sys`, vendor drivers such as the ASIX adapter's), the helper
exports of `usbd.sys`, and the OS INFs that bind by class id. Replaced:
`usbport.sys`, `usbhub.sys` and `usbhub20.sys`, and the composite-parent role
of `usbhub.sys` on Windows 98 and `usbccgp.sys` elsewhere. Two of the three
replacements are forced rather than chosen: the hub-to-port contract is
private and differs across five generations of `usbhub.sys`, none of which
knows a USB 3 hub descriptor, and the composite parent differs per target too.

What the tree already holds, and what the successor does with it:

| Reuse | Files in `src/` | What changes |
|---|---|---|
| Lifted, the pure core | `xhci_ring.c`, `xhci_ctx.c`, `xhci_caps.c`, `xhci_port.c`, `xhci_mem.c`, `xhci_topo.c`, `xhci_desc.c`, `xhci_log.c` and their headers | Compiled into the second binary from `src/` by path, host suites and all. Port classification gains the USB 3.x port class (Phase 29); the topology graph is fed by the bus's own hub traffic rather than a snoop |
| Adapted | `xhci_init.c`, `xhci_cmd.c`, `xhci_evt.c`, `xhci_pci.c` | The sequence, the command engine and the event drain stay; every `UsbPort*` service call, the registration packet and the usbport-owned resources go |
| Rewritten | `xhci_dispatch.c`, `xhci_rh.c`, `xhci_slot.c`, `xhci_xfer.c`, `xhci_vhub.c`, `xhci_probe.c` | Shaped by the usbport ABI end to end. The slot and endpoint command chains survive as hardware steps; the root-hub callback family, the SET_ADDRESS interception, the virtual hub and the probe have no counterpart, because there is no usbport to lie to or to probe |
| New | the HCD's own directory | FDO and PDO PnP and power, URB dispatch, the enumeration state machine, the hub class, composite splitting, the `USBUSER` and `XHCISNAP` door, the SuperSpeed link state machine, streams, and the UAS class driver |

Every build constraint of `AGENTS.md` binds the new code as it binds the old:
C89, no 64-bit arithmetic, no floating point, no bitfields on hardware
layouts, ASCII and CRLF source, the three flavours, the import gate on every
link, and the amd64 build from WDK 7.1 as a second package. What the HCD
relaxes is exactly the two rows the decisions table names, pool and the
Advanced tab, and nothing else by drift.

## Phase sequence

Phase 25 is the design record and the contract capture, driver-code-free.
Phase 26 is the USB 2.0 bus driver on root ports, at parity with the miniport
on both primary targets. Phase 27 brings external USB 2.0 hubs inside the
bus. Phase 28 takes the other eight targets, the amd64 build and the bench.
Phase 29 is SuperSpeed on root ports, Phase 30 SuperSpeed hubs, Phase 31
streams and UAS, and Phase 32 the `2.0.0.0` cut. The order is deliberate:
parity first, because round 12's device matrix and the acceptance test are a
free oracle for everything USB 2.0; SuperSpeed before hubs, because root-port
storage is what a user plugs in first; UAS last, because it needs streams and
a class driver of its own.

---

## Phase 25 - Design Record 13 and the Contract Capture

Goal: everything the bus driver is written against, read before a line of it
is written: the design of the stack, what each target's class drivers send a
USB device PDO, what Windows 98 exports for a driver that allocates and
enumerates, what `usbui.dll` and `XHCISNAP` need from the door, and what the
gates, the packager and the harness must learn for a second binary.

Status: open, added 2026-10-02. No task has run.

Why a phase: the miniport's Phase 3 spike and every `-0` batch since exist
because a body written against an assumed contract cost a rewrite. The HCD
has three contracts the miniport never had to match - the function-driver
contract above the PDO, the Windows 98 export surface below the FDO, and the
`USBUSER` door - and each differs by target. Reading them is cheap; writing
against a guess is not.

Tasks (plain ids, since every one is confirmed statically):

| Task | Subject |
|---|---|
| 25.1 | design record 13, `design/13-superspeed-hcd.md`, grown from the future-plans page that moved there on 2026-10-02: the stack shape above, the decisions table, the object model (FDO, device PDOs, function PDOs, the hub and port objects inside the bus), the enumeration state machine, the lock model against design record 05, the source layout (the HCD's own directory and `sources` file, the pure core compiled from `src/` by path, which `build.exe` must be shown to accept), the two INFs and the package |
| 25.2 | the function-driver contract, per target, static: for `hidusb.sys`, `usbstor.sys` (NUSB's, Windows 2000 SP4's, XP's, XP x64's, Vista's and 7's in both architectures), `usbaudio.sys` and the ASIX adapter's driver, every `IOCTL_INTERNAL_USB_*` code sent to the PDO, every `IRP_MN_QUERY_INTERFACE` GUID and version (`USB_BUS_INTERFACE_USBDI_V0` to `V3`), every `URB_FUNCTION_*` used, and every `usbd.sys` export imported; recorded as tables with a `legal-provenance.md` section 4 row each |
| 25.3 | the Windows 98 export evidence for the allowlist: pool (`ExAllocatePool`, `ExFreePool`), device objects (`IoCreateDevice`, `IoDeleteDevice`, `IoAttachDeviceToDeviceStack`, `IoDetachDevice`), interrupts and DPCs, the DMA pairing (`HalGetAdapter` with `HalAllocateCommonBuffer` against `IoGetDmaAdapter`, which `win98-wdm.md` leaves unverified), transfer-buffer mapping (`IoAllocateMdl`, `MmGetPhysicalAddress` or the map-register route), registry and the device-interface calls; each a row tagged `static`, read by the import gate's own method; and the rule that replaces "allocate no pool" for the HCD's file set |
| 25.4 | the door: the `USBUSER` request set `usbui.dll` sends on each target (the controller tab and the root hub's Power tab), and `XHCISNAP`'s `USBUSER_PASS_THRU` shape carried over so the tool changes as little as possible; what `IOCTL_USB_GET_NODE_INFORMATION` and its siblings must answer for Device Manager's view by connection |
| 25.5 | the harness and the gates: what `scripts/vm-matrix` reads by offset from the miniport extension and how it reads the HCD's, `gen-offsets.ps1` for a second binary, the import gate's second allowlist, the INF gate on `xhci98h.inf` and `xhci98h-amd64.inf`, `make-package.ps1` and `make-release.ps1` staging a second package beside the frozen first |
| 25.6 | the hub and composite design inside the bus: the hub class request set, the status-change pipe, port reset and enable, TT assignment from design record 02, hub depth, hub removal with the subtree beneath it; the per-function PDO ids including IAD grouping, URB routing from a function PDO to the parent's pipes, and the id strings each target's INFs match (`USB\VID_&PID_&REV_`, `MI_`, the class and `COMPOSITE` compatible ids) |
| 25.7 | the transfer-buffer policy: direct DMA from the URB's MDL against bounce buffers, decided on what 25.3 finds Windows 98 exports and on design record 04's arithmetic for the common buffer the HCD now allocates itself |

Checkpoint: design record 13 complete with the per-target contract tables of
25.2, the allowlist rows of 25.3, the door of 25.4 and the harness plan of
25.5, each fact carrying its provenance row; the source layout of 25.1 shown
to build an empty `xhci98h.sys` through `build-driver.cmd` with the gates
green, which is the one thing here a build settles and a reading cannot. Not
a checkpoint: a contract taken from the DDK headers alone, or a Windows 98
export assumed from Windows 2000's.

Records: `design/13-superspeed-hcd.md`; `legal-provenance.md` section 4;
`usb-xhci-info/win98-wdm.md`; `scripts/import-gate/`.

## Phase 26 - The USB 2.0 Bus Driver on Root Ports

Goal: `xhci98h.sys` as a WDM bus driver on Windows 98 SE and Windows 2000
SP4, driving every USB 2.0 device the miniport drives on a root port - HID,
mass storage, Ethernet and composite audio - through the OS's own class
drivers, with no `usbport.sys`, `usbhub.sys` or `usbhub20.sys` beneath or
above it.

Status: not opened. Waits on Phase 25.

Why a phase: it is the "be usbport" step the architecture record always
named as the large one, and it is where the HCD either matches the miniport
or does not. Hubs are kept out so that every reading here is a root-port
reading with one fewer variable.

Tasks, batched: `-A` host, `-V` VM.

| Task | Subject |
|---|---|
| 26-A.1 | the gates first: the HCD's directory and `sources`, `build-driver.cmd` building a second target in all three flavours, the second import allowlist, the two INFs through the INF gate, the packager staging the second package; an empty driver that installs and shows Code 10 on both primaries |
| 26-A.2 | the FDO: `DriverEntry`, `AddDevice`, start, stop, remove and the Windows 98 out-of-sequence remove that stands in for surprise removal, the power handlers of `win98-wdm.md` with `DO_POWER_PAGABLE` on every object, resources, BAR mapping, the interrupt, the HCD's own common buffer by 25.7, and the controller sequence of `xhci_init.c`, `xhci_cmd.c` and `xhci_evt.c` with the usbport services replaced |
| 26-A.3 | root ports: the port shadow and change path from the interrupt, reset and speed decode, the USB 2.0 port classification unchanged, and no root-hub report to anyone - the bus knows the true speed and tells the truth |
| 26-A.4 | enumeration and device PDOs: Enable Slot, Address Device and the descriptor reads as a state machine of the bus's own, the PDO per device, `IRP_MN_QUERY_ID` strings per target from 25.6, `QUERY_DEVICE_RELATIONS`, `QUERY_CAPABILITIES` and the PDO's PnP and power minimum set |
| 26-A.5 | URB dispatch: `IOCTL_INTERNAL_USB_SUBMIT_URB` and the function set `win98-wdm.md` lists, pipe handles, select configuration and interface through Configure Endpoint, control, bulk, interrupt and isochronous TD construction over the bus's own mapping, abort and reset pipe, cancellation, the interval and cadence taken from `bInterval` directly |
| 26-A.6 | the rest of the PDO contract 25.2 found: `GET_PORT_STATUS`, `RESET_PORT`, `GET_DEVICE_HANDLE`, the `USB_BUS_INTERFACE_USBDI` versions each target's drivers query |
| 26-A.7 | composite splitting: per-function PDOs, IAD grouping, routing, the function ids; the HID-plus-audio units of `test-equipment.md` are the specimens |
| 26-A.8 | the door: `USBUSER` for `usbui.dll` and `XHCISNAP`, the log ring moved in with its sinks and switches, `XHCISNAP` taught the second driver |
| 26-A.9 | host suites for every pure piece: URB parsing, id generation, the enumeration state machine, the composite split, the TD builder over the new mapping |
| 26-V.1 | Windows 98 SE under NUSB 3.3 and under SweetLow's stack: install, start, mouse, storage, composite audio, the ASIX adapter by passthrough, round 12's root-port rows, the Advanced tab, an `XHCISNAP` report |
| 26-V.2 | Windows 2000 SP4: the same rows, Driver Verifier on, the SMP guest of Phase 2d, the disable, enable, remove and rescan sequence |
| 26-V.3 | the stock reading: a fresh Windows 98 SE guest with no USB 2.0 stack installed, the HID mouse and the composite audio device bound, storage reading `NODRIVER` as design record 06 defines it |

Checkpoint: round 12's root-port rows at parity on both primary targets, in
every field the `1.2.0.0` matrix produced, with every device at its true
speed; the disable, enable, remove and rescan sequence on Windows 2000 and
the Windows 98 door sequence; Driver Verifier clean on Windows 2000; the
Advanced tab and the `XHCISNAP` report on both. 26-V.3 is a reading, not a
clause, until the owner says otherwise. Not a checkpoint: a device behind a
hub, or any SuperSpeed port powered.

Records: `design/13-superspeed-hcd.md`; a run sheet `runs/run-26.md`;
`scripts/vm-matrix/README.md`.

## Phase 27 - External USB 2.0 Hubs Inside the Bus

Goal: USB 2.0 hubs driven by the bus itself - High-Speed single-TT and
multi-TT, Full and Low Speed devices behind them, hubs behind hubs to the
depth xHCI allows - with the device matrix at full parity on both primaries.

Status: not opened. Waits on Phase 26.

Why a phase: the hub class is the second half of what `usbhub.sys` did, and
the transaction-translator path is where the miniport's issue 6 and design
record 12 spent two phases. The bus owns the hub now, so the virtual hub, the
High-Speed lie and the TT lookup that bugchecked usbport have no equivalent;
what remains is the hub class itself, and the churn that wedged Windows 98.

| Task | Subject |
|---|---|
| 27-A.1 | the hub class: hub descriptor, port power, the status-change pipe, per-port reset, enable, suspend and resume, port state machines, hub depth, the multi-TT alternate setting |
| 27-A.2 | topology: `xhci_topo.c` fed by the bus's own hub traffic, Route String, parent slot and TT fields per design record 02, Full and Low Speed behind High-Speed hubs, a Full-Speed hub behind a High-Speed one |
| 27-A.3 | removal: a hub pulled with devices beneath it, the subtree's PDOs and slots, a device pulled mid-transfer, the orderly and the surprise paths on each primary |
| 27-A.4 | host suites: the hub state machines and the topology fold against the host vectors design record 12 left |
| 27-V.1 | the full device matrix on both primaries at rig positions H1 to H4 with QEMU's `usb-hub`, 25 plug and unplug cycles per device, and the hub-churn soak that wedged Windows 98 under the miniport, re-measured |

Checkpoint: the full round 12 matrix at parity on both primary targets with
every device at its true speed and its own interval; a Low-Speed mouse at
`bInterval` 10 polled every 8 ms behind a hub and on a root port alike;
Full-Speed audio playing behind a hub and on a root port; the churn soak
either clean or recorded as the open defect it was. Not a checkpoint: a
SuperSpeed hub, or any port the miniport did not manage.

Records: `design/13-superspeed-hcd.md`; `design/02-hub-topology-route-string.md`;
`runs/run-27.md`.

## Phase 28 - The Other Eight Targets, the amd64 Build, and the Bench

Goal: the HCD on Windows ME, XP, XP x64, Vista and 7 in both architectures,
at the standing each tier holds today, and on the two physical Windows 98 SE
machines and the E460's Windows 7.

Status: not opened. Waits on Phase 27.

Why a phase: a target is not a build. The NT 5.x and 6.x class drivers query
interfaces Windows 2000's do not, the amd64 build is a second toolchain, and
the bench is where the miniport found issue 2, the hot-plug wedge no VM ever
showed.

| Task | Subject |
|---|---|
| 28-A.1 | the per-target deltas of 25.2: the `USB_BUS_INTERFACE_USBDI` versions, `GET_DEVICE_HANDLE` and whatever else XP onward sends; Windows ME under SweetLow's stack with its own `usbccgp.sys` left unused |
| 28-A.2 | the amd64 build of `xhci98h.sys`: WDK 7.1 as `WNET`, no `usbport` import library at all, the `_WIN64` halves of the bus's own structures, the amd64 allowlist, the second INF |
| 28-V.1 | the eight guests: ME, XP SP3, XP x64, Vista x86 and x64, 7 x86 and x64, each the same clauses as 26-V and 27-V, Vista and 7 at four virtual processors; the x64 guests on an F8 boot, as before |
| 28-E.1 | the bench: the E460 on Windows 98 SE and on 32-bit Windows 7, and the second Windows 98 SE machine, through `run-13e.md`'s stage list - install, HID, storage, Ethernet, audio, the hub rig, five hot-plugs, the controller disable that hung Windows 7 under the miniport |
| 28.3 | every known limitation of `1.2.0.0` re-measured under the HCD and written down as gone, carried or new: the NUSB stop crash (usbport's, so expected gone), the idle that never sleeps (now the bus's own power policy, so a decision), the Windows 98 churn wedge, the Windows 7 disable hang |

Checkpoint: the ten install legs read on the `qemu` build and then the
`release` flavour; the bench run on both Windows 98 SE machines and the E460's
Windows 7; the limitation list of 28.3 complete. Not a checkpoint: a
SuperSpeed device anywhere.

Records: `runs/run-28.md`; `build-and-test.md`'s target sections;
`design/11-x64-targets.md` for what the amd64 build keeps from the miniport's.

## Phase 29 - SuperSpeed on Root Ports

Goal: a USB 3.x device on a root port enumerated and driven at SuperSpeed,
with the bulk, interrupt and isochronous paths at that speed, and a USB 2.0
device on the same connector unchanged.

Status: not opened. Waits on Phase 28.

Why a phase: this is the reason the successor exists. The port class the
miniport left unpowered is powered for the first time, the link has a state
machine of its own, and enumeration changes shape at the first packet.

| Task | Subject |
|---|---|
| 29-0 | the specification transcription into `xhci-data-structures.md`: the SuperSpeed PSIV default and Slot Context speed, `PORTSC` `WPR`, `WRC` and `CEC`, the `PLS` encodings, the USB 3 port state machine of Figure 4-27 with the Disabled state's exit and the hot-to-warm conversion, `CCS` in Table 5-27, Endpoint Context Max Burst, Mult and Max ESIT Payload from the companion descriptor, TD Size against burst, Table 6-12's SuperSpeed row; and the USB 3.2 specification's BOS, SuperSpeed Device Capability and Endpoint Companion descriptors, its link states and its SS.Disabled rule for an upstream port that has fallen back |
| 29-A.1 | port classification: USB 3.x protocol ports managed and powered, the companion-pairing convention carried into the bus's port objects, the all-SuperSpeed controller accepted |
| 29-A.2 | the link state machine: Polling to U0, warm reset, U3 and resume, SS.Inactive and Compliance recovery, the automatic hot-or-warm reset policy and its reading |
| 29-A.3 | SuperSpeed enumeration: EP0 at 512 bytes, BOS and the companion descriptors, Slot and Endpoint Contexts with burst and ESIT, bandwidth as the xHC's own Configure Endpoint verdict surfaced as a URB status rather than modelled |
| 29-A.4 | transfers at SuperSpeed: bulk, interrupt and isochronous TDs with burst, 1024-byte endpoints, the TD Size rule |
| 29-A.5 | fallback: a SuperSpeed link that fails to train leaves the device to appear on its USB 2.0 companion port, as it does today, with a counter saying so |
| 29-V.1 | QEMU's `qemu-xhci` with `usb-storage` at SuperSpeed on both primaries: enumerated, bound to each target's `usbstor.sys`, a verified file round trip; the USB 2.0 rows unchanged |
| 29-E.1 | the bench: the two SuperSpeed drives of `test-equipment.md` at rig position D on Windows 98 SE and 32-bit Windows 7, round trips, throughput against the same unit behind a USB 2.0 hub, warm reset and SS.Inactive recorded if they occur |

Checkpoint: both SuperSpeed drives round-tripping at SuperSpeed on both
primary targets in QEMU and on the bench, with `usbstor.sys` unchanged and
Device Manager reporting SuperSpeed; a USB 2.0 device on the same connector
reading as it did in Phase 27. Not a checkpoint: a hub on a SuperSpeed port,
or UAS.

Records: `design/13-superspeed-hcd.md`; `usb-xhci-info/xhci-data-structures.md`;
`runs/run-29.md`; `test-equipment.md`, "What the SuperSpeed halves are for".

## Phase 30 - SuperSpeed Hubs

Goal: a USB 3.x hub's SuperSpeed half driven as a hub of the bus, with
SuperSpeed devices behind it, while its High-Speed half carries USB 2.0
devices as a Phase 27 hub.

Status: not opened. Waits on Phase 29.

Why a phase: a USB 3 hub is two hubs on two ports with their own descriptor,
their own port status format and their own depth and link-state requests, and
no QEMU device models one, so the readings are bench readings.

| Task | Subject |
|---|---|
| 30-0 | the USB 3 hub class: the hub descriptor, `SET_HUB_DEPTH`, the port status and change bits at SuperSpeed, the link-state and warm-reset port features, remote-wake masks; transcribed from the USB 3.2 specification |
| 30-A.1 | the SuperSpeed hub inside the bus: enumeration at depth with the Route String of design record 02, port reset through a hub, the two halves of one unit as two independent hubs with no pairing beyond a counter |
| 30-A.2 | host suites: the hub state machines at SuperSpeed over the vectors of 27-A.4 |
| 30-E.1 | the bench unit (`05E3:0610` and `05E3:0612`): a SuperSpeed drive behind the SuperSpeed half and a High-Speed device behind the other, on Windows 98 SE and 32-bit Windows 7, plugged, unplugged and re-plugged |

Checkpoint: the bench reading of 30-E.1 on Windows 98 SE. Windows 2000's half
of "observed on both" cannot be taken: it is a virtual-machine target and no
QEMU device models a SuperSpeed hub, so this phase ships that half as
untested ground stated in the release notes, the way the Full-Speed hub
clause of Phase 13 did. Not a checkpoint: UAS.

Records: `design/13-superspeed-hcd.md`; `runs/run-30.md`.

## Phase 31 - Streams and UAS

Goal: bulk streams in the bus and a UAS class driver of this project's own,
so that a UAS-capable device runs UAS at SuperSpeed and at High Speed, with
Bulk-Only still selectable.

Status: not opened. Waits on Phase 30.

Why a phase: streams are a transfer model the bus has never had, UAS is a
class driver with a storage-stack contract of its own on each target, and
which driver a UAS-capable device gets is a bus-side id decision; the three
are one slice.

| Task | Subject |
|---|---|
| 31-0 | static: Stream Context Arrays and stream ids in TRBs and events (xHCI section 4.12); the UAS class specification and T10's UAS; how `usbstor.sys` presents SCSI PDOs to `disk.sys` and `classpnp` on each target and, on Windows 98 SE, how NUSB's NTMAP port driver gives them drive letters (`usbntmap.inf` binds the objects to `*IOS` with `NTMAPHLP.PDR`), since the UAS driver's objects must take the same route; whether the 98 SE CD's `layout.inf` carries the NTMAP files, read from the disc, which decides whether the INF can have the OS supply them |
| 31-A.1 | primary streams in the bus: a private open-streams request on a pipe handle, stream rings, per-stream enqueue and event matching, abort of a stream, stream close |
| 31-A.2 | the UAS class driver, its own binary and INF in the package: SCSI PDOs, the command, status, data-in and data-out pipes, tags and queue depth, sense, task management and reset recovery, the SRB set `classpnp` sends |
| 31-A.3 | the id policy: the bus lists the `Prot_62` compatible id first when the device offers a UAS alternate setting and the controller streams, and a registry value forces Bulk-Only; the device matrix taught `UAS` as a verdict |
| 31-V.1 | QEMU's `usb-uas` model on both primaries: enumerated, bound to the UAS driver, a verified round trip; the same guest with the value set, Bulk-Only selected |
| 31-E.1 | the bench: the ASMedia bridge and the MSSU10 drive under UAS at SuperSpeed and behind a USB 2.0 hub at High Speed, on Windows 98 SE and 32-bit Windows 7, round trips and throughput against Bulk-Only |

Checkpoint: `usb-uas` round-tripping under the UAS driver on both primary
targets, the bench units at SuperSpeed on Windows 98 SE, and Bulk-Only
selected with the value set, read on both. On Windows 98 SE every clause is
taken with NUSB installed, because the UAS driver's disk objects reach a
drive letter only through NUSB's NTMAP port driver (the decisions table); on
a stock install UAS cannot work, and the release notes say so. Not a
checkpoint: isochronous or interrupt streams, which USB defines nowhere, or
UAS on a stock Windows 98 SE.

Records: `design/13-superspeed-hcd.md`; `runs/run-31.md`.

## Phase 32 - Release `2.0.0.0`

Goal: the first release of the successor, both architectures, with the
release notes, the acceptance test and the post-release run rewritten for a
driver that is a bus rather than a miniport.

Status: not opened. Waits on Phase 31.

Why a phase: a cut changes every user-facing statement, and this one changes
what the download is.

| Task | Subject |
|---|---|
| 32.1 | the record: `release-notes.md` rewritten - what the HCD is and is not, every target's standing, the F8 requirement on the x64 half, the stock Windows 98 SE statement, the limitations of 28.3 and 30's untested ground; the readme template; `releases/history.md`; `README.md` and `AGENTS.md` describing two drivers, one frozen |
| 32.2 | the acceptance test and the post-release run of design record 09 adapted to the HCD, including a UAS row and a SuperSpeed row |
| 32.3 | the cut: `xhci_version.h`'s successor, both INFs' `DriverVer`, `make-release.ps1` publishing `xhci98h` in four directories beside the frozen `xhci98` tree, the ten install legs read from the asset |

Checkpoint: the package `make-package.ps1` assembles holds the HCD, the UAS
driver, their INFs, the tools and the readme and nothing of Microsoft's; the
ten install legs read from the asset; the post-release run on both primaries
accepted by the owner. The upload and the hand-run acceptance on a fresh VM
and a physical machine are the owner's, as `roadmap.md` ends by saying.

Records: `releases/history.md`; `docs/using/release-notes.md`;
`docs/using/release-acceptance-test.md`.

---

## What is not on this roadmap

- **USB Audio 2.0.** A separate repository, by the owner's decision of
  2026-10-02, because it needs nothing from this stack and should serve
  machines on EHCI and vendor stacks too. The Sound Blaster X4 in
  `test-equipment.md` is its specimen, and it can be built against the frozen
  `1.2.0.0` miniport today.
- **Power management beyond what the targets force.** Selective suspend,
  U1 and U2, S3 resume and remote wake are the bus's own once it owns the
  power IRPs. Task 28.3 records the decision for `2.0.0.0`; a phase after it
  may take the rest.
- **USB 3.1 Gen 2 and above.** The same code with a different speed id; a
  reading, not a phase, once a controller and a device that negotiate it are
  held.
- **The SuperSpeed storage proposal behind a switch**
  (`future-plans/superspeed-storage-behind-a-switch.md`). It was the way to
  SuperSpeed storage without leaving the miniport; with the miniport frozen it
  stays a record of what that would have taken.
