# Issues found - repository audit of 2026-10-18

What this is: the findings of a complete read-only audit of the tree on branch
`3.1.0.0` (commit `ef46423c` plus the Phase 42 task 42-A.16 added the same day),
taken on 2026-10-18 by nine parallel audit passes, one per slice: the xHCI core
(`src/xhci_*`), the WDM half (`src/hcd_*`), the three smaller drivers
(`src/cm`, `src/uas`, `src/hlpr`), the host tests and `xhciqual`, the four
user-mode tools, the scripts and INFs, the top-level and user documents, the
roadmaps and design records, and the contributor guides, issue records and
spec transcriptions. Every item below was verified at the cited lines; nothing
in the repository was changed by the audit except this file and the roadmap
task named below. Line numbers are those of `ef46423c`.

How to use it: each item is to be fixed in a later session. Fix an item, then
delete it from this file or mark it `[fixed <commit>]`. The order inside each
section is most severe first. "Suggested fix" is a suggestion, not a decision;
the three items that needed the owner's call (D-H3, D-T13, U5) carry the decision taken on 2026-10-18.

Not in this file: the XHCITR Safely Remove of a plain USB device stopping
every USB port, which the owner raised on 2026-10-18. That is roadmap-usb4
task 42-A.16 (an investigation with a regression leg in 42-V.2), not an audit
finding.

Counts: 3 High, 24 Medium, about 110 Low. The three Highs are all documents.
No load-blocking import, memory-safety, IRP-lifetime or IRQL defect was found
in any driver; the register, TRB, context and completion-code definitions all
agree with the xHCI 1.2c transcription; all 246 counters are written under
their own names; every host suite is run by the runner; all eight INFs, five
`.rc` files, `xhci_version.h` and `releases/history.md` agree on `3.0.0.0`.

Where several passes reported the same thing it appears once.

---

## 1. Driver code - xhci98.sys, the WDM half (`src/hcd_*`)

### C1. HcdIoDeferred reads the transfer record's pipe after releasing the record
- Severity: Medium
- Where: `src/hcd_io.c:2656` (hcdRecordRelease), `:2662-2666` (`x->Pipe`, `x->Pipe->Parent` read after it); `src/hcd_io.c:2796-2806` (hcdWaitPipeOne polls `Xfers[i].State` unlocked); `src/hcd_cfg.c:1339-1341`, `:1301-1302` (HcdIoWaitPipe then hcdCfgPipeFree -> HcdPoolFree(pipe))
- Problem: the completion loop sets the record FREE under the controller lock, drops the lock, then evaluates `x->Pipe` and `x->Pipe->Parent` as HcdIoPark arguments. The record lives inside the HCD_PIPE allocation; the controller thread's close path spins on `State != FREE` without the lock and then frees the pipe. On SMP (2000/XP/amd64), with a DEVICE_GONE completion concurrent with a pipe close, the read lands on freed non-paged pool.
- Suggested fix: capture `pipe`, `parent` and the Ep0 test into locals under the same lock hold, before hcdRecordRelease, and pass the locals to HcdIoPark.

### C2. Controller FDO name and FdoSerial come from two reads of the global counter
- Severity: Low
- Where: `src/hcd_entry.c:139` (InterlockedIncrement result consumed by the digit loop), `:169` (`hc->FdoSerial = (ULONG)hcdFdoSerial;` re-reads the global); `src/hcd_door.c:433-435` (`\DosDevices\HCD<n>` built from FdoSerial)
- Problem: with two xHCI controllers, PnP can run both AddDevice calls before either reaches line 169, so the first controller records `FdoSerial == 2` while its object is `\Device\XHCI98HC1`, and the door's link points at the other controller.
- Suggested fix: keep the interlocked result in a second local and assign `hc->FdoSerial` from it.

### C3. Review-process wording in source comments
- Severity: Low (decided; see D-H3: sweep source comments too)
- Where: `src/hcd_io.c:2659-2661` ("review of 6dae92b, finding 2" naming the tool), `src/hcd_ctl.c:2727`, `src/uas/uas_xport.c:1787` ("review round 2"), and the xhciqual/xhcisnap sites listed under T12 and U1
- Problem: the owner's rule of 2026-10-14 (recorded in the repo memory, not in AGENTS.md) says tracked docs never name the review tool, a review round or a finding number. These comments explain nothing a reader can resolve. Whether the rule covers source comments is the owner's call.
- Suggested fix: replace each with the task id and date the comment already carries.

## 2. Driver code - the xHCI core (`src/xhci_*`, `src/xhci.h`, `src/xhci_hw.h`)

### X1. xhci_hw.h declares 42 functions and one extern that exist nowhere
- Severity: Medium
- Where: `src/xhci_hw.h:26` (`extern USBPORT_REGISTRATION_PACKET XhciRegPacket`), `:503`, `:526`, `:907-944`, `:1051`, `:1062`, `:1080`, `:1137-1210`, `:1291-1423`
- Problem: the miniport's root-hub and slot-layer API (`XhciRh*`, `XhciSlot*`, `XhciRootHubPoll`, ...) is still prototyped with IRQL and lock contracts, citing `xhci_dispatch.c`, `xhci_rh.c` and `xhci_slot.c`, all deleted on 2026-10-02. No definition, no call site.
- Suggested fix: delete the dangling prototypes and the extern; a host check that every prototype in `xhci_hw.h` has a definition would keep it from recurring.

### X2. XHCI_EXTENSION carries about 250 fields documented as live readings that nothing writes
- Severity: Medium
- Where: `src/xhci.h:5370-5513`, `:5548-5553`, `:6554-6600`, `:6770-7159`, `:7319-7444`, `:7671-7859`, `:8186-8206`, `:8528-8547`; `src/xhci.h:5093-5224` (XHCI_DEVICE, 20 of 32), `:3745-3805` (XHCI_TRANSFER, 13 of 35); `docs/usb-xhci-info/xhci-data-structures.md:1067-1100`
- Problem: 250 of 482 members (`RhPortStatusQueries` .. `VhubTtNamed`) are referenced by no `.c` file, yet their comments state present-tense identities ("enforced on the host by note_open_accounting", "XhciSlotDrainSettled recomputes it"). The data-structures doc's mid-TD verdict (1067-1100) rests on `MidTd*` fields and a print site in `xhci_dispatch.c` that do not exist; a snapshot reader reads a permanent 0 as a measurement.
- Suggested fix: remove the dead fields (bumping the snapshot schema and offset tables) or mark each dead block "unwritten under the HCD, kept for layout"; rewrite the doc's verdict paragraph around the `XHCI_TRANSFER_QUEUE` counters that do move.

### X3. CommandNoOpRewrittenPA can outlive its No Op and steal a later command's completion
- Severity: Low (latent)
- Where: `src/xhci_cmd.c:1477` (set), `:1183-1198` (consumed), `:1164-1166`, `src/xhci_ring.c:1118-1127` (retirement sweeps the dequeue past outstanding TRBs with no event)
- Problem: the marker is cleared only by its own completion, an invalidate or a ring rebuild. If the No Op's completion is lost, the marker stays armed; the command that lands on that position 62 commands later has its completion taken as the No Op's while `CommandState` stays PENDING, so a completed command is aborted by the watchdog.
- Suggested fix: clear `CommandNoOpRewrittenPA` in `xhciRetireCommand` (or after the matched retirement at 1218) whenever its index is no longer outstanding, and count the sweep.

### X4. xhci_topo.c takes the whole wIndex as the port number
- Severity: Low
- Where: `src/xhci_topo.c:770`, `:790-800`; senders `src/hcd_hub.c:704,1161,1201`, `src/hcd_susp.c:1737,1914`
- Problem: `SET_FEATURE(PORT_LINK_STATE)` and its siblings carry a selector in wIndex 15:8; the snoop records port 0x3nn/0x5nn. No effect today because `XhciTopoHubMark` requires the descriptor flag first.
- Suggested fix: `port = (ULONG)setup->wIndex & 0xFFUL;`.

