# Source Files

What every tracked file under `src\`, `src\uas\`, `src\hlpr\`, `src\cm\` and `test\` is for, one row per file, with the user-mode tools' trees after them. The authoritative description of each file is the comment block at its head; this page is the map. Line counts are left out on purpose: they drift, the roles do not.

> **History.** Until `1.2.0.0` `xhci98.sys` was a `usbport.sys` miniport; its sources, and the version of this page that mapped them (`xhci_dispatch.c`, `xhci_rh.c`, `xhci_slot.c`, `xhci_vhub.c`, `xhci_probe.c`, the host suites `test_init`, `test_vhub` and the rest), are on branch `1.2.0.0`. On 2026-10-02 the miniport left the tree and `src\` became the successor HCD (design record 13, section 5.1 for the kept, rewritten and deleted files). The miniport files the HCD kept still speak of usbport in their comments: `src\hcd_svc.h`, "READING THE KEPT FILES", is the key from each usbport name to the HCD code that now plays the part, and a comment that names a deleted file describes the miniport on branch `1.2.0.0`. Which task added which function to which file is the last section of this page.

The rule that organises the directory: files in the pure core **decide** (they encode TRBs, compute layouts, classify ports, run state machines over caller-supplied memory) and touch no register, no DDK service and no IRQL; the DDK side is where those decisions become bus cycles, IRPs and kernel calls. Each pure file builds twice, into its driver and on the build host with MSVC 6.0 under `XHCI_HOST_TEST` (`test\run-host-tests.cmd`, which `scripts\build-driver.cmd` runs before any DDK build); a file that needs `ntddk.h` does not belong in it. `src\sources` is the build list of `xhci98.sys` (every `.c` below that it names, and `xhci98.rc`) and records why each group is there.

## `xhci98.sys` - the pure core

| File | Role | Host suites |
|---|---|---|
| `xhci_mem.c` | The controller common buffer's layout, computed and checked: the size `hcd_dma.c` allocates, the offsets carved from it, and the specification's alignment and no-boundary-crossing rules, all checked against each other before any of it reaches a controller ([design record 04](design/04-controller-common-buffer.md)); `XhciPoolAcquireReserved` for the ring pool (task 38.10). `XhciCommonBufferAllocationBytes` has no driver caller; the host suite keeps it. | `test_membuf`, `test_ctx`, `test_desc`, `test_link` |
| `xhci_ring.c` | TRB encoding and the ring state machines: command, event and transfer rings, the cycle bit, Link TRBs, wrap, and `XhciRingIsOutstanding`. Rings no doorbell. | `test_ring`, `test_xfer`, `test_iso`, `test_td` |
| `xhci_caps.c` | The extended-capability walk over a caller-supplied reader, root-port classification by protocol, speed decoding from the PSI tables (`xhciProtocolRate`, task 35.1), the 0.96 version floor (task 38.11) and the qemu flavour's PSI override. | `test_caps`, `test_port`, `test_link`, `test_sshub`, `test_inj` |
| `xhci_port.c` | `PORTSC` writes that have only the intended effect (the `XhciPortsc*` builders the enumeration thread and the suspend executor use: neutral, clear changes, power, reset, warm reset, disable, link state, suspend and resume). `XhciRootHubBuild`, `XhciRootHubPortOf`, `XhciRootHubHubPortOf` and the `XhciPortShadow*` family served the miniport's root hub and have no driver caller; only the host suites exercise them. | `test_port`, `test_link` |
| `xhci_ctx.c` | Slot, Endpoint and Input Control Context encoding. A value that does not fit its field is refused, not masked. | `test_ctx`, `test_desc`, `test_link` |
| `xhci_xfer.c` | The transfer engine, kept from `1.2.0.0` (task 26-A.4): the control, bulk, interrupt and isochronous TD builders, the pending-transfer queue, what a Transfer Event means to one transfer, and the completion-code table; since then the soft retry (35-T.2), the slot-fatal decision, the TD Size form below xHCI 1.0 (38.11) and the ordered-list path (36.25). Its input structures are the usbport-shaped ones of `xhci_usbport.h`, which `hcd_io.c` builds. | `test_xfer`, `test_iso`, `test_td` |
| `xhci_desc.c` | The configuration-descriptor walk that recovers an isochronous endpoint's `bInterval` (task 9-A.2), and the `bMaxPower` and Power tab helpers `hcd_enum.c` and `hcd_door.c` call. Decides nothing itself. | `test_desc` |
| `xhci_topo.c` | The hub topology graph: each device's route string, parent hub and TT ([design record 02](design/02-hub-topology-route-string.md)), written for the miniport's snooping of `usbhub.sys` traffic; the HCD's hub class (`hcd_enum.c`, `hcd_hub.c`, `hcd_sshub.c`) records each hub and device in it directly. The miniport's claim and migration functions (`XhciTopoClaimChild`, `XhciTopoDropPending`, `XhciTopoSuppressClaim`, `XhciTopoMigrate`, `XhciTopoForgetAlternate`) have no driver caller; only the host suite exercises them. | `test_topo` |
| `xhci_log.c` | The in-memory log ring: a bounded byte ring, its producer set and per-code budget, and the flush decision; no file I/O. Its sink is `hcd_log.c` (and `src\cm\cm_log.c` compiles it into `xhcicm.sys`). In the release flavour too ([design record 08](design/08-build-flavours-and-the-log-channel.md)). | `test_log` |
| `xhci_enum.c` | The per-port enumeration state machine (design record 13 section 5.3), a root port's enumeration notes, and the busy engine's wait and step (tasks 36w.1, 36.4). | `test_enum`, `test_link` |
| `xhci_link.c` | A USB3 root port's link state machine and task 29-A.5's hold and release rules; the stuck-link quirk's check. | `test_link` |
| `xhci_pipe.c` | The pure half of URB dispatch: the configuration walk, endpoint parameters, Configure Endpoint plans, setup packets, buffer splits into chunks, the USBD status table, RESET_PIPE's steps and verdict (36.12) and the select's precedence and checks (36.46, 38.12). | `test_pipe`, `test_hub`, `test_tol`, `test_td` |
| `xhci_func.c` | Composite splitting: whether a device is split and its functions, the function and composite-parent ids, and the instance-id rules of task 33.2 (`XhciFuncSerialId`, `XhciFuncInstanceId`). | `test_func` |
| `xhci_hub.c` | The hub class inside the bus: the hub descriptor, the status-change bitmap, each port decision, the reset progress, the hub PDO's ids and node information, and the checks on the door's uncached descriptor request (36.45). | `test_hub`, `test_sshub` |
| `xhci_sshub.c` | The SuperSpeed hub class's decisions: the 0x2A descriptor, the SuperSpeed port decision, hot or warm hub-port reset, the extended port status and a downstream SuperSpeedPlus rate's speed ID. | `test_sshub` |
| `xhci_stream.c` | Bulk streams: the plan, the block layout (and the big layout of 36.25), Stream Contexts, the command encoders and an event's stream. | `test_stream` |
| `xhci_xport.c` | The mass-storage transport policy: Bulk-Only or UAS for an interface that offers both, and the ids that follow; applied in `hcd_pdo.c`. | `test_xport` |
| `xhci_psw.c` | The Intel EHCI-to-xHCI port switchover: the device-id gate, the board exemption, the `XhciIntelPortSwitch` rule, the start decision and the route and release sequences over caller-supplied configuration-space accessors ([design record 16](design/16-intel-port-switchover.md)); also compiled into `XHCIINST`. | `test_psw` |
| `xhci_pirq.c` | The Intel PCH interrupt routing quirk: the `XhciIntelIrqRoute` rule, the listed xHCI and LPC pairs, the PIRQ that delivers the assigned IRQ in APIC and PIC mode, and `XhciPirqRun` over caller-supplied accessors (issue 12 section 5.1); also built into `XHCIQUAL`'s routing report. | `test_pirq` |
| `xhci_tol.c` | Controller tolerance: each tolerant behaviour's decision, the `XhciTolerance`, `XhciIntervalCap` and `XhciAvgTrbEsit` rules, the tolerance clock's arithmetic, the budgets and the terminal reasons ([design record 17](design/17-controller-tolerance.md)). | `test_tol`, `test_xfer`, `test_iso`, `test_td` |
| `xhci_snap.c` | The layout of the snapshot's HCD region, word by word: each root port's enumeration state, the counter block, the delivery words and the copied records `hcd_door.c` fills. | `test_snap` |
| `xhci_xstat.c` | The transfer-size readings (`XHCI_XSTAT`: length, chunk and depth buckets, the hand-carried 64-bit sums, the interval rule over the time-stamp counter) and the `XhciMaxTransferKB` rule `XhciXpolValue`. | `test_snap` |
| `xhci_xpipe.c` | Stage 2 of the transfer size (design record 13 section 11.6): the TD planner over a URB's wide list, eligibility, the promotion gate, the register-free check's comparison, the pipelined record's machine and its counters. | `test_xpipe`, `test_xfer`, `test_td` |
| `xhci_poll.c` | Polled event delivery ([design record 15](design/15-polled-event-delivery.md)): the three values, the delivery and its reason, the poller's ownership machine, promotion, park and kick, the watcher's dead-line verdict, the adaptive period, and the finer timer's request machine and phase observer (38.3). | `test_poll` |
| `xhci_msi.c` | Message-signalled interrupts on Vista and 7: the MSI and MSI-X capability read, the requirements list's checked walk, class, strip and qemu no-line rewrite, the filter record and the trace record. | `test_msi` |
| `xhci_hlpr.c` | Both ends of the channel to the x86 helper `xhcihlpr.sys` (design record 18 section 15.3): the block, the binding machine with its timer event and the per-start latch. Compiled into `xhci98.sys` and the helper. | `test_msi` |
| `xhci_quirk.c` | Linux's xHCI controller quirks ([design record 20](design/20-linux-xhci-quirks.md)): the fourteen values, the id lists and the match, the effective policy, the packed word, `XhciMsiAvoid`'s decision. | `test_quirk` |
| `xhci_own.c` | Who holds an endpoint's ring across an interface switch (record 20 section 5): each step's decision, a completion code as an outcome, the one-setting walk. | `test_own` |
| `xhci_susp.c` | Selective suspend ([design record 14](design/14-selective-suspend.md)): the switch, eligibility and capabilities, the device's link machine, each function's power slot, the wake episode, the idle notification's batches, and stage B's SuperSpeed decisions. | `test_susp` |
| `xhci_silent.c` | The Windows 98 SE and ME silent install (design record 13 section 6.11): the `XhciSilentInstall` gate, the `SilentClasses` table's admission and cap, the configuration walk and the PDO's answer. | `test_silent` |
| `xhci_strict.c` | Strict mode's command precondition table: would a conforming controller accept this command in this Slot and EP state. Debug and qemu flavours only. | `test_strict` |
| `xhci_inj.c` | Task 35-T.9's fault injection: the `XhciQemuInject` trigger's encoding and sequence rule, the ports a fault may be aimed at, every register answer the layer gives. Compiled into the qemu flavour alone. | `test_inj` |
| `xhci_qry.c` | The answer of `IOCTL_XHCI98_QUERY` (design record 13 section 8.12): the counter block from level 1, the log ring as text at levels 2 and 3 (withheld at 4), the controller's configuration space at every level, and the sizing answers; `hcd_door.c` gathers and answers. | `test_xhq` |

## `xhci98.sys` - the DDK side

These need `ntddk.h`, take a lock, wait, or dereference BAR0. None may move into the pure list. The first five are the miniport's, kept and adapted by task 26-A.2 (their usbport service calls replaced by `hcd_svc.h`'s); every `hcd_*.c` file is the HCD's own.

| File | Role |
|---|---|
| `xhci_pci.c` | The only file that touches BAR0 or PCI configuration space: the MMIO accessors, configuration reads and bounded waits `xhci_hw.h` declares, and the qemu flavour's `PORTSC` injection hooks. |
| `xhci_init.c` | The controller sequence: BIOS handoff, reset, DCBAA, scratchpad, command and event rings, run, port power, the quirks that act at suspend, halt and shutdown, and the in-place recovery ([design record 07](design/07-controller-recovery-in-place.md)). |
| `xhci_evt.c` | The interrupt body (proves ownership, carries nothing forward) and the event-ring drain, called from `hcd_ctl.c`'s ISR and DPC and from the poller, which hands each event to the command, port, slot or transfer layer. |
| `xhci_cmd.c` | The asynchronous command engine every slot and endpoint command goes through: completion matching, software-owned timeouts, stuck-ring recovery ([design record 05](design/05-locking-model.md) for its lock). |
| `xhci_dbg.c` | The qemu flavour's per-line trace channel. Compiles to nothing without `XHCI_DBG_LIVE`, which `src\sources` defines for `chk_qemu` only, so the port-0xE9 mirror and its `HAL.dll!WRITE_PORT_UCHAR` import exist in that flavour alone. |
| `hcd_entry.c` | `DriverEntry`, `AddDevice` (which role a new device object takes) and the dispatch table; the flavour marker and the version string the gates read out of the image. |
| `hcd_pnp.c` | PnP for the controller FDO, written for Windows 98's removal without a prior stop or surprise removal ([win98-wdm.md](../usb-xhci-info/win98-wdm.md), "PnP Dispatch Requirements"). |
| `hcd_power.c` | Power for the controller FDO as the device's power policy owner: system to device IRPs, the D3 and D0 sequences, and at each return to D0 the switchover and interrupt-routing re-runs. |
| `hcd_ctl.c` | The controller FDO's start and stop: resources, the BAR, the interrupt and its DPC, the controller thread, the values read at each start, the lost-interrupt backstop (`HcdCtlBackstopTick`), the delivery's settling and the message ISR, the switchover and PIRQ starts, the command drain. |
| `hcd_svc.c` | The services the kept files call in place of usbport's (`hcd_svc.h`), the controller lock, and the tolerance clock. |
| `hcd_dma.c` | The only `IoGetDmaAdapter` and `DMA_OPERATIONS` calls: the adapter, the common buffer, the map pump and the wide mapper of task 36.24. |
| `hcd_pool.c` | The only pool and MDL calls (design record 13 section 7.5): `ExAllocatePoolWithTag` and `ExFreePool`. |
| `hcd_dev.c` | The device layer the kept files call into (the core's call-outs that `xhci_hw.h` declares): port change marks, command completions for the thread's waiter, transfer events to their pipes, the soft retry's replay, the device cycle's producer and the tolerance counters. |
| `hcd_rh.c` | The root hub: the PDO the controller FDO creates (`USB\XHCI98_ROOT_HUB`) and the FDO this driver attaches over it in its second role. |
| `hcd_enum.c` | The controller thread's root-port service and enumeration executor: each port's `PORTSC`, the machine's actions, `hcdCommand` (every command the thread issues, with the busy wait and strict mode's look), the serial read, the location budgets and the device cycle. |
| `hcd_pdo.c` | The device PDOs: one per enumerated device or function, their ids, capabilities and lifecycle, the composite parent and its functions, the duplicate and dormant matching, the transport and silent-install answers, and the D-IRPs of selective suspend. |
| `hcd_hubfdo.c` | The FDO of an external hub's PDO (the third PnP role, design record 13 section 10.11) and of a composite device's parent PDO (the fourth, section 10.12). |
| `hcd_hub.c` | The hub class inside the bus: an external hub brought up, its status-change pipe, its ports' service and its teardown. |
| `hcd_sshub.c` | The SuperSpeed half of a USB 3 hub, entered from `hcd_hub.c` where the hub is SuperSpeed. |
| `hcd_urb.c` | The device PDOs' internal device controls: each URB function routed, the private `IOCTL_XHCI98_*` requests of `xhci98_streams.h` and `xhci98_xfer.h`, the composite parent's select, the idle notification. |
| `hcd_io.c` | The URB transfer path: a request onto a pipe, through the map pump, onto the ring and back; cancellation, the wide path, the time-stamp readings. |
| `hcd_cfg.c` | The URBs that need commands, served on the controller thread: SELECT_CONFIGURATION, SELECT_INTERFACE, ABORT_PIPE, RESET_PIPE and the port reset; streams' build and fallback, the retry service, the interval cap, the held rings of an interface switch. |
| `hcd_door.c` | The user-mode door (design record 13 section 8): the controller's Advanced tab and `USBUSER` requests, `XHCISNAP`'s pass-through and the HCD region, `IOCTL_XHCI98_QUERY`, and the root hub's and each hub's hub IOCTLs. |
| `hcd_log.c` | The log ring's one sink: the DebugView emit, from the controller thread, under the lock-then-emit order of design record 08. |
| `hcd_susp.c` | Selective suspend's executor on the thread: the admission gate, the suspend and resume sequences at a root port and a hub port, U3 and Function Suspend at SuperSpeed, the wake evidence. |
| `hcd_poll.c` | Polled delivery's kernel half: the poll timer and its DPC, `PollLock`, the watcher's look, the retirement under `KeSynchronizeExecution`, PCI Interrupt Disable on containment, the doorbell hook. |
| `hcd_tmr.c` | The finer system timer while polling (task 38.3): the per-controller timer thread, the routine's lookup per image, the `ExSetTimerResolution` hold and release. |
| `hcd_msi86.c` | Message-signalled interrupts on Vista and 7, the kernel half that is not the connection: the mode cache, the requirements filter, the helper's look and reference, the routines per architecture, the counters and trace. |
| `hcd_quirk.c` | The quirks' controller-FDO half: the values read at each start, the identity `XhciMsiAvoid` needs, and the quirks that act from the FDO's own paths (record 20 sections 4 and 6); the value names from `xhci_quirk.h`'s `XHCI_QK_VALUE_NAMES`. |
| `hcd_strict.c` | Strict mode's driver half: before each command, the slot and endpoint states read from the Output Device Context and checked against `xhci_strict.c`'s table; it counts and traces, and sends the command anyway. Debug and qemu flavours only. |
| `hcd_inj.c` | Fault injection's driver half: the trigger read, the executors, the ISR's lost-interrupt window and the hooks. Qemu flavour only. |

## Headers

| File | Role | DDK-free |
|---|---|---|
| `hcd.h` | The WDM objects of the HCD: the device extensions of every role, the controller record, the pipe and transfer records, the cross-file entry points. | no |
| `hcd_svc.h` | The services the kept files call, with the usbport service each replaces, and "READING THE KEPT FILES", the key to their comments. | no |
| `xhci.h` | xHCI types, register and TRB bit definitions, the common-buffer layout, the extension `hcd.h` embeds, the snapshot's layout, and the declarations of most of the pure core. Bit positions are transcribed from [xhci-data-structures.md](../usb-xhci-info/xhci-data-structures.md), never from memory. | yes |
| `xhci_hw.h` | The driver-only side of the split: MMIO accessors, PCI configuration access and bounded waits (`xhci_pci.c`), and the core's call-outs that `hcd_dev.c` implements. | no |
| `xhci_compat.h` | Two jobs: the Windows 98 and 2000 DDK shims, chiefly undoing the DDK's `ExAllocatePool` rewrite so a stray untagged call reaches the name the import gate refuses; and, under `XHCI_HOST_TEST`, the DDK-free types the host suites compile the pure core with. Included by every driver of the tree. | yes, under `XHCI_HOST_TEST` |
| `xhci_counters.h` | The counter block `XHCIHC_COUNTERS` (roadmap-hcd.md 26-A.10), measured by `scripts\vm-matrix\gen-offsets.ps1` under `XHCI_HOST_TEST`, which is why it is a header of its own; `xhcisnap\check-counter-names.ps1` and `xhcitr\tr_hcctr.h` follow it name by name. | yes |
| `xhci_log.h` | The log ring's contract: the verbosity ladder, the sinks, and the rule that recording is not emission. | yes |
| `xhci_dbg.h` | The trace channel's macros; empty outside the qemu flavour. | no |
| `xhci_version.h` | The package version and release date, the single editable source of the version. Included by `xhci98.rc`, `uas\xhciuas.rc`, `cm\xhcicm.rc`, `hlpr\xhcihlpr.rc`, `hcd_entry.c`, `hcd_msi86.c`, `uas\uas_entry.c`, `cm\cm_entry.c`, `hlpr\hlpr_entry.c`, `xhciqual\qual.h`, `xhcisnap\xhcisnap.c`, `xhcicln\xhcicln.c`, `xhciinst\xhciinst.c`, `xhcitr\xhcitr.c` and `xhcitr\xhcitr.rc`; every INF's `DriverVer` literal is checked against it by the INF gates (`build-and-test.md`, "Versioning the driver"). | yes |
| `xhci_usbport.h` | The usbport miniport ABI as the miniport declared it, transcribed from [usbport-miniport-abi.md](../usb-xhci-info/usbport-miniport-abi.md). Kept because the transfer engine's input structures and the embedded extension use its types; the HCD calls no usbport service. | yes (`test_packet` compiles it) |
| `xhci_xfer.h` | The transfer engine's interface (`xhci_xfer.c`), kept out of `xhci.h` because it needs `xhci_usbport.h`. | yes |
| `xhci_desc.h` | The descriptor walk's interface, and the argument for why it must exist. | yes |
| `xhci_topo.h` | The topology graph's types and interface, with the measured wire constants. | yes |
| `xhci_probe.h` | Types only: those of the miniport's transfer-contract probe (task 6-V.1), kept because the extension `xhci.h` declares still embeds them; the probe and its functions left with the miniport. | yes |
| `xhci_vhub.h` | Types and values only: the miniport's virtual-hub records (task 24.3, design record 12) that `XHCI_EXTENSION` embeds, kept for the same reason; the HCD has no virtual hub, `xhci_vhub.c` left with the miniport, and `xhcisnap` still decodes the records. | yes |
| `xhci_enum.h` | The enumeration machine's states, events, actions and port record (`xhci_enum.c`). | yes |
| `xhci_link.h` | The USB3 link machine and the hold rules (`xhci_link.c`). | yes |
| `xhci_pipe.h` | URB dispatch's pure half (`xhci_pipe.c`). | yes |
| `xhci_func.h` | Composite splitting's pure half (`xhci_func.c`). | yes |
| `xhci_hub.h` | The hub class's pure half (`xhci_hub.c`), and the four id bases `XHCI_ID_ROOT_HUB`, `XHCI_ID_HUB`, `XHCI_ID_HUB30` and `XHCI_ID_COMPOSITE` the INF gate holds the INFs to (`HCD-IDSRC`). | yes |
| `xhci_sshub.h` | The SuperSpeed hub class's pure half (`xhci_sshub.c`). | yes |
| `xhci_stream.h` | Bulk streams' pure half (`xhci_stream.c`). | yes |
| `xhci_xport.h` | The transport policy (`xhci_xport.c`). | yes |
| `xhci_psw.h` | The port switchover (`xhci_psw.c`). | yes |
| `xhci_pirq.h` | The interrupt routing quirk (`xhci_pirq.c`). | yes |
| `xhci_tol.h` | Controller tolerance (`xhci_tol.c`), its constants and the state kept in the extension. | yes |
| `xhci_xstat.h` | The transfer-size readings (`xhci_xstat.c`). | yes |
| `xhci_xpipe.h` | Stage 2's planner (`xhci_xpipe.c`). | yes |
| `xhci_poll.h` | Polled delivery (`xhci_poll.c`). | yes |
| `xhci_msi.h` | Message-signalled interrupts' pure half (`xhci_msi.c`). | yes |
| `xhci_hlpr.h` | The helper channel (`xhci_hlpr.c`): IOCTL `0x0022240B`, magic `'XHLP'`, the block. | yes |
| `xhci_quirk.h` | The quirks (`xhci_quirk.c`), `XHCI_QUIRK_STATE`, and `XHCI_QK_VALUE_NAMES`, the X-macro of the fourteen registry value names that `xhci_quirk.c` and `hcd_quirk.c` both expand. | yes |
| `xhci_own.h` | Ring ownership (`xhci_own.c`). | yes |
| `xhci_susp.h` | Selective suspend's pure half (`xhci_susp.c`). | yes |
| `xhci_silent.h` | The silent install (`xhci_silent.c`). | yes |
| `xhci_strict.h` | Strict mode's table (`xhci_strict.c`). | yes |
| `xhci_inj.h` | Fault injection's encoding and reserved codes (`xhci_inj.c`, `hcd_inj.c`). | yes |
| `xhci_qry.h` | `IOCTL_XHCI98_QUERY`'s wire format, shared byte for byte by the driver, `XHCITR` and the host tests. | yes |
| `xhci_qryb.h` | The query answer's composer (`xhci_qry.c`). | yes |
| `xhci98_streams.h` | The private bulk-streams interface (`IOCTL_XHCI98_OPEN_STREAMS`, `CLOSE_STREAMS`) a class driver of this project includes; needs only the DDK's base types. | no (base types only) |
| `xhci98_xfer.h` | The private transfer-size interface (`IOCTL_XHCI98_TELEMETRY`, `IOCTL_XHCI98_TRANSFER_POLICY`) `xhciuas.sys` includes beside it. | no (base types only) |

## Build and packaging inputs

| File | Role |
|---|---|
| `sources` | The DDK build description of `xhci98.sys`: target, the `SOURCES` list, defines per flavour, linker flags, and the comments that say which group each file belongs to. Read it before adding a file. |
| `makefile` | The DDK build stub; it only includes `makefile.def`. Every setting lives in `sources`. |
| `xhci98.rc` | The file version resource (task 8-A.4), from `xhci_version.h`; adds no import. |
| `xhci98.inf` | The **32-bit** INF, one file for three setup engines: Windows 98 SE and ME read the undecorated sections and load the driver through `NTKERN`; Windows 2000 and 32-bit XP read the `.NTx86` sections and load it as a kernel service; 32-bit Vista and 7 read `[XhciModels.NTx86.6.0]`. Each binds `xhci98.sys` in its four roles (`PCI\CC_0C0330`, `USB\XHCI98_ROOT_HUB`, `USB\XHCI98_HUB`, `USB\XHCI98_HUB30`, `USB\XHCI98_COMPOSITE`); the NT paths also install `xhcihlpr.sys`, and the 98 and NT 5.x paths fetch `usbd.sys` and `usbui.dll` through `LayoutFile` (`usbport.sys` and `usbhub.sys` are not fetched: the HCD replaces both). Gated by `scripts\inf-gate\check-inf.ps1`. |
| `xhci98-amd64.inf` | The **64-bit package's** INF, a second file rather than a third path in the first (design record 11 section 12, decision 2): no Windows 98 path, a `[Manufacturer]` line decorated `NTamd64,NTamd64.6.0`, `[Xhci.Dev.NTamd64]` for XP x64 and Server 2003 x64 and `[Xhci.Dev6.NTamd64]` for Vista x64 and 7 x64. Neither file may grow the other's sections, and the gate refuses it by name (`PATH-NO9X`, `PATH-MFGDEC`, `OS-DEFAULT`); `scripts\inf-gate\test-inf-checks.ps1` checks the two against each other. |
| `portsw.vbs` | The Intel port switchover's user script (roadmap-hcd task 36.42, design record 16 section 5a): shows `XhciIntelPortSwitch` on every xhci98 controller and sets it to 0 or 1, then says a restart is needed. Finds the controller's driver key among the `NNNN` subkeys of the USB class key (listed by WMI on NT, by a `regedit /e` export on 98 and ME, probed `0000` to `9999` only if neither answers) for this driver's `DriverDesc`; on Vista and 7 asks for elevation itself. Published once by `make-release.ps1`, in `tools\portsw\` (roadmap-hcd task 38.9; `portsw\` at the version root from task 37.5 until `2.4.0.0`; no package carries it), held by the packager's self-tests and the source charset gate. |
| `portsw-on.bat`, `portsw-off.bat` | The script's two batch files (roadmap-hcd task 36.56, design record 16 section 5a, "The batch files"): each shows the value, sets 1 or 0 through `cscript //nologo portsw.vbs`, says a restart is needed and pauses. One file for `COMMAND.COM` (98 SE, ME) and `cmd.exe` (2000 to 7): `if "%OS%"=="Windows_NT" goto nt`, then `goto run`, so only `cmd.exe` reaches `cd /d "%~dp0"`. Published beside `portsw.vbs` in `tools\portsw\` by `make-release.ps1` (task 38.9; `portsw\` from task 37.5), held by the packager's self-tests (the split, the calls, ASCII and CRLF), the source charset gate and the batch-file line-ending gate. |
| `silent.reg` | The silent install's list (roadmap-hcd task 38.2, design record 13 section 6.11.8): `REGEDIT4`, the six `SilentClasses` entries - hubs alone; boot keyboard and mouse alone or composite; Bulk-Only SCSI storage alone or composite - under SweetLow's value names, written by this project from those facts. Published as `tools\silent\SILENT.REG`. Silences nothing while `XhciSilentInstall` is not 1. |
| `silent.vbs` | The silent install's user script (task 38.2, section 6.11.8), `portsw.vbs`'s pattern on Windows 98 SE and ME only: shows `XhciSilentInstall` on every xhci98 controller and the `SilentClasses` list (a `regedit /e` export read for its value lines), and sets the switch to 0, or to 1 only after the list reads back as `SILENT.REG`'s six; each write read back, each controller set, not set or unverified. Exit codes 0 done, 1 refused or nothing found (or NT), 2 usage, 3 partial, 4 list not verified, 5 not verified. Published as `tools\silent\SILENT.VBS`. |
| `silent-on.bat`, `silent-off.bat` | Its two batch files (task 38.2, section 6.11.8), `portsw-on.bat`'s shape: `silent-on.bat` shows, stops on any errorlevel, merges `silent.reg` with `regedit /s` and runs `silent.vbs 1`, reading its errorlevel 5, 4, 3, 1 in that order; `silent-off.bat` runs `silent.vbs 0` and says the list was left in place and how to delete it. On NT both change nothing. Published in `tools\silent\`. |
| `txtsetup.oem`, `txtsetup-amd64.oem` | The text-mode Setup driver descriptions (roadmap task 33.3, design record 13 section 5.6): what Windows 2000 and 32-bit XP (the first) and XP x64 (the second, staged under the first's name) read from drive A: when F6 is pressed, loading `xhci98.sys` as a `scsi` component and binding it to `PCI\CC_0C0330`, `USB\XHCI98_ROOT_HUB`, `USB\XHCI98_HUB`, `USB\XHCI98_HUB30` and `USB\XHCI98_COMPOSITE` (under `XHCI98\` before task 38.8). The x86 file also carries `xhcihlpr.sys` on a `dll` line, copied to `%windir%\OemDir` for GUI-mode Setup's install and not loaded in text mode (design record 18 section 15.9). Gated by `scripts\inf-gate\check-txtsetup-oem.ps1`, which holds both to the INF beside them (`OEM-COPY`: every file the INF's NT 5.x install copies from the disk is on the option) and to each other. |

The field reports' batch files live beside the tools they run, outside `src\`: `xhcisnap\snapon.bat`, `snapget.bat` and `snapoff.bat`, and `xhciqual\QUALREP.BAT` (the tools' section below).

## Generated, not tracked

| Path | What it is |
|---|---|
| `obj\`, `objchk*\`, `objfre*\` | DDK build output per flavour and architecture: the `.sys`, `.pdb`, `.res` and `.obj` files. |
| `build*.log` | The DDK build engine's logs. |

The three flavours (`debug`, `release`, `qemu`) and what distinguishes their outputs are in [design record 08](design/08-build-flavours-and-the-log-channel.md); the build procedure is in [build-and-test.md](build-and-test.md).

## The UAS class driver - `src\uas\`

`xhciuas.sys` (roadmap-hcd task 31-A.2), a class driver of this project's own above a device PDO whose interface is Mass Storage / SCSI / UAS, built by `scripts\build-driver.cmd` after `xhci98.sys` from its own `sources` and `makefile`, in the same three flavours.

| File | What it is |
|---|---|
| `uas.h` | The stack and the transports, the extensions of the FDO and the LUN PDOs, the cross-file entry points. |
| `uas_iu.c` / `uas_iu.h` | The pure core (host suite `test_uas`): the information units, the SAM LUN encoding, the tag allocator, the configuration walk for the UAS setting and its Pipe Usage descriptors, REPORT LUNS, the status fold, the CDB direction table, the storage ids, the policy reply and the telemetry buckets. |
| `uas_entry.c` | `DriverEntry`, `AddDevice`, the dispatch table, the flavour marker and version string. |
| `uas_fdo.c` | The FDO on the bus's PDO: PnP, power pass-through, the configuration and the four pipes, the transport choice, LUN discovery, the BusRelations of the LUN PDOs and the transfer-policy ask. |
| `uas_pdo.c` | One PDO per logical unit: its ids, the SRBs the storage class layer sends, the two storage IOCTLs it asks first. |
| `uas_xport.c` | The transport engine: queue, tags, the information units streamed and streamless, timeouts, task management, reset recovery, the telemetry record. |
| `uas_mem.c` | Every pool, MDL and IRP-memory call, the import allowlist's one site (`SITES=uas_mem.obj`). |
| `uas_streams.c` / `uas_streams.h` | The one adapter to the bus's `IOCTL_XHCI98_OPEN_STREAMS` and `CLOSE_STREAMS` (`src\xhci98_streams.h`). |
| `xhciuas.rc` | The version resource, from `src\xhci_version.h`. |
| `xhciuas.inf` / `xhciuas-amd64.inf` | The INFs, 32-bit and 64-bit (the second staged as `xhciuas.inf` in the 64-bit package); gated by `scripts\inf-gate\check-uas-inf.ps1`. |
| `sources`, `makefile` | Its DDK build description and stub. |

## The x86 helper - `src\hlpr\`

`xhcihlpr.sys` (design record 18 section 15; task 38.3, which replaced `src\msi86\`), built with WDK 7.1's `WXP` x86 environment at subsystem 5.00 and shipped in the x86 package only. It loads on 2000, XP, Vista and 7 and hands the x86 `xhci98.sys` the routines that image may not import because it also loads on Windows 98: the MSI pair on Vista and 7 and the timer service.

| File | What it is |
|---|---|
| `hlpr_entry.c` | The whole driver: `\Device\Xhci98Helper`, the channel's IOCTL (`xhci_hlpr.h`, compiled in from `src\`), the MSI pair looked up through `MmGetSystemRoutineAddress`, `ExSetTimerResolution` imported. |
| `xhcihlpr.rc` | The version resource, from `src\xhci_version.h`. |
| `sources`, `makefile` | Its build description and stub. |

## The Thunderbolt 3 connection manager - `src\cm\`

`xhcicm.sys`, the third driver (design record 21; `roadmap-usb4.md` Phase 39, scaffold of task 39-A.1), built by `scripts\build-driver.cmd` after `xhciuas.sys` in every x86 and amd64 run, from its own `sources` and `makefile`, in the same three flavours. One binary, two PnP roles: the NHI's function driver on every target and, on the NT targets only, the tunnel bridge's upper filter (with safe eject's child filter beside it), plus the query device of a started NHI. A new `.c` file is one more line in `src\cm\sources` (and, if it is pure, a `call :run` line in `test\run-host-tests.cmd`).

| File | What it is |
|---|---|
| `cm.h` | The driver's header: the extensions of the roles and of the query device (`CM_NHI`, `CM_FILTER`, `CM_KIND_QUERY`), the start stages (`CM_STAGE_*`) and the cross-file entry points. |
| `cm_vals.c` / `cm_vals.h` | The pure core (`test_cm`): the role a PDO's hardware ids name (`CmClassifyIds`: a PCI class 06/04 id is the filter's, anything else the NHI's), the vendor, device and class parsed from them, the NHI profile table (`CmProfileFor`: the firmware connection manager's generations and the USB4 software connection manager's hosts, Intel's ids from Linux `drivers/thunderbolt/nhi.h` and AMD's from FreeBSD `nhi_reg.h`), the INERT start's reasons (`CmStartDecision`), and the registry values' defaults, refusals and bounds (`CmValuesTake`; record 21 section 8). The INF gate reads the defaults out of its rule table. |
| `cm_entry.c` | `DriverEntry`, `AddDevice` (the role decided by `IoIsWdmVersionAvailable(1, 0x10)` and the PDO's `DevicePropertyHardwareID`), the dispatch table, the flavour marker and version string. |
| `cm_nhi.c` | The NHI function driver (task 39-A.4): attach; the start (the values read once, BAR0 found or the Code 10 refusal, the thread created) and, on the thread, the selection of record 21 section 1.4.2 (`TbtHwMatch` on the ids read from configuration space, `CmStartDecision`, `TbtHwPlatformInit`, `TbtHwSelect`), the ICM client's set-up and its bind (`TbtIcmStart`) after the PnP request is released, or the INERT start with its reason; the idle poll's tunnel actions handed to the re-enumeration seam (`cm_reenum.h`); the stop's unbind in section 5.6's order; the device table and the counters to the log. |
| `cm_filt.c` | The filters (NT only): the tunnel bridge's upper filter (its registration entry published after its start and withdrawn on stop, surprise removal and removal; every BusRelations read on its way back; a held bridge's D0 IRP retained) and safe eject's child filter (`Removable` set in `IRP_MN_QUERY_CAPABILITIES`; an orderly removal counted toward the port's eject); every other IRP passed down unchanged. |
| `cm_reg.c` | The one registry site: the values of record 21 section 8 from the NHI's driver key, through `IoOpenDeviceRegistryKey` and `RtlQueryRegistryValues` (`SITES=cm_reg.obj`). |
| `cm_log.c` | The shared log ring (`..\xhci_log.c`, compiled in by path): recording by `XhciCmLogVerbosity`; emission through `DbgPrint` at PASSIVE_LEVEL, from the thread after a pass that recorded something and at each stop, in every flavour when `XhciCmLogDebugView` is 1 and always in the checked flavours (the owner's decision of 2026-10-16, which reversed decision D12 for the release flavour), address records masked below level 4; mirrored to port 0xE9 in the qemu flavour (`SITES=cm_log.obj`). |
| `cm_pool.c` | The one pool and DMA site (`SITES=cm_pool.obj`): `ExAllocatePoolWithTag` / `ExFreePool` (optional rows: nothing allocates pool yet), and the ring block's common buffer below 4 GB from `IoGetDmaAdapter`'s adapter (`CmDmaOpen`, `CmDmaClose`, record 21 section 4.2). |
| `xhcicm.rc` | The version resource, from `src\xhci_version.h`. |
| `sources`, `makefile` | Its DDK build description and stub. |
| `xhcicm.inf` / `xhcicm-amd64.inf` | The NHI INFs, 32-bit (Windows 98 SE and ME, 2000 and XP, Vista and 7) and 64-bit (`NTamd64,NTamd64.6.0`), staged as `xhcicm.inf`; the NHI ids of record 21 section 3.3's list and design record 22 section 3.7's USB4 hosts with the class line `PCI\CC_0C0340` (the id lines exactly the rows of `cm_vals.c`, held by the gate's `CM-IDLIST`), and every registry value the driver reads written at its default under `FLG_ADDREG_NOCLOBBER` but `XhciCmTunnelBridge`, whose absence is meaningful. |
| `xhcicmf.inf` / `xhcicmf-amd64.inf` | The NT-only tunnel-port filter INFs (`$Windows NT$`), staged as `nt\xhcicmf.inf`, out of Windows 98's and ME's way (they read `$Windows NT$` INFs, task 39-0.4): a device-specific `UpperFilters` value on the Thunderbolt switch ports, the integrated controllers' tunnel root ports, the USB4 hosts' tunnel ports and the emulator's (record 21 section 3.3's lists, every id `&CC_0604`) with `pci.sys` kept as the port's service through `machine.inf`'s `PCI_DRV` sections (decision D2, read by task 39-0.4). |
| `cm_thr.c` | The NHI's connection-manager thread (record 21 section 4.4, task 39-A.4): created at start, ended at the stop, with `hcd_ctl.c`'s Windows 98 care around its handle; its work queue (a word of bits and the power IRP slots under a spin lock, PnP work waited for, power work never); the pure core's accessors - BAR0 through `READ_REGISTER_ULONG`, configuration space through `IRP_MN_READ_CONFIG` / `IRP_MN_WRITE_CONFIG` to the PDO (`hcd_svc.c`'s route), the Bus Master bit, the waits and a millisecond clock from the tick count (`KeQueryTickCount` and `KeQueryTimeIncrement`), monotonic. |
| `cm_power.c` | The NHI's power, as the device's power policy owner (record 21 section 4.4, task 39-A.4, on `hcd_power.c`'s model): the system IRP requests the device IRP and completes after it; a D3 runs the suspend's sequence (or at shutdown the unbind) on the thread before it goes down; a D0 is retained until the thread's reinitialisation and resume finish; no dispatch routine waits. |
| `cm_reenum.c` / `cm_reenum.h` | Re-enumeration and safe eject (tasks 39-A.5, driver half, and 39-A.8; record 21 sections 3.2, 6 and 7.4): the registration list between the NHI domains and the bridge filters (publish, withdraw, rundown; exclusive ownership and the active mark), the domain's tunnel-port set through `tbt_topo.c` over `HalGetBusData` reads, the port poll of section 6.5, the NT route (`IoInvalidateDeviceRelations` through each active bridge's filter) and the Windows 98 SE and ME route (CONFIGMG), the resume hold of a bridge's D0 IRP with its watchdog, safe eject's child filters and their eject notifications, and record 22's per-port eject entry (`CmReenumSetEject`). |
| `cm_ports.c` / `cm_ports.h` | The pure half of `cm_reenum.c` (`test_cm_ports`): the families' NHI, switch-port and root-port ids of record 21 section 3.3, checked against both INFs; Windows 98's PCI device ids; the bridge filter's snapshot; the resume hold's bound. |
| `cm_cfgmg.c` | CONFIGMG's services through the VMM dynalink (Windows 98 SE and ME, x86 only): the non-paged thunks, the `Get_Version` and root-id guards, and the calls of record 21 sections 6.1 and 7.4; and PCI.VXD's `_PCI_Read_Config` and `_PCI_Write_Config` through the same mechanism, behind their read guard (section 6.1.1) (`cm_cfgmg.h` has the numbers). |
| `cm_nvlink.c` / `cm_nvlink.h` | The Stor98 link-state channel (tasks 39-A.7 and 40-A.7; record 21 section 7.2, record 22 section 10), its pure half (`test_cm_nvlink`): the entry protocol over `stor98cm.h`'s table (UP with a new generation, `DOWN_QUIESCED`, the acknowledgement), both profiles' statement conditions, interface 3's locator bytes, proof query handler and publication order (residency, the proof opened first, the proof, the locator), its `REMOVE_PENDING` request, and both waits' steps (the deadline read before the acknowledgement). The header is the API the seam and the USB4 driver half call. |
| `cm_nvlinkos.c` | The channel's driver half: the never-freed table, the `\Callback\Stor98LinkState` callback object (NT), interface 3's `HKEY_DYN_DATA` current-boot proof (a dynamic key this image answers, made resident first, deleted at an unload) and `HKEY_LOCAL_MACHINE` locator through the VMM's registry services (98 SE and ME, x86), the scan that publishes UP before a re-enumeration, the channel's own SHPC reading, the statement, the acknowledgement wait, the 9x orderly removal, the idle poll's ceiling. |
| `stor98cm.h` | Stor98's interface header, carried byte for byte (BSD-3-Clause, its own notice; `legal-provenance.md` section 2a): never edited here. |
| `cm_cfgwk.c` | The Windows 98 SE and ME CONFIGMG worker: one system thread per domain that makes every CONFIGMG call of the re-enumeration and of safe eject's 9x half (the route's opening and guards, the bridge devnode walk, the vanished children's query-remove and remove, `Reenumerate_DevNode`, `DN_REMOVABLE`), so the connection-manager thread never blocks when CONFIGMG does (an Add New Hardware wizard open); requests posted and collapsed per member; a stop that finds it inside a call leaves it to free itself. |
| `cm_statos.c` | The status record's publication (design record 24 section 3.2): the value `XhciCmStatus` (`REG_BINARY`) of the NHI's hardware key, written by the connection-manager thread only when the record `cm_status.c` built has changed, and the install tool's ask `XhciCmStatusAsk` read from the driver key and echoed; both keys opened in the start (`CmStatusOpen`), since on 98 SE and ME `IoOpenDeviceRegistryKey` is a CONFIGMG service that waits while a wizard is open. A failed write is counted and retried at the next pass. |
| `cm_qryos.c` | The query device (design record 25 section 4.2): one control device object per started NHI, `\Device\XhciCm<n>` linked as `\DosDevices\XHCICM<n>` (n the first free of 0..15) and named in `XhciCmQueryName` beside `XhciCmStatus`; created after a start that succeeded, with a new epoch, and deleted once the thread has ended at a stop or removal (the name value written empty: `ZwDeleteValueKey` has no Windows 98 evidence). Its extension holds the snapshot the thread publishes after each pass under the device's own spin lock and no pointer to the NHI; `IOCTL_XHCICM_QUERY` answers from it with the pass counter, its age and the stopping flag, and never touches the hardware; the same device answers the log request (`CM_QL_IOCTL`) and the PCI request (`CM_QP_IOCTL`, `cm_qpci.c`). `cm_entry.c` routes every request to it by the extension's kind. |

Its gates: `scripts\inf-gate\check-cm-inf.ps1` (the four INFs, with `CM-NO9XBRIDGE`), `scripts\import-gate\xhcicm-imports.allow` and `xhcicm-imports-amd64.allow`, the flavour marker check and `scripts\source-stamp.ps1 -Driver xhcicm`.

### `src\cm\`'s pure core

The DDK-free half of `xhcicm.sys`, compiled into the driver from `src\cm\sources` and into the host suites below by `test\run-host-tests.cmd`. Each file names its sources in its header; `legal-provenance.md` section 2a lists the files lifted from `usb4cm` and those adapted from `usb4-emu` and Linux.

| File | What it is |
|---|---|
| `tbt_regs.h` | The host interface's registers, ring descriptor and control-packet fields, the firmware connection manager's status, mode and mailbox registers (task 39-0.2, record 21 section 4.1), and the router, adapter, path, TMU, PCIe, USB3 and notification fields the USB4 connection manager uses (task 40-0.1), each cited to the USB4 Specification 2.0 or the source it was checked against. Lifted from `usb4cm`'s `u4regs.h`, its MIT notice kept. |
| `tbt_ring.c` / `tbt_ring.h` | Ring 0 in Raw Mode, CRC-32C and the control transport with sequence numbers, timeouts and retries (task 39-A.2, record 21 sections 4.2 and 4.3), grown for the USB4 connection manager (task 40-A.1, record 22 sections 5.1 to 5.3): write requests, notifications, plain, enhanced and hot-plug acknowledgements, bounded waits and safe points; host suite `test_tbt_ring`. Lifted from `usb4cm`'s `u4hi.c`. |
| `tbt_icm.c` / `tbt_icm.h` | The firmware connection manager's client (task 39-A.3, record 21 sections 1.4 and 5): the ICM frames behind a per-generation table (Falcon Ridge, Alpine Ridge, Titan Ridge and Maple Ridge, Ice Lake), the bind, events, the device table, approval, the reconciliation and the resume refresh; host suite `test_tbt_icm`. Written for this project from `ICM-PROTOCOL.md` and Linux's layouts, cited. |
| `tbt_hw.c` / `tbt_hw.h` | The hardware profiles (the integrated controllers' force power, firmware-ready poll and LTR, with the power-ownership record), platform readiness and the four-test selection of the connection manager, the inert start included (tasks 39-A.3 and 40-A.2, record 21 section 1.4.2, record 22 section 3); host suites `test_tbt_hw` and `test_tbt_select`. |
| `cm_noirq.c` / `cm_noirq.h` | A Windows 98 SE or ME NVMe function with no routable interrupt, started with none (design record 28, `XhciCmNoIrqStart`, task 42-A.15): the pure half - the candidate rules, the result table, the per-device attempt state and the journal across generations, the gate over the existing logical configurations, the descriptor copy, the transaction's steps and budgets, late results, the diagnostic `XhciCmNoIrqDiag` and the counters - as a state machine of operations that `cm_cfgwk.c`'s worker makes (the interim of record 28's dated note) and, from task 42-A.12, the member's call worker |
| `tbt_brnum.c` / `tbt_brnum.h` | The Windows 98 SE and ME set-up of the bridges a tunnel brings up below a tunnel port (record 21 section 6.1.1, `XhciCmBridgeNumbering`): bus numbers depth first inside the port's range, memory windows sized from the BARs behind and placed inside the port's window, and the port's own interrupt line for functions whose pin reaches it on the port's pin; journalled, revalidated and undone on a refusal, over configuration-space accessors the caller supplies; host suite `test_tbt_brnum`. |
| `tbt_topo.c` / `tbt_topo.h` | A domain's tunnel-port set and its sources, the `XhciCmTunnelBridge` list, the PCI Express Link and Slot Status and SHPC readings and the port poll (task 39-A.5, record 21 sections 6.3 and 6.5), and the router-named source and learned correspondence of the USB4 profile (task 40-A.4, record 22 section 7.6), over configuration-space accessors the caller supplies; host suite `test_tbt_topo`. |
| `tbt_u4cm.c` / `tbt_u4cm.h` | The USB4 software connection manager (tasks 40-A.3 and 40-A.8's pure part, record 22 sections 6 to 9): the host and device routers, lane bonding, the TMU, Configuration Valid, paths, the PCIe and USB3 tunnels, hot plug and teardown, the quirk table, discovery and preservation of tunnels found at start, and Thunderbolt 3 routers behind a USB4 host; host suite `test_u4cm`. Adapted from `usb4-emu`'s `u4cm.c` and Linux's `drivers/thunderbolt`, notices kept. |
| `cm_cfgmg.h` | CONFIGMG's VxD id, service numbers, flags and result values for the Windows 98 SE and ME route (task 39-0.3, record 21 sections 6.1 and 7.4), each read from the Windows 98 SE DDK's `CONFIGMG.H` with its line, and PCI.VXD's VxD id and two configuration-space service numbers (section 6.1.1) from its `PCI.H` and `VMM.H`; values only. |
| `cm_live.c` / `cm_live.h` | The host interface's liveness monitor, its pure half (record 21 sections 5.5 and 5.6, record 22 section 3.5): whether the NHI's registers can still be trusted after a platform power-off or reset Windows did not report, judged from one register the driver programmed and reads back (RX ring 0's size); the states from SUSPECT to ABSENT and CONFIG_LOST, the light and full recovery and its re-arm rule, revocation, and the gate on which accesses the driver may make meanwhile; host suite `test_cm_live`. `cm_thr.c` samples and gates, `cm_nhi.c` runs the actions. |
| `cm_status.h` | The status record's layout (design record 24 section 3): little-endian 32-bit fields and byte strings at fixed offsets, shared byte for byte by `xhcicm.sys` and `XHCIINST.EXE` (`xhciinst\inst_core.c`), so it uses no DDK or Windows type; a layout change is a new `CM_ST_VERSION`. |
| `cm_status.c` / `cm_statb.h` | The status record's builder (record 24 section 3.1), from the connection manager's own snapshot: the ICM device table or the USB4 port set and its routers, names cut to printable ASCII, a UUID cut to its last 16 bits and no key or approval field ever copied, the change compare that ignores the stamp, the entry limit, and the Windows 98 instance path over configuration space; host suite `test_cm_status`. `cm_statos.c` publishes what it builds. |
| `cm_query.h` | The query device's answer (design record 25 section 4.3): header, the status record, one 32-byte extension per router, the NHI's instance id; little-endian fields at fixed offsets shared byte for byte by `xhcicm.sys`, `XHCITR.EXE` and the host tests, no DDK or Windows type; a layout change is a new `CM_Q_VERSION`. |
| `cm_query.c` / `cm_qryb.h` | The query device's answer builder: the record framed with the liveness state and the inert and truncated flags, the extensions (USB4: host lane 0's rate, width, link state and speed code as `tbt_u4cm.c` last read them in the port's current connection; ICM: the rate and width the device-connected event carried), the content generation, and the IOCTL's sizing answers; host suite `test_cm_query`. `cm_qryos.c` publishes and answers. |
| `cm_qpci.c` | The query device's PCI answer, `IOCTL_XHCICM_PCI` (design record 25 section 4.7): the NHI, the tunnel ports and the functions below them by a bounded walk over a reader the caller supplies, never a write; pure, host suite `test_trpci`. `cm_qryos.c` reads through `HalGetBusData` and answers. |

### `src\cm\`'s host suites

In `test\`, each run by `test\run-host-tests.cmd`, which `build-driver.cmd` runs before any DDK build.

| File | What it is |
|---|---|
| `test_cm.c` | `cm_vals.c`: the role a PDO's ids name, the profile table, the inert start's reasons, the registry values' defaults and bounds. |
| `test_cm_ports.c` | `cm_ports.c`: the families' ids against both INFs, Windows 98's PCI device ids, the bridge filter's snapshot, the resume hold's bound. |
| `test_tbt_ring.c` | `tbt_ring.c`, with the specification's Appendix A.2 CRC vectors, wrap, exhaustion and the USB4 transport. |
| `test_tbt_icm.c` | `tbt_icm.c` over the ring: every frame `ICM-PROTOCOL.md` quotes, the other generations' frames synthesised from Linux's structures, and a disconnect during an approve. |
| `test_tbt_hw.c`, `test_tbt_select.c` | `tbt_hw.c`: the hardware profiles and their id table against `cm_vals.c`'s, and the selection, every `NVM_AUTH_DONE` and `ICM_EN` combination among its cases. |
| `test_tbt_topo.c` | `tbt_topo.c` on the P14s and emulator layouts, a generic layout, two-port switches, listed and unlisted root ports, two domains and the override list. |
| `test_cm_noirq.c` | `cm_noirq.c` over a modelled devnode whose status, header, binding, logical configurations and descriptors its operations read and write: the started case, every rule, the interrupt record and the fresh classification, the gate, leftovers across generations, the attempts bound, the reprocess-only attempt, handles gone or moved, admission changed between steps, the budgets, late results, the gate closing |
| `test_tbt_brnum.c` | `tbt_brnum.c` over a modelled hierarchy whose configuration cycles route as bridges route them: the P14s tunnel port with the ORICO ASM2464 chain (bus numbers, windows, interrupt lines as its BIOS sets them), a chain present at boot, an endpoint alone, a dock, multi-function and mixed-alignment layouts, tight and short ranges, the bounds, and every refusal with its undo. |
| `test_cm_nvlink.c` | `cm_nvlink.c`: the Stor98 link-state channel's entry protocol, both profiles' statement conditions and the 9x record's bytes (tasks 39-A.7 and 40-A.7). |
| `test_u4cm.c` | `tbt_u4cm.c` over the fake fabric, with the `minimal`, `intel-adl` and `asm4242` presets and two domains. |
| `tbt_fake.h` | The modelled host interface `test_tbt_ring`, `test_tbt_icm` and `test_tbt_hw` drive, from the USB4 Specification 2.0 and `ICM-PROTOCOL.md`. |
| `tbt_topo_p14s.h` | The ThinkPad P14s Gen 1's PCI configuration space as captured under DOS in `usb4-emu`, for `test_tbt_topo`. |
| `u4_fakefab.c` / `u4_fakefab.h` | `test_u4cm`'s C89 fake USB4 fabric, one domain per instance, written from the specification and `usb4-emu`'s documented host models, independently of `tbt_u4cm.c` (decision U11). |
| `test_inst.c` | `xhciinst\inst_core.c`, the install tool's pure half (design record 23). |
| `test_tr.c` | `xhcitr\tr_core.c`, the USB tree tool's pure half (design record 25 section 10): text, the connection answers, descriptors, the query's parsing and eject gates, the router keys and event diff, the command line, the report. |
| `test_trpci.c` | `src\cm\cm_qpci.c` and `xhcitr\tr_pci.c`: the bounded walk below a tunnel port over modelled buses, the capability walk with bad lists, the decoders, 79 columns (design record 25 revision 12). |
| `test_trlog.c` | `xhcitr\tr_log.c` with `src\cm\cm_query.c` linked: `xhcicm.sys`'s log answer as the driver's builder makes it, its parse and bounds, `xhci98.sys`'s pass-through window, and `xhcitr\tr_hcctr.h`'s names held to `XHCIHC_COUNTERS` by offset (design record 25 revision 9). |
| `test_xhq.c` | `src\xhci_qry.c` and the tool's reader of it: what each log level serves, the ring's text and cut, the configuration read's states, the sizing, the default level 2. |
| `test_cm_live.c` | `cm_live.c`: sampling by elapsed time, SUSPECT on the first unanswered sample and ABSENT on the third, CONFIG_LOST on a changed register, the light and full recovery, its failure and the re-arm rule, the USB4 profile's containment, revocation and the access gate. |
| `test_cm_status.c` | `cm_status.c`: the record from an ICM table and from a USB4 port set, what is never copied into it, the change compare, the echo, the entry limit, and the Windows 98 instance path over a fake configuration space (design record 24 section 10). |
| `test_cm_query.c` | `cm_query.c`: the header's golden bytes, the record carried byte for byte, the extensions under the USB4 connection manager and the ICM, the instance id, the sizing answers, the router-count edge, a truncated or malformed record, the content generation and the pass age's arithmetic (design record 25 section 10). |

### Scripts for the Thunderbolt and USB4 work

| File | What it is |
|---|---|
| `scripts\inf-gate\check-cm-inf.ps1` | The INF gate for `xhcicm.sys`'s four INFs, with its self-test (record 21 sections 3.3 and 10.3, record 22 section 3.7). |
| `scripts\usb4\run-usb4-guest.ps1` | Boots one of this project's guests on the `usb4-emu` QEMU fork with its Thunderbolt 3 and USB4 device models, on this project's own overlays and ports 47900 to 47999 (task 39-0.7, record 21 section 10.2). |
| `scripts\usb4\usb4-qmp.ps1` | Drives such a guest over QMP: its state, the device router's unplug and plug, the rest of its verbs in its header. |
| `scripts\usb4-crosscheck\run-crosscheck.sh`, `u4cm-crosscheck.c`, `u4cm-glue.c`, `u4cm-glue.h` | Task 40-A.5's cross-check: `tbt_ring.c` and `tbt_u4cm.c` built strictly as C89 and run against `usb4-emu`'s fabric model, read in place and never modified (record 22 section 14.1). |

## Host suites and other files in `test\`

The host suites of `xhci98.sys` and `xhciuas.sys`, and the shared and guest-side pieces. Each suite is one `call :run` line in `test\run-host-tests.cmd`, which names the `src\` files it links; the suites of `src\cm\` and the tools are in the table above.

| File | What it is |
|---|---|
| `run-host-tests.cmd` | The host test runner: every suite compiled with the in-repo MSVC 6.0 under `XHCI_HOST_TEST` into its own output directory and run; `scripts\build-driver.cmd` runs it before any DDK build. |
| `test_harness.h` | The one copy of the check helpers every host suite includes (`CHECK`, `CHECK_EQ`, the counts and the failure format); header-only, with no expectations of its own (design record 03). |
| `test_membuf.c` | `xhci_mem.c` and the common-buffer constants of `xhci.h`; also built for amd64 (`test_membuf_amd64`). |
| `test_packet.c` | `xhci_usbport.h`'s declarations, offset by offset, on x86 and amd64 (`test_packet_amd64`). |
| `test_ring.c` | `xhci_ring.c`: TRB encoding, the cycle bit, Link TRBs and wrap. |
| `test_caps.c` | `xhci_caps.c`: the capability walk with malformed chains, port classification, the PSI tables and speed decoding, the version floor. |
| `test_port.c` | `xhci_port.c`'s `PORTSC` masks, bit by bit. |
| `test_ctx.c` | `xhci_ctx.c`'s encoders against hand-typed golden DWORDs. |
| `test_topo.c` | `xhci_topo.c` over hand-built setup packets; also checks `hub_port_vectors.h` is well formed. |
| `test_desc.c` | `xhci_desc.c`'s walk and the Interval conversion in `xhci_ctx.c`. |
| `test_log.c` | `xhci_log.c`: append, wrap, record cap, the flush decision, the drain's order, the accounting identities. |
| `test_xfer.c` | `xhci_xfer.c`'s control, bulk and interrupt engine, the soft retry and the slot-fatal decision. |
| `test_iso.c` | `xhci_xfer.c`'s isochronous engine over the usbport-shaped block `hcd_io.c` builds. |
| `test_td.c` | The TD builders over the HCD's own buffer mapping (`xhci_xfer.c` with `xhci_pipe.c`). |
| `test_enum.c` | `xhci_enum.c`: every path a device can take through the enumeration machine. |
| `test_link.c` | `xhci_link.c`: the USB3 link machine and the 29-A.5 hold. |
| `test_pipe.c` | `xhci_pipe.c`: hand-worked Endpoint Context, setup, split and status vectors. |
| `test_func.c` | `xhci_func.c`: splitting, the parent's and functions' ids, the instance-id rules. |
| `test_hub.c` | `xhci_hub.c`: the descriptor, the bitmap, each port decision, the ids, the descriptor request's checks. |
| `test_sshub.c` | `xhci_sshub.c`: the SuperSpeed hub decisions as tables. |
| `test_stream.c` | `xhci_stream.c`: array sizes, contexts, layouts and encoders. |
| `test_xport.c` | `xhci_xport.c` over constructed device shapes. |
| `test_silent.c` | `xhci_silent.c` over constructed configurations. |
| `test_psw.c` | `xhci_psw.c` over a modelled configuration space. |
| `test_pirq.c` | `xhci_pirq.c` over a modelled machine. |
| `test_msi.c` | `xhci_msi.c` and `xhci_hlpr.c`: the capability read, the requirements list, the helper binding with the timer's event. |
| `test_snap.c` | `xhci_snap.c` and `xhci_xstat.c`: every word of the HCD region located once, and the transfer-size readings. |
| `test_tol.c` | `xhci_tol.c`: the values, the tolerance clock, the cap, the retry, cycle and halt decisions, the budgets. |
| `test_strict.c` | `xhci_strict.c`, every cell of the table written out from the specification's pseudo-code. |
| `test_inj.c` | `xhci_inj.c`: the trigger's encoding and every register answer. |
| `test_susp.c` | `xhci_susp.c`: admission, the device's decision, the sequences, wake, stage B. |
| `test_xpipe.c` | `xhci_xpipe.c` over modelled physical layouts. |
| `test_poll.c` | `xhci_poll.c`: the values, the deadline arithmetic, the ownership machine, the watcher, the timer's request machine. |
| `test_quirk.c` | `xhci_quirk.c`: each value, every listed id and its neighbours, the policy, the packed word. |
| `test_own.c` | `xhci_own.c`: the decision table cell by cell and every path through an interface switch. |
| `test_uas.c` | `src\uas\uas_iu.c`, the pure core of `xhciuas.sys`. |
| `hub_port_vectors.h` | One downstream port of an external USB 2.0 hub as the bus's hub class tracks it, written as a vector table (roadmap-hcd tasks 27-A.1 and 27-A.4); `test_topo.c` checks it is well formed. |
| `pmclient\pmclient.c` | `XHCIPM.SYS`, selective suspend's test client (roadmap-hcd task 36.34, design record 14 section 10): a WDM function driver that idles one USB device on demand. Test only: the packaging gate refuses a release that carries it (`build-and-test.md`, "The test client"). |
| `pmclient\pmctl.c` | `PMCTL.EXE`, its console tool (MSVC 6.0). Test only. |
| `pmclient\pmioctl.h` | The control interface the two share. |
| `pmclient\xhcipm.inf` | The client's INF, bound by hardware id on a VM leg. |
| `pmclient\build.cmd`, `pmclient\sources`, `pmclient\makefile` | The client's build: the driver with the DDK, the tool with MSVC 6.0. |
| `xfertest\xfertest.c` | `XFERTEST.EXE`, the transfer-size test client (roadmap-hcd tasks 36.21, 36.22 and 36.26): moves data in requests of a given size from or to a file or raw disk and times them. In no package. |
| `xfertest\build.cmd` | Its MSVC 6.0 build. |

## The user-mode tools

Each tool is a tree at the repository root, built by its own `build.cmd` and published under `releases\<version>\tools\`; `scripts\build-driver.cmd` runs a tool's self-test when its executable has been built.

### `xhcitr\` - the USB and Thunderbolt tree (design record 25)

| File | What it is |
|---|---|
| `xhcitr.c` | The system half of `XHCITR.EXE` and `XHCITR64.EXE`: the window, the USB branch through the hub IOCTLs, the Thunderbolt branch through `xhcicm.sys`'s query device, the driver logs and PCI configuration nodes, the eject transaction under the mutex `XHCIINST` shares, the notification-area icon. |
| `tr_core.c` / `tr_core.h` | The pure half: text, the USB answers' bytes, descriptor decoding, the query's parsing and eject gates, the status record's texts, the tray's router keys and event diff, the command line, the report (host suite `test_tr`, which also links `xhciinst\inst_core.c` for record 24's rules). |
| `tr_log.c` / `tr_log.h` | The pure half of the "Driver log" nodes: `xhcicm.sys`'s log answer and `xhci98.sys`'s log and counters read as bytes and turned into text (record 25 sections 3.5, 4.5 and 4.6; host suites `test_trlog` and `test_xhq`). |
| `tr_pci.c` / `tr_pci.h` | The pure half of the "PCI configuration" nodes: `xhcicm.sys`'s PCI answer and `xhci98.sys`'s PCI region read as bytes and a function's configuration space decoded, every offset cited to the PCI specifications (record 25 sections 3.6 and 4.7; host suites `test_trpci` and `test_xhq`). |
| `tr_hcctr.h` | The names of `XHCIHC_COUNTERS`, one line per word in field order; `test_trlog` holds each line to the field of the same name by offset. |
| `tr_res.h` | The resource and command numbers shared by `xhcitr.rc` and `xhcitr.c`. |
| `xhcitr.rc`, `xhcitr.manifest`, `xhcitr.ico` | The icon, the version (from `src\xhci_version.h`), the menus and the About dialog; the manifest runs it as the invoker with Common Controls 6. |
| `build.cmd` | `XHCITR.EXE` with MSVC 6.0 and `XHCITR64.EXE` with WDK 7.1 amd64. |
| `selftest.cmd` | The import tables, the `/selftest` and the switch refusals, which `build-driver.cmd` runs. |
| `check-imports.ps1` | The import gate `selftest.cmd` runs: reads the PE import directory and requires exactly KERNEL32, USER32, GDI32, ADVAPI32, SHELL32, COMCTL32 and COMDLG32 and no delay-load imports. |
| `README.TXT` | The text of the published `readme.txt`. |

### `xhciinst\` - the install tool (design record 23)

| File | What it is |
|---|---|
| `xhciinst.c` | The system half of `XHCIINST.EXE` and `XHCIIN64.EXE`: install, update, uninstall, `/devices` and `/eject`. |
| `inst_core.c` / `inst_core.h` | The pure half: the INF reader, the models section per system, the action per device and per service group, the registry value table and its applicability, record 24's unit rules, the switches and exit codes (host suite `test_inst`). |
| `xhciinst.rc`, `xhciinst.manifest` | The version resource and the elevation request. |
| `build.cmd` | Both executables, each also compiling `src\xhci_psw.c` for the driver's own switchover gate. |
| `selftest.cmd`, `check-imports.ps1` | The `/selftest`, the switch refusals and the import gate (exactly `KERNEL32.dll` and `ADVAPI32.dll`). |
| `INSTALL.BAT` | Staged at the package root; runs the tool. |
| `README.TXT` | The published `readme.txt`. |

### `xhcicln\` - the cleanup tool (task 38.8)

| File | What it is |
|---|---|
| `xhcicln.c` | `XHCICLN.EXE` (MSVC 6.0) and `XHCICL64.EXE` (WDK 7.1 amd64), one C89 source: lists, and on request removes, the non-present devnodes an `xhci98.sys` before `2.4.0.0` left under `XHCI98\` (design record 13 section 10.13.8). |
| `xhcicln.rc`, `xhcicln.manifest` | The version resource and the elevation request. |
| `build.cmd`, `selftest.cmd` | The build, and the `-selftest` `build-driver.cmd` runs when the executable has been built. |
| `README.TXT` | The published `readme.txt`. |

### `xhcisnap\` - the snapshot reader

| File | What it is |
|---|---|
| `xhcisnap.c` | `XHCISNAP.EXE`: reads the extension, the PORTSC array, the enabled slots and the HCD region through the door's pass-through, and switches the log channel. To be retired in favour of `XHCITR` (roadmap-usb4 task 42-A.4). |
| `README.md` | Its user and maintainer guide. |
| `build.cmd`, `selftest.cmd` | The MSVC 6.0 build, and the report path driven with no controller. |
| `check-counter-names.ps1` | Checks the built tool's counter names against `src\xhci_counters.h` word by word. |
| `snapon.bat`, `snapget.bat`, `snapoff.bat` | The field reports' Windows batch files (roadmap-hcd task 38.1): `XHCISNAP -on %1`, `XHCISNAP -report %1` and `XHCISNAP -disable`, one file each for `COMMAND.COM` and `cmd.exe`, published as `SNAPON.BAT`, `SNAPGET.BAT` and `SNAPOFF.BAT` and held by the packager's self-tests (`Get-ReportBatchFaults`, `Assert-ReportBatchLayout`), the source charset gate and the batch-file line-ending gate. |

### `xhciqual\` - the DOS qualifier

`XHCIQUAL.EXE`, built with Open Watcom and DOS/32A; its files, host suites (`xhciqual\test\`) and batch files (`QUALREP.BAT` among them: the read-only probe saved to a new `mDDhhmmS.LOG`) are described in `xhciqual\README.md` and `xhciqual\hardware-testing.md`.

## Where each task's code went

The record of what each task added to which file, oldest first, as each was written; where two statements disagree the later one holds, and a block marked withdrawn or reverted describes code no longer in the tree. The tables above are the current map.

**Since task 26-A.2 (2026-10-03)** the kept files are built again - `xhci_init.c`,
`xhci_cmd.c`, `xhci_evt.c`, `xhci_pci.c`, `xhci_dbg.c` - with their usbport
service calls replaced by `hcd_svc.h`'s, and `xhci_xfer.c` came back holding only
the completion-code table, lifted unchanged from `1.2.0.0`, and whole since
26-A.4 as the transfer engine. The HCD's own files:
`hcd.h` (the device extensions), `hcd_entry.c` (DriverEntry, AddDevice,
dispatch), `hcd_pnp.c` and `hcd_power.c` (the controller FDO's PnP and power),
`hcd_ctl.c` (start and stop, the interrupt and DPC, the controller thread),
`hcd_svc.h`/`hcd_svc.c` (the services that replace usbport's, and the
controller lock), `hcd_dma.c` (the one DMA adapter file), `hcd_dev.c` (the
device layer the kept files call into) and `hcd_pool.c` (the one pool file;
not linked until the first allocation, 26-A.4). Batch (b) (26-A.3, 26-A.4)
added `hcd_rh.c` (the root-hub PDO and FDO), `hcd_enum.c` (the controller
thread's port service and enumeration executor), `hcd_pdo.c` (device PDOs,
their ids and lifecycle) and the pure `xhci_enum.c` (the per-port enumeration
machine, host suite `test_enum`). Batch (c) (26-A.5, 26-A.6) adds `hcd_urb.c`
(the device PDOs' internal device controls) and the pure `xhci_pipe.c`
(configuration walk, endpoint parameters, Configure Endpoint plans, setup
packets, buffer splits and the USBD status table; host suite `test_pipe`). Phase 29 (drafted
2026-10-04) adds the pure `xhci_link.c` (a USB3 root port's link state machine and
29-A.5's hold and release rules; host suite `test_link`). Phase 30 (drafted
2026-10-04) adds the pure `xhci_sshub.c` (the SuperSpeed hub class's decisions:
the 0x2A descriptor, the SuperSpeed port decision, hot or warm hub-port reset,
the extended port status and a downstream SuperSpeedPlus rate's speed ID; host
suite `test_sshub`) and `hcd_sshub.c` (the SuperSpeed half of a USB 3 hub on the
controller thread, entered from `hcd_hub.c` where `hub->Usb3`).
Task 31-A.1 adds the pure `xhci_stream.c` / `xhci_stream.h` (bulk streams'
plan, block layout, Stream Context and command encoders, the event's stream;
host suite `test_stream`) and `xhci98_streams.h`, the private open-streams
interface a separate class driver includes; the streams themselves are served
in `hcd_cfg.c` and reach every stream's pipe through `hcd_io.c`. Task 31-A.3
adds the pure `xhci_xport.c` / `xhci_xport.h` (the storage transport policy:
Bulk-Only or UAS for an interface offering either, and the ids that follow;
host suite `test_xport`), applied in `hcd_pdo.c`. Task 33.2 adds the
instance-id rules to the pure `xhci_func.c` (`XhciFuncSerialId`,
`XhciFuncSerialSame`, `XhciFuncInstanceId`; host suite `test_func`), the
serial read to `hcd_enum.c` (`HcdDeviceReadSerial`) and the duplicate and
dormant matching to `hcd_pdo.c`. Task 33.4 adds `hcd_hubfdo.c`
(an external hub's FDO, the third PnP role: each hub the bus serves is a
devnode under `USB\XHCI98_HUB` or `USB\XHCI98_HUB30` since task 38.8,
`XHCI98\HUB` or `XHCI98\HUB30` before, design record 13 section 10.11)
and the hub PDO's pure half in `xhci_hub.c` (its ids, the presented parent,
node information; host suite `test_hub`). Task 36.43 gives `hcd_hubfdo.c` a
fourth PnP role, a composite device's FDO over its composite-parent PDO
(`USB\XHCI98_COMPOSITE`, `XHCI98\COMPOSITE` before task 38.8, design record
13 section 10.12), with the parent's ids
in the pure `xhci_func.c` (`XhciFuncParentId`, host suite `test_func`) and
the parent, its functions and their in-place re-presentation in
`hcd_pdo.c`. Task 38.12 has that FDO select the configuration down its
own stack at its start, and the parent PDO answer it and keep the
caller's descriptor as the record a function's select takes its
`bInterval` from (`hcd_urb.c`, its last block; the pure precedence and
checks in `xhci_pipe.c`, host suite `test_pipe`; design record 13
section 10.14). Task 34.3 adds the pure
`xhci_psw.c` / `xhci_psw.h` (the Intel EHCI-to-xHCI port switchover: the
device-id gate, the board exemption, the `XhciIntelPortSwitch` rule, and the
route and release sequences over two caller-supplied configuration-space
accessors; host suite `test_psw`), executed from `hcd_ctl.c` at start and
stop and from `hcd_power.c` at each return to D0 and a shutdown's D3
([design record 16](design/16-intel-port-switchover.md)). Task 35-T adds the
pure `xhci_tol.c` / `xhci_tol.h` (controller tolerance: the decisions of
each tolerant behaviour, the `XhciTolerance`, `XhciIntervalCap` and
`XhciAvgTrbEsit` rules, the tolerance clock's tick arithmetic, the budgets
and intervals as named constants, and the counters and state kept in the
extension; host suite `test_tol`), its clock run from `hcd_svc.c` and its
values read in `hcd_ctl.c`
([design record 17](design/17-controller-tolerance.md)). Task 35.3 adds the
pure `xhci_snap.c` (the layout of the snapshot's HCD region - each root
port's enumeration state and the counter block, word by word; host suite
`test_snap`), filled by `hcd_door.c`, and a root port's enumeration notes to
`xhci_enum.c` (their packing and budget; host suite `test_enum`), written
from `hcd_enum.c`. Phase 35 adds no other file to `src\` but the qemu
flavour's fault injection below; what it changes in existing ones: task 35.1 changes the speed decoding in `xhci_caps.c`
(one rule, `xhciProtocolRate`, that the class, the rate and the inverse
lookup ask, the fallback for an ID 4 to 7 a USB 3 table does not list, and
`XhciPortSpeedSource`; the hub child's lookup in `xhci_sshub.c`; host
vectors in `test_caps`, `test_link` and `test_sshub`, the E460's, P14s
Gen 1's and B490's tables replayed from their `XHCIQUAL` logs); task 35.5
moves the switchover's start decision and its per-register write set
into `xhci_psw.c` (`XhciPswMode`, `XHCI_PSW_LIFE`, the lifetime functions;
`test_psw`), which `hcd_ctl.c` and `hcd_power.c` call; and task 35-T's
behaviours are executed in the existing HCD files (`hcd_ctl.c` the
backstop, the HCH and all-ones steps and the values read at start,
`xhci_cmd.c` and `xhci_init.c` the HCH request and the recovery window,
`hcd_io.c` and `hcd_urb.c` the parked submissions of a contained
controller, `hcd_enum.c` the location budgets, the root-port PED and
over-current handling and `HcdTolLocCharge`, `hcd_svc.c` the tolerance
clock, `hcd_cfg.c` the interval cap and its order with fast polling;
35-T.2's soft retry in `xhci_xfer.c` (the interception in
`XhciXferEvent`, the replay `XhciXferRetryReplay`, the refusal of a
pointer above 4 GB `XhciXferEventHighRefused`; `test_xfer`), `hcd_dev.c`
(`HcdDevRetryReplay` and the retry's counts) and `hcd_cfg.c` (the thread's
`HcdCfgRetryService`, `hcdCfgRetryOne` and `hcdCfgRetryResetEndpoint`, and
the quiesce's settle); 35-T.3/4's device cycle in `hcd_dev.c` (the
producer `hcdTolCycleEvent`), `hcd_enum.c` (`HcdTolCycleMark`,
`hcdCycleResolve`, `hcdCycleService`, the pre-PDO `hcdCycleAfter` and the
thread's own wait `hcdThreadControlQuiet`) and `xhci_enum.c` (the
`XHCI_ENUM_EV_ABANDONED` outcome; `test_enum`), its decisions in
`xhci_tol.c` (`test_tol`); and 35-T.8's counters in `hcd_dev.c`
(`hcdTolCountEvent`, `hcdTolSumQueue`), `xhci_snap.c` (the HCD region's
header grown to 20 words; `test_snap`), `hcd_door.c` (the terminal reason
under the lock, from `xhci_tol.c`'s `XhciTolTerminal`) and `hcd_log.c` (the
`tol.*` counter block at a stop). Outside `src\`,
`xhciqual\mmiodiag.c` prints each protocol's PSI table and the raw
extended-capability chain (host suite `xhciqual\test\test_mmiodiag.c`),
`xhciqual\quirks.c` gains the report-only AMD rows, and
`xhcisnap\xhcisnap.c` decodes the enumeration notes and the HCD region,
and since 35-T.8 the tolerance state (`print_tol`, `-selftest-tol`).
Task 35-T.9 adds the qemu flavour's fault injection,
compiled into that flavour alone (`XHCI_FLAVOUR_QEMU`) and into neither
image that ships: the pure `xhci_inj.c` / `xhci_inj.h` (the
`XhciQemuInject` trigger's encoding and sequence rule, the ports a fault
may be aimed at, and every register answer the layer gives; host suite
`test_inj`) and the driver half `hcd_inj.c` (the thread's trigger read
and executors, the ISR's lost-interrupt window, and the PORTSC hooks in
`xhci_pci.c`), and its second part's soft-retry and device-cycle faults
with their hooks in the drain (`xhci_evt.c`), the context reads, the
command path, the doorbells and the service step (`hcd_cfg.c`,
`hcd_ctl.c`, `hcd_enum.c`, `hcd_hub.c`); task 35.4's `XhciQemuPsiE460` override is
`XhciPortMapOverridePsi` in `xhci_caps.c`, read in `hcd_ctl.c`
([design record 17](design/17-controller-tolerance.md) section 5;
`build-and-test.md`, "The qemu flavour's test aids"). The branch's last
revision adds, in the same files: the slot-fatal teardown
(`hcd_dev.c`'s `hcdSlotFatalMark`, `hcd_enum.c`'s `hcdSlotFatalService`,
the pure `XhciXferSlotFatal` in `xhci_xfer.c`; `test_xfer`); the
unproven invalidation's proof and containment (`SlotsUnproven`,
`HcdCtlProveDmaStopped` in `hcd_ctl.c`, the branch in `hcd_enum.c`'s
enumeration service) and `hcd_ctl.c`'s `hcdRestoreBusMaster`; the
configuration's charged fallback `hcdCfgCycle` in `hcd_cfg.c`; and in
`xhci_tol.c` `XhciTolLocActive`, `XhciTolLocHeld`, the once-per-cycle
charge `XhciTolCycleCharge` and the terminal reason 5 (`test_tol`).
**Withdrawn 2026-10-07 with `2.2.0.1`; the code below is reverted and
`xhci98_report.h` removed (issue 12 section 3.2).**
Phase 36 (issue 12) adds one file, `xhci98_report.h`, the private
client-report interface `xhciuas.sys` includes beside `xhci98_streams.h`,
answered in `hcd_urb.c` (`hcdClientReport`) and kept for the snapshot by
the pure `XhciSnapClientPick` and `XhciSnapClientStore` in `xhci_snap.c`
(the HCD region's header grown to 23 words; `test_snap`), filled by
`hcd_door.c`. Task 36w.1 adds `HcdQueueDrain` to `hcd_ctl.c` (the drain at
interrupt enable, and the backstop's and the injector's queueing) and the
busy engine's wait to `hcd_enum.c`'s `hcdCommand`, its slices the pure
`XhciEnumBusyWait` in `xhci_enum.c` (`test_enum`); task 36w.2 the counter
block's six appended counters (`xhci_counters.h`), the client port resets
counted in `hcd_cfg.c`, and in `src\uas\` the report itself (`UasReport`
in `uas_fdo.c`, the transfer failures in `uas_xport.c`, the sense word
`UasSenseWord` in `uas_iu.c`; `test_uas`); task 36w.3 the qemu flavour's
`XhciQemuStartHold` in `hcd_inj.c`. `xhcisnap\xhcisnap.c` names the new
counters and decodes the reports (`print_hcd_clients`).
**Reverted 2026-10-08 (owner): `hcd_diag.c` is removed, and the code below,
`XHCISNAP`'s `print_diag` included, is restored to `2.2.0.0`'s (issue 12
section 3.3).**
The diagnostic build `2.2.0.2` (task 36.1, issue 12 section 3.3) adds one
file, `hcd_diag.c`: the recovery timeline (each slow request from
`HcdCfgQueue` to its completion in `HcdCfgService`, with the
CLEAR_FEATURE outcome and the commands `hcd_enum.c`'s `hcdCommand` issued
for it; each transfer cancel routine's run in `hcd_io.c`; the thread's
cancel, drain and removal aborts in `hcd_cfg.c`) and the backstop samples
`hcd_ctl.c`'s backstop (then `hcdBackstop`, now `HcdCtlBackstopTick`) takes, with the ISR's, DPC's and thread's
stamps beside them. `hcd_door.c` reads them, and each open pipe's oldest
transfer and Output Endpoint Context, into four parts appended to the HCD
region (the header grown to 31 words, version unchanged); the layout and
the pure `XhciSnapMs`, `XhciSnapOldest`, `XhciSnapRingNext`,
`XhciSnapRingFind`, `XhciSnapRecCommand`, `XhciSnapIrqClass`,
`XhciSnapPipeClass`, `XhciSnapPipeBefore` and `XhciSnapClearOf` are
`xhci_snap.c`'s (`test_snap`). `hcd_urb.c` and `hcd_io.c` keep each
transfer's URB function, flags and claim and publish ticks for it;
`xhcisnap\xhcisnap.c` decodes all of it (`print_diag`). It records and
decides nothing.
Task 36.3 (issue 12 section 5.1) adds the pure `xhci_pirq.c` /
`xhci_pirq.h` (the Intel PCH interrupt routing quirk: the
`XhciIntelIrqRoute` rule, the six reviewed xHCI/LPC pairs and the verified
E7250, the PIRQ that delivers the assigned IRQ in APIC and PIC mode, and
`XhciPirqRun`, which checks in order over caller-supplied configuration,
bus-data and register accessors and keeps `XHCI_PIRQ_STATE`; host suite
`test_pirq`), executed from `hcd_ctl.c` (`hcdPirqStart` at start, the
raw interrupt resource kept by the parser, the `HalGetBusData` and RCBA
accessors, the qemu flavour's `hcdPirqQemuProbe`) and from `hcd_power.c`
at each return to D0 (`HcdPirqResume`). The record is copied whole into
the HCD region's header (words 20 to 47, version unchanged; `xhci.h`'s
`XHCI_SNAPSHOT_HCD_PIRQ_*`, `test_snap`) by `hcd_door.c`, and
`xhcisnap\xhcisnap.c` names it (`print_pirq`, `-selftest-pirq`).
Task 36.4 adds no file: the bounded BUSY retry is `hcd_enum.c`'s
`hcdCommandBusy`, its look the pure `XhciEnumBusyStep` and its note
`XhciEnumNoteBusy` in `xhci_enum.c` (`test_enum`), its drain
`hcd_ctl.c`'s `HcdCommandDrain` under the backstop's admission
(`hcdDrainAdmitted`), and the qemu flavour's `XhciQemuStartHold` in
`hcd_inj.c` (`HcdInjStartHeld`, `HcdInjStartRelease`). Task 36.11 adds
`xhciqual\route.c` (the routing report's pure half, built with this
tree's `xhci_pirq.c` for its reviewed-pair table; host suite
`xhciqual\test\test_route.c`) and its reader, `xhciqual\pci.c`'s
`route_read`. Task 36.12 adds no file: RESET_PIPE in xHCI 4.6.8 order is
`hcd_cfg.c`'s `hcdCfgReset` (the device's half owed after a refused clear,
`HCD_PIPE.ClearOwed`, ended by `hcdCfgOwedEnd`), its steps and verdict the
pure `XhciPipeResetSteps` and `XhciPipeClearVerdict` in `xhci_pipe.c`
(`test_pipe`), and the qemu flavour's fault 13 in `hcd_inj.c`
(`HcdInjClearHalt`; `xhci_inj.h`, `test_inj`). Task 36.9 adds no file: the
HCD header's two delivery words are `xhci_snap.c`'s (`test_snap`) and
`XHCISNAP`'s decode is `xhcisnap.c`'s `print_intr`. Task 36.10 adds
`xhciqual\verdict.c` (C4's outcome, the C6/C8 gate, the verdict with
QUALIFIED WITH CAVEATS and the exit code, moved out of `report.c`; host
suite `xhciqual\test\test_verdict.c`).
Task 36.21 adds three files: the pure `xhci_xstat.c` / `xhci_xstat.h`
(the transfer-size readings `XHCI_XSTAT`: length, chunk and depth
buckets, the hand-carried 64-bit sums, the interval rule over the
processor's time-stamp counter; host suite `test_snap`), and
`xhci98_xfer.h`, the private transfer-size interface `xhciuas.sys`
includes beside `xhci98_streams.h` (`IOCTL_XHCI98_TELEMETRY`, the class
driver's bounded record). `hcd_io.c` reads the counter (`HcdTscRead`,
`HcdTscStamp`) and stamps a chunk's retirement, publication and the
deferred pass (`hcdChunkPublished`); `hcd_ctl.c` starts the readings
(`hcdXstatStart`, `hcdXstatGrant`); `hcd_urb.c` takes the record
(`hcdTelemetryIoctl`); `hcd_door.c` copies both into the HCD region's
header (words 48 to 201, version unchanged); in `src\uas\`,
`uas_xport.c` counts each command (`uasTeleCount`) and sends the record
from its timer (`uasTeleBuild`, `uasTeleSend`), its buckets
`UasTeleBucket` and `UasTeleDepthBucket` in `uas_iu.c` (`test_uas`).
`xhcisnap\xhcisnap.c` decodes them (`print_xstat`, `-selftest-xstat`).
The guest-side client `test\xfertest\` (`XFERTEST.EXE`, built by its own
`build.cmd`, in no package) moves data in requests of a given size and
times them. Task 36.22 adds no file: `XhciMaxTransferKB` is read in
`hcd_ctl.c` (`hcdReadValues`, the rule `XhciXpolValue` in `xhci_xstat.c`)
and answered through `xhci98_xfer.h`'s `IOCTL_XHCI98_TRANSFER_POLICY` in
`hcd_urb.c` (`hcdPolicyIoctl`); `xhciuas.sys` asks at its start
(`uasAskPolicy`, `uas_fdo.c`; the reply check `UasPolicyValue`,
`uas_iu.c`, `test_uas`) and its descriptor (`uas_pdo.c`) and pipes read
the answer; the qemu flavour's `XhciQemuMapGrant` constrains the grant
(`hcdXstatGrant`, `hcd_ctl.c`).
Task 36.24 adds the pure `xhci_xpipe.c` / `xhci_xpipe.h` (stage 2,
design record 13 section 11.6: the TD planner over a URB's wide list,
eligibility, the promotion gate, the wide mapper's run append, the
register-free check's comparison, the pipelined record's machine and
the `XHCI_XPIPE_STATS` counters; host suite `test_xpipe`, with
`test_td`, `test_xfer`, `test_ring` and `test_snap`). The driver half:
`hcd_dma.c` (`hcdMapWide`, the promotion in `hcdMapExecute`, the check
`hcdWideCheck` and the pool, `HcdDmaWideOpen` / `HcdDmaWideClose`),
`hcd_io.c` (`hcdWideClaim`, `hcdWidePump`, `hcdWideRetired`,
`HcdIoRefillFlush`, `HcdIoWideStop`), `hcd_cfg.c` (the cancel walk's
wider predicate and the doorbell gates), `hcd_ctl.c` (`XhciWideMap`),
`hcd_door.c` and `xhcisnap.c` (`print_xpipe`, `-selftest-xpipe`).
Task 36.25 (record 13 section 11.7) adds no file: the big stream layout
is `xhci_stream.c` (`XhciStreamBigBytes`, `XhciStreamLayoutPlaced`,
`test_stream`), its build and fallback `hcd_cfg.c`
(`hcdCfgStreamsBuild`, `hcdCfgBigWanted`), the one-TD publication
`hcd_io.c` (`hcdWideBigOne`) into the pool's scratch (`hcd_dma.c`), and
the engine's per-request cap and ordered-list path `xhci_xfer.c`
(`XHCI_NORMAL_REQUEST.MaxDataTrbs`, `xhciXferEmitSgList`; `test_xfer`,
`test_td`).
Tasks 36.6 to 36.8 (design record 15) add the pure `xhci_poll.c` /
`xhci_poll.h` (the three values, the delivery and its reason, the
poller's ownership machine and roles, promotion, park and kick, the
watcher's dead-line verdict, the adaptive period, the interval
observation and the line's retirement over register callbacks; host suite
`test_poll`) and the driver half `hcd_poll.c` (the poll timer and its DPC,
`PollLock`, the watcher's look and the move, the retirement under
`KeSynchronizeExecution`, `HcdPollContain`'s PCI Interrupt Disable, the
grant and revocation, the peeks and the doorbell hook `HcdSvcDoorbell`).
`hcd_ctl.c` settles the delivery at start (`hcdSettleDelivery`,
`hcdConnect`, the seam 36.15's MSI joins), and the counters are appended
to `XHCIHC_COUNTERS`.
Task 36.15 (design records 15 section 4.3 and 18) adds MSI on Vista and 7:
the pure `xhci_msi.c` / `xhci_msi.h` (the MSI/MSI-X capability read, the
requirements list's checked walk, class, strip and qemu no-line rewrite,
the filter record, the x86 helper's binding state machine and the `M86`
trace record) and `xhci_msi86.c` / `xhci_msi86.h` (both ends of the x86
helper's channel, compiled into `xhci98.sys`, the helper and `test_msi`);
the driver half `hcd_msi86.c` (the mode cache, the requirements filter,
the helper's look and reference, the routines per architecture, the
counters and the trace); in `hcd_ctl.c` the seam `hcdMsgConnect`, the
message ISR and the capability read; and `src/msi86/` - `msi86_entry.c`,
`xhcimsi.rc`, `sources` and `makefile` - the NT 6 x86 helper
`xhcimsi.sys`, built with WDK 7.1 for x86. Task 38.3 renamed both (below).
Task 38.3 (design records 15 section 7.2 and 18 section 15) adds the
finer system timer while polling and renames the helper. `src/hlpr/` -
`hlpr_entry.c`, `xhcihlpr.rc`, `sources` and `makefile` - replaces
`src/msi86/`: the x86 helper `xhcihlpr.sys`, built with WDK 7.1's `WXP`
x86 environment at subsystem 5.00, which loads on 2000, XP, Vista and 7,
creates `\Device\Xhci98Helper` and answers channel version 2 (IOCTL
`0x0022240B`, magic `'XHLP'`, a 0x34-byte block) with two services
negotiated apart, the MSI pair (Vista and 7 only, looked up through
`MmGetSystemRoutineAddress`) and the timer (`ExSetTimerResolution`,
imported). `xhci_hlpr.c` / `xhci_hlpr.h` replace `xhci_msi86.c` /
`xhci_msi86.h` as the channel's pure half (both ends, compiled into
`xhci98.sys`, the helper and `test_msi`, and included by `test_snap`), with the
binding machine's timer event and the per-start latch; `hcd_msi86.c`
keeps its name and takes the timer routine from the binding
(`HcdHlprTimerRoutine`). New `hcd_tmr.c`: the controller's timer thread
(one per controller, created after a successful start and joined at the
stop after the children's power IRPs are settled), the routine's lookup
per image (the helper on x86, the 32-bit NT 5 thread's own first look;
`MmGetSystemRoutineAddress` on amd64; none on 98 SE and ME), the
`ExSetTimerResolution` hold and release, and the `qemu` flavour's
`XhciQemuTmrDelayMs`. `xhci_poll.c` / `xhci_poll.h` gain the timer's pure
request machine (`XhciPollTmrWant`, `XhciPollTmrStep`, the release
causes) and its phase observer (`XhciPollTmrObserve` and the observation
epoch; host suite `test_poll`); the producers - the ACTIVE edge and the
generation in `hcd_poll.c`, the bar beside each power path's revoke in
`hcd_power.c`, the creation in `hcd_pnp.c`, and the switch's read, the
per-start reset and the stop's bar and join in `hcd_ctl.c` - signal the
thread; `hcd_susp.c` gains the `qemu` flavour's `XhciQemuSuspHoldDx`; the
counters are appended to `XHCIHC_COUNTERS` at words 176 to 198.
Tasks 36.28 to 36.40 (design record 14) add selective suspend: the pure
`xhci_susp.c` / `xhci_susp.h` (the switch, eligibility and capabilities,
the device's link machine, each function's power slot, the wake episode,
the idle notification's batches and claims, and stage B's Function
Suspend, wake arming and SuperSpeed hub port decisions; host suite
`test_susp`) and the driver half `hcd_susp.c` (the enum thread's
executor: the admission gate on a device's pipes, the suspend and resume
sequences at a root port and a hub port, U3 and Function Suspend at
SuperSpeed, the decision pass and the wake evidence). Dispatch's half
(D-IRPs, WAIT_WAKE, the idle notification) is in `hcd_pdo.c` and
`hcd_urb.c`; `XhciSelectiveSuspend` is read in `hcd_ctl.c`; all of it is
inert while the value is not 1. Task 36.45 adds no file: an uncached
descriptor is read from the device through the door (`hcd_door.c`, its
request's checks the pure half in `xhci_hub.c`, `test_hub`; the EP0 path
in `hcd_enum.c`; record 13 section 8.11). Task 36.46 adds no file: a
select honours `bNumEndpoints` and ignores surplus endpoint descriptors
(`xhci_pipe.c`, `test_pipe`, applied in `hcd_cfg.c`; `uas_iu.c`'s
`UasParseConfig` keeps the same boundary). Task 36.47 adds no file: `XHCISNAP`'s "captured on" header
and `-nosysinfo` are `xhcisnap\xhcisnap.c`'s. Task 36.48 adds
`xhciqual\sysinfo.c` (the system description's pure half: the SMBIOS
entry point and table walk with its identifiers never copied, CPUID
text, the printer; host suite `xhciqual\test\test_sysinfo.c`),
`xhciqual\sysprobe.c` (its DOS half: the BIOS area, the DPMI mapping,
E820 and the DOS version) and `xhciqual\sysasm.asm` (CPUID, every
register it touches saved). Task 36.33's fix adds
`xhcisnap\check-counter-names.ps1`, which checks the built `XHCISNAP`'s
counter names against `xhci_counters.h` word by word.
Task 38.8 (design record 13 section 10.13) moves the bus's own ids under
`USB\` and spells their four bases once, as `#define`s in the pure
`xhci_hub.h` (`XHCI_ID_ROOT_HUB`, `XHCI_ID_HUB`, `XHCI_ID_HUB30`,
`XHCI_ID_COMPOSITE`; host suite `test_hub` spells each out), which
`hcd_rh.c`, `xhci_hub.c` and `xhci_func.c` build every answer from and
`scripts\inf-gate\check-inf.ps1` holds the INFs to (`HCD-IDSRC`). It adds
the cleanup tool's tree at the repository root, beside `xhcisnap\`:
`xhcicln\xhcicln.c` (one C89 source, `XHCICLN.EXE` with MSVC 6.0 and
`XHCICL64.EXE` with WDK 7.1 amd64), `xhcicln.rc` and `xhcicln.manifest` (the
elevation request), `build.cmd`, `selftest.cmd` (the `-selftest` that
`build-driver.cmd` runs when the EXE is built) and `README.TXT` (the text
of the published `readme.txt`).
Task 40.5 (roadmap-usb4.md, design record 23) adds the install tool's
tree beside it: `xhciinst\xhciinst.c` (the system half), `inst_core.c` /
`inst_core.h` (the pure half: the INF reader, the models section per
system, the action per device and per service group, the registry value
table and its applicability, the switches and exit codes; host suite
`test_inst`), `xhciinst.rc` and `xhciinst.manifest` (the elevation
request), `build.cmd` (`XHCIINST.EXE` with MSVC 6.0, `XHCIIN64.EXE` with
WDK 7.1 amd64, both also compiling `src\xhci_psw.c` for the driver's own
switchover gate), `selftest.cmd` (the `/selftest`, the switch refusals
and the import-table check `build-driver.cmd` runs, through
`check-imports.ps1`, which reads the PE import directory), `INSTALL.BAT` (staged
at the package root) and `README.TXT` (the published `readme.txt`).
Tasks 41-A.2 to 41-A.5 (roadmap-usb4.md, design record 25) add the USB
and Thunderbolt tree tool beside it: `xhcitr\xhcitr.c` (the system half:
the window, the USB branch through the hub IOCTLs, the Thunderbolt branch
through `xhcicm.sys`'s query device, the eject transaction under the
mutex XHCIINST shares, the notification-area icon), `tr_core.c` /
`tr_core.h` (the pure half: text, the USB answers' bytes, descriptor
decoding, the query's parsing and eject gates, the status record's
texts, the tray's router keys and event diff, the command line, the
report; host suite `test_tr`, which also links `xhciinst\inst_core.c`
for record 24's unit rules and eject texts), `tr_res.h` (the resource
and command numbers), `xhcitr.rc` and `xhcitr.manifest` (the icon
`xhcitr.ico`, the version, the menus, the About dialog; as the invoker,
with Common Controls 6), `build.cmd` (`XHCITR.EXE` with MSVC 6.0,
`XHCITR64.EXE` with WDK 7.1 amd64), `selftest.cmd` (the `/selftest`, the
switch refusals and the import-table check `build-driver.cmd` runs,
through `check-imports.ps1`) and `README.TXT` (the published
`readme.txt`).
Task 38.2 (design record 13 section 6.11) adds the pure `xhci_silent.c` /
`xhci_silent.h` (the Windows 98 SE and ME silent install: the
`XhciSilentInstall` gate's rule, the `SilentClasses` table's admission,
cap and failed-query rule, and the configuration walk and PDO answer;
host suite `test_silent`), read at each start in `hcd_ctl.c`
(`hcdReadSilent`, `hcdTakeSilent`) and applied in `hcd_pdo.c`
(`hcdSilentDecide`, `hcdDeviceCapabilities`); and the switch's user
files `silent.reg`, `silent.vbs`, `silent-on.bat` and `silent-off.bat`.
Since the owner's change of 2026-10-14 `xhci98.inf` and
`xhci98-amd64.inf` write `XhciSilentInstall` at 0 on every install path;
the `SilentClasses` list stays `silent.reg`'s alone.
Task 38.11 (design record 17 section 4.14) adds no file: xHCI 0.96
controllers accepted behind `XhciAccept096`. The pure halves are in
`xhci_caps.c` (`XhciVersionFloor`, `XhciEffectiveCapDword0`,
`XhciTdSizeFormOf`, the counter words `XhciVersionPairWord` and
`XhciVersionPolicyWord`, the floor in `XhciCheckCapDword0` and
`XhciDeriveHcInfo`, and HCCPARAMS2 taken as 0 below 1.0; `test_caps`)
and `xhci_xfer.c` (the request's `TdSizeForm`, the TD Size in KiB, the
Setup TRB's Transfer Type and the isochronous TBC and TLBPC written 0, no
Frame ID below 1.0 in `XhciXferIsoFramePolicy` and
`XhciXferIsoStartPlan`; `test_xfer`, `test_iso`, `test_td`); the value
and the qemu flavour's `XhciQemuHciVersion` are read in `hcd_ctl.c`
(`hcdReadValues`, `hcdVersionCounters`, and the refused start's flush),
the effective version at step 0c in `xhci_init.c`
(`xhciEffectiveCapDword0`), the seven request sites in `hcd_enum.c`,
`hcd_hub.c` and `hcd_io.c`, the three counter words in `xhci_counters.h`
and `hcd_log.c` (`test_snap`), and outside `src\` `xhcisnap\xhcisnap.c`
(`print_hcd_version`, `-selftest-version`) and `xhciqual\report.c` (the
line after HCIVERSION and the HCCPARAMS2 line below 1.0).
Task 38.10 (design record 20, section 13) adds Linux's xHCI controller
quirks: the pure `xhci_quirk.c` / `xhci_quirk.h` (the fourteen values in
record 20 section 6.1's order, each with its name, Linux flag and log
label; the defaults, value 2 offered or read as 1, the normalization;
section 3.1's id lists and the match, VIA vendor-wide for
`XhciResumeReinit`; the effective policy; the per-start state
`XHCI_QUIRK_STATE` in the extension; the packed word the log and counter
words 199 to 212 carry; `XhciMsiAvoid`'s decision and `XhciStreamsOff`'s
HCCPARAMS1; host suite `test\test_quirk.c`, with each executor's vectors
in `test_ring`, `test_iso`, `test_link`, `test_stream`, `test_tol` and
`test_poll`) and the driver half `hcd_quirk.c` (the registry reads at
each start and, for `XhciMsiAvoid`, at AddDevice on NT 6; the PCI
identity read before the delivery is settled and the requirements
filter's cache; the ASMedia flow-control write after each start, recovery
and resume; the AMD bit before each suspend; the shutdown's Intel routing
write and HCRST with the bounded log flush after them; the post-resume
arming of the stuck-link check; and the counter block's copy). The quirks
that act in the core read `XhciQuirkOn` where they act: `xhci_init.c`
(suspend, halt, shutdown reset), `xhci_ring.c` and `xhci_xfer.c` (the
control No Op, the isochronous Success), `xhci_link.c` (the stuck link),
`xhci_stream.c` (the padded block), `xhci_tol.c` (the TI cap, and
1002:7316 under `XhciIntervalCap`) and `xhci_poll.c` (reason 19).
`xhciqual\quirks.c` gains the same rows and flags, host suite
`xhciqual\test\test_quirks.c` (every listed id with its Linux flag and
the driver value it prints, the vendor-wide rows, the neighbours refused,
and the HCIVERSION floor note).
Task 38.10's endpoint-ring fix (design record 20 section 5) adds the pure
`xhci_own.c` / `xhci_own.h` (who holds an endpoint's ring across an
interface switch: each step's decision for SELECT_INTERFACE and a
function's select, the drop-only command's, a completion code as an
outcome, and the one-setting walk; host suite `test_own`, over a
modelled controller and pool), carried out in `hcd_cfg.c` (the held
pipes `hcdCfgRetain` and their proofs, the switch `hcdCfgSwitch*`), with
`XhciPoolAcquireReserved` in `xhci_mem.c` (`test_membuf`), the pool
occupancy copied at `hcd_log.c`'s poll, and the qemu flavour's faults
`18` and `19` in `xhci_inj.h` and `hcd_inj.c`.
Two groups from before Phase 36 that this map did not name (added in
36.20): strict mode, `xhci_strict.c` / `xhci_strict.h` (the xHCI 1.2
command precondition table: would a conforming controller accept this
command in this Slot and EP state; pure, host suite `test_strict`) and
its driver half `hcd_strict.c` (before each command `hcd_enum.c`'s
`hcdCommand` submits, it reads the states from the Output Device Context,
counts and traces a violation and still sends the command; it only
observes), compiled into the debug and `qemu` flavours and none of it
into `release`; and `src\uas\`, the UAS class driver `xhciuas.sys` (task
31-A.2), built by its own `sources` and `makefile`: `uas.h` (the stack
and the transports), `uas_iu.c` / `uas_iu.h` (its pure core: the
information units, LUN encoding, tags, the configuration walk for the
UAS setting, REPORT LUNS, the status fold and the storage ids; host suite
`test_uas`), `uas_mem.c` (every pool, MDL and IRP-memory call, the import
allowlist's one site), `uas_streams.c` / `uas_streams.h` (the one
adapter to the bus's `IOCTL_XHCI98_OPEN_STREAMS` and `CLOSE_STREAMS`),
`uas_entry.c` (`DriverEntry`, `AddDevice`, the dispatch table),
`uas_fdo.c` (the FDO: PnP, the configuration and four pipes, the
transport choice, LUN discovery), `uas_pdo.c` (one PDO per logical unit:
its ids, SRBs and the storage IOCTLs), `uas_xport.c` (the transport
engine: queue, tags, the IUs streamed and streamless, timeouts, task
management, reset recovery), `xhciuas.rc` (the version resource) and the
two INFs, `xhciuas.inf` (32-bit) and `xhciuas-amd64.inf` (staged as
`xhciuas.inf` in the 64-bit package).
