# Development Roadmap - The Host Controller Driver, Phases 25 Onward

The third roadmap file, opened on 2026-10-02 at the project owner's request,
the day Phase 24 closed on the `1.2.0.0` cut. It carries every phase of the
miniport's successor: a monolithic USB host controller driver, `xhci98.sys`,
that replaces `usbport.sys`, the hub driver and the composite parent with code
of this project's own and so can drive USB 3.x SuperSpeed devices, hubs and
UAS storage on the same targets the miniport serves. The first two files are
unchanged by it: [`roadmap.md`](roadmap.md) keeps the status, the conventions
and Phases 0-16, and
[`roadmap-miniport-updates.md`](roadmap-miniport-updates.md) keeps Phases 17-24, the whole of the miniport's life after `1.0.0.0`. Every
convention of theirs holds here: the batching convention and the task-id
rules, "observed on both" as `AGENTS.md` qualifies it, and the rule that no
phase advances past an unobserved checkpoint.

**Status: `2.2.0.0` is cut and benched.** Phases 25 to 35 are closed. `2.0.0.0` was
cut on 2026-10-04 and squash-merged to `main` as `4038447` ("2.0.0.0
(#13)"); the owner benched it after the cut and reports no issue beyond the
known limitations (Phase 28, 28-E.1). `2.1.0.0`, the first update, was cut
on 2026-10-05 (Phase 33), with its polling-rate reading owed to the bench.
`2.1.1.0`, an interim release, was cut on 2026-10-06 (Phase 34). Phase 35,
SuperSpeed devices falling back to USB 2.0 on Intel Sunrise Point,
`XhciIntelPortSwitch` 2 and the controller tolerance work, is `2.2.0.0`
(owner, 2026-10-06; it was the interim `2.1.2.0` until the tolerance work
joined it), cut on 2026-10-07 and closed the same day on the owner's bench (35-E). Phase 36,
opened 2026-10-07 from a user's Dell E7250 under Windows XP x64 (issue 12),
is open on branch `2.3.0.0`, its target version. Its root cause was found on 2026-10-08: the
BIOS's `D20IR` interrupt routing. The fix is task 36.3. Polled event
delivery joined Phase 36 on 2026-10-08 and selective suspend, off by
default, on 2026-10-09 (owner); what remains in "Planned work" below
carries no phase number and no version until the owner opens it.

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
| Hub class and composite splitting | Inside the bus driver. External hubs are objects of the bus, not PDOs; a composite device is split by the bus into per-function PDOs. No hub-to-port contract to design, no INF binding path for an external hub or a composite parent (the root hub's own project-owned binding, in the root-hub row below, is the one hub binding the package carries), no per-target matching of `usbccgp.sys` or Windows 98's composite parent. From `2.1.0.0` an external hub also gets a devnode of its own, as every Microsoft stack shows its hubs (owner, 2026-10-04; task 33.4): the bus still runs the hub, and only its PnP presentation changes - each hub it serves is presented under a project-owned id (`XHCI98\HUB`, `XHCI98\HUB30` for a USB 3 hub's SuperSpeed half, so a USB 3 hub is two devnodes), bound by this package's own INFs to `xhci98.sys` as a hub FDO, with the devices behind it as its children; still no binding path for any OS hub driver (design record 13 section 10.11). **Amended 2026-10-10** (owner, after SweetLow's comment on issue 17; task 36.43): every composite device the bus splits also gets a composite-parent devnode under the project-owned `XHCI98\COMPOSITE`, bound by this package's own INFs to `xhci98.sys` as a composite FDO, with its function PDOs as its children, so one hub port is one devnode as on Microsoft's stacks; the bus still splits, configures and routes, and still no binding of `usbccgp.sys` or Windows 98's composite parent (design record 13 section 10.12) |
| Stock Windows 98 SE | A goal: the HCD loads, starts and binds devices on a Windows 98 SE install with no USB 2.0 stack. NUSB's and SweetLow's host-stack halves become inert under it. What a stock install still lacks is a mass-storage class driver, which NUSB's `usbstor.sys` and its `ntmap` layer supply and Windows ME ships; the HCD does not replace that. Phase 26 takes the reading (26-V.3), a checkpoint clause by the owner's decision of 2026-10-03 |
| Mass storage on Windows 98 SE | NUSB's, kept (owner, 2026-10-02, choosing this over an own storage driver). A drive letter on 9x comes from the IOS layer, and NUSB's `usbntmap.inf` binds `usbstor.sys`'s disk objects (`USBSTOR\GenDisk` and its siblings) to `DevLoader=*IOS` with `PortDriver=USBMPHLP.PDR`, the USB mapping port driver, with `NTMAP.SYS` and `USBNTMAP.SYS` beside it; NUSB's own install INF stamps `NTMAP.SYS` as `4.10.0.2227` and annotates it with hotfixes 242975 and 267304, a Windows 98 SE line, and annotates `USBMPHLP.PDR` and `USBNTMAP.SYS` as `WinMe` in its file list - the package author's notes, not a reading of the files, which were not examined (`legal-provenance.md` section 4, the NUSB 3.6 INF row; method static, text read, nothing executed). They are Microsoft's and this project does not redistribute them. An own BOT driver would still sit under that mapping, and an own IOS port driver is a VxD project with no use elsewhere. So `usbstor.sys` stays the BOT driver on every target, and on Windows 98 SE mass storage, UAS included, needs NUSB installed; on a stock install it cannot work and the release notes say so. Whether the 98 SE CD carries any of the mapping files is task 31-0's to read from the disc |
| Pool and DMA | The HCD allocates pool and its own common buffer. The miniport's "allocate no pool" rule stands for `src/`; for the HCD it is replaced by import-allowlist rows with Windows 98 export evidence, read in task 25.3, and a rule naming the sites that may allocate |
| Device names | `xHCI98 USB 3.x eXtensible Host Controller` for the controller and `xHCI98 USB 3.x Root Hub` for the root hub (owner, 2026-10-02; spelled xHCI98 by the owner 2026-10-03), as the INFs' device descriptions on every path |
| The root hub and both property tabs | Kept, all of it (owner, 2026-10-02, reversing a narrower answer given on the review earlier that day). The controller FDO creates a root-hub PDO under a **project-owned** hardware id, never `USB\ROOT_HUB` - which the OS's own `usbhub.sys` claims on every NT target and which would put Microsoft's hub driver back on top of this bus - and `xhci98.inf` binds that id to `xhci98.sys` itself, so one binary is both the controller's function driver and the root hub's, and every device PDO is a child of the root hub, where Device Manager users expect it. The controller's Advanced tab: the HCD answers the `USBUSER` request set `usbui.dll` sends to the controller devnode, and the INFs carry the two registrations the miniport's do - `EnumPropPages` to `sysclass.dll` on Windows 98, `EnumPropPages32` to `usbui.dll` on the NT paths. The root hub's Power tab: the root-hub devnode registers the hub property-page provider the OS INFs register for their own root hub, and answers the hub IOCTLs that page sends, read per target in task 25.4. `XHCISNAP` reaches the HCD through the controller's door. External hubs stay objects inside the bus (the hub decision above), so a device behind one appears under the root hub devnode, and the Power tab reports the budget the bus itself keeps. **Amended 2026-10-04 (task 33.4)**: each external hub is its own devnode with its own Power tab and door, and a device behind it appears under it (design record 13 section 10.11) |
| The miniport's virtual-hub values | Not supported (owner, 2026-10-02). `XhciVirtualHSHub`, `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid` exist to make usbport tell the truth about a root-port device's speed; the HCD has no usbport to lie to and reports every device at its true speed with no hub in the way, so there is nothing for them to switch. The HCD reads none of the three, its INFs write none, a value left behind by a miniport install has no effect, and the release notes say so (32.1). Which of the miniport's other values carry over - the log switches, the interrupt moderation value - is 25.1's list |
| SuperSpeed hubs on Windows 2000 | A vehicle is required, for now (owner, 2026-10-02, on the same review). Windows 2000 is a VM-only target and no QEMU device models a SuperSpeed hub, so Phase 30's "observed on both" has no vehicle today; the owner did not record an exception and revisits the question when Phase 30 opens. Until then Phase 30 cannot close, and Phases 31 and 32 wait on it. Revisited on 2026-10-04 ("SuperSpeed hubs on Windows 2000, revisited", below) |
| SuperSpeedPlus: USB 3.1 Gen 2 and USB 3.2 Gen 1x2 and Gen 2x2, to 20 Gbit/s | In `2.0.0.0`, untested if need be (owner, 2026-10-02, later the same day, reversing the roadmap line that had left it unassessed): the driver runs on every target to Windows 7, which has no USB 3 stack of its own, and newer chipsets with 10 and 20 Gbit/s ports often have no Windows 7 vendor driver either. A link that trains above 5 Gbit/s is accepted at its trained rate rather than sent back to USB 2.0. From the driver's side the step is small once Phase 29's Gen 1 path exists: the rate comes from the protocol capability's PSI dwords rather than a fixed table, bulk and interrupt framing are Gen 1's (1024-byte packets, burst to 16), and the new work is the SuperSpeedPlus isochronous companion with its 32-bit bytes-per-interval and the Endpoint Context's Max ESIT Payload Hi (task 29-A.6). Vehicles: QEMU models nothing above 5 Gbit/s; the P14s Gen 1's chipset controller (`8086:02ED`) advertises a USB 3.1 protocol with eight PSI dwords on ports 13-18 (`xhciqual/results/p14s-gen1-2026-07-25/PROBE.LOG`), but its published 10 Gbit/s connectors are the two USB-C 3.1 Gen 2 / Thunderbolt 3 ports, and neither source says which controller those reach - the chipset's ports or the Thunderbolt controller's own xHCI - so whether any P14s connector reaches 10 Gbit/s under this driver is 29-0's question and 29-E.2's reading; no 20 Gbit/s port is held (the B650M, whose chipset has one, is gone). What no vehicle reads is untested ground in the release notes (32.1) |
| USB Audio 2.0 | Not in this repository and not on this roadmap. A class driver of generic design, so that it serves machines on EHCI and vendor stacks as well as this one, built in its own repository; it needs nothing from xHCI, since UAC 2.0 runs at High Speed and binds by class through the standard URB contract |
| One bench session before the cut | **Superseded** on 2026-10-04 by "Cut before the bench" (below). All bench work moves to one combined bench session immediately before the `2.0.0.0` cut (owner, 2026-10-03). Phases 28, 29, 30 and 31 each close their V clauses and A tasks and stay open on their E.1 clause alone; work proceeds into the next phase while the earlier E.1 clauses wait - an explicit exception to the rule that no phase advances past an unobserved checkpoint, limited to the E.1 clauses. The session reads 28-E.1, 29-E.1, 30-E.1 and 31-E.1; each such phase closes only when its clause passes, and the cut (Phase 32) waits on the session. Supersedes the owner's instruction of 2026-10-02 to continue into Phase 29 while 28-E.1 waits. Accepted costs: Phase 30's SuperSpeed hub work, which no QEMU device models, is first executed on the bench; QEMU's SuperSpeed model is Phase 29's only runtime check until then; real-controller defects surface late and together. Mitigations in VMs: a strict mode in the `qemu` flavour checking xHCI slot and endpoint state preconditions before every command, real devices by `usb-host` passthrough, the SMP Windows 2000 guest with Driver Verifier, and reviews aimed at specification preconditions |
| Interrupt moderation default | 160 (40 us, Linux's long-standing value), **INF only** (owner, 2026-10-04): both INFs write 160 on every HCD install path, where they wrote 500 (125 us), the miniport's value from `1.1.1.0`; the code default `XHCI_IMOD_INTERVAL_DEFAULT` stays 4000 (1 ms), what an absent, unreadable or out-of-range value runs at, and the INF gate holds both files to 160 (`VAL-DEFAULT`). Evidence, the owner's ATTO runs on the P14s Gen 1 under Windows 98 SE (the MSSU10 over UAS at SuperSpeed, queue depth 1): at 8 MB, 181/181 MB/s write/read at 500, 211/221 at 160, 217/225 at 40. A Full-Speed audio stream was read again at 160 on a Windows 2000 guest before the change. `runs/run-28.md`, "The interrupt moderation default: 160" |
| The idle power policy (28.3) | Carried as a known limitation of `2.0.0.0` (owner, 2026-10-04): the bus never initiates selective suspend, of a device or of a hub port; it handles a suspend or resume it is asked for, or that a hub reports, the same "handle, don't initiate" ruling 27-A.1 took. The planned selective suspend work (`SUSP`, unnumbered) adds it (owner, 2026-10-05; design record 14); since 2026-10-09 it is Phase 36's 36.28 to 36.41, off by default. Built in Phase 36 (stage A and stage B, one switch, `XhciSelectiveSuspend`, 0 by default); from `2.3.0.0` the release notes replace the limitation with the switch, and at 0 the ruling above still describes the bus (36.20). Hubs, composite parents and the controller are still never suspended |
| `2.2.0.0` in three phases | Split on 2026-10-05 (owner), the day it opened: Phase 34 the registry values and the two Device Manager pages, Phase 35 polled event delivery, Phase 36 selective suspend, standby resume, the audio wedge, the docs and the cut. One release at the end; each phase closes on its own checkpoint. Task ids renumbered from the single Phase 34: 34c.N to 35.N, 34.4 to 36.1, 34.5 to 36.2, 34.6 to 36.3, 34.7 to 36.4, 34a and 34b to 36a and 36b. **Amended later the same day**: Phase 34 is cut on its own as `2.1.1.0` (the row below), and the four freed ids were taken again for Phase 34's new tasks - 34.4 the port switchover, 34.5 its docs, 34.6 its cut - no tracked file or published `readme.txt` having cited the old ones; renumbered 34.3, 34.4 and 34.5 when task 34.3 (the Vista/7 Advanced tab's bandwidth) was removed, below |
| `2.1.1.0` at Phase 34 | An interim release cut at the end of Phase 34 (owner, 2026-10-05), amending the split's one release at the end: the registry values, the two Device Manager pages and the port switchover ship as `2.1.1.0`, and `2.2.0.0` carries Phases 35 and 36. Phase 34 takes its own docs (34.4) and cut (34.5); 36.3 and 36.4 cover Phases 35 and 36. **Amended 2026-10-06**: the row below ("Phases numbered when opened") takes the numbers 35 and 36 off polled event delivery and selective suspend; their ids 35.N, 36.N, 36a.N and 36b.N are now `POLL.N`, `SUSP.N`, `SUSPa.N` and `SUSPb.N` |
| Phases numbered when opened | Owner, 2026-10-06: planned work carries no phase number until the owner starts it. Polled event delivery and selective suspend, drawn up as Phases 35 and 36 on 2026-10-05, become "Planned work" sections with placeholder ids (`POLL`, `SUSP`), still meant for `2.2.0.0`, and take the next free number when opened. Commits and memories before this change call them Phases 35 and 36 |
| A phase's closing task is `<phase>.final` | Owner, 2026-10-09: the task that builds, checks and cuts a phase's release is named `<phase>.final` and stays last in the list; a task added later takes the next free number above it, so nothing is renumbered. Phase 36's is the first (`36.final`, 36.17 in commits before then); earlier phases keep their numbers |
| Phase 35: SuperSpeed falling back to USB 2.0 on Sunrise Point | Owner, 2026-10-06, from the owner's E460 and a tester's HP EliteBook 850 G5, both Intel Sunrise Point-LP (`8086:9D2F`): SuperSpeed devices enumerate at High Speed or not at all, and a USB 3 hub shows only its USB 2.0 half; the P14s Gen 1 (`8086:02ED`) is unaffected. The Missing-CAS lead and its gate `XhciMissingCas` were planned first; 35.0's reading (owner, the same day) found the link trained and the device dropped by this driver's speed decoding instead, and the owner re-planned 35.1-35.4 around that fix, ungated, in `2.2.0.0`, the `XhciMissingCas` work dropped (issue 11). With it, `XhciIntelPortSwitch` 2 (35.5, the switchover row below). Planned as its own interim release, `2.1.2.0`, before the tolerance work joined the phase |
| Phase 35: controller tolerance (`35-T`) | Owner, 2026-10-06, from a tester's AMD AM5 report (a HID mouse randomly stops; moving it to another port helps for longer) and the review of Linux's xHCI quirks that followed: no AM5 or X570 controller id but `43F7` carries a Linux quirk flag (and `43FC`/`43FD` go to a Promontory 21 glue driver the mirror lacks), so Linux's robustness on them is taken, as a hypothesis, to be behaviour it applies to every controller, where this driver is stricter and has lost usbport's URB timeout. Joins Phase 35 and ships with it, not a release of its own; all seven tolerant behaviours (35-T.1 to 35-T.7) land together, not one at a time after a diagnostic reading; spec-legal behaviours that cost nothing on a compliant controller are unconditional, and only the interrupt-interval cap is gated - on every AMD controller (vendor `1022`), wider than Linux's list, by `XhciIntervalCap` 0/1/2 as `XhciIntelPortSwitch` is; the Average TRB Length change is not made by default but offered as the switch `XhciAvgTrbEsit`, written by the INFs at 0, off, so a tester can compare (owner, the same day, after review); a code nothing claims and a halt with no TD cycle the device, a replug done by the bus, rather than recover the endpoint in place (owner, the same day, after review); the budgets and intervals are constants, not registry values, and one switch, `XhciTolerance` (1 default, 0 every unconditional 35-T behaviour off, back to `2.1.1.0` handling), is the field escape (owner, the same day); the lost-interrupt backstop (35-T.1) is this phase's, and the planned polled delivery reuses its peek. Each taken from options with a recommendation. Design record 17 |
| Phase 35 is `2.2.0.0` | Owner, 2026-10-06, once the tolerance work joined it: the size of the change makes Phase 35 a minor release, `2.2.0.0`, not the interim `2.1.2.0`, and its branch is renamed `2.2.0.0` to match. Polled event delivery and selective suspend, meant for `2.2.0.0` until then, carry no version: each takes one when the owner opens it. The rows above that name `2.1.2.0` for Phase 35, or `2.2.0.0` for the planned work, record what was decided at the time |
| `2.2.0.0`: cut before the bench | Owner, 2026-10-06, as for `2.0.0.0` ("Cut before the bench", below): 35-E is read on real hardware after 35.7's cut, on the `2.2.0.0` asset rather than a release candidate, and the documents are written as if it passes. A 35-E outcome not met is changed and recut before the release is published. Taken after 35.2 had read SuperSpeed on the E460 with 35.1 and 35.3 (2026-10-06), which leaves 35-E the counted campaign, the P14s Gen 1, the B490 and 35-T on real hardware; the cost accepted is that a 35-T regression found there is a recut rather than a fix before the cut |
| The Intel port switchover | Phase 34, task 34.3 (owner, 2026-10-05, from a tester's report of Ivy Bridge machines): the HCD routes the Intel 7-, 8- and 9-series and C610 switchable connectors to itself, superseding the miniport's Phase 4 decision to leave the registers alone; a device-id gate of six, and the opt-out `XhciIntelPortSwitch` at 1. Design record 16. **Amended 2026-10-06** (owner; task 35.5): `XhciIntelPortSwitch` 2 performs the switchover the moment an Intel controller is detected, the gate bypassed; 1 and other nonzero numbers keep the gate, the default stays 1. Value 2 is at the user's own risk - on a controller without the switch registers it writes registers of unknown meaning - and the README and release notes say so; the owner chose to inform rather than add a register-layout check. **Amended 2026-10-09** (owner; task 36.42, after SweetLow's issue 17 comment 6082858626: a connector left unrouted stays on EHCI and works): the default is **0**, in both INFs and when the value is missing, of another type or unreadable, reversing the default 1 of 2026-10-05; 1 and 2 keep their meanings. A user script, `portsw.vbs`, shows and sets it; an existing install keeps its value under the don't-overwrite flag, and the release notes say how to change it |
| Polled event delivery | Planned, unnumbered (`POLL`; drawn up as Phase 35) (owner, 2026-10-05, first added as Phase 34's stage C; design record 15): a switch, `XhciInterruptMode`, chooses automatic (0, the default: the line interrupt where the controller has one, an adaptive poll of the event ring where it has none), the line interrupt (1, failing as today without one) or polling (2), with 3 and above reserved (MSI, not scheduled) and run as 0, so an MSI- or MSI-X-only controller, which neither primary target can serve by interrupt, can be driven; the idle period `XhciPollIdleMs` defaults to 50 ms and the active period `XhciPollActiveMs` to 1 ms, neither may be 0, the idle period is held to 5000 ms and the active to the idle, and all three join 34.1's install rule. The delivery in effect and its reason are captured by `XHCISNAP` and the DebugView log. Each taken from options with a recommendation; the numbering replaced the same day's "0 line, 1 polling, 2 MSI" |
| Polled event delivery joins Phase 36 | Owner, 2026-10-08, after issue 12: the `POLL.*` tasks become 36.5 to 36.10, 36.13 and 36.14. Record 15 is amended (section 4.2, decision 6): automatic mode also moves to polling when a line interrupt is proven dead at run time, which replaces 36.4's stalled-event watchdog. 36.4 keeps the BUSY retry |
| Selective suspend joins Phase 36, off by default | Owner, 2026-10-09: the planned selective suspend work (`SUSP.*`) becomes 36.28 to 36.41, its docs join 36.20 and its cut and install legs 36.final. `XhciSelectiveSuspend` defaults to **0**, replacing the default 1 of 2026-10-05 (design record 14 sections 7 and 11): off, it is today's behaviour bit for bit, so the work can ride in `2.3.0.0`; a user turns it on with 1. Stage B still ships with stage A, so `2.3.0.0` waits for both |
| MSI on Vista and 7 | Owner, 2026-10-08: added to Phase 36 for both architectures (36.15, 36.16), designed as a record 15 amendment. Owner, 2026-10-09, after 36.5's review found no safe way for the x86 binary alone: amd64 through `MmGetSystemRoutineAddress`; x86 through `xhcimsi.sys`, a small NT 6-only helper driver installed only by the Vista and 7 x86 INF section, which imports `IoConnectInterruptEx` and hands it to `xhci98.sys` over a named device and a versioned internal device control (design record 18). `xhci98.sys`'s x86 image gains one import, `IoGetDeviceObjectPointer`, with stock Windows 98 SE precedent, and none Windows 98 lacks (owner, 2026-10-09). Without the helper, the requirements filter keeps only the line configurations, so x86 keeps the line, the routing quirk and polling; the same filter makes `XhciInterruptMode` 1 force the line on Vista and 7, x86 and amd64 (owner, 2026-10-09), and mode 1 with no line offered and no helper fails the start as on XP. The Vista and 7 x86 install asks for a restart, so the boot-start helper is active at once (owner, 2026-10-09). **Amended by 36.53 (owner, 2026-10-11):** `XhciInterruptMode` 1 is the line only on every system, failing the start (Code 10) when only a message is offered; 3 is a message only on Vista and 7, failing the start when none is granted or it does not connect, and on 98 SE to XP; 36.6-36.8's and 36.15's lines, which read 3 as 0, are kept as history (design records 15 and 18, decision 8) |
| Phase 36 is `2.3.0.0` | Owner, 2026-10-08: the routing quirk, polled event delivery and MSI make Phase 36 a minor release, so the branch `2.2.1.0` is renamed `2.3.0.0`, and that is the target version. Commits before the rename were made on `2.2.1.0` |
| The other `1.2.0.0` limitations (28.3) | The NUSB stop crash, the Windows 98 churn wedge and the Windows 7 disable hang are each re-measured under the HCD and recorded as gone, carried or new (owner, 2026-10-04). A survivor is carried in the release notes and does not block the cut |
| 28-E.1's Full-Speed hub clause | Taken at the bench, not recorded as untested ground (owner, 2026-10-04): a USB 2.0 hub forced to Full Speed by an ADuM full/low-speed isolator in front of it, in place of a USB 1.1 hub (owner, the same day, superseding the plan to buy one) |
| Checkpoints of Phases 28 to 31 | Pre-approved (owner, 2026-10-04): each closes once its review is clean, the x86 and amd64 builds, gates and host tests pass, every roadmap VM leg passes with its package hash recorded, and no regression is open on an earlier phase. Scope rulings, waivers, push, tag and the cut stay the owner's |
| The run plan for Phases 28 to 31 | Every finished line is merged into the integration branch `p28-31-int` first, and the Phase 28 to 31 VM legs - 28-V.1's seven other guests, Vista x86 and x64 included, 29-V.1, 31-V.1 and 31-V.2 - are read once, on that merged build (owner, 2026-10-04). The early 28-V.1 pre-read on `1ed1ba6` was paused for this. Every leg runs on development host A; there is no second host: a development host B, planned that morning to take the Vista x86 and x64 legs, was dropped by the owner the same morning (about 10:30), and those legs ran on host A |
| `xhciuas` on Windows 98 SE | Its INF names NUSB's `USBNTMAP.SYS` as an upper filter, by name, and never ships it, so UAS on Windows 98 SE depends on NUSB's mass-storage component as Bulk-Only does (owner, 2026-10-04; the mass-storage row above). The INF has no temp-file copy, so the first UAS install needs no restart |
| Zero-size isochronous endpoints | An isochronous endpoint with `wMaxPacketSize` 0 and `bMaxBurst` 0 is accepted as zero-bandwidth, not refused (owner, 2026-10-04); the implementation is on branch `p29-spec` (`4cef6d3`: configured with MPS 0), merged into `p28-31-int` at `9077edf` |
| The P14s Gen 1 connector question (29-0) | Deferred to the combined bench session, and not a clause (owner, 2026-10-04): it serves only 29-E.2, the optional Gen 2 reading. If a Gen 2 device is at hand at the bench, the connector is read there with `XHCISNAP`; otherwise 29-E.2 is recorded as not taken |
| SuperSpeedPlus parent-hub fields | Implemented in `2.0.0.0`, not deferred (owner, 2026-10-04, about 12:15): a SuperSpeed device on a lower-rank link behind a SuperSpeedPlus hub gets the Slot Context's Parent Hub Slot ID and Parent Port Number, the hub's link rank tracked from `PORTLI` and the extended port status. On branch `p29-spec`, merged into `p28-31-int` at `9077edf`; built from the specification, no vehicle models such a hub |
| A departed PDO and its port (option 1) | A gone device PDO holds its port only until a relations answer has reported it missing; the port then enumerates a new device with a new PDO under the same location instance id, while the old PDO waits for its REMOVE or the controller's release, as usbport's children do (owner, 2026-10-04, about 11:40, to be taken if the Windows ME pull-during-install reading showed the REMOVE lost on a healthy guest, which it did). `7f1b4e0`, with `791f9f8` (a START on a departed PDO refused; a command failing on a departed device no controller reset) and `cc9da33` (a PDO removed while listed released at the relations answer); `runs/run-28.md`, "The device-pull fixes" |
| Device PDOs and `SurpriseRemovalOK` | FALSE, as Windows 2000's `usbhub.sys` reports, so the hot-plug applet lists a USB stick again (owner, 2026-10-04, about 14:40); a split function of a composite device keeps TRUE, and so does its composite-parent PDO (36.43, orchestrator decision 2026-10-10, owner confirmed 2026-10-10: the hot-plug applet keeps listing no composite device). `9001ebd`, `8993884` (review); found by the upgrade leg on Windows 2000 (`runs/run-28.md`, "The upgrade from `1.2.0.0`") |
| Windows 7 on the bench | Not benched (owner, 2026-10-04, about 14:05: "We won't bench Win 7"). The E460's 32-bit Windows 7 halves of 28-E.1, 29-E.1, 30-E.1 and 31-E.1 are dropped; Windows 7, both architectures, is a virtual-machine target only for `2.0.0.0` |
| The Windows 7 disable hang (28.3) | Removed from the `2.0.0.0` limitations, recorded as gone with no metal caveat (owner, 2026-10-04, about 14:10): a `1.2.0.0` miniport and usbport issue, not reproduced under the HCD in QEMU, five controller disable and enable cycles on Windows 7 x86 and x64 (`runs/run-28.md`, 28.3) |
| Windows ME, a UAS drive as the first storage device | Option C, carried as a limitation of `2.0.0.0` (owner, 2026-10-04, about 15:20): on a fresh ME install the drive shows Code 2 until ME has copied its own `USBNTMAP.SYS` and `USBMPHLP.PDR`, which it does when its first ordinary stick installs. An `xhciuas.inf` copy of the two files through `LayoutFile` cured ME but made Windows 98 SE prompt twice, since the 98 SE CD's `layout.inf` names neither file; it was parked on branch `p31-meuas-copy` (`dc5251f`, deleted after the cut by the owner's decision), and option B, an ME-only compatible id to gate it, is for a later release (`runs/run-31.md`, "Windows ME: a UAS drive first") |
| Windows ME: the controller re-enable, and a pull during an install | Carried as limitations of `2.0.0.0` (owner, 2026-10-04): re-enabling the controller with a device attached may hang ME, the HCD's own (ME's Microsoft UHCI stack re-enables with devices attached), so the release notes say to unplug first; the fix, first planned as `2.0.0.1`, is task 33.1 of `2.1.0.0` (`docs/issues/09-me-controller-reenable-stopped-pdos.md`). A pull while ME is installing the device freezes ME under its own UHCI stack too, so it is ME's behaviour, not the HCD's (`runs/run-28.md`) |
| Cut before the bench | The `2.0.0.0` cut was taken before the combined bench session, not after it (owner, 2026-10-04, about 16:40: "I will do the bench session after the cut; if it works we release that, otherwise change and recut"); supersedes "One bench session before the cut" (above). The documents were written as if the bench passes, and 28-E.1, 29-E.1, 30-E.1 and 31-E.1 were read by the owner on real hardware after the cut, on the ThinkPad E460 and the ThinkPad P14s Gen 1 under Windows 98 SE (the README's photos are the P14s); the owner reports no issue beyond the known limitations. Windows 7 was not benched ("Windows 7 on the bench", above) |
| SuperSpeed hubs on Windows 2000, revisited | Released untested (owner, 2026-10-04, with the cut), revisiting the 2026-10-02 row that required a vehicle: no vehicle exists, so the Windows 2000 half of Phase 30's checkpoint is untested ground, built from the specification, and the release notes say so |
| Gaps dropped from `2.0.0.0` | Two task gaps ticked with the gap recorded rather than closed (owner, 2026-10-04): 29-A.1's registry value restoring the SuperSpeedPlus refusal (SuperSpeedPlus is accepted at its trained rate; `HCD_HOLD_REASON_SSP_REFUSED` in `src\hcd.h` is unused), and 31-0's unread sources - the xHCI 4.12 streams rows marked "to verify", NT `disk.inf`'s `GenDisk` binding and QEMU's `hw/usb/dev-uas.c` - not needed for `2.0.0.0` since UAS passed on every target. Neither is scheduled |
| Issue 18: USB 2.0 speed IDs behind a hub | Orchestrator decision, 2026-10-10, on Codex's recommendation (session `01a1261e`), owner confirmed 2026-10-11; the owner asked for the fix in the `2.2.0.3` test build (task 36.49, issue 13): a general rule for any controller, on by default, rather than a VL805 gate - a Full- or Low-Speed device behind a hub on a USB 2.0 group with a speed table takes the default ID where the table names neither that speed nor that ID. The PCI-id and capability-pattern gates were set aside because HSO and IHI describe the root port, not downstream IDs, and the topology is the stronger evidence; the collision check keeps a listed ID winning. `XhciUsb2HubSpeedFallback` `0` restores the refusal; every INF writes it at 1 since 36.51 (owner, 2026-10-11), no INF before |
| Every registry value written by the INFs | Owner, 2026-10-11: every registry value the shipping driver reads is written by every install path of both INFs, at the driver's own default and under 34.1's don't-overwrite flag, so every setting is visible to a user and behaviour is unchanged. Supersedes the rule that `XhciWideMap`, `XhciSelectiveSuspend` and `XhciUsb2HubSpeedFallback` were set by hand and written by no INF; they are written at 0, 0 and 1 (task 36.51). A value whose absence meant something no written number does would be listed and left unwritten; none exists |

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
update, `2.1.0.0`; Phase 34 is `2.1.1.0`, an interim release: the registry
values, the Device Manager pages and the Intel port switchover; Phase 35 is
`2.2.0.0`: SuperSpeed devices falling back to USB 2.0 on Intel
Sunrise Point, `XhciIntelPortSwitch` 2, and the controller tolerance work
(35-T). Phase 36 is issue 12's E7250 interrupt routing, with polled event
delivery, which joined it on 2026-10-08, MSI on Vista and 7, and three of
issue 17's items (2026-10-09), and selective suspend, off by default
(36.28 to 36.41, 2026-10-09; its planned ids were `SUSP.n`).
The order is deliberate:
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
phase leaves to later tasks. **Closed 2026-10-03:** six review
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
- [x] 26-A.5 URB dispatch: `IOCTL_INTERNAL_USB_SUBMIT_URB` and the function set `win98-wdm.md` lists, pipe handles, select configuration and interface through Configure Endpoint, control, bulk, interrupt and isochronous TD construction over the bus's own mapping, abort and reset pipe, cancellation, the interval and cadence taken from `bInterval` directly. Done 2026-10-03 (`runs/run-26.md`, "Batch (c)", c13 to c16; review rounds 1-21 closed): control, bulk and interrupt through the transfer engine and the map pump, SELECT_CONFIGURATION, SELECT_INTERFACE, ABORT_PIPE, RESET_PIPE, cancellation, GET_CURRENT_FRAME_NUMBER, refusals completed at the next tick; a USB mouse, keyboard and tablet work on both primaries, every unplug pattern included (c12); the Waiting list in place of ERROR_BUSY (`08c0d5a`); the transfer MDL mapped alone and bounded to its byte count, as usbport maps it; isochronous transfers (`fb8d8f0`), admitted and published with audio playing on both primaries (c16)
- [x] 26-A.6 the rest of the PDO contract 25.2 found: `GET_PORT_STATUS`, `RESET_PORT`, `GET_DEVICE_HANDLE`, the `USB_BUS_INTERFACE_USBDI` versions each target's drivers query. Done 2026-10-03 (`runs/run-26.md`, c14): `GET_PORT_STATUS`, `RESET_PORT`, `CYCLE_PORT`, `GET_BUS_INFO` and `GET_HUB_COUNT` served (`1fa0cc2`); storage works on both primaries with `RESET_PORT`. Not needed on the primaries (design record 13 section 6.5): no class driver read sends `GET_DEVICE_HANDLE`, and none on Windows 98 SE or 2000 queries `USB_BUS_INTERFACE_USBDI`; the USBDI versions move to 28-A.1
- [x] 26-A.7 composite splitting: per-function PDOs, IAD grouping, and the grouping rules for devices that carry no IAD - chiefly audio, where an AudioControl interface and the AudioStreaming and MIDIStreaming interfaces that follow it are one function and a HID interface beside them is its own, the rule every UAC 1.0 unit in `test-equipment.md` needs since none of the five has an IAD - plus routing and the function ids; those units and the IAD-grouped X4 are the specimens. Done 2026-10-03 (`runs/run-26.md`, c15 and c16; design record 13; review rounds 19 and 20): the C-Media 0D8C:0014 (no IAD) split into an audio function (AudioControl and two AudioStreaming) and a HID function on both primaries, each bound and working, with no composite parent; c14's Windows 2000 `usbaudio.sys` bugcheck gone, and playback through the audio function (c16). The bench's UAC units and the X4 are 26-V.1's
- [x] 26-A.8 the door: `USBUSER` for `usbui.dll` and `XHCISNAP` on the controller, the hub IOCTLs of 25.4 on the root hub for its Power tab, the log ring moved in with its sinks and switches, `XHCISNAP` taught the second driver; and one sink the miniport could not have: a PASSIVE-level flusher that emits the ring through `DbgPrint` continuously, because the HCD owns a PASSIVE context - its own system thread or work item - where the miniport on Windows 98 never reached PASSIVE between `StartController` and shutdown. The ban stands on what it always stood on: per-line `DbgPrint` from DPC or ISR context under DebugView bugchecks Windows 98 on real hardware on three device classes (design record 08), and owning the driver object removes the door problem, not that one; the flusher records from any IRQL and emits from PASSIVE only, behind the same verbosity switch. Done 2026-10-04 (`runs/run-26.md`, c17, "26-V.1" and "26-V.2"): written and merged (`0e470b9`, review round 21's minors in `2df17c3`, round 22's fixes in `370c135`); the controller's Advanced tab and the root hub's Power tab read on both primaries, on Windows 98 SE under NUSB 3.3 and under SweetLow's stack and on Windows 2000; `XHCISNAP -probe` answering "the channel is live" with the channel on and "MINIPORT DECLINED" with it off, and a full `-o` report on Windows 98 SE under both stacks and on Windows 2000. `XHCISNAP`'s closing text still names "Windows' USB port driver", a stale string left for a later change
- [x] 26-A.9 host suites for every pure piece: URB parsing, id generation, the enumeration state machine, the composite split with and without an IAD, the TD builder over the new mapping. Done 2026-10-03 (merged `8c324e7`): `test_xfer` and `test_iso` restored, `test_td` new over the chunk planner's and map pump's lists, `test_enum` and `test_func` extended; one defect found and fixed (an empty isochronous block took Frame IDs)
- [x] 26-A.10 the matrix's expectations for the HCD, before any 26-V run: `scripts/vm-matrix/matrix.psd1` encodes the miniport's behaviour, not a device's - the `advance endpoint speed mismatches` row asserts the High-Speed lie on a root port with the switch off, the switch-aware rows assert the virtual hub's extra tier and usbport's naming of it, and the counters are read at the miniport extension's offsets - so a literal field-for-field comparison would fail a correct HCD. 25.5's plan becomes a second expectation set: the usbport mismatch counter has no HCD counterpart and leaves the executable expectations altogether, with its inapplicability recorded as a comment beside the row - `inert` would not do, because the harness resolves an `inert` label to a real counter field and reads it, so an absent field is an error rather than a reading; in its place each device row carries an expected speed and the harness asserts the HCD's decoded port speed and the speed it programmed into the Slot Context against it, so a wrong speed report is a `FAIL` and not a silence; the virtual-hub tier rows go; every other usbport-only counter is dropped or mapped to the HCD's; the offsets are regenerated for the HCD's extension; and the verdict rules of design record 06 are kept as they are. Done 2026-10-04 (`runs/run-26.md`, "26-A.10"): `matrix-hcd.psd1` written with a speed per device row and the usbport-only and virtual-hub labels refused (`a219fe1`, selftest 460 checks); the HCD publishes the counter block `gen-offsets` reads (`a1be180`); the set's first run, on overlays of both primaries' golden images with `40efd31`, at parity - every row PASS, NODRIVER where the row expects no driver, or EXCLUDED by the set on Windows 98 SE - once `f9f57fe` set `usb-bot` and `usb-uas` to High Speed: QEMU 11.1 presents both at 480 Mb/s, the HCD reported that speed truly, and the first run had failed both rows on the expectation alone. `f9f57fe` also gave each target its own `ExtraArgs`
- [x] 26-V.0 the scaffold on both primaries: `xhci98.inf`'s undecorated path on Windows 98 SE and its `.NTx86` path on Windows 2000, the empty driver installing with no prompt beyond Windows' own request for its in-box files (the owner's wording, 2026-10-03: Windows 98 SE asks for its CD for `usbd.sys`, which the INF fetches through `LayoutFile`, on a base that lacks it) as `xHCI98 USB 3.x eXtensible Host Controller` and showing Code 10; the amd64 INF's first install is 28-V.1's. Read 2026-10-03 (`runs/run-26.md`, "26-V.0"): Windows 2000 installed with no prompt and showed Code 10; Windows 98 SE showed Code 10 after its restart, its install asking only for the Windows 98 CD for `usbd.sys` (review of 26-A.2, round 1, note 12) - which the reworded clause allows; ticked on that reading
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
- 27-A.3: done, with review rounds 1 and 2 on the batch.
- The Windows 98 SE HID-unplug stall: 27-V.1 found it and five review rounds closed it (`18ecc83`).
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

- [x] 27-A.1 the hub class: hub descriptor, port power, the status-change pipe, per-port reset, enable, suspend and resume, port state machines, hub depth, the multi-TT alternate setting. Done 2026-10-04 (`runs/run-27.md`, "Batch (a)"; `23e7715`, review rounds 1 and 2 in `b5ed0c4` and `900dc83`): a hub is brought up by the bus and never gets a PDO - SET_CONFIGURATION, SET_INTERFACE(1) for a multi-TT hub, the hub descriptor, one Configure Endpoint with Hub, ports, TTT and MTT, port power, a GET_STATUS per port, the status-change transfer armed; its ports run the `xhci_enum.c` machine through hub class requests (three attempts, then `CLEAR_FEATURE(PORT_ENABLE)`), with a fresh `C_PORT_RESET` required and an elapsed-time debounce; the depth checked at the topology attach; pure logic in `xhci_hub.c`, host suite `test_hub`; on guests, the hub group of 27-V.1 on both primaries. Suspend and resume are handled, not initiated, by the owner's decision of 2026-10-04: the bus never suspends a hub port - selective suspend, hub ports included, is 28.3's idle-policy decision - but a port its hub reports suspended is resumed (`ClearPortFeature(PORT_SUSPEND)`, the resume signalling and recovery waited, USB 2.0 7.1.7.7, to transcribe) before it is reset or anything else is asked of it, and a `C_PORT_SUSPEND` on a connected, enabled port - a finished resume, a remote wake among them - is cleared with no re-enumeration (`XhciHubPortDecide`, `XhciHubResumeBeforeReset`, `XhciHubResumeProgress`; `test_hub`'s `test_suspend`)
- [x] 27-A.2 topology: `xhci_topo.c` fed by the bus's own hub traffic, Route String, parent slot and TT fields per design record 02, Full and Low Speed behind High-Speed hubs, a Full-Speed hub behind a High-Speed one. Done 2026-10-04 (`runs/run-27.md`, "Batch (a)"; `23e7715`, `fa6f199`): each device behind a hub takes its Route String, root port, tier and TT fields from `xhci_topo.c` through one Slot Context builder, with the instance id `(route << 8) | root port`; 16 nodes, SET_INTERFACE on an attached node and `C_PORT_ENABLE` with enable 0 as a departure close 27-A.4's gaps G1 to G3; CLEAR_TT_BUFFER on EP0 recovery, a bulk reset, an Address Device transaction error and the quiesce. On guests, the five-tier chain behind QEMU's Full-Speed hub addressed and bound to tier 5 on both primaries (27-V.1). The High-Speed hub and TT paths rest on the host vectors and are 28-E.1's
- [x] 27-A.3 removal: a hub pulled with devices beneath it, the subtree's PDOs and slots, a device pulled mid-transfer, the orderly and the surprise paths on each primary. Done 2026-10-04 (`runs/run-27.md`, "27-A.3" and "The Windows 98 SE HID-unplug stall"; `b5ed0c4`, then `26e7cb6` to `53b43c0`, merged `18ecc83`; review rounds 1-5 on the fix): one teardown, `hcdSubtreeGo`, in design record 13 section 10.5's order as corrected - freeze; the PDOs reported missing first; endpoints stopped and a departed device's URBs parked on its PDO, completed CANCELLED at the client's cancel, its ABORT_PIPE horizon (64-bit submission stamps) or the PDO's stop, surprise removal or removal; slots disabled leaf-first; hubs pruned deepest-first. Read: the churn's first hub pulled with the five-tier chain beneath it on both primaries (27-V.1); a keyboard, a mouse and a stick unplugged on Windows 98 SE, the REMOVE within about 1 s each; on Windows 2000 SURPRISE_REMOVAL and REMOVE within about 1 s, a stick pulled mid-copy of a 256 MB file included. The orderly path with a hub beneath it (`runs/run-27.md`, "The closing legs", on `981f56b`; a hub on root port 2 with a mouse at `2.2` and a stick at `2.3`): on Windows 2000 the root hub's Disable (QUERY_REMOVE then REMOVE for all, the transfer identity 575 = 569 + 6, Enable bringing all back), the controller's Disable and Enable and its Uninstall and rescan, each with `fc /b` clean; on Windows 98 SE the root hub's "Disable in this hardware profile" (STOP, not REMOVE, 68 = 64 + 4, START on re-enable) and the controller's disable and enable, `fc /b` clean. The Windows 98 SE audio-unplug fatal 0E at 0028:C002A3A7 - the configuration manager sends a removed PDO QUERY_DEVICE_RELATIONS and a second IRP after its REMOVE, and the PDO had been deleted inside the REMOVE since Phase 26's `27063e1` - is fixed in `c038326` (a removed PDO is deleted at the next BusRelations answer): 20 of 20 audio unplugs on two guests, where the baseline faulted on the first. Open, carried to 28.3: on Windows 2000 an audio device unplugged mid-playback receives no REMOVE, as on Phase 26's `a7ddbfa`
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

- [x] 28-A.1 the per-target deltas of 25.2 for XP onward, and Windows ME under SweetLow's stack with its own `usbccgp.sys` left unused. Done 2026-10-04 (`runs/run-28.md`, "28-A.1 - the per-target deltas for NT 5.1 onward"; `39d6105`, review fixes in `fe8480b` and `196c3c3`, merged at `5fdb9af`): `USB_BUS_INTERFACE_USBDI` versions 0 to 3, `GET_DEVICE_HANDLE`, `GET_TOPOLOGY_ADDRESS`, the idle notification held, `SYNC_RESET_PIPE` and `SYNC_CLEAR_STALL`; composite audio bound on all six NT guests, and split by the HCD itself on ME. The three `QUERY_INTERFACE` GUIDs the bus does not answer are left unanswered by design, as the stock hub and composite drivers leave them
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
cut, its Windows 7 half dropped. Review: the integration's nine rounds and
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
- [x] 29-A.1 port classification: USB 3.x ports managed and powered, the companion pairing, the all-SuperSpeed controller accepted, the rate read from the PSI dwords apart from the speed class, SuperSpeedPlus accepted at its trained rate. Done 2026-10-04 (`runs/run-29.md`, "29-A.1 to 29-A.6 - what was built"; `cd65bda`, review fixes in `017048c` and `0911520`, merged at `5d67641`): all of it, with `test_caps`' vectors for Gen 1, Gen 2x1, Gen 1x2 and Gen 2x2; the all-SuperSpeed controller is untested ground. Dropped from `2.0.0.0` by the owner's ruling (decisions table, "Gaps dropped from `2.0.0.0`"): the registry value restoring the SuperSpeedPlus refusal and its host vectors - no value restores it, and `HCD_HOLD_REASON_SSP_REFUSED` in `src\hcd.h` is unused
- [x] 29-A.2 the link state machine. Done 2026-10-04 (`runs/run-29.md`, "29-A.1 to 29-A.6 - what was built"; `cd65bda`, merged at `5d67641`, then `81d3942` and `2943dfe`): `xhci_link.c` (host suite `test_link`) - hot and warm reset with the hot-to-warm conversion, SS.Inactive, Compliance and CAS recovery within a bounded warm-reset budget, U3 resume; QEMU's links trained to U0 on every guest
- [x] 29-A.3 SuperSpeed enumeration. Done 2026-10-04 (same section; `cd65bda`, merged at `5d67641`; zero-bandwidth isochronous endpoints `4cef6d3`, merged at `9077edf`): EP0 at 512 bytes, the BOS at SuperSpeed only, burst, Mult and Max ESIT Payload from the companion, the xHC's Configure Endpoint verdict as the URB status; storage and UAS at 5000 Mb/s on all ten install legs
- [x] 29-A.4 transfers at SuperSpeed. Done 2026-10-04 (same section; `cd65bda`, merged at `5d67641`): bulk, interrupt and isochronous TDs with burst, 1024-byte endpoints and the TD Size rule; bulk read on every install leg; SuperSpeed isochronous built from the specification on host vectors, untested ground in the release notes
- [x] 29-A.5 fallback in both directions, and the active hold that sends a trained device back to USB 2.0 under the release rules of `future-plans/superspeed-storage-behind-a-switch.md` section 6.1, tightened to the held device's identity. Done 2026-10-04 (same section; `cd65bda`, `6fceeec`, review fixes in `017048c` and the integration's rounds to `c0f9ad6`): `HcdHoldRequestUsb2`, paired, unidentified and orphan holds, release only when the held device is seen on the companion and leaves, re-armed with `PLS` = RxDetect and `LWS`; seven counters; host vectors only, since no guest has a non-streaming controller or a link that fails to train
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
- [x] 30-A.1 the SuperSpeed hub inside the bus, a SuperSpeedPlus hub accepted at its trained rate. Done 2026-10-04 as code and host vectors (`runs/run-30.md`, "30-A.1 - the SuperSpeed hub inside the bus"; `034a119`, review fixes in `45b3a63`, merged at `63cba23`; parent-hub fields `b6e569e`, merged at `9077edf`): `xhci_sshub.c` and `hcd_sshub.c` - `SET_HUB_DEPTH`, the type 0x2A descriptor, Configure Endpoint with no TT fields, `BH_PORT_RESET` within the warm-reset budget, SS.Disabled ports re-armed on a backoff, the extended port status's rates, a pairing counter for the two halves. No QEMU device models a SuperSpeed hub
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
- [x] 31-A.1 primary streams in the bus. Done 2026-10-04 (`runs/run-31.md`, "31-A.1 - primary streams in the bus"; `506d1ac`, review fixes in `3611d81`, merged at `6f9f24d`; the reset-loop fix `c4ec1c3`, merged at `fe2577b`): `IOCTL_XHCI98_OPEN_STREAMS` and `_CLOSE_STREAMS`, one pipe handle per stream, stream-aware Configure Endpoint and Set TR Dequeue, abort and close; host suite `test_stream`; 16 streams per endpoint under `usb-uas` on every install leg
- [x] 31-A.2 the UAS class driver, `xhciuas.sys`, with its own INFs, both transports. Done 2026-10-04 (`runs/run-31.md`, "31-A.2 - the UAS class driver, `xhciuas.sys`" and "Windows 98 SE: the LUN's Code 10, and its fix"; `4e66702`, review fixes in `8619f23` and `21dda9e`, merged at `4d32167`; amd64 `3c2bd27`; the Windows 98 INF half `1ed1ba6`): bound on `Prot_62` only, a PDO per LUN, information units (`test_uas`), tags, autosense, task management and reset recovery; UAS at both speeds on every install leg, x86 and amd64
- [x] 31-A.3 the id policy: the bus chooses the transport and exposes only its compatible ids. Done 2026-10-04 (`runs/run-31.md`, "31-A.3 - the id policy"; `e0309f9`, review fixes in `ffe00b1`, merged at `030362f`): `xhci_xport.c` (`test_xport`) - UAS when offered and usable unless `XhciForceBulkOnly` and a Bulk-Only alternate exist, a refused interface under `USB\XHCI98_NOXPORT&...`, the non-streaming send-back through 29-A.5's hold (host vectors only)
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

## Phase 32 - Release `2.0.0.0`: The Bus Driver, SuperSpeed, Hubs and UAS

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

## Phase 33 - Release `2.1.0.0`: Hubs in Device Manager, Serial Instance IDs, F6 Setup and Faster Polling

Goal: the first update of the HCD generation: the Windows ME controller
re-enable fix, instance ids from serial numbers, device names from product
strings, a `txtsetup.oem` for Windows 2000 and XP text-mode Setup, external
hubs as devnodes, the Device Manager pages, hidusbf under the HCD, and
Low- and Full-Speed polling above 1000 Hz.

Status: closed 2026-10-05 on the cut. Opened 2026-10-04 on branch
`2.0.1.0` from `main` at `3eb2d2b`, renamed `2.1.0.0` by the owner the same
day when external hubs joined the release; worked on `p33-*` branches merged
into `2.1.0.0`, with a whole-branch review (`4b6fa60`) and the legs in
`out\phase33\`.

Why a phase: four of these change how Windows sees devices, one adds an
install path the project has never had, and the cut changes every
user-facing statement.

- [x] 33.1 the Windows ME controller re-enable fix: device PDOs stopped by an orderly controller stop kept dormant and revived at the same instance key, as `usbhub` does. Done 2026-10-05 (`a9a7577`; `docs/issues/09-me-controller-reenable-stopped-pdos.md`): ME re-enable 3 of 3 with a mouse, with a mouse and a stick, with a keyboard alone and with devices behind a hub; Windows 98 SE and 2000 disable and enable soaks 10 of 10 (`out\phase33\legs\results.md`)
- [x] 33.2 a device's instance id from its serial number, `UniqueID` TRUE, the location form kept for a device with no valid serial. Done 2026-10-05 (`5ba775a`; design record 13 section 10.7; host vectors in `test_func`): a serial stick keeps one devnode across root ports, behind a hub and on a SuperSpeed port with no new hardware found; two sticks get two devnodes; a duplicate serial takes the location form
- [x] 33.3 `txtsetup.oem` for Windows 2000, XP and XP x64. Done 2026-10-05 (`21c9e5f`, the first-enumeration wait `07ac963`; design record 13 sections 5.6 and 5.7): text mode and the Recovery Console pass on Windows 2000 and XP from the `release` package, with `XhciFirstEnumWaitMs` (5 s) and `XhciFirstEnumPortMs` (2 s per port) bounding the root hub's first answer; XP's GUI-mode prompts with a USB-only keyboard are a limitation (`docs/issues/10-xp-f6-gui-mode-usb-input.md`)
- [x] 33.4 external hubs as devnodes under project-owned ids (`XHCI98\HUB`, `XHCI98\HUB30`), the devices behind them re-parented, the door's hub recursion answered. Done 2026-10-05 (`f4cf324`, the NT hub re-enable fix `862c108`, the Address fix `f8226d9`): hub devnodes, the tree by connection, a two-tier chain, hub unplug and replug, and hub disable and enable on 98 SE, 2000 and XP. QEMU's hub is USB 1.1, so a High-Speed hub's entry and a USB 3 hub's "xHCI98 USB 3.x Hub" were read by the owner on real hardware after the cut, and work
- [x] 33.5 the Device Manager pages: the Power tab's descriptor request taken with `bRequest` 0, as every target's `usbui.dll` sends it. Done 2026-10-05 (`5586ed7`; design record 13 section 8.3): the Power tab shows mA on every target read. The Advanced tab's bandwidth counts only open isochronous pipes (98 SE to XP) or a WMI query the HCD does not serve (Vista and 7); both are in the release notes, and the audio reading was not taken
- [x] 33.6 a device's name from its product, function or interface string, "USB Device" when there is none. Done 2026-10-05 (`9be8bbb`): the string is answered on driverless devices, in 98 SE's New Hardware Found and in Windows 7's bus-reported description; a class INF's name still wins, as under Microsoft's hub driver
- [x] 33.7 SweetLow's hidusbf under the HCD. Done 2026-10-05 (the composite fix in `4b6fa60`, the Address fix `f8226d9`): the 1000, 500 and 250 Hz ladder programs Interval 3, 4 and 5 at a root port and behind a hub on 98 SE (NUSB 3.6 and stock), ME and XP; stock 98 SE's `usbd.sys` exports `USBD_ParseDescriptors` (`legal-provenance.md` section 4, corrected)
- [x] 33.8 Low- and Full-Speed interrupt polling above 1000 Hz (GitHub issue 4), opt-in under `XhciFastPollFsLs`, falling back to Interval 3 when refused. Done 2026-10-05 (`e83e476`; design record 13 section 13.6): built and reviewed, off by default; QEMU paces any Interval, so the release notes call it untested ground until the bench reading on the E460 and the P14s Gen 1
- [x] 33.9 the cut: `xhci_version.h` and the four INFs' `DriverVer` at `2.1.0.0`, `releases/history.md`, the release notes, the README and `make-release.ps1`, and the ten install legs read from the asset. Done 2026-10-05 (`4c9f65f`, `d1673ab`, `releases\2.1.0.0`, cut from `53f895a`): then a readme-only `-Force` re-cut (`369e9f8`) for the owner's hub reading, the binaries unchanged; asset `xhci98-2.1.0.0.zip`, 684,007 bytes; the ten release-asset install legs passed (`out\phase33\asset\`). An MSVC 6.0 miscompile of a count-down loop, found on the way, is fixed in `a9dd367` and recorded in `lessons.md`

Checkpoint: 33.1's re-enable legs, 33.2's port moves, 33.4's hub legs,
33.5's Power tab, 33.6's names and 33.7's hidusbf ladder passing on the
`2.1.0.0` package; 33.3's Setup legs on Windows 2000 and XP; 33.8 built,
reviewed and off by default; the ten install legs read from the asset.

The checkpoint **closed on 2026-10-05**. Owed to the bench: 33.8's polling
rate.

Records: `releases/history.md`; `releases/2.1.0.0/`;
`docs/using/release-notes.md`; `design/13-superspeed-hcd.md`;
`docs/issues/09-me-controller-reenable-stopped-pdos.md`;
`docs/issues/10-xp-f6-gui-mode-usb-input.md`.

---

## Phase 34 - Release `2.1.1.0`: The Registry Values, the Power Tab and the Intel Port Switchover

Goal: every registry value the driver reads written by the install, the
Power tab's current for a SuperSpeed device, and the Intel EHCI-to-xHCI port
switchover, released as the interim `2.1.1.0`.

Status: closed 2026-10-06 on the cut. Opened 2026-10-05 on branch
`2.1.1.0`. Split the same day from
a single phase into 34, 35 (polled event delivery) and 36 (selective
suspend and the rest), then made an interim release of its own, so
`2.2.0.0` was to carry Phases 35 and 36 (decisions table; both unnumbered
and unversioned since 2026-10-06, "Planned work", and `2.2.0.0` is now
Phase 35's). A first task 34.3,
the controller's Advanced tab bandwidth on Windows Vista and 7, was removed
by the owner the same day (its limitation stays in the release notes), and
the tasks after it were renumbered: commits before `8a8514d` name the
switchover 34.4, the docs 34.5 and the cut 34.6.

Why a phase: 34.1 changes what every install writes, 34.3 writes chipset
registers the project had left alone since Phase 4, and the phase ends in a
release.

- [x] 34.1 every registry value the driver reads written by the install, at its default, with FLG_ADDREG_NOCLOBBER (`0x00010003`) so an install or update keeps a value the user set (owner, 2026-10-05, over the unconditional form first built). Done 2026-10-05: the INF gate holds the values and flags (`VAL-*`); a fresh install and an update over `2.1.0.0` read on Windows 98 SE and 2000, and the update on Windows 7 x86 (34.1-V), which showed the driver store keeping the driver key; the other paths are covered by reasoning (`runs/run-34.md`, 34.1)
- [x] 34.2 the Power tab's current for a SuperSpeed device, a quarter of its draw because the page doubles bMaxPower, which is in 8 mA units at SuperSpeed. Done 2026-10-05 (`f393bb6`; design record 13 section 8.3): the door rewrites the byte usbui reads, saturating at 510 mA, and `XHCISNAP` logs each SuperSpeed device's exact current (owner: both). Read on the P14s Gen 1 under Windows 98 SE: an 896 mA device reads 510 mA where `2.1.0.0` read 224 mA, and `dev.ss.maxpower.ma=00000380` (`runs/run-34.md`, 34.2-H)
- [x] 34.3 the Intel EHCI-to-xHCI port switchover (owner, 2026-10-05, from a tester's report): on Intel 1E31, 8C31, 9C31, 8CB1, 9CB1 and 8D31 the HCD routes the switchable connectors to itself at start and resume and hands them back at stop and shutdown, superseding the miniport's Phase 4 decision; opt-out `XhciIntelPortSwitch` (1; only 0 is off). Done 2026-10-05 (`0def9a3` to `e591717`; design record 16; host vectors in `test_psw`): no config write on QEMU, the gate closed on the P14s Gen 1, and the switchover on the owner's Lenovo B490 (`1E31`) under Windows 98 SE with NUSB every clause but standby passing on Auto, and the firmware reading, the route, the SuperSpeed link and the shutdown again on Smart Auto (`runs/run-34.md`, 34.3-Q, -H1, -H2). Ticked on real hardware (owner)
- [x] 34.4 the docs of this phase: the release notes, README and readme template for 34.1 to 34.3, records 13 and 16, `xhci-programming.md`, `implementation-invariants.md`, `failure-diagnosis.md`, the acceptance test, `xhciqual/hardware-testing.md`, `source-files.md` and `runs/run-34.md` (record 16 section 12). Done 2026-10-05 (`874085b` to `e36a05b`), review-converged over four rounds (`runs/run-34.md`, review)
- [x] 34.5 the cut, dated 2026-10-06 (owner): `xhci_version.h` and the four INFs' `DriverVer` at `10/06/2026,2.1.1.0`, `releases/history.md`, the release notes, the README and `make-release.ps1`, and the ten install legs read from the asset, as 33.9. Done 2026-10-06 (`0be9dfc`, `41553dd`, `releases\2.1.1.0`, cut from `47f8d6c`): the ten release-asset install legs passed (`out\phase34\asset\`); Windows 98 SE's Driver tab read the driver file's date, not `DriverVer`, so `make-release.ps1` now dates every packaged file at noon on the `DriverVer` date, with a readme-only `-Force` re-cut (`e9bd04c`), the binaries unchanged, after which a fresh Windows 98 SE install read 10-6-2026 (`out\phase34\recut\`); asset `xhci98-2.1.1.0.zip`, 691,760 bytes

Checkpoint: 34.1's values read on Windows 98 SE, 2000 and 7 x86; 34.2 and
34.3 read on the owner's hardware; 34.3's host vectors passing and no
config write on QEMU; the ten install legs read from the `2.1.1.0` asset.

The checkpoint **closed on 2026-10-06**.

Records: `runs/run-34.md`; `design/16-intel-port-switchover.md`;
`releases/history.md`; `releases/2.1.1.0/`; `docs/using/release-notes.md`.

---

## Phase 35 - Release `2.2.0.0`: SuperSpeed on Intel Sunrise Point, the Ungated Port Switchover and Controller Tolerance

Goal: SuperSpeed devices working on Intel Sunrise Point-LP (`8086:9D2F`);
`XhciIntelPortSwitch` 2, the switchover on any Intel controller at the
user's own risk; and a transfer, port or controller fault no longer leaving
a device dead until it is replugged (design record 17); released as
`2.2.0.0`.

Status: closed 2026-10-07 on the owner's bench (35-E), after the cut
(35.7). Drawn up 2026-10-06 (owner) on branch `2.2.0.0` (named `2.1.2.0`
until the tolerance work joined it), from `main` at `c6f17ba`, after the
owner's E460 and a tester's HP EliteBook 850 G5 reported SuperSpeed devices
unseen and USB 3 hubs showing only their USB 2.0 half. The cause, read at
35.0 (issue 11): the link trains, but `9D2F`'s USB 3 speed table lists only
Intel's SSIC IDs 1 to 3, and the driver read a published table as
replacing the default IDs, so speed ID 4 decoded as unknown and the device
was dropped before Enable Slot. The tolerance work followed a tester's AMD
AM5 report of a mouse that stops at random. The bench was read after the
cut, as for `2.0.0.0` (decisions table, "`2.2.0.0`: cut before the bench").

Why a phase: it changes how every root port's speed is decoded, lets a user
send chipset register writes to Intel controllers nobody has read, adds
recovery behaviours to every controller, and ends in a release.

- [x] 35.0 the first reading on the E460 under Windows 98 SE with `2.1.1.0`. Done 2026-10-06 (owner): the SuperSpeed link trains (port 13 at U0, speed ID 4) and no slot is made; the Missing-CAS lead first planned is not supported; the cause is the speed decoding (issue 11)
- [x] 35.1 the speed-table fix, ungated: on a USB 3 protocol an ID the table does not list falls back to the defaults 4 to 7, a listed entry still wins, USB 2.0 unchanged; `XHCIQUAL` prints each protocol's PSI table and the raw extended capabilities. Done 2026-10-06 (`implementation-invariants.md`, "Port Speed Decoding"; the E460, P14s Gen 1 and B490 tables read)
- [x] 35.2 the E460 reading with 35.1. Done 2026-10-06 (owner): SuperSpeed at a root port and behind the USB 3 hub, ATTO about 242 MB/s writing and 245 MB/s reading; issue 11 fixed
- [x] 35.3 the shipping diagnostics: bounded enumeration notes for each root port, and a snapshot region with each root port's state and the controller's counters, decoded by `XHCISNAP`. Done 2026-10-06
- [x] 35.4 the regression on QEMU, and a `qemu`-only override giving the controller the E460's PSI table. Done 2026-10-07: Windows 98 SE and 2000 (two processors, Driver Verifier) read as `2.1.1.0`; QEMU has no SuperSpeed hub, so that clause rests on 35.2
- [x] 35.5 `XhciIntelPortSwitch` 2: the switchover on any Intel controller, past 34.3's gate, at the user's own risk; the release writes only the registers a route wrote. Done 2026-10-06 (design record 16 revision 3)
- [x] 35-T the controller tolerance (design record 17, revisions 1 to 11, reviewed to convergence before code): T.1 the lost-interrupt backstop; T.2 the soft retry of a USB Transaction Error, three per TD; T.3/T.4 the device cycle for a code nothing claims or a halt with no TD, before and after the PDO; T.5 a USB 2.0 root port the controller disabled or that reported an over-current, with a per-location budget and hold; T.6 an unexpected halt (recovery window) and an unreadable controller (containment, with and without the Bus Master Enable proof); T.7 the AMD interrupt-interval cap and `XhciAvgTrbEsit`; T.8 the counters in the snapshot, named by `XHCISNAP`; T.9 the pure core's vectors and a `qemu`-only injection of every fault; the switch `XhciTolerance` (1; 0 off). Done 2026-10-06 and 2026-10-07; each unit reviewed to convergence, then a whole-branch review, which also found and fixed older defects (`runs/run-35.md`, "Review findings on code already in the branch")
- [x] 35-V the legs. Done 2026-10-07: every fault made on QEMU under Windows 98 SE and 2000 (two processors, Driver Verifier) with the outcomes record 17 states; at `XhciTolerance` 0 the faults read handled as `2.1.1.0` handles them. Found and fixed: a controller latched failed could not be disabled. Known limitations: on Windows 2000 a mounted drive can come back at Code 31 after an in-place recovery; a controller closed off without proof that DMA stopped cannot be disabled until a restart
- [x] 35.6 the docs: the release notes, README, readme template, records 13, 15, 16 and 17, `implementation-invariants.md`, `failure-diagnosis.md`, `xhcisnap/README.md`, `xhciqual/hardware-testing.md`, `source-files.md`, issue 11 and `runs/run-35.md`. Done 2026-10-07, reviewed to convergence against the code
- [x] 35.7 the cut, dated 2026-10-07 (owner): `xhci_version.h`, the four INFs' `DriverVer` at `10/07/2026,2.2.0.0`, `releases/history.md`, and the ten install legs read from the asset. Done 2026-10-07: all ten passed, the install writing the three new values on each; two readme-only re-cuts the same day (the update steps naming the root hub; the Windows 98 storage-speed note removed), the binaries unchanged; asset `xhci98-2.2.0.0.zip`, 761,688 bytes
- [x] 35-E the bench (owner), read after the cut on the asset's `release-x86` package under Windows 98 SE. Done 2026-10-07: on the E460 SuperSpeed at a root port and behind the USB 3 hub, both its halves present, copies, a mouse and audio, the tolerance counters clean, 5 cold boots and 5 hot-plugs (the owner's counts); the P14s Gen 1 and the B490 at `XhciIntelPortSwitch` 1 unchanged. Value 2 on the B490 waived (owner); the HP 850 G5 and the AM5 board not read

Checkpoint: SuperSpeed on the E460 with 35.1; records 16 and 17 reviewed
to convergence; 35-V passing; the ten install legs read from the `2.2.0.0`
asset; 35-E read by the owner.

The checkpoint **closed on 2026-10-07**.

Records: `runs/run-35.md`; `design/17-controller-tolerance.md`;
`design/16-intel-port-switchover.md`;
`docs/issues/11-sunrise-point-ssic-psi-table.md`; `releases/history.md`;
`releases/2.2.0.0/`; `docs/using/release-notes.md`.

---

## Phase 36 - The Dell E7250 under Windows XP x64: a Device Present at Boot, and UAS With No Disk

Goal:
- xhci98 corrects the E7250 BIOS's `D20IR` interrupt routing, so the xHCI's
  interrupts reach the ISR. Then a device present at boot enumerates, and a
  hot-plugged disk mounts, on the Dell E7250 (Intel Wildcat Point-LP,
  `8086:9CB1`).
- Polled event delivery (design record 15) keeps a controller working when
  its interrupt cannot be delivered or repaired: no legacy interrupt at
  all, or a line proven dead at run time.
- MSI on Vista and 7 bypasses PIRQ routing altogether wherever the OS
  offers it (36.15).
- Three items from SweetLow's `2.x` testing (GitHub issue 17): the USB
  device interface, the device's real speed in the hub queries, and
  polling rates a client chooses (36.17 to 36.19).
- UAS commands of 1 MiB by default, up to 2 MiB by a registry value, with
  the HCD's TDs grown only as far as measurement calls for (36.21 to
  36.27).
- Selective suspend, off by default (`XhciSelectiveSuspend` 0), with the
  first standby and hibernate resume readings and the 98 SE audio-load
  wedge re-measured (36.28 to 36.41).
- Taken in during the phase (36.42 to 36.48): the Intel port switchover
  off by default with a script to set it, composite devices as devnodes,
  the 98 SE hot-plug hangs, string descriptors through the door, an
  interface with surplus endpoint descriptors, and the system named in
  `XHCISNAP`'s and `XHCIQUAL`'s reports.

Status: opened 2026-10-07 (owner) from a user report against `2.2.0.0`,
the first report of the HCD on real hardware under Windows XP x64 (issue
12). The root cause was found on 2026-10-08 (36.2). Its branch and target
version are `2.3.0.0` (owner, 2026-10-08): the branch was `2.2.1.0` until the
phase grew to take the routing quirk, polling and MSI.

Why a phase: its only evidence was a machine nobody here had, until the
owner reproduced the report on the owner's own E7250. The fix writes a
chipset register.

How it got here:
- **Report.** Finding 1: a device present at boot fails "no slot", because
  Enable Slot is refused BUSY behind the start's No Op. Finding 2: a
  hot-plugged UAS reader shows no disk. About half of all events came from
  the backstop.
- **2026-10-07.**
  - An interim build, `2.2.0.1`, held a Finding 1 fix and diagnostics. It
    was withdrawn before it was sent, and its code was reverted. Its record
    stays in issue 12 section 3.2.
  - The owner reproduced Finding 2 under Windows 7 x64 through Microsoft's
    `usbstor.sys` (issue 12 section 3.1).
- **2026-10-08.**
  - The diagnostic build `2.2.0.2` (36.1) and its read on the E7250 (36.2)
    found the cause (issue 12 sections 2 and 3). The E7250's BIOS routes the
    xHCI's INTA in the PCH's `D20IR` (RCBA+3160) to PIRQA (GSI 16), while
    ACPI tells Windows the xHCI is on GSI 21 (PIRQF). The ISR runs only
    when the EHCI on the shared line interrupts.
  - Both findings, and the sluggish HID, follow from that.
  - Rewriting `D20IR` to PIRQF live fixed the mouse and mounted the stick.
  - The owner chose a permanent fix in xhci98 behind a registry value that
    is on by default for this machine and controller only. No separate
    tool. The policy follows a Codex review (issue 12 section 5.1).
  - 36.3 comes first. The rest are separate changes, each committed on its
    own.
  - Later the same day the owner moved polled event delivery (`POLL.*`, until
    then planned work) into this phase. Its automatic mode gains one rule:
    a line interrupt proven dead at run time moves to polling (record 15
    section 4.2). That replaces the stalled-event watchdog 36.4 held, and
    36.4 keeps only the BUSY retry.
  - The owner then added MSI on Vista and 7, for both architectures, as
    36.15 and 36.16. The builds and E7250 check moved to the end of the
    list.
- **2026-10-09.** The owner took three of issue 17's six items into this
  phase as 36.17 to 36.19, item 6 in both its halves (36.18). The closing
  task, the builds and E7250 check, was renamed `36.final` the same day
  (owner), so a task added later goes above it and nothing is renumbered.
  Commits before then call it 36.17, which is now the device interface
  task. The 98 SE silent install
  is planned work ("Issue 17 follow-up"). The `XhciIntelPortSwitch`
  default waits on the reporter's reason, and the UAS figures are a report.
  The owner tests these items and answers the issue after.
- **2026-10-09, later.** A Codex review of the plan at `7567dec` (2 P1, 10
  P2) was taken into the tasks: 36.12's order corrected to xHCI 4.6.8; the
  dead-line rule's observer and live transition left open for 36.5 (record
  15 section 4.2); 36.4 kept, with its rules; 36.3's PIC IRQs limited to
  the routable ones; 36.17's lifecycle, 36.18's `_EX_V2` sources and 36.19's
  SuperSpeed composite path made explicit; the docs and install legs moved
  here from SUSP.3 and SUSP.4 (36.20, 36.final). The owner then decided
  the six points it left open: MSI's modes (3 as 0, the line forced on
  Vista and 7 by `MSISupported` 0, a failed message connection polls),
  single-message MSI-X admitted, the `_EX` Speed 3 and 4 on 98 and ME
  only, the poller's timer as the dead-line watcher, mode 4's loss across
  `SELECT_INTERFACE` documented, and the composite interface on the
  first-interface function, following its state. Rounds 2 to 4 followed
  (`0bacda3`, `6cac234`, `ef7fbac`); round 4 was clean.
- **Where it stands (end of 2026-10-09).** The plan is reviewed and no code
  of this phase is written: 36.1 was reverted and 36.2 was a reading. Next
  is 36.3, the routing quirk, which settles its LPC configuration-space path
  first. 36.5 settles the WATCHING design and record 15's mode rules before
  any polling code. After testing, the owner answers issue 17 on GitHub;
  that reply says Speed 3 and 4 are 98 and ME only, and that the composite
  interface follows the first-interface function.
- **2026-10-09, evening.** The owner moved the 1 MB transfer plan, drawn
  up that day as planned work with a registry value reaching 2 MB and
  reviewed once, into this phase as 36.21 to 36.27, above 36.final.
- **2026-10-09, later.** 36.4 done. The owner then moved the selective
  suspend work (`SUSP.*`, planned work until then) into this phase as
  36.28 to 36.41, its docs into 36.20 and its cut and install legs into
  36.final, with `XhciSelectiveSuspend` now defaulting to 0, since off by
  default it changes nothing for a user who does not set it.
- **2026-10-09 and 10, during the run toward the `2.2.0.3` test build.**
  The owner took in 36.42 to 36.48 above 36.final: from SweetLow's
  comments on issue 17, the port switchover off by default (36.42),
  composite devices as devnodes (36.43), string descriptors through the
  door (36.45) and the surplus-endpoint mouse (36.46); from the legs, the
  98 SE hot-plug hangs (36.44); and the system named in `XHCISNAP`'s and
  `XHCIQUAL`'s reports (36.47, 36.48). Also decided: MSI on Vista and 7
  x86 through an NT 6-only helper driver (design record 18), mode 1
  forcing the line, a per-device `MaximumTransferLength`, and stage 2's
  `XhciWideMap` read for every target's HAL, then off by default
  (owner, 2026-10-10: 1 opts in).
  `2.2.0.3` is a local test build for the owner's bench; `2.3.0.0` is cut
  after it.

- [x] 36.1 **done 2026-10-08** (`3d667f1`); **reverted 2026-10-08 (owner) after its read**: the code and the `XHCISNAP` decoder are removed, the tree is back to `2.2.0.0`, and the description is kept as the record (issue 12 section 3.3) - the diagnostic build, numbered `2.2.0.2` (owner, 2026-10-07), on `2.2.0.0`'s code with no change in behaviour (issue 12 section 3.3):
  - per open pipe of every slot: its flags, queue and oldest outstanding transfer (URB function, setup bytes, length, age, TD address), with the Output Endpoint Context's state, TR Dequeue and DCS read at dump time;
  - a timeline of the last RESET_PIPE, ABORT_PIPE, RESET_PORT and cancel requests: arrival, completion, endpoint, the CLEAR_FEATURE outcome, the commands and their status, and the slow-IRP depth;
  - the interrupter's registers (USBSTS, IMAN, ERDP, IMOD, USBCMD) and the pending event before each backstop drain, with ISR and DPC times.

  `XHCISNAP` decodes all of it. Host tests, every gate, a Codex review loop.
- [x] 36.2 **done 2026-10-08** (issue 12 section 3) - the read (owner): `2.2.0.2` on the E7250 under Windows 7 x64 and XP x64, with the stick hot-plugged and present at boot, and with the EHCI disabled. Then RW-Everything reads of the PCI and PCH registers, and the live `D20IR` write. The root cause is found, and the fix (36.3) replaces the superseded check.
- [x] 36.3 the routing quirk, `XhciIntelIrqRoute` (REG_DWORD on the controller's software key; the full design, with the reviewed ID table, is issue 12 section 5.1):
  - **The values.**
    - **0**: no writes; report only.
    - **1** (default, also for missing, wrong-type or out-of-range values, logged): only xHCI `8086:9CB1` with subsystem `1028:062D` and LPC `8086:9CC3`.
    - **2** (opt-in, never the default): any reviewed xHCI/LPC pair regardless of subsystem. The pairs are 1E31, 8C31, 9C31, 8CB1, 9CB1 and 8D31, each with its exact LPC IDs (issue 12 section 5.1). It never admits unlisted Intel, 100-series and later (PCR/ITSS) or non-Intel.
  - **Writes only when all hold:**
    - a valid, enabled RCBA read from LPC `00:1F.0` F0;
    - D20IP agrees with the xHCI's INTA;
    - a line-based, unambiguous interrupt;
    - a target PIRQ derived from the raw IRQ, and D20IR's IAR differing from it:
      - **APIC mode** (raw IRQ 16-23): the target is IRQ-16;
      - **PIC mode** (raw IRQ 3-7, 9-12, 14 or 15; Windows 98 and ME, or NT without the APIC): the target is a PIRQ whose LPC `PIRQx_ROUT` (`60h`-`63h`, `68h`-`6Bh`) is enabled and names that IRQ. The current one is kept if it matches, else the lowest is taken. Those registers are read, never written. **Written on 98 and ME only** (owner, 2026-10-09, after the rule's review): on 2000, XP, Vista and 7 an I/O APIC HAL can hand out an IRQ below 16 too and its route is not proven, so there the target is recorded and nothing is written, at every value (`STEERED_NT`). The other values below 16 (0, 1, 2, 8 and 13) are reserved encodings in `PIRQx_ROUT`'s IRQ field (Intel 330837-002 10.1.17, and each pair's datasheet), name no routable interrupt, and are report-only; host vectors cover each of them.

    Then it writes `(old & ~7) | target` as a 16-bit write and reads it back.
  - **Which deliveries it serves:** every line delivery. That means Windows 98, ME, 2000 and XP, and Vista and 7 with `MSISupported` 0 in the controller's hardware key or without an MSI grant (36.15).
  - **Not active under MSI or polling** (owner, 2026-10-09): when the delivery in effect is a message interrupt (Vista and 7 with an MSI grant) or a poll (`XhciInterruptMode` 2, or mode 0 with no line), it writes nothing and only reports. It decides at each run, so a later start or D0 return on the line repairs the route then. A move to polling at run time (the dead-line rule, 36.7) does not undo a write already made. This replaces "under polling it still repairs, for a later start on the line".
  - **Report-only** when no target can be derived, or the raw value is outside both ranges. The PIC-mode rule (owner, 2026-10-08) replaces an earlier "report-only in PIC mode"; its own review, in 36.3's Codex loop, led to the 98-and-ME-only write above.
  - **The parser** keeps the raw interrupt resource (level, vector, flags, share) (`hc->IntRaw`).
  - **Reaching LPC config space:** settled as `HAL.dll!HalGetBusData` over bus 0, from offset 0 (`hcd_ctl.c` `hcdPirqSlot`), since `HcdSvcConfigSpace` reaches only the xHCI's own PDO. It is exported on every NT target and named in 98 SE's `ntkern.vxd` (`legal-provenance.md` section 4, static); `...ByOffset` has no Windows 98 evidence. Whether 98 SE's and ME's answer with real data is the `qemu` flavour's probe, a VM leg still owed. Not CF8/CFC.
  - **Placement:** in the HCD layer.
    - At start: after IoConnectInterrupt and `XhciInitController`, before `XhciEnableInterrupts`.
    - On each D0 return: before `XhciResumeController`.
  - **What is recorded:** what it found, decided and wrote, in the snapshot and the note ring, decoded by `XHCISNAP`.
  - **Tests:** host tests of the pure policy, the delivery included. The INF and `docs/using` document the value, modelled on `XhciIntelPortSwitch`; copy its strict DWORD reader, not its "2 = unlisted Intel" meaning. `docs/using` says plainly that the value does nothing on Vista or 7 with MSI, or under polling.
  - **Done 2026-10-09** (`b71bd64..95b2c31`, the Codex loop clean at its fourth round; built x86 and `-amd64`, all flavours, every gate and the host suites passing at `86db9a3`). Owed, in VM legs: the `qemu` flavour's probe on 98 SE and ME (`HalGetBusData` and the USHORT register accessors answering there, not only resolving); 2000, XP, Vista and 7, x86 and x64; a start the lower driver refuses, for the record's reset site. The E7250 itself is 36.final. **Legs run 2026-10-09/10** (worker p36-misc, development host A, QEMU 11.1, `qemu` flavour at `ebe3ec9` and `04ef848`): the probe on 98 SE, ME, 2000, XP, Vista and 7, x86 and x64, read `pirq.qemu.device` 3, the xHCI's own slot (00:03.0), so `HalGetBusData` answers with real bus-0 data on 98 SE and ME too; `pirq.qemu.lpc.bytes` 2 (00:1F.0 absent), decision `NOT_REVIEWED`, no write, devices working; `XhciIntelIrqRoute` 0 and 2 on 7 x86 the same. Still owed: the USHORT register accessors executing on 98 and ME (no QEMU path reaches them: they run only on a reviewed Intel PCH), and a start the lower driver refuses.
- [x] 36.4 the bounded BUSY retry: `hcdCommand` on `XHCI_CMD_BUSY` waits a bounded time for the engine to go idle, asking for a drain, and retries under one deadline, instead of failing the port at once. This is the start No Op case of issue 12 Finding 1. A small change of its own; the stalled-event watchdog that 36.4 also held is replaced by polling's dead-line rule (36.5, 36.7).
  - **Still needed after 36.3, polling and MSI** (Codex review of `7567dec`, and the owner's reading). They deliver completions; none guarantees that the start's asynchronous No Op has completed before the next submit, and `XhciCommandSubmit` also answers BUSY during an abort (`xhci_cmd.c` ~1004). Its remaining cases: a dead line inside the window before the dead-line rule moves to polling; mode 1, which never moves; a completion waiting for the next poll at the idle period; and any transient BUSY, which is never a device's failure.
  - **The rules.** One deadline across waiting and resubmission; a completion counts only by its own TRB, as now; the retry stops on controller failure or a change of reset generation; it resubmits only once the engine reads idle. It complements the dead-line detector and does not replace it.
  - **Leg.** A `qemu`-flavour delay of the start No Op's completion, so BUSY is met deterministically with a healthy line: the device present at boot enumerates.
  - **Done 2026-10-09** (`9a8f32e..9dca404`, the review loop clean at its fifth round; built x86 and `-amd64`, all flavours, every gate and the host suites passing at `9dca404`). `hcd_enum.c`'s `hcdCommandBusy`, its look the pure `XhciEnumBusyStep` (`test_enum`); the drain `HcdCommandDrain` under the backstop's admission; the waiter's marks cleared in the same controller-lock hold that reads the engine idle (`hcdCommandArm`), which also closes an older race where an earlier command's loss was taken for the next one's. The aid is the `qemu` flavour's `XhciQemuStartHold`, held to 3000 ms (`build-and-test.md`, "The qemu flavour's test aids"). Owed, in VM legs: the leg above on 98 SE and 2000 at least, reading `enum.cmd.busy.end` and no `cmd.timeout` after the hold; a hold with no device present, read as `qemu.inj.starthold.expired`. **Legs run 2026-10-09/10** (worker p36-misc, development host A, QEMU 11.1, `qemu` flavour): on 2000 and 98 SE a hold of 2000 ms with a stick present read `qemu.inj.starthold.released`, `enum.cmd.busy.end` 09000002 and no `cmd.timeout`, the stick working; the stop's counter block (DebugView) read `enum.cmd.busy` and `enum.cmd.busy.cleared` 1 each per hold start (cumulative on 98, which keeps the driver loaded across a disable); with no device, `qemu.inj.starthold.expired` 1 and a stick hot-added after it working. Passed.
- [x] 36.5 (was POLL.0) the polling design: record 15 reviewed to convergence before code (the owner's decisions of 2026-10-05 are its section 12), with its amendment for issue 12 reviewed too (section 4.2, decision 6):
  - automatic mode (0) also moves to polling when a line interrupt is proven dead at run time;
  - **two gaps in that amendment, settled before code** (Codex review of `7567dec`):
    - **the detector.** Section 4.2 proved a line dead by repeated backstop looks, but `hcdBackstop` runs on `hcdThread` (`hcd_ctl.c` ~898), which `hcdCommand` blocks waiting for the very completion that never arrives (issue 12 section 2). **Decided (owner, 2026-10-09):** the poller's own timer and DPC watch instead. Under every line delivery in mode 0 the poller runs in a slow WATCHING state that peeks `IMAN`, `USBCMD.INTE` and the event ring and never drains; it runs whatever `hcdThread` is doing, command waits and `XhciTolerance` 0 included. The record settles its period, count and window;
    - **the transition** follows from that: proving the line dead is the poller's own move from WATCHING to in service, under `PollLock`, with the interrupter masked first, the ISR admitting nothing after, and a concurrent close. Section 5.2 gains WATCHING and that move, since today it lets the poller leave CLOSED only at a start under a poll delivery;
    - **WATCHING is a role, not an owner state** (Codex review of `6cac234`). Section 5.2's states say who owns the timer (PARKED, ARMED, FIRING, CLOSING, CLOSED); WATCHING says what the callback does while it owns it. A start whose effective mode is 0 under a line delivery opens the poller (CLOSED to PARKED, then the kick) in the watch role, so the ownership rules, the hardware admission under the controller lock and `PollService`, park and kick across D3 and the in-place recovery (section 5.3), and the close apply to a watcher unchanged. "Mode 0" is the effective mode: 0, 3, and a reserved value run as 0. Host vectors: a watcher's close against its expiry, and a watcher across recovery and D3;
    - **record 15's mode rules reconciled before code**, in this task, not in 36.20: section 1's "not in this design" for MSI, section 3's mode 3, section 4's table (written for a line resource), and section 11's expectation that modes 0 and 1 use the line on Vista and 7, all to 36.15's decisions;
    - and the legs gain a line dead from the first No Op and one that dies during a command wait;
  - the dead-line predicate (`IMAN.IP` set, nothing claimed) stays line-only: xHCI 4.17.5 clears `IP` itself after an MSI or MSI-X write, so it cannot detect a lost message;
  - the hardware statements of its section 4.1 (`IMAN.IP`, `USBSTS.EINT` and `ERDP.EHB` with the interrupt enables clear) checked against `xhci-data-structures.md`
  - **Done 2026-10-09** (`a744811..f7f0907`, record 15 revision 5; the Codex loop converged at its eighth round, section 13.5). Settled: the watcher (100 ms, three stale pairs over at least 300 ms, `PollServiceGen`), the move and the line's retirement under `KeSynchronizeExecution` with its containment (PCI Interrupt Disable, then the recovery), WATCH and SERVE as roles with `ParkPending`/`KickOwed`, the grants after D0 under the power gate, the period's inputs and the doorbell hook, and 36.15's design. MSI's x86 route: no safe way was found for the x86 binary alone, and the owner decided (2026-10-09) on an NT 6-only x86 helper driver, designed separately; amd64 calls `MmGetSystemRoutineAddress`; both feed one connection seam (36.15, 36.16 and the decisions table amended).
- [x] 36.6 (was POLL.1) the three values (record 15 section 3):
  - `XhciInterruptMode` 0: 0 automatic, 1 the line interrupt, 2 polling, 3 the same as 0 (MSI where granted is already 0's first choice, 36.15), 4 and above reserved and run as 0;
  - `XhciPollIdleMs` 50, held to 5000;
  - `XhciPollActiveMs` 1, held to the idle period;
  - neither period 0 (owner, 2026-10-05).

  They are read at each start with the out-of-range rules, and written by both INFs on every install path under 34.1's don't-overwrite rule, with the INF gate's `VAL-*` rows and the footprints. 36.final's install and update legs read them
  - **Done 2026-10-09** (`158ad18..53cb9e2`, one Codex loop over 36.6 to 36.8, clean at its fifth round; built x86 and `-amd64`, all flavours, every gate and the host suites passing at `53cb9e2`). `XhciInterruptMode`, `XhciPollIdleMs` and `XhciPollActiveMs` read at each start (`hcdSettleDelivery`, `XhciPollValues`), in both INFs on every path with the gate's value rows, footprints and mutations. Owed: 36.13's VM legs (no guest was run by this task), and 36.9/36.10's tool work.
- [x] 36.7 (was POLL.2) the delivery and the poller (record 15 sections 4 and 5):
  - the delivery and its reason settled at start and kept through recovery and resume;
  - mode 0's run-time move from a dead line to polling (section 4.2), kept the same way, with the delivery and its reason in the counter block updated at the move, not only set at start;
  - `ResourcesTypes` truthful, and the core's interrupt-bit and pin-0 gates applied to a line delivery only;
  - under a poll delivery: the ISR unconnected, or claiming nothing after a run-time move; `XHCI_EXT_FLAG_INTERRUPTS` never set; every poll drain passing no re-arm;
  - `PollTimer` with its own DPC, so polling does not depend on `hcdThread`, which can be blocked in a command wait (issue 12 section 2);
  - `IsrDpc` queued by the peeks, `PollLock` admission, and close by `KeCancelTimer` with the timer DPC retiring itself;
  - park and kick across D3 and the in-place recovery
  - **Done 2026-10-09** (`158ad18..53cb9e2`, one Codex loop over 36.6 to 36.8, clean at its fifth round; built x86 and `-amd64`, all flavours, every gate and the host suites passing at `53cb9e2`). `hcd_poll.c` and `xhci_poll.c` (`test_poll`): the delivery settled before connecting, `hcdConnect` as MSI's seam, the poller and its WATCH and SERVE roles, the dead-line move with the line's retirement and containment, the grants after D0, park and kick. Owed: 36.13's VM legs (no guest was run by this task), and 36.9/36.10's tool work.
- [x] 36.8 (was POLL.3) the adaptive period and the opportunistic checks (record 15 sections 6 and 7.1):
  - **The pure core:** `xhci_poll.c` with host suite `test_poll`. It covers ACTIVE, PERIODIC with interrupt IN and OUT, IDLE, the hysteresis and bounded-drain clauses, the clamps, promotion and the stale-decision rule.
  - **The peeks:** under the controller lock, after a doorbell, after a drain, on each thread pass, health poll and frame DPC. Each queues the drain and never drains inline; poll delivery only.
  - **The counters:** the observed interval, and the delivery in effect with its reason (automatic, forced, no interrupt, or line not delivered). They are in the counter block and in the start log line (a run-time move logs its own line). `XHCISNAP` reads them from `XhciLogVerbosity` 1, and the DebugView sink emits them from 2 with `XhciLogDebugView` set (owner, 2026-10-05; section 8)
  - **Done 2026-10-09** (`158ad18..53cb9e2`, one Codex loop over 36.6 to 36.8, clean at its fifth round; built x86 and `-amd64`, all flavours, every gate and the host suites passing at `53cb9e2`). The adaptive period, the peeks and the doorbell hook, and 33 counter words appended to `XHCIHC_COUNTERS` (63 to 96) with their log names. Owed: 36.13's VM legs (no guest was run by this task), and 36.9/36.10's tool work.
- [x] 36.9 (was POLL.4) `XHCISNAP` (record 15 section 9):
  - the three values in the companion, as `XhciImodInterval250ns` is;
  - the delivery in effect and its reason;
  - the moderation value said to have no effect when polled;
  - the poll counters;
  - the coherence wording for drains polled or not (the tear detector itself already counts them);
  - the header grown under the schema rule, with older headers still read
  - **Done 2026-10-10** (`fe2dcdc`, review fixes `e7d0da8`, `1de1913`; one Codex loop over 36.9 and 36.10, clean at its fourth round; built x86 and `-amd64`, all flavours, every gate and the host suites passing at `9935572`, `XHCISNAP`'s selftest 13 cases). Counter words 63 to 95 named and decoded in an "event delivery" section (the three values with their range rules, delivery and reason, pin, the moderation note under polling, poller, watcher, move, retirement); HCD header words 202 and 203 place the six extension words (header 204 words, version unchanged; `test_snap`); one layout check (`hcd_shape_ok`) before every HCD-derived reader; a 63-word block and a 202-word header still read. The transfer-size words of 36.21/36.22 were already decoded; 36.11 and 36.12 added no words. Read in QEMU on development host A on the `release` packages: Windows 98 SE mode 0 (the line, automatic, watcher looking) and mode 2 (polling, forced, poll runs and the observed interval, the moderation note), Windows 7 x64 mode 0 (the line, automatic), every counter word named.
- [x] 36.10 (was POLL.5) `XHCIQUAL`'s polling verdicts (record 15 section 10):
  - `Interrupt Pin = 0` no longer disqualifies: `quick_classify` and `quick_reason` say the controller needs polling. The other disqualifiers stand.
  - C4's "event in the ring, no interrupt" outcome reads "needs polling". Its tool failures, and a controller that fails even polled, do not.
  - The polled tests run after the interrupt hook is removed.
  - `main.c`'s C6/C8 gate and `report.c`'s verdict and exit code learn the outcome, with vectors through the verdict, not only `test_mmiodiag`.
  - **Its own verdict** (owner, 2026-10-09): a controller with no working interrupt (pin 0, or C4's "event in the ring, no interrupt") that passes the rest is **QUALIFIED WITH CAVEATS** - not plain QUALIFIED, not "QUALIFIED (with warnings)", not a failure, not provisional - with its own exit code, 3. It prints the caveats: the driver polls (`XhciInterruptMode` 0 or 2: more wake-ups, latency up to the poll period), or an MSI-capable OS; and from the PCI capability list (05h MSI, 11h MSI-X) whether Vista and 7 can take a message interrupt (x64 directly, 32-bit with the `xhcimsi.sys` helper) or polling is the only route. Warnings, if any, beside it. Record 15 section 10, `xhciqual/README.md`, `hardware-testing.md` and the release notes say so.
  - **Done 2026-10-10** (`d1bbaa9`, review fixes `e7d0da8`, `1de1913`, `9935572`; the Codex loop shared with 36.9, clean at its fourth round; `XHCIQUAL` built, host suites passing with the new `test_verdict` (80 checks), the QEMU matrix 46 cases and the Win98 batch harness 5 cases passing on development host A). `xhciqual/verdict.c` (pure, out of `report.c`): C4's PASS, POLL or FAIL; the C6/C8 gate; QUALIFIED WITH CAVEATS with its caveats and exit code 3; the quick scan's LOOKS QUALIFIED WITH CAVEATS. A controller whose IP was never seen is told `XhciInterruptMode` 2 (automatic mode's watcher needs IP); its MSI route is said as untested. The matrix reaches POLL through `SETIL05`/`SETILFF.COM`, which rewrite the xHCI's Interrupt Line before the run (IRQ 5, which nothing drives; FFh, none), reading C4 POLL, the verdict and exit 3. Orchestrator decision, owner confirmed 2026-10-10: an unusable Interrupt Line and a stuck PCI Interrupt Disable also read "needs polling" when the command completes polled. Owed to the bench: a real pin-0 or dead-line controller under DOS (36.14 / 36.final).
- [x] 36.11 `XHCIQUAL`'s routing report, read-only:
  - It reports the LPC identity, RCBA, D20IP and D20IR, the selected PIRQ, and the PIC destination and its disable bit.
  - It flags a mismatch only against a GSI supplied separately.
  - **Only on the reviewed RCBA chipsets** (the xHCI/LPC pairs of issue 12 section 5.1: 7, 8 and 9 series and C610/X99). The registers do not exist elsewhere:
    - 100 series and later, and the Apollo Lake-class Atom SoCs, route through ITSS behind P2SB;
    - Bay Trail and Braswell route through the iLB;
    - non-Intel controllers have no equivalent.

    Elsewhere it reports "routing registers not applicable" and reads nothing. Support for ITSS is not in scope.
  - **Done 2026-10-09** (`9c8a5ec`, `4848c08`, the review loop clean at its second round; `XHCIQUAL` built, its host suite passing with the new `test_route`, the QEMU matrix 42 cases passing on development host A). `xhciqual/route.c` (pure) and `pci.c`'s `route_read`, the reviewed pairs taken from the driver's own `src/xhci_pirq.c`; read before any active test in every mode, reads only. The GSI is `--gsi N`, accepted only beside `--quick` or `--probe-only`, so it never starts an active run; an IRQ below 16 is compared on the 8259 path alone, as DOS found it, and says that it proves nothing for an NT APIC HAL. No guest has these registers (QEMU reads "not applicable"); a reading on a reviewed chipset, the E7250 under DOS, is owed to the bench (36.final).
- [x] 36.12 RESET_PIPE in xHCI 4.6.8 order in `hcdCfgReset`. It is a spec fix, kept apart so it does not muddy the interrupt tests (issue 12 section 3.2).
  - **The order** (xHCI 1.2c 4.6.8, p.116, "reset a pipe"): Reset Endpoint (TSP 0); then `CLEAR_TT_BUFFER` to the hub, when the device is behind a TT and the endpoint is control or bulk (`HcdHubClearTt`); then `CLEAR_FEATURE(ENDPOINT_HALT)` to the device, for any endpoint but control; then Set TR Dequeue past the failed TD; then the doorbell. Today the device's clear comes first, as usbport ordered it (`hcd_cfg.c` ~2875).
  - **Halted and not.** This order is the Halted endpoint's. A non-Halted one keeps today's path, brought to Stopped and given a fresh context (`hcdCfgQuiesce`, `hcdCfgRecycle`), since Reset Endpoint is refused on it.
  - **A device clear that fails.** A successful Reset Endpoint has already reset the host's toggle and moved the endpoint from Halted to **Stopped** (4.6.8, pp.115-116), and 4.6.8 warns that the endpoint's behaviour is undefined if the device's toggle is not reset too. So a clear that stalls, fails or times out does not ring the doorbell and resume the pipe, as today's failure branches do (`hcd_cfg.c` ~2886). The pipe stays paused, the client gets the failure, and the pipe records that the host's half is done and the device's is owed.
  - **The next RESET_PIPE** reads that record, not only the Endpoint Context: Reset Endpoint would now be refused with Context State Error, since the endpoint is Stopped, not Halted. It sends the TT clear and the device clear again, then Set TR Dequeue (legal on a Stopped endpoint), then the doorbell. A pipe with no such record follows the order above if Halted, and today's quiesce and recycle if not.
  - **The record's lifetime** is the endpoint context's. It is cleared when a RESET_PIPE completes both halves, and whenever the context is replaced: `RESET_PORT`'s rebuild, which keeps the pipe objects and handles but builds new contexts (`hcdCfgResetPort`, `hcd_cfg.c` ~3089-3173), the endpoint recycle, a new select, and the device's invalidation. A record never outlives the context it describes.
  - **A device clear that times out** is not this path. It sets `Ep0Stuck`, taints the scratch and asks for the controller's recovery, which invalidates the device (`hcd_enum.c` ~1104-1113); that containment stays as it is, and the pipe is not retried on its old context.
  - **Tests.**
    - the same pipe: a device clear that stalls, and one that fails, each followed by a RESET_PIPE that succeeds without a second Reset Endpoint;
    - a failed clear, then `RESET_PORT`, then a RESET_PIPE that takes the Halted order or the recycle, not the owed-half path;
    - a device clear that times out, read as the recovery and re-enumeration it is today;
    - a TT-attached bulk endpoint, read for the TT clear's place in the order; a non-Halted endpoint.
  - **Done 2026-10-10** (`3c0fe00`, `0953831`, `04ef848`, the review loop clean at its second round; built x86 and `-amd64`, all flavours, every gate and the host suites passing at `04ef848`). `hcdCfgReset` takes its steps from the pure `XhciPipeResetSteps` and its clear's verdict from `XhciPipeClearVerdict` (`test_pipe`, which carries the order, the owed path, a failed clear on each type, and the not-Halted path); the owed record is `HCD_PIPE.ClearOwed`, holding one pause, ended by `hcdCfgOwedEnd` (both halves done, any successful Configure Endpoint that adds the endpoint, or a state other than Stopped). SYNC_CLEAR_STALL on an owed endpoint finishes the reset; SYNC_RESET_PIPE answers success and leaves it owed (round 1, finding 1). The `qemu` flavour's fault 13 answers the next device clear (`build-and-test.md`). **Legs** (2026-10-09/10, development host A, QEMU 11.1): on 2000 and 98 SE, fault 08 with `arg` 4 on the HID pointer, with fault 13 (STALL, failed, never sent) and without, reached RESET_PIPE only after the class driver's abort had reset the Halted endpoint, so every reset took the not-Halted path (the clear refused, the request failed, the pipe resumed, the mouse working after each): the Halted order and the owed path are covered by the host vectors alone, and QEMU's `usb-mouse` STALLs every CLEAR_FEATURE(ENDPOINT_HALT). A TT-attached bulk endpoint and a clear that times out cannot be made on QEMU. The `release` regression on 2000 and 98 SE with a mouse and two sticks passed (one 98 SE shutdown hang at "Windows is shutting down", not reproduced on a repeat). Owed: a vehicle whose class driver resets a Halted endpoint with nothing outstanding, or real hardware.
- [ ] 36.13 (was POLL-V) the polling legs (record 15 section 11):
  - **Guests:** mode 2 on 98 SE and 2000 (SMP, Driver Verifier) first, then ME, XP, Vista and 7, x86 and x64.
  - **Devices and actions:** HID, Bulk-Only and UAS storage, composite audio playing, behind QEMU's hub, hot-plug, disable and enable, remove and rescan.
  - **The no-interrupt start,** made deterministic by a `qemu`-flavour switch: mode 0 polls with its reason, and mode 1 fails as today.
  - **The dead-line move:** a `qemu`-flavour line that delivers nothing after start, under mode 0, read as the move to polling with its reason; under mode 1, as no move.
  - **Recovery and power under polling:**
    - a forced recovery;
    - the controller's D3 and back, where a guest offers one without system standby. Where none does, the forced recovery is read in its place (park and kick only where the poll timer's DPC reads the service failed or revoked during it; corrected 2026-10-10, wave 3 matrix M-2), and a disable and enable reads the close (record 15 section 11).
    - system standby and hibernate under polling are 36.28's.
  - **The races, on the SMP guest:** promotion against re-arm, close against peeks and expiry, a bounded drain continued.
  - **Measurements:** the period each kernel gives, from the counters; line against poll on the same guest (storage copy rate, audio continuity, idle CPU load).
  - **Regression:** modes 0 and 1 on a pin reading as the release before it.
  - **Visibility:** the delivery and its reason read in `XHCISNAP` and in DebugView for each of modes 0, 1 and 2, for the no-interrupt start and for the dead-line move.
  - x86 and amd64, and every gate
  - **Read 2026-10-10, not done** (`runs/run-36.md`, "36.13"; fixes `5bca7bd`, `824a6c7`). Every leg on `bfec7b0` passed on 98 SE, ME, 2000, the 2000 SMP guest under Driver Verifier, XP, XP x64, Vista and 7 x86 and x64, except the dead line under mode 1: a command or EP0 wait expired into a reset, since the backstop runs on the waiting thread. Fixed: the waiting thread queues the drain itself at `XhciTolerance` 1 (`enum.wait.drains`); one Codex loop, clean at round 2; built x86 and `-amd64`, every gate and the host suites passing at `824a6c7`. Read on the fixed build on 2000 and 98 SE. Owed to the matrix: the fixed build's dead-line legs on XP, XP x64 and 7 x64, and its `release` smoke on 7 x64.
- [ ] 36.14 (was POLL-E) the polling bench (owner):
  - mode 2 on the E460 and the P14s Gen 1 under Windows 98 SE;
  - an MSI-only controller if one is at hand (the synthetic no-interrupt leg does not wait for one);
  - the E7250 under XP x64 with `XhciIntelIrqRoute` 0 and mode 0, read as the dead-line move to polling.
- [ ] 36.15 MSI on Vista and 7, x86 and amd64 (owner, 2026-10-08; x86 through a helper driver, owner, 2026-10-09), designed as a record 15 amendment (section 4.3) and reviewed before code:
  - **Why.** A message-signalled interrupt bypasses PIRQ routing, so a BIOS routing fault like the E7250's cannot affect it. It is also how an MSI-only controller gets real interrupts on NT6 instead of polling. Windows 98, ME, 2000 and XP have no MSI, so they keep the line, the routing quirk and polling.
  - **The INF.** `MSISupported` 1 under `Interrupt Management\MessageSignaledInterruptProperties` in the controller's hardware key, with a message limit of 1 (Interrupter 0 only). The Vista and 7 install paths only (`Xhci.Dev6.NTamd64`; and `Xhci.Dev6.NTx86` with the helper's boot-start service and `Reboot`, design record 18 section 7); earlier systems ignore it.
  - **The resources.** `hcdParseResources` takes a message interrupt (`CM_RESOURCE_INTERRUPT_MESSAGE`) as well as a line one. Windows may still grant the line, so both paths stay supported.
  - **The connection.** `IoConnectInterruptEx` (`CONNECT_MESSAGE_BASED`, with `CONNECT_LINE_BASED` or `CONNECT_FULLY_SPECIFIED` for a line) and `IoDisconnectInterruptEx`, on Vista and later only.
    - **The single x86 binary** also loads on 98, ME, 2000 and XP, so it must not import these routines. `MmGetSystemRoutineAddress` is absent on 98 and ME too.
    - (Superseded: "so both architectures resolve them at run time by a bounded walk of the kernel image's export table".) **Amended in 36.5's review, then decided by the owner (2026-10-09; record 15 section 4.3).** No way was found for the one x86 binary to reach `IoConnectInterruptEx` safely by itself (a walk of kernel memory cannot prove its pages mapped; a parse of the kernel file proves its imports' places, not the target's). So amd64 imports `MmGetSystemRoutineAddress` (exported by every amd64 target), and x86 gets the pair from `xhcimsi.sys`, a small NT 6-only x86 helper driver installed only by the Vista and 7 x86 INF section, over a named device opened with `IoGetDeviceObjectPointer` (the x86 image's one new import, with stock Windows 98 SE precedent; owner, 2026-10-09) and a versioned internal device control (design record 18). Both feed one connection function (`hcdMsgConnect`). Without the helper, the requirements filter (`IRP_MN_FILTER_RESOURCE_REQUIREMENTS`, record 18 section 4.2) keeps only the line configurations, so Vista and 7 x86 keep the line, the routing quirk and polling, as XP does. When the routines are not found, a line resource connects through `IoConnectInterrupt`; a message resource has no line behind it, so that start polls (below), or fails under mode 1 on x86 with no line offered.
    - WDK 7.1's `iointex.lib` (`WdmlibIoConnectInterruptEx`) was considered for amd64 and not taken: no further third-party object in the amd64 image, and since 36.5's review amd64 calls the documented `MmGetSystemRoutineAddress` the library wraps (record 15 section 4.3).
  - **The ISR.** A message ISR: EINT then IP acknowledged and the drain queued on a claim, as now. It returns `XhciIsr`'s answer: Windows permits shared message interrupts, so FALSE stays the answer for an interrupt this controller did not raise (record 15 section 4.3, corrected in 36.5's review; this line said "an MSI is never shared" until then).
  - **Decided (owner, 2026-10-09)**, after a Codex review of `7567dec`:
    - **The modes on Vista and 7.** 0: MSI where Windows granted a message, else the line, else polling. 3: the same as 0. 1: **the line, forced by itself** (owner, 2026-10-09; design record 18 section 4.3a): the requirements filter keeps only the configurations offering a line, on x86 and amd64, so the start receives the line; where a message is granted anyway (no line offered, the filter disabled or not replacing the list) it runs as 0 and logs that the line was asked for - except on x86 with no helper bound and no line offered, where the start fails, strictly, as XP's forced line with no line does (owner, 2026-10-09). 2: polling.
    - **Forcing the line** on Vista and 7 is mode 1 (above), or `MSISupported` 0 in the controller's hardware key, documented in `docs/using`.
    - **A message connection that fails**, or a message resource with no `IoConnectInterruptEx` found, polls for that start with its reason; there is no line to fall back to. The one exception is mode 1 on x86 with no helper bound and no line offered (above); a bound helper's failed connection still polls.
    - **MSI-X with one message is admitted.** The message resource does not tell it from MSI, and QEMU offers both. The driver reads which capability is enabled in the controller's configuration space (MSI or MSI-X) and records it, so a leg shows which one ran. More than one interrupter stays out. `CONNECT_MESSAGE_BASED` takes no message count, so a grant of more than one message (a raised `MessageNumberLimit`) is connected and served as one: only Interrupter 0 is enabled, so only its message is ever written, and the count granted is recorded (record 15 section 4.3, 36.5's review).
    - **The dead-line rule is the line's only.** xHCI 1.2c 4.17.5 clears `IMAN.IP` once a message write completes, so its predicate cannot show a lost message; a message delivery is not watched.
    - **The routing quirk** is not active under a message delivery (36.3).
    - **XP x64** ignores the key and keeps the line.
  - **What is recorded:** the delivery (MSI, MSI-X, line or poll) and its reason, in the counter block, the start log line and `XHCISNAP`.
- [ ] 36.16 the MSI legs:
  - **Design record 18's legs, first**: R (the requirements list each guest's PCI driver offers, x86 and x64, read with the strip off: R1 and R2 decide whether it is enabled), R3 (the unsigned boot-start helper loads), R4 (the device before the restart the INF asks for); then F (mode 1 with `MSISupported` 1 read as the line, x86 and x64), H (the helper missing, broken, old; H1a mode 1 with no line offered failing the start), L (two controllers, rebalance, surprise removal, a failed start), U (updates, the downgrade) and D (98 SE's load of the new import - a required check -, ME, 2000, XP, the x64 package unchanged, Driver Verifier).
  - **R read (2026-10-10, QEMU, development host A; record 18 section 11.1):** Vista SP2 and 7 SP1, x86 and x64, all three `qemu-xhci` variants offered the same list: one configuration of the memory BAR and its alternative, a device-private (the device's own: Windows keeps it with `MSISupported` 0 too), then the message heading the interrupt run with the line as its alternative. A line is offered beside the message everywhere, so the strip is enabled (`HCD_MSI_STRIP_DEFAULT` 1) for that one admitted shape; any other shape is kept as "unsupported shape". R3: the unsigned boot-start helper loads on an ordinary boot on both x86 guests. R4: before the install's restart Vista and 7 x86 hold the controller unstarted (problem code 28 on 7, 1 on Vista, not 14); x64 asks no restart and starts at once. M (MSI, MSI-X and the default on all four; the helper disabled on x86 polls, then binds again) and D (98 SE, ME, 2000, XP: the line, no helper, the new import loads on 98 SE) passed.
  - **QEMU** (`qemu-xhci` offers MSI and MSI-X): Vista and 7 x64, and x86 with the helper driver, with HID, Bulk-Only and UAS storage, and audio, under mode 0 and mode 3. Also disable and enable, remove and rescan, a forced recovery, and the controller's D3 and back. These run three ways, because the launchers' `qemu-xhci,p3=0` (`scripts/qemu-nt6-common.ps1` ~253) leaves QEMU's default, under which `qemu-xhci` offers MSI-X and not MSI (Codex review of `6cac234`, from QEMU's `hw/usb/hcd-xhci-pci.c`; confirmed on the guest before the legs), and Windows prefers MSI-X where both are offered: `msi=on,msix=off`, read as MSI; `msi=off,msix=on`, read as MSI-X; and the launchers' default, read as whatever Windows granted. `XHCISNAP` must name each correctly. Then `MSISupported` 0, read as the line; mode 1 under a grant, read as the message with its log line; and a `qemu`-flavour failed message connection, read as polling with its reason. Mode 1 under a grant reads as the line once the filter is enabled, and as the message with its log line where no line is offered.
  - **Down-level:** 98 SE, 2000, XP x86 and x64, and Vista and 7 x86 without the helper, reading as the release before it, with the line (on Vista and 7 x86 through the filter); where no line is offered there, a message grant reads as polling with "message not connected", or a failed start under mode 1. The x86 binary loads on 98 and ME with the new import.
  - **The owner's E7250** under Windows 7 x64, with `XhciIntelIrqRoute` 0, read as MSI with the mouse and the stick working.
  - **The owner's E460 and P14s Gen 1** under Windows 7 if installed, read as MSI.
- [x] 36.17 `GUID_DEVINTERFACE_USB_DEVICE` (`{A5DCBF10-6530-11D2-901F-00C04FB951ED}`) on every device the bus presents (issue 17 item 3):
  - **Where.** Every device PDO and every external hub's PDO, as usbhub registers it; not the root hub, on which usbport registers none.
  - **A composite the bus splits** has no parent devnode, so the interface goes on one function's PDO: the function that owns the device's first interface (its `FirstInterface`, the group identity the door already uses for the connection's driver key, design record 13 section 8.10), not a hard-coded `MI_00`. Functions start and stop independently (`hcdPdoFunctionRelease` leaves siblings running). **Decided (owner, 2026-10-09):** the interface stays on that function and follows its state, so when it has no driver, fails its start or is disabled, the device has no interface while its siblings run. This is documented in record 13 section 8.10 and `docs/using`.
  - **The lifecycle** (`IoSetDeviceInterfaceState`'s contract):
    - registered once, and enabled after a successful start;
    - disabled at stop, at surprise removal, and at removal, never twice: a removal after a surprise removal finds it already off;
    - the string freed, idempotently, on every path that destroys the PDO: the normal remove, the retained PDO after a disable, and Windows 98's deferred deletion (`hcd_pdo.c` ~504, ~2317, ~2532), never only at `IoDeleteDevice`;
    - enabled again when a retained PDO is started again.
    - Best effort, as `HcdDoorHubStart`, `HcdDoorHubStop` and `HcdDoorHubRemove` are for the hub interface: a failed registration is a missing interface, not a failed device.
  - **Legs.** On every guest, x86 and amd64:
    - a SetupDi enumeration of the class: a device, a hub and a device behind it, a split composite;
    - arrival and removal notifications for the class (`RegisterDeviceNotification` with `DBT_DEVTYP_DEVICEINTERFACE`, on 98 and ME as on NT) across a hot-plug, a surprise unplug and replug, a disable and enable of one device in Device Manager, and a disable and enable of the controller;
    - a split composite whose representative function is unbound or disabled while a sibling works, read as no interface, as documented.
  - **Done 2026-10-10** (`465825d..b489836`, design `5b073c3`; the Codex loop clean at its fourth round; ticked at the integration of wave 2 on the i17 worker's recommendation, which the owner accepted). The interface sits on the group-first PDO (`Group` equal to its serial) and its string is freed in `hcdDeletePdo`. Legs on the `release` packages (worker p36-i17, development host A, QEMU, 2026-10-09/10; `out\p36-i17\legs\report.md`): on 98 SE, ME, 2000, XP, Vista x86 and 7 x86 and x64, the SetupDi list found one interface per device PDO, one on the external hub's PDO and the device behind it, one for the split audio device, and none on the root hub; on 98 SE, 2000, XP and 7 x86 and x64, arrival and removal notifications across a hot-plug, a surprise unplug and replug, a device's disable and enable, and the controller's (every interface away and back). The composite clause was read only in part: QEMU's `usb-audio` splits into one function, so disabling `MI_00` removed its interface and enabling it brought it back, but no sibling function was there to keep working. Passed. Three hangs of the 98 SE guest during these legs (one after a surprise removal of a stick whose install was still pending, then its replug) are tracked separately, by worker p36-w98hang; from release evidence they are not attributed to the driver.
- [x] 36.18 the device's real speed on every hub door (issue 17 item 6), both halves (owner, 2026-10-09):
  - **The `_EX` form's `Speed` byte** reads 3 for a SuperSpeed connection and 4 for SuperSpeedPlus, as Intel's USB 3 drivers for Windows 9x report it, **on Windows 98 and ME only** (owner, 2026-10-09, after the static reading below): where `IoIsWdmVersionAvailable(1, 0x10)` is FALSE, the test `hcd_pdo.c` already uses for them. Every NT target keeps 2 for both, and `_EX_V2` gives NT tools the real speed. 0 to 2 are unchanged. No Microsoft header defines 4, and the WDK 7.1 headers stop at 2, so design record 13 section 8.4 is amended to replace the High Speed ceiling.
  - **`IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2`** (`0x0022045C`), new.
  - **The structure.** `ConnectionIndex`, `Length`, `SupportedUsbProtocols` and `Flags`, 16 bytes, written byte by byte like the rest of the door. Three separate sources (Codex review of `7567dec`, from Microsoft's `USB_PROTOCOLS` and `_EX_V2_FLAGS` pages):
    - **`SupportedUsbProtocols`, out:** the **port's** signalling protocols, not the device's and not their intersection. An empty port has them; a Full Speed device on a USB 2.0 port leaves `Usb200` set. A USB 3 logical port does not gain `Usb110` or `Usb200` from its USB 2.0 companion, which is a port of its own here.
    - **The operating flags** (`DeviceIsOperatingAtSuperSpeedOrHigher`, `...SuperSpeedPlusOrHigher`): from the connection's negotiated speed.
    - **The capable flags** (`DeviceIsSuperSpeedCapableOrHigher`, `...SuperSpeedPlusCapableOrHigher`): from the device's BOS (its SuperSpeed and SuperSpeedPlus capability descriptors), not from the speed, since a SuperSpeedPlus device may be running at 5 Gbit/s. The BOS is read only on the SuperSpeed enumeration path today (`xhci_enum.c` ~280), so a USB 3 device running at High Speed reports neither capable flag unless its BOS is read too; this task decides which, and says so.
  - **Input and output** (Microsoft's IOCTL page): the caller sets `SupportedUsbProtocols` to the protocols it asks about, and Windows 8 refuses with `STATUS_INVALID_PARAMETER` unless `Usb300` is set; that is a property of the request, not of the port. `Length` and the input buffer must be `sizeof(USB_NODE_CONNECTION_INFORMATION_EX_V2)`, and the output buffer at least that, checked apart as `hcdDoorConnInfo` checks both (`hcd_door.c` ~1453); the byte count returned is the structure's. A port out of range is refused as the `_EX` form refuses it.
  - **Static reading of the `_EX` byte's consumers** (Codex review of `7567dec`, disassembly in `tools\win7-x86-extracted\ui` and `tools\vista-x64-extracted\ui`): Vista's and 7's `usbui!UsbItem::IsHighSpeed` tests the byte at `+0x17` for exactly 2, so 3 and 4 read as not High Speed, and the hub page's `AddFullSpeedDevicesToListView` lists such a device among the Full Speed devices: a misclassification, not a crash. XP's and XP x64's pages send the older `0x22040C` and never read the byte. Hence 98 and ME only; the reading goes into `legal-provenance.md` when the change is made.
  - **Host vectors:** the input flags and lengths, a valid input with a short output buffer, the returned byte count, an empty port, a slower device on a faster port, a SuperSpeedPlus-capable device at 5 Gbit/s, and each `_EX` value on 98 and ME and on NT.
  - **Legs.**
    - SweetLow's usbview (http://sweetlow.orgfree.com/download/usbview.zip) on 98 SE and 7: a SuperSpeed device on a root port and behind a USB 3 hub, a High Speed device, and a hub port with nothing on it.
    - Microsoft's own pages with a SuperSpeed device attached: the hub's Power tab on 98 SE and ME with 3 and 4 (a synthetic 4 on the `qemu` flavour where no SuperSpeedPlus device is at hand), and the hub's Advanced tab on Vista and 7, read for **which list the device lands in** (High Speed, as before). Read by the owner.
  - **Done 2026-10-10** (`465825d..b489836`, with 36.17 and 36.19; ticked at the integration of wave 2 on the i17 worker's recommendation, which the owner accepted; **bench clause owed (2.2.0.3)**: the owner's own reading of Microsoft's pages). The capable flags come from the BOS or the operating speed, and no BOS read was added at High Speed (record 13 section 8.4). Legs on the `release` packages (worker p36-i17, development host A, QEMU): on every guest, x86 and amd64, the `_EX` byte read 3 for a SuperSpeed stick on 98 SE and ME and 2 on 2000, XP, Vista x86 and 7 x86 and x64; `_EX_V2` read 16 bytes, `SupportedUsbProtocols` 4 on USB 3 ports, 3 on USB 2.0 root ports and 1 behind the Full Speed hub, flags 3 for the SuperSpeed stick and 0 elsewhere, and refused a request without `Usb300`, a `Length` of 12 and a port past the end with error 87 and an 8-byte output with error 122. SweetLow's usbview walked the tree on every guest and named the stick "Super". On 98 SE the root hub's Power tab rendered with the stick attached; on Vista and 7 the Advanced tab put the stick in no Full Speed list. No Low Speed device was attached. Passed, apart from the bench clause.
- [x] 36.19 polling rates a client chooses: `XhciFastPollFsLs` 4 (issue 17 item 2; design record 13 section 13 gains a section for it):
  - **The codes.** Under mode 4, an interrupt endpoint whose `bInterval` in the caller's descriptor is 128, 129 or 130 is programmed at Interval 2, 1 or 0 (2000, 4000 or 8000 Hz). Every other `bInterval` reads by Table 6-12. Mode 4 does not do modes 2 and 3's 16-63 mapping, and they do not honour the codes.
  - **Why an opt-in.** 128 to 255 ms are legal Full- and Low-Speed values, so a device may declare a code. And a client that sends codes with the mode off gets what they legally mean: 128 ms at Full Speed. The procedure is mode 4 first, then hidusbf set to the codes, and the release notes say so.
  - **The ceiling.** 125 us, Interval 0, at any speed; 131 and above are not codes.
  - **Scope.** Full and Low Speed on a root port only, as modes 2 and 3; High Speed and SuperSpeed anywhere. Never isochronous, bulk, or a hub's own endpoint.
  - **Keyed on the descriptor's byte** (`BInterval`), not on the Interval Table 6-12 derives from it: 10 at Full Speed, 15 after the High Speed clamp.
  - **Applied before the interval cap** (35-T.7), which leaves Interval 2 or less unchanged. Today a capped endpoint is never fast-polled, so on an AMD controller the cap would otherwise silence every code.
  - **A refusal falls back to Interval 3 (1 ms)**, not to Table 6-12's reading of the code, which would leave the device polled every 128 ms. The endpoint keeps its Table 6-12 Interval and its fallback apart, so modes 2 and 3 revert exactly as today. `RESET_PORT` and the endpoint recycle rebuild from the stored `pipe->Ep` (`hcdCfgConfigureAdd`), so they keep the chosen Interval and fallback without reading the descriptor again.
  - **SuperSpeed composite functions.** `hcdCfgFunctionEndpoint` (`hcd_cfg.c` ~654) does not take the caller's `bInterval` at SuperSpeed or SuperSpeedPlus, so a function's code there would be lost. This task changes it: the endpoint and its companion are parsed from the device's own configuration (`dev->Selected`), keeping burst, Mult and Max ESIT Payload; then the caller's matched byte replaces `BInterval`, the ordinary Interval is derived from it, and mode 4 is applied through the same decoder and fallback bookkeeping as every other endpoint (`hcdCfgFastPoll`). Not by sending a seven-byte copy through the USB 2.0 parser, and not by writing the Interval directly. An integration vector with a split SuperSpeed device, a nontrivial companion, and a device byte that differs from the caller's code.
  - **A composite function's later `SELECT_INTERFACE`** rebuilds from the device's own configuration (`hcd_cfg.c` ~1549) and so loses a code, as it loses modes 2 and 3's mapping today (design record 13 section 13.5). **Decided (owner, 2026-10-09):** accepted and documented, as for modes 2 and 3, in record 13 section 13 and the release notes; HID class drivers do not reselect an interface. Revisited if a report shows it.
  - **Docs:** record 13 section 13 gains mode 4; record 17 section 4.7, which orders the caller's `bInterval`, the cap and then fast polling, gains mode 4's place before the cap; the release notes' registry table and the procedure.
  - **Host vectors** in `test_fast_poll`: 4 accepted and 5 off; each code at Low, Full and High Speed and SuperSpeed; 127, 131 and 255 untouched; Full Speed behind a hub untouched and High Speed behind one applied; isochronous and bulk untouched; the two conventions kept apart; no double application; the revert landing at 3. **Legs:** hidusbf set to the codes on 98 SE and 7 in QEMU, read in the `fastpoll.*` records; mode 4 with an unpatched device reading as the release before it.
  - **Done 2026-10-10** (`465825d..b489836`, with 36.17 and 36.18; ticked at the integration of wave 2 on the i17 worker's recommendation, which the owner accepted). Built x86 and `-amd64`, all flavours, every gate and the host suites passing at `3283220` (after it, release-notes text only). Legs on the `release` packages (worker p36-i17, development host A, QEMU, 2026-10-09/10): on 7 x86 and 98 SE, hidusbf's codes 128, 129 and 130 under mode 4 programmed Interval 2, 1 and 0 (`fastpoll.open` records; on 98 SE also read from the Output Endpoint Context), modes 2 and 3 ignored the codes, a Full Speed mouse behind a hub was not sped up, mode 5 read as 0, and modes 0 to 3 with the mouse unpatched read as before; the mouse moved throughout. Passed.
- [x] 36.20 the docs of what `2.3.0.0` ships, moved here from SUSP.3 because this phase cuts polling and MSI (Codex review of `7567dec`):
  - record 15 section 14's list: `implementation-invariants.md`'s "Interrupt Delivery" (a pin-0 controller polled, not refused), `architecture.md`'s line-based delivery, the release notes' "Controller" row and rows for the switches, the acceptance test's step 3 and its stop on the pin, `xhciqual/hardware-testing.md` and record 01's C4 note, record 13 where it describes the ISR, `source-files.md`, the locking record, `xhcisnap/README.md`;
  - the MSI delivery, 36.3's `XhciIntelIrqRoute`, 36.17 to 36.19 and 36.22's transfer-size value, in the release notes, `docs/using` and the records they amend;
  - the user-facing statements of 36.15's decisions (record 15's own mode rules are reconciled in 36.5, before code);
  - selective suspend's (was SUSP.3, joined 2026-10-09): record 13 sections 5, 6.5, 10.2 and 10.9; the release notes (the idle-policy limitation replaced by the switch, off by default, and what turning it on does); the decisions table's 28.3 row; `source-files.md`; the locking record; the run record.
  - **Done 2026-10-10** (docs only, branch `p36-docs`: `6c7559b`, `75eb39e`, `2007beb`, `df04e7a`, `ccf3036`, `ac90c14`, `64e72b0`, `265cda0`, `1bdd0eb`, `59364b3`; `265cda0` also maps strict mode's files and `src\uas\`, which no earlier task had): the release notes marked "TEST 2.2.0.3, bench pending" (a "What `2.3.0.0` changes" section and Targets row; polling, MSI, `XhciMaxTransferKB` and the per-device value, `XhciWideMap` off, the port switchover, selective suspend, composite parents, USBView strings, the Low-Speed mouse, `XHCIQUAL`'s caveats and system header, the Verifier fix; sixteen INF values; the known notes), the acceptance test, the README's Tuning, `source-files.md`, three `lessons.md` entries, the invariants, `architecture.md`, record 13 (5.4, 5.5, 6.5, 10.2, 10.9), the 28.3 row, `xhcisnap/README.md`. Codex docs review thread `01a12517`: round 1 eleven P2s (`64e72b0`), round 2 seven (`1bdd0eb`), round 3 four and a P3 (`59364b3`), round 4 clean - the loop ended. **Owed** (ticked with these left to their owners): the run record's selective suspend legs (36.35, 36.39); the matrix's readings for the release notes' "being read" sentences (composite on the other systems, strings on 98 SE and ME, the update from `2.2.0.0`, the downgrade); 36.41's re-measure on stage A.
**The transfer size (36.21 to 36.27; owner, 2026-10-09).** Storage over
UAS moves 1 MiB per command by default, where today `xhciuas.sys` offers
64 KiB, and a bulk transfer goes to the controller in as few TDs as
measurement shows is worth it (today at most 128 KiB each). A registry
value sets the command size from 64 KiB up to 2 MiB: lower for a device
or controller that misbehaves at large transfers, higher to try more.
Everything below is sized for the 2 MiB the value can reach, not the
1 MiB default. Sizes here are binary (KiB, MiB); the value's name keeps
`KB`. Drawn up as planned work and reviewed twice, once before it was
moved here; the findings are folded in.

**Two sizes, not one.** The command size is what one UAS command IU asks
the device to move: `xhciuas.sys` sends each SRB as one command and one
data URB whatever its length (`uas_xport.c` `uasSlotPrepare`), and the
adapter descriptor's `MaximumTransferLength` is what tells the storage
class driver how large an SRB to build. The TD size is how much of one
data URB the HCD puts on the ring at once: `hcdPlanChunk` (`hcd_io.c`)
cuts a URB into chunks of at most 32 pages, packet-aligned, and maps and
submits the next only after the previous one completes. A 1 MiB command
therefore does not need a 1 MiB TD, and a sized SRB does not make a
sized TD: the planner still bounds each chunk by the map grant and 32
SG elements (`hcd_io.c` ~99). The work is staged so each stage is
measured before the next is taken:

- **Stage 1, the command size.** Raise what `xhciuas.sys` advertises to
  the value in effect, with the HCD's transfer engine unchanged (the
  value, its query and the instrumentation are new HCD code). With
  page-aligned buffers and a grant of at least 32 map registers a 1 MiB
  command goes as eight sequential 128 KiB TDs and 2 MiB as sixteen; an
  unaligned buffer needs nine or seventeen, and a smaller grant more
  (`hcd_io.c` ~96's offset subtraction and packet rounding). Where the
  storage stack does send full-size SRBs, full 1 MiB commands in place of
  full 64 KiB ones remove 15 of every 16 command and status IU pairs,
  and 2 MiB 31 of 32. Two consequences are kept, not changed: the
  whole-command timeout (the SRB's, or 30 seconds, taken once and counted
  down over the command's life, `uas_xport.c` ~569 and ~915, never
  scaled or restarted per chunk), and the caller's payload held under
  partial MDLs over pages its caller already locked (`uas_mem.c` ~31):
  up to 30 active streamed commands (`uas_xport.c` ~1595) represent
  60 MiB of caller payload at 2 MiB each against 1.875 MiB at 64 KiB;
  page coverage of unaligned buffers and requests still queued can
  retain more. That is residency, not new common buffer, and 36.26
  validates it.
- **Stage 2, bounded pipelining**, if stage 1's readings show the gaps
  between chunks cost throughput (the ring empty while the next chunk is
  mapped): more than one chunk of one URB on the ring at once. Not a
  removal of `Exclusive` (`hcd_io.c` ~938), which keeps another request
  from landing between two chunks: it needs per-chunk mappings, ordered
  completion of the URB, an early short or error with later chunks
  already queued, and a forward-progress rule when map registers are
  short. Two 32-TRB chunks do not fit today's 62 usable TRBs, so this
  stage also grows the ring modestly (128 TRBs, or 31-page chunks). Even
  128 TRBs moves the shared pool geometry: the pool/EP0 stride assert
  (`xhci.h` ~988), the isochronous limits derived from the pool count
  (~3721) and stream allocation (`hcd_cfg.c` ~3294). So the isochronous
  decoupling and the memory, stream and snapshot tests those touch come
  with whichever stage first changes the pool ring: stage 2 if it grows
  it. The event-burst review below comes with stage 2 whatever its ring,
  since two 31-page chunks in today's ring already put more than one TD
  of a URB on it.
- **Stage 3, larger TDs**, only if stage 2 falls short: the limits below.

**What the TD size touches** (stage 3, and in part stage 2):

- the transfer rings: 64 TRBs (1 KiB) each, 62 usable (one Link, one kept
  empty; `XhciRingCapacity`, `xhci_ring.c` ~38), carved from the one
  fixed common buffer (`xhci.h` `XHCI_POOL_RING_TRBS`, ~840 and ~875). A
  1 MiB TD over the HCD's page-split mapper is 256 data TRBs aligned and
  257 unaligned (the mapper splits at page boundaries, so no extra 64 KiB
  splits: `hcd_dma.c` ~305); 2 MiB is 512 and 513. Fixed single-segment
  ring sizes and what fits are in the table below; a segmented layout
  needs its own Link-TRB and allocation accounting. Above one page a ring
  must be placed by its physical address, not an aligned offset, to keep
  off a 64 KiB boundary (`xhci_mem.c` ~37's proof holds only to a page);
- the stream rings, outside the pool: up to 31 rings on each of a UAS
  device's three stream endpoints, limited to one page today
  (`xhci_stream.c` `XhciStreamLayout` ~130, `hcd_cfg.c` ~3294). With the
  context array and its alignment, 16 KiB rings are 512 KiB per endpoint,
  1.5 MiB per UAS device before placement padding, and 32 KiB rings
  3 MiB, so status streams may keep small rings;
- the isochronous limits, which derive from the pool ring today
  (`XHCI_XFER_MAX_ISO_TRBS`, `IsoScratch`, `HCD_ISO_BLOCK`, `hcd_urb.c`
  ~409): kept at today's values, decoupled from the bulk ring in the
  first stage that changes the pool ring;
- each record's SG list (`HCD_SG_ELEMENTS`, 24-byte elements): 257 or 513
  per record that can be mapped at once (four per pipe, two per stream
  pipe), allocated with the pipe, not one list shared by the records;
- the TRB build arrays on the stack (`hcd_io.c` ~1160, `hcd_enum.c` ~939,
  `hcd_hub.c` ~1218): bulk moves off the stack to a per-pipe scratch
  reused under the lock; control and enumeration keep their small arrays;
- the pure core's caps (`XHCI_XFER_MAX_DATA_TRBS`, `XHCI_PIPE_CHUNK_ELEMENTS`,
  `XHCI_PIPE_TD_MAX_BYTES`), and `hcdPlanChunk`, which bounds a chunk by
  pages and never by a byte limit: it becomes min(remaining, the size in
  effect, the map grant, the ring's room), packet-aligned when not the
  last, with the host tests calling the production arithmetic;
- the SG walk `xhciXferEmitSgList` (`xhci_xfer.c` ~543), which rescans the
  list for each element: linear for the HCD's ordered lists, its time
  under the controller lock measured;
- admission: a bulk request with no ring room completes busy today; with
  large TDs it waits unmapped instead, and the design says how many TDs
  a ring holds.

| Fixed ring TRBs | Usable | Worst-case 1 MiB TDs | Worst-case 2 MiB TDs | Controller common buffer, before padding |
|---:|---:|---:|---:|---:|
| 64 (today) | 62 | 0 | 0 | 400 KiB |
| 128 | 126 | 0 | 0 | 432 KiB |
| 512 | 510 | 1 | 0 | 624 KiB |
| 1,024 | 1,022 | 3 | 1 | 880 KiB |
| 2,048 | 2,046 | 7 | 3 | 1,392 KiB |

Unchanged by design: the 64 KiB TRB buffer rule and TD Size's five bits,
UAS's command and 288-byte status buffers, `usbstor.sys`'s own limits on
the Bulk-Only path (`XhciForceBulkOnly`), and the event ring's 256
entries, provided the burst review passes. A TD gives one normal
successful completion, not one per TRB, but an early short, an error or
a stop adds trailing events (`xhci.h` ~3638 remembers four tails); with
more TDs outstanding, the review argues those tails, the 256 entries,
the drain budget and moderation together, in the first stage that puts
more than one TD of a URB on the ring.

**The value.** A working name `XhciMaxTransferKB`, a `DWORD` in the
controller's driver key, default 1024, range 64 to 2048 in 4 KiB steps;
absent, mistyped, out of range or off-step reads as the default, with a
note. It is read once at controller start (`hcdReadValues`) and applies
to every device on that controller; it is not a per-device quirk. It is
an **advertised command limit**: what the adapter descriptor tells the
class driver, not a ceiling enforced on whatever SRB arrives.

The query. `xhciuas.sys` learns the value through a query of its own on
the private interface beside `xhci98_streams.h`, separate from the
streams requests (a High-Speed UAS device opens no streams), at its start
before pipe selection and before any LUN is exposed. Its contract:
- the request is pointer-free, carries a signature, version and size,
  and the reply is the normalised command-policy bytes, independent of
  streams and of the map registers available at the moment;
- `xhciuas.sys` sets its value to 64 KiB on every initialisation path
  before it asks, and the descriptor and the pipes'
  `MaximumTransferSize` both read that one stored value (no second
  registry read);
- unsupported (an older HCD completes an unknown internal IOCTL
  `STATUS_NOT_SUPPORTED`, `hcd_urb.c` ~1685) means 64 KiB, never an
  assumed 1 MiB;
- a malformed successful reply (signature, version, size, range or step
  wrong) means 64 KiB with its own note and counter;
- a transport failure (removal, no memory) is kept apart from both, as
  `uas_streams.c` ~35 keeps transport status apart from reply status: it
  means 64 KiB with its own note, and the start goes on through its
  ordinary sequence, whose own failures (selection, streams) still fail
  it;
- host vectors for the production reply-validation and fallback helper
  (unsupported, malformed, transport failure, each valid value); the
  mixed binaries themselves are guest legs in 36.26, since `test_uas`
  links only the pure `uas_iu.c` and reaches no dispatch or start.
- the counters handed to the HCD (36.21) go in bounded records copied
  into HCD-owned storage, never through the synchronous PASSIVE-only
  request helper from a path holding the UAS lock, and unavailable or
  stale telemetry reads apart from zero.

Activation. Storage class drivers keep the adapter descriptor they read
in their own device extensions, and a UAS stop and start keeps the LUN
PDOs already known (`uas_fdo.c` ~397 and ~537), so a controller restart
alone does not show a class driver re-reading it. A change therefore
takes effect at the next reboot; a controller restart with the storage
stack removed and enumerated again counts as well only once 36.26 shows
it refreshes the descriptor. An SRB already sized to the old value is
carried as it is (the HCD chunks it) and never cut short or refused.
Holding a device to a size live would need an SRB splitter (CDB and LBA
rewritten, sense and status gathered), which is not planned.

**Windows 98 and ME.** NUSB's storage path runs through `USBNTMAP.SYS`,
read statically as doing its I/O through a 64 KiB bounce MDL
(`runs/run-31.md`, NUSB's storage path); if that holds, a larger
descriptor changes nothing there. 36.21 measures the SRB sizes 98 SE and
ME actually send before anything is promised for them.

- [x] 36.21 (was MB.0) instrumentation, then the baseline. Each counter has a
  named producer:
  - `xhciuas.sys` counts SRB lengths and commands outstanding (beside
    its `Counters[]`, `uas.h` ~219, which nothing reports today) and
    hands them to the HCD over the private interface, which carries them
    in its snapshot;
  - the HCD counts data URB length, chunks per URB and the map grant;
  - the gap, per data pipe or stream and only while a URB has data not
    yet on the ring, measured as two parts: software time from a chunk's
    retirement to the next chunk's publication, and the observed
    software-empty interval (the ring empty in software's view with that
    URB unfinished). Plain ring-empty time includes idle and is not the
    gap. The ring's dequeue is software's view, advanced as events are
    handled (`xhci_ring.c` ~27), and a Transfer Event carries no
    timestamp. So the interval starts when software processes the
    completion and observes the ring empty, and omits any hardware-empty
    time before that while event delivery, moderation or DPC scheduling
    delayed the observation. Hardware starvation itself is recorded
    unmeasured unless a separate trace method shows it, and 36.23 decides
    on the observed interval, CPU cost and throughput;
  - the HCD region's header grows append-only (`xhci.h` ~8525's
    size-based extension), the outer snapshot header and schema
    unchanged, and `XHCISNAP` decodes the new fields; `test_snap` and
    `XHCISNAP`'s selftest show an older snapshot still read, and an
    actual older `XHCISNAP` reading a newer snapshot with the new fields
    simply absent.

  Then on this phase's build before 36.22, plus that instrumentation, on
  the E460 and the P14s Gen 1 under 98 SE and 7 (owner): ATTO Disk
  Benchmark and a large copy over UAS and Bulk-Only, recording the
  benchmark's version, queue depth, the device, connector,
  `XhciImodInterval250ns` in effect, flavour and repetitions. The SRB
  sizes Windows ME sends are read in its QEMU guest, a VM leg of this
  task
  - **Done 2026-10-09, bench clause owed (2.2.0.3)** (`f3a395c`, review
    fixes `ac5bbd0`; built x86 and `-amd64`, all
    flavours, every gate and the host suites passing). `xhci_xstat.c`
    (stamps by the time-stamp counter with the processor's APIC ID, the
    gap's two parts, publication, completion and mapping costs);
    `xhciuas.sys`'s record over `IOCTL_XHCI98_TELEMETRY`; the HCD header
    appended to 202 words and decoded by `XHCISNAP`; `releases\2.2.0.0`'s
    `XHCISNAP` read the newer snapshot with the new words absent. The
    QEMU baseline (development host A, `out\p36-mb\xp64\notes.md`): XP
    x64, UAS at SuperSpeed, raw reads at 64 KiB commands 214 / 281 / 343 /
    353 MB/s at 64 KiB / 256 KiB / 1 MiB / 2 MiB requests, every SRB at
    most 64 KiB; Bulk-Only 210 / 340 / 373. Windows 98 SE (NUSB) and ME,
    read in their QEMU guests (`out\p36-mb\w98n`, `out\p36-mb\me`): no
    SRB above 64 KiB reaches `xhciuas.sys` or the HCD, one at a time,
    whatever the descriptor says (`USBNTMAP.SYS`), and file reads no faster
    at 1024 than at 64. The x86 `hcdCpuId` did not preserve EBX around
    CPUID (STOP 0xB8 on Windows 2000, found by the integration smoke leg;
    fixed by `f22a42b`, here `fe76f91`); on 98 SE and ME the same clobber
    lowered the IRQL inside the controller lock, which is why those
    readings' HCD bulk-URB counts fell short of their chunk counts - their
    sizes come from `xhciuas.sys`'s record, which it did not touch; re-read on
    the fixed build, 98 SE's counts agree (3408 URBs, 3408 chunks). Owed:
    the bench (E460, P14s Gen 1, 98 SE and 7, ATTO and a large copy)
- [x] 36.22 (was MB.1) stage 1: the value and its query (the contract
  above), `xhciuas.sys`'s descriptor (`MaximumTransferLength`,
  `MaximumPhysicalPages` as bytes / page + 1) and pipes'
  `MaximumTransferSize` from its one stored value, the INF rows and gate,
  the docs; host vectors in `test_uas`. Also what 36.26 needs from its
  first run, whatever stage the work stops at:
  - the test client that sends sized SRBs;
  - a constrained map grant;
  - the qemu flavour's injection extended to bulk and stream TDs (short,
    error, stop, timeout), since today's endpoint faults target a
    one-TRB interrupt TD (`hcd_inj.c` ~36, ~710).

  36.21 repeated at 64 KiB, 1 MiB and 2 MiB
  - **Done 2026-10-09** (`4909421`, review rounds to `ac5bbd0`, clean at
    round 7, thread `01a1209e`; the device's own limit `8a7c21b..90c625c`,
    clean at round 2, thread `01a12143`; built x86 and `-amd64`, all
    flavours, gates and host suites passing). `XhciMaxTransferKB`
    (`hcd_ctl.c`, `XhciXpolValue`), `IOCTL_XHCI98_TRANSFER_POLICY`
    (`hcdPolicyIoctl`; `uasAskPolicy`, `UasPolicyValue`, `test_uas`), the
    note in the UAS device's hardware key, the INF rows and gate. Owner,
    2026-10-09: the default stays 1024 in both INFs; and `xhciuas.sys`
    also honours Microsoft's per-device `usbstor\VVVVPPPP`
    `MaximumTransferLength` (65535 to 2097120, nearest 4 KiB; record 13
    section 11.5, `UasDeviceLimit`); widened by the owner on 2026-10-10 to
    65535 to 2097152, so the 2097152 tools write is 2 MiB (with 36.24). The test client is
    `test\xfertest`; the constrained grant `XhciQemuMapGrant`; the qemu
    bulk faults 14 to 17 hex (`build-and-test.md`). 36.21 repeated (XP x64,
    QEMU): at 1024, 134 / 394 / 593 / 600 MB/s at 64 KiB / 256 KiB / 1 MiB
    / 2 MiB requests, a 1 MiB URB 8 chunks (9 unaligned); at 2048, 1 MiB
    696, 2 MiB 711 (550 unaligned), 16 chunks (17); at 64, 177 / 264 /
    243; VERIFY clean at each; 4096 noted out of range and 1024 used.
    The device's own value read on XP x64 in QEMU (`out\p36-mb\xp64\notes.md`):
    none, 1 MiB commands; 65535, 64 KiB commands (source 5); 2097120, 2 MiB
    commands with the controller at 1024; 2097152, refused (reason 3) and
    1 MiB, with another model's key ignored. Owed in 36.26: the rest of the
    geometry legs
- [x] 36.23 (was MB.2) the decision on stage 2, from 36.22's readings (owner): command
  rate, chunk rate, the gap's two parts, CPU cost and throughput
  - **Taken by owner decision 2026-10-09**: stages 2 and 3 are taken
    whatever the readings, and the decision does not choose the default
    (it stays 1024). The readings, recorded anyway (XP x64 in QEMU,
    development host A, guest QPC under TCG, so large run-to-run variance):
    command rate at 1 MiB reads 566 commands/s at 1024 against 5230 at 64
    KiB commands (baseline, 343 MB/s); chunk rate 4524 chunks/s at 1024
    (8 a URB); the gap, retirement to the next chunk's publication, 120 us
    mean at 1024 and 84 us at 2048, the software-empty interval the same
    (the ring empty for all of it); CPU cost a publication 34 us at 1024
    (27 at 2048) against 14 us at 64 KiB, a completion pass 3.8 us against
    2.0, a next chunk's mapping 22 us (9 at 2048); throughput as 36.22
    above, 1 MiB reads 343 MB/s at 64 KiB commands, 593 at 1024, 696 at
    2048
- [x] 36.24 (was MB.3) stage 2, if taken: its design reviewed to
  convergence, the event-burst review included whatever the ring, then
  the code with host vectors in `test_td`, `test_xfer` and `test_ring`.
  If it grows the pool ring, also the isochronous decoupling, the stride
  assert, `test_iso`, `test_membuf`, `test_stream` and `test_snap`
  - **Done 2026-10-10, legs owed to 36.26** (design `b76c16f..6aa2ae2`; the HAL reading, branch `p36-hal`, merged at `bb007ed`; code `b26f5c8`, `9b0c85f`, review fixes to `297d42a`, clean at round 4, thread `01a1236a`; the suspended-controller ordering `f9c02ec`). Record 13 section 11.6: `XhciWideMap` on by default (owner, 2026-10-10, on 11.6.1), 0 off - **then off by default, 1 opts in (owner decision 2026-10-10, after Codex session `01a124ef`; p36-int4)**; eligibility: every qualifying bulk OUT URB, bulk IN only on a stream; promotion only on NT, at the grant `0x80001`, past the start's register-free check and on a NULL base. Built x86 and `-amd64`, every gate and the host suites passing. Smoke (XP x64 in QEMU, development host A, `out\p36-mb\xp64\notes.md`): promotion fires at the default and not at 0, VERIFY clean either way
- [x] 36.25 (was MB.4) stage 3, if taken: its design reviewed to convergence (fixed
  rings placed by physical address, or a fixed set of page segments
  allocated with the pipe, sized from the table above and qualified on a
  low-memory Windows 98 machine and across repeated starts and
  hot-plugs), then the code; host vectors in `test_td` (which models
  the planner and mapper and hard-codes 32 elements and 80 pages today),
  `test_xfer`, `test_ring`, `test_stream`, `test_iso`, `test_membuf` and
  `test_snap`, for whatever 36.24 did not already cover; the injection
  reaching a 257- and 513-TRB TD
  - **Done 2026-10-10, legs owed to 36.26** (design, record 13 section 11.7, `8ee1c0d..cb4f0b0`, clean at round 4, thread `01a1239f`; code `a008b3e`, `520e0f5`, clean at round 1, thread `01a123f9`). One TD a URB on 1024-TRB stream rings placed by physical address; built x86 and `-amd64`, every gate and the host suites passing. Smoke (XP x64 in QEMU): three big blocks built, 1 MiB URBs as one TD of 256 TRBs (257 unaligned), VERIFY clean, 1 MiB reads 861 MB/s against 665 with `XhciWideMap` 0
  - **Design scope (record 13 section 11.7; orchestrator decision, owner
    to confirm):** the fixed rings placed by physical address are the
    **stream rings** of a streams endpoint (UAS at SuperSpeed), 1024 TRBs
    each, on NT starts where 36.24's promotion is enabled; Bulk-Only and
    High-Speed UAS keep 64-TRB pool rings and stage 2 - their large TDs
    are deferred, not refused. The TRB scratch is one per controller
    beside the wide pool, not per pipe, since every publication holds the
    controller lock. The low-memory qualification becomes an NT one (the
    big blocks, granted or refused into the fallback) plus a Windows 98 SE
    low-memory regression leg showing its allocations unchanged, since
    nothing of stage 3 runs on 98 or ME
- [ ] 36.26 (was MB-V) the legs, two claims kept apart. Compatibility: every guest, x86
  and amd64, UAS (streams, and High-Speed without them) and Bulk-Only,
  file compares, the value at 64, 1024, 2048 and out of range, the
  activation (a reboot takes a new value; a controller restart with the
  storage stack re-enumerated counts only if shown to refresh the
  descriptor), mixed binaries (this `xhciuas.sys` on the `2.2.0.x` HCD
  and the `2.2.0.x` `xhciuas.sys` on this HCD, streams and High-Speed
  without them, each showing the descriptor it advertises and file I/O
  passing), the query failing in transport alone (qemu injection) with
  the start otherwise completing, and removal during the query ending in
  the ordinary start failure and cleanup, Windows 2000 SMP under Driver
  Verifier, stop and restart,
  D3 and resume, unplug with work outstanding, audio and input under
  storage load. Geometry, through 36.22's test client rather than the
  file system's own SRBs, with assertions per stage:
  - stage 1: one large command as several sequential chunks; a short or
    error after earlier chunks completed, with the accumulated length and
    the termination right; 1 MiB and 2 MiB at unaligned and fragmented
    buffers, back-to-back, across the ring's wrap, under a constrained
    grant; a slow device completing successfully within the
    whole-command timeout, and a timeout, an abort and removal recovered
    from; full queue depth at 2 MiB under memory pressure;
  - stage 2: several chunks of one URB shown outstanding at once, and an
    early failure with successors queued;
  - stage 3: 257- and 513-data-TRB TDs observed on the ring where the
    geometry and the grant allow them. A 2 MiB SRB sent is not proof of
    a 513-TRB TD.

  DMA: an amd64 guest whose source pages are shown above 4 GB with
  mapped addresses below it (the bounce path), and x86 PAE likewise,
  each recorded untested if not shown; the amd64 leg does not cover PAE
- [ ] 36.27 (was MB-E) the bench (owner): 36.21's readings repeated on the same
  machines, devices and settings
**Selective suspend, standby resume and the 98 SE audio wedge (36.28 to
36.41; owner, 2026-10-09).** Moved here from planned work the same day,
because the switch is now off by default: `XhciSelectiveSuspend` defaults
to **0** (owner, 2026-10-09, replacing the default 1 of 2026-10-05; design
record 14 sections 7 and 11), so a user of `2.3.0.0` runs today's
behaviour unless the user sets 1. The bus then suspends a device when
Windows asks for it - a function's D-state in S0, or the idle notification
on XP onward - and wakes it on D0 or on the device's remote wake, never on
a timer of its own; USB 2.0 devices (stage A, 36.29 to 36.36) and
SuperSpeed devices (stage B, 36.37 to 36.40). Design record 14 is the
design; stage A's half converged over ten review rounds (`b2d03d5`). The
first standby and hibernate resume readings under the HCD (36.28) come
first, because the suspend design rests on them, and the wedge (36.41) is
re-measured against stage A's capabilities, so it waits for them. Their
docs join 36.20 and their install legs 36.final. The owner's other
decisions of 2026-10-05 stand: the test client is tracked under `test\`,
never shipped, refused by the packaging gate; stage A's checkpoint
includes the owner's bench; stage B ships in the same release, so the cut
waits for it. Stages A and B are built on branches of their own, both
merged into `2.3.0.0`. Their functional legs set `XhciSelectiveSuspend` to
1, and run once more under `XhciInterruptMode` 2, since suspend and
polling both touch the controller's power and teardown paths.

- [ ] 36.28 (was SUSP.1) system standby (S3) and hibernate (S4) resume, read for the first time under the HCD: the controller's D3 and D0 paths in `hcd_power.c` - the D0 resume, the work-item route and the direct transition - have never run on a guest (its header; neither primary's test guest sleeps, Windows 2000's runs `acpi=off`), and record 14 section 4.4 rests on them (pended D0s across a controller transition, a device's link still in U3 after a restore). Read on guests that sleep - QEMU with S3 enabled, ME (which hibernates on QEMU's power-down), and each target that can be made to - and on the E460 and the P14s Gen 1 (owner): standby and resume, hibernate and resume, with devices idle, devices suspended by stage A, and a D0 pended across it; a controller with and without Force Save Context (QEMU reads FSC 0); what breaks is fixed in this phase. The release notes' "a wake from standby" row moves from untested to read **Part 1 done 2026-10-10** (today's code, base `ebe3ec9`, then the fix builds; QEMU 11.1.0, development host A; readings in `out\p36-susp-3628\notes.md`, not tracked): XP and Vista offer hibernate only (no display driver for S3; XP's Cirrus driver vetoes standby), Windows 7 x86 hibernate only, ME standby and hibernate, 98 SE standby only. PASS: XP, Vista and Windows 7 hibernate idle and with Bulk-Only I/O (the I/O fails for about 10 s while the stick re-enumerates, then recovers), ME idle standby and hibernate - every resume through `XhciResumeController`'s reinitialization (QEMU has no FSC), every device re-enumerated. FAIL, open: ME standby or hibernate with a copy running on an xHCI stick hangs (no S0 or D0 reaches the controller after the wake; the hibernate never writes its file); `651d0d4`..`69d3b52` (every client request completed at the controller's D3, nothing published while suspended) did not change it, so the wait looks to be above the bus, and that attempt was reverted (owner, 2026-10-10; no proven benefit) - this ME standby/hibernate-with-I/O defect (ME-1) stays open with its evidence (`out\p36-susp-3628\me\`); and 98 SE's idle standby leaves USB dead after the wake on the base build already (untraced). **Part 2 owed** (after stage A and polling): the same with devices suspended by stage A, a D0 pended across it, and `XhciInterruptMode` 2; the bench (E460, P14s Gen 1) is the owner's. **Part 2, 98 SE standby resume, read 2026-10-11 (`p36-sby`, no code change; run-36.md "36.28 part 2 - 98 SE standby resume"): not this driver.** Traced on the `qemu` flavour: every 98 SE resume took `hcdD0Finish` inline (completion at PASSIVE_LEVEL) through the reinitialization, and with mice and hubs the resume completes and the devices work; the fatal exception 0E in KERNEL32 (`BFF9DFFF` and neighbours) follows a standby with no USB controller in the guest at all; with a USB stick attached Windows stops after the stick's D0 with this driver idle and owing nothing (unchanged with the bus's re-enumeration held), and the same stick stops the resume under NUSB's EHCI stack, while 98 SE's own UHCI driver does not resume on QEMU even with a mouse alone. Release-notes limitation drafted for 36.20. Still owed: part 2's stage A, pended-D0 and `XhciInterruptMode` 2 resumes on guests that resume (combined matrix), ME-1, and the bench.
- [x] 36.29 (was SUSPa.1) the link machine in the pure core, `xhci_susp.c`, with host suite `test_susp`: states and outcomes, the composite last-Dx rule, D0 during SUSPENDING, epoch-stamped Dx, wake classification, the episode's arming and completion by stamp, and the interleavings record 14 section 10 lists. Done 2026-10-09 (`2bb96c0`..`3915496`): `src/xhci_susp.c` and `.h` (in `src/sources`, called by nothing until 36.30), `test_susp` (445 checks, with exhaustive interleaving models of the cancel handshake and of the callback context's phases against teardown); record 14 revision 11 adds section 3.9, the split between the pure core and its executors, with the rules implementing it settled (the refusal latch by a per-slot change count `PwrSeq`, `WokeHold` per PDO after a claimed wake, START on a suspended device through `PwrWanted`, wake evidence only after the U3 request, `EXPIRED` apart from a transport timeout, teardown's step (5)). Codex-clean at round 4 (thread `01a1206f`); built x86 and amd64, every gate and the host suites
- [x] 36.30 (was SUSPa.2) the gate and the thread: one `SuspendPause` per pipe, the survivor restart, per-device slow-IRP deferral, root-port suspend and resume, the full-sample handoff at the port acknowledgement, USB 2.0 hub port suspend and both branches of the hub port look (record 14 section 3). Done 2026-10-09 (`a02e8b0`..`404feb1`): `src/hcd_susp.c` (new) with hooks in `hcd_enum.c`, `hcd_hub.c`, `hcd_cfg.c`, `hcd_io.c`, `hcd_pdo.c` and `hcd.h`; record 14 section 3.10 says how the thread carries sections 3 and 3.9 out (the thread's half of stage A: the decision pass, its completions and the wake claim are here; dispatch's half is 36.31). Inert until 36.31 fills the slots and 36.33 reads the switch. Codex-clean at round 5 (thread `01a120ba`); built x86 and amd64, every gate and the host suites. Its legs are 36.35's
- [x] 36.31 (was SUSPa.3) the PDO's power: slots, claiming, the composite rule, capabilities, WAIT_WAKE, GONE at the unlink, teardown in its order, START reopening, `PowerDown` admission and the controller's transitions (section 4 and 5). Done 2026-10-10 (`4b211ca`..`8d80e17`): dispatch's half in `src/hcd_susp.c`, called from `hcd_pdo.c`, `hcd_power.c`, `hcd_ctl.c` and `hcd_enum.c`; record 14 section 3.11 (Busy across the power dispatch, a Dx reported before its slot is filled, a per-PDO report flag ordering D0 and Dx reports, START's D0 reported once powered, the WAIT_WAKE cancel completing through a work item at PASSIVE_LEVEL, the epoch and `PowerDown` at the D3, recovery and stop). The idle rundown and 4.4 (b)'s split stay 36.32's; the 9x wake defect (5.5) and usbhub's latencies (5.2) are read in 36.35. Codex-clean at round 6 (thread `01a1214d`); built x86 and amd64 with the merge of `2.3.0.0` (`693f84f`), every gate and the host suites. Inert until 36.33 reads the switch
- [x] 36.32 (was SUSPa.4) the idle notification on the bus's callback thread: batches, the three-way claim, the context's phases, settlement of every idle IRP, quarantine (section 6 and 4.4). Done 2026-10-10 (`d2bf5b9`..`6093614`): `src/hcd_susp.c` (the idle slot and its triggers, the batch, the callback thread with its context and teardown's handoff, the rundown and quarantine), with `hcd_urb.c`, `hcd_pdo.c` (teardown (a) to (e) in order, the PDO's deferred slow IRPs flushed, a quarantined PDO never revived or started), `hcd_cfg.c`, `hcd_ctl.c` and `hcd_entry.c` (the unload waits out an abandoned callback thread); record 14 section 3.12 says how, including its deviations (no deferred-delete list, the phase moved under a spin lock, a 9x join polling an exit flag). The composite logic stays in two functions for the composite-parent devnode of 36.43. Codex-clean at round 3 (thread `01a1234a`); built x86 and amd64, every gate and the host suites. Inert until 36.33 reads the switch
- [x] 36.33 (was SUSPa.5) the switch, the counters in the counter block and `XHCISNAP`, the bounded log lines. Done 2026-10-10 (`77e5c62`, `eead296`): `XhciSelectiveSuspend` read in the start's value pass before `XhciInitController` (only DWORD 1 is on, stages A and B together; record 14 section 7); 28 counters at words 114-141 after the MSI words (`SuspSwitchRead` to `SuspNotesSuppressed`), the snapshot's counter block 142 words, the stop's dump and `XHCISNAP` naming them; bounded notes (64 per start: `susp.seq`, `susp.seq.epoch`, `idle.call`, `idle.abandon`, `idle.quarantine`); build-and-test.md says how to read them. Reviewed with 36.34 to convergence at round 7 (thread `01a1237f`); merged with `2.3.0.0` 533e721 (`4e3dea9`, its review clean, thread `01a123b9`); built x86 and amd64, every gate and the host suites
- [x] 36.34 (was SUSPa.6) the test client under `test\`, and the packaging gate refusing it. Done 2026-10-10 (`fd2cc2d`..`8860100`): `test\pmclient` - `XHCIPM.SYS`, a WDM function driver and power policy owner for QEMU's USB HID devices (`USB\VID_0627&PID_0001`) that on `PMCTL.EXE`'s demand requests D-states, WAIT_WAKE (its requester's callback counted apart from the IRP's completion: the 9x defect's read) and the idle notification, and keeps a read posted; `make-release.ps1` refuses its names or its marker `XHCI98_TEST_CLIENT_DO_NOT_SHIP` in every package, the upload set and the staged release before either rename (`Find-TestClientFiles`), with executed fixture tests in `test-package.ps1`. Codex-clean at round 7 (thread `01a1237f`)
- [ ] 36.35 (was SUSPa-V) stage A's legs (record 14 section 10): the test client on 98 SE, 2000 (SMP, Driver Verifier), ME and XP, with the 9x WAIT_WAKE completion read and wake disabled on 9x if the defect holds; hidusb's own idle on XP, Vista and 7; behind QEMU's hub; composite interleavings; unplug at each state; controller disable, enable, D3 and restart mid-transition; the fault-injection leg; `XhciSelectiveSuspend` at 0 reading as the build before it; the matrix unchanged; x86 and amd64 and every gate
- [ ] 36.36 (was SUSPa-E) stage A's bench (owner): a real remote-wake HID device on a 98 SE machine and on the E460's Windows 7
- [x] 36.37 (was SUSPb.1) stage B's design: record 14 section 8 grown into the record proper - Function Suspend with wake enable per interface, the function remote-wake capability, Function Wake Device Notifications (DNCTRL, the event, interface-to-function ownership), PLS=Resume on USB 3 root ports and the `xhci_link.c` classification, SuperSpeed hub ports (no C_PORT_LINK_STATE on a wake) - reviewed to convergence before code. Done 2026-10-10 (`2e9540a`..`1109793`, branch p36-suspb, merged into p36-suspbc `a899d6e`): record 14 sections 8.1 to 8.13 and a stage-B table in section 9; Codex-converged at round 3 (thread `01a12333`); the decisions of 8.13 are orchestrator decisions, owner confirmed 2026-10-10
- [x] 36.38 (was SUSPb.2) stage B built on stage A's machine, with host vectors. Done 2026-10-10 (`fe68330`..`a75c19b`, branch p36-suspbc): `xhci_susp.c` (STOP_FUNCS appended as step 13, the SuperSpeed order, `Armed` from the PDOs' `FwArmed`, the refusal's disarm, the strict disarm, every SuperSpeed wake the host's resume, the notification window and latch in the link record, per-PDO `ArmNow` and the per-function rearm, SuperSpeed hub observation, verification and evidence), `xhci_link.c` (WANT_RESUME on Resume with CCS and PED), `xhci_func.c` (first interfaces, ownership), `hcd_susp.c` (FUNCTION_SUSPEND per wake function, the latch's three consumers, the DPC hook, the configuration transaction with GetStatus(interface)), `hcd_sshub.c`, `hcd_cfg.c`, `hcd_pdo.c`, `hcd_dev.c`, `xhci_evt.c`, `xhci_init.c` and `hcd_ctl.c` (DNCTRL N1 only with `XhciSelectiveSuspend` 1), counters 142-159, the HCD header's words 204-272 (237-305 after the merge with 36.24's stage 2 words, integration wave 4) and XHCISNAP; record 14 section 8.10's as-built paragraph. Codex-converged at round 3 (thread `01a12405`); built x86 and amd64 all, every gate, the host suites
- [ ] 36.39 (was SUSPb-V) stage B's legs on QEMU (root ports only: QEMU has no SuperSpeed hub). Smoke boot 2026-10-10 (owner rule: the condensed matrix runs the full set on the integrated build), release build `300fc27`, development host A, a QEMU usb-storage on a qemu-xhci SuperSpeed port: Windows 7 x64 with `XhciSelectiveSuspend` 1 - DNCTRL read back `0002h`, GetStatus(interface) STALLed by QEMU's model (`SsSuspCapFailed`, no capable function, so no arm); usbstor sends no Dx on a disable, so x64 (no amd64 test client) shows no suspend - and at 0 DNCTRL not read, enables 0, no stage A or B counter, the stick read; Windows 7 x86 with the test client bound to the stick (a copy of `XHCIPM.INF` naming `USB\VID_46F4&PID_0001`, not tracked): WAIT_WAKE refused NOT_SUPPORTED, `PMCTL D2` suspended the link (susp.seq `00102110`, PORTSC PLS 3 in two readings 6 s apart), `PMCTL D0` resumed it (susp.seq `00120250`, PLS 0). Still open: the matrix's 36.39 checks
- [ ] 36.40 (was SUSPb-E) stage B's bench (owner): a SuperSpeed device with remote wake on a root port and behind a USB 3 hub
- [x] 36.41 (was SUSP.2) the Windows 98 SE audio-load wedge (carried since `2.0.0.0`, release notes' known limitations; 28.3): a USB audio device attached about 40 s after a cold boot wedged the guest 5 of 6 times (taskbar clock stopped), after 120 s none of 20; every IRP the bus was given had been answered, the wait is in the configuration manager after SELECT_INTERFACE to alternate 0 and QUERY_CAPABILITIES, and `1.2.0.0` showed it too. In this phase because stage A changes what QUERY_CAPABILITIES answers (D1/D2, wake): re-measure it on stage A's build first (better, worse or the same), then find where the configuration manager waits (a debug `VMM`/`CONFIGMG` or a trace of the devnode's state), and fix it if a bus answer is the cause; if it is Windows 98's own, it stays a documented limitation with the measurement refreshed. Done 2026-10-10: the baseline (`4b5c936`, run-36.md) found it not bus-side (UHCI with no `xhci98` wedges too); the qualifying re-measure on stage A's build (`4282007`, run-36.md "36.41's qualifying re-measure") reads the same at `XhciSelectiveSuspend` 0 and 1 (120 s: 6 of 9 and 5 of 9; 300 s: 3 of 3 and 2 of 2) - Windows 98's own, a documented limitation; the release notes' wedge text no longer advises waiting
- [x] 36.42 the Intel port switchover off by default (owner, 2026-10-09, after SweetLow's issue 17 comment 6082858626: an unrouted port stays on the EHCI and works there; the decisions table, "The Intel port switchover"): `XhciIntelPortSwitch` defaults to **0** in both INFs on every install path and in the driver when the value is missing, of another type or unreadable, reversing 34.3's default 1; 1 and 2 mean what they meant. Design record 16 revision 4. A user script, `portsw.vbs`, Windows Script Host VBScript (98 SE and ME have no `reg.exe`, 2000 none by default), at the root of every package directory: shows the value on every xhci98 controller (found by `DriverDesc` among the USB class key's `NNNN` subkeys, listed by WMI on NT and a `regedit /e` export on 98 and ME), sets 1 or 0, and says a restart is needed. Host vectors, the INF gate's row, footprints and self-tests, the packager and its self-tests, the release notes (an existing install keeps its value under FLG_ADDREG_NOCLOBBER; the script changes it), README, readme template, build-and-test.md. VM legs on the `release` package: the script on 98 SE, 2000 and 7 x64 (show, 1, 0, each across a restart) and a fresh install writing 0; QEMU's controller is not Intel, so the switchover itself is the owner's bench (a listed Intel chipset at 0 and at 1)
  - **Done 2026-10-10** (`dfb0508`, `aa4f84f`, `1d50090`; the Codex loop clean at its third round; built x86 and `-amd64`, all flavours, every gate and the host suites passing). VM legs on the `release` package (development host A, QEMU, `qemu-xhci`): Windows 98 SE (NUSB base, Add New Hardware wizard), 2000 SP4 (Found New Hardware wizard) and Windows 7 x64 (command-line install, F8 boot) each a fresh install showing `0 - off`; then `portsw.vbs 1`, a restart, `1` kept; `portsw.vbs 0`, a restart, `0` kept; exit code 0 throughout; on 7 x64 a non-elevated `1` raised the elevation prompt and the elevated copy set it. `XHCISNAP` at verbosity 2 after each restart: `psw.gate=00000000` and no other `psw.` record on all three, because QEMU's `1B36:000D` is not Intel and the value is not read there. Bench clause owed (2.3.0.0): the switchover at 0 and 1 on a listed Intel chipset
- [ ] 36.43 composite devices as devnodes (owner, 2026-10-10, after SweetLow's comment on issue 17; the decisions table, "Hub class and composite splitting"; design record 13 section 10.12): every composite device the bus splits gets a composite-parent PDO under the project-owned `XHCI98\COMPOSITE` (`&VID_&PID_[&REV_]`, no compatible id; its instance id the device's serial id or place; its text `iProduct`), bound by both INFs to `xhci98.sys` as a composite FDO (`hcd_hubfdo.c`'s code, no door), with the function PDOs as its children, so a hub port maps to one devnode; 33.4's parent rules (relations, ancestor, invalidation, let-go, dormant tiers) for a hub or a composite parent; a parent's disable and enable re-presenting its functions in place rather than cycling the port; the serial tests on group heads; `GET_NODE_CONNECTION_DRIVERKEY_NAME` answering the parent's key and 36.17's USB device interface moved to the parent; `txtsetup.oem` mapping the id; the INF gate's `HCD-COMP` and `HCD-COMPPAGE` and `HCD-HUBCOPY` over the composite install, with self-tests and footprints; two counters appended. Host vectors (`test_func`), the Codex loop, x86 and `-amd64` all with every gate. Stage A's code (branch `p36-susp`) needs, and this task does not make: its slot and batch walks skip a composite parent, its eligibility refuses one as a hub, its chain cap counts functions only (record 13 section 10.12, "Power"). Legs on the `release` package: QEMU's `usb-audio` (a parent with one function) on 98 SE, ME, 2000, XP, Vista and 7, x86 and x64 - one parent per port in Device Manager by connection with the function beneath it, `usbview` and Windows' USB pages showing the parent, the parent's and the function's disable and enable, a surprise removal, an update from `2.2.0.0` (re-detection once, the function working), non-composite devices unchanged; a real multi-function device (UAC with HID) by passthrough where the host offers one, else owed to the bench
  - **Progress 2026-10-10** (not ticked): design `364e69b..90b9655` (Codex clean at round 2), code `80eb758`, `5edd643` (Codex clean at round 1); built x86 and `-amd64`, all flavours, every gate and the host suites. Legs on the `release` package (development host A, QEMU): Windows 2000 legs 1-8 and 10 pass (both parents installed silently, the tree, `USBIF list` on the parent only, usbview and the Power tab naming the parent, parent disable/enable three times with the function re-presented in place, function disable/enable, surprise removal, hub and controller disable/enable); Windows 98 SE install, tree and interfaces pass, then the guest UI froze after usbview was closed, the signature of 36.17's earlier 98 SE hangs, not shown to be this task's. Owed: XP, Vista, 7 x86 and x64 and ME, the update from `2.2.0.0` everywhere, 98 SE's remaining legs, the release notes' TODOs; stage A's changes (the parent excluded from its votes) wait for both merges
- [x] 36.44 the Windows 98 SE hot-plug hangs seen in 36.17-36.19's legs (2026-10-10): Windows stopped responding with the pointer still moving (1) after a stick with a never-seen serial was unplugged while its Add New Hardware wizard was open and the same serial plugged back 40 s later, the replugged stick never addressed; (2) after eight devices were hot-added about a minute after the desktop came up; (3) after the root hub's Power properties page was closed. Each is reproduced on the `qemu` flavour, against `2.2.0.0`, and with Windows 98 SE's own UHCI stack as a control (36.41's method); one found to be the bus's is fixed with host vectors and a review loop, the others recorded with their evidence (`runs/run-36.md`).
  - **Done 2026-10-10** (`e811dbf`; docs only - no hang is the bus's, so no code, review loop or build; `runs/run-36.md`, "98 SE hot-plug hangs"). Reproduced on `2.3.0.0` at `bfec7b0` (`qemu`), on `2.2.0.0` (`release`) and with Windows 98 SE's own UHCI stack, development host A, QEMU 11.1.0 TCG, on a copy of the guest that hung. (1) **Not the bus's:** the configuration manager sends no relations query while the wizard holds the gone devnode, so the port waits in Gone by design (no slot, nothing in flight) and enumerates the replug once the query comes; the wedge comes from keystrokes typed into the wizard and is identical on the UHCI control and on `2.2.0.0`. (2) **Not the bus's:** 36.41's audio-load wedge (its UHCI control's VMM EIPs; the bus serves throughout; never without the audio device; also on `2.2.0.0`). (3) **Not a hang:** Windows' Power popup ignores Esc on every stack (UHCI control, `2.2.0.0`); Alt+C closes it and the UI is live. (4) Added by the orchestrator from 36.43's legs (UI frozen after usbview closed): **not the bus's** - it needs the audio device and usbview together and comes on `2.2.0.0` and the UHCI control alike (`USBAUDIO(01)+00002ED4` or a frozen UI), none in 32 control cycles with audio and no usbview. QEMU answered its monitor through every freeze, so none is the QEMU process stop.
- [ ] 36.45 string and other uncached descriptors through the door (issue 17 item 8; owner, 2026-10-10): `IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION` answers a request the cache cannot (any string and language id, the language list, another configuration, the BOS, any other type) with a real `GET_DESCRIPTOR` on the device's default endpoint, with usbhub's length semantics, pended to the controller thread's EP0 path; design record 13 section 8.11, reviewed before code, host vectors for the request's checks; legs with SweetLow's usbview (x86 and x64) on 98 SE, ME, 2000, XP, Vista and 7 x86/x64 showing the Manufacturer, Product, Serial and interface strings of a stick, a mouse or tablet and a hub, a stick copy with `fc` and the mouse moving while usbview hammers the strings, and an unplug during a pending request
- [ ] 36.46 an interface with more endpoint descriptors than its `bNumEndpoints` (GitHub issue 17 item 7, SweetLow): a low-speed HID mouse, VID `073A` PID `2230`, whose interface declares one endpoint and carries two (`0x81` IN and `0x01` OUT, interrupt, 8 bytes, `bInterval` 10) is configured with one pipe under Microsoft's stack and never configured under `xhci98` (Current Config Value 0, no open pipe). Find every point where it fails, with its exact bytes as host vectors through every pure path (the configuration walk, the composite split and filtered descriptor, the select's pipe set and Configure Endpoint plan, the Low-Speed interrupt endpoint context at `bInterval` 10, the 239-byte report descriptor over an 8-byte EP0, hidusbf's rewritten `bInterval`), and take such a device as usbport does: honour `bNumEndpoints`, ignore the surplus, never less strict than xHCI requires. Regression legs on 98 SE and 7 x64 with QEMU's mouse, tablet, keyboard and a stick; the mouse itself on the bench (owner or SweetLow)
- [x] 36.47 `XHCISNAP`'s header block names the system it was captured on (owner, 2026-10-10): the OS version, build and service pack, x86, x64 or 32-bit on x64, the CPU, the memory, and the manufacturer, model, board and BIOS where the OS exposes them (Vista SP1 and 7 in `HARDWARE\DESCRIPTION\System\BIOS`; 2000 and XP the BIOS version and date only; 98 and ME what `Enum\Root\*PNP0C01` holds). Never the computer name, the user name, a domain, a serial number, a UUID or an asset tag, because the `.TXT` is pasted publicly. Legs: the header read on every guest.
  - **Done 2026-10-10** (`c68391d`, review fixes `3796b59`, `40dade1`, `5e85c5f`, `3bdf8c4`; `-nosysinfo` (owner addition, 2026-10-10) `3c074d1`, `297c945`; the Codex loop clean at its seventh round, thread `01a12386`; `XHCISNAP` built, `selftest.cmd` 15 cases passing, imports KERNEL32 and ADVAPI32 only, source charset clean; `src\` untouched). The header's "captured on" block: the OS by `GetVersionEx` with its name, x86 or WOW64, the allow-listed SMBIOS strings (Vista SP1 and 7), `SystemBiosVersion`/`SystemBiosDate` (2000, XP, XP x64), `BIOSVersion`/`BIOSDate` (98 SE and ME: the export shows no `BIOSName` on SeaBIOS), the CPU and processor count, the RAM; any registry string holding the computer, user, domain or workgroup name is withheld (code-page fold); `-nosysinfo` beside a dump skips all of it. Legs (development host A, QEMU 11.1, x86 `release` package of integration wave 2): 98 SE, ME, 2000, XP and Vista x86 read and correct, `-nosysinfo` on ME, XP and Vista x86; XP x64, Vista x64, 7 x86 and 7 x64 owed to the combined matrix (default and `-nosysinfo`).
  - **Follow-up 2026-10-10 (wave 3 matrix, M-3; `126a7e9`):** XP x64 has neither `SystemBiosVersion` nor `SystemBiosDate` and keeps the SMBIOS strings in `HARDWARE\DESCRIPTION\System\BIOS`, so the matrix read "BIOS not in the registry" there. 2000, XP and XP x64 now read that key, through the same allow list, when both values are absent; the existing sources keep their order (`-selftest-sysinfo` checks the choice; `selftest.cmd` 19 cases). Read on the XP x64 guest (development host A, `-selftest-sysinfo`): SystemManufacturer QEMU, SystemProductName, SystemVersion, BIOSVendor SeaBIOS, BIOSVersion, BIOSReleaseDate, no name. A dump's header on XP x64 is owed to the final matrix.
- [x] 36.48 `XHCIQUAL`'s report header names the machine too (owner, 2026-10-10), read under DOS from the SMBIOS tables (types 0, 1 and 2: BIOS vendor, version and date, the system's manufacturer, product, version and family, the board's manufacturer and product), `CPUID` (vendor and brand), the memory and the DOS version, with the BIOS date and model byte where there is no SMBIOS. The same rule: no serial number, UUID, asset tag or SKU. Host vectors for valid, 3.0, truncated and garbled tables; a QEMU case with `-smbios` strings set, serials included, shows them left out.
  - **Done 2026-10-10** (`dfa5222`, review fixes `5256531`, `e7d31d3`, `8bf7f4a`, `7738ec8`, `90fc4fe`; the owner's `--no-sysinfo` `044921a`; README sample `8e2ebf6`; the Codex loop clean at its sixth round; `XHCIQUAL` built, its host suites passing with the new `test_sysinfo` (243 checks, windows on a guard page), `check-build-exit-codes.ps1` 6 cases (a crashing suite now fails the build), the QEMU matrix 50 cases on development host A). `xhciqual/sysinfo.c` (pure) and `sysprobe.c`, `sysasm.asm` (CPUID, every register saved): SMBIOS `_SM3_`/`_SM_`/`_DMI_` with checksums, the table direct below 1 MB, DPMI `0800h` above, assembled across 1 MB, at most 64 KB and 4096 structures walked; Types 0, 1, 2 and 4; CPUID vendor, brand, family/model/stepping; RAM from E820 (DPMI `0300h`), else Type 17, else Type 19; DOS `30h`/`3306h` and INT 2Fh `1600h`. No identifiers: serials, UUID, asset tags, SKU and part numbers are never copied, and any printed string containing one (any length, any case; the UUID as hex in both byte orders) prints `(withheld)`, as does every SMBIOS string when the walk cannot finish. No SMBIOS: the ROM date and model byte. The quick scan: two lines on the screen, the block in the log. `--no-sysinfo` skips all probing and says so. Owed to the bench: a real machine's header under DOS (the E7250 or any fleet machine at 36.final) and a pre-SMBIOS one if one turns up.
- [x] 36.49 Low- and Full-Speed devices behind a hub on a USB 2.0 root port whose speed table names neither (GitHub issue 18, SweetLow; `docs/issues/13-via-vl805-usb2-integrated-hub-speed-ids.md`): on the VIA VL805 (`1106:3483`) the USB 2.0 port lists only its integrated hub's High Speed, as xHCI 1.2c 7.2.1 allows, and `HcdHubPlace` refused every Full- and Low-Speed device behind that hub. A general rule, on by default (orchestrator decision on Codex's advice, owner confirmed 2026-10-11): such a device takes the default ID (1 Full, 2 Low) where the table has no entry of that speed and does not list the ID; an explicit entry always wins; root-port decoding and task 35.1 unchanged; the provenance on the device record for every later decode; `XhciUsb2HubSpeedFallback` `0` turns it off; counters at words 162 to 166, bounded notes, `XHCISNAP`, the VL805's firmware dword noted at start; `implementation-invariants.md`, "Port Speed Decoding", amended. Host vectors on the VL805's table and on conflicting ones, the Codex loop, x86 and `-amd64` all with every gate; a QEMU regression leg (QEMU publishes no USB 2.0 table). The hardware result is not observed before release: the fix has never run on a VL805, and it does not block the cut.
  - **Done 2026-10-10** (`e7d4243` the code and host vectors, `d213f62` the docs, `2fddc57` round 1's fix; Codex thread `01a12643`: round 1 no P1/P2, its P3 fixed; round 2, the confirmation of that fix, returned nothing). Built x86 and `-amd64` all, every gate, the host suites, `XHCISNAP` and its self-test. QEMU on development host A, Windows 98 SE, the `qemu` flavour: QEMU's Full-Speed hub hot-added on a root port with a mouse and a keyboard behind it, both enumerated at Full Speed (route `00001`, tier 1), the mouse moving the pointer, `XHCISNAP` showing the switch on and no fallback taken (QEMU publishes no speed table); QEMU models no Low-Speed device. Not observed on hardware before release.
- [x] 36.50 the x64 binaries test-signed - **a prerequisite of the `2.3.0.0` cut** (owner, 2026-10-11; design record 19, reversing design record 11 decision 9 for the x64 package only). Done when all of these are: the packager option and the `make-release.ps1` refusal; the certificate made by the owner (`scripts\local\testsign\xhci98-test.pfx`, thumbprint `A512DF4530C63AFEA9C0F18D64E333BF75E5ABFF`); the `2.2.0.3` re-cut signed; the Vista x64 and Windows 7 x64 test-signing legs, on and off, and XP x64; the procedure in `README.md`, the release readme and the release notes. Every published x64 directory carries `xhci98.sys` and `xhciuas.sys` with an embedded Authenticode signature by the project's own test certificate - SHA-1 file digest, no timestamp, no catalog, both INFs byte-identical - applied by `make-package.ps1 -TestSignPfx` at staging, with `xhci98-test.cer` beside them, and `make-release.ps1` refuses an x64 cut without it; the x86 binaries are never signed. So Vista x64 and Windows 7 x64 load the driver in test-signing mode (`bcdedit /set testsigning on`, once) instead of only on an F8 start. The key is made by the owner (`scripts\package\make-testcert.ps1`) and held only in the git-ignored `scripts\local\testsign\` or outside the tree. Into the `2.2.0.3` TEST build at once (re-cut, same version and date, the x86 halves byte-identical to the unsigned cut's) and every cut after it. Legs: Vista x64 and Windows 7 x64 in test-signing mode, installed from the signed zip and started with no F8, twice, the controller and devices working and "Test Mode" shown; the same without test-signing mode (the Code recorded); XP x64 installing and loading the signed binary as before. - **done 2026-10-11:** the rehearsal legs on Vista x64, 7 x64 and XP x64 (run-36 "36.50"; Code 39 on Vista, Code 52 on 7 with test-signing off), then the owner's signed `2.3.0.0` cut (`out\p36-stamp3`, zip SHA-256 `879768458F92CE14EC225DFB45E1EDDB982D1CE2DB7BD9555560A1DD3112265E`): all four x64 images signed by `A512DF4530C63AFEA9C0F18D64E333BF75E5ABFF` (SHA-1, no timestamp) and the build plus the signature only, the x86 images byte-identical to the stamped build, the key containers SignTool left deleted by the owner
- [x] 36.51 every registry value the driver reads written by the INFs at the driver's own default (owner, 2026-10-11; the decisions table, "Every registry value written by the INFs"): the inventory from the code (`build-and-test.md`, "The registry values the driver reads"), `XhciWideMap` 0, `XhciSelectiveSuspend` 0 and `XhciUsb2HubSpeedFallback` 1 added to `[Xhci.AddReg]` and `[Xhci.AddReg.NT]` of `src\xhci98.inf` and `[Xhci.AddReg.NT]` of `src\xhci98-amd64.inf` with the same `0x00010003` flags as the sixteen, the INF gate's `VAL-*` table, self-tests and footprints, the README, release notes, release readme template and records; fresh install and update over `2.2.0.0` on 98 SE, ME, 2000, XP, XP x64, Vista x86 and 7 x64 - done (p36-regall, merged at `8e74422`): review converged in two rounds; both builds, the INF self-tests (1118), the packager (471) and the host suites pass; legs on development host A, every guest PASS: a fresh install wrote all nineteen at their defaults and `XHCISNAP` read the three new ones as set; an update over `2.2.0.0` (whose INFs wrote eleven) kept the user's values and added the eight missing at their defaults. `XhciIntelPortSwitch` stays at `2.2.0.0`'s `1` on update, as 36.42 documents; the root hub's Driver tab keeps the old version, and 98 SE and ME keep the old driver until a restart they do not ask for, both already documented and now worded for any update
- [x] 36.53 `XhciInterruptMode` 1 the interrupt line only and 3 a message-signalled interrupt only (owner, 2026-10-11; design record 15 revision 7 and decision 8, design record 18 revision 12 and decision 8, amending the 2026-10-09 mode rules of 36.5, 36.15 and record 18 sections 4.2 to 4.3a). Mode 1 on every system: on Vista and 7 a message Windows grants anyway fails the start (Code 10) on x86 and amd64, helper bound or not - it no longer runs as 0, and reasons 11 and 13 are no longer produced. Mode 3, which read as 0: on Vista and 7 the requirements filter keeps only the message configurations (outcome 12, `XhciMsiKeepMessage`), the message runs where granted and connected (reason 15), and the start fails (Code 10) with no message granted (16), the message not connected - 32-bit without `xhcimsi.sys` bound, a refused connection, or Windows connecting the line instead (17) - or on Windows 98 SE, ME, 2000 and XP, 32- and 64-bit (18). 0 and 2 unchanged; 4 and above still run as 0. A start the delivery refuses records itself before it returns: the counter block, the `qemu` flavour's `M86 S` record, and the start's notes with `start.refused.delivery`, flushed to DebugView through `HcdLogFlush` when `XhciLogDebugView` is set. Host vectors in `test_poll` for every row by OS family, mode, grant, routines and pin, and `XhciPollConnected`; `test_msi` for the message keep; `XHCISNAP` names reasons 14 to 18 and outcome 12. Legs: 7 x64 and Vista x86 with the helper under modes 0 to 3 with MSI offered; 7 x86 without the helper under mode 3; XP and 98 SE under mode 3 and modes 0 to 2.
  - **Done 2026-10-11** (`5b6ddfc` design, `4856baa` code, `943f551` docs, `a2d5837` round 1's documentation fixes; review round 1 no P1/P2, round 2, the confirmation of that fix, returned nothing). Built x86 and `-amd64` all, every gate, the host suites, `XHCISNAP` and its self-test, on `4e11974` (2.3.0.0 merged). QEMU on development host A, every row passed (`docs/contributing/runs/run-36.md`, "36.53"): 7 x64 and Vista x86 with the helper ran a message under 0 and 3 (outcome 12 under 3) and the line under 1; mode 1 with only a message offered, mode 3 with a failed connection, with `MSISupported` 0, and on 7 x86 with the helper disabled each failed with Code 10 and reasons 14, 17, 16 and 17; XP and 98 SE failed mode 3 with Code 10 and reason 18 and kept modes 0, 1 and 2. Not read: a controller with no line, mode 3 on MSI rather than MSI-X, 64-bit Vista, ME, 2000, XP x64.
- [x] 36.54 Transaction Error recovery on a stream endpoint (owner, 2026-10-11: fixed before the `2.3.0.0` cut; was the planned item STE.1). A UAS drive whose data stream takes a Transaction Error moves data again promptly, without a replug. Design record 17 left stream endpoints out of the soft retry (EP0, isochronous and stream endpoints keep their handling), so the failed command was left to `xhciuas`'s class timeout, abort, LUN reset and port reset. Phase 36's matrix (XP x64, `qemu` flavour, `XhciQemuInject` fault 16 on a UAS data-in stream TD) read the failed read completing cleanly, then every later command timing out until the device was removed and added again (2 of 3; the third recovered after 266.6 s); the 36.22 stage-1 build behaves the same, so this predates stages 2 and 3, and a Bulk-Only stick under the same fault is recovered by the soft retry with its copies `fc` clean. Done when: the stuck runs' failure to converge is diagnosed from traces; record 17 gains a reviewed section choosing between a soft retry for stream TDs (Reset Endpoint and Set TR Dequeue for the halted stream, the stream's doorbell rung with its stream ID, bounded as the Bulk-Only retry is), a prompt `xhciuas` recovery in place of the class timeout, or a fix of whatever keeps the existing recovery from converging; host vectors for the stream ring's dequeue and DCS after the reset; legs: the fault 16 injection on XP x64, 2000 and 7 x64, a UAS copy `fc` clean after it with no replug, the Bulk-Only control unchanged, faults 15 and 17 on UAS still recovering, and a plain UAS copy and VERIFY on 98 SE and 7 x64 (`release`) unchanged.
  - **Done 2026-10-11** (p36-ste: design `9b41fee`, review fixes `10460c8`, `86f8393`, `c1c5029`, `f9714be`; code `9f1f11f`, review fixes `c6408d0`; docs `d753a28`, `deb6269`, `250065a`; design review converged at its fifth round, code review at its second; built x86 and `-amd64` all, every gate, the host suites and `XHCISNAP`'s selftest passing). Root cause, traced on development host A (QEMU trace events and guest memory): QEMU 11.1's xHCI ignores a Stream Context's DCS bit (`hcd-xhci.c`, `xhci_find_stream` calling `xhci_ring_init`, which forces the consumer cycle to 1) after every configure, stop or reset of a streams endpoint, so each stream whose ring sat on a cycle-0 lap went dead, and every `xhciuas` recovery step re-forced it; the driver's contexts and Set TR Dequeue DCS were correct. Fault 17's and B9(b)'s "no timeout" was the class driver's length-scaled timeout (160 s for 1 MiB, observed); the 98 SE `XHCISNAP` hang was Windows 98's file system queued behind the stuck read, not the door. Record 17 section 4.12: the soft retry on a stream; stream rings restarted at their base at RESET_PORT; a QEMU-only (`1B36:000D`) stream resync; a `StreamFault` leak after client resets fixed. Counter words 167-172 appended. Legs on the `qemu` flavour, each fault run on a fresh UAS device after its rings had wrapped: fault 16 on the data-in stream on XP x64 (3), Windows 2000 SP4 at two processors under Driver Verifier (3) and 7 x64 (3), and on 98 SE during a copy with `fc` (3), and on the data-out stream on XP x64 (3): every run recovered in place with no stall, no `xhciuas` timeout or reset and no replug (`StreamRetryResets` +1 each); faults 17 and 14 recover after one class-timeout stall (XP x64, 2000, 7 x64); fault 15 clean at 1 MiB and, B9(b), at 2 MiB with `XhciWideMap` 1 and 0; the Bulk-Only fault-16 control unchanged (XP x64, 2000); `release` regressions on 98 SE and 7 x64: UAS copies `fc` clean and `VERIFY` 0 bad. Evidence `out\p36-ste\legs`. A real drive's Transaction Error, and a real controller's partial progress across the soft retry, are bench items.
- [x] 36.55 the lost-interrupt backstop is taken in the tolerance clock's DPC, not on the controller thread (design record 17 section 4.1, "Amended"; orchestrator decision, owner confirmed 2026-10-11). The final matrix found it: on Windows 98 SE, under `XhciInterruptMode` 1 with the `qemu` flavour's dead line, a Bulk-Only copy from an MS-DOS prompt stopped, 2 of 2. Traced, the controller thread stopped being scheduled while the copy's request waited for a completion only the backstop would drain; the tolerance clock's timer DPC kept running and an event stood at the dequeue. Done when the step runs in that DPC (one peek and at most one queued drain per tick, no power gate, the drain's own admission under the controller lock), the release closes the event DPC with an interlocked exchange, records 17, 15 and 13 say so, the Codex loop converges, x86 and `-amd64` build all with every gate and the host suites, and these legs pass on the `qemu` and `release` packages: 98 SE dead line mode 1 twice (an MS-DOS prompt copy, and the same after a HID mouse is installed), 98 SE mode 0 with injected lost interrupts during a copy, ME dead line mode 1, XP and 2000 dead line mode 1, 2000 SMP with Driver Verifier dead line, and the mode 0 and 2 regressions on 98 SE and Windows 7 x64
  - **Done 2026-10-11** (p36-dl98: design `7debbe0`, code `2ad2b0d`, review round 1's wording fix `5d96e69`, docs `b7c2b55`, merged with `2.3.0.0` at `e9d25b9` in `2c74b47`; review round 1 no P1/P2 and one wording P3, round 2 the confirmation and round 3 the merge returned nothing). Built x86 and `-amd64` all, every gate and the host suites, `XHCISNAP` and its self-test, on the merge. Every leg named above passed on `5d96e69`'s packages, and the 98 SE copy again on the merge's `qemu` build (runs/run-36.md, "36.55"). Found on the way: the stop needs a drive in MS-DOS compatibility mode (the BIOS-mapped E: of a stick attached at power-on), and is reachable in mode 0 by one lost interrupt; every release since `2.2.0.0` has the exposure. 98 SE Stand by with such a stick still stops after the wake (36.28 part 2), so that limitation is unchanged
- [x] 36.56 `portsw-on.bat` and `portsw-off.bat` beside `portsw.vbs` in every package directory (owner, 2026-10-11, at the E7250 bench, where a stick ran at USB 2.0 speed with the switchover at its default 0): each changes to its own directory, shows the value, sets 1 or 0 through `portsw.vbs` and says a restart is needed; run as administrator. Folded into the next cut after the bench (a recut of `2.3.0.0` or the following release), never into the benched zip. Design record 16 section 5a, the packager's staging and copy lists, its self-tests and the package-content gates, the README's switchover section and package table, the release notes and the readme template; legs: each batch on 98 SE, 2000, XP and 7 x64 (elevated and not), the value read back with `portsw.vbs show`. **Done 2026-10-11** (p36-pswbat: design `e67f778`, files and packager `c5e1e29`, review fixes `ae65c26`, `9bb2c5b`, build-and-test `837bc17`, the legs' two text points `bb80117`; review round 1 two P2 and two P3, round 2 one P2 and three P3, rounds 3 and 4 nothing). One file for `COMMAND.COM` and `cmd.exe`: `if "%OS%"=="Windows_NT" goto nt`, `goto run`, and only `cmd.exe` reaches `cd /d "%~dp0"`. Self-tests hold every line to a whitelist (`Get-PswBatchFaults`) with broken controls. Legs (development host A, the stamped `2.3.0.0` release package plus the two files): 98 SE (double-click, prompt, from `C:\`), ME, 2000, XP, 7 x64 (Run as administrator, not elevated, the prompt declined) all PASS, the revised text read again on 98 SE, 2000 and 7 x64; `COMMAND.COM` sees `cscript`'s exit code. A rebuild does not reproduce the cut's bytes (link timestamp, checksum, PDB path), so the re-cut publishes the first cut's binaries, restored and verified against its hashes (run-36, "36.56"). Re-cut owed to the owner.
- [ ] 36.57 an explicit isochronous `StartFrame` is never refused for want of Frame IDs: without CFC a request that starts a data flow carries a Frame ID on its first TD, and anything the controller cannot be told goes out with Start Isoch ASAP (owner's bench, 2026-10-11, on the signed `2.3.0.0` cut: a Sound Blaster Play! 2, `041E:323D`, asynchronous playback with feedback endpoint `0x83`, played only hiss on the E7250, Windows 7 x64, at 44.1 and 48 kHz; a C-Media device played; design record 13 section 6.10). The snapshot read `IsoSubmits` 0, 4,420 commands and 1,882 Stopped - Length Invalid events, and the controller (`8086:9CB1`) has no CFC. Windows 7's `usbaudio.sys` gives every data URB and every feedback read of an asynchronous stream an explicit `StartFrame` (static, `legal-provenance.md` section 4), and `hcdIsoPublish` refused each one as `BAD_START_FRAME` when its packets could not all have Frame IDs; the commands are `usbaudio.sys`'s `RESET_PIPE` before each feedback read. Plain SIA is not enough: the feedback read is paced by its completion at the frame it names (xHCI 4.11.2.5 p.200 allows a first-TD Frame ID without CFC). Done when: such a request is published with a first-TD Frame ID when it starts a data flow without CFC (a new context, or an Underrun or Overrun handled with nothing queued; an Interval of one frame or less), Frame IDs on every TD with CFC, and SIA otherwise (a flow under way without CFC, no frame sample, a frame outside the window, a cadence the Interval does not share), a start beyond 895 frames still refused; `iso.fid1` and `iso.sia` noted for the first four each; host vectors for the plan against the builder's TRBs, 300 paced feedback reads inside the window, the first and later data URBs, the notes, and the Play 2's descriptors (two 9-byte isochronous endpoints in one alternate setting, Interval 3, Max ESIT Payload 3 and 200, 3-byte packets, 192/196-byte packet offsets); the review loop converges; x86 and `-amd64` build all with every gate and the host suites; regression legs: QEMU's `usb-audio` on 98 SE and 7 x64 at the same timing, and a stick copy. Bench clause: the Play 2 plays a tone without hiss on the E7250 at 44.1 and 48 kHz, `iso.fid1` for the feedback endpoint with a lead near 34, Stopped events at about 31 a second.
- [ ] 36.final the `2.3.0.0` builds, x86 and `-amd64`, after every task above:
  - the stamp, and the x64 binaries test-signed by the project certificate (design record 19; `make-release.ps1` refuses otherwise); **stamped 2026-10-11** (`46c5e23`: `2.3.0.0`, `DriverVer` `10/11/2026,2.3.0.0`; owner, 2026-10-11: the bench build is cut directly as `2.3.0.0`, under `out\` until the bench is good, and the TEST `2.2.0.3` label is dropped), the signed cut to follow;
  - the host suites and every gate;
  - a Codex review loop per task;
  - the QEMU regression legs (still owed from `2.2.0.2`);
  - the ten install legs read from the asset, with the update legs reading 36.3's, 36.6's and 36.22's values preserved across an update (34.1's rule), moved here from SUSP.4, and `XhciSelectiveSuspend` absent (0, today's behaviour) after an install;
  - then the owner's check on the E7250 under XP x64 and Windows 7 x64, with no manual `D20IR` write, which verifies:
    - the mouse;
    - the stick hot-plugged and present at boot;
    - the EHCI disabled in Device Manager, read as delivered by the line interrupt (not moved to polling), with the backstop near idle;
    - S3 resume;
    - Windows 98 SE (PIC mode), and Windows 7 with `MSISupported` 0 (the line forced, 36.15), with the routing fix repairing the route in both;
    - Windows 7 with MSI granted, and any target under `XhciInterruptMode` 2, with the routing fix reporting only and `D20IR` left as the BIOS set it.

Checkpoint: `2.3.0.0` on the E7250 under XP x64 and Windows 7 x64, with
`XhciIntelIrqRoute` at its default:
- the xHCI's interrupts delivered by the ISR (the backstop near idle);
- a device present at boot enumerated;
- the stick mounted;
- the controller working with the EHCI disabled, its interrupts delivered
  by the line rather than polled;
- the route still correct after S3;
- 36.13 passing, with its functional and regression legs on the `release`
  package and its fault-injection legs on the `qemu` flavour, kept apart.
  The `qemu` flavour is never published;
- 36.14 read by the owner;
- `XhciInterruptMode` at 0 and at 1, on a controller whose line interrupt
  works, reading as the release before it on the down-level systems;
- 36.16 passing: MSI on Vista and 7, and the x86 binary still loading on
  98 and ME;
- 36.17 to 36.19's legs passing, and `XhciFastPollFsLs` 0 to 3 reading as
  the release before it;
- 36.26 passing at every setting it lists (64, 1024, 2048 and out of
  range), with the default finally chosen identified and the DMA legs
  not shown named as untested; a passing default does not stand for a
  failing 2048. 36.27 read against 36.21 with the same settings and
  repetitions: 1 MiB stays
  the default (owner, 2026-10-09: measurement decides) only if it is
  no slower than 64 KiB anywhere beyond the
  readings' own spread; otherwise the default becomes the size that is,
  and the INF's default changes with it (an existing installation keeps
  a value it already has, under the don't-overwrite rule, so the release
  notes say how to take the new one);
- 36.35 and 36.39 passing (stage A's and B's legs): their functional
  and regression legs on the `release` package with `XhciSelectiveSuspend`
  1, 36.35's fault-injection leg on the `qemu` flavour, kept apart; 36.35's
  functional legs on 98 SE and 2000 (SMP, Driver Verifier) and 36.39's
  root-port legs run once more under `XhciInterruptMode` 2; 36.36 and
  36.40 read by the owner; 36.28's resumes read on guests and the bench;
  36.41 re-measured and, if bus-side, fixed; `XhciSelectiveSuspend` at 0,
  the default, reading as the release before it;
- 36.20's docs done, and the ten install legs read from the asset.

The phase is cut as `2.3.0.0` (owner, 2026-10-08).

Records: `docs/issues/12-e7250-xp64-boot-enable-slot-refused.md`;
`design/15-polled-event-delivery.md`; `design/14-selective-suspend.md`;
GitHub issue 17
(https://github.com/yeokm1/xhci98/issues/17).

---

# Planned work

Not phases yet: each takes the next free phase number when the owner opens
it, and its placeholder ids take that number then (owner, 2026-10-06; the
decisions table), and a version only then too: `2.2.0.0` is Phase 35's
(owner, 2026-10-06). Polled event delivery moved into Phase 36 on
2026-10-08 and selective suspend on 2026-10-09 (owner), so the cut that
ships it is `2.3.0.0`'s.
Issue 17's follow-up is not a release of its own: it joins whichever phase
the owner opens it in. The 1 MB transfers, planned here on 2026-10-09, moved
into Phase 36 the same day (owner).

## Planned - Issue 17 follow-up

Goal: the one item of SweetLow's `2.x` testing (GitHub issue 17) that
Phase 36 did not take (owner, 2026-10-09), waiting on something before it
can be designed.

Status: not opened, unnumbered and unversioned. It is not a release of
its own; it joins the phase the owner opens it in.

- [ ] I17.1 the Windows 98 SE silent install (issue 17 item 4): the
  `SilentInstall` capability, today FALSE on every PDO
  (`hcd_pdo.c`, `hcd_rh.c`, `uas_pdo.c`), reported TRUE where 98 SE's
  `usbhub.sys` and SweetLow's `usbhub20.sys` report it, so a device
  already installed does not bring back the New Hardware wizard on
  another port. **Waits on** SweetLow's description of the rule: when it
  is set, and what it reads. Code offered for it is taken only under
  `legal-provenance.md`'s rules: GPL-2.0-compatible, and not derived
  from Microsoft's binaries.

## Planned - S5 shutdown latch

Goal: no transfer, command or port request can wait forever after the
controller's D3 at shutdown (S5). Hardening, not tied to an observed hang:
Phase 36's matrix traced Windows ME's Shut Down hang with an unopened USB
disk to ME itself (the same hang on ME's own UHCI + USBSTOR stack and on
`2.2.0.0`; the xhci98 trace shows nothing sent to the driver after the
D3). Reading the code found the gap (`2.2.0.0` has it too): after the D3 a
transfer URB is published to the halted ring and waits for a resume
(`hcd_io.c`, HcdIoMapped); command URBs and RESET_PORT wait on
HcdCfgService, skipped while the controller is unpowered (`hcd_enum.c`);
nothing times out, and at S5 no resume comes.

Status: not opened, unnumbered and unversioned (orchestrator, 2026-10-11;
owner confirmed 2026-10-11).

- [ ] S5L.1 the latch: at a controller Dx whose system state is
  PowerSystemShutdown, including the direct-transition path, set it before
  the halt; after it, refuse new transfer and command URBs with
  `USBD_STATUS_DEVICE_NOT_RESPONDING` through HcdIoRefuseLater (never
  DEVICE_GONE, which HcdIoDeferred parks), complete RESET_PORT inline (its
  IRP carries no URB, and the refusal DPC reads Argument1 as one), and
  after the halt flush what is already queued with the same status. S3 and
  S4 keep hold-until-resume. A full Codex loop under record 13 section 5's
  lock model, IRQL tags, stage A PowerDown and 36.35's no-thread-creation
  rule. Legs: shutdown with devices on ME, 98 SE (the 36.35 case), 2000,
  XP and 7 x64.

## Planned - an event ring check after the re-arm

Goal: an event stranded while IMAN.IE was clear is served without waiting
for the 100 ms backstop, on an emulator or a controller that loses the
interrupt the re-arm should raise. Phase 36's matrix found QEMU's MSI-X
does: on Vista and 7 under `msix=on`, `BackstopDrains` counted 3 to 288
a session, 0 under MSI, the line and polling. The cause, from the source
and consistent with the trace: `xhci_intr_update` raises before it marks
the vector used again, and `msix_notify` returns on an unused vector before
its mask and PBA check, so the message is neither sent nor pended. Codex
(2026-10-11) read no lost-interrupt window on compliant hardware, so this
is a QEMU defect, and `2.3.0.0` ships unchanged (orchestrator, owner to
confirm). The owner's bench reads `BackstopDrains` under MSI-X on real
hardware, where 0 is the expected clean result, not an invariant.

Status: not opened, unnumbered and unversioned.

- [ ] RRC.1 after the DPC's re-arm, read the next event's cycle bit and,
  if an event is pending, queue one more bounded drain through the existing
  admission and accounting (`hcd_ctl.c`), never a loop under the
  controller lock. IE stays 0 in the ISR, so record 05's DIRQL exception
  is untouched. Codex's preferred option over re-arming before the final
  ERDP write or a blind second ERDP write. Legs: 7 x64 and Vista x64 under
  MSI-X (`BackstopDrains` 0, TONE and UAS timings against MSI), and the
  line and MSI rows unchanged.
## Planned - releasing the transfers kept by a pinned containment

Goal: disabling, updating or removing a controller the driver closed off
without proof that DMA stopped (record 17's containment, `XHCISNAP`
"CONTAINED" and "PINNED") completes instead of waiting for ever on the
transfers kept. Since `2.2.0.0` the driver keeps every transfer the
controller might still write into, by design, and the release notes list
the hang under "Known limitations". Phase 36's matrix read it again on
Windows 2000 (4 of 4 from fresh boots, with selective suspend on and off:
hidusb's interrupt-IN read kept, its REMOVE waiting) and on Windows 98 SE
(owner, 2026-10-11: planned after `2.3.0.0`, documented as a limitation).

Status: not opened, unnumbered and unversioned.

- [ ] PCR.1 at the controller's stop or remove, look for proof that it can
  no longer write to memory, and only then complete the kept transfers as
  failed: (a) the device has left the bus (its PCI configuration space,
  the vendor id included, reads all ones), or (b) the device entered D3hot
  through its PCI power management capability and reads back D3hot, where
  it may not master (PCI Bus Power Management 1.2). With neither proof the
  transfers stay kept, as today: a buffer the controller might still write
  into is never handed back. Record 17 gains the section first, a full
  review loop on the DMA-safety path, host vectors for each proof, and
  legs: the all-ones injection then a disable on 98 SE, ME, 2000, XP and
  7 x64, with and without a client read pending, and the unproven case
  still holding.