### X5. XhciUnmaskInterrupts carries a read HCRST/LHCRST back into USBCMD
- Severity: Low
- Where: `src/xhci_init.c:1942-1944`; contrast `:1727-1731` (the mask strips HCRST and refuses during a reset)
- Problem: `XHCI_USBCMD_DEFINED_MASK` includes bits 1 and 7; an unmask during a slow reset re-issues it. Unreachable on the start path, but the two siblings state one rule and only one applies it.
- Suggested fix: mask HCRST and LHCRST out of the carried bits and add the same "reset in progress: write nothing" test.

### X6. Stopped - Length Invalid arm drops a failed ring sum without counting SumFailures
- Severity: Low
- Where: `src/xhci_xfer.c:1823-1827`; contrast `:1780-1784`, `:1512-1516`
- Suggested fix: add `queue->SumFailures++;` before the `return 0` at line 1826.

### X7. XhciXferIsoEvent never fills Code, Refused or Unattributed
- Severity: Low
- Where: `src/xhci_xfer.c:3936`, `:3983-3986`, `:4027-4030`, `:4042-4045`; contract `src/xhci_xfer.h:552-562`
- Problem: the header documents the three fields for both producers; the isochronous path zeroes them. The bulk path's tolerance hook (`hcd_dev.c:620-622`) would be inert if applied to an isochronous pipe.
- Suggested fix: set the three fields as the bulk path does, or state in the header that the isochronous path leaves them 0.

### X8. XhciFuncFirsts and XhciFuncSplit disagree on an IAD's first interface when descriptors are out of order
- Severity: Low
- Where: `src/xhci_func.c:172-191`, `:216`, `:335-347`; `src/xhci_func.h:102-105`; `test/test_func.c:1366-1390` covers only the in-order case
- Problem: with no `bFirstInterface`, Split publishes the first present interface in descriptor order as MI_nn while Firsts arms the lowest-numbered one.
- Suggested fix: in Split pick the lowest-numbered interface of the mask (or make Firsts use descriptor order), and add the out-of-order vector.

### X9. XhciPipeFunctionEndpointFrom bounds its walk by the caller descriptor's own wTotalLength
- Severity: Low
- Where: `src/xhci_pipe.c:921-925`; caller `src/hcd_cfg.c:3078-3084`
- Problem: `callerLength` is read from the URB's `ConfigurationDescriptor` bytes 2-3, for which the URB carries no size, so the walk is bounded by a client-written value. Kernel clients are trusted, hence Low.
- Suggested fix: run `XhciPipeRecordCheck` on `sc->ConfigurationDescriptor` once before the per-endpoint loop, or take an explicit length from the HCD.

### X10. xECP is not bounded below, unlike RTSOFF and DBOFF
- Severity: Low
- Where: `src/xhci_caps.c:99`, `:346`; contrast `:1254`, `:1263`
- Suggested fix: refuse an xECP byte offset below `XHCI_CAP_REGISTERS_BYTES` in `XhciDeriveHcInfo`.

### X11. XhciPswRoute overwrites the first failure after a PSSEN read-back fault
- Severity: Low
- Where: `src/xhci_psw.c:123-142`, `src/xhci_psw.h:59-73`
- Suggested fix: use `xhciPswFail` for the XUSB2PRM and XUSB2PR_WRITE stops too.

### X12. Root and hub suspend evidence disagree on when SUSPEND_ENTRY is reported
- Severity: Low
- Where: `src/xhci_susp.c:812-815` (root: any link state) vs `:852-855` (hub: only SUSPENDING); `src/xhci_susp.h:414`
- Suggested fix: gate the root case on `XHCI_SUSP_SUSPENDING` or document the difference.

### X13. USB3 PLS = Resume: xhci_link.c and record 14 contradict the transcription
- Severity: Low
- Where: `src/xhci_link.c:17-18`, `:229-245` (accepts PLS 15 on a USB3 port for WANT_RESUME) vs `:129-131` (classifies it UNKNOWN as a USB2 encoding); `docs/usb-xhci-info/xhci-data-structures.md:1729`, `:1742`; `docs/contributing/design/14-selective-suspend.md:1386-1393`
- Problem: the transcription (and xHCI 4.15.2.1, Linux) say Resume is USB2-only; a USB3 port exits U3 via Recovery to U0. The branch is dead on real hardware.
- Suggested fix: align the three; if the branch is kept as tolerance, say so instead of citing 4.15.2.1 as the rule.

### X14. Dead miniport surfaces documented as live: xhci_port.c, xhci_vhub.h, xhci_topo claims
- Severity: Low
- Where: `src/xhci_port.c:20-27`, `:196-803`, `:507-535` (the "reported as High Speed" lie no code performs), `src/xhci.h:2525-2760`, `src/xhci_init.c:3963` (logs `RootHub.Status`, never written); `src/xhci_vhub.h` whole (20 prototypes, `.c` deleted; still included from `src/xhci.h:38`, structs embedded at `:8514-8521`), `src/xhci_enum.h:12`; `src/xhci_topo.c:12`, `:659-725`, `:820-863`, `src/xhci_topo.h:284-295`, `:358-367`, `:575`, `:597-615`, `:653-661` (claim/migration API and nine counters nothing can move)
- Problem: `XhciRootHubBuild`, the `XhciPortShadow*` family, `XhciTopoClaimChild`/`DropPending`/`SuppressClaim`/`Migrate` and the vhub API have no driver caller; comments send readers to deleted files and `test_port.c`/`test_topo.c` maintain the surfaces as if live. `implementation-invariants.md:1291-1300` still states the High-Speed rule as current (see D-G5).
- Suggested fix: delete the dead machinery and counters or mark each section host-suite-only; drop the vhub include or reduce the header to the embedded structs (note if their size is load-bearing for the snapshot schema).

### X15. Stale comments across the core
- Severity: Low
- Where and what:
  - `src/xhci.h:844-851`, `:5516-5521`, `:5592-5599`, `src/xhci_init.c:12`, `:2104-2114`, `src/xhci_caps.c:17-20`: say USB 3.x ports are left unpowered and unmanaged; since task 29-A.1 every port is powered (`xhci.h:2039-2045`, `xhci_init.c:2013-2043`); the variable feeding `PortsUnpowered` is still `superSpeed` (`xhci_init.c:2248-2298`).
  - `src/xhci_ring.c:377-383`, `:390-392`, `src/xhci_ctx.c:329-333`: "this driver enables no Streams"; `xhci_stream.c:243-278` does since 31-A.1.
  - `src/xhci_ring.c:12`, `src/xhci_ctx.c:12`, `:124`, `src/xhci_cmd.c:1263`, `src/xhci.h:4375`, `:4416`, `:4662`, `:4702`, `:5705`, `src/xhci_port.c:578`: 21 references to `xhci_slot.c`, `xhci_rh.c`, `xhci_dispatch.c` (deleted); `XhciSlotCommandEvent` lives in `hcd_dev.c`; the reading key in `src/hcd_svc.h:39-71` does not map these files.
  - `src/xhci.h:885-891`, `:966-969`, `:1380-1386`, `src/xhci_mem.c:644`: common-buffer sizing described as usbport's `MiniPortResourcesSize` contract with a 48-byte header; the HCD allocates `XHCI_HC_RESOURCES_SIZE` in `hcd_dma.c:54-71`.
  - `src/xhci_xfer.c:2-11`: file header says the file is the control engine "kept whole and unchanged" and that `test_xfer` left the tree; contradicted by `xhci_xfer.h:12-30` and line 16.
  - `src/xhci_cmd.c:414-420`, `:771-779`: "the arm returns 0 either way"; `XhciAsyncTimerArm` (246-263) now reports and counts refusals.
  - `src/xhci_log.h:505-508`: says FLUSH_EMPTY is unreachable; the periodic flush (`hcd_log.c:407-416`, `hcd_ctl.c:2466`, counters 0) reaches it.
  - `src/xhci_inj.h:25`, `:129`, `:306`, `:315-318`: fault lists lag `xhci_inj.c:246-271`, `:291`.
  - `src/xhci_pci.c:202-204`: says the parser treats an all-ones header as "no capability"; `xhci_caps.c:52-61` returns OUT_OF_RANGE, a refusal.
  - `src/xhci.h:212-218`, `src/xhci_cmd.c:966-967`, `docs/usb-xhci-info/xhci-data-structures.md:420-436`: command range "9-23" omits xHCI 1.1's 24 and 25 (Get/Set Extended Property).
  - `src/xhci.h:2901-2905`: "the cycle bit is not the builder's business" is wrong for `XhciTrbLink` (`xhci_ring.c:150-162`).
  - `src/xhci_hw.h:134-139`, `:158-159`, `src/hcd_poll.c:34`, `src/hcd_ctl.c:1496-1504`: PCI Command bit 10 (INTx Disable) has no header macro; a private define and a bare `0x0400U` instead.
