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
| Mass storage on Windows 98 SE | NUSB's, kept (owner, 2026-10-02, choosing this over an own storage driver). A drive letter on 9x comes from the IOS layer, and NUSB's `usbntmap.inf` binds `usbstor.sys`'s disk objects (`USBSTOR\GenDisk` and its siblings) to `DevLoader=*IOS` with `PortDriver=USBMPHLP.PDR`, the USB mapping port driver, with `NTMAP.SYS` and `USBNTMAP.SYS` beside it; NUSB's own install INF stamps `NTMAP.SYS` as `4.10.0.2227` and annotates it with hotfixes 242975 and 267304, a Windows 98 SE line, and annotates `USBMPHLP.PDR` and `USBNTMAP.SYS` as `WinMe` in its file list - the package author's notes, not a reading of the files, which were not examined (`legal-provenance.md` section 4, the NUSB 3.6 INF row; method static, text read, nothing executed). They are Microsoft's and this project does not redistribute them. An own BOT driver would still sit under that mapping, and an own IOS port driver is a VxD project with no use elsewhere. So `usbstor.sys` stays the BOT driver on every target, and on Windows 98 SE mass storage, UAS included, needs NUSB installed; on a stock install it cannot work and the release notes say so. Whether the 98 SE CD carries any of the mapping files is task 31-0's to read from the disc |
| Pool and DMA | The HCD allocates pool and its own common buffer. The miniport's "allocate no pool" rule stands for `src/`; for the HCD it is replaced by import-allowlist rows with Windows 98 export evidence, read in task 25.3, and a rule naming the sites that may allocate |
| The Advanced tab | The controller's, kept; the root hub's, gone (owner, 2026-10-02, on the Codex review of that day). The HCD answers the `USBUSER` request set `usbui.dll` sends to the controller devnode, read per target in task 25.4, and its INFs carry the two registrations the miniport's do - `EnumPropPages` to `sysclass.dll` on Windows 98, `EnumPropPages32` to `usbui.dll` on the NT paths - so the controller's Advanced tab keeps appearing, and `XHCISNAP` reaches the HCD through the same door. There is no USB Root Hub devnode under the HCD - a PDO with id `USB\ROOT_HUB` would be claimed by the OS's own `usbhub.sys` on every NT target - so the root hub's Power tab, which `usbui.dll` draws on that devnode's `EnumPropPages32` registration, is not retained, and devices appear directly under the controller |
| The miniport's virtual-hub values | Not supported (owner, 2026-10-02). `XhciVirtualHSHub`, `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid` exist to make usbport tell the truth about a root-port device's speed; the HCD has no usbport to lie to and reports every device at its true speed with no hub in the way, so there is nothing for them to switch. The HCD reads none of the three, its INFs write none, a value left behind by a miniport install has no effect, and the release notes say so (32.1). Which of the miniport's other values carry over - the log switches, the interrupt moderation value - is 25.1's list |
| SuperSpeed hubs on Windows 2000 | A vehicle is required, for now (owner, 2026-10-02, on the same review). Windows 2000 is a VM-only target and no QEMU device models a SuperSpeed hub, so Phase 30's "observed on both" has no vehicle today; the owner did not record an exception and revisits the question when Phase 30 opens. Until then Phase 30 cannot close, and Phases 31 and 32 wait on it |
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
bus. Phase 28 takes the seven other guests, the amd64 build and the bench.
Phase 29 is SuperSpeed on root ports, Phase 30 SuperSpeed hubs, Phase 31
streams and UAS, and Phase 32 the `2.0.0.0` cut. The order is deliberate:
parity first, because round 12's device matrix and the acceptance test are a
free oracle for everything USB 2.0; SuperSpeed before hubs, because root-port
storage is what a user plugs in first; UAS last, because it needs streams and
a class driver of its own. One phase has no observation vehicle on Windows
2000 today: Phase 30, the SuperSpeed hubs, and the decisions table says what
the owner decided about that and when it is revisited.

---

## Phase 25 - Design Record 13 and the Contract Capture

Goal: everything the bus driver is written against, read before a line of it
is written: the design of the stack, what each target's class drivers send a
USB device PDO, what Windows 98 exports for a driver that allocates and
enumerates, what `usbui.dll` and `XHCISNAP` need from the door, and what the
gates, the packager and the harness must learn for a second binary. The one
piece of code it builds is the empty scaffold of 25.8, so that the build
path is proven before Phase 26 fills it.

