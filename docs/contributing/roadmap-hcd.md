# Development Roadmap - The Host Controller Driver, Phases 25 Onward

The third roadmap file, opened on 2026-10-02 at the project owner's request,
the day Phase 24 closed on the `1.2.0.0` cut. It carries every phase of the
miniport's successor: a monolithic USB host controller driver, `xhci98.sys`,
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
are closed records. **And it has left the tree** (owner, 2026-10-02, later
the same day): `src\` is the successor's, which took the name `xhci98.sys`;
the miniport's last sources are on branch `1.2.0.0` and its binaries in
`releases\1.2.0.0`, and nothing here builds it.

## The decisions this roadmap rests on

All taken by the owner on 2026-10-02, when the phases below were drawn up.
Each is restated where it binds a task; this table is the index.

| Decision | Taken |
|---|---|
| Pure HCD, not a registry switch | The successor is a second binary in a second package, chosen at install time through Update Driver against the same PCI class id. The miniport imports `USBPORT_RegisterUSBPortDriver`, a load-time gate, so one binary could never both serve a stock Windows 98 SE and remain the miniport; and a switch would owe every HCD change a miniport regression matrix under the "switch off is byte-identical" rule |
| Binary and package name | `xhci98.sys`, `xhci98.inf` and `xhci98-amd64.inf`, package `xhci98-<version>.zip`, built from `src\` (owner, 2026-10-02, the third decision of the day on it: `xhci98h`, then `xhci98hc` coexisting on disk with the miniport, then - once task 25.8 had built the scaffold as `xhci98hc.sys` in `src\hcd\` - the miniport's removal from the tree and this name). A `2.0.0.0` install replaces the miniport's file; going back is a reinstall of the `1.2.0.0` package |
| Version and cut point | One cut, `2.0.0.0`, after Phase 31: USB 2.0 parity on every target, SuperSpeed on root ports and behind SuperSpeed hubs, and UAS. No intermediate release |
| The miniport | Frozen at `1.2.0.0`, no further cuts, and removed from the tree (above) |
| Hub class and composite splitting | Inside the bus driver. External hubs are objects of the bus, not PDOs; a composite device is split by the bus into per-function PDOs. No hub-to-port contract to design, no INF binding path for an external hub or a composite parent (the root hub's own project-owned binding, in the root-hub row below, is the one hub binding the package carries), no per-target matching of `usbccgp.sys` or Windows 98's composite parent |
| Stock Windows 98 SE | A goal: the HCD loads, starts and binds devices on a Windows 98 SE install with no USB 2.0 stack. NUSB's and SweetLow's host-stack halves become inert under it. What a stock install still lacks is a mass-storage class driver, which NUSB's `usbstor.sys` and its `ntmap` layer supply and Windows ME ships; the HCD does not replace that. Phase 26 takes the reading (26-V.3), a checkpoint clause by the owner's decision of 2026-10-03 |
| Mass storage on Windows 98 SE | NUSB's, kept (owner, 2026-10-02, choosing this over an own storage driver). A drive letter on 9x comes from the IOS layer, and NUSB's `usbntmap.inf` binds `usbstor.sys`'s disk objects (`USBSTOR\GenDisk` and its siblings) to `DevLoader=*IOS` with `PortDriver=USBMPHLP.PDR`, the USB mapping port driver, with `NTMAP.SYS` and `USBNTMAP.SYS` beside it; NUSB's own install INF stamps `NTMAP.SYS` as `4.10.0.2227` and annotates it with hotfixes 242975 and 267304, a Windows 98 SE line, and annotates `USBMPHLP.PDR` and `USBNTMAP.SYS` as `WinMe` in its file list - the package author's notes, not a reading of the files, which were not examined (`legal-provenance.md` section 4, the NUSB 3.6 INF row; method static, text read, nothing executed). They are Microsoft's and this project does not redistribute them. An own BOT driver would still sit under that mapping, and an own IOS port driver is a VxD project with no use elsewhere. So `usbstor.sys` stays the BOT driver on every target, and on Windows 98 SE mass storage, UAS included, needs NUSB installed; on a stock install it cannot work and the release notes say so. Whether the 98 SE CD carries any of the mapping files is task 31-0's to read from the disc |
| Pool and DMA | The HCD allocates pool and its own common buffer. The miniport's "allocate no pool" rule stands for `src/`; for the HCD it is replaced by import-allowlist rows with Windows 98 export evidence, read in task 25.3, and a rule naming the sites that may allocate |
| Device names | `xHCI98 USB 3.x eXtensible Host Controller` for the controller and `xHCI98 USB 3.x Root Hub` for the root hub (owner, 2026-10-02; spelled xHCI98 by the owner 2026-10-03), as the INFs' device descriptions on every path |
| The root hub and both property tabs | Kept, all of it (owner, 2026-10-02, reversing a narrower answer given on the Codex review earlier that day). The controller FDO creates a root-hub PDO under a **project-owned** hardware id, never `USB\ROOT_HUB` - which the OS's own `usbhub.sys` claims on every NT target and which would put Microsoft's hub driver back on top of this bus - and `xhci98.inf` binds that id to `xhci98.sys` itself, so one binary is both the controller's function driver and the root hub's, and every device PDO is a child of the root hub, where Device Manager users expect it. The controller's Advanced tab: the HCD answers the `USBUSER` request set `usbui.dll` sends to the controller devnode, and the INFs carry the two registrations the miniport's do - `EnumPropPages` to `sysclass.dll` on Windows 98, `EnumPropPages32` to `usbui.dll` on the NT paths. The root hub's Power tab: the root-hub devnode registers the hub property-page provider the OS INFs register for their own root hub, and answers the hub IOCTLs that page sends, read per target in task 25.4. `XHCISNAP` reaches the HCD through the controller's door. External hubs stay objects inside the bus (the hub decision above), so a device behind one appears under the root hub devnode, and the Power tab reports the budget the bus itself keeps |
| The miniport's virtual-hub values | Not supported (owner, 2026-10-02). `XhciVirtualHSHub`, `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid` exist to make usbport tell the truth about a root-port device's speed; the HCD has no usbport to lie to and reports every device at its true speed with no hub in the way, so there is nothing for them to switch. The HCD reads none of the three, its INFs write none, a value left behind by a miniport install has no effect, and the release notes say so (32.1). Which of the miniport's other values carry over - the log switches, the interrupt moderation value - is 25.1's list |
| SuperSpeed hubs on Windows 2000 | A vehicle is required, for now (owner, 2026-10-02, on the same review). Windows 2000 is a VM-only target and no QEMU device models a SuperSpeed hub, so Phase 30's "observed on both" has no vehicle today; the owner did not record an exception and revisits the question when Phase 30 opens. Until then Phase 30 cannot close, and Phases 31 and 32 wait on it |
| SuperSpeedPlus: USB 3.1 Gen 2 and USB 3.2 Gen 1x2 and Gen 2x2, to 20 Gbit/s | In `2.0.0.0`, untested if need be (owner, 2026-10-02, later the same day, reversing the line in "What is not on this roadmap" that left it unassessed): the driver runs on every target to Windows 7, which has no USB 3 stack of its own, and newer chipsets with 10 and 20 Gbit/s ports often have no Windows 7 vendor driver either. A link that trains above 5 Gbit/s is accepted at its trained rate rather than sent back to USB 2.0. From the driver's side the step is small once Phase 29's Gen 1 path exists: the rate comes from the protocol capability's PSI dwords rather than a fixed table, bulk and interrupt framing are Gen 1's (1024-byte packets, burst to 16), and the new work is the SuperSpeedPlus isochronous companion with its 32-bit bytes-per-interval and the Endpoint Context's Max ESIT Payload Hi (task 29-A.6). Vehicles: QEMU models nothing above 5 Gbit/s; the P14s Gen 1's chipset controller (`8086:02ED`) advertises a USB 3.1 protocol with eight PSI dwords on ports 13-18 (`xhciqual/results/p14s-gen1-2026-07-25/PROBE.LOG`), but its published 10 Gbit/s connectors are the two USB-C 3.1 Gen 2 / Thunderbolt 3 ports, and neither source says which controller those reach - the chipset's ports or the Thunderbolt controller's own xHCI - so whether any P14s connector reaches 10 Gbit/s under this driver is 29-0's question and 29-E.2's reading; no 20 Gbit/s port is held (the B650M, whose chipset has one, is gone). What no vehicle reads is untested ground in the release notes (32.1) |
| USB Audio 2.0 | Not in this repository and not on this roadmap. A class driver of generic design, so that it serves machines on EHCI and vendor stacks as well as this one, built in its own repository; it needs nothing from xHCI, since UAC 2.0 runs at High Speed and binds by class through the standard URB contract |
| One bench session before the cut | All bench work moves to one combined bench session immediately before the `2.0.0.0` cut (owner, 2026-10-03). Phases 28, 29, 30 and 31 each close their V clauses and A tasks and stay open on their E.1 clause alone; work proceeds into the next phase while the earlier E.1 clauses wait - an explicit exception to the rule that no phase advances past an unobserved checkpoint, limited to the E.1 clauses. The session reads 28-E.1, 29-E.1, 30-E.1 and 31-E.1; each such phase closes only when its clause passes, and the cut (Phase 32) waits on the session. Supersedes the owner's instruction of 2026-10-02 to continue into Phase 29 while 28-E.1 waits. Accepted costs: Phase 30's SuperSpeed hub work, which no QEMU device models, is first executed on the bench; QEMU's SuperSpeed model is Phase 29's only runtime check until then; real-controller defects surface late and together. Mitigations in VMs: a strict mode in the `qemu` flavour checking xHCI slot and endpoint state preconditions before every command, real devices by `usb-host` passthrough, the SMP Windows 2000 guest with Driver Verifier, and Codex reviews aimed at specification preconditions |

## The shape of the successor

One bus driver, in two roles. As the controller's FDO it owns the hardware
and creates one child, the root-hub PDO under a project-owned id; as that
root hub's FDO, bound by its own INF, it enumerates every device on the bus
and creates a PDO per device, or per function of a composite device, with the
id strings each target's class INFs match. Reused unchanged:
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

Status: closed 2026-10-03 (below), added 2026-10-02. Every task was worked on 2026-10-02, in one
session: the readings of 25.2 to 25.6 by subagents, statically, each fact
tagged and its row added to `legal-provenance.md` section 4 (design record
13 sections 6 to 10 say which reading is whose), and 25.1, 25.7 and 25.8 by
the coordinating session. Two owner decisions taken during it changed the
shape of 25.8 and are in the decisions table: `src\` is the HCD, and the
HCD is `xhci98.sys`. Design record 13 section 12 is the index of what the
phase leaves to later tasks. **Closed 2026-10-03:** six Codex review
rounds over `3c1d7f2..86cc59a` (the last clean), then merged into the
`2.0.0.0` branch, on the owner's instruction of 2026-10-02 to merge once the
loop was clean - taken as the owner's acceptance
of the checkpoint below.

Why a phase: the miniport's Phase 3 spike and every `-0` batch since exist
because a body written against an assumed contract cost a rewrite. The HCD
has three contracts the miniport never had to match - the function-driver
contract above the PDO, the Windows 98 export surface below the FDO, and the
`USBUSER` door - and each differs by target. Reading them is cheap; writing
against a guess is not.

Tasks (plain ids: every one but 25.8 is confirmed statically, and 25.8 by
the build wrapper alone):

- [x] 25.1 design record 13, `design/13-superspeed-hcd.md`, grown from the future-plans page that moved there on 2026-10-02: the stack shape above, the decisions table, the object model (FDO, device PDOs, function PDOs, the hub and port objects inside the bus), the enumeration state machine, the lock model against design record 05, the source layout (the HCD's own directory and `sources` file, the pure core compiled from `src/` by path, which `build.exe` must be shown to accept), the two INFs and the package
- [x] 25.2 the function-driver contract, per target, static: for `hidusb.sys`, `usbstor.sys` (NUSB's, Windows 2000 SP4's, XP's, XP x64's, Vista's and 7's in both architectures), `usbaudio.sys` and the ASIX adapter's driver, every `IOCTL_INTERNAL_USB_*` code sent to the PDO, every `IRP_MN_QUERY_INTERFACE` GUID and version (`USB_BUS_INTERFACE_USBDI_V0` to `V3`), every `URB_FUNCTION_*` used, and every `usbd.sys` export imported; recorded as tables with a `legal-provenance.md` section 4 row each
- [x] 25.3 the Windows 98 export evidence for the allowlist: pool (`ExAllocatePool`, `ExFreePool`), device objects (`IoCreateDevice`, `IoDeleteDevice`, `IoAttachDeviceToDeviceStack`, `IoDetachDevice`), interrupts and DPCs, the DMA pairing (`HalGetAdapter` with `HalAllocateCommonBuffer` against `IoGetDmaAdapter`, which `win98-wdm.md` leaves unverified), transfer-buffer mapping (`IoAllocateMdl`, `MmGetPhysicalAddress` or the map-register route), registry and the device-interface calls; each a row tagged `static`, read by the import gate's own method; and the rule that replaces "allocate no pool" for the HCD's file set
- [x] 25.4 the door and the two tabs: the `USBUSER` request set `usbui.dll` sends to the controller devnode on each target for the controller's Advanced tab; the hub IOCTLs `usbui.dll` sends to the root-hub devnode for its Power tab (`IOCTL_USB_GET_NODE_INFORMATION`, the node-connection family, the hub capabilities, the power budget it draws), and the registration each OS's own INF writes for its root hub - `EnumPropPages32` to `usbui.dll`'s hub provider on the NT paths, and on Windows 98 whatever page NUSB's and SweetLow's `USB2.INF` register for theirs, read from those files; the project-owned root-hub id and its INF section on all four install paths; the registrations the HCD's INFs must write for it - on Windows 98 `EnumPropPages` naming `sysclass.dll`'s controller page, with `usbui.dll` drawing its dialogs, and on the NT paths `EnumPropPages32` naming `usbui.dll`'s provider, each as the miniport's INFs write them today (`build-and-test.md`, the INF table and its `EnumPropPages` bullet) - and `XHCISNAP`'s `USBUSER_PASS_THRU` shape carried over so the tool changes as little as possible; what `IOCTL_USB_GET_NODE_INFORMATION` and its siblings must answer for Device Manager's view by connection, and the fact that the node-connection interface those systems' UI uses predates SuperSpeed and reports High Speed at most, which is why no checkpoint reads a speed from Device Manager
- [x] 25.5 the harness and the gates: what `scripts/vm-matrix` reads by offset from the miniport extension and how it reads the HCD's, `gen-offsets.ps1` for a second binary, the import gate's second allowlist, the INF gate on the HCD's two INFs, `make-package.ps1` and `make-release.ps1` staging a second package beside the frozen first (since the owner's decisions of 2026-10-02 the HCD is the only product, so 'second' reads 'the HCD's' throughout; design record 13 section 9)
- [x] 25.6 the hub and composite design inside the bus: the hub class request set, the status-change pipe, port reset and enable, TT assignment from design record 02, hub depth, hub removal with the subtree beneath it; the per-function PDO ids, the grouping rules - an IAD where the device has one, and for devices without one the legacy interface-collection rules Microsoft's composite parent applies, chiefly the audio rule that groups an AudioControl interface with the AudioStreaming and MIDIStreaming interfaces after it, which every UAC 1.0 unit in `test-equipment.md` needs - URB routing from a function PDO to the parent's pipes, and the id strings each target's INFs match (`USB\VID_&PID_&REV_`, `MI_`, the class and `COMPOSITE` compatible ids)
- [x] 25.7 the transfer-buffer policy: direct DMA from the URB's MDL against bounce buffers, decided on what 25.3 finds Windows 98 exports and on design record 04's arithmetic for the common buffer the HCD now allocates itself
- [x] 25.8 the build scaffold, the one code task of this phase (since the owner's decisions of 2026-10-02: `src\` itself, the miniport removed from it, the binary `xhci98.sys`; design record 13 section 5.1): the HCD's directory and `sources` file with the pure core compiled from `src/` by path, a `DriverEntry` that registers nothing and returns, `build-driver.cmd` building the second target in all three flavours and both architectures, the second import allowlist with only the rows that empty driver needs, the two INFs through the INF gate, and the packager refusing to stage the scaffold; nothing here installs on a guest, which is 26-A.1's

Checkpoint: design record 13 complete with the per-target contract tables of
25.2, the allowlist rows of 25.3, the door of 25.4 and the harness plan of
25.5, each fact carrying its provenance row; 25.8's empty `xhci98.sys`
built through `build-driver.cmd` with the gates green, which is the one
thing here a build settles and a reading cannot. Not a checkpoint: a contract
taken from the DDK headers alone, a Windows 98 export assumed from Windows
2000's, or the scaffold on a guest.

Records: `design/13-superspeed-hcd.md`; `legal-provenance.md` section 4;
`usb-xhci-info/win98-wdm.md`; `scripts/import-gate/`.

## Phase 26 - The USB 2.0 Bus Driver on Root Ports

Goal: `xhci98.sys` as a WDM bus driver on Windows 98 SE and Windows 2000
SP4, driving every USB 2.0 device the miniport drives on a root port - HID,
mass storage, Ethernet and composite audio - through the OS's own class
drivers, with no `usbport.sys`, `usbhub.sys` or `usbhub20.sys` beneath or
above it.

Status: opened 2026-10-03 on the owner's go, on branch `phase-26` (the
owner's rule of 2026-10-02: one branch per phase, merged into `2.0.0.0` when
its checkpoint closes); the owner has pre-authorized closing its checkpoint
when its written clauses pass. Worked in four batches: 26-A.1, A.2 and
26-V.0; A.3 and A.4; A.5 and A.6; A.7 to A.10, then 26-V.1 to V.3. Batches (a), (b) and (c) are closed, and batch (d) closed on 2026-10-04: 26-A.7 to A.10 done and 26-V.1 to V.3 read (`runs/run-26.md`). Every clause of the checkpoint below has been read - round 12's root-port rows at parity in 26-A.10's first matrix run; the disable, enable, remove and rescan sequence on Windows 2000 in 26-V.2; the Windows 98 door sequence - disable, enable, remove and rescan with a mouse attached - on Windows 98 SE under NUSB 3.3 and under SweetLow's stack on `a7ddbfa` (`runs/run-26.md`, "The Windows 98 door sequence"); Driver Verifier on Windows 2000 in 26-V.2; the root hub with every device beneath it, the Advanced tab, the Power tab and an `XHCISNAP` report on both in c17, 26-V.1 and 26-V.2; and 26-V.3 - and the checkpoint closes on `a7ddbfa`, the build of 26-V.1's and 26-V.2's last rows (`ac25e4e`'s fix for the ASIX adapter's Windows 98 driver over `40efd31`).

Why a phase: it is the "be usbport" step the architecture record always
named as the large one, and it is where the HCD either matches the miniport
or does not. Hubs are kept out so that every reading here is a root-port
reading with one fewer variable.

Tasks, batched: `-A` host, `-V` VM.

- [x] 26-A.1 the scaffold of 25.8 made stageable: the packager staging the second package, and the allowlist grown to what the FDO of 26-A.2 imports; the guest half is 26-V.0. Done 2026-10-03: the scaffold marker left `hcd_entry.c`, which gained an FDO that attaches over the PCI PDO, forwards PnP and power, and refuses its start once the PCI stack has started (26-V.0's Code 10); the allowlist took its first 14 rows from design record 13 section 7.4, verbatim, and the import gate learned the `SITES=` field of section 7.5 (checked per object with `dumpbin /symbols`, with self-tests); `make-release.ps1` refuses the scaffold marker in its publish loop as `make-package.ps1` does at staging (section 9.3)
- [x] 26-A.2 the FDO: `DriverEntry`, `AddDevice`, start, stop, remove and the Windows 98 out-of-sequence remove that stands in for surprise removal, the power handlers of `win98-wdm.md` with `DO_POWER_PAGABLE` on every object, resources, BAR mapping, the interrupt, the HCD's own common buffer by 25.7, and the controller sequence of `xhci_init.c`, `xhci_cmd.c` and `xhci_evt.c` with the usbport services replaced. Done 2026-10-03 (`runs/run-26.md`, "26-A.2"): the kept sequence runs on the HCD's own resources, interrupt, DMA adapter and common buffer, with usbport's services replaced by `hcd_svc.h`'s and a per-controller lock; on both primaries in QEMU the controller starts ("working properly"), passes its No Op self-test through its own interrupt and DPC, and survives two disable/enable cycles - which took three Windows 98 SE fixes and one Windows 2000 fix the runs found: PCI configuration IRPs sent to the PDO (as 98 SE's `uhcd.sys` and NUSB's usbport send them), and the controller thread waited for on an event on Windows 98 and on its thread object on NT, chosen by `IoIsWdmVersionAvailable(1, 0x10)`. The power handlers are written and have not run: neither primary's test guest sleeps
- [x] 26-A.3 root ports: the port shadow and change path from the interrupt, reset and speed decode, the USB 2.0 port classification unchanged, and no usbport-style root-hub callbacks and no High-Speed lie - the bus knows each port's true speed and tells the truth, to its own device PDOs and, through 26-A.8's hub IOCTLs, to the root hub's Power tab. Done 2026-10-03 (`runs/run-26.md`, "Batch (b)"): the controller thread services each changed USB 2.0 root port through the pure enumeration machine of design record 13 section 5.3 (`xhci_enum.c`, host suite `test_enum`) - debounce, reset, the speed read from PORTSC and carried unchanged; the root hub's Power-tab answers are 26-A.8's
- [x] 26-A.4 the root hub and device PDOs: the root-hub PDO under its project-owned id and the driver's second role as that PDO's FDO through `xhci98.inf`, then Enable Slot, Address Device and the descriptor reads as a state machine of the bus's own, the PDO per device, `IRP_MN_QUERY_ID` strings per target from 25.6, `QUERY_DEVICE_RELATIONS`, `QUERY_CAPABILITIES` and the PDO's PnP and power minimum set. Done 2026-10-03 (`runs/run-26.md`, "Batch (b)"): the root-hub PDO and FDO, Enable Slot, Address Device, the descriptor reads, a device PDO per enumerated device with section 10.7's ids, BusRelations, capabilities and the PDO PnP and power minimum; observed on both primaries with a hot-plugged mouse that binds `hidusb.sys` (Code 10 until 26-A.5). The instance id is the root port until the serial string is read
- [x] 26-A.5 URB dispatch: `IOCTL_INTERNAL_USB_SUBMIT_URB` and the function set `win98-wdm.md` lists, pipe handles, select configuration and interface through Configure Endpoint, control, bulk, interrupt and isochronous TD construction over the bus's own mapping, abort and reset pipe, cancellation, the interval and cadence taken from `bInterval` directly. Done 2026-10-03 (`runs/run-26.md`, "Batch (c)", c13 to c16; Codex rounds 1-21 closed): control, bulk and interrupt through the transfer engine and the map pump, SELECT_CONFIGURATION, SELECT_INTERFACE, ABORT_PIPE, RESET_PIPE, cancellation, GET_CURRENT_FRAME_NUMBER, refusals completed at the next tick; a USB mouse, keyboard and tablet work on both primaries, every unplug pattern included (c12); the Waiting list in place of ERROR_BUSY (`08c0d5a`); the transfer MDL mapped alone and bounded to its byte count, as usbport maps it; isochronous transfers (`fb8d8f0`), admitted and published with audio playing on both primaries (c16)
- [x] 26-A.6 the rest of the PDO contract 25.2 found: `GET_PORT_STATUS`, `RESET_PORT`, `GET_DEVICE_HANDLE`, the `USB_BUS_INTERFACE_USBDI` versions each target's drivers query. Done 2026-10-03 (`runs/run-26.md`, c14): `GET_PORT_STATUS`, `RESET_PORT`, `CYCLE_PORT`, `GET_BUS_INFO` and `GET_HUB_COUNT` served (`1fa0cc2`); storage works on both primaries with `RESET_PORT`. Not needed on the primaries (design record 13 section 6.5): no class driver read sends `GET_DEVICE_HANDLE`, and none on Windows 98 SE or 2000 queries `USB_BUS_INTERFACE_USBDI`; the USBDI versions move to 28-A.1
- [x] 26-A.7 composite splitting: per-function PDOs, IAD grouping, and the grouping rules for devices that carry no IAD - chiefly audio, where an AudioControl interface and the AudioStreaming and MIDIStreaming interfaces that follow it are one function and a HID interface beside them is its own, the rule every UAC 1.0 unit in `test-equipment.md` needs since none of the five has an IAD - plus routing and the function ids; those units and the IAD-grouped X4 are the specimens. Done 2026-10-03 (`runs/run-26.md`, c15 and c16; design record 13; Codex rounds 19 and 20): the C-Media 0D8C:0014 (no IAD) split into an audio function (AudioControl and two AudioStreaming) and a HID function on both primaries, each bound and working, with no composite parent; c14's Windows 2000 `usbaudio.sys` bugcheck gone, and playback through the audio function (c16). The bench's UAC units and the X4 are 26-V.1's
- [x] 26-A.8 the door: `USBUSER` for `usbui.dll` and `XHCISNAP` on the controller, the hub IOCTLs of 25.4 on the root hub for its Power tab, the log ring moved in with its sinks and switches, `XHCISNAP` taught the second driver; and one sink the miniport could not have: a PASSIVE-level flusher that emits the ring through `DbgPrint` continuously, because the HCD owns a PASSIVE context - its own system thread or work item - where the miniport on Windows 98 never reached PASSIVE between `StartController` and shutdown. The ban stands on what it always stood on: per-line `DbgPrint` from DPC or ISR context under DebugView bugchecks Windows 98 on real hardware on three device classes (design record 08), and owning the driver object removes the door problem, not that one; the flusher records from any IRQL and emits from PASSIVE only, behind the same verbosity switch. Done 2026-10-04 (`runs/run-26.md`, c17, "26-V.1" and "26-V.2"): written and merged (`0e470b9`, Codex round 21's minors in `2df17c3`, round 22's fixes in `370c135`); the controller's Advanced tab and the root hub's Power tab read on both primaries, on Windows 98 SE under NUSB 3.3 and under SweetLow's stack and on Windows 2000; `XHCISNAP -probe` answering "the channel is live" with the channel on and "MINIPORT DECLINED" with it off, and a full `-o` report on Windows 98 SE under both stacks and on Windows 2000. `XHCISNAP`'s closing text still names "Windows' USB port driver", a stale string left for a later change
- [x] 26-A.9 host suites for every pure piece: URB parsing, id generation, the enumeration state machine, the composite split with and without an IAD, the TD builder over the new mapping. Done 2026-10-03 (merged `8c324e7`): `test_xfer` and `test_iso` restored, `test_td` new over the chunk planner's and map pump's lists, `test_enum` and `test_func` extended; one defect found and fixed (an empty isochronous block took Frame IDs)
- [x] 26-A.10 the matrix's expectations for the HCD, before any 26-V run: `scripts/vm-matrix/matrix.psd1` encodes the miniport's behaviour, not a device's - the `advance endpoint speed mismatches` row asserts the High-Speed lie on a root port with the switch off, the switch-aware rows assert the virtual hub's extra tier and usbport's naming of it, and the counters are read at the miniport extension's offsets - so a literal field-for-field comparison would fail a correct HCD. 25.5's plan becomes a second expectation set: the usbport mismatch counter has no HCD counterpart and leaves the executable expectations altogether, with its inapplicability recorded as a comment beside the row - `inert` would not do, because the harness resolves an `inert` label to a real counter field and reads it, so an absent field is an error rather than a reading; in its place each device row carries an expected speed and the harness asserts the HCD's decoded port speed and the speed it programmed into the Slot Context against it, so a wrong speed report is a `FAIL` and not a silence; the virtual-hub tier rows go; every other usbport-only counter is dropped or mapped to the HCD's; the offsets are regenerated for the HCD's extension; and the verdict rules of design record 06 are kept as they are. Done 2026-10-04 (`runs/run-26.md`, "26-A.10"): `matrix-hcd.psd1` written with a speed per device row and the usbport-only and virtual-hub labels refused (`a219fe1`, selftest 460 checks); the HCD publishes the counter block `gen-offsets` reads (`a1be180`); the set's first run, on overlays of both primaries' golden images with `40efd31`, at parity - every row PASS, NODRIVER where the row expects no driver, or EXCLUDED by the set on Windows 98 SE - once `f9f57fe` set `usb-bot` and `usb-uas` to High Speed: QEMU 11.1 presents both at 480 Mb/s, the HCD reported that speed truly, and the first run had failed both rows on the expectation alone. `f9f57fe` also gave each target its own `ExtraArgs`
- [x] 26-V.0 the scaffold on both primaries: `xhci98.inf`'s undecorated path on Windows 98 SE and its `.NTx86` path on Windows 2000, the empty driver installing with no prompt beyond Windows' own request for its in-box files (the owner's wording, 2026-10-03: Windows 98 SE asks for its CD for `usbd.sys`, which the INF fetches through `LayoutFile`, on a base that lacks it) as `xHCI98 USB 3.x eXtensible Host Controller` and showing Code 10; the amd64 INF's first install is 28-V.1's. Read 2026-10-03 (`runs/run-26.md`, "26-V.0"): Windows 2000 installed with no prompt and showed Code 10; Windows 98 SE showed Code 10 after its restart, its install asking only for the Windows 98 CD for `usbd.sys` (Codex review of 26-A.2, round 1, note 12) - which the reworded clause allows; ticked on that reading
- [x] 26-V.1 Windows 98 SE under NUSB 3.3 and under SweetLow's stack: install, start, mouse, storage, composite audio, the ASIX adapter by passthrough, round 12's root-port rows, `xHCI98 USB 3.x Root Hub` under the controller with every device beneath it, the controller's Advanced tab and the root hub's Power tab, an `XHCISNAP` report. Done 2026-10-04 (`runs/run-26.md`, "26-V.1"; each row names its build, `40efd31` or `a7ddbfa`): on both stacks, the install from clean with only Windows' own CD prompts and its restart, the root hub with every device beneath it, the mouse, storage (`fc /b` clean), the composite audio device split with both functions bound and a play to its end, the Advanced and Power tabs, an `XHCISNAP` report, and a clean shutdown. The ASIX adapter's Windows 98 driver (`AX88772.SYS` 3.0.3.12) faulted on `40efd31` - "fatal exception 0E ... in VXD ax88772(01) + 00001576": it polls `IoStatus.Status` for `STATUS_PENDING`, which the HCD did not write - and bound and worked on both stacks on `a7ddbfa`, after `ac25e4e` made `STATUS_PENDING` visible on every pended URB IRP. Round 12's root-port rows are 26-A.10's matrix run
- [x] 26-V.2 Windows 2000 SP4: the same rows, Driver Verifier on, the SMP guest of Phase 2d, the disable, enable, remove and rescan sequence. Done 2026-10-04 (`runs/run-26.md`, "26-V.2"): on `40efd31`, the install from clean with no prompt, Driver Verifier on `xhci98.sys` from the second boot (`verifier /flags 27`: the flags are decimal on Windows 2000), the mouse, storage, the ASIX adapter bound with ASIX's driver, the Advanced and Power tabs and an `XHCISNAP` report, and the disable, enable, remove and rescan sequence; composite audio failed there under Verifier in an EP0 stall storm, recorded as a transient host or passthrough state, and played to its end on `a7ddbfa` with Verifier on and no storm; the SMP guest of Phase 2d on `a7ddbfa` under TCG (under WHPX it hung in the BIOS before the driver ran): install over the miniport, mouse, storage, disable and enable, Verifier on. No bugcheck. An install-notes item: the first Update Driver after a cancelled Found New Hardware Wizard left the controller's driver key empty, and a second filled it
- [x] 26-V.3 the stock reading, a checkpoint clause (the owner's decision, 2026-10-03): a fresh Windows 98 SE guest with no USB 2.0 stack installed, the HID mouse and the composite audio device bound, storage reading `NODRIVER` as design record 06 defines it. Done 2026-10-04 (`runs/run-26.md`, "26-V.3", on `40efd31`): the base made by uninstalling NUSB 3.3 from the NUSB base (the owner's decision of 2026-10-04); the controller installed with Windows' CD prompt for `usbd.sys` and a restart, the HID mouse bound and moving the pointer, the composite audio device split with both functions bound and a play to its end, and the stick reading `NODRIVER`

Checkpoint: round 12's root-port rows at parity on both primary targets,
where parity means the same device behaviour under 26-A.10's expectations -
each device enumerated, bound and exercised as round 12 saw it, at its true
speed, with design record 06's verdicts - and not the miniport's field
values, which encode usbport's lie and the virtual hub; the disable, enable,
remove and rescan sequence on Windows 2000 and
the Windows 98 door sequence; Driver Verifier clean on Windows 2000; the
root hub devnode with every device beneath it, the controller's Advanced tab,
the root hub's Power tab and the `XHCISNAP` report on both. 26-V.3 is a clause (the
owner's decision, 2026-10-03). Not a checkpoint: a device behind a
hub, or any SuperSpeed port powered.

Records: `design/13-superspeed-hcd.md`; a run sheet `runs/run-26.md`;
`scripts/vm-matrix/README.md`.

## Phase 27 - External USB 2.0 Hubs Inside the Bus

Goal: USB 2.0 hubs driven by the bus itself - High-Speed single-TT and
multi-TT, Full and Low Speed devices behind them, hubs behind hubs to the
depth xHCI allows - with the device matrix at full parity on both primaries.

Status: opened 2026-10-04, worked on the integration branch `p27-int` for the phase branch `phase-27`. The tasks and their records:
- 27-A.1, A.2 and A.4, and 28-A.2, which the owner moved here: done in parallel and merged.
- 27-A.3: done, with Codex rounds 1 and 2 on the batch.
- The Windows 98 SE HID-unplug stall: 27-V.1 found it and five Codex rounds closed it (`18ecc83`).
- 27-V.1: read on both primaries (`runs/run-27.md`).

**Every checkpoint clause, and where it was read:**
- **The round 12 matrix at parity, every device at its true speed:**
  - Windows 2000, all 17 rows on `h2kb`, and the HID, storage and hub groups on three more builds: every row PASS, or NODRIVER where expected.
  - Windows 98 SE, all 17 rows on two builds: every row PASS, NODRIVER where expected, or EXCLUDED by the set; the hub group, the five-tier churn included, on the re-prepared image (`h98jh`).
  - Every row's port speed and Slot Context speed held.
  - "Its own interval" has no expectation in `matrix-hcd.psd1`. It rests on 26-A.5's interval taken from `bInterval` directly.
- **A Low-Speed mouse at `bInterval` 10 polled every 8 ms, behind the hub and on a root port:** moved to 28-E.1's bench by the owner's decision of 2026-10-04, since no QEMU model is Low Speed (`matrix-hcd.psd1`). Its host vectors are in `test_hub` (`test_low_speed_mouse`): the endpoint programmed from the device's own speed class on a root port and behind a Full-Speed hub whose port reports it Low Speed, to xHCI Interval 6, 8 ms, with a Low-Speed bulk endpoint refused to show the hub's Full Speed is not what it is given.
- **Full-Speed audio bound with its isochronous endpoints opened and the isochronous error counters at zero:**
  - On a root port: `usb-audio/fs` PASS on both primaries, with the iso error counters 0 on Windows 2000 and the traffic inert on Windows 98 SE, as the clause allows.
  - Behind the Full-Speed hub: not read. No run put `usb-audio` behind `usb-hub`.
- **The churn soak:** clean.
  - Windows 98 SE: 120 of 120 hubs enumerated with the guest responsive (`soak-h98j`, `soak-h98f`), where the miniport wedged at 12 and 18.
  - Windows 2000: 120 of 120 on every build.
  - The harness's frozen-IDE-IRQ-14 clause, which the healthy Windows 98 SE guest also showed, became a NOTE (`cd38a98`).
- **The TT and High-Speed hub paths on 27-A.4's host vectors:** green. `test_topo` has 2206 checks and no gap open, beside `test_hub` and the hub-port vectors.

The clauses QEMU cannot take rest on 27-A.4's host vectors and are 28-E.1's bench clauses: the High-Speed hub, the single-TT and multi-TT hubs, Full and Low Speed devices behind them, and a Full-Speed hub behind a High-Speed one.

Suspend and resume are handled, not initiated, by the owner's decision of 2026-10-04 (27-A.1): the bus starts no suspend - selective suspend, hub ports included, is 28.3's idle-policy decision - but a hub port reported suspended is resumed before it is reset or enumerated, and a finished resume, a remote wake among them, needs no re-enumeration.

Open limitation, carried to 28.3: on Windows 2000, an audio device unplugged during playback receives SURPRISE_REMOVAL and ABORT_PIPE and no REMOVE. It predates the phase: the control on Phase 26's `a7ddbfa` reads the same.

The coordinator judges the checkpoint **ready to close, pending the owner's go**. The Low-Speed mouse clause is the bench's by the owner's decision of 2026-10-04 (above); Full-Speed audio behind the hub, which QEMU can present but no run did, still needs the owner's ruling.

Why a phase: the hub class is the second half of what `usbhub.sys` did, and
the transaction-translator path is where the miniport's issue 6 and design
record 12 spent two phases. The bus owns the hub now, so the virtual hub, the
High-Speed lie and the TT lookup that bugchecked usbport have no equivalent;
what remains is the hub class itself, and the churn that wedged Windows 98.

Phase 27 opens with 28-A.2, the amd64 build that loads in a guest and its
second INF (the owner's decision, 2026-10-03), so every later batch reads an
amd64 leg beside the x86 ones.

- [x] 27-A.1 the hub class: hub descriptor, port power, the status-change pipe, per-port reset, enable, suspend and resume, port state machines, hub depth, the multi-TT alternate setting. Done 2026-10-04 (`runs/run-27.md`, "Batch (a)"; `23e7715`, Codex rounds 1 and 2 in `b5ed0c4` and `900dc83`): a hub is brought up by the bus and never gets a PDO - SET_CONFIGURATION, SET_INTERFACE(1) for a multi-TT hub, the hub descriptor, one Configure Endpoint with Hub, ports, TTT and MTT, port power, a GET_STATUS per port, the status-change transfer armed; its ports run the `xhci_enum.c` machine through hub class requests (three attempts, then `CLEAR_FEATURE(PORT_ENABLE)`), with a fresh `C_PORT_RESET` required and an elapsed-time debounce; the depth checked at the topology attach; pure logic in `xhci_hub.c`, host suite `test_hub`; on guests, the hub group of 27-V.1 on both primaries. Suspend and resume are handled, not initiated, by the owner's decision of 2026-10-04: the bus never suspends a hub port - selective suspend, hub ports included, is 28.3's idle-policy decision - but a port its hub reports suspended is resumed (`ClearPortFeature(PORT_SUSPEND)`, the resume signalling and recovery waited, USB 2.0 7.1.7.7, to transcribe) before it is reset or anything else is asked of it, and a `C_PORT_SUSPEND` on a connected, enabled port - a finished resume, a remote wake among them - is cleared with no re-enumeration (`XhciHubPortDecide`, `XhciHubResumeBeforeReset`, `XhciHubResumeProgress`; `test_hub`'s `test_suspend`)
- [x] 27-A.2 topology: `xhci_topo.c` fed by the bus's own hub traffic, Route String, parent slot and TT fields per design record 02, Full and Low Speed behind High-Speed hubs, a Full-Speed hub behind a High-Speed one. Done 2026-10-04 (`runs/run-27.md`, "Batch (a)"; `23e7715`, `fa6f199`): each device behind a hub takes its Route String, root port, tier and TT fields from `xhci_topo.c` through one Slot Context builder, with the instance id `(route << 8) | root port`; 16 nodes, SET_INTERFACE on an attached node and `C_PORT_ENABLE` with enable 0 as a departure close 27-A.4's gaps G1 to G3; CLEAR_TT_BUFFER on EP0 recovery, a bulk reset, an Address Device transaction error and the quiesce. On guests, the five-tier chain behind QEMU's Full-Speed hub addressed and bound to tier 5 on both primaries (27-V.1). The High-Speed hub and TT paths rest on the host vectors and are 28-E.1's
- [x] 27-A.3 removal: a hub pulled with devices beneath it, the subtree's PDOs and slots, a device pulled mid-transfer, the orderly and the surprise paths on each primary. Done 2026-10-04 (`runs/run-27.md`, "27-A.3" and "The Windows 98 SE HID-unplug stall"; `b5ed0c4`, then `26e7cb6` to `53b43c0`, merged `18ecc83`; Codex rounds 1-5 on the fix): one teardown, `hcdSubtreeGo`, in design record 13 section 10.5's order as corrected - freeze; the PDOs reported missing first; endpoints stopped and a departed device's URBs parked on its PDO, completed CANCELLED at the client's cancel, its ABORT_PIPE horizon (64-bit submission stamps) or the PDO's stop, surprise removal or removal; slots disabled leaf-first; hubs pruned deepest-first. Read: the churn's first hub pulled with the five-tier chain beneath it on both primaries (27-V.1); a keyboard, a mouse and a stick unplugged on Windows 98 SE, the REMOVE within about 1 s each; on Windows 2000 SURPRISE_REMOVAL and REMOVE within about 1 s, a stick pulled mid-copy of a 256 MB file included. The orderly path with a hub beneath it (a root-hub or controller disable) was not read on a guest in this phase. Open, carried to 28.3: on Windows 2000 an audio device unplugged mid-playback receives no REMOVE, as on Phase 26's `a7ddbfa`
- [x] 27-A.4 host suites: the hub state machines and the topology fold against the host vectors design record 12 left. Done 2026-10-04 (`runs/run-27.md`, "Batch (a)"; `74ac195`, `fa6f199`): `test_topo` with 20 placement rows (Route String, tier, root port, parent, TT triple), the depth limit, hub marking, removal sweeps and malformed hub descriptors, citing design records 02, 12 and 13; 39 hub-port state-machine rows as data (`hub_port_vectors.h`); its three gaps G1 to G3 closed by the hub class and turned into checks (2206 checks, none open); and `test_pipe`'s vectors for the submission stamps across the 32-bit wrap
- [x] 27-V.1 the device matrix on both primaries behind QEMU's `usb-hub`, which is a Full-Speed hub and the only hub QEMU models: a Full-Speed hub on a root port, Low and Full Speed devices behind it, hubs behind hubs to the depth limit - recounted for the HCD, since the virtual hub's extra tier is gone and the matrix's five-tier chain is five deep in Windows' view again, so the tier-5 device the miniport's expectations assert as never addressed is now addressed and expected to bind - 25 plug and unplug cycles per device, and the hub-churn soak that wedged Windows 98 under the miniport, re-measured. No High-Speed hub, transaction translator or Full-Speed-hub-behind-High-Speed-hub clause can be taken in QEMU; those are 28-E.1's bench clauses, and on Windows 2000 they rest on 27-A.4's host vectors, the standing design record 02 gave the miniport's TT path. Done 2026-10-04 (`runs/run-27.md`, "27-V.1"; the matrix subagent's runs on five builds, `7fddbac` to `09ed9d1`): Windows 2000 - every row PASS or NODRIVER as expected on `h2kb` (all 17 rows) and on `h2kc`, `h2kf` and `h2kh` (HID, storage and hub), the five-tier churn with its tier-5 mouse bound, and every soak PASS - 25 cycles per class on two builds and 10 on two - with 120 of 120 hubs enumerated in the churn; Windows 98 SE - every row PASS, NODRIVER or EXCLUDED as expected on two builds, the hub group with the five-tier churn and its tier-5 mouse bound on the re-prepared image (`h98jh`), 25-cycle soaks, and the 120-hub churn 120 of 120 with the guest responsive (`soak-h98j`, `soak-h98f`), where the miniport wedged at 12 to 18 (`lessons.md`, task 12.5). The soak's IDE IRQ 14 clause read frozen on that responsive guest and became a NOTE with its reason (`cd38a98`); Windows 98 SE's `USBAUDIO.VXD` fault on audio cycles is the known guest limitation, and audio is not a soak class there; a location a Windows 98 SE image has not seen raises the wizard and holds that port's enumeration, per port and by design

Checkpoint: the round 12 matrix at parity on both primary targets for every
row QEMU can present, with every device at its true speed and its own
interval; a Low-Speed mouse at `bInterval` 10 polled every 8 ms behind the
Full-Speed hub and on a root port alike; Full-Speed audio bound behind that
hub and on a root port with its isochronous endpoints opened and the
isochronous error counters at zero - binding, not playback, which no QEMU run
can show on Windows 98: its `USBAUDIO.VXD` faults after one URB through this
driver and through a UHCI control alike (`lessons.md`, "Windows 98 SE cannot
play USB audio here"), and the matrix treats that traffic as inert there, so
audible playback is 28-E.1's bench clause as it was the miniport's; the churn
soak either clean or recorded as the
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
- [x] 28-A.2 (moved to the start of Phase 27 by the owner's decision of 2026-10-03, so amd64 guest legs run beside the x86 legs from then on; it stays numbered here) the amd64 build of the HCD: WDK 7.1 as `WNET`, no `usbport` import library at all, the `_WIN64` halves of the bus's own structures, the amd64 allowlist, the second INF. Done 2026-10-04 in Phase 27 (`runs/run-27.md`, "28-A.2 on Windows XP x64"; `dff49ae`): the overflow arrays' layout checked at compile time on both architectures, a common buffer above 4 GB refused, the stale INF, allowlist and packager text gone; on Windows XP x64 SP2 under QEMU, the `qemu` flavour installed from clean with Windows' signature and Logo prompts only and no restart, the mouse moving the pointer, storage `fc /b` clean, disable, enable, uninstall and rescan, the Advanced and Power tabs, `XHCISNAP -probe` through WOW64, and a clean shutdown; the `release` package the same, its gates passed. Audio there showed Code 10 on the unanswered `USB_BUS_INTERFACE_USBDI` query until 28-A.1, and bound and played in real time with 28-A.1 on branch `p28-a1` (`fe8480b`, not in Phase 27). The other guests' amd64 legs are 28-V.1's
- [ ] 28-V.1 the seven other guests: ME, XP SP3, XP x64, Vista x86 and x64, 7 x86 and x64, each the same clauses as 26-V and 27-V, Vista and 7 at four virtual processors; Vista x64 and Windows 7 x64 on an F8 boot, as before, and XP x64 with no such need. With the three Windows 98 SE and Windows 2000 legs of Phases 26 and 27 these are the ten install legs every cut reads
- [ ] 28-E.1 the bench: the E460 on Windows 98 SE and on 32-bit Windows 7, and the second Windows 98 SE machine, through `run-13e.md`'s stage list - install, HID, storage, Ethernet, audio played and heard on a root port and behind a hub (Phase 27's playback clause, which only the bench can take), five hot-plugs, the controller disable that hung Windows 7 under the miniport - and Phase 27's High-Speed hub clauses, which only the bench can take: the hub rig at positions H1 to H4 with the single-TT and multi-TT units of `test-equipment.md`, Low and Full Speed devices behind each, the Full-Speed hub clause if a specimen is held by then and otherwise recorded as untested ground as Phase 13 recorded it; and Phase 27's Low-Speed mouse clause, moved here by the owner's decision of 2026-10-04 since no QEMU model is Low Speed - a Low-Speed mouse at `bInterval` 10 polled every 8 ms on a root port and behind a hub, on the host vectors of `test_hub`'s `test_low_speed_mouse` until then; read in the combined bench session before the `2.0.0.0` cut (owner, 2026-10-03; decisions table)
- [ ] 28.3 every known limitation of `1.2.0.0` re-measured under the HCD and written down as gone, carried or new: the NUSB stop crash (usbport's, so expected gone), the idle that never sleeps (now the bus's own power policy, so a decision, which also decides whether the bus suspends hub ports: Phase 27 handles a suspended or resumed hub port and initiates no suspend, by the owner's decision of 2026-10-04), the Windows 98 churn wedge, the Windows 7 disable hang

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

Status: not opened. Waits on Phase 28's A and V work; its E.1 clause waits for the combined bench session (decisions table).

Why a phase: this is the reason the successor exists. The port class the
miniport left unpowered is powered for the first time, the link has a state
machine of its own, and enumeration changes shape at the first packet.

- [ ] 29-0 the specification transcription into `xhci-data-structures.md`: the SuperSpeed PSIV default and Slot Context speed, `PORTSC` `WPR`, `WRC` and `CEC`, the `PLS` encodings, the USB 3 port state machine of Figure 4-27 with the Disabled state's exit and the hot-to-warm conversion, `CCS` in Table 5-27, Endpoint Context Max Burst, Mult and Max ESIT Payload from the companion descriptor, TD Size against burst, Table 6-12's SuperSpeed row; and the USB 3.2 specification's BOS, SuperSpeed Device Capability and Endpoint Companion descriptors, its link states and its SS.Disabled rule for an upstream port that has fallen back; and for SuperSpeedPlus (decisions table): the Supported Protocol Capability's minor revisions and PSI dwords (PSIV, PSIE, PSIM, PLT, PFD and LP) as section 7.2.1 defines them, and the negotiated lane counts, which are not in the PSI dword but in `PORTLI`'s RLC and TLC fields (section 5.4.10.1, each encoded as the count minus one) - a PSI rate alone cannot tell Gen 2x1 from Gen 1x2; the Endpoint Context's Max ESIT Payload Hi and the capability that governs it, `HCCPARAMS2.LEC` (Table 6-8, section 6.2.3.8); the SuperSpeedPlus Device Capability and SuperSpeedPlus Isochronous Endpoint Companion descriptors; and which P14s Gen 1 connectors reach which controller - whether its Gen 2 USB-C ports are the chipset controller's ports 13-18 or the Thunderbolt controller's own xHCI, and whether that one is present to Windows 98 SE at all
- [ ] 29-A.1 port classification: USB 3.x protocol ports managed and powered, the companion-pairing convention carried into the bus's port objects, the all-SuperSpeed controller accepted; and the rate kept distinct from the speed class, since `xhci_caps.c` today folds every protocol speed id at or above 5 Gbit/s into one SuperSpeed class: the port's rate is read from the PSI dwords of its protocol capability (the default table only when PSIC is 0, as `qemu-xhci` reports), and a link that trains above 5 Gbit/s is SuperSpeedPlus, accepted at its trained rate and counted by rate (decisions table; 29-A.6). One registry value, off by default, restores the refusal this task first carried - such a link sent back to USB 2.0 by 29-A.5's mechanism at link training, before any slot is enabled or descriptor read, as an unidentified hold that lasts until the controller's next start - as the user's way out of SuperSpeedPlus ground no vehicle has read. Host vectors for Gen 1, for each SuperSpeedPlus rate from PSI dwords (Gen 2, Gen 1x2, Gen 2x2), for a PSI table that lists no SuperSpeedPlus rate, and for the value set
- [ ] 29-A.2 the link state machine: Polling to U0, warm reset, U3 and resume, SS.Inactive and Compliance recovery, the automatic hot-or-warm reset policy and its reading
- [ ] 29-A.3 SuperSpeed enumeration: EP0 at 512 bytes, BOS and the companion descriptors, Slot and Endpoint Contexts with burst and ESIT, bandwidth as the xHC's own Configure Endpoint verdict surfaced as a URB status rather than modelled
- [ ] 29-A.4 transfers at SuperSpeed: bulk, interrupt and isochronous TDs with burst, 1024-byte endpoints, the TD Size rule. Bulk is the path this phase observes. SuperSpeed interrupt is first observed in Phase 30, on the SuperSpeed hub's status-change pipe. SuperSpeed isochronous has no vehicle: QEMU models none and no SuperSpeed isochronous device is held, so it is built from the specification against host vectors, carried as untested ground in the release notes until a specimen is held, and is not a clause of any checkpoint
- [ ] 29-A.5 fallback, in both directions: a SuperSpeed link that fails to train leaves the device to appear on its USB 2.0 companion port, as it does today, with a counter saying so; and the active case, sending a device whose link has trained back to USB 2.0 - needed by 31-A.3's UAS-only device on a controller that does not stream, and by 29-A.1's value that refuses SuperSpeedPlus - which means writing PED on the trained SuperSpeed port so its link goes to SS.Disabled and its terminations are withdrawn, tearing down the slot, and holding the port disabled for as long as the device may be on the USB 2.0 side, since every bus reset the companion port takes during enumeration and error recovery makes the device look for the SuperSpeed terminations again and a restored port would ping-pong it between the two buses. The two release rules are the ones `future-plans/superspeed-storage-behind-a-switch.md` section 6.1 derived, carried over unchanged: a companion-paired port's hold is released on the companion port's disconnect only if that companion reported a connect after the hold began **and the device that enumerated there is the held device** - the same vendor id, product id and serial string as the descriptor read on the SuperSpeed port before the hold; vendor and product id alone release nothing, since two units of one model with no serial cannot be told apart, so a device with no serial string, a hold taken before any descriptor was read (29-A.1's SuperSpeedPlus refusal under its value, decided from the port's speed id at link training before any slot exists) and a hold whose identity read failed are all unidentified holds that last until the controller's next start like an orphan's, counted apart - because a connect alone proves nothing: the pairing is a convention, and an unrelated device plugged into a mis-paired companion would otherwise release the wrong hold and restart the ping-pong (this tightens the proposal's rule, which took the connect alone as evidence; the proposal's section 6.1 now carries a note saying so). A companion connect of some other device leaves the hold in place and is counted. The release re-arms the SuperSpeed port with a `PORTSC` write of `PLS` = RxDetect with `LWS`, the Disabled state's exit to Disconnected, never a warm reset (which does not act on a Disabled port) and never a power cycle; an orphan port, or a paired port whose companion never connected, holds until the controller's next start, because a Disabled port has withdrawn the very terminations it would detect a disconnect with and a timed re-arm cannot tell a device that left from one still on its way. Whether the Disabled state raises `CSC` on a physical disconnect is a 29-0 transcription from Figure 4-27, and the orphan rule is written on the pessimistic reading. Counters for the paired, unpaired and orphan holds, and host vectors for each case including a companion disconnect with no prior connect, an unrelated device's connect and disconnect on a mis-paired companion, and a second unit of the same model with no serial on the companion, each of which must release nothing
- [ ] 29-A.6 SuperSpeedPlus (decisions table): enumeration and transfers at Gen 2, Gen 1x2 and Gen 2x2 over 29-A.3 and 29-A.4, unchanged for bulk and interrupt; the SuperSpeedPlus Isochronous Endpoint Companion read when the SuperSpeed companion says one follows; its 32-bit bytes-per-interval goes into Max ESIT Payload Lo and Hi, which hold 24 bits between them, and only on a controller with `HCCPARAMS2.LEC` set - there `Mult` is reserved and written zero; with `LEC` clear, Hi is reserved, a payload above 48 KiB per interval cannot be described, and the endpoint is refused at select-interface with a counter rather than truncated, while one at or below it is programmed the Gen 1 way - with `Mult` copied from the SuperSpeed companion only when no SuperSpeedPlus companion follows, since the USB 3.2 specification has the SuperSpeed companion's `Mult` ignored when one does (Table 9-28); then the burst count is derived from the SuperSpeedPlus companion's bytes-per-interval over `wMaxPacketSize` and `bMaxBurst`, and an endpoint whose count the legacy `Mult` field cannot hold is refused the same way; the BOS SuperSpeedPlus capability read and kept for `XHCISNAP`; `XHCISNAP` decoding the port's mode - rate from its PSI dword, lanes from `PORTLI`. Built from the specification against host vectors - a Gen 2x1 and a Gen 1x2 bulk device (one aggregate rate, two modes), a Gen 2x2 bulk device, a SuperSpeedPlus isochronous endpoint at 48 KiB and just above it under `LEC` clear and under `LEC` set, one under `LEC` clear whose SuperSpeed companion says `Mult` 0 while its SuperSpeedPlus companion needs two bursts, a payload past the 24-bit field, and a malformed companion - since QEMU models no such link; untested ground in the release notes for every mode, rate and lane count, no bench reading takes
- [ ] 29-V.1 QEMU's `qemu-xhci` with `usb-storage` at SuperSpeed on both primaries: enumerated, bound to each target's `usbstor.sys`, a verified file round trip; the USB 2.0 rows unchanged
- [ ] 29-E.1 the bench: the two SuperSpeed drives of `test-equipment.md` at rig position D on Windows 98 SE and 32-bit Windows 7, round trips, throughput against the same unit behind a USB 2.0 hub, warm reset and SS.Inactive recorded if they occur; read in the combined bench session before the `2.0.0.0` cut (owner, 2026-10-03; decisions table)
- [ ] 29-E.2 a reading, not a clause: a Gen 2 device on a P14s Gen 1 connector that 29-0 finds reaching 10 Gbit/s under this driver on Windows 98 SE, its trained rate from `XHCISNAP` and a round trip with throughput, if such a connector and a Gen 2 device are both held; otherwise recorded as not taken

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
SuperSpeed port, UAS, a SuperSpeed interrupt or isochronous transfer
(29-A.4), or a SuperSpeedPlus link (29-A.6's host vectors and 29-E.2's
reading carry it).

Records: `design/13-superspeed-hcd.md`; `usb-xhci-info/xhci-data-structures.md`;
`runs/run-29.md`; `test-equipment.md`, "What the SuperSpeed halves are for".

## Phase 30 - SuperSpeed Hubs

Goal: a USB 3.x hub's SuperSpeed half driven as a hub of the bus, with
SuperSpeed devices behind it, while its High-Speed half carries USB 2.0
devices as a Phase 27 hub.

Status: not opened. Waits on Phase 29's A and V work; its E.1 clause waits for the combined bench session (decisions table).

Why a phase: a USB 3 hub is two hubs on two ports with their own descriptor,
their own port status format and their own depth and link-state requests, and
no QEMU device models one, so the readings are bench readings.

- [ ] 30-0 the USB 3 hub class: the hub descriptor, `SET_HUB_DEPTH`, the port status and change bits at SuperSpeed, the link-state and warm-reset port features, remote-wake masks, and for a SuperSpeedPlus hub the extended port status (`GET_PORT_STATUS` with the extended type) that names a downstream port's rate and lane count; transcribed from the USB 3.2 specification
- [ ] 30-A.1 the SuperSpeed hub inside the bus: enumeration at depth with the Route String of design record 02, port reset through a hub, the two halves of one unit as two independent hubs with no pairing beyond a counter; a SuperSpeedPlus hub accepted at its trained rate with its downstream rates read from the extended port status, on 29-A.6's rules and host vectors
- [ ] 30-A.2 host suites: the hub state machines at SuperSpeed over the vectors of 27-A.4
- [ ] 30-E.1 the bench unit (`05E3:0610` and `05E3:0612`): a SuperSpeed drive behind the SuperSpeed half and a High-Speed device behind the other, on Windows 98 SE and 32-bit Windows 7, plugged, unplugged and re-plugged; read in the combined bench session before the `2.0.0.0` cut (owner, 2026-10-03; decisions table)

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

Status: not opened. Waits on Phase 30's A and V work; its E.1 clause waits for the combined bench session (decisions table).

Why a phase: streams are a transfer model the bus has never had, UAS is a
class driver with a storage-stack contract of its own on each target, and
which driver a UAS-capable device gets is a bus-side id decision; the three
are one slice.

- [ ] 31-0 static: Stream Context Arrays and stream ids in TRBs and events (xHCI section 4.12); the UAS class specification and T10's UAS, in both its shapes - streamed at SuperSpeed, and streamless at High Speed where the status pipe carries READ READY and WRITE READY information units and the data pipes are shared; how `usbstor.sys` presents SCSI PDOs to `disk.sys` and `classpnp` on each target and, on Windows 98 SE, how NUSB's USB mapping port driver gives them drive letters (`usbntmap.inf` binds `USBSTOR\GenDisk` and its siblings to `*IOS` with `USBMPHLP.PDR`; the decisions table and the provenance row), since the UAS driver's objects must take the same route; whether the 98 SE CD's `layout.inf` carries any of the mapping files, read from the disc; and what QEMU's `usb-uas` model presents, read from `hw/usb/dev-uas.c` - the expectation is UAS only, with no Bulk-Only alternate setting at either speed, which decides what 31-V.1 can and cannot show
- [ ] 31-A.1 primary streams in the bus: a private open-streams request on a pipe handle, stream rings, per-stream enqueue and event matching, abort of a stream, stream close
- [ ] 31-A.2 the UAS class driver, its own binary and INF in the package: SCSI PDOs, the command, status, data-in and data-out pipes, tags and queue depth, sense, task management and reset recovery, the SRB set `classpnp` sends; both transports, streamed at SuperSpeed over 31-A.1 and streamless at High Speed with the READ READY and WRITE READY handling, chosen by the enumerated speed
- [ ] 31-A.3 the id policy: the bus chooses the transport and exposes only that transport's class compatible ids - `Class_08&SubClass_06&Prot_62` and never `Prot_50` when it chooses UAS, the reverse when it chooses Bulk-Only - because id order alone decides nothing: the NT setup engines rank a signed inbox `usbstor.inf` above an unsigned match, and `usbstor.inf` carries vendor-id entries that match the hardware id regardless. UAS is chosen when the device offers the setting at the speed it enumerated at (at SuperSpeed only when the controller streams, at High Speed whenever the setting is there) and the registry value does not force Bulk-Only. One combination has no transport at all: a UAS-only device at SuperSpeed on a controller that does not stream (`MaxPSASize` 0 in `HCCPARAMS1`), since SuperSpeed UAS needs streams and the device offers no Bulk-Only; on a root port it is sent back to USB 2.0 by 29-A.5's mechanism, where it runs streamless UAS at High Speed - as an identified hold only when 29-A.5's identity read succeeded with a serial string, and otherwise as an unidentified hold until the controller's next start, on 29-A.5's rules unchanged - and on a root port with no USB 2.0 companion it is refused with no storage id exposed at all, never a Bulk-Only id it cannot honour. Behind a SuperSpeed hub there is no send-back: 29-A.5's mechanism is the controller's own `PORTSC` and companion tracking, and a USB 3 hub's two halves are unpaired (30-A.1), so the device is refused in place with no storage id exposed, its hub port left as the hub reported it, and its siblings untouched. Every branch is counted and stated in the release notes, with host vectors for the root-port send-back with and without a serial, a failed identity read, the root port with no companion, and the downstream refusal with a sibling device on the same hub still working; the value applies only to a device that also offers a Bulk-Only setting, and a UAS-only device stays UAS under it, with a counter saying so. A device whose vendor id `usbstor.inf` names by hand still goes to `usbstor.sys` on a hardware-id match, which is recorded as the residual case rather than fought. Switching: a change of the value takes effect at the next enumeration, and on NT a devnode whose compatible ids changed keeps its installed service until it is uninstalled in Device Manager and re-plugged, which the release notes state as the procedure; the device matrix gains an expected and an observed transport field per storage row, `UAS` or `Bulk-Only`, with design record 06's verdicts unchanged - a wrong transport, a failed round trip or a failed teardown is a `FAIL` on the existing rules, and the transport is never a verdict of its own
- [ ] 31-V.1 QEMU's `usb-uas` model on both primaries, at SuperSpeed and at High Speed: enumerated, bound to the UAS driver, a verified round trip in each mode, on a fresh install and again after an uninstall and re-plug. The forced-Bulk-Only leg cannot be shown on this model if 31-0 confirms it has no Bulk-Only setting; it is 31-V.2's and 31-E.1's
- [ ] 31-V.2 the Windows 2000 half of the dual-transport clauses, which has no bench: the ASMedia bridge passed through to the Windows 2000 guest with `usb-host` at High Speed, where it offers both transports - UAS chosen on a fresh install, the forced-Bulk-Only value selecting Bulk-Only, and the switch between them through uninstall and re-plug; the same leg on the Windows 98 SE guest under NUSB as a VM control for 31-E.1
- [ ] 31-E.1 the bench, on Windows 98 SE and 32-bit Windows 7: the ASMedia bridge under UAS at SuperSpeed and, behind a USB 2.0 hub, under UAS at High Speed, since it offers both transports at both speeds (`test-equipment.md`); the MSSU10 drive under UAS at SuperSpeed, and behind a USB 2.0 hub under Bulk-Only, which is all it offers there and is the expected result rather than a failure; the forced-Bulk-Only value on both units at SuperSpeed; round trips and throughput against Bulk-Only in each case; read in the combined bench session before the `2.0.0.0` cut (owner, 2026-10-03; decisions table)

Checkpoint: `usb-uas` round-tripping under the UAS driver on both primary
targets at SuperSpeed and at High Speed, each mode its own clause, on a
fresh install and after an uninstall and re-plug; the ASMedia bridge under
UAS at both speeds and the MSSU10 at SuperSpeed on the bench on Windows 98
SE; the forced-Bulk-Only value selecting Bulk-Only on a dual-transport unit
on both primary targets - the bench for Windows 98 SE, the passed-through
bridge of 31-V.2 for Windows 2000 - and leaving a UAS-only device on UAS in
QEMU on both; the non-streaming controller's branch rests on 31-A.3's host
vectors, since QEMU's controller streams and no non-streaming one is held. On Windows 98 SE every clause is taken with NUSB
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

Status: not opened. Waits on Phase 31. The cut also waits on the combined
bench session that reads 28-E.1, 29-E.1, 30-E.1 and 31-E.1 (owner,
2026-10-03; decisions table).

Why a phase: a cut changes every user-facing statement, and this one changes
what the download is.

- [ ] 32.1 the record: `release-notes.md` rewritten - what the HCD is and is not, every target's standing, the F8 requirement on Vista x64 and Windows 7 x64, the stock Windows 98 SE statement, the miniport's virtual-hub values having no effect (decisions table), the limitations of 28.3, the untested ground that is agreed today (29-A.4's SuperSpeed isochronous path; SuperSpeedPlus in every mode 29-E.2 did not read, named by rate and lane count, since a Gen 2x1 reading at 10 Gbit/s leaves Gen 1x2 at the same rate untested, and Gen 2x2 certainly; and its isochronous path, 29-A.6) and whatever Phase 30's revisited decision adds, if anything; the readme template; `releases/history.md`; `README.md` and `AGENTS.md` describing two drivers, one frozen
- [ ] 32.2 the acceptance test and the post-release run of design record 09 adapted to the HCD, including a UAS row and a SuperSpeed row
- [ ] 32.3 the cut: `xhci_version.h`'s successor, both INFs' `DriverVer`, `make-release.ps1` publishing `2.0.0.0` in four directories beside the untouched `releases\1.2.0.0`, the ten install legs read from the asset - and each leg now carries what Phases 29 and 31 read only on the primaries: QEMU's `usb-storage` at SuperSpeed bound to that target's `usbstor.sys` with a round trip, and `usb-uas` bound to the UAS driver at SuperSpeed and, as a separate clause, at High Speed, each with a round trip and a clean teardown, on every guest, the three x64 guests on the amd64 UAS binary, which has no other runtime reading

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
- **The SuperSpeed storage proposal behind a switch**
  (`future-plans/superspeed-storage-behind-a-switch.md`). It was the way to
  SuperSpeed storage without leaving the miniport; with the miniport frozen it
  stays a record of what that would have taken.