- Suggested fix: reword each to the current state; add `XHCI_PCI_COMMAND_INTX_DISABLE` to `xhci_hw.h`; rename `superSpeed`.

## 3. Driver code - xhcicm.sys, xhciuas.sys, xhcihlpr.sys

### M1. A UAS task-management answer landing after the wait's timeout is discarded and its tag quarantined
- Severity: Low (rare; the consequence is a port reset that ends every in-flight command BUS_RESET)
- Where: `src/uas/uas_xport.c:1241`, `:1246`, `:1251`
- Problem: `uasTmf` decides from the wait's return value (`wait == STATUS_SUCCESS && ...`). A RESPONSE IU arriving between the 3 s timeout and re-taking the lock leaves the slot FINAL with a valid code, yet the answer is discarded, the tag quarantined and `uasRecoveryNext` escalates to a reset.
- Suggested fix: decide from the slot under the lock (`slot->State == UAS_SLOT_FINAL && !slot->Failed`), and quarantine only when no RESPONSE IU was parsed for the slot.

### M2. A 98 SE child answering Configuration Request Retry Status is taken as vanished
- Severity: Low
- Where: `src/cm/cm_cfgwk.c:592-604` (`cmWkBusIds` records vendor `0001h` as present), `:1555-1574` (`cmWkMember` removes a child whose id does not match); `src/cm/tbt_brnum.c:53` defers on the same id
- Suggested fix: in `cmWkBusIds` treat vendor `0001h` as "bus not completely readable" (return `room + 1`) so nothing is taken for vanished on that pass.

### M3. The empty-bus wait is dropped on the last two numbering attempts, not the last
- Severity: Low
- Where: `src/cm/cm_cfgwk.c:1435-1443` (`tries + 1UL < CM_WK_NUMBER_TRIES`); comment above it and `docs/contributing/design/21-thunderbolt3-icm-client.md:1563-1565` say "every attempt but the last"
- Suggested fix: `tries < CM_WK_NUMBER_TRIES`, or amend the comment and record.

### M4. A fresh UP bumps LinkGeneration and clears REMOVE_PENDING while a 9x removal request is outstanding
- Severity: Low (self-healing after the 14 s timeout)
- Where: `src/cm/cm_nvlink.c:140-152` (CmNvlinkPublishUp), `src/cm/cm_nvlinkos.c:812-821`, `src/cm/stor98cm.h:432-435` (R9a: generation unchanged while REMOVE_PENDING is set)
- Suggested fix: treat an entry with `CM_NVLINK_REMOVE_PENDING` as `CM_NVLINK_UP_ALREADY` until `CmNvlinkWkWithdraw` clears it, or withdraw the request before any fresh exposure of that bridge.

### M5. A command refused by the ring before it was sent still marks the ICM session ambiguous
- Severity: Low
- Where: `src/cm/tbt_icm.c:1133-1136`, `:1242-1247` (`rc != TBT_OK && rc != TBT_E_ERROR_FLAG && rc != TBT_E_PARAM` sets `Ambiguous` for RING_FULL and STOPPED too)
- Suggested fix: set `Ambiguous` only after a successful `TbtRingSend` (track a `sent` flag in the attempt loop).

### M6. A disconnected USB4 port with only USB3 or DP residue at start is torn down as if it had a PCIe tunnel
- Severity: Low
- Where: `src/cm/tbt_u4cm.c:4530-4544` (`p->Pcie.Written |= TBT_U4_E_HOST_ENABLE` unconditionally at 4538), `:3646-3662` (`hadPcie`); record 22 sections 7.3 and 9.2
- Problem: a spurious path-closed statement, `RemovalOwed` and a `TUNNEL_DOWN` action for a port that carried no PCIe path; counted in `PcieTornDown` and the statement counters.
- Suggested fix: drop the forced bit or make it conditional on `p->Pcie.Written != 0`.

### M7. A DROM longer than 512 bytes is disbelieved whole, silently losing its Unused Adapter entries
- Severity: Low
- Where: `src/cm/tbt_u4cm.c:826`, `src/cm/tbt_u4cm.h:76` (`TBT_U4_DROM_MAX 512`); the Length field is 12 bits (up to 4108 bytes); record 22 section 6.11 (line 1238) names no size cap
- Suggested fix: parse the DROM in a bounded streaming way, raise the cap to the 12-bit field, or at least state the cap and count it.

### M8. tbt_brnum.c: the prefetchable window is closed through 24h alone, and the line stage reports DONE after skipping functions
- Severity: Low
- Where: `src/cm/tbt_brnum.c:303`, `:766-767`, `:873-874` (28h/2Ch untouched on the REOPENED path; journal at 1208 covers only 20h); `:1004-1009`, `:1020-1022`, `:1117-1118`, `src/cm/tbt_brnum.h:89` (`brLines` breaks on a probe of 2/3 or a failed 3Ch read, `brIrq` still answers DONE); `docs/contributing/design/21-thunderbolt3-icm-client.md:1583-1584`
- Suggested fix: when 24h bits 3:0 read 1, journal and write 28h/2Ch to 0 (restored in `brWinUndo`); return ACCESS or NOT_READY from `brLines` for a skipped function.

### M9. NHI ring index registers are written as whole DWs (hardware-unverified assumption)
- Severity: Low
- Where: `src/cm/tbt_ring.c:546`, `:682`, `:775`; Linux `nhi.c` writes the 16-bit halves alone
- Problem: correct only if the host interface ignores the read-only half; exercised on the emulator only (39-V.2). On a part that latches the written half the RX producer resets to 0 after every frame.
- Suggested fix: record the assumption in record 21 section 4.2 as hardware-unverified and read it on the P14s, or switch to 16-bit writes (`WRITE_REGISTER_USHORT`, import-gated).

### M10. cm/uas comment and build-description drift
- Severity: Low
- Where and what:
  - `src/cm/cm_power.c:27`, `:586`, `src/cm/cm_thr.c:490`: "passes down untouched" after removal; the code completes with STATUS_DELETE_PENDING (which is right, as `hcd_power.c:480-482` does).
  - `src/cm/cm_pool.c:6`, `scripts/import-gate/xhcicm-imports.allow:15`, `xhcicm-imports-amd64.allow:63`: "nothing allocates pool"; `cm_cfgmg.c:111`, `cm_cfgwk.c:2036`, `cm_nvlinkos.c:347,602,635` call CmAlloc, `cm_qryos.c:237,271` CmFree; the pool rows are still `optional`.
  - `src/cm/sources:26`: cm_log.c "debug and qemu only"; every flavour prints under `XhciCmLogDebugView` since the D12 reversal of 2026-10-16.
  - `src/cm/cm_reg.c:12`: "the one value no INF writes"; `XhciCmBridgeNumbering` (line 37) is a second.
  - `src/cm/cm_query.h:21-24` and record 25 section 4.2 (`25-usb-tree-tool.md:380-382`): `XhciCmQueryName` "deleted"; the code writes it empty (`cm_qryos.c:34-39,541-545`), as record 25 line 1446 says.
  - `src/cm/cm_status.h:28`, record 24 line 129: generation "never 0"; `CmStatusOpen` writes the first record with 0 (`cm_statos.c:220-243,292-302`; record 24 line 204 says so).
  - `src/uas/uas.h:55`: `UAS_STATUS_BUFFER 288` "a SENSE IU with 252 sense bytes" (that is 268).
  - `scripts/inf-gate/check-uas-inf.ps1:41`: the UAS-TMPNAME rule's header states it backwards (line 200 and `src/uas/xhciuas.inf:94` have it right).