Status: open, added 2026-10-02. No task has run.

Why a phase: the miniport's Phase 3 spike and every `-0` batch since exist
because a body written against an assumed contract cost a rewrite. The HCD
has three contracts the miniport never had to match - the function-driver
contract above the PDO, the Windows 98 export surface below the FDO, and the
`USBUSER` door - and each differs by target. Reading them is cheap; writing
against a guess is not.

Tasks (plain ids: every one but 25.8 is confirmed statically, and 25.8 by
the build wrapper alone):

- [ ] 25.1 design record 13, `design/13-superspeed-hcd.md`, grown from the future-plans page that moved there on 2026-10-02: the stack shape above, the decisions table, the object model (FDO, device PDOs, function PDOs, the hub and port objects inside the bus), the enumeration state machine, the lock model against design record 05, the source layout (the HCD's own directory and `sources` file, the pure core compiled from `src/` by path, which `build.exe` must be shown to accept), the two INFs and the package
- [ ] 25.2 the function-driver contract, per target, static: for `hidusb.sys`, `usbstor.sys` (NUSB's, Windows 2000 SP4's, XP's, XP x64's, Vista's and 7's in both architectures), `usbaudio.sys` and the ASIX adapter's driver, every `IOCTL_INTERNAL_USB_*` code sent to the PDO, every `IRP_MN_QUERY_INTERFACE` GUID and version (`USB_BUS_INTERFACE_USBDI_V0` to `V3`), every `URB_FUNCTION_*` used, and every `usbd.sys` export imported; recorded as tables with a `legal-provenance.md` section 4 row each
- [ ] 25.3 the Windows 98 export evidence for the allowlist: pool (`ExAllocatePool`, `ExFreePool`), device objects (`IoCreateDevice`, `IoDeleteDevice`, `IoAttachDeviceToDeviceStack`, `IoDetachDevice`), interrupts and DPCs, the DMA pairing (`HalGetAdapter` with `HalAllocateCommonBuffer` against `IoGetDmaAdapter`, which `win98-wdm.md` leaves unverified), transfer-buffer mapping (`IoAllocateMdl`, `MmGetPhysicalAddress` or the map-register route), registry and the device-interface calls; each a row tagged `static`, read by the import gate's own method; and the rule that replaces "allocate no pool" for the HCD's file set
- [ ] 25.4 the door: the `USBUSER` request set `usbui.dll` sends to the controller devnode on each target for the controller's Advanced tab (the root hub's Power tab is not retained, decisions table), the registrations the HCD's INFs must write for it - on Windows 98 `EnumPropPages` naming `sysclass.dll`'s controller page, with `usbui.dll` drawing its dialogs, and on the NT paths `EnumPropPages32` naming `usbui.dll`'s provider, each as the miniport's INFs write them today (`build-and-test.md`, the INF table and its `EnumPropPages` bullet) - and `XHCISNAP`'s `USBUSER_PASS_THRU` shape carried over so the tool changes as little as possible; what `IOCTL_USB_GET_NODE_INFORMATION` and its siblings must answer for Device Manager's view by connection, and the fact that the node-connection interface those systems' UI uses predates SuperSpeed and reports High Speed at most, which is why no checkpoint reads a speed from Device Manager
- [ ] 25.5 the harness and the gates: what `scripts/vm-matrix` reads by offset from the miniport extension and how it reads the HCD's, `gen-offsets.ps1` for a second binary, the import gate's second allowlist, the INF gate on `xhci98h.inf` and `xhci98h-amd64.inf`, `make-package.ps1` and `make-release.ps1` staging a second package beside the frozen first
- [ ] 25.6 the hub and composite design inside the bus: the hub class request set, the status-change pipe, port reset and enable, TT assignment from design record 02, hub depth, hub removal with the subtree beneath it; the per-function PDO ids, the grouping rules - an IAD where the device has one, and for devices without one the legacy interface-collection rules Microsoft's composite parent applies, chiefly the audio rule that groups an AudioControl interface with the AudioStreaming and MIDIStreaming interfaces after it, which every UAC 1.0 unit in `test-equipment.md` needs - URB routing from a function PDO to the parent's pipes, and the id strings each target's INFs match (`USB\VID_&PID_&REV_`, `MI_`, the class and `COMPOSITE` compatible ids)
- [ ] 25.7 the transfer-buffer policy: direct DMA from the URB's MDL against bounce buffers, decided on what 25.3 finds Windows 98 exports and on design record 04's arithmetic for the common buffer the HCD now allocates itself
- [ ] 25.8 the build scaffold, the one code task of this phase: the HCD's directory and `sources` file with the pure core compiled from `src/` by path, a `DriverEntry` that registers nothing and returns, `build-driver.cmd` building the second target in all three flavours and both architectures, the second import allowlist with only the rows that empty driver needs, the two INFs through the INF gate, and the packager refusing to stage the scaffold; nothing here installs on a guest, which is 26-A.1's

