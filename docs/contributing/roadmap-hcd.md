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

**Status: `2.0.0.0` is released.** Phases 25 to 32 are closed. The cut was
taken on 2026-10-04 and the owner squash-merged it to `main` as `4038447`
("2.0.0.0 (#13)"); the owner benched it after the cut and reports no issue
beyond the known limitations (Phase 28, 28-E.1). What comes next is Phase
33, the `2.1.0.0` update, and "What is not on this roadmap" below.

**The miniport is frozen.** `1.2.0.0` is the last release of the miniport
(owner, 2026-10-02): no further miniport cut, and a defect reported against
it is answered by the successor rather than by a `1.2.x.0`. `2.0.0.0`
replaced it as the shipping download, and its two roadmap files are closed
records. **And it has left the tree** (owner, 2026-10-02, later
the same day): `src\` is the successor's, which took the name `xhci98.sys`;
the miniport's last sources are on branch `1.2.0.0` and its binaries in
`releases\1.2.0.0`, and nothing here builds it.

## The decisions this roadmap rests on

Taken by the owner from 2026-10-02, when the phases below were drawn up, to
the `2.0.0.0` cut on 2026-10-04. Each is restated where it binds a task; this
table is the index.

| Decision | Taken |
|---|---|
| Pure HCD, not a registry switch | The successor is a second binary in a second package, chosen at install time through Update Driver against the same PCI class id. The miniport imports `USBPORT_RegisterUSBPortDriver`, a load-time gate, so one binary could never both serve a stock Windows 98 SE and remain the miniport; and a switch would owe every HCD change a miniport regression matrix under the "switch off is byte-identical" rule |
| Binary and package name | `xhci98.sys`, `xhci98.inf` and `xhci98-amd64.inf`, package `xhci98-<version>.zip`, built from `src\` (owner, 2026-10-02, the third decision of the day on it: `xhci98h`, then `xhci98hc` coexisting on disk with the miniport, then - once task 25.8 had built the scaffold as `xhci98hc.sys` in `src\hcd\` - the miniport's removal from the tree and this name). A `2.0.0.0` install replaces the miniport's file; going back is a reinstall of the `1.2.0.0` package |
| Version and cut point | One cut, `2.0.0.0`, after Phase 31: USB 2.0 parity on every target, SuperSpeed on root ports and behind SuperSpeed hubs, and UAS. No intermediate release |
| The miniport | Frozen at `1.2.0.0`, no further cuts, and removed from the tree (above) |
| Hub class and composite splitting | Inside the bus driver. External hubs are objects of the bus, not PDOs; a composite device is split by the bus into per-function PDOs. No hub-to-port contract to design, no INF binding path for an external hub or a composite parent (the root hub's own project-owned binding, in the root-hub row below, is the one hub binding the package carries), no per-target matching of `usbccgp.sys` or Windows 98's composite parent. From `2.1.0.0` an external hub also gets a devnode of its own, as every Microsoft stack shows its hubs (owner, 2026-10-04; task 33.4): the bus still runs the hub, and only its PnP presentation changes - each hub it serves is presented under a project-owned id (`XHCI98\HUB`, `XHCI98\HUB30` for a USB 3 hub's SuperSpeed half, so a USB 3 hub is two devnodes), bound by this package's own INFs to `xhci98.sys` as a hub FDO, with the devices behind it as its children; still no binding path for any OS hub driver (design record 13 section 10.11) |
| Stock Windows 98 SE | A goal: the HCD loads, starts and binds devices on a Windows 98 SE install with no USB 2.0 stack. NUSB's and SweetLow's host-stack halves become inert under it. What a stock install still lacks is a mass-storage class driver, which NUSB's `usbstor.sys` and its `ntmap` layer supply and Windows ME ships; the HCD does not replace that. Phase 26 takes the reading (26-V.3), a checkpoint clause by the owner's decision of 2026-10-03 |
| Mass storage on Windows 98 SE | NUSB's, kept (owner, 2026-10-02, choosing this over an own storage driver). A drive letter on 9x comes from the IOS layer, and NUSB's `usbntmap.inf` binds `usbstor.sys`'s disk objects (`USBSTOR\GenDisk` and its siblings) to `DevLoader=*IOS` with `PortDriver=USBMPHLP.PDR`, the USB mapping port driver, with `NTMAP.SYS` and `USBNTMAP.SYS` beside it; NUSB's own install INF stamps `NTMAP.SYS` as `4.10.0.2227` and annotates it with hotfixes 242975 and 267304, a Windows 98 SE line, and annotates `USBMPHLP.PDR` and `USBNTMAP.SYS` as `WinMe` in its file list - the package author's notes, not a reading of the files, which were not examined (`legal-provenance.md` section 4, the NUSB 3.6 INF row; method static, text read, nothing executed). They are Microsoft's and this project does not redistribute them. An own BOT driver would still sit under that mapping, and an own IOS port driver is a VxD project with no use elsewhere. So `usbstor.sys` stays the BOT driver on every target, and on Windows 98 SE mass storage, UAS included, needs NUSB installed; on a stock install it cannot work and the release notes say so. Whether the 98 SE CD carries any of the mapping files is task 31-0's to read from the disc |
| Pool and DMA | The HCD allocates pool and its own common buffer. The miniport's "allocate no pool" rule stands for `src/`; for the HCD it is replaced by import-allowlist rows with Windows 98 export evidence, read in task 25.3, and a rule naming the sites that may allocate |
| Device names | `xHCI98 USB 3.x eXtensible Host Controller` for the controller and `xHCI98 USB 3.x Root Hub` for the root hub (owner, 2026-10-02; spelled xHCI98 by the owner 2026-10-03), as the INFs' device descriptions on every path |
| The root hub and both property tabs | Kept, all of it (owner, 2026-10-02, reversing a narrower answer given on the Codex review earlier that day). The controller FDO creates a root-hub PDO under a **project-owned** hardware id, never `USB\ROOT_HUB` - which the OS's own `usbhub.sys` claims on every NT target and which would put Microsoft's hub driver back on top of this bus - and `xhci98.inf` binds that id to `xhci98.sys` itself, so one binary is both the controller's function driver and the root hub's, and every device PDO is a child of the root hub, where Device Manager users expect it. The controller's Advanced tab: the HCD answers the `USBUSER` request set `usbui.dll` sends to the controller devnode, and the INFs carry the two registrations the miniport's do - `EnumPropPages` to `sysclass.dll` on Windows 98, `EnumPropPages32` to `usbui.dll` on the NT paths. The root hub's Power tab: the root-hub devnode registers the hub property-page provider the OS INFs register for their own root hub, and answers the hub IOCTLs that page sends, read per target in task 25.4. `XHCISNAP` reaches the HCD through the controller's door. External hubs stay objects inside the bus (the hub decision above), so a device behind one appears under the root hub devnode, and the Power tab reports the budget the bus itself keeps. **Amended 2026-10-04 (task 33.4)**: each external hub is its own devnode with its own Power tab and door, and a device behind it appears under it (design record 13 section 10.11) |
| The miniport's virtual-hub values | Not supported (owner, 2026-10-02). `XhciVirtualHSHub`, `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid` exist to make usbport tell the truth about a root-port device's speed; the HCD has no usbport to lie to and reports every device at its true speed with no hub in the way, so there is nothing for them to switch. The HCD reads none of the three, its INFs write none, a value left behind by a miniport install has no effect, and the release notes say so (32.1). Which of the miniport's other values carry over - the log switches, the interrupt moderation value - is 25.1's list |
| SuperSpeed hubs on Windows 2000 | A vehicle is required, for now (owner, 2026-10-02, on the same review). Windows 2000 is a VM-only target and no QEMU device models a SuperSpeed hub, so Phase 30's "observed on both" has no vehicle today; the owner did not record an exception and revisits the question when Phase 30 opens. Until then Phase 30 cannot close, and Phases 31 and 32 wait on it. Revisited on 2026-10-04 ("SuperSpeed hubs on Windows 2000, revisited", below) |
| SuperSpeedPlus: USB 3.1 Gen 2 and USB 3.2 Gen 1x2 and Gen 2x2, to 20 Gbit/s | In `2.0.0.0`, untested if need be (owner, 2026-10-02, later the same day, reversing the line in "What is not on this roadmap" that left it unassessed): the driver runs on every target to Windows 7, which has no USB 3 stack of its own, and newer chipsets with 10 and 20 Gbit/s ports often have no Windows 7 vendor driver either. A link that trains above 5 Gbit/s is accepted at its trained rate rather than sent back to USB 2.0. From the driver's side the step is small once Phase 29's Gen 1 path exists: the rate comes from the protocol capability's PSI dwords rather than a fixed table, bulk and interrupt framing are Gen 1's (1024-byte packets, burst to 16), and the new work is the SuperSpeedPlus isochronous companion with its 32-bit bytes-per-interval and the Endpoint Context's Max ESIT Payload Hi (task 29-A.6). Vehicles: QEMU models nothing above 5 Gbit/s; the P14s Gen 1's chipset controller (`8086:02ED`) advertises a USB 3.1 protocol with eight PSI dwords on ports 13-18 (`xhciqual/results/p14s-gen1-2026-07-25/PROBE.LOG`), but its published 10 Gbit/s connectors are the two USB-C 3.1 Gen 2 / Thunderbolt 3 ports, and neither source says which controller those reach - the chipset's ports or the Thunderbolt controller's own xHCI - so whether any P14s connector reaches 10 Gbit/s under this driver is 29-0's question and 29-E.2's reading; no 20 Gbit/s port is held (the B650M, whose chipset has one, is gone). What no vehicle reads is untested ground in the release notes (32.1) |
| USB Audio 2.0 | Not in this repository and not on this roadmap. A class driver of generic design, so that it serves machines on EHCI and vendor stacks as well as this one, built in its own repository; it needs nothing from xHCI, since UAC 2.0 runs at High Speed and binds by class through the standard URB contract |
| One bench session before the cut | **Superseded** on 2026-10-04 by "Cut before the bench" (below). All bench work moves to one combined bench session immediately before the `2.0.0.0` cut (owner, 2026-10-03). Phases 28, 29, 30 and 31 each close their V clauses and A tasks and stay open on their E.1 clause alone; work proceeds into the next phase while the earlier E.1 clauses wait - an explicit exception to the rule that no phase advances past an unobserved checkpoint, limited to the E.1 clauses. The session reads 28-E.1, 29-E.1, 30-E.1 and 31-E.1; each such phase closes only when its clause passes, and the cut (Phase 32) waits on the session. Supersedes the owner's instruction of 2026-10-02 to continue into Phase 29 while 28-E.1 waits. Accepted costs: Phase 30's SuperSpeed hub work, which no QEMU device models, is first executed on the bench; QEMU's SuperSpeed model is Phase 29's only runtime check until then; real-controller defects surface late and together. Mitigations in VMs: a strict mode in the `qemu` flavour checking xHCI slot and endpoint state preconditions before every command, real devices by `usb-host` passthrough, the SMP Windows 2000 guest with Driver Verifier, and Codex reviews aimed at specification preconditions |
| Interrupt moderation default | 160 (40 us, Linux's long-standing value), **INF only** (owner, 2026-10-04): both INFs write 160 on every HCD install path, where they wrote 500 (125 us), the miniport's value from `1.1.1.0`; the code default `XHCI_IMOD_INTERVAL_DEFAULT` stays 4000 (1 ms), what an absent, unreadable or out-of-range value runs at, and the INF gate holds both files to 160 (`VAL-DEFAULT`). Evidence, the owner's ATTO runs on the P14s Gen 1 under Windows 98 SE (the MSSU10 over UAS at SuperSpeed, queue depth 1): at 8 MB, 181/181 MB/s write/read at 500, 211/221 at 160, 217/225 at 40. A Full-Speed audio stream was read again at 160 on a Windows 2000 guest before the change. `runs/run-28.md`, "The interrupt moderation default: 160" |
| The idle power policy (28.3) | Carried as a known limitation of `2.0.0.0` (owner, 2026-10-04): the bus never initiates selective suspend, of a device or of a hub port; it handles a suspend or resume it is asked for, or that a hub reports, the same "handle, don't initiate" ruling 27-A.1 took. A later phase may add selective suspend |
| The other `1.2.0.0` limitations (28.3) | The NUSB stop crash, the Windows 98 churn wedge and the Windows 7 disable hang are each re-measured under the HCD and recorded as gone, carried or new (owner, 2026-10-04). A survivor is carried in the release notes and does not block the cut |
| 28-E.1's Full-Speed hub clause | Taken at the bench, not recorded as untested ground (owner, 2026-10-04): a USB 2.0 hub forced to Full Speed by an ADuM full/low-speed isolator in front of it, in place of a USB 1.1 hub (owner, the same day, superseding the plan to buy one) |
| Checkpoints of Phases 28 to 31 | Pre-approved (owner, 2026-10-04): each closes once its Codex review is clean, the x86 and amd64 builds, gates and host tests pass, every roadmap VM leg passes with its package hash recorded, and no regression is open on an earlier phase. Scope rulings, waivers, push, tag and the cut stay the owner's |
| The run plan for Phases 28 to 31 | Every finished line is merged into the integration branch `p28-31-int` first, and the Phase 28 to 31 VM legs - 28-V.1's seven other guests, Vista x86 and x64 included, 29-V.1, 31-V.1 and 31-V.2 - are read once, on that merged build (owner, 2026-10-04). The early 28-V.1 pre-read on `1ed1ba6` was paused for this. Every leg runs on development host A; there is no second host: a development host B, planned that morning to take the Vista x86 and x64 legs, was dropped by the owner the same morning (about 10:30), and those legs ran on host A |
| `xhciuas` on Windows 98 SE | Its INF names NUSB's `USBNTMAP.SYS` as an upper filter, by name, and never ships it, so UAS on Windows 98 SE depends on NUSB's mass-storage component as Bulk-Only does (owner, 2026-10-04; the mass-storage row above). The INF has no temp-file copy, so the first UAS install needs no restart |
| Zero-size isochronous endpoints | An isochronous endpoint with `wMaxPacketSize` 0 and `bMaxBurst` 0 is accepted as zero-bandwidth, not refused (owner, 2026-10-04); the implementation is on branch `p29-spec` (`4cef6d3`: configured with MPS 0), merged into `p28-31-int` at `9077edf` |
| The P14s Gen 1 connector question (29-0) | Deferred to the combined bench session, and not a clause (owner, 2026-10-04): it serves only 29-E.2, the optional Gen 2 reading. If a Gen 2 device is at hand at the bench, the connector is read there with `XHCISNAP`; otherwise 29-E.2 is recorded as not taken |
| SuperSpeedPlus parent-hub fields | Implemented in `2.0.0.0`, not deferred (owner, 2026-10-04, about 12:15): a SuperSpeed device on a lower-rank link behind a SuperSpeedPlus hub gets the Slot Context's Parent Hub Slot ID and Parent Port Number, the hub's link rank tracked from `PORTLI` and the extended port status. On branch `p29-spec`, merged into `p28-31-int` at `9077edf`; built from the specification, no vehicle models such a hub |
| A departed PDO and its port (option 1) | A gone device PDO holds its port only until a relations answer has reported it missing; the port then enumerates a new device with a new PDO under the same location instance id, while the old PDO waits for its REMOVE or the controller's release, as usbport's children do (owner, 2026-10-04, about 11:40, to be taken if the Windows ME pull-during-install reading showed the REMOVE lost on a healthy guest, which it did). `7f1b4e0`, with `791f9f8` (a START on a departed PDO refused; a command failing on a departed device no controller reset) and `cc9da33` (a PDO removed while listed released at the relations answer); `runs/run-28.md`, "The device-pull fixes" |
| Device PDOs and `SurpriseRemovalOK` | FALSE, as Windows 2000's `usbhub.sys` reports, so the hot-plug applet lists a USB stick again (owner, 2026-10-04, about 14:40); a split function of a composite device keeps TRUE. `9001ebd`, `8993884` (Codex); found by the upgrade leg on Windows 2000 (`runs/run-28.md`, "The upgrade from `1.2.0.0`") |
| Windows 7 on the bench | Not benched (owner, 2026-10-04, about 14:05: "We won't bench Win 7"). The E460's 32-bit Windows 7 halves of 28-E.1, 29-E.1, 30-E.1 and 31-E.1 are dropped; Windows 7, both architectures, is a virtual-machine target only for `2.0.0.0` |
| The Windows 7 disable hang (28.3) | Removed from the `2.0.0.0` limitations, recorded as gone with no metal caveat (owner, 2026-10-04, about 14:10): a `1.2.0.0` miniport and usbport issue, not reproduced under the HCD in QEMU, five controller disable and enable cycles on Windows 7 x86 and x64 (`runs/run-28.md`, 28.3) |
| Windows ME, a UAS drive as the first storage device | Option C, carried as a limitation of `2.0.0.0` (owner, 2026-10-04, about 15:20): on a fresh ME install the drive shows Code 2 until ME has copied its own `USBNTMAP.SYS` and `USBMPHLP.PDR`, which it does when its first ordinary stick installs. An `xhciuas.inf` copy of the two files through `LayoutFile` cured ME but made Windows 98 SE prompt twice, since the 98 SE CD's `layout.inf` names neither file; it was parked on branch `p31-meuas-copy` (`dc5251f`, deleted after the cut by the owner's decision), and option B, an ME-only compatible id to gate it, is for a later release (`runs/run-31.md`, "Windows ME: a UAS drive first") |
| Windows ME: the controller re-enable, and a pull during an install | Carried as limitations of `2.0.0.0` (owner, 2026-10-04): re-enabling the controller with a device attached may hang ME, the HCD's own (ME's Microsoft UHCI stack re-enables with devices attached), so the release notes say to unplug first; the fix, first planned as `2.0.0.1`, is task 33.1 of `2.1.0.0` (`docs/issues/09-me-controller-reenable-stopped-pdos.md`). A pull while ME is installing the device freezes ME under its own UHCI stack too, so it is ME's behaviour, not the HCD's (`runs/run-28.md`) |
| Cut before the bench | The `2.0.0.0` cut was taken before the combined bench session, not after it (owner, 2026-10-04, about 16:40: "I will do the bench session after the cut; if it works we release that, otherwise change and recut"); supersedes "One bench session before the cut" (above). The documents were written as if the bench passes, and 28-E.1, 29-E.1, 30-E.1 and 31-E.1 were read by the owner on real hardware after the cut, on the ThinkPad E460 and the ThinkPad P14s Gen 1 under Windows 98 SE (the README's photos are the P14s); the owner reports no issue beyond the known limitations. Windows 7 was not benched ("Windows 7 on the bench", above) |
| SuperSpeed hubs on Windows 2000, revisited | Released untested (owner, 2026-10-04, with the cut), revisiting the 2026-10-02 row that required a vehicle: no vehicle exists, so the Windows 2000 half of Phase 30's checkpoint is untested ground, built from the specification, and the release notes say so |
| Gaps dropped from `2.0.0.0` | Two task gaps ticked with the gap recorded rather than closed (owner, 2026-10-04): 29-A.1's registry value restoring the SuperSpeedPlus refusal (SuperSpeedPlus is accepted at its trained rate; `HCD_HOLD_REASON_SSP_REFUSED` in `src\hcd.h` is unused), and 31-0's unread sources - the xHCI 4.12 streams rows marked "to verify", NT `disk.inf`'s `GenDisk` binding and QEMU's `hw/usb/dev-uas.c` - not needed for `2.0.0.0` since UAS passed on every target. Both are listed under "What is not on this roadmap" |

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
streams and UAS, and Phase 32 the `2.0.0.0` cut; Phase 33 is the first
update, `2.1.0.0`. The order is deliberate:
parity first, because round 12's device matrix and the acceptance test are a
free oracle for everything USB 2.0; SuperSpeed before hubs, because root-port
storage is what a user plugs in first; UAS last, because it needs streams and
a class driver of its own. One phase had no observation vehicle on Windows
2000: Phase 30, the SuperSpeed hubs, and the decisions table records what the
owner decided about that and its revisit at the cut.

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

Status: **complete** at this checkpoint, 2026-10-04, on the owner's pre-approval of the close; opened the same day and worked on the integration branch `p27-int` for the phase branch `phase-27`. The tasks and their records:
- 27-A.1, A.2 and A.4, and 28-A.2, which the owner moved here: done in parallel and merged.
- 27-A.3: done, with Codex rounds 1 and 2 on the batch.
- The Windows 98 SE HID-unplug stall: 27-V.1 found it and five Codex rounds closed it (`18ecc83`).
- 27-V.1: read on both primaries (`runs/run-27.md`).
- The closing legs (`runs/run-27.md`, "The closing legs"): Full-Speed audio bound at a root port and behind the hub on both primaries, and the orderly path with a hub subtree beneath it, on `981f56b`; the Windows 98 SE audio-unplug fatal 0E they found - a use after free of the device PDO after its REMOVE, from Phase 26's `27063e1` - fixed in `c038326` and read on Windows 98 SE and Windows 2000; the hub-port resume (`e0c7617`, `6f2e1ce`) on host vectors and its regression legs on `6f2e1ce`.

**Every checkpoint clause, and where it was read:**
- **The round 12 matrix at parity, every device at its true speed:**
  - Windows 2000, all 17 rows on `h2kb`, and the HID, storage and hub groups on three more builds: every row PASS, or NODRIVER where expected.
  - Windows 98 SE, all 17 rows on two builds: every row PASS, NODRIVER where expected, or EXCLUDED by the set; the hub group, the five-tier churn included, on the re-prepared image (`h98jh`).
  - Every row's port speed and Slot Context speed held.
  - "Its own interval" has no expectation in `matrix-hcd.psd1`. It rests on 26-A.5's interval taken from `bInterval` directly.
- **A Low-Speed mouse at `bInterval` 10 polled every 8 ms, behind the hub and on a root port:** moved to 28-E.1's bench by the owner's decision of 2026-10-04, since no QEMU model is Low Speed (`matrix-hcd.psd1`). Its host vectors are in `test_hub` (`test_low_speed_mouse`): the endpoint programmed from the device's own speed class on a root port and behind a Full-Speed hub whose port reports it Low Speed, to xHCI Interval 6, 8 ms, with a Low-Speed bulk endpoint refused to show the hub's Full Speed is not what it is given.
- **Full-Speed audio bound with its isochronous endpoints opened and the isochronous error counters at zero:**
  - On a root port: `usb-audio/fs` PASS on both primaries, with the iso error counters 0 on Windows 2000 and the traffic inert on Windows 98 SE, as the clause allows. Read again in the closing legs on `981f56b`: on Windows 2000 the AudioStreaming interface at alternate 0 and then 1, `endpoints opened` 1 to 3, `iso packets answered` 660, the iso missed-service and packet-error counters 0; on Windows 98 SE the function PDO bound at 01/01/00, alternate 0, the volume icon.
  - Behind the Full-Speed hub (the closing legs, `981f56b`): on Windows 2000 the same, `iso packets answered` +660 and the iso error counters 0, with one hub started, folded and marked and the audio device addressed and opened behind it; on Windows 98 SE installed and bound, then the pre-existing audio-load wedge below.
- **The churn soak:** clean.
  - Windows 98 SE: 120 of 120 hubs enumerated with the guest responsive (`soak-h98j`, `soak-h98f`), where the miniport wedged at 12 and 18.
  - Windows 2000: 120 of 120 on every build.
  - The harness's frozen-IDE-IRQ-14 clause, which the healthy Windows 98 SE guest also showed, became a NOTE (`cd38a98`).
- **The TT and High-Speed hub paths on 27-A.4's host vectors:** green. `test_topo` has 2206 checks and no gap open, beside `test_hub` and the hub-port vectors.

The clauses QEMU cannot take rest on 27-A.4's host vectors and are 28-E.1's bench clauses: the High-Speed hub, the single-TT and multi-TT hubs, Full and Low Speed devices behind them, and a Full-Speed hub behind a High-Speed one.

Suspend and resume are handled, not initiated, by the owner's decision of 2026-10-04 (27-A.1): the bus starts no suspend - selective suspend, hub ports included, is 28.3's idle-policy decision - but a hub port reported suspended is resumed before it is reset or enumerated, and a finished resume, a remote wake among them, needs no re-enumeration.

Open limitation, carried to 28.3: on Windows 2000, an audio device unplugged during playback receives SURPRISE_REMOVAL and ABORT_PIPE and no REMOVE. It predates the phase: the control on Phase 26's `a7ddbfa` reads the same.

Pre-existing limitation, carried to 28.3: the Windows 98 SE audio-load wedge. After SELECT_INTERFACE to alternate 0 and QUERY_CAPABILITIES the configuration manager makes no further progress and the taskbar clock stops, with every IRP to the function PDO completed and nothing outstanding at the HCD. It is timing-dependent (an attach about 40 s after boot wedged 5 of 6, a 120 s wait 0 of 20), it is on every build tried back to Phase 26's `40efd31`, and the miniport showed the same intermittent wedge (`lessons.md`, "Windows 98 wedges when a USB audio device is replugged after a cold boot"). It made two of the fix's Windows 98 SE legs inconclusive (`runs/run-27.md`, "The Windows 98 SE audio-load wedge").

The checkpoint **closed on 2026-10-04**, on the owner's pre-approval, on the builds of the records above: `c038326` for the Windows 98 SE audio unplug, `981f56b` for Full-Speed audio and the orderly path, `09ed9d1` for the soak. The Low-Speed mouse clause is the bench's by the owner's decision of 2026-10-04 (above).

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
- [x] 27-A.3 removal: a hub pulled with devices beneath it, the subtree's PDOs and slots, a device pulled mid-transfer, the orderly and the surprise paths on each primary. Done 2026-10-04 (`runs/run-27.md`, "27-A.3" and "The Windows 98 SE HID-unplug stall"; `b5ed0c4`, then `26e7cb6` to `53b43c0`, merged `18ecc83`; Codex rounds 1-5 on the fix): one teardown, `hcdSubtreeGo`, in design record 13 section 10.5's order as corrected - freeze; the PDOs reported missing first; endpoints stopped and a departed device's URBs parked on its PDO, completed CANCELLED at the client's cancel, its ABORT_PIPE horizon (64-bit submission stamps) or the PDO's stop, surprise removal or removal; slots disabled leaf-first; hubs pruned deepest-first. Read: the churn's first hub pulled with the five-tier chain beneath it on both primaries (27-V.1); a keyboard, a mouse and a stick unplugged on Windows 98 SE, the REMOVE within about 1 s each; on Windows 2000 SURPRISE_REMOVAL and REMOVE within about 1 s, a stick pulled mid-copy of a 256 MB file included. The orderly path with a hub beneath it (`runs/run-27.md`, "The closing legs", on `981f56b`; a hub on root port 2 with a mouse at `2.2` and a stick at `2.3`): on Windows 2000 the root hub's Disable (QUERY_REMOVE then REMOVE for all, the transfer identity 575 = 569 + 6, Enable bringing all back), the controller's Disable and Enable and its Uninstall and rescan, each with `fc /b` clean; on Windows 98 SE the root hub's "Disable in this hardware profile" (STOP, not REMOVE, 68 = 64 + 4, START on re-enable) and the controller's disable and enable, `fc /b` clean. The Windows 98 SE audio-unplug fatal 0E at 0028:C002A3A7 - the configuration manager sends a removed PDO QUERY_DEVICE_RELATIONS and a second IRP after its REMOVE, and the PDO had been deleted inside the REMOVE since Phase 26's `27063e1` - is fixed in `c038326` (a removed PDO is deleted at the next BusRelations answer): 20 of 20 audio unplugs on two guests, where the baseline faulted on the first. Open, carried to 28.3: on Windows 2000 an audio device unplugged mid-playback receives no REMOVE, as on Phase 26's `a7ddbfa`
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

Status: closed 2026-10-04. Worked on the integration branch `p28-31-int`,
its VM legs read on Package B (`p28-31-int` at `417199e`), merged into
`2.0.0.0` at `c9fcbde` and released on `main` as `4038447`. The VM clauses
closed on the owner's pre-approval (decisions table, "Checkpoints of Phases
28 to 31"); the bench clause was read after the cut (decisions table, "Cut
before the bench").

Why a phase: a target is not a build. The NT 5.x and 6.x class drivers query
interfaces Windows 2000's do not, the amd64 build is a second toolchain, and
the bench is where the miniport found issue 2, the hot-plug wedge no VM ever
showed.

- [x] 28-A.1 the per-target deltas of 25.2 for XP onward, and Windows ME under SweetLow's stack with its own `usbccgp.sys` left unused. Done 2026-10-04 (`runs/run-28.md`, "28-A.1 - the per-target deltas for NT 5.1 onward"; `39d6105`, Codex in `fe8480b` and `196c3c3`, merged at `5fdb9af`): `USB_BUS_INTERFACE_USBDI` versions 0 to 3, `GET_DEVICE_HANDLE`, `GET_TOPOLOGY_ADDRESS`, the idle notification held, `SYNC_RESET_PIPE` and `SYNC_CLEAR_STALL`; composite audio bound on all six NT guests, and split by the HCD itself on ME. The three `QUERY_INTERFACE` GUIDs the bus does not answer are left unanswered by design, as the stock hub and composite drivers leave them
- [x] 28-A.2 the amd64 build: WDK 7.1 as `WNET`, the amd64 allowlist, the second INF (moved to the start of Phase 27 by the owner's decision of 2026-10-03). Done 2026-10-04 in Phase 27 (`runs/run-27.md`, "28-A.2 on Windows XP x64"; `dff49ae`): the `qemu` and `release` flavours through every clause on Windows XP x64 SP2; the other amd64 legs are 28-V.1's
- [x] 28-V.1 the seven other guests - ME, XP SP3, XP x64, Vista x86 and x64, 7 x86 and x64 - each the clauses of 26-V and 27-V; with the primaries' legs, the ten install legs every cut reads. Done 2026-10-04 (`runs/run-28.md`, "The per-guest procedure", "The reading on the merged build, `qemu` flavour" and "The ten install legs on the `release` flavour"; Package B, `417199e`): the six NT guests PASS or NOTE on every clause with no HCD or `xhciuas` defect (Windows 7 x64's soak note is the port hold, not a defect); Windows ME PASS but clause 8, the controller re-enable with a HID device attached, which hangs and is carried (decisions table, "Windows ME: the controller re-enable"); the ten `release`-flavour legs the same
- [x] 28-E.1 the bench: the E460 on Windows 98 SE and the second Windows 98 SE machine through `run-13e.md`'s stage list, audio played and heard at a root port and behind a hub, Phase 27's High-Speed hub clauses and Full-Speed hub clause, and its Low-Speed mouse clause. Done 2026-10-04 (`runs/run-28.md`, "28-E.1 - the bench"): read by the owner on real hardware after the cut (owner's decision of 2026-10-04 to cut before the bench), on the ThinkPad E460 and the ThinkPad P14s Gen 1 under Windows 98 SE; the owner reports no issue beyond the known limitations. No per-device reading is recorded. The Windows 7 half was dropped (decisions table, "Windows 7 on the bench"), and the Full-Speed hub clause was a USB 2.0 hub behind an ADuM isolator (decisions table)
- [x] 28.3 every known limitation of `1.2.0.0` re-measured under the HCD and written down as gone, carried or new. Done 2026-10-04 (`runs/run-28.md`, "28.3 - the limitations of `1.2.0.0` under the HCD"): gone - the Windows 98 churn wedge, the Windows 7 disable hang (by ruling), the Windows 2000 audio unplug during playback; carried - the NUSB stop crash on the upgrade path only, the idle that never sleeps (by ruling), the Windows 98 SE audio-load wedge; new and carried - Windows ME's UAS-first Code 2 and its controller re-enable hang; a known difference - a device moved to another port is found again as new hardware. The release notes carry every survivor

Checkpoint: the ten install legs read on the `qemu` build and then the
`release` flavour; the bench run on both Windows 98 SE machines and the E460's
Windows 7, Phase 27's High-Speed hub clauses included; the limitation list of
28.3 complete. Not a checkpoint: a SuperSpeed device anywhere.

The checkpoint **closed on 2026-10-04**: the ten install legs and 28.3 on
Package B (with `c0d8a51` and Package A, `2f6030a`, for the NT guests'
clauses 1 to 11 and 13 to 15), and the bench on the owner's verdict after the
cut, its Windows 7 half dropped. Codex: the integration's nine rounds and
each later branch's to clean.

Records: `runs/run-28.md`; `build-and-test.md`'s target sections;
`design/11-x64-targets.md` for what the amd64 build keeps from the miniport's.

## Phase 29 - SuperSpeed on Root Ports

Goal: a USB 3.x device on a root port enumerated and driven at SuperSpeed,
with the bulk, interrupt and isochronous paths at that speed, and a USB 2.0
device on the same connector unchanged.

Status: closed 2026-10-04. Worked on `p28-31-int`, merged into `2.0.0.0` at
`c9fcbde`. 29-A.1's SuperSpeedPlus refusal value was dropped from `2.0.0.0`
by the owner's ruling, 29-E.1 was read after the cut, and 29-E.2 is recorded
as not taken.

Why a phase: this is the reason the successor exists. The port class the
miniport left unpowered is powered for the first time, the link has a state
machine of its own, and enumeration changes shape at the first packet.

- [x] 29-0 the SuperSpeed and SuperSpeedPlus specification transcription into `xhci-data-structures.md`, and which P14s Gen 1 connectors reach which controller. Done 2026-10-04 (`runs/run-29.md`, "29-0 - the specification transcription"; `c2ed9cf`, the code's "to verify" comments settled in `6aada06`): sections 10 and 11 checked row by row against xHCI 1.2c and USB 3.2 r1.1. The connector question was deferred to the bench (decisions table) and answered at 29-E.2
- [x] 29-A.1 port classification: USB 3.x ports managed and powered, the companion pairing, the all-SuperSpeed controller accepted, the rate read from the PSI dwords apart from the speed class, SuperSpeedPlus accepted at its trained rate. Done 2026-10-04 (`runs/run-29.md`, "29-A.1 to 29-A.6 - what was built"; `cd65bda`, Codex in `017048c` and `0911520`, merged at `5d67641`): all of it, with `test_caps`' vectors for Gen 1, Gen 2x1, Gen 1x2 and Gen 2x2; the all-SuperSpeed controller is untested ground. Dropped from `2.0.0.0` by the owner's ruling (decisions table, "Gaps dropped from `2.0.0.0`"): the registry value restoring the SuperSpeedPlus refusal and its host vectors - no value restores it, and `HCD_HOLD_REASON_SSP_REFUSED` in `src\hcd.h` is unused
- [x] 29-A.2 the link state machine. Done 2026-10-04 (`runs/run-29.md`, "29-A.1 to 29-A.6 - what was built"; `cd65bda`, merged at `5d67641`, then `81d3942` and `2943dfe`): `xhci_link.c` (host suite `test_link`) - hot and warm reset with the hot-to-warm conversion, SS.Inactive, Compliance and CAS recovery within a bounded warm-reset budget, U3 resume; QEMU's links trained to U0 on every guest
- [x] 29-A.3 SuperSpeed enumeration. Done 2026-10-04 (same section; `cd65bda`, merged at `5d67641`; zero-bandwidth isochronous endpoints `4cef6d3`, merged at `9077edf`): EP0 at 512 bytes, the BOS at SuperSpeed only, burst, Mult and Max ESIT Payload from the companion, the xHC's Configure Endpoint verdict as the URB status; storage and UAS at 5000 Mb/s on all ten install legs
- [x] 29-A.4 transfers at SuperSpeed. Done 2026-10-04 (same section; `cd65bda`, merged at `5d67641`): bulk, interrupt and isochronous TDs with burst, 1024-byte endpoints and the TD Size rule; bulk read on every install leg; SuperSpeed isochronous built from the specification on host vectors, untested ground in the release notes
- [x] 29-A.5 fallback in both directions, and the active hold that sends a trained device back to USB 2.0 under the release rules of `future-plans/superspeed-storage-behind-a-switch.md` section 6.1, tightened to the held device's identity. Done 2026-10-04 (same section; `cd65bda`, `6fceeec`, Codex in `017048c` and the integration's rounds to `c0f9ad6`): `HcdHoldRequestUsb2`, paired, unidentified and orphan holds, release only when the held device is seen on the companion and leaves, re-armed with `PLS` = RxDetect and `LWS`; seven counters; host vectors only, since no guest has a non-streaming controller or a link that fails to train
- [x] 29-A.6 SuperSpeedPlus at Gen 2, Gen 1x2 and Gen 2x2. Done 2026-10-04 (same section; `cd65bda`, merged at `5d67641`; the parent-hub fields `b6e569e` to `f7abde2`, merged at `9077edf`): the SuperSpeedPlus isochronous companion under `HCCPARAMS2.LEC`, refused with a counter where the fields cannot hold it, `XHCISNAP` decoding rate and lanes; host vectors only, every mode untested ground in the release notes
- [x] 29-V.1 QEMU's `usb-storage` at SuperSpeed on both primaries. Done 2026-10-04 (`runs/run-29.md`, "The reading on the merged build"; `c0d8a51`): on Windows 98 SE under NUSB 3.3 and on Windows 2000 SP4, 5000 Mb/s, each target's `usbstor.sys`, `fc /b` clean, the USB 2.0 row unchanged, disable, enable and shutdown clean; again on Package B, and on all ten install legs (28-V.1 clause 14)
- [x] 29-E.1 the bench: the two SuperSpeed drives of `test-equipment.md` at rig position D on Windows 98 SE, round trips and throughput. Done 2026-10-04 (`runs/run-29.md`, "29-E.1 - the bench"): read by the owner on real hardware after the cut (owner's decision of 2026-10-04 to cut before the bench), on the ThinkPad E460 and the ThinkPad P14s Gen 1 under Windows 98 SE; the owner reports no issue beyond the known limitations. No per-device reading is recorded; the Windows 7 half was dropped (decisions table)
- [x] 29-E.2 a reading, not a clause: a Gen 2 device on a P14s Gen 1 connector reaching 10 Gbit/s. Recorded 2026-10-04 as not taken (`runs/run-29.md`, "29-E.1 - the bench"): the P14s Gen 1's 10 Gbit/s connector is a Thunderbolt port, and a Gen 2 drive there did not mount even under Windows 11, nor under Windows 98 SE; SuperSpeedPlus stays untested ground

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

The checkpoint **closed on 2026-10-04**: 29-V.1 on both primaries, and
29-E.1 on the owner's verdict after the cut, its Windows 7 half dropped.

Records: `design/13-superspeed-hcd.md`; `usb-xhci-info/xhci-data-structures.md`;
`runs/run-29.md`; `test-equipment.md`, "What the SuperSpeed halves are for".

## Phase 30 - SuperSpeed Hubs

Goal: a USB 3.x hub's SuperSpeed half driven as a hub of the bus, with
SuperSpeed devices behind it, while its High-Speed half carries USB 2.0
devices as a Phase 27 hub.

Status: closed 2026-10-04. Worked on `p28-31-int`, merged into `2.0.0.0` at
`c9fcbde`. Built and reviewed on host vectors; the owner's bench after the
cut was its first execution. The Windows 2000 half was released untested,
built from the specification (decisions table, "SuperSpeed hubs on Windows
2000, revisited").

Why a phase: a USB 3 hub is two hubs on two ports with their own descriptor,
their own port status format and their own depth and link-state requests, and
no QEMU device models one, so the readings are bench readings.

- [x] 30-0 the USB 3 hub class, transcribed from the USB 3.2 specification. Done 2026-10-04 with 29-0 (`runs/run-30.md`, "30-0 - the USB 3 hub class transcription"; `c2ed9cf`): section 11 of `xhci-data-structures.md` verified against USB 3.2 r1.1
- [x] 30-A.1 the SuperSpeed hub inside the bus, a SuperSpeedPlus hub accepted at its trained rate. Done 2026-10-04 as code and host vectors (`runs/run-30.md`, "30-A.1 - the SuperSpeed hub inside the bus"; `034a119`, Codex in `45b3a63`, merged at `63cba23`; parent-hub fields `b6e569e`, merged at `9077edf`): `xhci_sshub.c` and `hcd_sshub.c` - `SET_HUB_DEPTH`, the type 0x2A descriptor, Configure Endpoint with no TT fields, `BH_PORT_RESET` within the warm-reset budget, SS.Disabled ports re-armed on a backoff, the extended port status's rates, a pairing counter for the two halves. No QEMU device models a SuperSpeed hub
- [x] 30-A.2 host suites. Done 2026-10-04 (`runs/run-30.md`, "30-A.2 - host suites"; `034a119`): `test_sshub` (553 checks) and SuperSpeed rows in `test_topo`, none failing on `c9fcbde`
- [x] 30-E.1 the bench unit (`05E3:0610` and `05E3:0612`): a SuperSpeed drive behind the SuperSpeed half and a High-Speed device behind the other on Windows 98 SE, plugged, unplugged and re-plugged. Done 2026-10-04 (`runs/run-30.md`, "30-E.1 - the bench"): read by the owner on real hardware after the cut (owner's decision of 2026-10-04 to cut before the bench), on the ThinkPad E460 and the ThinkPad P14s Gen 1 under Windows 98 SE; the owner reports no issue beyond the known limitations. No per-device reading is recorded; the Windows 7 half was dropped (decisions table)

Checkpoint: the bench reading of 30-E.1 on Windows 98 SE, and the same
clauses observed on Windows 2000. The second half has no vehicle today:
Windows 2000 is a virtual-machine target and no QEMU device models a
SuperSpeed hub. The owner's decision of 2026-10-02 (decisions table) is that
a Windows 2000 vehicle is required for now and the question is revisited
when this phase opens; until a vehicle exists or that decision changes, this
phase cannot close and Phases 31 and 32 wait on it. Not a checkpoint: UAS.

The checkpoint **closed on 2026-10-04**: 30-E.1 on the owner's verdict after
the cut, and the Windows 2000 half released as untested ground by the
revisit of the 2026-10-02 decision (decisions table).

Records: `design/13-superspeed-hcd.md`; `runs/run-30.md`.

## Phase 31 - Streams and UAS

Goal: bulk streams in the bus and a UAS class driver of this project's own,
so that a UAS-capable device runs UAS at SuperSpeed and at High Speed, with
Bulk-Only still selectable.

Status: closed 2026-10-04. Worked on `p28-31-int`, merged into `2.0.0.0` at
`c9fcbde`. 31-0's unread sources were dropped from `2.0.0.0` by the owner's
ruling, and 31-E.1 was read after the cut.

Why a phase: streams are a transfer model the bus has never had, UAS is a
class driver with a storage-stack contract of its own on each target, and
which driver a UAS-capable device gets is a bus-side id decision; the three
are one slice.

- [x] 31-0 static: streams (xHCI section 4.12), UAS in both its shapes, the storage-stack route on each target and NUSB's mapping path on Windows 98 SE, the 98 SE CD's `layout.inf`, and QEMU's `usb-uas`. Done 2026-10-04 (`runs/run-31.md`, "31-0 - the static readings"): the 98 SE CD's layout files name none of `usbntmap.sys`, `usbmphlp.pdr` and `usbstor.sys`, ME's all three; NUSB's mapping path read statically; QEMU's `usb-uas` offers UAS at both speeds (31-V.1). Dropped from `2.0.0.0` by the owner's ruling, since UAS passed on every target (decisions table, "Gaps dropped from `2.0.0.0`"): the xHCI 4.12 streams rows of `xhci-data-structures.md` still marked "to verify", NT `disk.inf`'s `GenDisk` binding and QEMU's `hw/usb/dev-uas.c`, none read
- [x] 31-A.1 primary streams in the bus. Done 2026-10-04 (`runs/run-31.md`, "31-A.1 - primary streams in the bus"; `506d1ac`, Codex in `3611d81`, merged at `6f9f24d`; the reset-loop fix `c4ec1c3`, merged at `fe2577b`): `IOCTL_XHCI98_OPEN_STREAMS` and `_CLOSE_STREAMS`, one pipe handle per stream, stream-aware Configure Endpoint and Set TR Dequeue, abort and close; host suite `test_stream`; 16 streams per endpoint under `usb-uas` on every install leg
- [x] 31-A.2 the UAS class driver, `xhciuas.sys`, with its own INFs, both transports. Done 2026-10-04 (`runs/run-31.md`, "31-A.2 - the UAS class driver, `xhciuas.sys`" and "Windows 98 SE: the LUN's Code 10, and its fix"; `4e66702`, Codex in `8619f23` and `21dda9e`, merged at `4d32167`; amd64 `3c2bd27`; the Windows 98 INF half `1ed1ba6`): bound on `Prot_62` only, a PDO per LUN, information units (`test_uas`), tags, autosense, task management and reset recovery; UAS at both speeds on every install leg, x86 and amd64
- [x] 31-A.3 the id policy: the bus chooses the transport and exposes only its compatible ids. Done 2026-10-04 (`runs/run-31.md`, "31-A.3 - the id policy"; `e0309f9`, Codex in `ffe00b1`, merged at `030362f`): `xhci_xport.c` (`test_xport`) - UAS when offered and usable unless `XhciForceBulkOnly` and a Bulk-Only alternate exist, a refused interface under `USB\XHCI98_NOXPORT&...`, the non-streaming send-back through 29-A.5's hold (host vectors only)
- [x] 31-V.1 QEMU's `usb-uas` on both primaries at SuperSpeed and at High Speed. Done 2026-10-04 (`runs/run-31.md`, "The reading on the merged build"; `c0d8a51`): every clause PASS - 16 streams at SuperSpeed, streamless at High Speed, `fc /b` both ways, fresh install with no restart, uninstall and re-plug, the UAS-only device staying UAS under the value; the same on the other eight install legs (28-V.1)
- [x] 31-V.2 the dual-transport clauses on Windows 2000 through the passed-through ASMedia bridge, with a Windows 98 SE control. Done 2026-10-04 (`runs/run-31.md`, "The reading on Package A" and "The Windows 98 SE retake on Package B"; `2f6030a`, `417199e`): the StoreJet (`174C:5106`) at 480 Mb/s - UAS on a fresh install, Bulk-Only under the value, and back, each with a round trip, on Windows 2000 and on Windows 98 SE; the earlier Code 10s were the passthrough's
- [x] 31-E.1 the bench on Windows 98 SE: the ASMedia bridge under UAS at both speeds, the MSSU10 under UAS at SuperSpeed and Bulk-Only behind a USB 2.0 hub, the forced-Bulk-Only value on both. Done 2026-10-04 (`runs/run-31.md`, "31-E.1 - the bench"): read by the owner on real hardware after the cut (owner's decision of 2026-10-04 to cut before the bench), on the ThinkPad E460 and the ThinkPad P14s Gen 1 under Windows 98 SE; the owner reports no issue beyond the known limitations. The P14s Gen 1 UAS reading is the README's photo (`images/xhci98-flash-speed-test.jpg`: the MSSU10 over UAS at SuperSpeed, ATTO at about 200 to 220 MB/s); no other per-device reading is recorded. The Windows 7 half was dropped (decisions table)

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

The checkpoint **closed on 2026-10-04**: 31-V.1 and 31-V.2 on the VMs, and
31-E.1 on the owner's verdict after the cut.

Records: `design/13-superspeed-hcd.md`; `runs/run-31.md`.

## Phase 32 - Release `2.0.0.0`

Goal: the first release of the successor, both architectures, with the
release notes, the acceptance test and the post-release run rewritten for a
driver that is a bus rather than a miniport.

Status: closed 2026-10-04 on the cut. Opened the same day on branch
`p32-cut`, merged into `2.0.0.0` at `c9fcbde`; the owner pushed the result
and squash-merged it to `main` as `4038447` ("2.0.0.0 (#13)"). By the
owner's decision of 2026-10-04 the cut came before the bench (decisions
table, "Cut before the bench").

Why a phase: a cut changes every user-facing statement, and this one changes
what the download is.

- [x] 32.1 the record: the release notes rewritten for the HCD, the readme template, `releases/history.md`, `README.md` and `AGENTS.md`. Done 2026-10-04 (`dacee67` to `34efeb0`, filled in `92d52ec` and `0ae1408`): every target's standing, the F8 requirement on Vista x64 and Windows 7 x64, the stock Windows 98 SE statement, the virtual-hub values with no effect, 28.3's limitations, and the untested ground (SuperSpeed isochronous, SuperSpeedPlus by rate and lane count, a controller with no USB 2.0 port, SuperSpeed hubs on Windows 2000). Written, by the owner's choice, as if the bench passes; the owner's bench reported no issue beyond the known limitations
- [x] 32.2 the acceptance test and the post-release run of design record 09 adapted to the HCD. Done 2026-10-04 (`70e0185`, filled in `92d52ec` and `0ae1408`): `docs/using/release-acceptance-test.md` and `design/09-post-release-unattended-run.md` rewritten with SuperSpeed storage and UAS rows; `make-release.ps1` refuses a cut while a `TODO(` or `TBD` remains (`9bb071b`)
- [x] 32.3 the cut: `xhci_version.h`, the four INFs' `DriverVer`, `make-release.ps1` publishing `2.0.0.0` in four directories, the ten install legs read from the asset with their SuperSpeed storage and UAS clauses. Done 2026-10-04 (`f418735`, `releases\2.0.0.0`): cut with `make-release.ps1`, then three readme-only `-Force` re-cuts, the binaries unchanged; asset `xhci98-2.0.0.0.zip`, 633,026 bytes; the ten release-asset install legs passed every clause (`out\phase32\asset\`). Windows 7's "Browse my computer" does not upgrade over `1.2.0.0`, so the README's "Let me pick" route is the one. The owner pushed it and squash-merged it to `main` as `4038447` ("2.0.0.0 (#13)")

Checkpoint: the package `make-package.ps1` assembles holds the HCD, the UAS
driver, their INFs, the tools and the readme and nothing of Microsoft's; the
ten install legs read from the asset, each with its SuperSpeed storage and
UAS clauses; the post-release run on both primaries accepted by the owner. The upload and the hand-run acceptance on a fresh VM
and a physical machine are the owner's, as `roadmap.md` ends by saying.

The checkpoint **closed on 2026-10-04**: the asset and its ten install legs
as above, and the owner's acceptance in the bench verdict and the merge to
`main`. No post-release unattended run is recorded for `2.0.0.0`.

Records: `releases/history.md`; `releases/2.0.0.0/`;
`docs/using/release-notes.md`; `docs/using/release-acceptance-test.md`.

## Phase 33 - Release `2.1.0.0`

Goal: the first update of the HCD generation: the Windows ME controller
re-enable fix, a device named by its serial number, a `txtsetup.oem` so
text-mode Setup of Windows 2000 and XP can load the driver, external hubs
shown in Device Manager as devnodes of their own, and the root hub's Power
tab and the controller's Advanced tab showing what Windows' own stack shows.

Status: opened 2026-10-04 on branch `2.0.1.0`, from `main` at `3eb2d2b`
(the closed roadmap after the `2.0.0.0` release), and renamed `2.1.0.0` by
the owner the same day, when external hubs joined the release.

Why a phase: three of these change how Windows sees devices, and the fourth
adds an install path the project has never had.

- [x] 33.1 the Windows ME controller re-enable fix: device PDOs STOPPED by an orderly controller stop are kept dormant and revived at the same instance key on re-enable, as `usbhub` does, instead of being reported gone and replaced (the hang with a USB mouse attached, 3 of 3 on `2.0.0.0`; a stick alone completed, and no keyboard was ever tried - issue 9 section 1). Merged into `2.1.0.0` at `a9a7577` from `p28-reenable` (`fa2af9b`; Codex clean); its legs passed on the `qemu` package `out\phase28\pkg-reenable1`: ME re-enable with a mouse 3 of 3, with a mouse and a stick, Windows 98 SE and Windows 2000 disable and enable with a soak each (`out\phase28\pdoleak\`). Owed: the same legs on the `2.1.0.0` package, plus a USB keyboard alone, and the limitation removed from the README, the release notes and the readme template (`docs/issues/09-me-controller-reenable-stopped-pdos.md`) **Done 2026-10-05** on the `2.1.0.0` integration build (legs 1a, 1d, 1f) and its `release` flavour (3d): ME re-enable with a mouse 3 of 3, with a mouse and a stick, with a USB keyboard alone, and with devices behind a hub; Windows 98 SE and 2000 disable/enable with soaks 10 of 10 (`out\phase33\legs\results.md`).
- [x] 33.2 a device's instance id from its serial number: answer `UniqueID` TRUE and an instance id built from the serial number string for a device with a valid one, as `usbhub` does, and keep the location for a device without one, on every target. A device moved to another port keeps its devnode, and two devices with the same VID and PID on one port no longer share one (the `2.0.0.0` difference recorded in the release notes; `out\phase28\w2k-disable\`). The serial read must tell "no serial" from "read failed"; composite functions and devices behind hubs keep their own suffixes; host vectors for the id rules; legs on both primaries and the NT guests, moving a stick between ports and checking no new hardware is found. *Draft, not ticked:* the code is on branch `p33-serial` (design record 13 section 10.7, "Instance ids from the serial number"): the serial string is read once per enumeration before the first PDO (`HcdDeviceReadSerial`), checked by `XhciFuncSerialId` (`0x21`-`0x7E` less `,` and `\`, after `usbhub`'s rule as ReactOS documents it, `legal-provenance.md` section 4) and answered by `XhciFuncInstanceId` with `UniqueID` TRUE; no serial, a refused string and a read failed three times in a row keep the location form, the last two counted (`serial.refused`, `serial.readfailed`), and a timed-out read gives no PDO until the controller reset; a serial a present PDO of the same VID and PID already carries, on any of the driver's controllers, leaves the newcomer on the location form (`serial.duplicate`); a function adds `&nn`; Windows 98's dormant PDOs are matched by the id they answer. Host vectors in `test\test_func.c`. Owed: the guest legs above, and the release notes' line for the `2.0.0.0` difference and the one-time new devnode on upgrade **Done 2026-10-05**: a serial stick keeps one devnode across root ports, behind a hub and on a SuperSpeed port on 98 SE, 2000, ME and XP with no new hardware found; two sticks two devnodes; a duplicate serial takes the location form (legs 1b, 1e, 1f, 1g; Vista and 7 in wave 3).
- [x] 33.3 `txtsetup.oem`: a text-mode Setup driver description for Windows 2000 and XP (and XP x64 from the x64 directory), so a machine whose keyboard or install medium sits on an xHCI controller can load `xhci98.sys` at Setup's driver prompt. First the feasibility: which files text-mode Setup must load with a bus driver that replaces `usbport.sys` and `usbhub.sys` (`usbd.sys`, the HID and storage class drivers), and whether Setup's own USB support conflicts with it; then the file, its INF gate, the packaging (`make-package.ps1`, `make-release.ps1`) and an install leg per target from a floppy image in a virtual machine. The feasibility is design record 13 section 5.6 (2026-10-04, every clause `static`): feasible with limits on all three - Setup's own media carry `usbd.sys`, `hidclass.sys`, `hidparse.sys`, `hidusb`, `kbdhid`, `mouhid` and `usbstor` and bind no `PCI\CC_0C0330`, the root hub's project-owned id keeps `usbhub` off it, and the bus's own composite splitting keeps `usbccgp` out; F6 itself needs the firmware's USB keyboard, a disk the bus selects for UAS is unusable in text mode (a Bulk-Only-only one always works), and a Windows install onto a USB disk is out. Done on `p33-txtsetup`: `src\txtsetup.oem` and `src\txtsetup-amd64.oem` (a `scsi` component, driver and INF, the two INF model ids), `scripts\inf-gate\check-txtsetup-oem.ps1` with its self-tests on every build, and both packagers staging, gating and publishing `txtsetup.oem` at each flavour directory's root. The Windows 2000 text-mode leg first failed (2026-10-04, `lessons.md`): Windows 2000's text mode starts only the devices in a hub's first `BusRelations` answer, and the root hub answered it empty. Fixed on `p33-initenum` (design record 13 section 5.7, Codex-reviewed): each hub FDO's first answer after a start waits until the devices connected at start have settled, bounded by `XhciFirstEnumWaitMs` (default 5 s) and `XhciFirstEnumPortMs` (default 2 s per port), on every target; the Windows 2000 F6 leg then passed in QEMU on the `release` and `qemu` flavours, keyboard at a root port and behind a hub (keyboard answering at Welcome, the stick at the partition screen), the XP SP3 leg still passed, and installed Windows 98 SE and Windows 2000 boots with a mouse and a stick at root ports and behind a hub, and Windows 98 SE's controller disable and enable, passed with first answers of 0 ms to 1.1 s and 20 ms with nothing attached. Owed: the rest of each target's install leg (GUI mode and the installed system, and XP x64's reading again on this change) and the legs on the `2.1.0.0` package. The XP GUI-mode limitation found on its legs is `docs/issues/10-xp-f6-gui-mode-usb-input.md` **Done 2026-10-05**: Windows 2000 and XP text mode pass on the `release` package with the first-enumeration wait (legs 2e, 2f), the Recovery Console passes on both (2g, 2h); XP's GUI-mode prompts are a limitation (issue 10).
- [x] 33.4 external hubs as devnodes: a PDO per external hub under a project-owned hardware id (never `USB\Class_09` or a VID/PID id that the OS's `usbhub.inf` would claim), bound by `xhci98.inf` with `xhci98.sys` as its FDO as the root hub is, the devices behind it re-parented under it, the door's `DeviceIsHub` recursion answered so `usbui` and USBView walk into it, on every target (owner, 2026-10-04: standard Windows practice, every Microsoft stack shows its hubs). Legs: a hub with a mouse and a stick behind it, the tree by connection on both primaries and the NT guests, hub unplug and replug, a two-tier hub chain, and the 33.1 re-enable legs with a device behind a hub **Done 2026-10-05**: hub devnodes, the tree by connection, a two-tier chain, hub unplug and replug, and hub disable and enable (after its NT fix, merge `862c108`) read on 98 SE, 2000 and XP; a device's Address is its port on its parent hub (merge `f8226d9`). High-Speed hubs and the SuperSpeed hub half are bench-only: QEMU's hub is USB 1.1.
- [x] 33.5 the Device Manager pages (owner, 2026-10-04, on the P14s Gen 1 and the E460 under Windows 98 SE, every device): the root hub's Power tab showed every device's power as unknown, and the controller's Advanced tab's bandwidth did not rise as devices were added. Power: `hcdDoorDescriptor` refused `IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION` unless `bRequest` was 6, and every target's `usbui.dll` sends it zero-filled with only `ConnectionIndex`, `wValue` 0x0200 and `wLength` 9 written, one call of 0x15 bytes that must return exactly 0x15 (static, the nine listings; design record 13 section 8.3); fixed on `p33-door` by taking `bRequest` 0 as GET_DESCRIPTOR. Bandwidth: on 98 SE to XP `usbui` counts only open isochronous pipes, as over Microsoft's stack, so a mouse, a keyboard or a drive adds nothing and is not a defect; on Vista and 7 it comes from the `GUID_USB_WMI_DEVICE_PERF_INFO` WMI query the HCD does not serve and stays at zero, and a SuperSpeed device's power reads a quarter of its draw (the page doubles `bMaxPower`, which is in 8 mA units at SuperSpeed): both recorded as limitations in the release notes. Legs: the Power tab showing mA values for a mouse, a keyboard and a stick on both primaries and an NT guest, the same behind a hub with 33.4, an audio device playing raising the Advanced tab's figure on 98 SE, and `scripts\hub-characterise.ps1` taught the `usbui`-shaped request (`bRequest` 0, in = out = 0x15) so the probe that masked this cannot again **Done 2026-10-05**: the Power tab shows mA on every target read (legs 1c, 1e, 1g, 3g); the Advanced tab's figure while audio plays was not read (Windows 98 SE's USBAUDIO fails on play in QEMU).
- [x] 33.6 a device's name from its product string (owner, 2026-10-04, Windows 98 SE: the Add New Hardware wizard names every new device "USB Device", where under the `1.x.x.x` miniport, with Microsoft's `usbhub` above it, it showed the device's own name): the device PDOs answer `IRP_MN_QUERY_DEVICE_TEXT` `DeviceTextDescription` with a fixed `"USB Device"` (`src/hcd_pdo.c`), against design record 13 section 10.7's rule - `iProduct` for a device PDO, `iInterface` or an IAD's `iFunction` for a function PDO, falling back to the device's `iProduct`. Read the strings at enumeration beside 33.2's serial number, in the device's first LANGID, sanitised, with "USB Device" when there is none or the read fails; a host vector for the pick and fallback rule. Legs: a mouse and a stick plugged on Windows 98 SE, the wizard naming each; the Device Manager names on Windows 2000 and XP **Done 2026-10-05**: the product string is answered (driverless devices, New Hardware Found on 98 SE, Windows 7's bus-reported description); where a class INF names a device its name wins, as under Microsoft's hub driver.
- [x] 33.7 SweetLow's hidusbf under the HCD (owner, 2026-10-04): the `1.x.x.x` miniport's hidusbf readings (`run-24.md`, the 1000, 500 and 250 Hz ladder at a root port and behind a hub) have no `2.x` counterpart. The HCD should honour the filter by construction - `URB_FUNCTION_SELECT_CONFIGURATION` copies the client's configuration descriptor and takes each endpoint's interval from the copy (`src/hcd_cfg.c`), which is the descriptor hidusbf rewrites (static) - but nothing has run it. Legs: hidusbf's current release (LordOfMice's of 2026-10-03 or later, which lists `xhci98` among its known drivers; version and SHA-256 recorded, the binary not tracked; `tools\hidusbf-extracted` is an older copy) on a mouse at 1000, 500 and 250 Hz, at a root port and behind a hub, with the interval the driver programmed read back from the controller's Output Endpoint Context (the QEMU monitor's `xp`) or the qemu trace (the `1.x.x.x` `ep.open.ival` record has no `2.x` counterpart), on Windows 98 SE under NUSB 3.6, on Windows ME and on 32-bit XP; and on a stock Windows 98 SE (where the expected Code 2 did not come: its `usbd.sys` exports `USBD_ParseDescriptors` after all, `legal-provenance.md` section 4, corrected 2026-10-05). The release notes say what was read, and that the filter's setting must be applied again after an upgrade from `1.x.x.x` (a new devnode per device) **Done 2026-10-05**: the 1000/500/250 Hz ladder programs Interval 3/4/5 at a root port and behind a hub on 98 SE (NUSB 3.6 and stock), ME and XP (legs 2a-2d, and XP behind a hub after the Address fix).
- [x] 33.8 Low- and Full-Speed interrupt endpoints polled faster than 1000 Hz (owner, 2026-10-04, from GitHub issue 4: LordOfMice asks for the 2000 to 8000 Hz that hidusbf reaches on a Low-Speed mouse under modern Windows on xHCI): the HCD floors a Low- or Full-Speed interrupt endpoint's Interval at 3 (1 ms), the bottom of xHCI 1.2c Table 6-12's range for those speeds (`src/xhci_pipe.c`). First the reading: how hidusbf expresses a rate above 1000 Hz for such a device, what Linux programs, what the specification says of an Interval below the range, and whether QEMU's controller can show the difference; then, if feasible, an opt-in registry value (default off, so the stock driver stays inside the specification) that lets the faster Interval through, falling back to Interval 3 with a counter when Configure Endpoint refuses it. Legs: the rate measured in QEMU if it can show it; otherwise the bench procedure for the E460 and the P14s Gen 1, and the release notes calling it untested ground until read there **Done 2026-10-05** as built and reviewed, off by default; no rate was read in a VM (QEMU paces any Interval) - the bench reading on the E460 and the P14s Gen 1 (design record 13 section 13.6) is owed, and the release notes call it untested ground.
- [ ] 33.9 the cut: `xhci_version.h` and the four INFs' `DriverVer` at `2.1.0.0`, `releases/history.md`, the release notes and the README for 33.1 to 33.8, `make-release.ps1`, and the ten install legs read from the asset

Checkpoint: 33.1's legs, 33.2's port-move legs, 33.4's hub legs, 33.5's
Power and Advanced tab legs and 33.6's device-name legs, 33.7's hidusbf legs and 33.8's polling-rate reading passing on the `2.1.0.0` package on every target, 33.3's Setup leg on Windows 2000 and
XP or its feasibility recorded as a decision, and the ten install legs read
from the asset.

Records: `releases/history.md`; `docs/using/release-notes.md`.

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
- **The SuperSpeedPlus refusal value (29-A.1).** Dropped from `2.0.0.0`:
  no registry value sends a SuperSpeedPlus link back to USB 2.0, and
  `HCD_HOLD_REASON_SSP_REFUSED` in `src\hcd.h` is unused.
- **31-0's unread sources.** The xHCI 4.12 streams rows of
  `xhci-data-structures.md` still marked "to verify", NT `disk.inf`'s
  `GenDisk` binding and QEMU's `hw/usb/dev-uas.c`; dropped from `2.0.0.0`,
  since UAS passed on every target.