- Suggested fix: reword each; consider making the pool rows `required`.

### M11. Record 21 drift against the cm code
- Severity: Low
- Where: `docs/contributing/design/21-thunderbolt3-icm-client.md:1741` ("the five" PnP codes; `cm_filt.c:184` watches six, DEVICE_USAGE_NOTIFICATION included), `:1457` (Get_DevNode_Status "the log only"; `cm_cfgwk.c:1658-1661` uses it for the DN_REMOVABLE check of section 7.4), `:920` (Total Paths "bits 9:0"; `tbt_regs.h:94` and the spec say 10:0), `:1119`, `:1296` (trailer "stripped unchecked"; `tbt_ring.c:755` checks, counts `cm.ring.crc-errors` and flags every frame)
- Suggested fix: correct the four passages.

## 4. Host tests (`test/`) and the DOS qualifier (`xhciqual/`)

### T1. XHCIQUAL's HCIVERSION floor is stale: one report contradicts itself
- Severity: Medium
- Where: `xhciqual/qual.h:270-277` (`QUIRK_HCI_FLOOR 0x0100U`, "38.11 moves it to 0.96 ... this follows it then"), `xhciqual/quirks.c:449-455` ("the driver refuses this controller"), `xhciqual/report.c:342-349` ("xhci98 drives it with XhciAccept096 at its default 1"), `src/xhci_caps.c:999-1002`; `xhciqual/README.md:175-181`, `xhciqual/hardware-testing.md:330-333`, `xhciqual/test/test_quirks.c:1137-1151` pin the stale value
- Suggested fix: `QUIRK_HCI_FLOOR 0x0096U`, reword the note to "below 0.96: refused at every XhciAccept096", update README, hardware-testing and `test_floor`.