Checkpoint: design record 13 complete with the per-target contract tables of
25.2, the allowlist rows of 25.3, the door of 25.4 and the harness plan of
25.5, each fact carrying its provenance row; 25.8's empty `xhci98h.sys`
built through `build-driver.cmd` with the gates green, which is the one
thing here a build settles and a reading cannot. Not a checkpoint: a contract
taken from the DDK headers alone, a Windows 98 export assumed from Windows
2000's, or the scaffold on a guest.

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

- [ ] 26-A.1 the scaffold of 25.8 made stageable: the packager staging the second package, and the allowlist grown to what the FDO of 26-A.2 imports; the guest half is 26-V.0
- [ ] 26-A.2 the FDO: `DriverEntry`, `AddDevice`, start, stop, remove and the Windows 98 out-of-sequence remove that stands in for surprise removal, the power handlers of `win98-wdm.md` with `DO_POWER_PAGABLE` on every object, resources, BAR mapping, the interrupt, the HCD's own common buffer by 25.7, and the controller sequence of `xhci_init.c`, `xhci_cmd.c` and `xhci_evt.c` with the usbport services replaced
- [ ] 26-A.3 root ports: the port shadow and change path from the interrupt, reset and speed decode, the USB 2.0 port classification unchanged, and no root-hub report to anyone - the bus knows the true speed and tells the truth
- [ ] 26-A.4 enumeration and device PDOs: Enable Slot, Address Device and the descriptor reads as a state machine of the bus's own, the PDO per device, `IRP_MN_QUERY_ID` strings per target from 25.6, `QUERY_DEVICE_RELATIONS`, `QUERY_CAPABILITIES` and the PDO's PnP and power minimum set
- [ ] 26-A.5 URB dispatch: `IOCTL_INTERNAL_USB_SUBMIT_URB` and the function set `win98-wdm.md` lists, pipe handles, select configuration and interface through Configure Endpoint, control, bulk, interrupt and isochronous TD construction over the bus's own mapping, abort and reset pipe, cancellation, the interval and cadence taken from `bInterval` directly
- [ ] 26-A.6 the rest of the PDO contract 25.2 found: `GET_PORT_STATUS`, `RESET_PORT`, `GET_DEVICE_HANDLE`, the `USB_BUS_INTERFACE_USBDI` versions each target's drivers query
- [ ] 26-A.7 composite splitting: per-function PDOs, IAD grouping, and the grouping rules for devices that carry no IAD - chiefly audio, where an AudioControl interface and the AudioStreaming and MIDIStreaming interfaces that follow it are one function and a HID interface beside them is its own, the rule every UAC 1.0 unit in `test-equipment.md` needs since none of the five has an IAD - plus routing and the function ids; those units and the IAD-grouped X4 are the specimens
- [ ] 26-A.8 the door: `USBUSER` for `usbui.dll` and `XHCISNAP`, the log ring moved in with its sinks and switches, `XHCISNAP` taught the second driver; and one sink the miniport could not have: a PASSIVE-level flusher that emits the ring through `DbgPrint` continuously, because the HCD owns a PASSIVE context - its own system thread or work item - where the miniport on Windows 98 never reached PASSIVE between `StartController` and shutdown. The ban stands on what it always stood on: per-line `DbgPrint` from DPC or ISR context under DebugView bugchecks Windows 98 on real hardware on three device classes (design record 08), and owning the driver object removes the door problem, not that one; the flusher records from any IRQL and emits from PASSIVE only, behind the same verbosity switch
- [ ] 26-A.9 host suites for every pure piece: URB parsing, id generation, the enumeration state machine, the composite split with and without an IAD, the TD builder over the new mapping
- [ ] 26-A.10 the matrix's expectations for the HCD, before any 26-V run: `scripts/vm-matrix/matrix.psd1` encodes the miniport's behaviour, not a device's - the `advance endpoint speed mismatches` row asserts the High-Speed lie on a root port with the switch off, the switch-aware rows assert the virtual hub's extra tier and usbport's naming of it, and the counters are read at the miniport extension's offsets - so a literal field-for-field comparison would fail a correct HCD. 25.5's plan becomes a second expectation set: the usbport mismatch counter has no HCD counterpart and is marked `inert` rather than asserted zero, since design record 06 section 3 separates a counter nothing can reach from a meaningful zero; in its place each device row carries an expected speed and the harness asserts the HCD's decoded port speed and the speed it programmed into the Slot Context against it, so a wrong speed report is a `FAIL` and not a silence; the virtual-hub tier rows go; every other usbport-only counter is dropped or mapped to the HCD's; the offsets are regenerated for the HCD's extension; and the verdict rules of design record 06 are kept as they are
- [ ] 26-V.0 the scaffold on both primaries: `xhci98h.inf`'s undecorated path on Windows 98 SE and its `.NTx86` path on Windows 2000, the empty driver installing with no prompt and showing Code 10; the amd64 INF's first install is 28-V.1's
- [ ] 26-V.1 Windows 98 SE under NUSB 3.3 and under SweetLow's stack: install, start, mouse, storage, composite audio, the ASIX adapter by passthrough, round 12's root-port rows, the Advanced tab, an `XHCISNAP` report
- [ ] 26-V.2 Windows 2000 SP4: the same rows, Driver Verifier on, the SMP guest of Phase 2d, the disable, enable, remove and rescan sequence
- [ ] 26-V.3 the stock reading: a fresh Windows 98 SE guest with no USB 2.0 stack installed, the HID mouse and the composite audio device bound, storage reading `NODRIVER` as design record 06 defines it

