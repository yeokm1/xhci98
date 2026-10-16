# Source Files

> **Since 2026-10-02 `src\` is the successor HCD** (design record 13 section 5.1,
> which has the current map). Deleted that day with the miniport: `xhci_dispatch.c`,
> `xhci_rh.c`, `xhci_slot.c`, `xhci_xfer.c`, `xhci_vhub.c`, `xhci_probe.c` and the
> host suites `test_xfer`, `test_iso`, `test_vhub` and `test_init`. **Since task
> 26-A.2 (2026-10-03)** the kept files are built again - `xhci_init.c`,
> `xhci_cmd.c`, `xhci_evt.c`, `xhci_pci.c`, `xhci_dbg.c` - with their usbport
> service calls replaced by `hcd_svc.h`'s, and `xhci_xfer.c` came back holding only
> the completion-code table, lifted unchanged from `1.2.0.0`, and whole since
> 26-A.4 as the transfer engine. The HCD's own files:
> `hcd.h` (the device extensions), `hcd_entry.c` (DriverEntry, AddDevice,
> dispatch), `hcd_pnp.c` and `hcd_power.c` (the controller FDO's PnP and power),
> `hcd_ctl.c` (start and stop, the interrupt and DPC, the controller thread),
> `hcd_svc.h`/`hcd_svc.c` (the services that replace usbport's, and the
> controller lock), `hcd_dma.c` (the one DMA adapter file), `hcd_dev.c` (the
> device layer the kept files call into) and `hcd_pool.c` (the one pool file;
> not linked until the first allocation, 26-A.4). Batch (b) (26-A.3, 26-A.4)
> added `hcd_rh.c` (the root-hub PDO and FDO), `hcd_enum.c` (the controller
> thread's port service and enumeration executor), `hcd_pdo.c` (device PDOs,
> their ids and lifecycle) and the pure `xhci_enum.c` (the per-port enumeration
> machine, host suite `test_enum`). Batch (c) (26-A.5, 26-A.6) adds `hcd_urb.c`
> (the device PDOs' internal device controls) and the pure `xhci_pipe.c`
> (configuration walk, endpoint parameters, Configure Endpoint plans, setup
> packets, buffer splits and the USBD status table; host suite `test_pipe`). Phase 29 (drafted
> 2026-10-04) adds the pure `xhci_link.c` (a USB3 root port's link state machine and
> 29-A.5's hold and release rules; host suite `test_link`). Phase 30 (drafted
> 2026-10-04) adds the pure `xhci_sshub.c` (the SuperSpeed hub class's decisions:
> the 0x2A descriptor, the SuperSpeed port decision, hot or warm hub-port reset,
> the extended port status and a downstream SuperSpeedPlus rate's speed ID; host
> suite `test_sshub`) and `hcd_sshub.c` (the SuperSpeed half of a USB 3 hub on the
> controller thread, entered from `hcd_hub.c` where `hub->Usb3`).
> Task 31-A.1 adds the pure `xhci_stream.c` / `xhci_stream.h` (bulk streams'
> plan, block layout, Stream Context and command encoders, the event's stream;
> host suite `test_stream`) and `xhci98_streams.h`, the private open-streams
> interface a separate class driver includes; the streams themselves are served
> in `hcd_cfg.c` and reach every stream's pipe through `hcd_io.c`. Task 31-A.3
> adds the pure `xhci_xport.c` / `xhci_xport.h` (the storage transport policy:
> Bulk-Only or UAS for an interface offering either, and the ids that follow;
> host suite `test_xport`), applied in `hcd_pdo.c`. Task 33.2 adds the
> instance-id rules to the pure `xhci_func.c` (`XhciFuncSerialId`,
> `XhciFuncSerialSame`, `XhciFuncInstanceId`; host suite `test_func`), the
> serial read to `hcd_enum.c` (`HcdDeviceReadSerial`) and the duplicate and
> dormant matching to `hcd_pdo.c`. Task 33.4 adds `hcd_hubfdo.c`
> (an external hub's FDO, the third PnP role: each hub the bus serves is a
> devnode under `USB\XHCI98_HUB` or `USB\XHCI98_HUB30` since task 38.8,
> `XHCI98\HUB` or `XHCI98\HUB30` before, design record 13 section 10.11)
> and the hub PDO's pure half in `xhci_hub.c` (its ids, the presented parent,
> node information; host suite `test_hub`). Task 36.43 gives `hcd_hubfdo.c` a
> fourth PnP role, a composite device's FDO over its composite-parent PDO
> (`USB\XHCI98_COMPOSITE`, `XHCI98\COMPOSITE` before task 38.8, design record
> 13 section 10.12), with the parent's ids
> in the pure `xhci_func.c` (`XhciFuncParentId`, host suite `test_func`) and
> the parent, its functions and their in-place re-presentation in
> `hcd_pdo.c`. Task 38.12 has that FDO select the configuration down its
> own stack at its start, and the parent PDO answer it and keep the
> caller's descriptor as the record a function's select takes its
> `bInterval` from (`hcd_urb.c`, its last block; the pure precedence and
> checks in `xhci_pipe.c`, host suite `test_pipe`; design record 13
> section 10.14). Task 34.3 adds the pure
> `xhci_psw.c` / `xhci_psw.h` (the Intel EHCI-to-xHCI port switchover: the
> device-id gate, the board exemption, the `XhciIntelPortSwitch` rule, and the
> route and release sequences over two caller-supplied configuration-space
> accessors; host suite `test_psw`), executed from `hcd_ctl.c` at start and
> stop and from `hcd_power.c` at each return to D0 and a shutdown's D3
> ([design record 16](design/16-intel-port-switchover.md)). Task 35-T adds the
> pure `xhci_tol.c` / `xhci_tol.h` (controller tolerance: the decisions of
> each tolerant behaviour, the `XhciTolerance`, `XhciIntervalCap` and
> `XhciAvgTrbEsit` rules, the tolerance clock's tick arithmetic, the budgets
> and intervals as named constants, and the counters and state kept in the
> extension; host suite `test_tol`), its clock run from `hcd_svc.c` and its
> values read in `hcd_ctl.c`
> ([design record 17](design/17-controller-tolerance.md)). Task 35.3 adds the
> pure `xhci_snap.c` (the layout of the snapshot's HCD region - each root
> port's enumeration state and the counter block, word by word; host suite
> `test_snap`), filled by `hcd_door.c`, and a root port's enumeration notes to
> `xhci_enum.c` (their packing and budget; host suite `test_enum`), written
> from `hcd_enum.c`. Phase 35 adds no other file to `src\` but the qemu
> flavour's fault injection below; what it changes in existing ones: task 35.1 changes the speed decoding in `xhci_caps.c`
> (one rule, `xhciProtocolRate`, that the class, the rate and the inverse
> lookup ask, the fallback for an ID 4 to 7 a USB 3 table does not list, and
> `XhciPortSpeedSource`; the hub child's lookup in `xhci_sshub.c`; host
> vectors in `test_caps`, `test_link` and `test_sshub`, the E460's, P14s
> Gen 1's and B490's tables replayed from their `XHCIQUAL` logs); task 35.5
> moves the switchover's start decision and its per-register write set
> into `xhci_psw.c` (`XhciPswMode`, `XHCI_PSW_LIFE`, the lifetime functions;
> `test_psw`), which `hcd_ctl.c` and `hcd_power.c` call; and task 35-T's
> behaviours are executed in the existing HCD files (`hcd_ctl.c` the
> backstop, the HCH and all-ones steps and the values read at start,
> `xhci_cmd.c` and `xhci_init.c` the HCH request and the recovery window,
> `hcd_io.c` and `hcd_urb.c` the parked submissions of a contained
> controller, `hcd_enum.c` the location budgets, the root-port PED and
> over-current handling and `HcdTolLocCharge`, `hcd_svc.c` the tolerance
> clock, `hcd_cfg.c` the interval cap and its order with fast polling;
> 35-T.2's soft retry in `xhci_xfer.c` (the interception in
> `XhciXferEvent`, the replay `XhciXferRetryReplay`, the refusal of a
> pointer above 4 GB `XhciXferEventHighRefused`; `test_xfer`), `hcd_dev.c`
> (`HcdDevRetryReplay` and the retry's counts) and `hcd_cfg.c` (the thread's
> `HcdCfgRetryService`, `hcdCfgRetryOne` and `hcdCfgRetryResetEndpoint`, and
> the quiesce's settle); 35-T.3/4's device cycle in `hcd_dev.c` (the
> producer `hcdTolCycleEvent`), `hcd_enum.c` (`HcdTolCycleMark`,
> `hcdCycleResolve`, `hcdCycleService`, the pre-PDO `hcdCycleAfter` and the
> thread's own wait `hcdThreadControlQuiet`) and `xhci_enum.c` (the
> `XHCI_ENUM_EV_ABANDONED` outcome; `test_enum`), its decisions in
> `xhci_tol.c` (`test_tol`); and 35-T.8's counters in `hcd_dev.c`
> (`hcdTolCountEvent`, `hcdTolSumQueue`), `xhci_snap.c` (the HCD region's
> header grown to 20 words; `test_snap`), `hcd_door.c` (the terminal reason
> under the lock, from `xhci_tol.c`'s `XhciTolTerminal`) and `hcd_log.c` (the
> `tol.*` counter block at a stop). Outside `src\`,
> `xhciqual\mmiodiag.c` prints each protocol's PSI table and the raw
> extended-capability chain (host suite `xhciqual\test\test_mmiodiag.c`),
> `xhciqual\quirks.c` gains the report-only AMD rows, and
> `xhcisnap\xhcisnap.c` decodes the enumeration notes and the HCD region,
> and since 35-T.8 the tolerance state (`print_tol`, `-selftest-tol`).
> Task 35-T.9 adds the qemu flavour's fault injection,
> compiled into that flavour alone (`XHCI_FLAVOUR_QEMU`) and into neither
> image that ships: the pure `xhci_inj.c` / `xhci_inj.h` (the
> `XhciQemuInject` trigger's encoding and sequence rule, the ports a fault
> may be aimed at, and every register answer the layer gives; host suite
> `test_inj`) and the driver half `hcd_inj.c` (the thread's trigger read
> and executors, the ISR's lost-interrupt window, and the PORTSC hooks in
> `xhci_pci.c`), and its second part's soft-retry and device-cycle faults
> with their hooks in the drain (`xhci_evt.c`), the context reads, the
> command path, the doorbells and the service step (`hcd_cfg.c`,
> `hcd_ctl.c`, `hcd_enum.c`, `hcd_hub.c`); task 35.4's `XhciQemuPsiE460` override is
> `XhciPortMapOverridePsi` in `xhci_caps.c`, read in `hcd_ctl.c`
> ([design record 17](design/17-controller-tolerance.md) section 5;
> `build-and-test.md`, "The qemu flavour's test aids"). The final review of
> the branch adds, in the same files: the slot-fatal teardown
> (`hcd_dev.c`'s `hcdSlotFatalMark`, `hcd_enum.c`'s `hcdSlotFatalService`,
> the pure `XhciXferSlotFatal` in `xhci_xfer.c`; `test_xfer`); the
> unproven invalidation's proof and containment (`SlotsUnproven`,
> `HcdCtlProveDmaStopped` in `hcd_ctl.c`, the branch in `hcd_enum.c`'s
> enumeration service) and `hcd_ctl.c`'s `hcdRestoreBusMaster`; the
> configuration's charged fallback `hcdCfgCycle` in `hcd_cfg.c`; and in
> `xhci_tol.c` `XhciTolLocActive`, `XhciTolLocHeld`, the once-per-cycle
> charge `XhciTolCycleCharge` and the terminal reason 5 (`test_tol`).
> **Withdrawn 2026-10-07 with `2.2.0.1`; the code below is reverted and
> `xhci98_report.h` removed (issue 12 section 3.2).**
> Phase 36 (issue 12) adds one file, `xhci98_report.h`, the private
> client-report interface `xhciuas.sys` includes beside `xhci98_streams.h`,
> answered in `hcd_urb.c` (`hcdClientReport`) and kept for the snapshot by
> the pure `XhciSnapClientPick` and `XhciSnapClientStore` in `xhci_snap.c`
> (the HCD region's header grown to 23 words; `test_snap`), filled by
> `hcd_door.c`. Task 36w.1 adds `HcdQueueDrain` to `hcd_ctl.c` (the drain at
> interrupt enable, and the backstop's and the injector's queueing) and the
> busy engine's wait to `hcd_enum.c`'s `hcdCommand`, its slices the pure
> `XhciEnumBusyWait` in `xhci_enum.c` (`test_enum`); task 36w.2 the counter
> block's six appended counters (`xhci_counters.h`), the client port resets
> counted in `hcd_cfg.c`, and in `src\uas\` the report itself (`UasReport`
> in `uas_fdo.c`, the transfer failures in `uas_xport.c`, the sense word
> `UasSenseWord` in `uas_iu.c`; `test_uas`); task 36w.3 the qemu flavour's
> `XhciQemuStartHold` in `hcd_inj.c`. `xhcisnap\xhcisnap.c` names the new
> counters and decodes the reports (`print_hcd_clients`).
> **Reverted 2026-10-08 (owner): `hcd_diag.c` is removed, and the code below,
> `XHCISNAP`'s `print_diag` included, is restored to `2.2.0.0`'s (issue 12
> section 3.3).**
> The diagnostic build `2.2.0.2` (task 36.1, issue 12 section 3.3) adds one
> file, `hcd_diag.c`: the recovery timeline (each slow request from
> `HcdCfgQueue` to its completion in `HcdCfgService`, with the
> CLEAR_FEATURE outcome and the commands `hcd_enum.c`'s `hcdCommand` issued
> for it; each transfer cancel routine's run in `hcd_io.c`; the thread's
> cancel, drain and removal aborts in `hcd_cfg.c`) and the backstop samples
> `hcd_ctl.c`'s `hcdBackstop` takes, with the ISR's, DPC's and thread's
> stamps beside them. `hcd_door.c` reads them, and each open pipe's oldest
> transfer and Output Endpoint Context, into four parts appended to the HCD
> region (the header grown to 31 words, version unchanged); the layout and
> the pure `XhciSnapMs`, `XhciSnapOldest`, `XhciSnapRingNext`,
> `XhciSnapRingFind`, `XhciSnapRecCommand`, `XhciSnapIrqClass`,
> `XhciSnapPipeClass`, `XhciSnapPipeBefore` and `XhciSnapClearOf` are
> `xhci_snap.c`'s (`test_snap`). `hcd_urb.c` and `hcd_io.c` keep each
> transfer's URB function, flags and claim and publish ticks for it;
> `xhcisnap\xhcisnap.c` decodes all of it (`print_diag`). It records and
> decides nothing.
> Task 36.3 (issue 12 section 5.1) adds the pure `xhci_pirq.c` /
> `xhci_pirq.h` (the Intel PCH interrupt routing quirk: the
> `XhciIntelIrqRoute` rule, the six reviewed xHCI/LPC pairs and the verified
> E7250, the PIRQ that delivers the assigned IRQ in APIC and PIC mode, and
> `XhciPirqRun`, which checks in order over caller-supplied configuration,
> bus-data and register accessors and keeps `XHCI_PIRQ_STATE`; host suite
> `test_pirq`), executed from `hcd_ctl.c` (`hcdPirqStart` at start, the
> raw interrupt resource kept by the parser, the `HalGetBusData` and RCBA
> accessors, the qemu flavour's `hcdPirqQemuProbe`) and from `hcd_power.c`
> at each return to D0 (`HcdPirqResume`). The record is copied whole into
> the HCD region's header (words 20 to 47, version unchanged; `xhci.h`'s
> `XHCI_SNAPSHOT_HCD_PIRQ_*`, `test_snap`) by `hcd_door.c`, and
> `xhcisnap\xhcisnap.c` names it (`print_pirq`, `-selftest-pirq`).
> Task 36.4 adds no file: the bounded BUSY retry is `hcd_enum.c`'s
> `hcdCommandBusy`, its look the pure `XhciEnumBusyStep` and its note
> `XhciEnumNoteBusy` in `xhci_enum.c` (`test_enum`), its drain
> `hcd_ctl.c`'s `HcdCommandDrain` under the backstop's admission
> (`hcdDrainAdmitted`), and the qemu flavour's `XhciQemuStartHold` in
> `hcd_inj.c` (`HcdInjStartHeld`, `HcdInjStartRelease`). Task 36.11 adds
> `xhciqual\route.c` (the routing report's pure half, built with this
> tree's `xhci_pirq.c` for its reviewed-pair table; host suite
> `xhciqual\test\test_route.c`) and its reader, `xhciqual\pci.c`'s
> `route_read`. Task 36.12 adds no file: RESET_PIPE in xHCI 4.6.8 order is
> `hcd_cfg.c`'s `hcdCfgReset` (the device's half owed after a refused clear,
> `HCD_PIPE.ClearOwed`, ended by `hcdCfgOwedEnd`), its steps and verdict the
> pure `XhciPipeResetSteps` and `XhciPipeClearVerdict` in `xhci_pipe.c`
> (`test_pipe`), and the qemu flavour's fault 13 in `hcd_inj.c`
> (`HcdInjClearHalt`; `xhci_inj.h`, `test_inj`). Task 36.9 adds no file: the
> HCD header's two delivery words are `xhci_snap.c`'s (`test_snap`) and
> `XHCISNAP`'s decode is `xhcisnap.c`'s `print_intr`. Task 36.10 adds
> `xhciqual\verdict.c` (C4's outcome, the C6/C8 gate, the verdict with
> QUALIFIED WITH CAVEATS and the exit code, moved out of `report.c`; host
> suite `xhciqual\test\test_verdict.c`).
> Task 36.21 adds three files: the pure `xhci_xstat.c` / `xhci_xstat.h`
> (the transfer-size readings `XHCI_XSTAT`: length, chunk and depth
> buckets, the hand-carried 64-bit sums, the interval rule over the
> processor's time-stamp counter; host suite `test_snap`), and
> `xhci98_xfer.h`, the private transfer-size interface `xhciuas.sys`
> includes beside `xhci98_streams.h` (`IOCTL_XHCI98_TELEMETRY`, the class
> driver's bounded record). `hcd_io.c` reads the counter (`HcdTscRead`,
> `HcdTscStamp`) and stamps a chunk's retirement, publication and the
> deferred pass (`hcdChunkPublished`); `hcd_ctl.c` starts the readings
> (`hcdXstatStart`, `hcdXstatGrant`); `hcd_urb.c` takes the record
> (`hcdTelemetryIoctl`); `hcd_door.c` copies both into the HCD region's
> header (words 48 to 201, version unchanged); in `src\uas\`,
> `uas_xport.c` counts each command (`uasTeleCount`) and sends the record
> from its timer (`uasTeleBuild`, `uasTeleSend`), its buckets
> `UasTeleBucket` and `UasTeleDepthBucket` in `uas_iu.c` (`test_uas`).
> `xhcisnap\xhcisnap.c` decodes them (`print_xstat`, `-selftest-xstat`).
> The guest-side client `test\xfertest\` (`XFERTEST.EXE`, built by its own
> `build.cmd`, in no package) moves data in requests of a given size and
> times them. Task 36.22 adds no file: `XhciMaxTransferKB` is read in
> `hcd_ctl.c` (`hcdReadValues`, the rule `XhciXpolValue` in `xhci_xstat.c`)
> and answered through `xhci98_xfer.h`'s `IOCTL_XHCI98_TRANSFER_POLICY` in
> `hcd_urb.c` (`hcdPolicyIoctl`); `xhciuas.sys` asks at its start
> (`uasAskPolicy`, `uas_fdo.c`; the reply check `UasPolicyValue`,
> `uas_iu.c`, `test_uas`) and its descriptor (`uas_pdo.c`) and pipes read
> the answer; the qemu flavour's `XhciQemuMapGrant` constrains the grant
> (`hcdXstatGrant`, `hcd_ctl.c`).
> Task 36.24 adds the pure `xhci_xpipe.c` / `xhci_xpipe.h` (stage 2,
> design record 13 section 11.6: the TD planner over a URB's wide list,
> eligibility, the promotion gate, the wide mapper's run append, the
> register-free check's comparison, the pipelined record's machine and
> the `XHCI_XPIPE_STATS` counters; host suite `test_xpipe`, with
> `test_td`, `test_xfer`, `test_ring` and `test_snap`). The driver half:
> `hcd_dma.c` (`hcdMapWide`, the promotion in `hcdMapExecute`, the check
> `hcdWideCheck` and the pool, `HcdDmaWideOpen` / `HcdDmaWideClose`),
> `hcd_io.c` (`hcdWideClaim`, `hcdWidePump`, `hcdWideRetired`,
> `HcdIoRefillFlush`, `HcdIoWideStop`), `hcd_cfg.c` (the cancel walk's
> wider predicate and the doorbell gates), `hcd_ctl.c` (`XhciWideMap`),
> `hcd_door.c` and `xhcisnap.c` (`print_xpipe`, `-selftest-xpipe`).
> Task 36.25 (record 13 section 11.7) adds no file: the big stream layout
> is `xhci_stream.c` (`XhciStreamBigBytes`, `XhciStreamLayoutPlaced`,
> `test_stream`), its build and fallback `hcd_cfg.c`
> (`hcdCfgStreamsBuild`, `hcdCfgBigWanted`), the one-TD publication
> `hcd_io.c` (`hcdWideBigOne`) into the pool's scratch (`hcd_dma.c`), and
> the engine's per-request cap and ordered-list path `xhci_xfer.c`
> (`XHCI_NORMAL_REQUEST.MaxDataTrbs`, `xhciXferEmitSgList`; `test_xfer`,
> `test_td`).
> Tasks 36.6 to 36.8 (design record 15) add the pure `xhci_poll.c` /
> `xhci_poll.h` (the three values, the delivery and its reason, the
> poller's ownership machine and roles, promotion, park and kick, the
> watcher's dead-line verdict, the adaptive period, the interval
> observation and the line's retirement over register callbacks; host suite
> `test_poll`) and the driver half `hcd_poll.c` (the poll timer and its DPC,
> `PollLock`, the watcher's look and the move, the retirement under
> `KeSynchronizeExecution`, `HcdPollContain`'s PCI Interrupt Disable, the
> grant and revocation, the peeks and the doorbell hook `HcdSvcDoorbell`).
> `hcd_ctl.c` settles the delivery at start (`hcdSettleDelivery`,
> `hcdConnect`, the seam 36.15's MSI joins), and the counters are appended
> to `XHCIHC_COUNTERS`.
> Task 36.15 (design records 15 section 4.3 and 18) adds MSI on Vista and 7:
> the pure `xhci_msi.c` / `xhci_msi.h` (the MSI/MSI-X capability read, the
> requirements list's checked walk, class, strip and qemu no-line rewrite,
> the filter record, the x86 helper's binding state machine and the `M86`
> trace record) and `xhci_msi86.c` / `xhci_msi86.h` (both ends of the x86
> helper's channel, compiled into `xhci98.sys`, the helper and `test_msi`);
> the driver half `hcd_msi86.c` (the mode cache, the requirements filter,
> the helper's look and reference, the routines per architecture, the
> counters and the trace); in `hcd_ctl.c` the seam `hcdMsgConnect`, the
> message ISR and the capability read; and `src/msi86/` - `msi86_entry.c`,
> `xhcimsi.rc`, `sources` and `makefile` - the NT 6 x86 helper
> `xhcimsi.sys`, built with WDK 7.1 for x86. Task 38.3 renamed both (below).
> Task 38.3 (design records 15 section 7.2 and 18 section 15) adds the
> finer system timer while polling and renames the helper. `src/hlpr/` -
> `hlpr_entry.c`, `xhcihlpr.rc`, `sources` and `makefile` - replaces
> `src/msi86/`: the x86 helper `xhcihlpr.sys`, built with WDK 7.1's `WXP`
> x86 environment at subsystem 5.00, which loads on 2000, XP, Vista and 7,
> creates `\Device\Xhci98Helper` and answers channel version 2 (IOCTL
> `0x0022240B`, magic `'XHLP'`, a 0x34-byte block) with two services
> negotiated apart, the MSI pair (Vista and 7 only, looked up through
> `MmGetSystemRoutineAddress`) and the timer (`ExSetTimerResolution`,
> imported). `xhci_hlpr.c` / `xhci_hlpr.h` replace `xhci_msi86.c` /
> `xhci_msi86.h` as the channel's pure half (both ends, compiled into
> `xhci98.sys`, the helper and `test_msi`, and included by `test_snap`), with the
> binding machine's timer event and the per-start latch; `hcd_msi86.c`
> keeps its name and takes the timer routine from the binding
> (`HcdHlprTimerRoutine`). New `hcd_tmr.c`: the controller's timer thread
> (one per controller, created after a successful start and joined at the
> stop after the children's power IRPs are settled), the routine's lookup
> per image (the helper on x86, the 32-bit NT 5 thread's own first look;
> `MmGetSystemRoutineAddress` on amd64; none on 98 SE and ME), the
> `ExSetTimerResolution` hold and release, and the `qemu` flavour's
> `XhciQemuTmrDelayMs`. `xhci_poll.c` / `xhci_poll.h` gain the timer's pure
> request machine (`XhciPollTmrWant`, `XhciPollTmrStep`, the release
> causes) and its phase observer (`XhciPollTmrObserve` and the observation
> epoch; host suite `test_poll`); the producers - the ACTIVE edge and the
> generation in `hcd_poll.c`, the bar beside each power path's revoke in
> `hcd_power.c`, the creation in `hcd_pnp.c`, and the switch's read, the
> per-start reset and the stop's bar and join in `hcd_ctl.c` - signal the
> thread; `hcd_susp.c` gains the `qemu` flavour's `XhciQemuSuspHoldDx`; the
> counters are appended to `XHCIHC_COUNTERS` at words 176 to 198.
> Tasks 36.28 to 36.40 (design record 14) add selective suspend: the pure
> `xhci_susp.c` / `xhci_susp.h` (the switch, eligibility and capabilities,
> the device's link machine, each function's power slot, the wake episode,
> the idle notification's batches and claims, and stage B's Function
> Suspend, wake arming and SuperSpeed hub port decisions; host suite
> `test_susp`) and the driver half `hcd_susp.c` (the enum thread's
> executor: the admission gate on a device's pipes, the suspend and resume
> sequences at a root port and a hub port, U3 and Function Suspend at
> SuperSpeed, the decision pass and the wake evidence). Dispatch's half
> (D-IRPs, WAIT_WAKE, the idle notification) is in `hcd_pdo.c` and
> `hcd_urb.c`; `XhciSelectiveSuspend` is read in `hcd_ctl.c`; all of it is
> inert while the value is not 1. Task 36.45 adds no file: an uncached
> descriptor is read from the device through the door (`hcd_door.c`, its
> request's checks the pure half in `xhci_hub.c`, `test_hub`; the EP0 path
> in `hcd_enum.c`; record 13 section 8.11). Task 36.46 adds no file: a
> select honours `bNumEndpoints` and ignores surplus endpoint descriptors
> (`xhci_pipe.c`, `test_pipe`, applied in `hcd_cfg.c`; `uas_iu.c`'s
> `UasParseConfig` keeps the same boundary). Task 36.47 adds no file: `XHCISNAP`'s "captured on" header
> and `-nosysinfo` are `xhcisnap\xhcisnap.c`'s. Task 36.48 adds
> `xhciqual\sysinfo.c` (the system description's pure half: the SMBIOS
> entry point and table walk with its identifiers never copied, CPUID
> text, the printer; host suite `xhciqual\test\test_sysinfo.c`),
> `xhciqual\sysprobe.c` (its DOS half: the BIOS area, the DPMI mapping,
> E820 and the DOS version) and `xhciqual\sysasm.asm` (CPUID, every
> register it touches saved). Task 36.33's fix adds
> `xhcisnap\check-counter-names.ps1`, which checks the built `XHCISNAP`'s
> counter names against `xhci_counters.h` word by word.
> Task 38.8 (design record 13 section 10.13) moves the bus's own ids under
> `USB\` and spells their four bases once, as `#define`s in the pure
> `xhci_hub.h` (`XHCI_ID_ROOT_HUB`, `XHCI_ID_HUB`, `XHCI_ID_HUB30`,
> `XHCI_ID_COMPOSITE`; host suite `test_hub` spells each out), which
> `hcd_rh.c`, `xhci_hub.c` and `xhci_func.c` build every answer from and
> `scripts\inf-gate\check-inf.ps1` holds the INFs to (`HCD-IDSRC`). It adds
> the cleanup tool's tree at the repository root, beside `xhcisnap\`:
> `xhcicln\xhcicln.c` (one C89 source, `XHCICLN.EXE` with MSVC 6.0 and
> `XHCICL64.EXE` with WDK 7.1 amd64), `xhcicln.rc` and `xhcicln.manifest` (the
> elevation request), `build.cmd`, `selftest.cmd` (the `-selftest` that
> `build-driver.cmd` runs when the EXE is built) and `README.TXT` (the text
> of the published `readme.txt`).
> Task 40.6 (roadmap-usb4.md, design record 23) adds the install tool's
> tree beside it: `xhciinst\xhciinst.c` (the system half), `inst_core.c` /
> `inst_core.h` (the pure half: the INF reader, the models section per
> system, the action per device and per service group, the registry value
> table and its applicability, the switches and exit codes; host suite
> `test_inst`), `xhciinst.rc` and `xhciinst.manifest` (the elevation
> request), `build.cmd` (`XHCIINST.EXE` with MSVC 6.0, `XHCIIN64.EXE` with
> WDK 7.1 amd64, both also compiling `src\xhci_psw.c` for the driver's own
> switchover gate), `selftest.cmd` (the `/selftest`, the switch refusals
> and the import-table check `build-driver.cmd` runs, through
> `check-imports.ps1`, which reads the PE import directory), `INSTALL.BAT` (staged
> at the package root) and `README.TXT` (the published `readme.txt`).
> Tasks 41-A.2 to 41-A.5 (roadmap-usb4.md, design record 25) add the USB
> and Thunderbolt tree tool beside it: `xhcitr\xhcitr.c` (the system half:
> the window, the USB branch through the hub IOCTLs, the Thunderbolt branch
> through `xhcicm.sys`'s query device, the eject transaction under the
> mutex XHCIINST shares, the notification-area icon), `tr_core.c` /
> `tr_core.h` (the pure half: text, the USB answers' bytes, descriptor
> decoding, the query's parsing and eject gates, the status record's
> texts, the tray's router keys and event diff, the command line, the
> report; host suite `test_tr`, which also links `xhciinst\inst_core.c`
> for record 24's unit rules and eject texts), `tr_res.h` (the resource
> and command numbers), `xhcitr.rc` and `xhcitr.manifest` (the icon
> `xhcitr.ico`, the version, the menus, the About dialog; as the invoker,
> with Common Controls 6), `build.cmd` (`XHCITR.EXE` with MSVC 6.0,
> `XHCITR64.EXE` with WDK 7.1 amd64), `selftest.cmd` (the `/selftest`, the
> switch refusals and the import-table check `build-driver.cmd` runs,
> through `check-imports.ps1`) and `README.TXT` (the published
> `readme.txt`).
> Task 38.2 (design record 13 section 6.11) adds the pure `xhci_silent.c` /
> `xhci_silent.h` (the Windows 98 SE and ME silent install: the
> `XhciSilentInstall` gate's rule, the `SilentClasses` table's admission,
> cap and failed-query rule, and the configuration walk and PDO answer;
> host suite `test_silent`), read at each start in `hcd_ctl.c`
> (`hcdReadSilent`, `hcdTakeSilent`) and applied in `hcd_pdo.c`
> (`hcdSilentDecide`, `hcdDeviceCapabilities`); and the switch's user
> files `silent.reg`, `silent.vbs`, `silent-on.bat` and `silent-off.bat`.
> Since the owner's change of 2026-10-14 `xhci98.inf` and
> `xhci98-amd64.inf` write `XhciSilentInstall` at 0 on every install path;
> the `SilentClasses` list stays `silent.reg`'s alone.
> Task 38.11 (design record 17 section 4.14) adds no file: xHCI 0.96
> controllers accepted behind `XhciAccept096`. The pure halves are in
> `xhci_caps.c` (`XhciVersionFloor`, `XhciEffectiveCapDword0`,
> `XhciTdSizeFormOf`, the counter words `XhciVersionPairWord` and
> `XhciVersionPolicyWord`, the floor in `XhciCheckCapDword0` and
> `XhciDeriveHcInfo`, and HCCPARAMS2 taken as 0 below 1.0; `test_caps`)
> and `xhci_xfer.c` (the request's `TdSizeForm`, the TD Size in KiB, the
> Setup TRB's Transfer Type and the isochronous TBC and TLBPC written 0, no
> Frame ID below 1.0 in `XhciXferIsoFramePolicy` and
> `XhciXferIsoStartPlan`; `test_xfer`, `test_iso`, `test_td`); the value
> and the qemu flavour's `XhciQemuHciVersion` are read in `hcd_ctl.c`
> (`hcdReadValues`, `hcdVersionCounters`, and the refused start's flush),
> the effective version at step 0c in `xhci_init.c`
> (`xhciEffectiveCapDword0`), the seven request sites in `hcd_enum.c`,
> `hcd_hub.c` and `hcd_io.c`, the three counter words in `xhci_counters.h`
> and `hcd_log.c` (`test_snap`), and outside `src\` `xhcisnap\xhcisnap.c`
> (`print_hcd_version`, `-selftest-version`) and `xhciqual\report.c` (the
> line after HCIVERSION and the HCCPARAMS2 line below 1.0).
> Task 38.10 (design record 20, section 13) adds Linux's xHCI controller
> quirks: the pure `xhci_quirk.c` / `xhci_quirk.h` (the fourteen values in
> record 20 section 6.1's order, each with its name, Linux flag and log
> label; the defaults, value 2 offered or read as 1, the normalization;
> section 3.1's id lists and the match, VIA vendor-wide for
> `XhciResumeReinit`; the effective policy; the per-start state
> `XHCI_QUIRK_STATE` in the extension; the packed word the log and counter
> words 199 to 212 carry; `XhciMsiAvoid`'s decision and `XhciStreamsOff`'s
> HCCPARAMS1; host suite `test\test_quirk.c`, with each executor's vectors
> in `test_ring`, `test_iso`, `test_link`, `test_stream`, `test_tol` and
> `test_poll`) and the driver half `hcd_quirk.c` (the registry reads at
> each start and, for `XhciMsiAvoid`, at AddDevice on NT 6; the PCI
> identity read before the delivery is settled and the requirements
> filter's cache; the ASMedia flow-control write after each start, recovery
> and resume; the AMD bit before each suspend; the shutdown's Intel routing
> write and HCRST with the bounded log flush after them; the post-resume
> arming of the stuck-link check; and the counter block's copy). The quirks
> that act in the core read `XhciQuirkOn` where they act: `xhci_init.c`
> (suspend, halt, shutdown reset), `xhci_ring.c` and `xhci_xfer.c` (the
> control No Op, the isochronous Success), `xhci_link.c` (the stuck link),
> `xhci_stream.c` (the padded block), `xhci_tol.c` (the TI cap, and
> 1002:7316 under `XhciIntervalCap`) and `xhci_poll.c` (reason 19).
> `xhciqual\quirks.c` gains the same rows and flags, host suite
> `xhciqual\test\test_quirks.c` (every listed id with its Linux flag and
> the driver value it prints, the vendor-wide rows, the neighbours refused,
> and the HCIVERSION floor note).
> Task 38.10's endpoint-ring fix (design record 20 section 5) adds the pure
> `xhci_own.c` / `xhci_own.h` (who holds an endpoint's ring across an
> interface switch: each step's decision for SELECT_INTERFACE and a
> function's select, the drop-only command's, a completion code as an
> outcome, and the one-setting walk; host suite `test_own`, over a
> modelled controller and pool), carried out in `hcd_cfg.c` (the held
> pipes `hcdCfgRetain` and their proofs, the switch `hcdCfgSwitch*`), with
> `XhciPoolAcquireReserved` in `xhci_mem.c` (`test_membuf`), the pool
> occupancy copied at `hcd_log.c`'s poll, and the qemu flavour's faults
> `18` and `19` in `xhci_inj.h` and `hcd_inj.c`.
> Two groups from before Phase 36 that this map did not name (added in
> 36.20): strict mode, `xhci_strict.c` / `xhci_strict.h` (the xHCI 1.2
> command precondition table: would a conforming controller accept this
> command in this Slot and EP state; pure, host suite `test_strict`) and
> its driver half `hcd_strict.c` (before each command `hcd_enum.c`'s
> `hcdCommand` submits, it reads the states from the Output Device Context,
> counts and traces a violation and still sends the command; it only
> observes), compiled into the debug and `qemu` flavours and none of it
> into `release`; and `src\uas\`, the UAS class driver `xhciuas.sys` (task
> 31-A.2), built by its own `sources` and `makefile`: `uas.h` (the stack
> and the transports), `uas_iu.c` / `uas_iu.h` (its pure core: the
> information units, LUN encoding, tags, the configuration walk for the
> UAS setting, REPORT LUNS, the status fold and the storage ids; host suite
> `test_uas`), `uas_mem.c` (every pool, MDL and IRP-memory call, the import
> allowlist's one site), `uas_streams.c` / `uas_streams.h` (the one
> adapter to the bus's `IOCTL_XHCI98_OPEN_STREAMS` and `CLOSE_STREAMS`),
> `uas_entry.c` (`DriverEntry`, `AddDevice`, the dispatch table),
> `uas_fdo.c` (the FDO: PnP, the configuration and four pipes, the
> transport choice, LUN discovery), `uas_pdo.c` (one PDO per logical unit:
> its ids, SRBs and the storage IOCTLs), `uas_xport.c` (the transport
> engine: queue, tags, the IUs streamed and streamless, timeouts, task
> management, reset recovery), `xhciuas.rc` (the version resource) and the
> two INFs, `xhciuas.inf` (32-bit) and `xhciuas-amd64.inf` (staged as
> `xhciuas.inf` in the 64-bit package). The tables below are the
> miniport's map as of `1.2.0.0` and are rewritten in Phase 26. Comments in the
> kept files still speak of usbport as the design argument for each step;
> `src\hcd_svc.h`, "READING THE KEPT FILES", is the key from each usbport name
> to the HCD code that now plays the part, and a comment that names a deleted
> file (`xhci_dispatch.c`, `xhci_slot.c`, `test_init.c` and the rest) describes
> the miniport on branch `1.2.0.0`.

What every file in `src/` is for, in one table per kind. The authoritative description of each file is the comment block at its head; this page is the map, and it says which files the host test suite compiles (the "pure core" of [design record 03](design/03-host-unit-tests.md)) and which need the DDK. Line counts are omitted on purpose - they drift, the roles do not.

The rule that organises the whole directory: files in the pure core **decide** (they encode TRBs, compute layouts, classify ports, run state machines over caller-supplied memory) and touch no register, no DDK service and no IRQL; everything else is where those decisions become bus cycles and usbport callbacks. `src/sources` names the pure list and the reason each file is on or off it.

## Driver sources - the pure core

Each builds twice: into `xhci98.sys`, and on the build host with MSVC 6.0 under `XHCI_HOST_TEST` (`test\run-host-tests.cmd`). A file that needs `ntddk.h` does not belong here.

**"Pure core" is about what a file may DEPEND on, not about what the host suite compiles**, and the two are no longer the same set. `test_init.c` compiles every DDK-side file below under `XHCI_HOST_TEST` against a model controller and a stand-in registration packet, so `xhci_dispatch.c`, `xhci_init.c`, `xhci_slot.c`, `xhci_cmd.c`, `xhci_evt.c`, `xhci_rh.c`, `xhci_pci.c` and `xhci_probe.c` are all host-compiled too - see `test\run-host-tests.cmd`'s `test_init` target for the list it actually passes to the compiler. What the rule still buys is the thing it was written for: a pure-core file can be reasoned about and tested without a controller at all, and nothing in it can reach a register by accident.

| File | Role | Host test |
|---|---|---|
| `xhci_mem.c` | Controller common-buffer layout, computed and checked: the resource size committed in `DriverEntry`, the offsets carved at `StartController`, and the spec's alignment and no-cross-boundary rules, all checked against each other before any of it reaches a controller ([design record 04](design/04-controller-common-buffer.md)). | `test_membuf.c` |
| `xhci_ring.c` | TRB encoding and the ring state machines (command, event and transfer rings, cycle bit, link TRBs, wrap). Rings no doorbell; the one call site that does is in the driver-only layer. | `test_ring.c` |
| `xhci_caps.c` | Extended-capability walk, port classification (which root ports this driver manages, USB 2.0 only) and speed decoding, over a caller-supplied reader so a malformed capability chain is a test rather than a crash. | `test_caps.c` |
| `xhci_port.c` | Constructing `PORTSC` writes that have only the intended effect (the register where a naive read-modify-write disables the port or discards a change bit), and deciding what a port's state means to the USB hub class. | `test_port.c` |
| `xhci_ctx.c` | Slot, Endpoint and Input Control Context encoding. A value that does not fit its field is refused, not masked. | `test_ctx.c` |
| `xhci_xfer.c` | The transfer engine: TD construction from usbport's SG list, the pending-transfer queue, and what a Transfer Event means to one usbport transfer. Speaks the usbport transfer ABI, so it includes `xhci_usbport.h`; still calls no usbport service. | `test_xfer.c`, `test_iso.c` |
| `xhci_desc.c` | The configuration-descriptor snoop (task 9-A.2): recovers an isochronous endpoint's `bInterval`, which usbport does not carry, from reply bytes the caller has already copied out. Decides nothing itself. | `test_desc.c` |
| `xhci_topo.c` | The hub topology graph (task 7b-A.1): reconstructs each device's route string, parent hub and TT from the hub-class traffic `usbhub.sys` sends through `SubmitTransfer`, since usbport hands the miniport no route ([design record 02](design/02-hub-topology-route-string.md)). | `test_topo.c` |
| `xhci_log.c` | The in-memory log ring: a bounded byte ring in the miniport extension, its producer set and per-code budget, and the flush *decision*. No file I/O anywhere; the DDK half (registry switches, IRQL check, `DbgPrint` emission) is in `xhci_dispatch.c`. Compiled into the release build deliberately ([design record 08](design/08-build-flavours-and-the-log-channel.md)). | `test_log.c` |
| `xhci_qry.c` / `xhci_qryb.h` / `xhci_qry.h` | The answer of `xhci98.sys`'s query request, `IOCTL_XHCI98_QUERY` (design record 13 section 8.12): the counter block from level 1, the log ring as text at levels 2 and 3 (withheld at 4), the controller's configuration space at every level, and the sizing answers; pure, host suite `test_xhq`. `hcd_door.c` reads the configuration space and answers the request. |

## Driver sources - the DDK side

These need `ntddk.h` or a usbport service, take the controller lock, or dereference BAR0. None may move into the pure list.

| File | Role |
|---|---|
| `xhci_dispatch.c` | `DriverEntry`, the usbport registration packet, and the whole miniport callback surface - every callback the target's `usbport.sys` build can reach, signature-correct and IRQL-safe, delegating to the files below. Also home to the DDK half of the log ring and its registry switches. |
| `xhci_pci.c` | The only file that touches BAR0 or PCI config space: the MMIO accessors, config-space reads and bounded waits declared in `xhci_hw.h`. Every function is a thin wrapper so "no MMIO outside the wired-up lifecycle" is a checkable property. |
| `xhci_init.c` | The controller initialization sequence (`StartController` onward): BIOS handoff, reset, DCBAA, scratchpad, command and event rings, run, port power - with every interrupt source still masked because usbport calls `EnableInterrupts` the moment start succeeds. Also the in-place recovery path ([design record 07](design/07-controller-recovery-in-place.md)). |
| `xhci_evt.c` | The interrupt path: the miniport ISR body (proves ownership, carries nothing forward) and the event-ring drain that usbport's DPC calls, which hands each event to the command, root-hub, slot or transfer layer. |
| `xhci_cmd.c` | The asynchronous command engine every Enable Slot, Address Device, Configure Endpoint, Reset Endpoint and Set TR Dequeue Pointer goes through: completion matching, software-owned timeouts, stuck-ring recovery. Nothing may wait. Holds the miniport's own interior spin lock ([design record 05](design/05-locking-model.md)). |
| `xhci_rh.c` | The root-hub callback family usbport reaches - port count, port status, port feature set/clear, change notification enable - and the port shadow behind it. Reads and writes `PORTSC` under the controller lock, which is why it is not `xhci_port.c`. |
| `xhci_slot.c` | Devices: slots, the default control endpoint, the command chain from "a port reset finished" to "usbport has an address", `SET_ADDRESS` interception, the usbport-address to Slot ID map, endpoint open/close for every transfer type, and the hub and device positions it asks `xhci_topo.c` for. The largest file in the driver. |
| `xhci_probe.c` | The runtime transfer-contract probe (task 6-V.1): instrumentation that classifies what usbport hands over at the registration-packet surface and leaves counters behind. Nothing in the driver branches on it; it stays here rather than in the pure core because it takes the controller lock. |
| `xhci_dbg.c` | The `qemu` flavour's per-line trace channel. Compiles to nothing without `XHCI_DBG_LIVE`, which `src/sources` defines for `chk_qemu` only, so the port-`0xE9` mirror and its `HAL.dll!WRITE_PORT_UCHAR` import exist in that flavour alone. Both shipping flavours still import `ntoskrnl.exe!DbgPrint`, for one deliberate site: the `XhciLogDebugView` sink in `xhci_dispatch.c`, which emits the bounded log ring from a PASSIVE-level flush when a user switches it on (AGENTS.md, "Coding Style"). |

## Headers

| File | Role | DDK-free |
|---|---|---|
| `xhci.h` | xHCI types, register and TRB bit definitions, the fixed common-buffer layout, and the declarations of the whole pure core. Bit positions are transcribed from [xhci-data-structures.md](../usb-xhci-info/xhci-data-structures.md), never from memory. | yes |
| `xhci_usbport.h` | The `usbport.sys` miniport ABI as this driver declares it - registration packet, callback signatures, transfer and endpoint parameter structures - transcribed from [usbport-miniport-abi.md](../usb-xhci-info/usbport-miniport-abi.md) and confirmed against the shipping binaries. An independent declaration of an interoperability contract, not ReactOS code. | yes (`test_packet.c` compiles it) |
| `xhci_xfer.h` | The transfer engine's interface (`xhci_xfer.c`), kept out of `xhci.h` because it needs `xhci_usbport.h`. | yes |
| `xhci_desc.h` | The descriptor snoop's interface, and the argument for why it must exist. | yes |
| `xhci_topo.h` | The topology graph's types and interface, with the measured wire constants. | yes |
| `xhci_counters.h` | The device matrix's counter block, `XHCIHC_COUNTERS` (roadmap-hcd.md 26-A.10): embedded in the controller FDO's extension, zeroed at every start, and measured by `scripts\vm-matrix\gen-offsets.ps1` under `XHCI_HOST_TEST`, which is why it is a header of its own. | yes |
| `xhci_log.h` | The log ring's contract: the verbosity ladder, the sinks, and the rule that recording is not emission. | yes |
| `xhci_probe.h` | The probe's classification and counters. | no - it takes the lock |
| `xhci_hw.h` | The driver-only side of the split: MMIO accessors, PCI config access, bounded waits. Implemented in `xhci_pci.c`. | no |
| `xhci_dbg.h` | The trace channel's macros; empty outside the `qemu` flavour. | no |
| `xhci_compat.h` | Win98/Win2000 DDK compatibility shims, chiefly undoing the Win2K DDK's `ExAllocatePool` to `ExAllocatePoolWithTag` rewrite - a policy (Option A needs no private pool), not a missing export. | no |
| `xhci_version.h` | The package version and release date, the single editable source of the version. Included by `xhci98.rc`, `xhciqual\qual.h`, `xhcisnap\xhcisnap.c` and `xhcicln\xhcicln.c`; the INF's `DriverVer` literal is checked against it by the INF gate. | yes |

## Build and packaging inputs

| File | Role |
|---|---|
| `sources` | The Windows 2000 DDK build description: target name and type, the `SOURCES` list, defines per flavour, linker flags, and the comments that record which file belongs to the pure core and why. Read it before adding a file. |
| `makefile` | The DDK build stub; it only includes `makefile.def`. Every setting lives in `sources`. |
| `xhci98.rc` | The file version resource (task 8-A.4), so a binary recovered from a user's machine can be identified. Takes its fields from `xhci_version.h`; adds no import. |
| `xhci98.inf` | The **32-bit** INF: one file for two setup engines. Windows 98 SE reads the undecorated sections and loads the driver through `NTKERN`; Windows 2000 SP4 reads the `.NTx86` sections and loads it as a kernel service. Both install sections copy the project's two files through the shared `Xhci.CopyFiles`, and each adds its own OS-supplied list (`Xhci.CopyW98`: `usbd.sys` and `usbhub.sys`; `Xhci.CopyNT`: those plus `usbport.sys`) that `LayoutFile` has the OS fetch from its own install source. Both also copy `usbui.dll` through `Xhci.CopyUI`, to dirid 11 rather than 10, since 1.0.2.0. Since 1.1.0.0 it carries a third install path for Windows Vista and Windows 7: `%Mfg%=XhciModels,NTx86.6.0` in `[Manufacturer]` leads NT 6.x to `[XhciModels.NTx86.6.0]` and `[Xhci.Dev6.NTx86]`, which copies `xhci98.sys` alone (those systems already carry the four OS-supplied files, and their file queue aborts on a `LayoutFile` copy). |
| `xhci98-amd64.inf` | The **64-bit package's** INF, and it is a second file rather than a third path in the first (design record 11 section 12, decision 2): no Windows 98 path, a `[Manufacturer]` line decorated `NTamd64,NTamd64.6.0`, and two install paths - `[Xhci.Dev.NTamd64]` for Windows XP x64 and Server 2003 x64, which fetches the OS-supplied files from `Driver Cache\amd64`, and `[Xhci.Dev6.NTamd64]` for Vista x64 and Windows 7 x64, which copies `xhci98.sys` alone. Neither file may grow the other's sections - an undecorated or `.NT*` section here is one a 32-bit engine reaches, which would put an amd64 binary on a 32-bit machine, and the gate refuses it by name (`PATH-NO9X`, `PATH-MFGDEC`, `OS-DEFAULT`). The two are checked against each other by `scripts\inf-gate\test-inf-checks.ps1`, because the accepted cost of two files is that they can drift. |
| `portsw.vbs` | The Intel port switchover's user script (roadmap-hcd task 36.42, design record 16 section 5a): Windows Script Host VBScript that shows `XhciIntelPortSwitch` on every xhci98 controller and sets it to 0 or 1, then says a restart is needed. Finds the controller's driver key among the `NNNN` subkeys of the USB class key (listed by WMI on NT, by a `regedit /e` export on 98 and ME, probed `0000` to `9999` only if neither answers) for this driver's `DriverDesc`; on Vista and 7 asks for elevation itself. Published once by `make-release.ps1`, in `tools\portsw\` (roadmap-hcd task 38.9; `portsw\` at the version root from task 37.5 until `2.4.0.0`; no package carries it), held by the packager's self-tests and the source charset gate. |
| `portsw-on.bat`, `portsw-off.bat` | The script's two batch files (roadmap-hcd task 36.56, design record 16 section 5a, "The batch files"): each shows the value, sets 1 or 0 through `cscript //nologo portsw.vbs`, says a restart is needed and pauses. One file for `COMMAND.COM` (98 SE, ME) and `cmd.exe` (2000 to 7): `if "%OS%"=="Windows_NT" goto nt`, then `goto run`, so only `cmd.exe` reaches `cd /d "%~dp0"`. Published beside `portsw.vbs` in `tools\portsw\` by `make-release.ps1` (task 38.9; `portsw\` from task 37.5), held by the packager's self-tests (the split, the calls, ASCII and CRLF), the source charset gate and the batch-file line-ending gate. |
| `silent.reg` | The silent install's list (roadmap-hcd task 38.2, design record 13 section 6.11.8): `REGEDIT4`, the six `SilentClasses` entries - hubs alone; boot keyboard and mouse alone or composite; Bulk-Only SCSI storage alone or composite - under SweetLow's value names, written by this project from those facts. Published as `tools\silent\SILENT.REG`. Silences nothing while `XhciSilentInstall` is not 1. |
| `silent.vbs` | The silent install's user script (task 38.2, section 6.11.8), `portsw.vbs`'s pattern on Windows 98 SE and ME only: shows `XhciSilentInstall` on every xhci98 controller and the `SilentClasses` list (a `regedit /e` export read for its value lines), and sets the switch to 0, or to 1 only after the list reads back as `SILENT.REG`'s six; each write read back, each controller set, not set or unverified. Exit codes 0 done, 1 refused or nothing found (or NT), 2 usage, 3 partial, 4 list not verified, 5 not verified. Published as `tools\silent\SILENT.VBS`. |
| `silent-on.bat`, `silent-off.bat` | Its two batch files (task 38.2, section 6.11.8), `portsw-on.bat`'s shape: `silent-on.bat` shows, stops on any errorlevel, merges `silent.reg` with `regedit /s` and runs `silent.vbs 1`, reading its errorlevel 5, 4, 3, 1 in that order; `silent-off.bat` runs `silent.vbs 0` and says the list was left in place and how to delete it. On NT both change nothing. Published in `tools\silent\`. |
| `..\xhcisnap\snapon.bat`, `..\xhcisnap\snapget.bat`, `..\xhcisnap\snapoff.bat` | The field reports' Windows batch files (roadmap-hcd task 38.1), outside `src\` beside the tool they run: `XHCISNAP -on %1` (the log channel on: `-verbosity 2` with no `LEVEL`, an optional `LEVEL` of exactly 1 to 4, `0` refused pointing at `SNAPOFF.BAT`, all checked by the tool), `XHCISNAP -report %1` (the `.DEV` status, resources and file hashes, and the dump, in the current directory under a date-and-time 8.3 name `XHCISNAP` builds, or `-report NAME` for an optional 8.3 `NAME` the tool validates and refuses rather than overwrites; the channel left on) and `XHCISNAP -disable` (owner, 2026-10-13, for the two arguments). One file each for `COMMAND.COM` and `cmd.exe`, with `portsw-on.bat`'s split. Published as `SNAPON.BAT`, `SNAPGET.BAT` and `SNAPOFF.BAT` beside `XHCISNAP.EXE` in `tools\xhcisnap\` by `make-release.ps1` from `2.4.0.0`, held by the packager's self-tests (`Get-ReportBatchFaults`: the split, the one command line - for the two that take an argument (`ArgPass`), exactly `XHCISNAP -on %1` or `XHCISNAP -report %1`, with no `if` on `%1` (from the review of 38.1) - no percent variable but that `%1`, the tool run last (`ToolLast`, owner, 2026-10-13: reached by one `if exist XHCISNAP.EXE goto` to the label just above it, only labels and `pause` after it and no `IF ERRORLEVEL`, `XHCISNAP` printing the outcome and, last, the files to attach), no drive path, ASCII and CRLF; `Assert-ReportBatchLayout`), the source charset gate and the batch-file line-ending gate. |
| `..\xhciqual\QUALREP.BAT` | The field reports' MS-DOS batch file (roadmap-hcd task 38.1): `XHCIQUAL --probe-only --no-page --log-stamp`, the read-only probe saved to a new `mDDhhmmS.LOG` in the current directory that `XHCIQUAL` names from the DOS clock. Published beside `XHCIQUAL.EXE` in `tools\xhciqual\` from `2.4.0.0`, held by the same self-tests and gates. |
| `txtsetup.oem`, `txtsetup-amd64.oem` | The text-mode Setup driver descriptions (roadmap task 33.3, design record 13 section 5.6): what Windows 2000 and 32-bit XP (the first) and XP x64 (the second, staged under the first's name) read from drive A: when F6 is pressed, loading `xhci98.sys` as a `scsi` component and binding it to `PCI\CC_0C0330`, `USB\XHCI98_ROOT_HUB`, `USB\XHCI98_HUB`, `USB\XHCI98_HUB30` and `USB\XHCI98_COMPOSITE` (under `XHCI98\` before task 38.8). The x86 file also carries `xhcihlpr.sys` on a `dll` line, copied to `%windir%\OemDir` for GUI-mode Setup's install and not loaded in text mode (design record 18 section 15.9). Gated by `scripts\inf-gate\check-txtsetup-oem.ps1`, which holds both to the INF beside them (`OEM-COPY`: every file the INF's NT 5.x install copies from the disk is on the option) and to each other. |

## Generated, not tracked

| Path | What it is |
|---|---|
| `usbport.lib` | The import library for `usbport.sys`, generated by `scripts\make-usbport-lib.cmd` after a fresh clone. |
| `usbport_amd64.lib` | The same for the 64-bit build, from the same script and the same `usbport-imports.expected`. The names are undecorated on amd64, where the 32-bit file's stdcall decoration would not resolve. |
| `obj\`, `objchk*\`, `objfre*\` | DDK build output per flavour: `xhci98.sys`, `.pdb`, `.res` and `.obj` files. |
| `build*.log` | The DDK build engine's logs. |

The three flavours (`debug`, `release`, `qemu`) and what distinguishes their outputs are in [design record 08](design/08-build-flavours-and-the-log-channel.md); the build procedure is in [build-and-test.md](build-and-test.md).

## The Thunderbolt 3 connection manager - `src\cm\`

`xhcicm.sys`, the third driver (design record 21; `roadmap-usb4.md` Phase 39, scaffold of task 39-A.1), built by `scripts\build-driver.cmd` after `xhciuas.sys` in every x86 and amd64 run, from its own `sources` and `makefile`, in the same three flavours. One binary, two PnP roles: the NHI's function driver on every target and, on the NT targets only, the tunnel bridge's upper filter. The later pieces of Phase 39 (`tbt_ring.c`, `tbt_regs.h`, `tbt_icm.c`, `tbt_topo.c`, `cm_cfgmg.h`) land in this directory; a new `.c` file is one more line in `src\cm\sources` (and, if it is pure, a `call :run` line in `test\run-host-tests.cmd`).

| File | What it is |
|---|---|
| `cm.h` | The driver's header: the two roles' extensions (`CM_NHI`, `CM_FILTER`), the start stages (`CM_STAGE_*`) and the cross-file entry points. |
| `cm_vals.c` / `cm_vals.h` | The pure core (`test_cm`): the role a PDO's hardware ids name (`CmClassifyIds`: a PCI class 06/04 id is the filter's, anything else the NHI's), the vendor, device and class parsed from them, the NHI profile table (`CmProfileFor`: the firmware connection manager's generations and the USB4 software connection manager's hosts, Intel's ids from Linux `drivers/thunderbolt/nhi.h` and AMD's from FreeBSD `nhi_reg.h`), the INERT start's reasons (`CmStartDecision`), and the registry values' defaults, refusals and bounds (`CmValuesTake`; record 21 section 8). The INF gate reads the defaults out of its rule table. |
| `cm_entry.c` | `DriverEntry`, `AddDevice` (the role decided by `IoIsWdmVersionAvailable(1, 0x10)` and the PDO's `DevicePropertyHardwareID`), the dispatch table, the flavour marker and version string. |
| `cm_nhi.c` | The NHI function driver (task 39-A.4): attach; the start (the values read once, BAR0 found or the Code 10 refusal, the thread created) and, on the thread, the selection of record 21 section 1.4.2 (`TbtHwMatch` on the ids read from configuration space, `CmStartDecision`, `TbtHwPlatformInit`, `TbtHwSelect`), the ICM client's set-up and its bind (`TbtIcmStart`) after the PnP request is released, or the INERT start with its reason; the idle poll's tunnel actions handed to the re-enumeration seam (`cm_reenum.h`); the stop's unbind in section 5.6's order; the device table and the counters to the log. |
| `cm_filt.c` | The filters (NT only): the tunnel bridge's upper filter (its registration entry published after its start and withdrawn on stop, surprise removal and removal; every BusRelations read on its way back; a held bridge's D0 IRP retained) and safe eject's child filter (`Removable` set in `IRP_MN_QUERY_CAPABILITIES`; an orderly removal counted toward the port's eject); every other IRP passed down unchanged. |
| `cm_reg.c` | The one registry site: the values of record 21 section 8 from the NHI's driver key, through `IoOpenDeviceRegistryKey` and `RtlQueryRegistryValues` (`SITES=cm_reg.obj`). |
| `cm_log.c` | The shared log ring (`..\xhci_log.c`, compiled in by path): recording by `XhciLogVerbosity`, emission at stop through `DbgPrint` under `#if DBG` only, mirrored to port 0xE9 in the qemu flavour (decision D12; `SITES=cm_log.obj`, `debug` and `qemu` rows). |
| `cm_pool.c` | The one pool and DMA site (`SITES=cm_pool.obj`): `ExAllocatePoolWithTag` / `ExFreePool` (optional rows: nothing allocates pool yet), and the ring block's common buffer below 4 GB from `IoGetDmaAdapter`'s adapter (`CmDmaOpen`, `CmDmaClose`, record 21 section 4.2). |
| `xhcicm.rc` | The version resource, from `src\xhci_version.h`. |
| `xhcicm.inf` / `xhcicm-amd64.inf` | The NHI INFs, 32-bit (Windows 98 SE and ME, 2000 and XP, Vista and 7) and 64-bit (`NTamd64,NTamd64.6.0`), staged as `xhcicm.inf`; the NHI ids of record 21 section 3.3's list and design record 22 section 3.7's USB4 hosts with the class line `PCI\CC_0C0340` (the id lines exactly the rows of `cm_vals.c`, held by the gate's `CM-IDLIST`), and every registry value the driver reads written at its default under `FLG_ADDREG_NOCLOBBER` but `XhciCmTunnelBridge`, whose absence is meaningful. |
| `xhcicmf.inf` / `xhcicmf-amd64.inf` | The NT-only tunnel-port filter INFs (`$Windows NT$`), staged as `nt\xhcicmf.inf`, out of Windows 98's and ME's way (they read `$Windows NT$` INFs, task 39-0.4): a device-specific `UpperFilters` value on the Thunderbolt switch ports, the integrated controllers' tunnel root ports, the USB4 hosts' tunnel ports and the emulator's (record 21 section 3.3's lists, every id `&CC_0604`) with `pci.sys` kept as the port's service through `machine.inf`'s `PCI_DRV` sections (decision D2, read by task 39-0.4). |
| `cm_thr.c` | The NHI's connection-manager thread (record 21 section 4.4, task 39-A.4): created at start, ended at the stop, with `hcd_ctl.c`'s Windows 98 care around its handle; its work queue (a word of bits and the power IRP slots under a spin lock, PnP work waited for, power work never); the pure core's accessors - BAR0 through `READ_REGISTER_ULONG`, configuration space through `IRP_MN_READ_CONFIG` / `IRP_MN_WRITE_CONFIG` to the PDO (`hcd_svc.c`'s route), the Bus Master bit, the waits and a millisecond clock from the tick count (`KeQueryTickCount` and `KeQueryTimeIncrement`), monotonic. |
| `cm_power.c` | The NHI's power, as the device's power policy owner (record 21 section 4.4, task 39-A.4, on `hcd_power.c`'s model): the system IRP requests the device IRP and completes after it; a D3 runs the suspend's sequence (or at shutdown the unbind) on the thread before it goes down; a D0 is retained until the thread's reinitialisation and resume finish; no dispatch routine waits. |
| `cm_reenum.c` / `cm_reenum.h` | Re-enumeration and safe eject (tasks 39-A.5, driver half, and 39-A.8; record 21 sections 3.2, 6 and 7.4): the registration list between the NHI domains and the bridge filters (publish, withdraw, rundown; exclusive ownership and the active mark), the domain's tunnel-port set through `tbt_topo.c` over `HalGetBusData` reads, the port poll of section 6.5, the NT route (`IoInvalidateDeviceRelations` through each active bridge's filter) and the Windows 98 SE and ME route (CONFIGMG), the resume hold of a bridge's D0 IRP with its watchdog, safe eject's child filters and their eject notifications, and record 22's per-port eject entry (`CmReenumSetEject`). |
| `cm_ports.c` / `cm_ports.h` | The pure half of `cm_reenum.c` (`test_cm_ports`): the families' NHI, switch-port and root-port ids of record 21 section 3.3, checked against both INFs; Windows 98's PCI device ids; the bridge filter's snapshot; the resume hold's bound. |
| `cm_cfgmg.c` | CONFIGMG's services through the VMM dynalink (Windows 98 SE and ME, x86 only): the non-paged thunks, the `Get_Version` and root-id guards, and the calls of record 21 sections 6.1 and 7.4 (`cm_cfgmg.h` has the numbers). |
| `cm_nvlink.c` / `cm_nvlink.h` | The Stor98 link-state channel (tasks 39-A.7 and 40-A.7; record 21 section 7.2, record 22 section 10), its pure half (`test_cm_nvlink`): the entry protocol over `stor98cm.h`'s table (UP with a new generation, `DOWN_QUIESCED`, the acknowledgement), both profiles' statement conditions, where the channel is offered (NT only in `3.0.0.0`) and the acknowledgement wait with no receiver, interface 3's locator bytes and its `REMOVE_PENDING` request. The header is the API the seam and the USB4 driver half call. |
| `cm_nvlinkos.c` | The channel's driver half: the never-freed table, the `\Callback\Stor98LinkState` callback object (NT), no rendezvous on 98 SE and ME in `3.0.0.0` (interface 3's current-boot proof and locator are not published, so nothing is published or awaited there), the scan that publishes UP before a re-enumeration, the channel's own SHPC reading, the statement, the acknowledgement wait, the 9x orderly removal, the idle poll's ceiling. |
| `stor98cm.h` | Stor98's interface header (formerly `nvme98`'s `nvme98cm.h`), carried byte for byte (BSD-3-Clause, its own notice; `legal-provenance.md` section 2a): never edited here. |
| `cm_cfgwk.c` | The Windows 98 SE and ME CONFIGMG worker: one system thread per domain that makes every CONFIGMG call of the re-enumeration and of safe eject's 9x half (the route's opening and guards, the bridge devnode walk, the vanished children's query-remove and remove, `Reenumerate_DevNode`, `DN_REMOVABLE`), so the connection-manager thread never blocks when CONFIGMG does (an Add New Hardware wizard open); requests posted and collapsed per member; a stop that finds it inside a call leaves it to free itself. |
| `cm_statos.c` | The status record's publication (design record 24 section 3.2): the value `XhciCmStatus` (`REG_BINARY`) of the NHI's hardware key, written by the connection-manager thread only when the record `cm_status.c` built has changed, and the install tool's ask `XhciCmStatusAsk` read from the driver key and echoed; both keys opened in the start (`CmStatusOpen`), since on 98 SE and ME `IoOpenDeviceRegistryKey` is a CONFIGMG service that waits while a wizard is open. A failed write is counted and retried at the next pass. |
| `cm_qryos.c` | The query device (design record 25 section 4.2): one control device object per started NHI, `\Device\XhciCm<n>` linked as `\DosDevices\XHCICM<n>` (n the first free of 0..15) and named in `XhciCmQueryName` beside `XhciCmStatus`; created after a start that succeeded, with a new epoch, and deleted once the thread has ended at a stop or removal (the name value written empty: `ZwDeleteValueKey` has no Windows 98 evidence). Its extension holds the snapshot the thread publishes after each pass under the device's own spin lock and no pointer to the NHI; `IOCTL_XHCICM_QUERY` answers from it with the pass counter, its age and the stopping flag, and never touches the hardware. `cm_entry.c` routes every request to it by the extension's kind. |