### T2. README says OHCI's C4 runs under --poll-only; legacy.c skips it, and the comment's reason is false
- Severity: Medium
- Where: `xhciqual/legacy.c:840-849` (SKIP, justified by "ohci_irq installs one"), `:516-594` (`ohci_irq` installs no vector), `xhciqual/README.md:620-622`; no `ohci_poll_only` case in `xhciqual/test/run-qemu-matrix.ps1`
- Suggested fix: keep C4 in poll-only (the README's reasoning is sound), fix the comment, add a matrix case.

### T3. strcpy overruns TBT_ICM_NAMES.Model by 8 bytes in test_cm_status
- Severity: Medium
- Where: `test/test_cm_status.c:80-84`; `src/cm/tbt_icm.h:154`, `:228-231` (`Model[48]`)
- Problem: 56 bytes into 48; masked only because the next two fields are overwritten afterwards. UB in the suite.
- Suggested fix: a model string of at most 47 characters (anything over 31 still exercises the cut).

### T4. "Lane Disable cleared at unplug" cannot fail: the fake fabric clears it itself
- Severity: Medium
- Where: `test/test_u4cm.c:517-520`, `test/u4_fakefab.c:1685-1694` (`fabDisconnect` clears 0x4000 on both host lanes), `src/cm/tbt_u4cm.c:3774-3777`
- Suggested fix: drop the clear from `fabDisconnect` (a real router keeps a CM-written Lane Disable across an unplug) or assert `fabFindWrite(...) >= 0` after the unplug.

### T5. Record 24 section 10 promises test_inst vectors that do not exist
- Severity: Medium
- Where: `docs/contributing/design/24-thunderbolt-device-tool.md:744-748` ("against records built by the driver's own cm_status.c", "a wrapped tick count"), `test/test_inst.c:1451-1511` (records built by hand with `put32`, no wrap vector), `test/run-host-tests.cmd:523` (links `inst_core.c` only)
- Suggested fix: add the two vectors (link `cm_status.c` into test_inst) or reword section 10.

### T6. Design record 03 still describes a twelve-suite harness built around test_init
- Severity: Medium
- Where: `docs/contributing/design/03-host-unit-tests.md:6-10`, `:398-409` (nine rows naming `test_init`), `:457-470`; `test/run-host-tests.cmd:277-281` (test_init and test_vhub retired 2026-10-02; 52 suites now)
- Problem: the properties the `test_init` rows list (ISR/DPC ordering, init write order, DMA-stop proof) are pinned by no suite and the record does not say so; no CM, tool or HCD-era suite appears in its table.
- Suggested fix: regenerate the status paragraph and section 5 table from the runner, mark each former `test_init` row retired with its successor or "unpinned".

### T7. Test-suite claims in records 21, 22 and 24 that the suites do not back
- Severity: Low
- Where and what:
  - Record 22 `:2342-2343` "an unplug in each wait": `test/test_u4cm.c:161-179`, `:356-379` fire the hook on writes only; no unplug inside the Detect, L0, lane-1 CL0 or HCA polls.
  - Record 24 `:757-760` "each approval state": `test/test_cm_status.c` never asserts PENDING; `CM_ST_APPR_REJECTED` (`cm_status.h:110`) is produced by nothing in `src/cm` though `xhcitr/tr_core.c:1104,1389` decode it.
  - Record 21 `:2919` "operating modes 0, 1, 2 and 7": `test/test_tbt_hw.c:217-229` has no mode-7 vector.
  - Record 22 `:2348` "quirk table row by row": `test/test_u4cm.c:731-733` checks AMD 0208 and 020B only (0209, 020A unpinned).
  - Record 22 `:2356-2362` attributes the resume Router-Ready-late cases to the ASM2464 preset; they run on the generic router (`test_u4cm.c:1651-1721`).
  - Record 24 `:740-743` names an "incomplete walk" unit-selection vector the pure API (`inst_core.h:731-735`) cannot express.
  - Record 03 `:422` claim identity has two terms; `test_topo.c:137-146` maintains three. Record 03 `:395`, `:463`, `run-host-tests.cmd:378-380`, `test_port.c:705` describe the pre-29-A.1 "zero managed ports refused" rule; the refusal exercised is "zero USB 2.0 ports".
- Suggested fix: add the missing vectors or narrow each sentence.

### T8. Weak or vacuous checks
- Severity: Low
- Where and what:
  - `test/test_tbt_hw.c:12-14` promises "an inert selection wrote nothing" on every inert reason; only line 215 checks it.
  - `test/test_u4cm.c:2588-2600`: the Time Disruption block is guarded by `if (CapTmu != 0)` with no assertion that it held.
  - `test/u4_fakefab.c:131-141`: the RX pump holds frames when the ring has no vacancy; `tbt_fake.h:268-273` and the hardware drop them.
  - `test/test_cm.c:212-213`, `test/test_sshub.c:260-261`, `xhciqual/test/test_sysinfo.c:696`: `CHECK(1, ...)` after a call.
  - `test/test_tol.c:219-230`: one input under five labels claiming per-type coverage.
  - `test/test_inst.c:315-331`, `:1366-1399`: the cm INF checks `return` with no CHECK when the INFs fail to load (line 231 treats a missing INF as a failure).
  - `src/hcd_quirk.c:60-75` vs `src/xhci_quirk.c:176-205`: the 14 quirk value names exist twice; `test/test_quirk.c:92-99` pins two of one copy and no suite compiles `hcd_quirk.c`.
- Suggested fix: as each line says; generate both quirk tables from one X-macro list.

### T9. Suite hygiene
- Severity: Low
- Where and what: `test/test_own.c:752`, `test/test_poll.c:1376`, `test/test_susp.c:2870` return 0/1 where `test_harness.h:33-34` and record 03 line 455 say "exit code = failure count"; `test/test_tbt_brnum.c:978`, `:1021` read the uninitialised dead local `sib0`; `test/test_log.c:148-151`, `:176` hard-code `"test_log.c"` instead of `__FILE__` and print no file/line on one failure; `test/test_xfer.c:388-389`, `:2300-2307` cite vectors in the retired `test_init.c`; `test/run-host-tests.cmd:531-532` runs `test_tbt_select` and `test_u4cm` with no `rem` block; `test/pmclient/pmclient.c:6` names `pmclient.inf` (the file is `xhcipm.inf`) and `:351`, `:1195` use untagged `ExAllocatePool`.
- Suggested fix: as each line says.

### T10. xhciqual: smaller defects
- Severity: Low
- Where and what: `xhciqual/verdict.c:194-196`, `:255-257` (`--irq-selftest` with a static disqualifier closes "IRQ SELF-TEST FAILED" although every gate passed); `xhciqual/legacy.c:645-646`, `:720-721` ("ESC to skip" while any key skips; `bringup.c:924-931` was fixed); `xhciqual/bringup.c:25-33`, `qual.h:766-769` (`ticks_now`/`ticks_to_ms` dead); `xhciqual/qual.h:1090`, `xhciqual/sysasm.asm:6` (cite a git-ignored memory id `lesson-cpuid-ebx`; the tracked record is `docs/contributing/lessons.md` "MSVC 6.0 inline __asm CPUID clobbers EBX").
- Suggested fix: as each line says.

### T11. xhciqual docs vs code
- Severity: Low
- Where and what: `xhciqual/hardware-testing.md:127-128` says the quick scan "ends with the next command to run" (README `:319-320` and `main.c:896-918` say it does not); `hardware-testing.md:152` quotes the QUALREP command without the `--done-flag QUALREP.FLG` that `QUALREP.BAT:15` passes and lines 173-177 rely on; `xhciqual/README.md:292-295` lists five `check-build-exit-codes.ps1` cases, the script runs six (`:958-967`, `:1008`).
- Suggested fix: correct the three passages.

### T12. Review-process wording in xhciqual sources and scripts
- Severity: Low (decided; see D-H3: sweep these too)
- Where: `xhciqual/stamp.h:52`, `sysprobe.c:82`, `:406`, `verdict.c:101`, `mmiodiag.c:261`, `bringup.c:519`, `:665`, `:679`, `sysinfo.c:165`, `:347`, `:360`, `:369`, `test/test_stamp.c:289`, `test/test_mmiodiag.c:485`, `:566`, `test/test_sysinfo.c:125`, `:828`, `:895`, `:941`, `:965`, `:1078`, `:1139`, `:1292`, `:1334`, `test/run-win98-batch.ps1:538`

## 5. User-mode tools (xhciinst, xhcicln, xhcisnap, xhcitr)

### U1. xhcisnap/README.md names the review tool and review rounds
- Severity: Medium (a tracked doc; the rule applies without question)
- Where: `xhcisnap/README.md:153`, `:312`, `:364`; also `xhcisnap/snapon.bat:20`, `snapget.bat:24`, `selftest.cmd:15-16` ("finding 6", "the audit's C4") and about 25 comments in `xhcisnap/xhcisnap.c` (e.g. `:269`, `:715`, `:757`, `:760`, `:1617`, `:1850`, `:4331`, `:5994`)
- Suggested fix: "reviewed and revised" or drop the parentheticals, and sweep the `.bat`/`.cmd`/`.c` comments in the same pass (owner, 2026-10-18: the rule covers source and script comments; D-H3). Note 42-A.4 retires XHCISNAP; if the tree is deleted the README item goes with it.

### U2. xhcitr.manifest carries a literal version that nothing derives or gates
- Severity: Low
- Where: `xhcitr/xhcitr.manifest:10` (`version="3.0.0.0"`); `src/xhci_version.h:5-9` lists "four other sites" and omits `xhciinst.c:49`, `xhcicln.c:84`, `xhcitr.c:46`, `xhcitr.rc:9` and the manifest; no script greps `assemblyIdentity`
- Problem: the 3.1.0.0 bump will leave the manifest at 3.0.0.0 silently.
- Suggested fix: drop the `version` attribute (informational for an unregistered assembly) or add it to `check-inf.ps1`'s comparison; update the consumer list in `xhci_version.h` (see also D-G8).

### U3. xhciinst.c imports MoveFileExA statically in two places while the rest looks it up at run time
- Severity: Low
- Where: `xhciinst/xhciinst.c:4853`, `:7458`; contrast `:2442-2449`, `:2706-2708` ("98 SE and ME have no such call")
- Problem: loads on 98 only because 9x's KERNEL32 exports a failing stub; `check-imports.ps1` checks module names, not functions. Both sites are on NT-only paths.
- Suggested fix: route both through the existing `PFN_MOVEEX` lookup.

### U4. INSTALL.BAT refuses a ninth argument but says "nine at most"
- Severity: Low
- Where: `xhciinst/INSTALL.BAT:35-36`, `:44-46`; `README.TXT` does not mention the limit
- Suggested fix: say "eight at most" and add a line to README.TXT.

### U5. XHCICLN's console text does not follow the 79-column / "Enter for [x]" rule
- Severity: Low
- Where: `xhcicln/xhcicln.c:1352-1354`, `:1420-1428`, `:1489-1493`; `xhcicln/README.TXT:49-52`; the rule `xhciinst/inst_core.h:346-357`
- Decision (owner, 2026-10-18): bring it under the rule. Reuse `inst_wrap_text`/`inst_yesno_question` from `inst_core.c`, inside 42-A.2 or 42-A.3 where the tool set is reworked anyway; a selftest case for the wrap.

### U6. Record 23 section 9.4 still places the 98 SE/ME package copy under %windir%\XHCI98
- Severity: Low
- Where: `docs/contributing/design/23-install-upgrade-tool.md:1530-1534` vs `:1639` (section 9.6, the install directory) and `xhciinst/xhciinst.c:4216-4228`, `:7101-7120`
- Suggested fix: rewrite 9.4 step 1 as "the package copy in the install directory (section 9.6) and any earlier build's copies under %windir%\XHCI98".

## 6. Scripts, INFs, version stamping, git configuration

### S1. The documented version-bump procedure names two INFs; eight carry a DriverVer
- Severity: Medium
- Where: `releases/README.md:447-450`, `docs/contributing/build-and-test.md:301` ("both src\xhci98.inf and src\xhci98-amd64.inf"); the UAS and CM INFs and `scripts/inf-gate/check-uas-inf.ps1:161-171`, `check-cm-inf.ps1:296-306` (rule BOTH-VERSION) hold each to the header
- Problem: the next cut (3.1.0.0) following the written step leaves six INFs stale; the build then fails at the gates rather than shipping, but the authority text is wrong.
- Suggested fix: say every `*.inf` under `src/`, `src/uas/` and `src/cm/` carries the literal, and name the two extra gates.

### S2. Four tracked `.labels.txt` files carry a UTF-8 BOM, written by a generator that disagrees with itself
- Severity: Low
- Where: `scripts/vm-matrix/gen-offsets.ps1:356` (ascii) vs `:366` (utf8), `scripts/vm-matrix/selftest.ps1:1106` (ascii); `scripts/vm-matrix/offsets.labels.txt`, `offsets-amd64.labels.txt`, `offsets-hcd.labels.txt`, `offsets-hcd-amd64.labels.txt`; `scripts/check-source-charset.ps1:42-47` says "three" files carry a BOM and lists one
- Suggested fix: `-Encoding ascii` at line 366, regenerate the four files, update the gate's comment.

### S3. .gitignore gaps
- Severity: Low
- Where: `.gitignore:239-240` (`build*.wrn` only under `src/` and `src/uas/`; `src/cm/` and `src/hlpr/` write them too), `:60-74` (`test/pmclient/build.cmd` is tracked but matched by `*.cmd` with no negation)
- Suggested fix: add `src/cm/build*.wrn`, `src/hlpr/build*.wrn`, `!test/pmclient/build.cmd`.

### S4. .gitattributes names a script that no longer exists and carries review-round wording
- Severity: Low
- Where: `.gitattributes:17-20` (`scripts/make-usbport-lib.cmd`), `:66-68` ("review of 38.2, round 1" naming the tool)
- Suggested fix: drop the dead name; cite the task id and date.

### S5. INF details
- Severity: Low
- Where and what: `src/xhci98.inf:256-258`, `:287-288`: comment calls `0x00010000` DELFLG_IN_USE (it is DELFLG_IN_USE1; behaviour unaffected); `src/xhci98.inf:303-307`: `[DefaultInstall.NTx86]` omits `XhciHlpr.CopyFiles` that `[Xhci.Dev.NTx86]` (`:150-152`) copies, so a right-click install drops no helper (no service is installed by that route either).
- Suggested fix: fix the constant name; add the CopyFiles or state the right-click sections are deliberately driver-only.

### S6. build-driver.cmd header describes a two-file, two-INF build
- Severity: Low
- Where: `scripts/build-driver.cmd:116-117` ("two files and nothing else"), `:154-161` ("BOTH production INFs"); the script builds four drivers and runs eight INF gates (`:359-435`); the numbered list at `:9-80` is current
- Suggested fix: reword the two paragraphs.

### S7. The built cm binaries under `src/cm/objfre` are older than the source
- Severity: Low (informational; nothing ships from there)
- Where: `src/cm/objfre/*/xhcicm.sys` dated 2026-10-15, `src/cm` last changed 2026-10-17; the binaries lack `IoRegisterDeviceInterface`, `IoCreateSymbolicLink`, `IoDeleteSymbolicLink`, `DbgPrint` that the allowlists mark required and the current `cm_qryos.c:263,475,547`, `cm_log.c:97` call
- Suggested fix: rebuild before the next gate run; no tree change.

## 7. Documents - top level and user-facing

### D-T1. LICENSE scope note contradicts the tree on copied code, the tool set, the driver list and txtsetup.oem
- Severity: High
- Where: `LICENSE:1-2`, `:392-399`, `:401-423`, `:424-446`, `:457-459`
- Problem: says "No function body from any of them is copied into src/" while `src/cm/tbt_u4cm.c` carries `SPDX-License-Identifier: GPL-2.0-only` with Linux and `usb4-emu` copyright lines (AGENTS.md:639-657, README.md:409 admit it); names only XHCIQUAL and XHCISNAP as statically linked tools (3.0.0.0 ships XHCICLN, XHCIINST and XHCITR pairs with NOTICE.TXT under `tools\`); omits `xhcicm.sys` from the driver list; says the txtsetup.oem files name no Microsoft file (the 32-bit one names `usbd.sy_`, `hidclass.sy_`, `hidparse.sy_` since 2.4.0.0, `src/txtsetup.oem:64`). AGENTS.md:720 requires the licensing claims in LICENSE to be kept true.
- Suggested fix: rewrite the scope note: name the Thunderbolt/USB4 exception (adapted GPL-2.0-only code in `src/cm/` with notices kept, per legal-provenance 2a), list all five tool pairs and their `tools\` location, add `xhcicm.sys`, say the 32-bit `txtsetup.oem` names three Windows 2000 CD files without shipping them.

### D-T2. README.md and AGENTS.md licensing lists omit XHCITR.EXE and XHCITR64.EXE
- Severity: Medium
- Where: `README.md:406-414`, `AGENTS.md:693-696`; `releases/3.0.0.0/tools/xhcitr/NOTICE.TXT`
- Suggested fix: add the pair to both lists. Same gap in legal-provenance 2a: see D-G4.

### D-T3. AGENTS.md repository layout omits xhcitr/, xhcicln/, temp/, src/cm/ and src/hlpr/
- Severity: Medium
- Where: `AGENTS.md:212-280`; `docs/README.md:206-208` relies on the block for "everything tracked is reachable"; README.md:357-374 already lists `cm/`, `xhcicln/`, `xhcitr/`
- Suggested fix: add `src/cm/` and `src/hlpr/` to the `src/` entry, rows for `xhcicln/` and `xhcitr/`, and `temp/` (git-ignored scratch) to the block and the "tracked except" sentence at line 272.

### D-T4. releases/README.md describes a package schema and upload set that no longer exist
- Severity: Medium
- Where: `releases/README.md:98-149` (schema "as a cut produces today": two files per flavour, tools at the root), `:138-139` ("adds nothing" to the upload; Stor98 is bundled download-only, `make-release.ps1:334-346`), `:141-144`, `:165-171`, `:226-229`, `:437-441` ("two files"), `:367-369` (x64 "never real hardware"; the E7250 since 2.3.0.0), `:451-455` (only two tools expand the version macro; five do)
- Suggested fix: redraw the schema from `releases/3.0.0.0/`, fix the five sentences.

### D-T5. The release acceptance test still expects the 2.0.0.0 package and Device Manager tree
- Severity: Medium
- Where: `docs/using/release-acceptance-test.md:152`, `:154`, `:159-160`, `:169-170` (file lists; no `tools\xhciinst\`, `tools\xhcitr\`, `xhcicm.*`, `nt\`), `:259-261` ("hubs appear nowhere in Device Manager"; they have since 2.1.0.0), `:362` (composite expectation; since 2.3.0.0 a composite has its own entry), the `-SkipCleanTool` sentence in 2.9 belongs to 2.8
- Suggested fix: cite the generated readme's section 8 list; rewrite the hub and composite rows.

### D-T6. The acceptance test says the x64 package is unsigned and needs F8 at every boot
- Severity: Medium
- Where: `docs/using/release-acceptance-test.md:56`, `:243`, `:429` (7.20 expects Code 39 on an ordinary boot); the binaries are test-signed since 2.3.0.0 (AGENTS.md:587-601, README.md:220-232)
- Suggested fix: rewrite row 2, 4.10 and 7.20 to the test-signing procedure, keeping F8 as the one-boot alternative.

### D-T7. Release notes contradict themselves on whether 3.0.0.0 ran on real hardware
- Severity: Medium
- Where: `docs/using/release-notes.md:23-25` ("on no real machine yet") vs `:267-272`, `:555-557` (the P14s Gen 1 under 98 SE); `releases/history.md:18-21` is a cut entry and stays
- Suggested fix: say the Thunderbolt 3 half was read on the P14s Gen 1 under Windows 98 SE and everything else in virtual machines.

### D-T8. Release notes "Installing" still says each flavour directory holds six files
- Severity: Medium
- Where: `docs/using/release-notes.md:1257-1266`; `releases/3.0.0.0/release-x86/` holds ten entries and the download adds the Stor98 files (`:1912-1918`)
- Suggested fix: recount for 3.0.0.0 and name `xhcicm.inf`, `xhcicm.sys` and `nt\`.

### D-T9. Smaller release-notes and README slips
- Severity: Low
- Where and what: `release-notes.md:120-121` "supported in virtual machines only for 2.4.0.0" under 3.0.0.0 notes; `release-notes.md:119-126` and `README.md:295-296` real-hardware summaries omit the Lenovo B490 (`release-notes.md:2246-2264`, `:2859-2861`, `README.md:301`); `release-notes.md:2514-2515` "INSTALL.BAT asks about eight of these values" (seven are asked, four of them controller values: `xhciinst/README.TXT:112-121`, record 23 section 8.1); `docs/README.md:206-229` "Four trees" lists six and omits `xhcitr/README.TXT`; `docs/README.md:31` "Phases 39 and 40" (the file holds 39-42), `:221` cites task 40.5 (renumbered 40.4 on 2026-10-16; `:78`, `:180` already say 40.4).
- Suggested fix: as each says.

### D-T10. Acceptance test: 8.7 expects XhciLogVerbosity 0 and substep 7.9 is duplicated
- Severity: Low
- Where: `docs/using/release-acceptance-test.md:462` (the INFs write `XhciLogVerbosity` 2 since 3.0.0.0, `src/xhci98.inf:94,196`), `:397` and `:403` (two rows numbered 7.9; the equipment row at `:56` assigns ME 7.7-7.8 and XP 7.9-7.12)
- Suggested fix: "XhciLogVerbosity 2 and the other eleven 0"; renumber the ME row.

### D-T11. tuning.md claims every value but omits eleven XhciCm* values
- Severity: Low
- Where: `docs/using/tuning.md:3`, `README.md:290` (contradicts itself within one paragraph); the values `src/cm/cm_reg.c:21-39` reads, defaults `src/cm/cm_vals.c:21-34`
- Suggested fix: add the `XhciCm*` table (Mode 1 (0-3), Approve 1, Reenum 1, PollIdleMs 200 (50-5000), CmdTimeoutMs 5000 (1000-60000), ApproveTimeoutMs 10000 (1000-60000), SafeEject 1, LogVerbosity 2, Usb3Tunnel 1, LaneBonding 1, PreOsCleanup 0 (0-2), LogDebugView 0, TunnelBridge, BridgeNumbering 1) or narrow README's sentence.

### D-T12. The GitHub issue template does not know 3.0.0.0
- Severity: Low
- Where: `.github/ISSUE_TEMPLATE/bug_report.yml:38` (placeholder "e.g. 2.0.0.0"), `:59-66` (no 3.x update option), `:145-196` (XHCISNAP route only; no XHCITR Save Report / `/report`, no `XHCIINST.LOG`, no Thunderbolt/USB4 controller id)
- Suggested fix: update when 42-A.4 rewrites the bug-report steps around XHCITR (the task already names the issue form).

### D-T13. VL805 wording
- Severity: Low (decided)
- Where: `docs/using/release-notes.md:1040-1042`, `:3140`, `:3849`, `:3857`; `docs/using/tuning.md:61` ("has never run on a VL805")
- Problem: the owner confirmed on 2026-10-13 that the VL805 works on 2.3.0.0 and asked that the lines not be rewritten until details are known.
- Decision (owner, 2026-10-18): change each "has never run on a VL805" to "reported working on 2.3.0.0 by the owner, not yet read in detail" and keep the VL805 in "Untested ground" with that note; move it out only when a run sheet carries the reading.

## 8. Documents - roadmaps, design records, run sheets

### D-R1. roadmap-hcd.md and roadmap.md still say Phase 37/38 is open and 2.3.x is the latest cut
- Severity: Medium
- Where: `docs/contributing/roadmap-hcd.md:16` ("2.3.0.0 is cut, and Phase 37 is open"), `:799`, `:840` (Phases 37 and 38 "Status: open", every task ticked, `2.3.1.0` cut 2026-10-13 and `2.4.0.0` cut 2026-10-14); `docs/contributing/roadmap.md:129-150` ("Phase 38 is open", cuts listed to 2.3.1.0); `AGENTS.md:290-297` never names the open phase or 3.1.0.0
- Suggested fix: close the two phases with dates, extend `roadmap.md`'s "Since then" to 2.4.0.0, 3.0.0.0 and the open Phase 42 / 3.1.0.0, add a line to AGENTS.md.

### D-R2. roadmap-usb4.md: Phases 39, 40 and 41 are "Status: open" while the file's status line says closed
- Severity: Medium
- Where: `docs/contributing/roadmap-usb4.md:20` vs `:120`, `:214`, `:256`
- Suggested fix: "Status: closed 2026-10-17 on the 3.0.0.0 cut" (keeping the open-after-cut 39-E.1 bench note).

### D-R3. 40-V.1 does not record the post-cut reading run-41.md holds, inside a closed phase
- Severity: Medium
- Where: `docs/contributing/roadmap-usb4.md:233` (`[ ]`, "U.4, U.7, U.9 and U.11 to U.13 owed"), `:240-243`; `docs/contributing/runs/run-41.md:1544-1566` (2026-10-17: U.4, U.7, U.9, U.12, U.13 pass on the signed asset; U.11 open, the reason for 42-A.6 / 42-V.2)
- Suggested fix: append the reading to 40-V.1 and tick it or state the closure; say U.11 is carried to 42-A.6.

### D-R4. Planned-work ids retired by Phase 42 are still cited as the current plan
- Severity: Low
- Where: `docs/contributing/design/25-usb-tree-tool.md:150`, `:677-678` (SNAP.1-3, EJRB.1), `design/21-thunderbolt3-icm-client.md:2352`, `:2382` (NV9X.2/3), `:2521` (EJRB.1), `design/22-usb4-software-connection-manager.md:2050` (NV9X.3), `design/24-thunderbolt-device-tool.md:14` (EJRB.1), `docs/issues/README.md:95`, `docs/issues/17-9x-tunnel-bridge-chain-code-29.md:198,220,236` (NOIRQ.1), `docs/contributing/legal-provenance.md:411` (STOR.1, NV9X.1); `roadmap-usb4.md:231` (40-A.8 "the 2000 leg is EMU.1's": EMU.1, done, is in neither the closed list at line 89 nor anywhere else)
- Suggested fix: SNAP.1-3 -> 42-0.3/42-A.4, NV9X.2 -> 42-0.7/42-A.12, NV9X.3 -> 42-A.13, EJRB.1 -> 42-0.4/42-A.5, NOIRQ.1 -> 42-0.6/42-A.15, STOR.1/NV9X.1 -> the run-41 record; add EMU.1 (done 2026-10-17) to the widened row.

### D-R5. Design index and roadmap disagree with the records on decision counts and revisions
- Severity: Low
- Where: `docs/contributing/design/README.md:57` (record 22 "U1 to U14"), `roadmap-usb4.md:220` ("U1-U16"), the record's table ends at U17 (`22-...md:2472-2474`); `design/README.md:58` (record 23 "revision 1"; it is at revision 10), `:59` (record 24 "revision 4"; it is at 5)
- Suggested fix: U1 to U17; refresh the two revision numbers.

### D-R6. Design record 23 has no "## 9." heading although "record 23 section 9" is cited
- Severity: Low
- Where: `docs/contributing/design/23-install-upgrade-tool.md:1397-1399` (8.5 then 9.1); `roadmap-usb4.md:331` (42-A.3 "record 23 section 9 revised first")
- Suggested fix: insert "## 9. The uninstall" before 9.1 (42-A.3 revises the section anyway).

### D-R7. Roadmap-usb4 checkbox and closure slips
- Severity: Low
- Where: `docs/contributing/roadmap-usb4.md:267` ("[x ]" on 41-V.1); `:156` (39-A.7 ticked, "XP x64 and ME legs owed"), `:230` (40-A.7 ticked, "the Windows 2000 guest leg on usb4-generic owed"); neither says where the owed legs went (the per-port waits are 42-A.12; the legs are named in no Phase 42 task)
- Suggested fix: fix the checkbox; add a closing note to each naming the run or the Phase 42 task that carries the leg.

### D-R8. Relative date and section order in record 25
- Severity: Low
- Where: `docs/contributing/design/25-usb-tree-tool.md:12` ("the night before"); section 6.6 (line 782) precedes 6.5 (line 835)
- Suggested fix: "on 2026-10-15"; swap or renumber the two sections.

## 9. Documents - contributor guides, issue records, spec transcriptions

### D-G1. source-files.md's main tables still describe the deleted miniport
- Severity: High
- Where: `docs/contributing/source-files.md:463-469`, `:479` ("test_init.c compiles every DDK-side file"), `:481-525`, `:549-550` (`usbport.lib` "generated by scripts\make-usbport-lib.cmd", which does not exist)
- Problem: the three "what every file is for" tables list `xhci_dispatch.c`, `xhci_rh.c`, `xhci_slot.c`, `xhci_probe.c` as the DDK side; not one `hcd_*.c` has a row; the HCD is described only in the 465-line prose preamble. The page's stated purpose is "one table per kind" and it maps the wrong driver.
- Suggested fix: replace the tables with the HCD's map (one row per tracked file in `src/`, `src/uas/`, `src/hlpr/`, `src/cm/`, `test/`), drop the usbport.lib row, reduce the preamble to a history pointer; a build-time diff of the table against `git ls-files src` would keep it current.

### D-G2. architecture.md describes a usbport miniport with no banner saying the HCD replaced it
- Severity: High
- Where: `docs/contributing/architecture.md:5`, `:28`, `:49-57`, `:61-154`, `:199-267`, `:271-305`, `:333-344`; the one HCD note is at `:309`
- Problem: AGENTS.md:363-367 says the page is "on the 1.2.0.0 branch's terms", but the page never says so and is the first file a reader opens for "architecture".
- Suggested fix: the banner `source-files.md` and `build-and-test.md` carry, or a rewrite for the HCD with the miniport text moved to a 1.2.0.0 appendix.

### D-G3. build-and-test.md's "Building" section still routes the build through usbport import libraries
- Severity: Medium
- Where: `docs/contributing/build-and-test.md:34` (`MSVC6` redirects `make-usbport-lib.cmd`), `:260-291` (a printed `src/sources` with `xhci_dispatch.c` and `TARGETLIBS=usbport.lib`; the real file has none), `:388-406` ("run make-usbport-lib.cmd before the first build"), `:464-470` (`-amd64` selects `usbport_amd64.lib`), `:603` (gate input `scripts\usbport-lib\usbport-imports.expected`, absent); the banner at `:3-12` says all of it is gone
- Suggested fix: delete the usbport paragraphs and the TARGETLIBS lines, correct the `-amd64` and `MSVC6` sentences, drop the gate-input row.

### D-G4. legal-provenance.md section 2a omits XHCITR.EXE and XHCITR64.EXE
- Severity: Medium
- Where: `docs/contributing/legal-provenance.md:144-153` ("from 3.0.0.0 six"; eight are tracked under `releases/3.0.0.0/tools`), `:303-335`; section 6's own rule gives each new tracked build output an entry
- Suggested fix: an XHCITR subsection on the XHCIINST pattern; count eight.

### D-G5. implementation-invariants.md keeps miniport rules the HCD does not follow
- Severity: Medium
- Where: `docs/contributing/implementation-invariants.md:1291-1300` ("every root port reported to usbport as High Speed"; `XhciPortShadowReport` has no caller, `hcd_enum.c` enumerates at the decoded speed), `:504` (`xhci_slot.c`), `:518-520` (`XHCI_RECOVERY_DELIVERY_POLLS` "unconditional"; used by no `.c`), `:579` (Option A SG contract), `:1580-1585` (`xhciRhRefresh`, `xhci_rh.c`)
- Suggested fix: mark the rules 1.2.0.0 history or delete them, and remove the dead code they describe (X14).

### D-G6. docs/issues index and pages report stale states
- Severity: Medium
- Where and what:
  - `docs/issues/README.md:90`, `docs/issues/12-e7250-xp64-boot-enable-slot-refused.md:3-5`: "Open, root cause found. Fix planned" (36.3, 36.4, 36.6-36.8, 36.15 shipped in 2.3.0.0, read on the E7250; `roadmap-hcd.md:715-727`).
  - `docs/issues/README.md:44-53`, `:84`, `docs/issues/06-full-speed-root-port-bugcheck.md:3-31`: issue 6 "open, fix being built as 24.3"; the bugcheck is inside usbport, moot from 2.0.0.0, and `xhci_vhub.c` was deleted 2026-10-02.
  - `docs/issues/README.md:87`: issue 9 "legs on the 2.1.0.0 package owed"; the page (`09-...md:3-22`) says they passed on the integration build.
  - `docs/issues/README.md:95-96` vs `docs/issues/18-p14s-gen1-tb3-nvme-hot-plug-98se.md:3-9`: index says the merged build's recheck is owed; the page says it was read by the owner on 2026-10-16.
- Suggested fix: close issue 12 as fixed in 2.3.0.0 (noting anything still owed), issue 6 as moot from 2.0.0.0, align the issue 9, 17 and 18 rows with their pages.

### D-G7. Miniport-era reference documents cite deleted files as present, with no banner
- Severity: Medium
- Where: `docs/usb-xhci-info/usbport-miniport-abi.md:1-14`, `:198`, `:404`, `:1451`, `:1787`, `:3035`, `:3063`; `docs/usb-xhci-info/usbport-miniport-interface.md:1-14`, `:152`, `:458`, `:633-634`, `:731`; `docs/contributing/passthru-snapshot-instrument.md:31-33` ("Where the code lives": `xhci_dispatch.c`, `test_init.c`; the door is `hcd_door.c`), `:97`, `:202`, `:250`, `:259`; `docs/contributing/failure-diagnosis.md:130`, `:211-245`, `:274`; `docs/usb-xhci-info/win98-wdm.md:623`, `:629`; `docs/usb-xhci-info/xhci-programming.md:636-642` ("reported to usbport as High Speed"), `:667` (code 34 "this driver opens no streams")
- Suggested fix: a one-paragraph banner naming branch 1.2.0.0 on each of the five, and fix the three live claims in place.

### D-G8. source-files.md's tool and header lists are incomplete
- Severity: Low
- Where: `docs/contributing/source-files.md:358-374` (xhcitr list omits `tr_pci.c/.h`, `tr_log.c/.h`, `tr_hcctr.h`, `check-imports.ps1`; `test_trlog.c`, `test_trpci.c` reachable only via the `src\cm` table), `:525` (`xhci_version.h` consumers: eleven more files include it), `:164` (`hcdCommandBody`, no such symbol); headers `xhci_enum.h`, `xhci_func.h`, `xhci_link.h`, `xhci_pipe.h`, `xhci_sshub.h`, `xhci_vhub.h` appear nowhere
- Suggested fix: with D-G1.

### D-G9. Smaller guide and issue slips
- Severity: Low
- Where and what: `docs/issues/12-...md:72`, `:488`, `docs/contributing/design/15-polled-event-delivery.md:151`, `:439`, `:1333` cite `hcdBackstop` (now `HcdCtlBackstopTick`, `src/hcd_ctl.c:2167`) and `hcdCommandBody`; `docs/issues/11-sunrise-point-ssic-psi-table.md:38` backticks `XhciMissingCas` as if it existed (never written); `docs/issues/README.md:80-95` link text differs from eleven pages' H1 titles; `docs/usb-xhci-info/xhci-data-structures.md:69` says only HCCPARAMS2 bits 0-2 are used while `:1714-1719` and `src/xhci.h:433` use LEC (bit 4).
- Suggested fix: as each says.

### D-G10. Non-ASCII bytes and unbalanced line endings in tracked docs
- Severity: Low
- Where: `docs/contributing/passthru-snapshot-instrument.md:43`, `:59`, `:83`, `:149`, `:328`, `:478`, `:480` (U+00A7 section sign); `docs/contributing/design/05-locking-model.md:154-156` (box-drawing characters); fourteen `docs/contributing/runs/run-{16,19,20,22,23}-post-release/*.txt` (UTF-8 BOM); `docs/contributing/runs/run-13e-evidence/l3a1.txt`, `l3a2.txt`, `l3d2.txt` (more CR than LF); the four `.labels.txt` of S2
- Problem: AGENTS.md's rule is ASCII in tracked text; `scripts/check-source-charset.ps1` does not cover `docs/`.
- Suggested fix: "section" for the sign, an ASCII diagram, strip the BOMs, normalise the three transcripts (or say in `run-13e-evidence/README.md` that the stray CRs are evidence); consider extending the gate to `docs/`.

### D-H3. The review tool's name, "review round N" and "finding N" across tracked text (owner)
- Severity: Medium as policy, Low per site
- Where: about 190 lines naming the tool, about 600 "round N" and about 170 "finding N" lines: `README.md:386` (a deliberate credit line), `docs/contributing/design/README.md:47-56`, `docs/contributing/legal-provenance.md:1157-1187`, `docs/contributing/runs/run-20,24,26,27,28,29,30,31,34,35,36,37,38.md`, `run-13e.md`, design records 05, 08, 12, 13, 15, 17, 18, 19, 20, `docs/contributing/lessons.md`, `build-and-test.md:4053`, `:6760`, `:8001`, `roadmap-hcd.md`, `roadmap.md:100`; plus the source and script sites of C3, T12, U1, S4
- Problem: the owner's rule of 2026-10-14 (repo memory `feedback-no-review-round-mentions-in-docs`) says tracked docs never say these; AGENTS.md does not state the rule, and nearly all of the text predates it. Whether the rule is retroactive, and whether it covers source comments, is the owner's decision.
- Decision (owner, 2026-10-18): the split sweep. Write the rule into AGENTS.md; sweep the design records, legal-provenance, the guides, the roadmaps and every source and script comment (C3, T12, U1, S4), replacing each site with the commit, date, task id or run section the text already carries ("reviewed and revised" at most). Leave the dated run sheets (`runs/run-*.md`, `run-13e.md`) and README.md's credit line as they are.