Checkpoint: round 12's root-port rows at parity on both primary targets,
where parity means the same device behaviour under 26-A.10's expectations -
each device enumerated, bound and exercised as round 12 saw it, at its true
speed, with design record 06's verdicts - and not the miniport's field
values, which encode usbport's lie and the virtual hub; the disable, enable,
remove and rescan sequence on Windows 2000 and
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

- [ ] 27-A.1 the hub class: hub descriptor, port power, the status-change pipe, per-port reset, enable, suspend and resume, port state machines, hub depth, the multi-TT alternate setting
- [ ] 27-A.2 topology: `xhci_topo.c` fed by the bus's own hub traffic, Route String, parent slot and TT fields per design record 02, Full and Low Speed behind High-Speed hubs, a Full-Speed hub behind a High-Speed one
- [ ] 27-A.3 removal: a hub pulled with devices beneath it, the subtree's PDOs and slots, a device pulled mid-transfer, the orderly and the surprise paths on each primary
- [ ] 27-A.4 host suites: the hub state machines and the topology fold against the host vectors design record 12 left
- [ ] 27-V.1 the device matrix on both primaries behind QEMU's `usb-hub`, which is a Full-Speed hub and the only hub QEMU models: a Full-Speed hub on a root port, Low and Full Speed devices behind it, hubs behind hubs to the depth limit - recounted for the HCD, since the virtual hub's extra tier is gone and the matrix's five-tier chain is five deep in Windows' view again, so the tier-5 device the miniport's expectations assert as never addressed is now addressed and expected to bind - 25 plug and unplug cycles per device, and the hub-churn soak that wedged Windows 98 under the miniport, re-measured. No High-Speed hub, transaction translator or Full-Speed-hub-behind-High-Speed-hub clause can be taken in QEMU; those are 28-E.1's bench clauses, and on Windows 2000 they rest on 27-A.4's host vectors, the standing design record 02 gave the miniport's TT path

Checkpoint: the round 12 matrix at parity on both primary targets for every
row QEMU can present, with every device at its true speed and its own
interval; a Low-Speed mouse at `bInterval` 10 polled every 8 ms behind the
Full-Speed hub and on a root port alike; Full-Speed audio playing behind
that hub and on a root port; the churn soak either clean or recorded as the
open defect it was; the TT and High-Speed hub paths green on 27-A.4's host
vectors. The High-Speed hub clauses - single-TT and multi-TT hubs, Full and
Low Speed devices behind them, a Full-Speed hub behind a High-Speed one -
close in Phase 28 on the bench and nowhere else, and Windows 2000's coverage
of them is the host vectors alone. Not a checkpoint: a SuperSpeed hub, or any
port the miniport did not manage.

Records: `design/13-superspeed-hcd.md`; `design/02-hub-topology-route-string.md`;
`runs/run-27.md`.