Its gates: `scripts\inf-gate\check-cm-inf.ps1` (the four INFs, with `CM-NO9XBRIDGE`), `scripts\import-gate\xhcicm-imports.allow` and `xhcicm-imports-amd64.allow`, the flavour marker check and `scripts\source-stamp.ps1 -Driver xhcicm`.

### `src\cm\`'s pure core

The DDK-free half of `xhcicm.sys`, compiled into the driver from `src\cm\sources` and into the host suites below by `test\run-host-tests.cmd`. Each file names its sources in its header; `legal-provenance.md` section 2a lists the files lifted from `usb4cm` and those adapted from `usb4-emu` and Linux.

| File | What it is |
|---|---|
| `tbt_regs.h` | The host interface's registers, ring descriptor and control-packet fields, the firmware connection manager's status, mode and mailbox registers (task 39-0.2, record 21 section 4.1), and the router, adapter, path, TMU, PCIe, USB3 and notification fields the USB4 connection manager uses (task 40-0.1), each cited to the USB4 Specification 2.0 or the source it was checked against. Lifted from `usb4cm`'s `u4regs.h`, its MIT notice kept. |
| `tbt_ring.c` / `tbt_ring.h` | Ring 0 in Raw Mode, CRC-32C and the control transport with sequence numbers, timeouts and retries (task 39-A.2, record 21 sections 4.2 and 4.3), grown for the USB4 connection manager (task 40-A.1, record 22 sections 5.1 to 5.3): write requests, notifications, plain, enhanced and hot-plug acknowledgements, bounded waits and safe points; host suite `test_tbt_ring`. Lifted from `usb4cm`'s `u4hi.c`. |
| `tbt_icm.c` / `tbt_icm.h` | The firmware connection manager's client (task 39-A.3, record 21 sections 1.4 and 5): the ICM frames behind a per-generation table (Falcon Ridge, Alpine Ridge, Titan Ridge and Maple Ridge, Ice Lake), the bind, events, the device table, approval, the reconciliation and the resume refresh; host suite `test_tbt_icm`. Written for this project from `ICM-PROTOCOL.md` and Linux's layouts, cited. |
| `tbt_hw.c` / `tbt_hw.h` | The hardware profiles (the integrated controllers' force power, firmware-ready poll and LTR, with the power-ownership record), platform readiness and the four-test selection of the connection manager, the inert start included (tasks 39-A.3 and 40-A.2, record 21 section 1.4.2, record 22 section 3); host suites `test_tbt_hw` and `test_tbt_select`. |
| `tbt_topo.c` / `tbt_topo.h` | A domain's tunnel-port set and its sources, the `XhciCmTunnelBridge` list, the PCI Express Link and Slot Status and SHPC readings and the port poll (task 39-A.5, record 21 sections 6.3 and 6.5), and the router-named source and learned correspondence of the USB4 profile (task 40-A.4, record 22 section 7.6), over configuration-space accessors the caller supplies; host suite `test_tbt_topo`. |
| `tbt_u4cm.c` / `tbt_u4cm.h` | The USB4 software connection manager (tasks 40-A.3 and 40-A.8's pure part, record 22 sections 6 to 9): the host and device routers, lane bonding, the TMU, Configuration Valid, paths, the PCIe and USB3 tunnels, hot plug and teardown, the quirk table, discovery and preservation of tunnels found at start, and Thunderbolt 3 routers behind a USB4 host; host suite `test_u4cm`. Adapted from `usb4-emu`'s `u4cm.c` and Linux's `drivers/thunderbolt`, notices kept. |
| `cm_cfgmg.h` | CONFIGMG's VxD id, service numbers, flags and result values for the Windows 98 SE and ME route (task 39-0.3, record 21 sections 6.1 and 7.4), each read from the Windows 98 SE DDK's `CONFIGMG.H` with its line; values only. |
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
| `test_cm_nvlink.c` | `cm_nvlink.c`: the `nvme98` link-state channel's entry protocol, both profiles' statement conditions and the 9x record's bytes (tasks 39-A.7 and 40-A.7). |
| `test_u4cm.c` | `tbt_u4cm.c` over the fake fabric, with the `minimal`, `intel-adl` and `asm4242` presets and two domains. |
| `tbt_fake.h` | The modelled host interface `test_tbt_ring`, `test_tbt_icm` and `test_tbt_hw` drive, from the USB4 Specification 2.0 and `ICM-PROTOCOL.md`. |
| `tbt_topo_p14s.h` | The ThinkPad P14s Gen 1's PCI configuration space as captured under DOS in `usb4-emu`, for `test_tbt_topo`. |
| `u4_fakefab.c` / `u4_fakefab.h` | `test_u4cm`'s C89 fake USB4 fabric, one domain per instance, written from the specification and `usb4-emu`'s documented host models, independently of `tbt_u4cm.c` (decision U11). |
| `test_inst.c` | `xhciinst\inst_core.c`, the install tool's pure half (design record 23). |
| `test_tr.c` | `xhcitr\tr_core.c`, the USB tree tool's pure half (design record 25 section 10): text, the connection answers, descriptors, the query's parsing and eject gates, the router keys and event diff, the command line, the report. |
| `test_trpci.c` | `src\cm\cm_qpci.c` and `xhcitr\tr_pci.c`: the bounded walk below a tunnel port over modelled buses, the capability walk with bad lists, the decoders, 79 columns (design record 25 revision 12). |
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

### Other files in `test\`

The host suites of `xhci98.sys` and `xhciuas.sys` (`test_enum.c`, `test_pipe.c`, `test_uas.c` and the rest) are named with the files they test above; these are the shared and guest-side pieces.

| File | What it is |
|---|---|
| `test_harness.h` | The one copy of the check helpers every host suite includes (`CHECK`, `CHECK_EQ`, the counts and the failure format); header-only, with no expectations of its own (design record 03). |
| `hub_port_vectors.h` | One downstream port of an external USB 2.0 hub as the bus's hub class tracks it, written as a vector table (roadmap-hcd tasks 27-A.1 and 27-A.4); `test_topo.c` checks it is well formed. |
| `pmclient\pmclient.c`, `pmioctl.h`, `pmctl.c`, `xhcipm.inf`, `build.cmd`, `sources`, `makefile` | Selective suspend's test client (roadmap-hcd task 36.34, design record 14 section 10): `XHCIPM.SYS`, a WDM function driver that idles one USB device on demand, and `PMCTL.EXE`, its console tool. Test only: the packaging gate refuses a release that carries either (`build-and-test.md`, "The test client"). |