## Phase 28 - The Seven Other Guests, the amd64 Build, and the Bench

Goal: the HCD on Windows ME, XP, XP x64, Vista and 7 in both architectures,
at the standing each tier holds today, and on the two physical Windows 98 SE
machines and the E460's Windows 7.

Status: not opened. Waits on Phase 27.

Why a phase: a target is not a build. The NT 5.x and 6.x class drivers query
interfaces Windows 2000's do not, the amd64 build is a second toolchain, and
the bench is where the miniport found issue 2, the hot-plug wedge no VM ever
showed.

- [ ] 28-A.1 the per-target deltas of 25.2: the `USB_BUS_INTERFACE_USBDI` versions, `GET_DEVICE_HANDLE` and whatever else XP onward sends; Windows ME under SweetLow's stack with its own `usbccgp.sys` left unused
- [ ] 28-A.2 the amd64 build of `xhci98h.sys`: WDK 7.1 as `WNET`, no `usbport` import library at all, the `_WIN64` halves of the bus's own structures, the amd64 allowlist, the second INF
- [ ] 28-V.1 the seven other guests: ME, XP SP3, XP x64, Vista x86 and x64, 7 x86 and x64, each the same clauses as 26-V and 27-V, Vista and 7 at four virtual processors; Vista x64 and Windows 7 x64 on an F8 boot, as before, and XP x64 with no such need. With the three Windows 98 SE and Windows 2000 legs of Phases 26 and 27 these are the ten install legs every cut reads
- [ ] 28-E.1 the bench: the E460 on Windows 98 SE and on 32-bit Windows 7, and the second Windows 98 SE machine, through `run-13e.md`'s stage list - install, HID, storage, Ethernet, audio, five hot-plugs, the controller disable that hung Windows 7 under the miniport - and Phase 27's High-Speed hub clauses, which only the bench can take: the hub rig at positions H1 to H4 with the single-TT and multi-TT units of `test-equipment.md`, Low and Full Speed devices behind each, the Full-Speed hub clause if a specimen is held by then and otherwise recorded as untested ground as Phase 13 recorded it
- [ ] 28.3 every known limitation of `1.2.0.0` re-measured under the HCD and written down as gone, carried or new: the NUSB stop crash (usbport's, so expected gone), the idle that never sleeps (now the bus's own power policy, so a decision), the Windows 98 churn wedge, the Windows 7 disable hang

Checkpoint: the ten install legs read on the `qemu` build and then the
`release` flavour; the bench run on both Windows 98 SE machines and the E460's
Windows 7, Phase 27's High-Speed hub clauses included; the limitation list of
28.3 complete. Not a checkpoint: a SuperSpeed device anywhere.

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

- [ ] 29-0 the specification transcription into `xhci-data-structures.md`: the SuperSpeed PSIV default and Slot Context speed, `PORTSC` `WPR`, `WRC` and `CEC`, the `PLS` encodings, the USB 3 port state machine of Figure 4-27 with the Disabled state's exit and the hot-to-warm conversion, `CCS` in Table 5-27, Endpoint Context Max Burst, Mult and Max ESIT Payload from the companion descriptor, TD Size against burst, Table 6-12's SuperSpeed row; and the USB 3.2 specification's BOS, SuperSpeed Device Capability and Endpoint Companion descriptors, its link states and its SS.Disabled rule for an upstream port that has fallen back
- [ ] 29-A.1 port classification: USB 3.x protocol ports managed and powered, the companion-pairing convention carried into the bus's port objects, the all-SuperSpeed controller accepted; and the rate kept distinct from the speed class, since `xhci_caps.c` today folds every protocol speed id at or above 5 Gbit/s into one SuperSpeed class - a link that trains above 5 Gbit/s (SuperSpeedPlus, "What is not on this roadmap") is counted and sent back to USB 2.0 by 29-A.5's mechanism rather than enumerated, with host vectors for both the Gen 1 and the refused case, until the later review takes it up
- [ ] 29-A.2 the link state machine: Polling to U0, warm reset, U3 and resume, SS.Inactive and Compliance recovery, the automatic hot-or-warm reset policy and its reading
- [ ] 29-A.3 SuperSpeed enumeration: EP0 at 512 bytes, BOS and the companion descriptors, Slot and Endpoint Contexts with burst and ESIT, bandwidth as the xHC's own Configure Endpoint verdict surfaced as a URB status rather than modelled
- [ ] 29-A.4 transfers at SuperSpeed: bulk, interrupt and isochronous TDs with burst, 1024-byte endpoints, the TD Size rule. Bulk is the path this phase observes. SuperSpeed interrupt is first observed in Phase 30, on the SuperSpeed hub's status-change pipe. SuperSpeed isochronous has no vehicle: QEMU models none and no SuperSpeed isochronous device is held, so it is built from the specification against host vectors, carried as untested ground in the release notes until a specimen is held, and is not a clause of any checkpoint
- [ ] 29-A.5 fallback, in both directions: a SuperSpeed link that fails to train leaves the device to appear on its USB 2.0 companion port, as it does today, with a counter saying so; and the active case, sending a device whose link has trained back to USB 2.0 - needed by 29-A.1's SuperSpeedPlus refusal - which means writing PED on the trained SuperSpeed port so its link goes to SS.Disabled and its terminations are withdrawn, tearing down the slot, and holding the port disabled for as long as the device may be on the USB 2.0 side, since every bus reset the companion port takes during enumeration and error recovery makes the device look for the SuperSpeed terminations again and a restored port would ping-pong it between the two buses. The two release rules are the ones `future-plans/superspeed-storage-behind-a-switch.md` section 6.1 derived, carried over unchanged: a companion-paired port's hold is released on the companion port's disconnect only if that companion reported a connect after the hold began **and the device that enumerated there is the held device** - the same vendor id, product id and serial string as the descriptor read on the SuperSpeed port before the hold, or vendor and product id alone for a device with no serial, counted apart as the weaker match - because a connect alone proves nothing: the pairing is a convention, and an unrelated device plugged into a mis-paired companion would otherwise release the wrong hold and restart the ping-pong (this tightens the proposal's rule, which took the connect alone as evidence; the proposal's section 6.1 now carries a note saying so). A companion connect of some other device leaves the hold in place and is counted. The release re-arms the SuperSpeed port with a `PORTSC` write of `PLS` = RxDetect with `LWS`, the Disabled state's exit to Disconnected, never a warm reset (which does not act on a Disabled port) and never a power cycle; an orphan port, or a paired port whose companion never connected, holds until the controller's next start, because a Disabled port has withdrawn the very terminations it would detect a disconnect with and a timed re-arm cannot tell a device that left from one still on its way. Whether the Disabled state raises `CSC` on a physical disconnect is a 29-0 transcription from Figure 4-27, and the orphan rule is written on the pessimistic reading. Counters for the paired, unpaired and orphan holds, and host vectors for each case including a companion disconnect with no prior connect, and an unrelated device's connect and disconnect on a mis-paired companion, each of which must release nothing
- [ ] 29-V.1 QEMU's `qemu-xhci` with `usb-storage` at SuperSpeed on both primaries: enumerated, bound to each target's `usbstor.sys`, a verified file round trip; the USB 2.0 rows unchanged
- [ ] 29-E.1 the bench: the two SuperSpeed drives of `test-equipment.md` at rig position D on Windows 98 SE and 32-bit Windows 7, round trips, throughput against the same unit behind a USB 2.0 hub, warm reset and SS.Inactive recorded if they occur

Checkpoint, in two halves because Windows 2000 has no bench: QEMU's
`usb-storage` at SuperSpeed round-tripping on both primary guests with each
target's `usbstor.sys` unchanged (29-V.1); and both physical SuperSpeed
drives round-tripping at SuperSpeed on the bench on Windows 98 SE and 32-bit
Windows 7 (29-E.1). The witness that a transfer ran at SuperSpeed is
`XHCISNAP`'s decoded port speed and the slot's speed field, plus the
throughput against the same unit behind a USB 2.0 hub; Device Manager's
speed text is not a clause, since the node-connection interface those
systems' UI uses reports High Speed at most (task 25.4). A USB 2.0 device on
the same connector reads as it did in Phase 27. Not a checkpoint: a hub on a
SuperSpeed port, UAS, or a SuperSpeed interrupt or isochronous transfer
(29-A.4).

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

- [ ] 30-0 the USB 3 hub class: the hub descriptor, `SET_HUB_DEPTH`, the port status and change bits at SuperSpeed, the link-state and warm-reset port features, remote-wake masks; transcribed from the USB 3.2 specification
- [ ] 30-A.1 the SuperSpeed hub inside the bus: enumeration at depth with the Route String of design record 02, port reset through a hub, the two halves of one unit as two independent hubs with no pairing beyond a counter
- [ ] 30-A.2 host suites: the hub state machines at SuperSpeed over the vectors of 27-A.4
- [ ] 30-E.1 the bench unit (`05E3:0610` and `05E3:0612`): a SuperSpeed drive behind the SuperSpeed half and a High-Speed device behind the other, on Windows 98 SE and 32-bit Windows 7, plugged, unplugged and re-plugged

Checkpoint: the bench reading of 30-E.1 on Windows 98 SE, and the same
clauses observed on Windows 2000. The second half has no vehicle today:
Windows 2000 is a virtual-machine target and no QEMU device models a
SuperSpeed hub. The owner's decision of 2026-10-02 (decisions table) is that
a Windows 2000 vehicle is required for now and the question is revisited
when this phase opens; until a vehicle exists or that decision changes, this
phase cannot close and Phases 31 and 32 wait on it. Not a checkpoint: UAS.

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

- [ ] 31-0 static: Stream Context Arrays and stream ids in TRBs and events (xHCI section 4.12); the UAS class specification and T10's UAS, in both its shapes - streamed at SuperSpeed, and streamless at High Speed where the status pipe carries READ READY and WRITE READY information units and the data pipes are shared; how `usbstor.sys` presents SCSI PDOs to `disk.sys` and `classpnp` on each target and, on Windows 98 SE, how NUSB's USB mapping port driver gives them drive letters (`usbntmap.inf` binds `USBSTOR\GenDisk` and its siblings to `*IOS` with `USBMPHLP.PDR`; the decisions table and the provenance row), since the UAS driver's objects must take the same route; whether the 98 SE CD's `layout.inf` carries any of the mapping files, read from the disc; and what QEMU's `usb-uas` model presents, read from `hw/usb/dev-uas.c` - the expectation is UAS only, with no Bulk-Only alternate setting at either speed, which decides what 31-V.1 can and cannot show
- [ ] 31-A.1 primary streams in the bus: a private open-streams request on a pipe handle, stream rings, per-stream enqueue and event matching, abort of a stream, stream close
- [ ] 31-A.2 the UAS class driver, its own binary and INF in the package: SCSI PDOs, the command, status, data-in and data-out pipes, tags and queue depth, sense, task management and reset recovery, the SRB set `classpnp` sends; both transports, streamed at SuperSpeed over 31-A.1 and streamless at High Speed with the READ READY and WRITE READY handling, chosen by the enumerated speed
- [ ] 31-A.3 the id policy: the bus chooses the transport and exposes only that transport's class compatible ids - `Class_08&SubClass_06&Prot_62` and never `Prot_50` when it chooses UAS, the reverse when it chooses Bulk-Only - because id order alone decides nothing: the NT setup engines rank a signed inbox `usbstor.inf` above an unsigned match, and `usbstor.inf` carries vendor-id entries that match the hardware id regardless. UAS is chosen when the device offers the setting at the speed it enumerated at (at SuperSpeed only when the controller streams, at High Speed whenever the setting is there) and the registry value does not force Bulk-Only; the value applies only to a device that also offers a Bulk-Only setting, and a UAS-only device stays UAS under it, with a counter saying so. A device whose vendor id `usbstor.inf` names by hand still goes to `usbstor.sys` on a hardware-id match, which is recorded as the residual case rather than fought. Switching: a change of the value takes effect at the next enumeration, and on NT a devnode whose compatible ids changed keeps its installed service until it is uninstalled in Device Manager and re-plugged, which the release notes state as the procedure; the device matrix gains an expected and an observed transport field per storage row, `UAS` or `Bulk-Only`, with design record 06's verdicts unchanged - a wrong transport, a failed round trip or a failed teardown is a `FAIL` on the existing rules, and the transport is never a verdict of its own
- [ ] 31-V.1 QEMU's `usb-uas` model on both primaries, at SuperSpeed and at High Speed: enumerated, bound to the UAS driver, a verified round trip in each mode, on a fresh install and again after an uninstall and re-plug. The forced-Bulk-Only leg cannot be shown on this model if 31-0 confirms it has no Bulk-Only setting; it is 31-V.2's and 31-E.1's
- [ ] 31-V.2 the Windows 2000 half of the dual-transport clauses, which has no bench: the ASMedia bridge passed through to the Windows 2000 guest with `usb-host` at High Speed, where it offers both transports - UAS chosen on a fresh install, the forced-Bulk-Only value selecting Bulk-Only, and the switch between them through uninstall and re-plug; the same leg on the Windows 98 SE guest under NUSB as a VM control for 31-E.1
- [ ] 31-E.1 the bench, on Windows 98 SE and 32-bit Windows 7: the ASMedia bridge under UAS at SuperSpeed and, behind a USB 2.0 hub, under UAS at High Speed, since it offers both transports at both speeds (`test-equipment.md`); the MSSU10 drive under UAS at SuperSpeed, and behind a USB 2.0 hub under Bulk-Only, which is all it offers there and is the expected result rather than a failure; the forced-Bulk-Only value on both units at SuperSpeed; round trips and throughput against Bulk-Only in each case

Checkpoint: `usb-uas` round-tripping under the UAS driver on both primary
targets at SuperSpeed and at High Speed, each mode its own clause, on a
fresh install and after an uninstall and re-plug; the ASMedia bridge under
UAS at both speeds and the MSSU10 at SuperSpeed on the bench on Windows 98
SE; the forced-Bulk-Only value selecting Bulk-Only on a dual-transport unit
on both primary targets - the bench for Windows 98 SE, the passed-through
bridge of 31-V.2 for Windows 2000 - and leaving a UAS-only device on UAS in
QEMU on both. On Windows 98 SE every clause is taken with NUSB
installed, because the UAS driver's disk objects reach a drive letter only
through NUSB's USB mapping port driver (the decisions table); on a stock
install UAS cannot work, and the release notes say so. Not a checkpoint:
isochronous or interrupt streams, which USB defines nowhere, or UAS on a
stock Windows 98 SE.

Records: `design/13-superspeed-hcd.md`; `runs/run-31.md`.

## Phase 32 - Release `2.0.0.0`

Goal: the first release of the successor, both architectures, with the
release notes, the acceptance test and the post-release run rewritten for a
driver that is a bus rather than a miniport.

Status: not opened. Waits on Phase 31.

Why a phase: a cut changes every user-facing statement, and this one changes
what the download is.

- [ ] 32.1 the record: `release-notes.md` rewritten - what the HCD is and is not, every target's standing, the F8 requirement on Vista x64 and Windows 7 x64, the stock Windows 98 SE statement, the miniport's virtual-hub values having no effect (decisions table), the limitations of 28.3, the untested ground that is agreed today (29-A.4's SuperSpeed isochronous path) and whatever Phase 30's revisited decision adds, if anything; the readme template; `releases/history.md`; `README.md` and `AGENTS.md` describing two drivers, one frozen
- [ ] 32.2 the acceptance test and the post-release run of design record 09 adapted to the HCD, including a UAS row and a SuperSpeed row
- [ ] 32.3 the cut: `xhci_version.h`'s successor, both INFs' `DriverVer`, `make-release.ps1` publishing `xhci98h` in four directories beside the frozen `xhci98` tree, the ten install legs read from the asset - and each leg now carries what Phases 29 and 31 read only on the primaries: QEMU's `usb-storage` at SuperSpeed bound to that target's `usbstor.sys` with a round trip, and `usb-uas` bound to the UAS driver at SuperSpeed and, as a separate clause, at High Speed, each with a round trip and a clean teardown, on every guest, the three x64 guests on the amd64 UAS binary, which has no other runtime reading

Checkpoint: the package `make-package.ps1` assembles holds the HCD, the UAS
driver, their INFs, the tools and the readme and nothing of Microsoft's; the
ten install legs read from the asset, each with its SuperSpeed storage and
UAS clauses; the post-release run on both primaries accepted by the owner. The upload and the hand-run acceptance on a fresh VM
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
- **USB 3.1 Gen 2 and above.** Unassessed scope, not a free reading.
  SuperSpeedPlus brings its own capability descriptor, a different
  isochronous companion with a 32-bit bytes-per-interval field that the
  Endpoint Context's Max ESIT Payload derivation has to take from the other
  path (xHCI section 4.14.2), and port speed ids read through the protocol
  capability's PSI descriptors rather than a fixed table. It needs a
  descriptor, capability and transfer-format review of its own before any
  device is plugged in, and that review is a `-0` batch of a later phase.
- **The SuperSpeed storage proposal behind a switch**
  (`future-plans/superspeed-storage-behind-a-switch.md`). It was the way to
  SuperSpeed storage without leaving the miniport; with the miniport frozen it
  stays a record of what that would have taken.
