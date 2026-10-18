# Plan

Status 2026-10-18: 0.4.0.0 is the current release (2026-10-16; 0.3.0.0 and 0.3.0.1 came
before it, [releases/history.md](../releases/history.md)). The phase 0.0 gates and the 9x
and NT qualifications ran where the test VMs allow, and every maintainer decision this
plan names is decided: MD-1 to MD-7 and D1 to D5 ("Maintainer decisions"). Open: 4 KiB-sector
namespaces (K3, planned for the release after 0.4.0.0); XP x86, Vista and 7 x86 (no test
VM); measurements on real hardware (Flush and SHST latency, MD-2's verification); and, on
98 SE / ME, the link-down statement and the clean-eject handshake with xhci98, which
xhci98 3.0.0.0 and 3.1.0.0 do not make there. The status lines in the sections below are dated and
record the state when they were written.

## Starting point

Base: SweetLow's nvme9x/nvment (port of nvme2k 1.0.0.2; Windows 9x, NT4 and 2000 targets
from one tree), BSD-3-Clause per SweetLow (2026-10-14). Reference for backports: upstream nvme2k 1.2.0.0
(commit `0c41727`), which adds an amd64 target and several fixes.

Scope: Windows 98 SE, ME, 2000 SP4, XP, Vista and 7, 32- and 64-bit. **Windows NT4 is
outside the product scope.** The inherited NT4 build path (`NVME2K_NT4`) stays only while it
costs nothing to keep compiling; it is not tested, validated or shipped, and any item below
may drop it (R2-12).

Evidence behind this plan:

- Static reviews: [reviews/review-nvme9x.md](reviews/review-nvme9x.md) (the base) and
  [reviews/review-nvme2k-1.2.0.0.md](reviews/review-nvme2k-1.2.0.0.md) (upstream). File
  references are relative to each source tree. Line references into `src/` in this plan
  are to the tree at commit 1784969; commit 035bc30 added a 30-line licence header to every
  file there, so add 30 for the current tree.
- Plan reviews by Codex, 2026-10-14: round 1 (C1-C16), round 2 (R2-1 to R2-12), round 3
  (R3-1 to R3-14, reviewing 1784969) and round 4 (R4-1 to R4-7, reviewing 702834c), folded
  in below and cited where they apply, round 5 (H1, M1, M2) and round 6 (R6-1 to R6-9,
  reviewing the phase 0.0 results, 2026-10-15). The finding tags cited in this plan (C,
  R, H, M, P and N numbers) refer to the maintainer's review notes, which are not in the
  repository. The maintainer's calls
  are recorded under "Maintainer decisions" at the end (MD-1 and MD-2 decided 2026-10-14; `CachesData` TRUE
  on NT, 9x and Me, MD-3 to MD-7 decided 2026-10-15).
- Emulator runs (usb4-emu, 2026-10-14), full results in usb4-emu `docs/NVME-DRIVER-EVAL.md`
  and `vms/*/RESULTS-*.md` (local):
  - Windows 98 SE, nvme9x: FAT16/FAT32 I/O correct through a USB4 PCIe tunnel on 512-byte
    namespaces; a 4 KiB namespace gets 4 KiB transfers for 512-byte OS sectors and Windows 98
    hangs at startup.
  - Windows 2000, nvme2k: works on 512-byte namespaces; surprise removal during I/O hangs;
    4 KiB namespace: STOP 0x1E. Safe removal: Device Manager Disable, then tunnel down.
  - XP x64, nvme2k 1.2.0.0 amd64: works on 512-byte namespaces, boot-present works; surprise
    removal followed by I/O wedges the volume indefinitely (event 117 bus reset, then event
    9 timeouts every 15 s; scsiport keeps retrying; re-plugging does not recover); 4 KiB
    namespace: 128 KiB DMA for a 16 KiB request, bugcheck 0x19 BAD_POOL_HEADER.
  - Re-enumeration (usb4-emu `docs/REENUM-EXPERIMENT.md`): the CM driver can make 98 and
    2000 enumerate and drop the tunnelled controller. stor98 must cope with being stopped
    and removed that way, and with surprise removal when it is not.

## Principles

- **Request engine first, features second.** Every later item (MDTS splitting, 4 KiB
  translation, barriers, removal) needs one SRB to own several NVMe commands safely. That
  engine, admission control and a DMA-safe reset/removal path come before any of them (C1,
  C2, C6).
- **Start serialized.** The first translating implementation runs one parent request per
  namespace at a time. Concurrency is added only after the overlap tests pass (C3).
- **Each item lands with an emulator test that failed before it.**
- **A buffer is released only when the device can no longer reach it.** Generation counters
  and CID tracking discard stale completions; they do not stop a device from writing into
  memory. Only the evidence listed in 0.10 does (R2-1). No path accepts a "residual risk"
  of DMA into memory that is freed or reused: if the evidence cannot be obtained, the memory
  stays owned and out of use (R3-1). All-ones reads, from MMIO or config space, are
  presence-loss signals, never that evidence (R4-1). **One exception, accepted by the
  maintainer (decision MD-1):** at the gone deadline without evidence, held SRBs are
  completed with the terminal status, so the callers' buffers go back to the OS, while
  driver-owned DMA memory is quarantined, never freed. No other path may complete or free
  without evidence.
- **Experiment before contract.** Where correctness depends on SCSIport or IOS behaviour
  that only an experiment can establish, that behaviour is a feasibility gate (0.0) with a
  pass condition and a stated consequence if it fails, settled before the contract that
  relies on it is written (R3-8).

## Dependencies

Phase 0 is ordered internally (R3-8):

1. 0.1 (build matrix including a minimal amd64 build) and the fault-injection harness
   (TESTING.md, harness work) in parallel.
2. 0.0, the feasibility gates: minimal throwaway prototypes on each port, driven by the
   harness, answer the questions in the gate table. Each gate records its result and the
   design it selects.
3. The contracts 0.10-0.12 are written down from the gate results (decisions, not yet
   product code).
4. 0.7-0.9 (state definitions, DMA negotiation, queue validation).
5. 0.2-0.6, implemented to those contracts.
6. 0.10-0.12 implemented on the engine; phase 0 exits when their tests pass.

A failed gate stops the items that depend on it for the affected OS until the gate's stated
consequence is applied. No item in phase 1 or later starts before phase 0 is
done, because they all issue I/O through the engine and rely on the state definitions.
Within phase 1, 1.7 (4 KiB translation) needs 1.1-1.6 and 1.9. Phase 2 implements the
transitions defined in 0.7 under the contracts of 0.10-0.12; it does not redefine them.
Product code that depends on the allocation, shutdown or removal contracts also waits for
the qualification items listed under 0.0 ("Qualification before dependent product code",
R6).
Phase 4 needs phase 2. Phase 5 is install, signing and full validation of an amd64 binary
that has built since 0.1 (R2-10).

## Phases

### 0. Foundations

| # | Item | Source |
|---|---|---|
| 0.0 | **Feasibility gates** (below): minimal prototypes on each port, with the harness's fault injection, answering the port and IOS questions the contracts depend on. Runs right after 0.1; its results fix the contracts 0.10-0.12. **Status 2026-10-15: complete on 2000 and XP x64, mostly on 7 x64; 9x partial** ("Phase 0.0 results" below, R6-8): G1 passes on 2000, XP x64 and 7 x64; G2, G4 and G7 (resume path, allocations and resume-failure semantics) on 2000, XP x64 and 7 x64; G3 with the link-state table (MD-6) and G6 on 2000 and XP x64; G8's 2000 shutdown failure is resolved by the restartable SHUTDOWN, demonstrated (Q-NT); G9 passes on 98 SE with QuickInstall's IOS (orderly stop shown only with the inherited shutdown code); G5 fails on 2000, XP x64, 7 x64 and Me (MD-3 (a) on NT, MD-4 on 9x) and G9 on Me (MD-5 open); still open: G3 on 9x, 9x G1/G4, G6 on 7 x64, and XP x86, Vista and 7 x86 | R3-8; R3-1 to R3-7 |
| 0.1 | Buildable target matrix from one tree: 9x (`.mpd`), NT x86 and a **minimal NT amd64 build** (compiles and links with WDK 7.1 `wnet`/`win7` amd64; intrinsics instead of `atomic.h` inline assembly, 9x VMM/PTE code excluded). CI-style build script per toolchain. Every later commit keeps all three building | AGENTS.md; R2-10 |
| 0.2 | **Parent/child request engine.** A parent (the SRB) owns its original buffer and length, fragment list, phase, outstanding-child count, first error, deadline, locks and resources. Each live CID maps to (child, controller generation); CIDs are no longer derived from `QueueTag` (on 9x `QueueTag` is a rolling request number and every SRB is untagged at depth 1, G9). The parent completes exactly once, after every child has completed or been safely quiesced (0.10); all copies and bookkeeping finish before `RequestComplete`, and the SRB is never touched afterwards. A failed fragment does not undo fragments already written. The parent also carries a **cancellation state** (0.10, "Parent cancellation"): once abort, reset or removal suppresses any required unfinished work, the parent can no longer complete with success. Final status comes from the parent error mapping below | C1, R2-4, H1; src/nvme2k.h:167, nvme2k_nvme.c:368, nvme2k_cpl.c:509 |
| 0.3 | **Admission control without blocking.** Driver-owned limits for admitted parents, live children, PRP pages and bounce buffers. Accepted SRBs wait in a bounded pending list and are advanced from synchronized completion/timer callbacks; `NextRequest`/`NextLuRequest` are withheld while capacity is exhausted. Alternative: before issuing any child, complete with `SRB_STATUS_BUSY`. Never return BUSY after partial execution, never hold a completed SRB, never use BUSY for removal. No sleeping, event waits or spinning in StartIo/ISR. **Hold-then-BUSY (G1, measured):** a request held with no child issued (bring-up, or capacity exhausted) is held for at most a fraction of its own SRB `TimeOutValue`, measured as elapsed time (the prototype used a quarter, counted in ticks), then completed `SRB_STATUS_BUSY` and held again when re-sent. Pure hold is unsafe: Windows 2000's bus-scan INQUIRY has a 4 s timeout and the port reset it at 6 s RDY. On 9x `TimeOutValue` is unusable (0 on 98 SE and pointer-like garbage on Me for TEST UNIT READY and READ CAPACITY, G9), so 9x uses a fixed hold bound. **The port's BUSY limit (Q-NT, 2000, XP x64, 7 x64):** each port re-issues an SRB completed `SRB_STATUS_BUSY` 20 times (21 BUSY completions), then fails it to its owner. The limit is a count, not a time (holds of 1.5-4 s gave the same count); the retries come about 1.56 s apart on 2000 and about 1.0 s on XP x64 and 7 x64, and the bus scan issues INQUIRY 3 times (63 BUSY in all). So one request can be carried for at most 21 x (hold + the port's retry delay); with a quarter-`TimeOutValue` hold that is about 33 s per INQUIRY issue on 2000, shorter than the largest CAP.TO (127.5 s). Therefore the hold uses most of the request's `TimeOutValue` (elapsed time, minus a margin; on 2000 about 3 s of the INQUIRY's 4 s, about 95 s per issue), the driver never completes BUSY more than 20 times for one SRB and fails the request itself before what would be the 21st BUSY, and the bring-up deadline stays CAP.TO, after which the request completes in the failed state. Not measured: the disk class driver's own retries on top of the port's limit for READ/WRITE. **As implemented (engine):** the hold is `TimeOutValue` minus half of it (at least 1 s), measured with the larger elapsed estimate (0.10, "Which clock, which bound"; half, not a quarter, because the estimate can still be off). Coverage of CAP.TO: the bus scan (INQUIRY 4 s on 2000, 3 issues) 21 x (2 s + 1.56 s) x 3 = about 224 s, above the largest CAP.TO (127.5 s); I/O behind a re-enable (after a restartable shutdown or a reset; READ/WRITE with the disk class driver's 10 s) about 138 s per issue on 2000 and about 126 s on XP x64 and 7 x64, so a CAP.TO above about 126 s there relies on the class driver's own retry (not measured). **9x measured (engine, 98 SE, 2026-10-15):** with RDY delayed 6 s at the bus scan, the INQUIRY held for `hold9x` (4 s, now measured with the OS clock) and completed BUSY is not re-sent by IOS: the disk is not found (a second scan after RDY finds it). Before the qualified clock the 9x hold ran on a TSC calibrated far too slow there (0.1x) and nominal ticks, which stretched it past such RDY delays by accident. **9x rule (Q-9x, implemented in review round 2):** on 9x the bus-scan INQUIRY held during bring-up is never completed BUSY: it stays held until bring-up succeeds (then it is replayed) or fails (then it fails as in the failed state); bring-up is bounded by CAP.TO. A `HwResetBus` during bring-up returns FALSE and completes nothing (the port calls it again about every 7.5 s and never completes the held SRB itself; completing the held INQUIRY with BUS_RESET after a TRUE return made Me's port fail `ScsiPortInitialize` and unload the adapter, Q-9x G1). Other requests held on 9x keep the fixed `hold9x` bound | C2, G1, Q-NT; nvme2k_scsi.c:35, nvme2k.c:54 |
| 0.4 | **Per-child PRP construction.** PRPs built from each child's buffer offset and byte count; validate address, contiguous length, page alignment and list capacity for every mapping. Bounce buffers and PRP list pages are carved from the **uncached extension**, the memory the port can translate with `ScsiPortGetPhysicalAddress(..., NULL, ...)` (pool and device-extension memory cannot be translated that way). **This rule is conditional on MD-3/MD-4 (R6-2):** if MD-3 option (b) is chosen, bounce buffers, PRP list pages and queues come from the driver-owned pool instead, their physical addresses are recorded at allocation, and `ScsiPortGetPhysicalAddress(..., NULL, ...)` is no longer their translation route (dump mode keeps the uncached extension, 4.1). Caller storage is translated with the SRB; `MapBuffers=TRUE` kept for CPU copies. Bounced edges and the direct middle are separate commands (PRP lists cannot concatenate them in general) | C4, R2-5; nvme2k_nvme.c:608 |
| 0.5 | Allocation separate from hardware init; allocations aligned against their **physical** address; lifecycle flags reset on every start. Allocations are reused only where gates G5 and G7 show the port preserves them (expected: power stop and resume); after a stop for removal or resource rebalance the driver assumes they may have been freed and reacquires (0.11). Memory the controller may still reach is never handed back to the port (0.10, stop path). **Measured (G5, G7, G9):** on 2000 and XP x64 a power stop preserves the device extension (not zeroed) and the uncached extension (same VA/PA, contents intact), so reuse across power applies on both, with the sub-allocator and lifecycle flags reset on every start (the base lacked this). A removal stop frees and reuses both within seconds, acknowledged or not (7 x64 the same, at once, Q-NT), and a surprise removal frees them with no callback; the driver image is unloaded with its last device. On 9x the `.mpd` is unloaded at every removal and its extensions freed. DMA-reachable memory stays in the port's allocations on NT (MD-3 (a), decided) and comes from a `_PageAllocate` pool on 9x (MD-4, decided, to be prototyped). **`ArgumentString` is NULL at resume** on 2000, XP x64 and 7 x64 (Q-NT): `ConfigInfo` values (`CachesData` above all) are constants, never derived from DriverParameter, and registry-derived tunables are parsed once and kept in the device extension (which survives resume), never re-parsed from a NULL `ArgumentString` | C4, N2, #8, #11, R2-2, R3-4, G5, G7, G9; utils.c:707 |
| 0.6 | **DMA-safe reset and abort.** Release a buffer only on DMA-isolation evidence (0.10). Do not reuse queues/CIDs until old completions are excluded. Invalidate child mappings, pending parents, locks and timers together | C6, #1; nvme2k.c:1290 |
| 0.7 | **Lifecycle state definitions** (moved forward from phase 2): starting, running, resetting, stopping, stopped, gone, failed. For each state: which callbacks may arrive, whether new SRBs are admitted, who owns outstanding SRBs, whether the timer may rearm. All-ones CSTS/MMIO (or config space, where read) is a presence-loss trigger into `gone`, not proof that DMA has stopped; `gone` holds SRBs until isolation evidence or the gone deadline (0.10). Absent hardware is distinguished from present-but-fatal (CSTS.CFS) and stuck hardware | C6, #7, R2-10, R4-1 |
| 0.8 | **DMA and transfer negotiation in `HwFindAdapter`.** Today `MaximumTransferLength` (256 KiB) and `NumberOfPhysicalBreaks` (511) are set unconditionally. Instead: keep any lower value the port supplied (registry `MaximumSGList` and similar), treat `SP_UNINITIALIZED_VALUE` as "no port limit"; set `Dma32BitAddresses`/`Dma64BitAddresses`, `Master`, `ScatterGather`, `AlignmentMask` and the limits **before** `ScsiPortGetUncachedExtension`, which fixes the DMA adapter from them; size PRP list pages for the worst-case buffer offset (a buffer of N pages at a non-zero page offset touches N+1 pages). Child splitting by MDTS (1.4) is independent of these port limits | R2-5; src/nvme2k.c:181, 196 |
| 0.9 | **Queue capability validation** (moved from 3.5): power-of-two queue sizes from a widened MQES; CAP.MPSMIN/MPSMAX against the host page size; doorbell stride and BAR range checked before the first doorbell write. Needed before any translated I/O | #14, R2-10 |
| 0.10 | **Recovery contract** (below), written from the gate results and implemented on the engine. Exit: the contract is written down with the measured port behaviour, and the bring-up-failure, delayed-RDY, failed-disable, stop-without-acknowledgement, presence-loss (including the gone-deadline outcome of MD-1), abort, parent-cancellation and reopen tests pass on 98 SE, 2000, XP, XP x64, 7 x86 and 7 x64 | R2-1, R3-1 to R3-4, R4-1, R4-3, R4-4, H1, MD-1; src/nvme2k.c:837, 1298 |
| 0.11 | **NT callback, state and resource matrix** (below), implemented for `HwAdapterControl` and `SRB_FUNCTION_SHUTDOWN`/`FLUSH` | R2-2, R3-5, R3-7; src/nvme2k.c:1118, 1334, 1351; nvme2k_nvme.c:744 |
| 0.12 | **Windows 9x IOS integration prototype** on 98 SE and ME: decide whether stock IOS and Configuration Manager behaviour is enough for removal and re-arrival, or whether a helper VxD is needed. Name the owner of each: IOS linkage block (ILB) access, DCB identity and lifetime, volume removal and re-arrival, callback teardown on unload. If a helper VxD is needed, its build target, INF install and load order become phase-0 work items. Its minimal form runs first as gate G9, which also settles 9x config-space access (3.4) and G5's memory lifetime on 9x. Feeds 2.7. **G9 result: on 98 SE no helper VxD is needed** (nor NVMEVSD.VXD): IOS, CONFIGMG and the stock port handle arrival, orderly removal, dismount, the open-file veto and re-arrival with the plain `.mpd`; no VxD build target, INF install or load-order item is added for 98 SE. Qualified (R6-8): the measured IOS was QuickInstall's updated set, not gold 98 SE (gold `SCSIPORT.PDR` measured, gold IOS *inferred* from static analysis); the orderly-removal stop ran the inherited shutdown code inside `HwAdapterState(FALSE)` (RDY=0 seen), so a bounded stop and a stop whose disable fails are not yet shown. Owners: IOS/CONFIGMG own DCB identity, lifetime and volume removal (only CONFIGMG `Remove_SubTree`/`Reenumerate_DevNode` removes a surprise-removed volume, driven by xhci98's CM or the user); the miniport owns failing requests fast. **Windows Me is blocked**: its port never calls a stop (`HwAdapterState(FALSE)`), at removal or at shutdown; its only shutdown notification is `SRB_FUNCTION_SHUTDOWN`, which arrives only with `CachesData` TRUE (measured). See MD-5 | R2-6, R3-8, R4-2, G9; src/sources (miniport only) |

#### Feasibility gates (0.0)

Each gate is a minimal prototype, not product code, run with the harness's fault injection
(TESTING.md) on 98 SE (ME where the 9x answer could differ), 2000, XP, XP x64 and 7 (x86
and x64), and Vista where the port could differ. A gate passes only on its stated
condition. Its result, including the measured port behaviour, is recorded under the gate
before the contract that depends on it is written. If a gate fails on a port, the stated
consequence applies to that port; no consequence is "accept the risk", apart from the
gone-deadline exception the maintainer accepted under MD-1 (G3's consequence). Throughout, QMP or
model-hook tunnel gating is the test stimulus; it is never evidence the guest driver can
rely on.

| Gate | Question | Pass condition | If it fails | Source |
|---|---|---|---|---|
| G1 Deferred bring-up | Can `HwInitialize` (and the resume path, G7) set CC.EN, return TRUE and finish bring-up (RDY, queue creation, identify, namespace selection) from the timer, holding requests or returning BUSY before any child is issued until it is ready? | On each port, with RDY delayed well past the callback budget and, separately, with identify failing: the bus scan completes (disk present, or no disk), no SRB times out into a port reset during bring-up, and a failed bring-up ends in state `failed` with the adapter started and its memory still owned | That port uses synchronous bring-up in `HwFindAdapter` and returns failure only before CC.EN was ever written, after an acknowledged disable, or after Bus Master Enable was cleared and read back (config writes are permitted there). If none of these fits the port's rules, the port is blocked pending a maintainer decision | R3-1 |
| G2 Gone-state hold | (Was "config-space presence"; withdrawn as an evidence gate in round 4: an all-ones config read is presence-loss evidence only, and NT runtime callbacks have no legal config access, R4-1, R4-2.) What does each port do while the miniport holds SRBs in `gone` up to and beyond the gone deadline (0.10): does the port's own SRB timeout fire, does it then call `HwResetBus` (G4), and does it retire the held SRBs itself before the OS removal path? | Measured and recorded per port with the tunnel gate closed and no G3 notification; the port never completes a held SRB before the OS has itself established removal | As G4's consequence | R4-1, R4-2 |
| G3 CM link-down notification | Can xhci98's CM tell stor98 that it has torn down the tunnel carrying the controller (so the PCIe link to the function went down, assumption A1 of 0.10), sent only **after completed quiescence** (the notification boundary in 0.10, evidence 2) and before the OS removal path starts? The sending side is owned by the xhci98 CM driver; stor98 owns the receiving interface | Two stages (R4-7). **Interface prototype:** the notification is delivered on 98 SE and on one NT port; interface named (9x: a VxD service or registered callback; NT candidate: `IOCTL_SCSI_MINIPORT` with an stor98 signature). **Per-port qualification**, required on every OS and architecture (98 SE, ME, 2000, XP, XP x64, Vista, 7 x86 and x64) that relies on G3 for isolation evidence: (1) delivery at every injection point; (2) ordering: after completed quiescence and before that OS's first removal callback, checked in the host trace (in usb4-emu, the notification's marker timestamp is later than the stage-2 endpoint reset/quiesce completion trace event); (3) queue saturation: delivered within the gone deadline while the admission limit and the port's queues are full of held SRBs (on NT the IOCTL must not queue behind them). **Result:** the IOCTL fails (3); the NT interface is the CM-owned link-state table (MD-6, [`include/stor98cm.h`](../include/stor98cm.h)) | Without G3 on an OS, surprise removal there has only evidence 1 (impossible on absent hardware) and evidence 3 (an ownership event, not isolation, R6-1); the gone-deadline outcome (decision MD-1) applies to that OS | R3-2, R4-1, R4-7, M1 |
| G4 Port after a failed reset | What does each port do when `HwResetBus` returns FALSE, or returns while the miniport still holds SRBs: complete them itself, call the reset again, or start removal? | Measured and recorded per port, and the port never completes a held SRB while the device may still reach its buffer (a forced completion only after the OS has itself established removal) | The held-SRB design of the unresponsive row is not safe on that port; the port is blocked pending a maintainer decision | R3-3 |
| G5 Memory lifetime at stop | Does the port free or reuse the uncached extension (and SRB extensions) after `ScsiStopAdapter` for removal, rebalance and power, and when relative to the callback's return? Does it call the stop in a context where a longer bounded poll is acceptable? 9x: the same for Configuration Manager stop/remove (from G9) | Either the port keeps the memory while the controller has not acknowledged disable, or the stop path always obtains evidence within its budget (RDY=0 within budget on every tested controller model, or a G3 link-down notification already received; all-ones reads do not count, R4-1) | The stop path cannot be made DMA-safe on that port; the port is blocked pending a maintainer decision | R3-4 |
| G6 Dump-mode timing and reset | Does each NT dump port tolerate a synchronous dump-mode `HwInitialize` that polls up to CAP.TO in short stalls, and which reset requests does it send during dump I/O? | Crash dump and hibernate complete with RDY delayed by 2 s, and a bus-reset request during dump I/O is disregarded without failing the dump | Scenario C (phase 4) is unsupported on that port | R3-6, R3-4 |
| G7 Resume path | With `ScsiRestartAdapter` not advertised, does the port resume through `HwFindAdapter`/`HwInitialize`, and are allocations preserved? | Resume works through the normal init path with the failure semantics of the bring-up rule (0.10) | Advertise `ScsiRestartAdapter` on that port only with measured evidence of how the port handles each return value; `ScsiAdapterControlUnsuccessful` is never returned without it | R3-7 |
| G8 Final shutdown trigger | For each OS and each `CachesData` setting, which callback arrives after the last I/O at system shutdown, hibernate and Device Manager disable (`ScsiStopAdapter`, `SRB_FUNCTION_SHUTDOWN`, a 9x IOS or Configuration Manager event)? | A final trigger is identified for each OS and recorded in 0.11 | CC.SHN is issued on the last shutdown notification seen, and any later I/O re-enables the controller first (tested) | R3-5 |
| G9 9x IOS integration | 0.12 in minimal form. Also, separately (R4-2): which PCI config-space access method is legal from 9x runtime callbacks, and from which ones, for Bus Master Enable (evidence 4) and 3.4. A 9x config read is a presence signal only, like MMIO, never isolation evidence | as 0.12; for config access, a method is named per callback with its serialisation against other config users | as 0.12; without a legal runtime method, 9x config access is confined to initialisation, as on NT | R2-6, R4-2 |

#### Phase 0.0 results

Run 2026-10-14/15 in usb4-emu (TCG; i440FX for 98 SE, Me, 2000 and XP x64, q35 for 7 x64).
One record per gate under [gates/](gates/), with its evidence; the prototypes stay on the
`phase0-*` branches (and `qual-nt` for the NT qualification runs) named in each record and
are not on `main`. **Status: complete on 2000 and XP x64, mostly complete on 7 x64, partial
on 9x (R6-8).** G1 passes on 2000, XP x64 and 7 x64. G2 and G4 pass on 2000, XP x64 and
7 x64 ([Q-NT](gates/Q-NT.md) for 7 x64; G4's stuck-controller variant not run on 7). G7 is a
full pass on 2000, XP x64 and 7 x64: resume path, allocation reuse and the resume-failure
semantics with the deferred bring-up and no `ScsiRestartAdapter` (Q-NT). G8 passes for
hibernate and Device Manager disable on 2000, XP x64 and 7 x64 and for shutdown/restart on
XP x64 and 7 x64 with `CachesData` TRUE; its 2000 shutdown failure (two SHUTDOWN SRBs with
I/O between) is resolved by the restartable SHUTDOWN of 0.11, demonstrated with CC.SHN on
each SHUTDOWN (Q-NT). G3 passes on 2000 and XP x64 with the link-state table (the IOCTL
route fails qualification item 3); MD-6 adopts the table. G6 passes on 2000 and XP x64
booting from the tunnelled NVMe. G9 passes on 98 SE with QuickInstall's IOS set (gold IOS
inferred), and its orderly-removal pass used the inherited shutdown code: no bounded stop
and no failed-disable stop has been shown on 9x. G5 fails on 2000, XP x64, 7 x64 and Me, and
G9 fails on Me: MD-3 (a) and MD-4 are decided, MD-5 was open then (decided since; see the maintainer decisions). "7 x64" below means only that;
7 x86 is untested.

| Gate | 98 SE | Me | 2000 | XP x64 | 7 x64 | XP x86, Vista, 7 x86 |
|---|---|---|---|---|---|---|
| [G1](gates/G1.md) deferred bring-up | not run (see G9) | not run | **pass**, hybrid policy only (pure hold fails at 6 s RDY) | **pass** | **pass** | not run |
| [G2](gates/G2.md) gone-state hold | gone prototype in G9: NO_DEVICE ends IOS retries | as 98 SE | **pass** | **pass** | **pass** ([Q-NT](gates/Q-NT.md)); with native PCIe hot-plug a surprise removal makes the OS remove the controller at once and the port retires held SRBs itself | not run |
| [G3](gates/G3.md) CM link-down notification | designed, not prototyped (rendezvous follows MD-5) | as 98 SE | **pass** with the link-state table; IOCTL **fails** (3) | **pass** with the link-state table; IOCTL **fails** (3) | not run (OS's own hot-plug removal races the CM) | not run |
| [G4](gates/G4.md) port after a failed reset | not run | not run | **pass** | **pass** | **pass** for the failed disable (Q-NT); stuck controller not run | not run |
| [G5](gates/G5.md) memory lifetime at stop | orderly removal pass (inherited shutdown code, RDY=0 seen; bounded or failed-disable stop not run); removal without evidence **fails** for the uncached extension (MD-4) | **fail** even for orderly removal (no stop; MD-5) | **fail** for removal, pass for power (MD-3) | **fail** for removal, pass for power (MD-3) | **fail** for removal (Q-NT; MD-3); driver-owned contiguous pool survives; surprise removal with DMA open not run | not run |
| [G6](gates/G6.md) dump mode | n/a | n/a | **pass** (scenario C, booting from the tunnelled NVMe; pump deadline mandatory) | **pass** (crash with delayed RDY not run) | not run | not run |
| [G7](gates/G7.md) resume path | not run | not run | **pass** (resume path, allocations; resume failure, Q-NT) | **pass**, same | **pass**, same (Q-NT) | not run |
| [G8](gates/G8.md) final shutdown trigger | **pass**: `HwAdapterState(FALSE)` | `SRB_FUNCTION_SHUTDOWN` with `CachesData` TRUE (measured); none with FALSE | hibernate, disable **pass**; shutdown: two SHUTDOWN SRBs, I/O between, handled by the restartable SHUTDOWN (**demonstrated**, Q-NT) | **pass** with `CachesData` TRUE | **pass** with `CachesData` TRUE | not run |
| [G9](gates/G9.md) 9x IOS integration | **pass**, no helper VxD (QuickInstall's IOS set; gold IOS inferred) | **blocked**, no stop callback (MD-5) | n/a | n/a | n/a | n/a |

Still open in 0.0:

- **G3 on 9x** (98 SE, Me): designed (G3.md, "Windows 9x receiving interface"), not
  prototyped; the rendezvous depends on MD-5, and a 9x CM stand-in is needed. G3 on 7 x64
  (q35, native hot-plug, where the OS's removal races the CM) needs a test-signed stand-in.
- **G6 on 7 x64** (crashdmp/diskdump, test-signed boot driver).
- **7 x64 variants:** G4 with a stuck controller, G5 surprise removal with the DMA path open,
  the hybrid BUSY run (15 s holds).
- **XP x86 and Vista** (and 7 x86): no gate run; no VM in these sessions.
- **Stock (gold) 98 SE IOS:** untested, it hangs at boot on the QuickInstall image; static
  analysis shows the same events and DDB flag, so it is *inferred* to stop the same way.
  Gold 98 SE `SCSIPORT.PDR` was run and behaves as QuickInstall's.
- **S3 (standby)** is unavailable on the test VMs (XP x64: std VGA driver vetoes it; 2000:
  `acpi=off`); hibernate is the tested power transition, three cycles on XP x64 and two
  on 2000, not the 20-cycle soak.
- Resource rebalance: no stimulus found (G5). IRQL of `ScsiStopAdapter` not instrumented.
- Real-SSD Flush and SHST latency (the Q-NT numbers are the emulator's; the P14s with an
  enclosure is the next measurement).
- Windows Me: Device Manager Disable, standby and hibernate not tested.

#### Qualification before dependent product code (R6)

Verdict of review round 6: **build work (0.1) and isolated request-engine scaffolding (the
parts of 0.2-0.3 that need no lifecycle, memory-placement or shutdown decision, testable in
the unit harness) may proceed.** The allocation, shutdown and removal contracts need
resolution before any product code that depends on them. Each item below must pass, or
have its maintainer decision recorded, before the code that depends on it is written:

| Item | Blocks | Status (2026-10-15) |
|---|---|---|
| Windows 7 x64 runs of G2, G4, G5 and G7 (and 7 x86, XP x86 and Vista where they are claimed) | gone-state, reset, stop and resume code on those OSes | **done for 7 x64** ([Q-NT](gates/Q-NT.md)): G2, G4, G7 pass, G5 fails (MD-3 (a) covers it). Open: G4 stuck-controller and G5 surprise-removal variants on 7; XP x86, Vista and 7 x86 (no VM) |
| Combined G1 + G7 prototype: resume failure with the deferred bring-up and `ScsiRestartAdapter` not advertised (R6-7) | the resume-failure handling of 0.10 and 0.11 | **done**: pass on 2000, XP x64 and 7 x64 (Q-NT item 2) |
| Windows 2000 shutdown with CC.SHN actually written on each `SRB_FUNCTION_SHUTDOWN`, and re-enable on the I/O between them (R6-5) | the restartable shutdown notification of 0.11 | **done**: demonstrated on 2000 (two SHUTDOWNs, data verified), XP x64 and 7 x64 (Q-NT item 3); R6-5 closed |
| Port `SRB_STATUS_BUSY` retry limit for long RDY waits | hybrid hold-then-BUSY (0.3) beyond the measured waits | **done**: 20 re-issues (21 BUSY) per request, count-based (Q-NT item 5; rule in 0.3). Open: class-driver retries on top for READ/WRITE |
| 9x runs of G1 and G4 (deferred bring-up, failed reset on the 9x port; what the 9x port does after `HwResetBus` returns FALSE) | 9x bring-up, reset and gone-state code | open |
| G3, on every OS where isolation relies on the CM notification (R4-7) | release of memory on evidence 2 on that OS | **done for 2000 and XP x64** with the link-state table (MD-6, [G3](gates/G3.md)); the product NT receiver (0.4.0.0) passed with xhci98 3.0.0.0's CM there too (TESTING "CM notification, product NT receiver"). Open: 98 SE and Me (designed, not prototyped; rendezvous pending MD-5), 7 x64, XP x86, Vista, 7 x86 (xhci98 withholds the statement there, so MD-1 applies) |
| G6, including dump scenario C; the allocation interface must keep a separate dump-mode route (R6-2) | phase 4; and any allocation interface (0.5) that dump mode would otherwise share | **done for 2000 and XP x64** ([G6](gates/G6.md)). Open: 7 x64 (and XP x86, Vista, 7 x86) |
| Flush and SHST latency against the stop-callback budget, per OS and controller (R6-3) | the stop path's durability claim (0.10 stop) | **measured in the emulator** (Q-NT item 4): Flush within the budget refuted for the XP x64 and 7 disable stop; **decided as MD-7 (a)** (idle flush plus accept-and-document). Open: real-SSD latency |
| MD-4 pool prototype: operational queues, PRP lists and bounce buffers in the `_PageAllocate` pool, not only a sentinel (R6-2) | adoption of the 9x pool | **integrated on `int-0.2` (2026-10-16, after D4 was adopted; [INTEGRATION-0.2.md](INTEGRATION-0.2.md)); the rest of this cell is the state of 2026-10-15.** Prototyped on branch `qual-9x` (Q-9x item 3, pass on 98 SE and Me in the emulator), not integrated in the engine then. Known blocker (review round 2, P1-1): integrate on D4 adoption. The pool records live in interface 3 of `include/stor98cm.h`, which D4 (adopted 2026-10-16) settled, so the engine took the provider only after that decision. Until then the engine's 9x build still carves its DMA memory from the port's uncached extension, which the 9x port frees when it unloads the `.mpd` at removal: after the MD-1 gone deadline that memory stays quarantined (owned, out of every pool) only until that removal, so 9x removal after unconfirmed isolation can free memory the device might still reach (G5 on 9x, review round 2 P1-1). Each such quarantine is counted and logged (`STG_DMA_GAP`). **The engine side is ready:** the provider hook in `src/platform.h` (`PLAT_DMA_PROVIDER`: `Acquire` before any CC write and without writing memory a previous load may have left reachable, `PagePa` per page, `Note` for record owner changes in the interface 3 states: RESERVED, OWNED and ISOLATED while this load holds a block, RELEASED or QUARANTINED only when the engine relinquishes it for good: at the end of its stop, review round 3 N2, or on 9x when an interface 3 removal transaction's disable is acknowledged, after the queue gates close (accepted in the `int-0.2` review, 2026-10-16; [INTEGRATION-0.2.md](INTEGRATION-0.2.md)); `HoldsPastRemoval`); every engine allocation goes through `PlatDmaAlloc` and every page given to the controller through `PlatDmaPagePa`; a build with `STOR98_DMA_POOL` takes `PlatDmaPoolProvider` on 9x and fails the find before CC.EN when the pool cannot be had (no silent fallback). Integration work: the pool module (allocation, cap, records in the CM table, reclaim on a newer link generation) implementing that table, plus the per-boot rendezvous of Q-9x item 2 |
| MD-5: the Windows Me removal stop | Me support; the 9x G3 rendezvous | decided 2026-10-15 (handshake + write-through on Me); prototyped on `qual-9x` (Q-9x), integrated on `int-0.2` (2026-10-16) |

#### Recovery contract (0.10)

Applies to bring-up (`HwInitialize`, resume), `HwResetBus`, `SRB_FUNCTION_ABORT_COMMAND`,
`SRB_FUNCTION_RESET_BUS`/`RESET_DEVICE`, the driver's own watchdog recovery, and stop.
Today `HwResetBus` completes every outstanding request immediately without touching the
hardware (src/nvme2k.c:1298), and the abort/reset SRBs return success without doing
anything.

**Stall budget (R3-4).** Each `ScsiPortStallExecution` call is at most 100 µs, and the
total stall in one callback invocation is under 1 ms, which is SCSIport's limit per
routine (to be confirmed against the installed DDK documentation in 0.0; if the kit states
a lower figure, that figure applies). This budget is separate from hardware deadlines
(CAP.TO up to 127.5 s; shutdown up to RTD3E or a driver limit). A wait longer than the
budget is carried by a progress mechanism that outlives the callback:

| Entry point | Progress mechanism | Timeout outcome |
|---|---|---|
| `HwFindAdapter` | none needed: CC.EN is not written here (unless G1 fails on that port) | failure returned only under the bring-up rule below |
| `HwInitialize`, resume | timer-driven bring-up (G1) | `failed`; controller disabled, or memory quarantined |
| `HwStartIo`, `HwInterrupt` | none; they never wait | n/a |
| `HwResetBus`, reset SRBs | watchdog timer continues the disable wait | `quarantined` (unresponsive row) |
| Watchdog recovery | timer | `failed` |
| `ScsiStopAdapter` (synchronous; no timer runs after it returns) | bounded poll within the budget only | stop path below |
| Dump-mode init and I/O (no timers) | synchronous poll in stalls of at most 100 µs, total limit from G6 | the dump fails; dump mode frees nothing |

**Completion rules.** Writing CC.EN=0 does not complete a disable: the disable is
acknowledged when CSTS.RDY reads 0 from a CSTS value that is not all-ones. Writing CC.SHN
does not complete a shutdown: it is clean only when CSTS.SHST reads "shutdown processing
complete". DMA resources are released only after an acknowledged disable or the other
evidence below.

**Assumption A1: link-down resets the endpoint (R4-1; accepted under MD-2).** On real
hardware, tearing down the USB4/Thunderbolt PCIe tunnel takes down the PCIe link above the
endpoint (the virtual link between the host's PCIe downstream adapter and the device
router's upstream port). The basis, from the PCIe Base Specification (r3.0 and later):

- §2.9.1 (Transaction Layer behaviour in DL_Down status): when an Upstream Port's link
  goes to DL_Down, the component is reset as for a hot reset, and a Switch propagates that
  hot reset to all of its Downstream Ports.
- §3.2.1 (Data Link Control and Management State Machine): an Upstream Port's link going
  down is treated the same as a hot reset.
- §6.6.1 (Conventional Reset): a hot reset is a conventional reset; a reset Function
  returns its Command register to defaults, Bus Master Enable included.

Then from the NVM Express specifications: NVMe over PCIe Transport §3.3 and NVMe Base
§3.7.2 (Controller Level Reset): a PCIe conventional reset is a controller-level reset, so
CC.EN returns to 0 and every queue and outstanding command is gone. Because the reset
propagates through a Switch, it also reaches an endpoint behind an enclosure's internal
PCIe switch. So after a genuine link-down the old controller execution cannot continue,
and nothing it had in flight can complete later. The maintainer accepted A1 on this basis
(MD-2); it is to be verified on real hardware: the P14s Gen 1 with an NVMe enclosure, and
with an enclosure that has an internal PCIe switch. The emulator models it the same way:
usb4-emu `docs/ARCHITECTURE.md` §4.4, stage 2 (quiesce) resets the endpoint after the gate
closes, and a later `up` always realises a fresh endpoint.

**Presence loss is not isolation (R4-1).** All-ones from CSTS, any MMIO register or a
config read means a completion timeout or Unsupported Request was turned into all-ones by
the host. That is consistent with the link being down, but also with a hung device, a
bridge that stopped decoding, or a fault that blocks the host-to-device direction while
the device can still master DMA. It moves the adapter to `gone` and nothing else.

**DMA-isolation evidence**, in order of preference (R3-2, R4-1):

1. *Controller disable acknowledged*, as defined above (CC.EN=0 and CSTS.RDY=0 read from a
   CSTS value that is not all-ones).
2. *Link-down statement* (G3): xhci98's CM, which tore the tunnel down itself, reports that
   the PCIe link to this function went down. Under A1 that implies the endpoint was reset.
   **Notification boundary (M1): after completed quiescence.** The CM may send the
   statement only once the tunnel's PCIe path is closed *and* the endpoint reset or drain
   has completed; a statement sent earlier, for example as the teardown starts, does not
   count. In usb4-emu, guest memory is safe only at the end of stage 2 of
   `docs/ARCHITECTURE.md` §4.4, observable as the endpoint reset/quiesce trace event. On
   real hardware it is the point where the CM has torn down the path and the link-down reset
   has propagated to the endpoint (A1). This is a CM obligation under MD-2; G3 qualification
   checks the ordering in host traces.
   **Carrier (G3, MD-6): the CM-owned link-state table** ([`include/stor98cm.h`](../include/stor98cm.h)).
   On NT the CM creates `\Callback\Stor98LinkState` and publishes a never-freed non-paged
   table through it; stor98 opens the object once in `HwFindAdapter` (PASSIVE_LEVEL, not
   in dump mode), records the entry's link generation and polls the entry from its
   watchdog tick, the bring-up timer and `HwStartIo`; it registers nothing with the CM.
   CM obligations, besides MD-2's: (a) publish `UP` with a new generation **before** the OS
   can enumerate the endpoint; (b) set `DOWN_QUIESCED` only after the path is closed and the
   endpoint reset has completed; (c) wait for stor98's acknowledgement (its
   `AckGeneration`), up to the gone deadline, **before** starting the OS removal path. stor98
   applies `DOWN_QUIESCED` for its recorded generation or a newer one and then acknowledges;
   it ignores `DOWN` without `QUIESCED` and older generations. The `IOCTL_SCSI_MINIPORT`
   route fails G3 item 3 (under a saturated queue it is started only after the MD-1 deadline,
   and it fails once the LU is gone) and is kept only as a diagnostic query. The ordering
   (b) is not checkable by the miniport (G3 run E): it stays a host-trace check per OS. On 9x
   the same table is to be found through a rendezvous that follows MD-5 (G3.md); designed,
   not prototyped.
3. *OS-established removal*: `ScsiStopAdapter` after removal (NT), or the Configuration
   Manager stop/remove (9x). **This is an ownership event, not proof of DMA isolation
   (R6-1):** it says the OS has taken the device away from the driver, not that the device
   can no longer master DMA. On NT it arrives after SCSIport has already retired the
   outstanding requests, so it is never the evidence for completing SRBs or freeing DMA
   memory; it only releases what the port leaves to the miniport (timers, state). On the
   orderly NT path, the isolation that actually follows is PCI.sys clearing Bus Master
   Enable and setting D3hot 2-4 ms after `ScsiStopAdapter` returns (G5, measured on 2000
   and XP x64). The driver cannot observe it, and the port's free of its own memory is not
   ordered against it: the window is up to those 2-4 ms. A surprise removal produces no
   callback at all (G2, G5). The item keeps its number so that "evidence 4" below keeps its
   meaning.
4. *Bus mastering disabled*: Bus Master Enable cleared in the function's own PCI command
   register and read back as clear from a read that is not all-ones, only from callbacks
   where PCI config writes are permitted (NT: `HwFindAdapter`, `ScsiSetRunningConfig`; 9x:
   per G9). Fallback only.

Never on this list: all-ones MMIO or config reads, alone or together (R4-1); a controller
generation counter, which filters stale completions but cannot stop DMA; an NVMe Abort
completion (R4-4, below).

**Gone state and its deadline (R4-1).** On presence loss the adapter enters `gone`: no new
child is issued, new SRBs are fast-failed with the terminal status of 2.2 (safe, since they
never reached the device), and SRBs already issued stay held with their buffers owned. The
driver then waits for evidence 1 (if CSTS becomes readable again) or 2, up to the **gone
deadline**. The deadline is a tunable, measured from entering `gone`: default 5 s in every build
(`gone=` on NT; README "Settings"), the same budget as T1-T2 in removal acceptance. Measured by
G2 and G9, it is set as follows:

- It is measured as **elapsed time** against a clock (TSC or the port's/VMM's system time),
  never as a count of timer ticks: ticks ran up to 55% late under load on NT (50 ticks of
  100 ms took 7.8 s) and up to 3x late on 9x. The same applies to the watchdog deadlines
  of 2.3 and the bring-up waits.
- **Which clock, which bound (engine, src/clock.h).** The TSC's calibration against
  `ScsiPortStallExecution` ranged from 0.57x to 2.05x the true rate between boots under
  TCG, so the TSC is not trusted alone. The driver also reads the OS clock (NT: the
  interrupt time in the shared user data page, the fixed address the DDK's
  `KeQueryInterruptTime` reads, no import; 9x: VMM `Get_System_Time`), which runs slow
  under load but never fast; a "safe" clock follows it (or, if it is frozen or implausible
  against the nominal timer periods over a 2 s window, the nominal periods, which only
  run late), and the TSC is re-calibrated against it. The TSC estimate is a monotonic
  accumulated millisecond count: each reading folds the TSC units since the previous one
  at the rate then in force, so a re-calibration prices only later units and never
  re-prices an interval that has already elapsed (review round 2: dividing old stamps by
  a new rate made elapsed estimates jump). **Rule (corrected in review round 2):** two
  kinds of decision are kept apart. *Permission to release*, that is every deadline whose
  expiry completes an SRB the device may still reach or releases memory without isolation
  evidence (the gone deadline and the per-SRB bound below), and every hardware wait that
  must not end early (CAP.TO for RDY and the disable, the SHST bound), uses
  **trustworthy elapsed time**: the smaller of the TSC and safe estimates, the safe one
  reduced by its resolution (one OS clock-interrupt period, 16 ms on NT and 55 ms on 9x;
  one timer period, at most 1 s, if the OS clock was rejected), which makes it a lower
  bound of the real interval. A fast-running TSC cannot shorten it and the quantisation of
  the OS clock cannot make it early. *Early recovery and admission decisions* that release
  nothing (hold-then-BUSY and the BUSY count, for requests that never had a child issued)
  use the *larger* estimate (the earlier deadline), with a margin for a safe clock running
  slow. The earlier rule, which measured the per-SRB bound with the larger estimate, was
  wrong: either check can release the same buffers, so a TSC running 2x fast released an
  SRB about one real second after admission when its bound was two seconds away.
- **Per-SRB bound (R6-6).** The port times each SRB from when it was started, not from
  presence loss, so a deadline measured only from entering `gone` can outlast an SRB that
  was already outstanding for a while. On NT each held SRB therefore has its own effective
  deadline: the earlier of (entry into `gone` + the gone deadline) and (the time the SRB
  was started + its `TimeOutValue` − a margin). The margin covers detection time and timer
  lateness (at least one late tick under the measured 55%) and a safe clock running slow;
  as implemented it is half the `TimeOutValue`, at least 1.5 s (a tunable, to be set from
  measurement). Completion at that earlier point is the MD-1 outcome for that SRB, not a
  new exception, so the bound is measured with trustworthy elapsed time (above), never
  the larger estimate. With this bound the port's own timeout does not fire during `gone`
  on NT, provided `TimeOutValue` is honest there (2000, XP x64, 7 x64: 4-240 s, G1) and
  the safe clock runs no slower than the margin allows; if it does run slower, the port's
  timeout comes first and the outcome is the measured-safe one (`HwResetBus` in `gone`
  returns FALSE with the SRB still held, G4), never an early release.
- **9x: no "never resets" guarantee.** `TimeOutValue` is not usable (0 on 98 SE, garbage
  on Me, G9), and the port's own timeout is not a fixed number: `HwResetBus` came 13.2 s
  after the request was issued on 98 SE and 22.7 s on Me, while in the deadline runs it did
  not fire within 15.8 s (98 SE) and 15.96 s (Me) of the gate; the variance was not
  investigated (TCG times). The 9x deadline is a fixed value chosen below the shortest
  observed port timeout, measured from entering `gone`, and the reset-safe fallback is
  explicit: if `HwResetBus` arrives during `gone`, it returns FALSE without completing held
  SRBs (as on NT, G4), and the gone deadline still applies. What the 9x port does after a
  FALSE `HwResetBus` is unmeasured (G4 not run on 9x; "Qualification before dependent
  product code").
- On NT, if the port does reset first despite the bound (for example a dishonest
  `TimeOutValue`), the outcome is still safe: one FALSE reset and a hang until the gone
  deadline completes the held SRBs (G4 measured a late completion after a FALSE reset only
  with `SRB_STATUS_BUS_RESET`; a terminal-status completion there is still to be checked).
- At the deadline the held SRBs complete with `SRB_STATUS_NO_DEVICE` (the terminal status;
  `SRB_STATUS_SELECTION_TIMEOUT` also ends retries but gives Win32 error 1167 and Disk
  event 15, where NO_DEVICE gives error 2 and no event), and on NT `BusChangeDetected` is
  notified at once. That makes the port rescan and surprise-remove the disk PDO within
  2-25 ms (G2). It is a disk-level removal only; the controller goes away through PnP.
  On 2000 and XP x64 this ended the XP x64 117/9 retry loop: no retries, no events, the
  application has its error at T2. On 9x NO_DEVICE ends IOS's retries after 6 tries per
  request (G9).

Evidence that
arrives before the deadline always takes precedence: the path-down or acknowledged-reset row
below applies and the deadline is cancelled. If the deadline passes with neither, the
**gone-deadline outcome (decided, MD-1 option (b))** applies:

- The held SRBs are completed with the terminal device-gone status of 2.2, and new SRBs keep
  fast-failing.
- Driver-owned DMA memory (bounce buffers, PRP list pages, queues) is quarantined: kept
  owned and out of every pool, never freed, reused or handed back to the port.
- The event is logged and a gone-deadline counter is incremented (readable from the debug
  build and recorded by the tests).
- **Accepted residual risk (owner-accepted exception, MD-1):** the callers' own buffers go
  back to the OS while the device is not proven isolated. If the all-ones reads were not
  caused by a real link-down (for example a fault that blocks reads and doorbells but leaves
  the device's upstream DMA path open, with the controller still executing), late DMA from a
  command still in flight may corrupt pages the OS has reused: silent memory corruption,
  with no detection by the driver. This is the only path in this plan that completes an
  SRB without DMA-isolation evidence. The negative test in TESTING.md (presence lost with
  the DMA path open) shows the exposure in the emulator.

Recovery from `gone` without removal (the tunnel reopens before the OS removes the device)
follows the reopen invariant below.

**Reopen invariant (R4-3).** Memory released on evidence 2 must stay unreachable if the
tunnel reopens before the driver has reset the controller. That holds only because of A1
and two obligations on the CM (MD-2): xhci98's CM re-establishes a tunnel only to an
endpoint whose link went down since the last tunnel (so it was reset), and it sends the G3
link-down statement only after completed quiescence (the notification boundary of
evidence 2). The miniport checks, and cannot do
more than check: whenever it regains contact with the controller (CSTS readable again in
`gone`, or a new start after removal), it reads CC and CSTS before trusting any state. CC.EN=0
and RDY=0 means a fresh controller; bring-up continues as in 0.10. CC.EN=1 or RDY=1 means
the controller kept its old queues across the gap: an isolation-contract violation. The
driver logs it, writes CC.EN=0, waits for RDY=0 (timer-carried, as a reset) and reuses
nothing (no queue, CID, PRP list or bounce buffer) until the disable is acknowledged; if it
is never acknowledged, the unresponsive row applies. The controller is present, so this is
not the gone-deadline outcome: the MD-1 exception does not apply to it. Detection does not undo DMA the
controller performed between the reopen and the driver's first read; preventing that is
the CM's obligation, and the emulator test that skips the endpoint reset shows the exposure
(TESTING.md, reopen tests). Part of decision MD-2. DMA by such a controller between the
driver's CC.EN=0 write and its RDY=0 acknowledgement is documented behaviour, not a driver
fault: it is exposure after a contract the CM has already violated, and the driver reuses
nothing until RDY=0 is acknowledged (M2).

**Entry points and request ownership (R3-3).** The driver's watchdog recovery is separate
from `HwResetBus`: it starts from per-command deadlines (2.3) without the port, may span
many timer ticks, and returns no status to the port. Return values are truthful:

| Entry point | Can pend | Returns | Outstanding SRBs |
|---|---|---|---|
| `HwResetBus` | no | TRUE only when a disable was acknowledged within the budget (queue re-creation then continues from the timer), or when no child is issued (in `quarantined`, none quarantined either): then the SRBs it completes have nothing the device could still reach, and no hardware action is taken (a SHUTDOWN waiting for SHST completes too; that wait continues from the timer). On 9x in `starting` it returns FALSE and completes nothing (Q-9x, 0.3). In `gone` always FALSE: no waits on absent hardware, and evidence 2, when it arrives, has already completed every held SRB. FALSE otherwise. If watchdog recovery is already running, no new reset: O(1) MMIO reads, then the same rule | on TRUE, completed with `SRB_STATUS_BUS_RESET` before returning; on FALSE, still owned by the driver, and the watchdog continues the disable wait. What each port does after FALSE is measured (G4, 2000 and XP x64): it never completes the held SRBs itself (only PnP removal retires them), calls `HwResetBus` at most once per expired port timeout (a second call 15-23 s later only if other requests completed in between; otherwise one call in 3-7 min), starts no removal and withholds further SRBs from the LU |
| `SRB_FUNCTION_RESET_BUS`/`RESET_DEVICE` (where the port passes them to the miniport) | yes | the reset SRB completes with SUCCESS once the disable is acknowledged, with ERROR if the adapter enters `failed` | as for `HwResetBus` |
| `SRB_FUNCTION_ABORT_COMMAND` | yes | SUCCESS once every child of the target parent is quiesced in the command-level sense below, or controller-wide isolation (evidence 1 or 2) is established; `SRB_STATUS_ABORT_FAILED` if neither can be shown | the target stays owned until then; see "Abort is not quiescence" |
| Watchdog recovery | timer | no return to the port; `ResetDetected` notified on NT when a driver-initiated reset is acknowledged, including a disable acknowledged late after `HwResetBus` returned FALSE: the held SRBs are completed with `SRB_STATUS_BUS_RESET` first, then `ResetDetected`; the port resumes dispatch about 3 s later with no double completion (G4) | owned until evidence, then completed per the outcomes below |

**Abort is not quiescence (R4-4).** Two different things are proved in this plan:
*command-level quiescence* of one NVMe command, which holds once the driver has consumed
that command's own completion entry (any status) from its completion queue, and
*controller-wide isolation*, which is evidence 1 or 2 above. The completion of an NVMe Abort
command proves neither for its target. Its Dword 0 bit 0 (NVM Express Base Specification,
Abort command, command-specific completion dword) is 0 if the target was aborted and 1 if
it was not; even when it reads 0, the target is only quiesced once its own completion
(normally with status Command Abort Requested) has been consumed, and the two entries may
arrive in either order. Handling of `SRB_FUNCTION_ABORT_COMMAND`:

- Stop dispatching further children of the target parent at once.
- For every live child, submit an NVMe Abort (within the controller's Abort Command Limit;
  a child that cannot get one is left to escalation).
- A child counts as quiesced only when its own completion has been consumed. An Abort
  result of "not aborted" (bit 0 = 1), Abort Command Limit Exceeded, or a child completion
  not seen by the child's deadline escalates to watchdog recovery (a controller reset under
  this contract), whose evidence 1 or 2 then quiesces every child at once.
- The parent and the abort SRB complete only when every child it ever issued is accounted
  for: quiesced, or covered by established isolation. The parent completes `ABORTED` if any
  child was aborted or reset, or if the abort suppressed any required unfinished work
  (parent cancellation, below); otherwise with its own status from the parent error mapping.
- Base: the abort SRB returns success without doing anything (see above).

**Parent cancellation (H1).** A parent's work is complete only when every required step
has been done: every fragment dispatched and completed, and for a read-modify-write every
write-back issued after its pre-read and completed. The parent carries a cancellation
state, set when an abort, a reset, the gone state or a removal suppresses any of that
work (an undispatched fragment that will not be issued, or a pending RMW write-back that
was not issued after its pre-read):

- Once set, the parent can never complete with `SUCCESS`, even if every child it did
  issue succeeded. Its status is `ABORTED` for the driver's own abort, `BUS_RESET` for an
  acknowledged reset, and the terminal status of 2.2 for path down or the gone-deadline
  outcome, as in the parent error mapping; `DataTransferLength` is 0.
- Setting it stops further dispatch but releases nothing: children already issued must
  still quiesce (own completion consumed, or controller-wide isolation established, or the
  gone-deadline outcome of MD-1) before the parent completes and before its bounce buffers
  and PRP pages are released.
- A cancellation that races successful child completions is decided under the parent's
  lock: whichever completion is processed last sees the cancellation state and applies it.
  A parent all of whose required work had already completed before the cancellation
  arrived completes with its own status.

Tests (TESTING.md, parent cancellation): a fully pending parent (no child dispatched yet)
aborted; an abort between an RMW pre-read and its write-back; successful child completions
racing cancellation while fragments are still undispatched.

**Outcomes:**

| Situation | Hardware action | Outstanding SRBs | Buffers |
|---|---|---|---|
| Acknowledged reset (evidence 1) | disable, then re-create queues (continues from the timer) | completed with `SRB_STATUS_BUS_RESET` (or `ABORTED` for the abort target) | released |
| Path down (evidence 2) | none (no waits on absent hardware) | completed per 2.2 | released; reopen invariant applies |
| Presence lost, no evidence by the gone deadline (MD-1) | none | completed at the deadline with the terminal status of 2.2; event logged, gone-deadline counter incremented | driver-owned memory quarantined, never freed; callers' buffers returned to the OS under the accepted residual risk |
| Unresponsive (present, RDY does not clear within CAP.TO, no evidence 2, evidence 4 unavailable or unconfirmed) | state `quarantined` (failed without an acknowledged disable); the timer keeps checking CSTS at a low rate, moves to the acknowledged-reset row if RDY=0 is read with CC.EN=0, and to the path-down row on evidence 2 | SRBs whose buffers the device can still reach stay owned and are not completed; new SRBs fast-fail (in practice only the few already dispatched: after a FALSE reset the port withholds further SRBs, so the volume hangs without application errors until acknowledgement, removal or the gone deadline, G4) | quarantined: out of the pool, never reused or handed back |

The unresponsive row accepts that the affected volume hangs rather than risking memory
corruption, and is logged once. Before the gone deadline, the presence-lost case holds its
SRBs the same way. The safety of both depends on G2 and G4: the port must not complete the
held SRBs itself while the device may still reach them. The gone-deadline row is the one
accepted exception (MD-1). G2 and G4 passed on 2000, XP x64 and 7 x64 (Q-NT; not yet run
on 7 x86, Vista, XP x86, or on 9x for G4): no port completes a held SRB before PnP removal
of the controller, and none produces repeated resets. On q35 with native PCIe hot-plug
(7 x64) a surprise removal makes the OS remove the controller at once, and the port retires
the held SRBs itself before any gone deadline; there MD-1's deadline matters only when
presence is lost without a hot-plug event (a hung device, or the harness gate).

**Bring-up (R3-1).** Every check that can fail without the controller enabled runs before
CC.EN is written: BAR and CAP decoding, queue capabilities (0.9), page size, DMA
negotiation and allocation (0.8, 0.5), CSTS not all-ones, CFS clear, and RDY clear (if a
previous owner left RDY set, a disable is done first and must be acknowledged). Identify
and queue creation need the controller enabled, so they cannot move before CC.EN.
`HwInitialize` therefore writes CC.EN and returns TRUE (G1), and bring-up continues from
the timer: wait for RDY within CAP.TO, create queues, identify, select the namespace (1.9).
Until it finishes the adapter is `starting`: requests are held in the admission list, or
returned BUSY before any child is issued (0.3), within the limits G1 measured: the
hold-then-BUSY policy of 0.3 (pure hold fails on Windows 2000). `RequestTimerCall` from
`HwInitialize` works on 2000, XP x64 and 7 x64 (first call 5-25 ms after the return), and
G1 passed on all three with RDY delayed 2 s and 6 s, RDY never set, and identify failing.
**`HwInitialize` never returns FALSE on resume** once CC.EN was written: G7 measured that
it hangs the volume on 2000 and XP x64 (no reset, no timeout for over 4 min). A bring-up
failure after CC.EN disables the controller. Once RDY=0 is acknowledged the adapter is
`failed` with the controller disabled; if it is not acknowledged within CAP.TO, the adapter
is `failed` with its memory quarantined. Either way the adapter stays started and owns its
memory; no disk is exposed if identify never succeeded, and requests fail as in the parent
error mapping. At boot or start (not resume), init failure is returned to the port only
before CC.EN was ever written, or after an acknowledged disable. **Resume is excluded from
that permission (R6-7):** on resume `HwInitialize` returns TRUE in every case and expresses
failure as state `failed` (or `gone` on all-ones CSTS). G7 measured the hang only for FALSE
after CC.EN, but nothing measured shows the port treats FALSE differently before CC.EN or
after an acknowledged disable. **The deferred resume-failure handling is verified** by the
combined G1 + G7 prototype (Q-NT item 2: deferred bring-up, `ScsiRestartAdapter` not
advertised, identify failing and, separately, RDY never set on resume) on 2000, XP x64 and
7 x64: `HwInitialize` returned TRUE, the bring-up failed into `failed` (controller disabled
and acknowledged after an identify failure; after CAP.TO for RDY never set), the held
request completed with an error, and the volume never hung. The port then fails requests
fast: 2000 keeps the volume mounted with errors; XP x64 and 7 x64 remove the disk after a
rescan. No `HwResetBus`, no `ScsiStopAdapter` and no `HwAdapterControl` call follows. On
resume `HwFindAdapter` gets no `ArgumentString` (0.5). Base: `HwInitialize` writes CC.EN (src/nvme2k.c:837) and
returns FALSE on an RDY or identify timeout without disabling. Upstream 1.2.0.0 moves the
same synchronous sequence into `HwFindAdapter` (third_party nvme2k.c:452, nvme2k_nvme.c:1306)
and returns `SP_RETURN_ERROR` the same way, so it offers no deferred model to backport.

**Stop (R3-4).** `ScsiStopAdapter` is synchronous and no timer runs after it returns. If
evidence 2 already holds, it does no waits; if CSTS reads all-ones without it, it does no
waits either, and what it held stays owned or quarantined. Otherwise, within the budget: submit a Flush
if the adapter is dirty (below; not only if writes are outstanding: on
XP x64 and 7 a Device Manager disable sends no flush of any kind before the stop, G8) and poll it, write CC.SHN and poll SHST, write CC.EN=0 and poll
RDY. A shutdown whose SHST completion was not seen is logged as not clean; durability then
rests on the flush that preceded the stop (G8 establishes which flush that is per OS).

*Durability at stop (R6-3).* Write tracking is conservative: the adapter becomes dirty when
any write child (edge write-backs included) is **submitted**, and the dirty state is
cleared only when an NVMe Flush that was submitted after every write tracked so far had
completed **completes successfully** (an ordered flush). A failed, timed-out or abandoned
Flush leaves it dirty. Writing CC.EN=0 does not establish durability; a completed ordered
Flush is the evidence that written data reached non-volatile media. Whether SHST reporting
shutdown processing complete is equivalent evidence for the volatile write cache is to be
confirmed against the NVM Express Base Specification's shutdown rules before the driver
relies on it.

Measured (Q-NT item 4, emulator; real SSDs are slower): with the conservative tracking the
adapter is clean at every stop that follows an OS flush (2000 for disable and hibernate;
XP x64 and 7 x64 for hibernate, where SYNCHRONIZE CACHE and START STOP UNIT precede the
stop) and at every shutdown (SYNC before SHUTDOWN), and there CC.SHN/SHST and CC.EN=0/RDY=0
completed at the first poll. **On XP x64 and 7 x64 a Device Manager disable finds the
adapter dirty every time** (writes completed milliseconds before, no flush, G8), and a
stop-path Flush took 2.1-18.9 ms (XP x64) and 5.8-17.1 ms (7 x64): **a Flush within the
1 ms budget is refuted** for that stop, even in the emulator. Runtime flushes took
0.1-6.3 ms.

**Decided (MD-7 = (a), 2026-10-15): idle flush from the timer, plus accept-and-document.**

- *Idle flush.* While running, the watchdog timer submits an NVMe Flush once the adapter
  is dirty and has been write-idle (no write child submitted) for a short configurable
  time, measured as elapsed time; a completed ordered Flush clears the dirty state as
  above. It cannot be timed to the stop, so it bounds the unflushed window at any stop to
  that idle time plus the Flush's own latency, at the cost of extra flushes.
  The idle Flush has no SRB, so no port timeout or `HwResetBus` covers it; it has its own
  deadline (30 s, at least CAP.TO, trusted elapsed time from its submission), and a missed
  deadline runs the disable recovery of a timed-out request (review round 4).
- *At the stop* the stop path still submits a Flush if the adapter is dirty, then CC.SHN
  and CC.EN=0, each polled only within the budget. A Flush or SHST not seen within the
  budget is logged as not clean, the dirty state is not cleared, and nothing claims the
  data written since the last completed Flush durable. The callback never exceeds the stall
  budget for it.
- *Residual window, documented for users:* on XP x64 and 7, data written within the idle
  time (plus Flush latency) before a Device Manager disable or eject may not have reached
  non-volatile media when the controller is shut down. 2000 and every hibernate and
  shutdown are covered by the OS's own flush. Rejected: write-through on those OSes
  (performance) and exceeding the stall budget (violates the SCSIport contract); flushing
  at a sign of the coming stop is not available on NT (no notification before a disable,
  G8).

Still to measure: Flush and SHST latency on real SSDs (the P14s with an enclosure), and the
unflushed window at disable with the product's idle flush (TESTING.md).
If RDY=0 is not acknowledged, the DMA memory is quarantined: deliberately leaked rather than
freed. That is only possible where the port keeps the memory (G5); where the port frees the
uncached extension regardless, G5's consequence applies. G5 measured that the port keeps
it only across a power stop: on 2000, XP x64 and 98 SE a removal frees it after the
stop callback returns whether or not RDY=0 was acknowledged (and after a surprise removal
on NT, and any removal on Me, with no callback at all), so quarantine works only for
memory outside the port (open: MD-3 for NT, MD-4 for 9x). On the orderly NT path PCI.sys
clears Bus Master Enable and sets D3hot 2-4 ms after `ScsiStopAdapter` returns (measured;
not evidence the driver can observe or use). On 9x the stop is `HwAdapterState(FALSE)`,
no timer runs after it, so it must finish everything itself and must stop at the first
all-ones read (the base stalls 12.7 s there on absent hardware).

Tests (independent of tunnel removal): controller that sets RDY only after 2 s at bring-up;
identify failing at bring-up; controller that clears RDY only after a delay longer than the
callback budget; controller that never clears RDY, during reset and during stop; reset with
I/O in flight; abort SRB targeting a live command, with the Abort reporting "not aborted",
with the target's completion racing the Abort's in both orders, and with several live
children (R4-4); parent cancellation (H1); presence loss with and without the G3
notification, with no evidence arriving before the gone deadline (MD-1: SRBs completed at
the deadline, driver memory quarantined, counter incremented), and with the controller hung
behind a live tunnel; the negative test, presence lost while the DMA path stays open
(R4-1); reopen with a fresh endpoint and reopen without the endpoint reset (R4-3, M2). These need emulator hooks (see TESTING.md, harness work).

#### NT callback, state and resource matrix (0.11)

`HwAdapterControl(ScsiStopAdapter)` is synchronous and can arrive after the hardware is
already gone. Allocations survive a power stop/resume but may be freed after a stop for
removal or rebalance (G5, G7). The teardown invariants (interrupts disabled at the
controller by INTMS or CC.EN=0, and the timer unable to rearm, before the callback returns)
apply only to real stop transitions, the `ScsiStopAdapter` rows. Resume restores
interrupts and the timer (R3-5).

Three classes of event are kept apart (R6-5); every row below belongs to exactly one:

1. **Flush barriers** (`SRB_FUNCTION_FLUSH`, SYNCHRONIZE CACHE): NVMe Flush in the running
   state, submitted only after every earlier write (and all of its children) has
   completed, whatever the SRB's queue tag (an NVMe Flush covers only commands completed
   before it). No controller state change; later I/O proceeds with no re-init.
2. **Restartable shutdown notifications** (`SRB_FUNCTION_SHUTDOWN` on every NT port and on
   Me, whether or not it turns out to be the last): NVMe Flush, then CC.SHN; the SRB pends
   until SHST reports shutdown processing complete, the wait carried by the timer (the
   adapter is still running, so the timer and interrupts remain the driver's and stay
   active), bounded by RTD3E or a driver limit, with a timeout logged as not clean. The
   adapter is then `shut down`: resources are retained (nothing is freed or handed back),
   and the next I/O re-enables the controller first (CC.EN=0 with RDY=0 acknowledged, then
   the bring-up of 0.10 from the timer, requests held or BUSY under 0.3 meanwhile). No
   teardown invariants apply.
3. **Actual adapter stops** (`ScsiStopAdapter`; `HwAdapterState(FALSE)` on 9x): the stop
   path of 0.10, teardown invariants, resources per G5 and MD-3/MD-4. No I/O follows on
   that adapter instance (on 9x the image is unloaded).

| Event | Callbacks | Hardware action | SRBs | Resources | Notes |
|---|---|---|---|---|---|
| Orderly stop (disable, eject) | `ScsiStopAdapter` | stop path of 0.10 (Flush if the adapter is dirty under the conservative tracking of 0.10, CC.SHN, SHST, CC.EN=0, RDY, each polled within the budget; a Flush within the budget is refuted for the XP x64 and 7 disable stop, so the idle flush of MD-7 (a) bounds the unflushed window and the rest is documented, R6-3) | none expected; any owned are completed per 0.10 | released only after an acknowledged disable; otherwise quarantined (G5); no pointers kept. The port frees its own memory after the return either way (G5, 2000 and XP x64), so quarantine needs memory outside the port (MD-3) | teardown invariants apply. Measured (G5): PCI.sys clears Bus Master Enable and sets D3hot 2-4 ms after the callback returns; on XP x64 and 7 no flush precedes the stop (G8) |
| Stop with device already absent (surprise removal) | **none on 2000, XP x64 and 7 x64** (G2, G5, Q-NT): PnP surprise removal produces no `HwAdapterControl` call, with or without held SRBs; the miniport timer stops ticking. A `ScsiStopAdapter` with the device absent is still handled as below should another port send one | none; all-ones MMIO reads, no config access (not permitted here, R4-2), no waits | the port retires held SRBs itself (as "no such device") on PnP removal of the controller, with no callback to the miniport (G2); any the driver still holds complete only on evidence 2, otherwise at the gone deadline (MD-1) | freed by the port without a callback (G5); driver-owned memory outside the port stays quarantined (MD-3) | teardown invariants cannot be enforced from a callback on 2000/XP x64; whether a `RequestTimerCall` armed before the removal fires afterwards is unmeasured. On real hardware a surprise removal is a link-down (A1), so for callers' buffers the emulator's DMA-open case is the MD-1 residual risk; for driver-owned DMA memory left in the port's extensions it is the broader risk of MD-3 option (a) (R6-1) |
| Resource rebalance | `ScsiStopAdapter`, then a new start | as orderly stop | as orderly stop | BAR mapping, interrupt and allocations reacquired from the new `ConfigInfo` | teardown invariants apply |
| Power down / resume | Measured (G7, 2000, XP x64 and 7 x64, hibernate): `ScsiStopAdapter` at power-down; on resume `HwFindAdapter` (same device extension, not zeroed; **no `ArgumentString`**) then `HwInitialize`, lazily at the first request to the disk on 2000 and XP x64, during the resume itself on 7 x64 (a held START STOP UNIT). `ScsiRestartAdapter` not advertised; `ScsiSetRunningConfig` advertised but never called, and on 7 x64 no `HwAdapterControl` call at all on resume | stop path on power down (Hibernate sends START STOP UNIT (stop) after the last SYNCHRONIZE CACHE, then the stop, G8); on resume, PCI command restored in `HwFindAdapter` (permitted there, 3.4; `ScsiSetRunningConfig` cannot be relied on), then the bring-up of 0.10 | none across the transition | reused: device extension and uncached extension preserved (same VA/PA, contents intact, G7, also 7 x64 in G1); sub-allocator and flags reset | clear `IsShutDown`; interrupts and timer restored by bring-up. S3 not testable on the VMs; hibernate only |
| Flush barrier (class 1) | `SRB_FUNCTION_FLUSH` (passed only when `CachesData` is TRUE), SYNCHRONIZE CACHE | NVMe Flush after the earlier writes (1.6); a successful ordered Flush clears the dirty state (0.10 stop) | the SRB completes only after the NVMe Flush completes | unchanged | running state; later I/O proceeds normally, no re-init |
| Shutdown notification (class 2, restartable) | `SRB_FUNCTION_SHUTDOWN` (passed only when `CachesData` is TRUE) on 2000, XP x64, 7 x64 and Me | NVMe Flush, then CC.SHN; SHST wait carried by the timer; the next I/O re-enables the controller first (class 2 above) | the SHUTDOWN SRB completes after SHST complete (or the logged timeout); other SRBs as in the running state, held or BUSY during a re-enable | **retained**: nothing freed or handed back; timer and interrupts stay the driver's | may or may not be the last notification (2000 sends two with I/O between, G8). Today `IsShutDown` is set before the flush (src/nvme2k.c:1118) and then suppresses timer cancellation (nvme2k_nvme.c:744); it becomes the restartable `shut down` state, and the timer is cancelled unconditionally only on a class-3 stop. **Demonstrated (Q-NT item 3, R6-5 closed):** on 2000 CC.SHN on each of two SHUTDOWNs, SHST at the first tick, the I/O between re-enabled the controller in about 100 ms with the resources retained, data verified after boot; XP x64 and 7 x64 the same with one SHUTDOWN. The SHUTDOWN SRB is untagged on 2000 but **tagged on XP x64 and 7 x64**: never rely on it being untagged or alone. Untested: SHST slower than one tick, and a SHUTDOWN with writes still outstanding |
| Final shutdown trigger | per OS, measured by G8 and G9 with `CachesData` TRUE (below). **Hibernate and Device Manager disable** (2000, XP x64, 7 x64): `ScsiStopAdapter` (class 3), nothing after it. **Shutdown and restart:** no `ScsiStopAdapter` on any NT port. XP x64 and 7 x64: one `SRB_FUNCTION_SHUTDOWN`, final. 2000: two `SRB_FUNCTION_SHUTDOWN` with reads and writes between them, so neither is recognisable as final. 98 SE: `HwAdapterState(FALSE)` (also the removal stop), no SHUTDOWN SRB even with `CachesData` TRUE. Me: `SRB_FUNCTION_SHUTDOWN` with `CachesData` TRUE (with FALSE nothing at all) | by class: a `SRB_FUNCTION_SHUTDOWN` is handled as the restartable shutdown notification above (on 2000 on each, re-enabling for the I/O between); `ScsiStopAdapter` and `HwAdapterState(FALSE)` run the stop path of 0.10. On 9x and Me, whichever of `SRB_FUNCTION_SHUTDOWN` and `HwAdapterState(FALSE)` comes first does the Flush and CC.SHN; a later `HwAdapterState(FALSE)` still runs the stop path | by class (restartable notification or stop) | by class: retained after a SHUTDOWN SRB, per the orderly-stop row after a stop | with `CachesData` FALSE NT gives no shutdown notification at all and CC.SHN is never issued at shutdown/restart; hence `CachesData` TRUE |
| Resume failure | `HwInitialize` on resume | bring-up failure under 0.10 | later SRBs fast-fail | owned until the next stop | state `failed`; **`HwInitialize` never returns FALSE on resume** after CC.EN: G7 measured that both 2000 and XP x64 then hang the I/O (XP x64 never starts an SRB; 2000 starts one that never completes; no reset or timeout in 4 min). `ScsiAdapterControlUnsuccessful` is not used: legacy documentation reserves it, and the base returns it from `ScsiRestartAdapter` (src/nvme2k.c:1351). An OS exception needs measured evidence (G7). The boot-time permission to return FALSE after an acknowledged disable does not apply here (R6-7). **Verified** with the deferred bring-up on 2000, XP x64 and 7 x64 (Q-NT item 2): no hang; 2000 keeps the volume mounted and failing fast, XP x64 and 7 x64 remove the disk after a rescan; no reset, stop or `HwAdapterControl` follows |

`CachesData` (C8, 1.6): **TRUE on NT, decided 2026-10-15 (maintainer), from G8; TRUE
on 9x and Me too, decided 2026-10-15 (maintainer), from G9.** The adapter itself holds no
write data after an SRB completes (bounce images live only inside an incomplete parent);
`CachesData` TRUE is set so that the port passes `SRB_FUNCTION_SHUTDOWN` and
`SRB_FUNCTION_FLUSH` to the miniport. G8 measured that with FALSE the NT miniport gets no
shutdown notification at all (2000, XP x64, 7 x64: no `ScsiStopAdapter` at shutdown or
restart), so CC.SHN would never be issued and the SSD would see every shutdown as unsafe.
G9 measured that on Me `CachesData` TRUE is the only way the miniport hears of shutdown
(`SRB_FUNCTION_SHUTDOWN` after the last write); on 98 SE it changes nothing (no SHUTDOWN
SRB; `HwAdapterState(FALSE)` remains the trigger). Handling:

- `SRB_FUNCTION_SHUTDOWN` on XP x64 and 7 x64 (one, final, tagged): the restartable
  shutdown notification (Flush, CC.SHN, SHST), resources retained; nothing follows.
- On 2000 (two, with I/O between, untagged): the same on each; I/O after the first
  re-enables the controller first (fresh bring-up with the retained resources), so the
  second shuts it down again. Demonstrated with CC.SHN written on each (Q-NT, R6-5).
- `CachesData` is a constant in `HwFindAdapter`, never taken from DriverParameter: the
  resume `HwFindAdapter` gets no `ArgumentString` (Q-NT), so a parsed value would silently
  fall back to its default at every resume.
- On 9x/Me: Flush and CC.SHN on whichever of `SRB_FUNCTION_SHUTDOWN` and
  `HwAdapterState(FALSE)` comes first; I/O after a SHUTDOWN SRB re-enables the controller
  first; `HwAdapterState(FALSE)` is always a class-3 stop (no timer runs after it and the
  image is unloaded, G9).
- `SRB_FUNCTION_FLUSH` reaches the miniport only with TRUE and pairs with SYNCHRONIZE
  CACHE (both map to NVMe Flush).

The SSD's volatile write cache is still reported as the disk's cache: MODE SENSE caching
page WCE = 1 when the controller reports a volatile write cache that is enabled (Get
Features, Volatile Write Cache), else 0; SYNCHRONIZE CACHE maps to NVMe Flush. Tests: on each OS, the trace
shows an NVMe Flush after the last write and before CC.SHN at shutdown, hibernate and
Device Manager disable; I/O issued after a FLUSH or a SYNCHRONIZE CACHE completes normally
with no controller re-init (R3-5); I/O issued after a shutdown notification (CC.SHN
written) completes normally after the controller is re-enabled, with resources retained and
the timer and interrupts active throughout (R6-5).

### 1. Correctness and data safety

| # | Item | Source |
|---|---|---|
| 1.1 | Decode the namespace: identify-namespace layout (NGUID at 104, LBAF at 128), block size from the selected format's LBADS, metadata/protection settings validated. Never misreport. Formats the driver cannot serve (interleaved metadata, protection information) are refused; 4 KiB is never refused (1.7) | both reviews, #2; observed 98/2000/XP failures |
| 1.2 | **Native vs exposed units everywhere.** Separate native block size/count from the exposed 512-byte count (`NSZE * native/512`, checked). Audit READ CAPACITY(10/16), MODE SENSE block descriptors, format and geometry pages, bounds checks and command paths. READ/WRITE(6) count 0 means 256 blocks; (10/16) count 0 means none. NLB limits apply after translation and splitting | C10; nvme2k_scsi.c:138, 587, 754; nvme2k_nvme.c:473 |
| 1.3 | Validate every transfer: 64-bit checked arithmetic, namespace bounds, zero-length commands completed without I/O | #4 |
| 1.4 | MDTS in units of CAP.MPSMIN (checked shift), plus the port's limits negotiated in 0.8; split into children | C10, N5, upstream #12 |
| 1.5 | **Remove the magic-pattern TRIM completely.** Three parts go together: the submission transform (a write whose first 4 KiB matches the pattern becomes a deallocate of the whole request and overwrites the caller's first 16 bytes with a DSM range, nvme2k_nvme.c:541); the completion-side "restoration" (any completed write whose bytes 16-4095 match the pattern gets its first 16 bytes overwritten with the pattern's, nvme2k_cpl.c:448, which corrupts the caller's buffer even when no TRIM happened); and the enabling IOCTLs `TRIM_MODE_ON`/`OFF` and their device-extension state (`TrimEnable`, `TrimPattern`; nvme2k_scsi.c:1043). Any future deallocate interface validates and translates ranges, joins overlap/barrier scheduling and never rounds outward. Regression test: with the old IOCTL issued (now rejected), write a buffer whose bytes 16-4095 match the pattern and whose first 16 bytes differ; verify the disk contents and that the caller's buffer is unchanged after completion | C5, R2-3; nvme2k_nvme.c:541, nvme2k_cpl.c:448, nvme2k_scsi.c:1043 |
| 1.6 | **Barriers and durability.** Parents carry sequence numbers. FLUSH waits for all earlier writes and their children; ORDERED waits for earlier tasks and blocks later ones (ordering does not imply flushing). FUA goes on every write child, edge write-backs included. Cover SYNCHRONIZE CACHE, `SRB_FUNCTION_FLUSH`, shutdown, IMMED, error propagation. `CachesData`/WCE as decided in 0.11 | C8, #9, R2-2; nvme2k_scsi.c:229, 698; nvme2k.c:186 |
| 1.7 | **4 KiB-sector namespaces are supported (maintainer decision 2026-10-14: never refuse them).** See the section below. Lands after phase 0, 1.1-1.6 and 1.9 | maintainer |
| 1.8 | Correct INQUIRY EVPD decoding and bounded responses; reject unsupported SERVICE ACTION IN | #15, N8 |
| 1.9 | **Namespace selection and identity.** Today NSID 1 is hardcoded for identify and I/O. First release: expose the **lowest active NSID** as target 0, LUN 0; active NSIDs come from the active namespace list (Identify CNS 02h) or, on NVMe 1.0 controllers without it, from probing 1..NN for a non-zero NSZE. If that namespace's format cannot be served, no disk is exposed and the reason is logged; other namespaces are logged and ignored. Other target/LUN values are rejected as today. Multiple namespaces as several LUNs is a later item. Identity is (NGUID if non-zero, else EUI64 if non-zero, else controller serial + model + NSID), plus NSZE and LBA format; it is recorded at start and compared on reconnect (2.6). Tests: controller whose only active namespace is NSID 2; controller with several namespaces | R2-8; nvme2k_nvme.c:274, 506; nvme2k.c:975 |

### 2. Removal and lifecycle (the reason this fork exists)

| # | Item | Source |
|---|---|---|
| 2.1 | **Presence detection and transitions** over the states defined in 0.7: all-ones CSTS/MMIO moves to `gone`, which holds issued SRBs until evidence 1 or 2 (the G3 notification) or the gone deadline of 0.10 (MD-1 outcome); all-ones is never itself the evidence. CSTS.CFS or a failed reset moves to `failed` | C6, #7, R3-2, R4-1 |
| 2.2 | **Terminal removal contract** (applies on path down, evidence 2 of 0.10, and at the gone deadline under MD-1 option (b), where held SRBs complete with this status while driver-owned DMA memory stays quarantined; ordinary I/O errors use the parent error mapping). One gone-state gate before every dispatch. Complete held SRBs promptly with `SRB_STATUS_NO_DEVICE` (consistent `ScsiStatus`, conservative transfer length), fast-fail later media requests and discovery probes (INQUIRY, TEST UNIT READY must fail, not return BUSY), and keep issuing readiness notifications so port-held requests drain. On NT, `BusChangeDetected` once per transition. Determine per OS (2000, XP, XP x64, Vista, 7) which status stops class-driver retries; `SRB_STATUS_SELECTION_TIMEOUT` is a candidate to test, not a known answer. Measured: on 2000 and XP x64 both NO_DEVICE and SELECTION_TIMEOUT stop retries (NO_DEVICE stays the default, G2); on 7 x64 NO_DEVICE at the deadline gave the application error 2 at once and the disk was gone 1 s later (Q-NT, G2 run B; SELECTION_TIMEOUT not run); on 98 SE and Me NO_DEVICE ends IOS retries after 6 tries (G9); Vista and XP x86 not yet run | C7, R2-4, G2, G9; nvme2k_scsi.c:68, 204; nvme2k.c:1040; XP x64 hang |
| 2.3 | Watchdog armed while any command is outstanding; per-command deadlines; presence checks independent of unrelated activity | N1 |
| 2.4 | Bounded waits within the stall budget of 0.10; no multi-second stall loops on removal, shutdown or init | #6, #7 |
| 2.5 | Safe bring-up failure per the bring-up rule of 0.10: at boot or start, failure is returned to the port only before CC.EN was written or after an acknowledged disable; on resume never (R6-7); otherwise the adapter stays started in `failed` and keeps its memory. Resume-failure handling verified by the combined G1 + G7 prototype on 2000, XP x64 and 7 x64 (Q-NT) | #6, R3-1, R6-7, Q-NT |
| 2.6 | **Reconnect** establishes fresh controller state and re-reads namespace identity (1.9). Before trusting anything it checks the reopen invariant of 0.10: CC.EN=0 and RDY=0, or else an isolation-contract violation is logged and the controller disabled and acknowledged before any reuse (R4-3). Same identity: the disk resumes as the same LUN. Different identity, or changed NSZE/format: the old disk is gone (2.2) and the new one arrives as a new disk (fresh INQUIRY/VPD serial, `BusChangeDetected` on NT). Requests admitted for the old disk never reach the replacement: the admission gate stays closed until identity is resolved, and anything admitted before the drop fails with the terminal status. Never resumes an old bounce image or pending parent. Identity is what the controller and namespace report, so a reconnect that changes only the contents (the same serial, NGUID and EUI64 with a different backing image) is undetectable by this policy and is treated as the same disk; user notes say so | C6, C13, R2-8, R3-14 |
| 2.7 | **Windows 9x lifecycle contract**, built on the 0.12 prototype. IOS has no `BusChangeDetected`/`BusyRequest` (legacy `SRB.H`), and `RequestTimerCall` is unavailable during `SYS_CRITICAL_EXIT`. Write a 9x callback/notification matrix: admission, completion, critical shutdown, Config Manager stop/start/remove (as driven by xhci98's CM, see usb4-emu `REENUM-EXPERIMENT.md`), real-mode handoff (`HwAdapterState`, N3), rescan, and the helper VxD's role if 0.12 required one. Verify tag delivery and mapped buffers on 98 SE and ME; support untagged operation; drain RMW work before phases where its progress mechanism stops. **Measured by G9** (98 SE, Me): no tags (`SRB_FLAGS_QUEUE_ACTION_ENABLE` never set, `QueueTag` a rolling number, depth 1 per LU although tagged queuing is advertised; `NonTaggedInFlight` must be cleared after `HwResetBus` and covered by fast-fail); buffers always mapped system-linear addresses; transfers up to 64 KiB on 98 SE and **at most 61,440 bytes on Me**; `RequestTimerCall` works but fires late (100 ms requests after 100-315 ms on 98 SE, up to 1.5 s on Me), so every deadline is elapsed time; **no timer runs after `HwAdapterState(FALSE)`** (armed there, it never fires), so the RMW drain and the whole stop finish inside that callback; `HwAdapterState(TRUE)` comes once per load before `HwInitialize`; the `.mpd` is unloaded at every removal and reloaded fresh (`DriverEntry`, new static data); no SHUTDOWN or FLUSH SRB on 98 SE; **on Me `TimeOutValue` is garbage** for TEST UNIT READY and READ CAPACITY (0 on 98 SE), so no timeout is derived from it on 9x; `SRB_STATUS_NO_DEVICE` ends IOS retries after 6 tries; discovery uses `ConfigInfo`'s bus and slot, not the NT4 `HwContext` scan; runtime `ScsiPortGetBusData` on the own function works on 9x from `HwStartIo` and the timer callback (all-ones after surprise removal; a presence signal only), runtime config writes untested; only CONFIGMG removes a surprise-removed volume. Me's port never calls `HwAdapterState(FALSE)` (MD-5) | C11, N3, R2-6, G9; tools/w98ddk/inc/SRB.H:541 (`SCSI_NOTIFICATION_TYPE`), nvme2k_nvme.c:728 |

### 3. Platform correctness

| # | Item | Source |
|---|---|---|
| 3.1 | PnP-constrained discovery (only the supplied function); `ConfigInfo` bus/slot consistent | N4; backport |
| 3.2 | Port/HAL interrupt routing on PnP targets; manual routing only where needed | N6; backport |
| 3.3 | Full 64-bit BAR decoding, sizing with decode disabled | #13 |
| 3.4 | **PCI config access only from permitted callbacks** (R4-2). NT: `ScsiPortGetBusData` only in `HwFindAdapter` and `ScsiSetRunningConfig`, `ScsiPortSetBusDataByOffset` likewise; no raw mechanism #1 (CF8/CFC) access on NT, since it cannot be serialised against the HAL's config cycles on another processor. The base reads and writes the PCI command register from `HwInitialize` (src/nvme2k.c:925); that moves to `HwFindAdapter`. 9x: per G9. Return lengths (the helpers request 256 bytes and ignore the count, src/utils.c:752, 770, 788): request only the bytes needed from offset 0; a return of 0 (no such bus) or 2 (no function; only the invalid Vendor ID is returned) is "not found" and `HwFindAdapter` returns `SP_RETURN_NOT_FOUND`; a Vendor ID of 0xFFFF is the same; a short return makes every field beyond it unavailable, and a needed field that is unavailable fails `HwFindAdapter` with `SP_RETURN_ERROR` before CC.EN is written. `ScsiPortSetBusDataByOffset` must return the requested length, and a Bus Master Enable or Command change is read back. No runtime callback reads config space for presence on NT (presence comes from MMIO; isolation from 0.10) | #10, R4-2; src/utils.c:752, 770, 788; src/nvme2k.c:925 |
| 3.5 | (Moved to 0.9.) | #14, R2-10 |
| 3.6 | Windows 9x cache-mode fallback: supported VMM memory-type call or proper TLB/cache sync | N7 |

### 4. Crash dump and hibernation

OS applicability: Windows 98 SE and ME have no miniport crash-dump path and their
hibernation does not go through a dump-mode miniport, so phase 4 covers NT only; what 9x
standby/hibernate requires of the miniport is part of 0.12 and 2.7. On NT, three scenarios
are separate deliverables (R2-7):

| Scenario | What it needs | 2000 / XP / 2003 x64 | Vista / 7 |
|---|---|---|---|
| A. Boot from another disk, stor98 data disk attached: crash, hibernate/resume | stor98 is not the dump driver; power stop/restart per 0.11 | required | required |
| B. Boot from a translated stor98 disk (runtime only) | firmware or loader access to the NVMe disk (INT 13h option ROM or equivalent); outside this driver | only where firmware provides it | same |
| C. Crash dump and hibernate/resume on the stor98 boot disk | B plus the dump-mode engine; resume also needs firmware/loader reads of the hibernation file | only where B works | same |

| # | Item | Source |
|---|---|---|
| 4.1 | **Dump-mode detection and allocation.** Recognise dump mode both from the dump stack calling `DriverEntry` with NULL arguments and from the `ArgumentString` dump indication in `HwFindAdapter` (G6, 2000 and XP x64, crash and hibernate: both present every time, `ArgumentString` exactly `"dump=1;"`; the dump instance is a fresh image with fresh statics); allocate once there; XP-and-earlier dump/hibernate uncached allocation **under 32 KiB** including physical-alignment slack (G6: 28 KiB granted, 64 KiB returns NULL on 2000; the base asks 64 KiB). Align every queue and PRP page against the **physical** address (G6: the 28 KiB block's physical base was not page aligned, and offset-aligned queues made the controller fail to start), and leave a page of slack. **Use the access range the port supplies in `ConfigInfo`** in dump mode; never size the BAR there (the write-ones read-back returns the BAR value itself, so the computed length is wrong and `ScsiPortGetDeviceBase` returns NULL). The G3 link-state table stays unused in dump mode (its imports did not disturb the dump driver load) | C9, R2-7, G6; nvme2k.h:80, nvme2k.c:430 |
| 4.2 | Dump-mode engine: one parent, one child at a time, edge buffer reused sequentially, a completion pump that works under the dump port. No timers, DPCs, registry or wall-clock services. 4 KiB translation stays active. **Dump-mode controller initialisation** is distinct from later bus resets. Initialisation disables the controller left by the runtime driver (RDY=0 acknowledged), re-enables it, creates the I/O queues and identifies the namespace **before init returns**, since the dump port sends I/O immediately; it polls synchronously in stalls of at most 100 µs up to the total limit G6 establishes (no timers exist; 0.10 stall budget table). A later `HwResetBus` in dump mode follows the dump-port contract: Microsoft's guidance is that dump miniports disregard bus-reset requests, so it performs no controller reset and returns TRUE; a command that does not complete fails the dump through the completion pump's deadline. The dump disk keeps the same target/LUN and identity (1.9) as at runtime. **Measured (G6, 2000 and XP x64):** synchronous init in 100 µs stalls took 40-60 ms, 2.4-2.6 s with RDY delayed 2 s, with no timeout from the dump port; the crash dump instance starts from an enabled controller (CSTS.RDY=1), the hibernate one from a controller the runtime stop already disabled; the dump port sends **one bus reset right after the first INQUIRY** (2000 and XP x64), which is disregarded; on 2000 a held write drew one more reset 7.7-15 s later, also disregarded, and XP x64 sent none in 90 s. **The pump deadline is mandatory:** neither dump port times out a lost completion (2000 waited indefinitely after its reset; XP x64 sent nothing), so a dropped completion hangs the dump at the bugcheck screen unless the engine's own deadline (counted in polls or stall time in `HwInterrupt`/`HwStartIo`) completes the SRB with an error. The dump port polls `HwInterrupt` (13-60 polls per write); no timer callback runs. Tests: dump and hibernate with RDY delayed at init; a bus-reset request during dump I/O; a dropped completion ends the dump with an error, not a hang | C9, R2-7, R3-6, G6 |
| 4.3 | Validate scenario A on every NT target; B and C on translated (4 KiB) volumes where the firmware supports them. G6: the dump stack only ever targets the boot/system disk, so scenario C needs B; **2000 and XP x64 boot and run from the tunnelled NVMe** in usb4-emu (SeaBIOS hooks it into INT 13h, stor98 as a boot-start driver) and scenario C passes there (crash dump and hibernate/resume). Whether the P14s firmware (UEFI CSM) boots a Thunderbolt-attached NVMe at all is outside the driver. 7 x64 not run | C9, R2-7, G6 |

### 5. amd64 target

The minimal amd64 build exists from 0.1. Phase 5 adds what makes it a product: INF
`NTamd64` section modelled on upstream 1.2.0.0's `WDM/sources` target, the signing story
(test-signed like xhci98's x64 binaries), and the full test matrix on XP x64, Vista x64 and
7 x64. Already proven buildable: upstream nvme2k 1.2.0.0 builds with WDK 7.1 `wnet` amd64
and runs on XP x64 (R2-10).

### 6. Packaging

INFs for 9x (`$CHICAGO$`; 0.12 decided that no helper VxD is needed) and NT (x86, amd64), one
package per family; `NOTICE` with all upstream notices; version resources naming the
lineage; user notes on 4 KiB drives and partition alignment.
The NT INF installs a demand-start service for data disks (0.1.1.0); booting from NVMe (boot-start, and on Vista/7 x64 a trusted signature) is not supported yet.
**The NT service name must equal the image base name** (`stor98` / `stor98.sys`) on every
NT target (G6): XP x64's dump loader derives the dump image from the service name, and a
mismatch (upstream nvme2k's x64 pairing, service `nvme2k` with `NVMe2Kx64.sys`) made the
crash dump driver fail to load (events 45 and 49) and hibernation fail with 0xC0000034.

## Parent error mapping

How a parent completes when a child fails (R2-4). The first error wins; no new children are
issued after it; children already in flight are allowed to complete or are quiesced under
0.10, and only then does the parent complete. Children that completed successfully stand
(see the durability statement). `DataTransferLength` is set to 0 on every failure in the
first release. Sense data follows the sense rules below.

| Child outcome | `SrbStatus` | `ScsiStatus` | Sense (key/ASC/ASCQ) |
|---|---|---|---|
| Unrecovered read error, including on an RMW pre-read of a write | `ERROR` | CHECK CONDITION | MEDIUM ERROR 03/11/00; Information per the sense rules |
| NVMe Write Fault | `ERROR` | CHECK CONDITION | MEDIUM ERROR 03/03/00 (PERIPHERAL DEVICE WRITE FAULT), as the NVM Express SCSI Translation Reference maps Write Fault |
| LBA out of range | `ERROR` | CHECK CONDITION | ILLEGAL REQUEST 05/21/00 |
| Invalid opcode / invalid field (a driver bug) | `ERROR` | CHECK CONDITION | ILLEGAL REQUEST 05/20/00 or 05/24/00; logged |
| Aborted by the driver's own abort | `ABORTED` | GOOD | none |
| Aborted by an acknowledged reset (0.10) | `BUS_RESET` | GOOD | none |
| Controller fatal (CFS) or reset failed, device present | `ERROR` | CHECK CONDITION | HARDWARE ERROR 04/44/00; adapter enters `failed`, later requests fast-fail the same way |
| Path down (evidence 2 of 0.10), or the gone deadline passed without evidence (MD-1; driver-owned memory quarantined) | per 2.2 | per 2.2 | per 2.2 |
| Cancelled with required work suppressed, all issued children successful (0.10, parent cancellation) | as the cause: `ABORTED`, `BUS_RESET`, or per 2.2 | as the cause | as the cause; never SUCCESS |
| Other NVMe status | `ERROR` | CHECK CONDITION | HARDWARE ERROR 04/00/00; NVMe status logged |

**Sense rules (R3-9).**

- Autosense is built only when `SenseInfoBuffer` is non-NULL, `SenseInfoBufferLength` is at
  least 18 (fixed format) and `SRB_FLAGS_DISABLE_AUTOSENSE` is clear. Then exactly 18
  bytes of fixed-format sense are written, never more than `SenseInfoBufferLength`, and
  `SRB_STATUS_AUTOSENSE_VALID` is set. Otherwise no byte of the sense buffer is written,
  `AUTOSENSE_VALID` stays clear, and the sense is kept per LUN for a following REQUEST
  SENSE, for which a handler is added (the base has none). Both base builders
  ignore `SRB_FLAGS_DISABLE_AUTOSENSE` (src/nvme2k.c:987, nvme2k_cpl.c:489).
- **Ownership of kept sense (review round 2).** Each kept entry belongs to the request
  that failed without autosense, and nothing else consumes it: autosense delivered for
  another request leaves it alone (two tagged requests failing in one completion batch,
  one without autosense and one with it, used to lose the first one's sense); several
  such failures keep one entry each, in completion order (up to 4), and REQUEST SENSE
  returns and removes the oldest. On overflow (review round 3, N4) the further failure's
  sense is dropped and logged but its place is kept as a "sense lost" placeholder behind
  the kept entries; later failures queue as placeholders while one is pending, so a freed
  slot never gives a dropped request a later request's sense: REQUEST SENSE reaching a
  placeholder returns NO SENSE. Placeholders saturate at 255 (review round 4); from then on the loss state is sticky: every REQUEST SENSE past the stored entries returns NO SENSE and nothing is stored until a start, resume, acknowledged reset or new command ends the kept sense. Kept sense ends
  when a new command other than REQUEST SENSE, INQUIRY or REPORT LUNS starts for the LU,
  at every start and resume (`EngResetSoftState`, `HwFindAdapter`) and at an acknowledged
  reset. Implemented in src/sense.c; host unit test `tests/unit` (NT and 9x `SRB.H`).
- The Information field and its VALID bit are set only when the failing LBA is actually
  known: the failing child covered exactly one native block (as an RMW pre-read does), or a
  later item reads the NVMe Error Information log. The value is the first exposed LBA of
  that native block. If it exceeds 0xFFFFFFFF, VALID stays 0 and Information stays 0: the
  fixed format cannot hold it and the value is never truncated. Descriptor-format sense is
  not used, since the legacy class drivers expect fixed format.

Tests: inject a media error on the native block that an unaligned write must pre-read
(QEMU `blkdebug` on the backing image); the write fails with 03/11/00, no write-back of that
block is issued, other blocks are unchanged. Sense construction cases in TESTING.md.

## 4 KiB-sector namespaces

None of the target systems supports a disk with 4 KiB **logical** sectors (native 4Kn):
Windows 9x, 2000, XP and Vista assume 512-byte sectors, and Windows 7 supports 512e
(512-byte logical, 4 KiB physical) but not 4Kn. stor98 therefore always presents
**512-byte logical sectors** and translates to the namespace's block size in the driver.
Supported native sizes: 512 and 4096 bytes (mandatory); larger sizes only within the bounce
buffer budget, otherwise refused with a logged reason.

### Request handling (on the phase 0 engine)

- **Aligned** (start and length multiples of the native block): LBA and count divided by
  the ratio, one or more direct children. No extra media traffic.
- **Unaligned read:** head/tail native blocks read into bounce buffers and copied out; the
  aligned middle is read directly. Head and tail in the same native block share one
  bounce read.
- **Unaligned write:** head/tail blocks are read, patched and written back (read-modify-write)
  through bounce buffers; the aligned middle is written directly. Shared head/tail block
  coalesced.
- **Overlap (symmetric):** every admitted read and write registers its native-block range
  before dispatch. A read-modify-write takes exclusive ownership of its edge blocks only
  after earlier conflicting commands (aligned writes included) have drained, holds it
  through write-back completion or quiescence, and blocks later conflicting requests.
  Multiple ranges are acquired atomically or in a fixed order. Callback serialization is not
  enough: commands already executing on the SSD must be covered. First implementation: one
  parent per namespace at a time.
- **Barriers:** as item 1.6, with FUA on edge write-backs.
- **Failure and removal:** a failing or interrupted child fails the whole parent with the
  status from the parent error mapping (2.2's terminal status only for path down, evidence 2
  of 0.10, or the gone deadline under MD-1). An RMW whose write-back was suppressed never
  completes with success (parent cancellation, 0.10); bounce buffers are released only under
  0.10 and stay quarantined after the gone deadline. After any recovery, edges are pre-read
  again; a stale bounce image is never written.

### Durability statement (for users)

A translated SRB is not transactional: removal or power loss can leave a multi-block write
partly applied, and data not yet flushed can be lost, as with any disk. Rewriting the
untouched neighbouring sectors of an edge block is safe when the SSD writes that single
native block atomically (both old and new images contain the same neighbour bytes); NVMe
reports power-fail atomicity separately (AWUPF/NAWUPF), and the driver can log it. Do not
claim the same guarantees as any particular 512e hard disk.

### Reporting and alignment

- Capacity: after checking NSZE is non-zero (a zero-size namespace is not exposed), compute
  `exposed_count = NSZE * (native/512)` with overflow checking and
  `last_exposed_lba = exposed_count - 1`. READ CAPACITY(10) returns
  `min(last_exposed_lba, 0xFFFFFFFF)` (0xFFFFFFFF tells the OS to use RC16). READ
  CAPACITY(16) returns `last_exposed_lba` (not the count), logical block length 512,
  LBPPBE = log2(native/512) (3 for 4 KiB) and lowest aligned LBA 0. The legacy `scsi64.h`
  RC16 layout marks these bytes reserved and must be updated (R2-9).
- Capacity tests check exact values (R2-9, R3-10). 512-byte namespaces: exposed counts
  2^32 - 1, 2^32 and 2^32 + 1 sectors (RC10 = 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF;
  RC16 = 0xFFFFFFFE, 0xFFFFFFFF, 0x100000000). 4 KiB namespaces expose NSZE * 8 sectors, so
  the count is always a multiple of 8: exposed counts 2^32 - 8, 2^32 and 2^32 + 8 (NSZE
  0x1FFFFFFF, 0x20000000, 0x20000001; RC10 = 0xFFFFFFF7, 0xFFFFFFFF, 0xFFFFFFFF;
  RC16 = 0xFFFFFFF7, 0xFFFFFFFF, 0x100000007). In every case I/O to `last_exposed_lba`
  succeeds and to `last_exposed_lba + 1` fails with 05/21/00.
- Whether Vista or 7 actually aligns new partitions through this SCSIport stack is unknown
  (Microsoft's advanced-format support depends on OS updates and was documented for
  Storport/USBSTOR). Test the CDB traffic and `StorageAccessAlignmentProperty` per OS before
  promising it.
- Media traffic of an unaligned write (R3-11): with E the number of distinct native blocks
  the request covers only partly (0, 1 or 2; a shared head/tail block counts once) and B
  the native block size, traffic = the write rounded out to native blocks + E x B of
  pre-reads = `aligned_middle_bytes + 2 x E x B`. The write-back of an edge block is part of
  the rounded-out write and is not counted again. Aligned requests move exactly their own
  bytes; an unaligned read moves the request rounded out to native blocks.
- Alignment depends on each request's absolute LBA and the filesystem data-area offset, not
  only the partition start. Examples (B = 4 KiB): a 4 KiB write at exposed LBA 63 covers
  exposed LBAs 63-70, partly covering native blocks 7 and 8 with no aligned middle (E = 2):
  8 KiB written + 8 KiB pre-read = 16 KiB of media traffic; a single 512-byte write (E = 1)
  costs 4 KiB written + 4 KiB pre-read = 8 KiB. User notes recommend partitions starting at sector 2048 and FAT data
  areas aligned to 4 KiB, with measured numbers from the benchmark below.
- Presenting 512-byte sectors does not convert a disk already partitioned and formatted as
  native 4Kn on another OS; that layout will not be readable here. Firmware (INT 13h) access
  and booting from a translated disk are separate questions from data-disk use (phase 4,
  scenarios B and C).

### Tests (deterministic, in usb4-emu)

- Every start offset modulo 8, same-block head/tail, single sector, cross-block, MDTS
  boundary; fragmented and non-page-aligned caller buffers; DMA above 4 GiB where the OS
  allows it.
- Partial/partial and partial/aligned overlaps in both arrival orders; reads during a
  read-modify-write; exhausted bounce/PRP/CID pools; tagged, untagged and ORDERED traffic.
- Fault injection at exact points: removal/reset after the pre-read, before the write-back,
  during each child and before parent completion; delayed and stale completions; lost
  interrupts; media error on a pre-read. These need the harness work in TESTING.md.
- FUA/flush failures; sentinel patterns verified outside the requested sectors.
- Capacity boundaries as above on a sparse image over 2 TiB; dump and hibernate per phase 4;
  every OS and architecture.
- DMA bounds (no byte beyond the caller's or bounce buffer) checked separately from total
  media traffic (which read-modify-write legitimately increases).
- Benchmark: partitions at 63 and 2048, small random and large sequential I/O, latency.

## Removal acceptance (all OSes)

Scope: a non-critical data disk (not the boot, system, paging or dump disk); surprise
removal of a critical disk may crash the OS and is not a gate (R2-7). The XP x64 sequence
becomes a gate, with the thresholds in TESTING.md: after `tunnel surprise` during I/O, the
driver detects the loss and completes everything it holds within a bounded time, the
application gets an error within a bounded time, the number of pending SRBs stays bounded,
the volume is reported gone by T4, at most T0 + 60 s, and there is no recovery loop. Recovery
events (NT event 117, bus reset, and event 9, timeout; on 9x the driver's reset and timeout
counters) are counted over the whole 10-minute window starting at T0, with one initial
recovery allowed: at most one bus reset and at most one timeout, both before T4, and none
after T4 (R4-6; TESTING.md states the same criterion). Two cases are tested separately,
with the measurement definitions in TESTING.md:

- **Sustained absence:** the tunnel stays down for the whole 10-minute window starting at
  the gate close (T0); every threshold above is met within it.
- **Reconnect:** a separate run in which `tunnel up` plus the OS rescan (or xhci98's CM)
  brings the disk back working, under the identity rules of 2.6 and the reopen invariant
  of 0.10.

On 9x the volume does not disappear by itself (R6-9): `SRB_STATUS_NO_DEVICE` ends IOS's
retries and the application gets its error, but the drive letter stays until CONFIGMG
removes or re-enumerates the devnode (G9). T4 on 9x therefore needs a named actor
(xhci98's CM, or the test harness standing in for it) and is measured from that actor's
call; the miniport's standalone part (error completion, T1-T3) is judged separately from
the CM-assisted disappearance (TESTING.md, measurement definitions).

Acceptance on an OS and architecture also needs G3 qualified there (per-port qualification,
R4-7), since all-ones reads are not isolation evidence (R4-1). Without it, the gone-deadline
outcome of MD-1 applies on that OS: held SRBs complete at the deadline (the gone deadline
equals the T1-T2 budget) with driver-owned memory quarantined, under the accepted residual risk, and the
acceptance record for that OS states that it relied on the MD-1 exception rather than on
isolation evidence.

## Maintainer decisions

- **MD-1 (R4-1): consequence at the gone deadline. Decided 2026-10-14 (Yeo Kheng Meng):
  option (b).** When presence is lost and neither an acknowledged disable nor a G3
  link-down statement arrives by the gone deadline, the driver completes the held SRBs with
  the terminal device-gone status (2.2) and quarantines (never frees) driver-owned DMA
  memory. The deadline stays a tunable (default 5 s, `gone=` on NT; README "Settings"); evidence that arrives
  earlier always takes precedence; the event is logged and counted. Accepted residual risk,
  as an explicit owner-accepted exception to the DMA-ownership principle: if the all-ones
  reads were not caused by a real link-down, late DMA may corrupt pages the OS has reused.
  This is the only such exception (Principles; 0.10 gone state; AGENTS.md). Rejected
  option (a) was to keep holding the SRBs until the OS removal path retired them, leaving
  surprise removal without xhci98's CM unsupported. Tests: TESTING.md "Gone deadline, no
  evidence" and the negative test "Presence lost with the DMA path open".
- **MD-2 (R4-1, R4-3, M1): assumption A1 and the CM obligations as the basis of
  evidence 2. Decided 2026-10-14 (Yeo Kheng Meng): accepted, to be verified on real
  hardware** (the P14s Gen 1 with an NVMe enclosure, including one with an internal PCIe
  switch). Basis: PCIe Base §2.9.1 (Upstream Port reset on DL_Down and hot-reset
  propagation through a Switch), §3.2.1 (link-down treated as hot reset), §6.6.1
  (conventional reset); NVMe over PCIe Transport §3.3 and NVMe Base §3.7.2 (a conventional
  reset is a controller-level reset). The reset propagates through an enclosure's internal
  switch to the endpoint. CM obligations, owned by xhci98: (1) a tunnel is re-established
  only to an endpoint reset since the last one; (2) the link-down statement is sent only
  after completed quiescence: the tunnel's PCIe path closed and the endpoint reset or
  drain completed (0.10, evidence 2). The driver detects a reopen violation but cannot undo
  DMA that happened before its first read.
- **`CachesData` = TRUE on NT. Decided 2026-10-15 (maintainer)**, from G8: with FALSE the
  NT miniport gets no shutdown notification and CC.SHN is never issued at shutdown or
  restart (0.11). For 9x and Me, TRUE is **decided 2026-10-15 (maintainer)**: G9 measured
  that it gives Me an `SRB_FUNCTION_SHUTDOWN` at shutdown (Me's only shutdown trigger) and
  changes nothing on 98 SE.
- Conditional, raised only if a gate fails: G1, G4 and G5 each name "blocked pending a
  maintainer decision" for the port where they fail; such a port is added here with the
  gate's recorded result. G1 and G4 passed where run; G5 failed on 2000, XP x64, 7 x64 and
  Me (MD-3 to MD-5).

### Decided 2026-10-15 (maintainer, Yeo Kheng Meng)

- **D1 (9x CM services) = allowed:** xhci98's CM, a WDM driver, may call documented
  Windows 98/Me VMM, CONFIGMG and PCI-enumerator services through the standard `INT 20h`
  VxD service call (no VxD binary is built). This is within MD-5's "no VxD, no
  undocumented APIs" (Q-9x, branch qual-9x).
- **D2 (9x eject cancel) = remove and re-enumerate:** once `Query_Remove_SubTree` has
  succeeded the volume's I/O is already failed by IOS and cannot be resumed in place, so a
  cancelled or non-clean eject ends in `Remove_SubTree` + `Reenumerate_DevNode` (the disk
  returns as a fresh device). The CM sequence is query (file-system quiescence and veto) →
  REMOVE_PENDING → CLEAN ack → `Remove_SubTree` → teardown (Q-9x).
- **D3 (write-through) = `WriteThrough` setting, default `auto`:** `auto` turns write-through
  on only on Windows Me (detected by VMM version 4.90), where removals that bypass the CM
  get no stop callback; other OSes keep the volatile write cache. Mechanism when on:
  volatile write cache off via Set Features, verified with Get Features at every bring-up,
  falling back to FUA on every write if the drive refuses. A registry value `WriteThrough`
  (`auto` | `off` | `cacheoff` | `fua`) in the adapter's driver key (9x, read through VMM
  registry services at HwFindAdapter) or the service's Parameters key (NT, read in
  HwFindAdapter and kept in the device extension because no ArgumentString arrives at
  resume) overrides it; the INF writes the default. Cost to be measured on a real SSD.
  0.3.0.0 and 0.3.0.1 read the 9x value as `WriteThrough`. From 0.4.0.0 it is
  `StorWriteThrough` in HKLM `Services\stor98`, the old name is not read, and no INF
  writes it ("0.4.0.0 plan" item 3).
- **D4 (interface 3 in include/stor98cm.h):** the first Codex review found it not ready; the maintainer chose to revise all findings and requalify, and approved xhci98's locator (HKLM `Services\stor98` `LinkState` with a CM-hosted `HKEY_DYN_DATA` current-boot proof) and `REMOVE_PENDING` layout (Flags bit 3, `RemoveSequence`/`RemoveAck`). Revised and requalified on 98 SE and Me ([gates/Q-9x.md](gates/Q-9x.md), "Interface 3 revision"). **Adopted 2026-10-16 (maintainer) as cleared by Codex at qual-9x `cf56ccd`**; integrated in the product 9x receiver on `int-0.2` ([INTEGRATION-0.2.md](INTEGRATION-0.2.md)).
- **D5 (rename, 2026-10-16):** the project is renamed Stor98 (`stor98`) before its first
  release, for the NVM Express trademark guidelines; 0.2.0.0 is not cut and the first
  release is 0.3.0.0. The CM interface takes the new names, signatures `'S98L'` and
  `"STOR98CM"` and version 4 in every version field, with no old-name compatibility. The former
  name was never released, so there is no migration. The same release splits the INQUIRY vendor
  and product at the model number's first word. [RENAME-STOR98.md](RENAME-STOR98.md).

- **MD-3 = option (a)**: DMA-reachable memory stays in SCSIport's allocations on NT. The
  maintainer accepts the additional risk stated under MD-3(a) below (a late DMA into memory
  freed after a stop, in the 2-4 ms before the OS clears Bus Master Enable, or after a
  removal without isolation evidence), as a second, explicit exception beside MD-1. Option
  (b) is not pursued unless evidence shows the window matters. **MD-3 (a) also covers
  Windows 7 x64**, where Q-NT measured the same G5 failure (the port frees and reuses its
  extensions at once after an unacknowledged removal stop).
- **MD-4 = adopt the `_PageAllocate` pool** on 98 SE and Me: DMA memory comes from VMM
  outside the port, freed only with isolation evidence, leaked otherwise. Prototype it
  (allocation, DMA, unload lifetime, failure) before 0.10 on 9x. A block a previous
  instance left quarantined is released by E1 (the CM table shows DOWN_QUIESCED for its
  generation or a later one) or E2 (a later load of the same bus and slot has shown its
  controller disabled, CC.EN=0 and RDY=0); a later generation alone is not evidence
  (2026-10-18, below).
- **MD-6 = adopt the shared link-state table on NT** (G3, branch phase0-g3g6,
  [gates/G3.md](gates/G3.md); interface [`include/stor98cm.h`](../include/stor98cm.h),
  0.10 evidence 2): the CM publishes a non-paged table through the callback
  object `\Callback\Stor98LinkState`; stor98 opens it once in HwFindAdapter and polls it
  from its timers and HwStartIo. The `ntoskrnl` imports this needs are accepted as a
  deliberate step outside the strict SCSIport miniport contract. The IOCTL_SCSI_MINIPORT
  route is rejected (measured to arrive after the gone deadline under a saturated queue).
- **Interrupt report through the registry on NT, decided 2026-10-16 (maintainer).** For
  the polling mode's user-readable interrupt report ("Polling mode", under the 0.4.0.0
  plan), the NT build writes a REG_SZ value in `HKLM\SYSTEM\CurrentControlSet\Services\stor98`
  from `HwFindAdapter` with `ZwOpenKey`, `ZwSetValueKey` and `ZwClose` (ntoskrnl.lib). It
  does so at PASSIVE_LEVEL, only at a start (never at a resume, whose IRQL is unmeasured)
  and never in a dump instance. Like MD-6's imports, this is a deliberate step outside the
  strict SCSIport miniport contract. The alternative inside the contract,
  `ScsiPortLogError`, was rejected: it would put an error event that reads as a
  controller fault ("internal adapter error") in the System log at every start. The 9x
  build uses the documented VMM registry services (D1's table).
- **MD-5 = CM-driven removal handshake + write-through on Me (decided 2026-10-15,
  maintainer), no VxD and no undocumented APIs.** Design (Codex assessment): the 9x
  link-state table gains a REMOVE_PENDING request (with link generation and request
  sequence). Before a CM-controlled removal the CM obtains file-system quiescence for the
  affected volumes and honours the CONFIGMG removal veto, then sets REMOVE_PENDING; stor98,
  from its timer/StartIo (idle poll kept armed; elapsed-time deadlines), drains admitted
  work, issues ordered Flushes, sets CC.SHN, waits for SHST, clears CC.EN, verifies RDY=0,
  keeps its MD-4 pool and acknowledges with the request identity and outcome (clean /
  isolated-not-clean / failed); only a clean ack lets the CM report a clean eject and call
  CONFIGMG `Remove_SubTree`; veto/cancel resumes, timeout aborts the orderly eject (*superseded by D2*:
  a veto before the request changes nothing, and after a successful query there is no in-place
  resume: cancel, timeout and not-clean outcomes end in `Remove_SubTree` and a fresh devnode).
  Discovery on 9x: a CM-written registry record (or a CM-hosted dynamic registry key),
  read once at HwFindAdapter through documented VMM/CONFIGMG registry services; a missing
  or unverifiable record means "CM channel unavailable"; never trust a pointer across
  boots. The MD-4 pool's allocation records live in the CM-owned table so a reloaded .mpd
  can recover them; without a trustworthy record, leak with an accounting cap (*superseded by the
  interface 3 revision*: records and the boot-wide budget live in an stor98 boot block; the CM
  table carries only statements and the removal transaction). **On Me,
  write-through from startup** (FUA on every physical write incl. RMW write-back, or a
  verified volatile-cache disable reapplied after resets) covers removals that bypass the
  CM (Device Manager Remove/Disable), which still get no clean SSD shutdown on Me.
  98 SE keeps its native stop. Unprototyped: needs a 9x CM stand-in and qualification
  (file-system ordering, cancellation, reload, delayed timers) before "clean eject on Me"
  is claimed.
- *Superseded (history).* **MD-5 constraints (2026-10-15):** no undocumented APIs (the DDB flag write is out) and
  avoid a VxD (the helper VSD is out). Final MD-5 choice pending a Codex assessment of
  documented, VxD-free options.
- *Superseded (history; MD-5 is decided above).* **MD-5 = decide after G3**: if G3 shows 98 SE/Me needs a VxD service for the CM
  link-down notice, the helper VSD (option i) is chosen and also delivers the Me removal
  stop; otherwise the maintainer chooses again with G3's results. G3 has now run on NT
  only: the 9x receiver is designed (the same link-state table, polled from
  `RequestTimerCall` and `HwStartIo`, found by a VSD's `DDB_Reference_Data` or by a
  registry rendezvous without a VxD) but not prototyped, so the 9x half of G3 waits on
  MD-5, which stays **open**.
- **MD-7 (R6-3, Q-NT item 4): durability at orderly disable on XP x64 and 7. Decided
  2026-10-15 (maintainer): option (a), idle flush from the timer plus accept-and-document.**
  Evidence: [gates/Q-NT.md](gates/Q-NT.md) item 4. A Device Manager disable on XP x64 and
  7 x64 reaches `ScsiStopAdapter` with unflushed writes every time, and a stop-path Flush
  took 2.1-18.9 ms (XP x64) and 5.8-17.1 ms (7 x64) in the emulator, never within the 1 ms
  budget (real SSDs are slower). The driver flushes from the timer once the adapter has been
  write-idle for a short configurable time (elapsed time); the stop path still tries a Flush
  within the budget and otherwise logs the stop as not clean; the residual window (data
  written within the idle time plus Flush latency before a disable or eject on XP x64 and 7)
  is documented for users (0.10 stop). Options not chosen: (b) write-through (FUA, or the
  volatile write cache off) on those OSes, at a performance cost; (c) exceed the stop stall
  budget, which violates the SCSIport contract (and the stop's IRQL is unmeasured);
  (d) accept-and-document only, leaving the window unbounded. Q-NT recommended (a).
  Test: TESTING.md "Unflushed window at disable".
- **A newer generation on 9x without DOWN_QUIESCED, decided 2026-10-18 (owner).** The 9x
  receiver took any LinkGeneration above its bound one as R8b evidence, and the adapter
  went gone. xhci98 3.0.0.0 and 3.1.0.0 increment an entry's generation when they expose
  a tunnel that stayed up again (another port of the same ICM domain coming up, a
  resync; xhci98 roadmap item 42-A.6), so a working drive would fail when a second one is
  plugged. Now an UP (or IN_USE alone) above the bound generation is evidence only once
  the instance has lost presence (the engine is gone, or CSTS reads all-ones). While the
  controller still answers it is identity loss: no evidence, nothing completed, no
  acknowledgement; the drive keeps running, and a later presence loss ends at the MD-1
  deadline. DOWN_QUIESCED at a newer generation stays evidence: it is the CM's explicit
  statement. Options not chosen: keep the rule and rely on xhci98 alone; never take a
  newer UP as evidence on 9x. The same holds for the pool at the next find: E1 releases
  a quarantined record only on DOWN_QUIESCED for its generation or a later one, not on a
  later generation alone; a record of the same bus and slot is released instead by E2,
  once the new load has shown its controller disabled. Records of another bus or slot
  stay quarantined (a block leaked for the boot, within the budget). Code: `src/p9bind.h`
  (P9LinkSee, P9EvidenceE1Snap, P9EvidenceE2, host-tested), `src/pool9x.c`
  PlatCmLinkPoll, P9EvidenceE1, P9Work; contract text: `include/stor98cm.h` R7, R8d.
  Tests: TESTING.md "Two drives in one ICM domain (9x)" and "Quarantined block after a
  surprise removal (9x)".

### Open (undecided)

The entries below record the options and evidence behind the decisions above (MD-7's are
under its decision); MD-5 is decided (see Decided). Otherwise the following are **not decided**. Each lists the options, the evidence and the gate
agents' recommendations; nothing here is to be implemented until the maintainer chooses.

- **MD-3 (G5 on NT, 2000 and XP x64): where DMA-reachable memory lives. DECIDED: (a).**
  Evidence: [gates/G5.md](gates/G5.md) ("Consequence and decision needed"),
  [gates/G2.md](gates/G2.md) ("Observations"). The port frees and reuses the uncached
  extension, the SRB extensions and the device extension after a removal stop whether or
  not the disable was acknowledged, and after a surprise removal with no callback at all; a
  late CQE landed in a reused NTFS page. Power stops preserve everything.
  - **(a)** Stay within the SCSIport contract: rely on the OS clearing Bus Master Enable
    and setting D3hot 2-4 ms after `ScsiStopAdapter` returns for the orderly stop (a small
    window), and on MD-1 and link-down (A1, MD-2) for surprise removal.
    **(a) is a new risk that needs its own acceptance; MD-1 does not cover it (R6-1).**
    MD-1 accepts returning the callers' buffers at the gone deadline while driver-owned
    DMA memory stays quarantined. Under (a), driver-owned DMA memory (queues, PRP list
    pages, bounce buffers) lives in the port's uncached extension, which the port frees
    after every removal stop whether or not the disable was acknowledged, possibly before
    any gone deadline, and after a surprise removal with no callback at all (G5). So (a)
    also hands driver-owned DMA memory back to the OS without isolation evidence: after an
    orderly stop whose disable was not acknowledged, protection rests on PCI.sys's BME
    clear in the 2-4 ms after the callback (the port's free is not ordered against it);
    after a surprise removal, on A1 alone. G5 measured the consequence with the DMA path
    open: a late CQE landed in a reused NTFS page. Choosing (a) means the maintainer
    accepts this broader risk explicitly, as a second exception beside MD-1.
  - **(b)** A driver-owned contiguous DMA pool (`MmAllocateContiguousMemory[SpecifyCache]`
    at the first `HwFindAdapter`, below the negotiated DMA limit) for queues, PRP list
    pages and bounce buffers; released only with isolation evidence, quarantined (leaked
    for the rest of the boot, 16-64 KiB per event) otherwise. Measured to survive stop,
    removal and image unload on 2000 and XP x64, and stop and reuse on 7 x64 (Q-NT). Outside the miniport
    contract (direct ntoskrnl imports, bypasses the HAL DMA adapter; PAE and 32-bit limits
    must be enforced by the allocation). **G5 tested only a 16 KiB sentinel block, not
    operational queues, PRP lists or bounce buffers in it, so (b) needs a prototype before
    adoption.** The contract (b) would need (R6-2), to be confirmed by that prototype:
    - *Contexts and IRQL.* The legacy DDK documentation allows
      `MmAllocateContiguousMemory[SpecifyCache]` at IRQL up to DISPATCH_LEVEL and
      `MmFreeContiguousMemory` only at PASSIVE_LEVEL (to be confirmed against the installed
      2000 DDK and WDK 7.1 documentation; `SpecifyCache` exists from Windows 2000, so every
      NT target in scope has it). Allocation happens only in `HwFindAdapter`, whose IRQL is
      to be instrumented. No other miniport callback is known to run at PASSIVE_LEVEL
      (`ScsiStopAdapter`'s IRQL is unmeasured), so "released with evidence" may in practice
      mean "kept by the adapter and reused at its next start", and a pool still owned when
      the image unloads is leaked. Where the pool may be freed must be named per callback
      before (b) is adopted.
    - *Ownership.* One pool per adapter instance, referenced from its device extension and
      never shared with or handed to another adapter. A quarantined pool is leaked, since
      the next image cannot find it (G5: static data is lost at unload). A cap on memory
      leaked per boot (a tunable) bounds repeated quarantines; past it, a new start fails
      before CC.EN with a logged reason.
    - *Physical addresses.* Taken from `MmGetPhysicalAddress` at allocation and held as
      full 64-bit values; every PRP, queue base and range check uses 64-bit arithmetic and
      never truncates to 32 bits (PAE on x86 and every x64 system can place memory above
      4 GiB).
    - *Physical limits.* `HighestAcceptableAddress` is the DMA limit negotiated in 0.8:
      below 4 GiB unless 64-bit addressing was negotiated for the adapter, and always below
      4 GiB for a device or path limited to 32-bit DMA addresses. Allocation also respects
      page alignment and no-boundary-crossing rules for queues and PRP lists (0.9).
    - *CPU physical versus DMA-adapter addresses.* The pool bypasses the HAL DMA adapter
      (`ScsiPortGetPhysicalAddress`, map registers), so it hands the controller CPU
      physical addresses. That is correct only if bus addresses equal CPU physical
      addresses and nothing remaps device DMA. **Stated assumption:** the target platforms
      (x86 and x64 PCs running Windows 2000 to 7; MD-4 states the same for 9x) have no active IOMMU or DMA remapping
      for this device and no host-bridge address translation, and the allocation's own
      limit removes any need for HAL double buffering. A platform that violates this is
      unsupported.
    - *Allocation failure.* Failure at the first `HwFindAdapter` returns `SP_RETURN_ERROR`
      before CC.EN is written (bring-up rule of 0.10); the uncached extension is not a
      silent fallback.
    - *Dump mode.* The dump stack cannot call the memory manager, so dump mode keeps a
      separate route: the port's uncached extension, allocated once under 4.1's limits.
      The allocation interface keeps the two routes apart, and G6 tests the dump route.
    - *0.4 changes.* The uncached-extension rule of 0.4 (bounce buffers and PRP pages
      carved from the uncached extension and translated with
      `ScsiPortGetPhysicalAddress(..., NULL, ...)`) no longer applies to runtime memory if
      (b) is chosen.
  - Note: on real hardware a surprise removal is a link-down, which resets the endpoint
    (A1), so the emulator's DMA-open surprise-removal case is, for the callers' buffers,
    the MD-1 residual risk already accepted. For driver-owned DMA memory left in the port's
    extensions it is the broader risk of option (a) above.
  - Agent recommendation (G5): option (b) was proposed by the G5 run as the measured way
    out; (a) keeps the driver inside the contract with a small orderly-stop window. No
    option chosen.
- **MD-4 (G5 on 9x): DMA memory on 98 SE and Me. DECIDED: adopt the pool.** Evidence:
  [gates/G9.md](gates/G9.md) ("G5 on 9x", "Helper VxD"). The port frees the device and
  uncached extensions after `HwAdapterState(FALSE)` and unloads the `.mpd` at every
  removal; removal after presence loss without evidence fails G5 for the uncached
  extension. Options: **adopt** a VMM `_PageAllocate` pool outside the port (fixed,
  contiguous, physical address known; the `.mpd` already calls VMM services through
  `INT 20h`, so no helper VxD), freed only with evidence and leaked when quarantined
  (*inferred*, not prototyped); or **accept the risk** and keep DMA memory in the uncached
  extension. Adopting the pool raises the same questions as MD-3 (b), to be answered
  before adoption (R6-2): legal allocation and free contexts, per-adapter ownership, physical limits, the
  no-remapping assumption, failure handling, and a prototype with operational queues in
  the pool. "Accept the risk" would widen MD-1 for driver-owned memory, as MD-3 (a) does
  (R6-1). Agent recommendation (G9): adopt the `_PageAllocate` pool and prototype it
  before 0.10 on 9x; a resident VxD owning a pool across loads is the alternative if
  leaking is unwanted (see MD-5).
- **MD-5 (G9 on Windows Me): the missing removal stop. DECIDED (see Decided): handshake + write-through on Me.** (G3 has run on NT; its 9x
  receiver is designed and its rendezvous follows this decision, [gates/G3.md](gates/G3.md)
  "Windows 9x receiving interface"). Evidence:
  [gates/G9.md](gates/G9.md) ("Windows Me", "Windows Me stop investigation"),
  [gates/G9/me/](gates/G9/me/), [gates/G9/me-stop/](gates/G9/me-stop/). Me's IOS never sets
  the DDB flag the port's removal stop depends on and never sends
  `AEP_SYSTEM_CRIT_SHUTDOWN`, so `HwAdapterState(FALSE)` never comes; the port frees its
  memory with the controller enabled. Shutdown is covered by `CachesData` TRUE
  (`SRB_FUNCTION_SHUTDOWN`, measured). Options:
  - **(i)** A helper IOS VSD: resident `.VXD` in `SYSTEM\IOSUBSYS` that sends an
    `SRB_FUNCTION_SHUTDOWN` down to stor98's DCB at `AEP_PEND_UNCONFIG_DCB`/
    `AEP_UNCONFIG_DCB`. About 300-500 lines of MASM, 1-2 days with tests; the toolchain is
    available in xhci98 `tools\win98ddk-extracted` (to be copied into stor98's git-ignored
    `tools\`). It could also carry G3's 9x receiving interface and a quarantine pool that
    outlives the `.mpd`. Cost: a second binary to ship and load on 98 SE and Me.
    *Inferred*, not prototyped. **The removal-time I/O route is unproven (R6-4):** the
    Windows 98 DDK says `AEP_PEND_UNCONFIG_DCB` is the first call IOS makes when a DCB is
    being destroyed and that layers are expected not to initiate further I/O to the device
    after it, and describes `AEP_UNCONFIG_DCB` as a notice that the DCB is going away
    (xhci98 `tools\win98ddk-extracted\incs\INC_WIN98_AEP.H`:222 and :201). Sending an
    `SRB_FUNCTION_SHUTDOWN` at either point is therefore outside the documented contract.
    Option (i) needs a prototype proving submission, completion and teardown ordering
    (the SRB reaches the miniport, completes, and the port's `AEP_UNINITIALIZE` and memory
    release come only after that completion, with IOS not tearing the DCB down under it),
    or an earlier removal handshake (for example the VSD or xhci98's CM asking stor98 to
    quiesce before `CM_Query_Remove_SubTree`/`CM_Remove_SubTree` begins).
  - **(ii)** The undocumented "please destroy" flag: the miniport sets `DDB_ios_flags` bit
    `0x04` (DDB at `DeviceExtension - 0x8C`), guarded by checks (DDB signature `0x4442`,
    plausible pointers). Measured to deliver the stop at a CONFIGMG remove twice on Me; it
    puts Me in the state 98 SE always runs in. IOS-private field and SCSIPORT-internal
    offset. **Trade-off (R6-4, G9.md "Windows Me stop investigation"):** bit `0x04` does
    more than enable the port's stop callback: Me's IOS also tests it in its port-driver
    inquiry loop, which sends a DDB with the bit set down a destroy path that calls
    `CONFIGMG_Remove_SubTree` (static analysis, not traced). The same code exists in the
    98 SE IOS, where every enumerated DDB has the bit, so setting it is plausibly harmless,
    not proven. A removal before the first read gets no stop; Device Manager Remove/Disable,
    removal after surprise removal and multiple adapters untested.
  - **(iii)** Option (a) on Me: leak DMA memory at every Me removal (MD-4 pool) and run
    write-through (volatile write cache off or FUA) on Me, since the cache is never
    flushed before the CM drops the tunnel.
  - Agent recommendation (G9): decide together with G3, since a VSD (i) could also host
    G3's 9x receiver.

## Known issues in 0.3.0.0 (fixed in 0.3.0.1)

From the Codex review of `stor98-0.3` (round 1, 2026-10-16); P2, deferred by the
maintainer's triage rule for this release (P0/P1 fixed before release, P2/P3 logged).

- **K1 (P2) Terminal states do not take a link-down statement** (`src/lifecycle.c`, timer
  dispatch): in `SHUTDOWN` after the shutdown notification has completed (`SHN_DONE`) and
  in `FAILED`, the timer does not poll the CM entry, so a `DOWN_QUIESCED` published then
  is applied and acknowledged only if an SRB arrives. Effect: on 9x the CM waits its
  link-acknowledgement bound (R12 S4) and then removes the devnode anyway; nothing is
  completed early and no memory is released without evidence. Fix: poll pending link
  statements before the state dispatch, enter `gone` with evidence, then acknowledge.
- **K2 (P2) Pool-lock retries can exceed the per-callback stall budget** (`src/pool9x.c`,
  `P9Note` -> `P9Work(.., 3)` per block becoming `OWNED`, and `P9Work(.., 10)` at the
  relinquish): with the boot block's lock contended, one callback can stall up to about
  1.4 ms (PLAN 0.10 stall budget). Fix: one non-blocking attempt in runtime callbacks,
  deferred work otherwise, and one shared budget per callback.

Fixed in 0.3.0.1 (branch `fix-0.3.0.1`):

- K1: `EngTimer` takes a CM link statement (`LcLinkStatement`, `src/lifecycle.c`) before
  any state's step and before the removal transaction's, in every state; HwStartIo still
  takes one in `running`. Without a transaction (or after its hand-over to `gone`) the
  engine enters `gone` with evidence 2 (completions already posted are consumed first in
  `running` and `gone`; a shutdown notification still waiting completes its SRB as on an
  SHST timeout). With an open transaction `PlatCmRemoveLinkDown` (`src/pool9x.c`) releases
  children and held requests, relinquishes the blocks and writes RemoveAck
  ISOLATED_NOT_CLEAN (R12 S3); after FAILED it releases what FAILED held (the outcome
  stays FAILED); after DONE nothing is held. Then `PlatCmLinkAcknowledge` relinquishes the
  pool and writes AckGeneration last. The 9x timer keeps the link poll period in every
  state while a statement can come, also after a removal's acknowledgement (its devnode
  may survive a failed Remove_SubTree, R11 O6), when only DOWN_QUIESCED for the
  instance's own generation is accepted.
  Review rounds on the branch added: AckGeneration only for the bound generation (a later
  one is evidence, R8b, but never acknowledged by this instance), another generation during
  an open removal transaction is identity loss (no evidence, no acknowledgement), every
  state change arms the CM poll timer (a resume ending in `failed` armed none), HwStartIo
  takes a statement in every started state, and a pool record with no carved block (a
  resume that failed before carving) is finished at the relinquish and before the link
  acknowledgement.
- K2: every boot-block lock acquisition is one non-blocking attempt in runtime callbacks
  (`src/p9lock.h`); unfinished work stays pending and `PlatCmLinkPoll` retries it on later
  ticks, also after the instance has ended; HwFindAdapter's attempts share one budget,
  the stalls the clock calibration leaves of that callback (`P9_FIND_STALLS`, currently
  none). On uniprocessor 9x a busy-wait cannot let the holder run (it is the context the
  callback interrupted), so stalling never helped. Host test: `tests/unit/p9lock_ut.c`.

Fixed for 0.3.0.0 from the same review: the pinned test-certificate thumbprint (P1), and
the 9x link-down acknowledgement now relinquishes the pool and retires the owner first
(P2-2; repeated surprise removals at changing PCI locations on Me no longer use up the
pool budget).

## Open questions

- Whether Windows 7 x64 users are better served by Microsoft's `stornvme` (KB2990941 +
  KB3087873); usb4-emu's evaluation compares it with nvme2k on the same tests.
- Interrupts: the base is effectively INTx/polling; MSI is not available to SCSIport
  miniports on these systems.
- Which completion status makes each OS's class driver give up after removal (item 2.2):
  answered for 2000, XP x64, 98 SE and Me, and for NO_DEVICE on 7 x64; open for Vista and
  XP x86.
- Settled by the feasibility gates (0.0), not by argument: what each port does with SRBs
  held in `gone` (G2); whether xhci98's CM can deliver a link-down statement on each OS
  (G3); what each port does with SRBs left outstanding after `HwResetBus` (G4); whether the port frees memory after a stop the controller has not
  acknowledged (G5); which callback is the final shutdown trigger per OS (G8); whether 9x
  needs a helper VxD and how it may access config space at runtime (0.12, G9). Answers so
  far are in "Phase 0.0 results" (0.0); G3 and G6 are answered on 2000 and XP x64, G3's
  9x half and Me's G9 wait on MD-5, and the remaining qualification items are listed
  under "Qualification before dependent product code".

## Known issues in 0.3.0.1

- **K3 4 KiB-sector (4Kn) namespaces are not translated.** The 2026-10-14 decision is
  to support 4Kn drives by presenting 512-byte sectors and translating (read-modify-write
  for unaligned writes). The code reports the native sector size
  (`src/nvme2k_scsi.c` READ CAPACITY) and issues requests in it (`src/engine.c`), found by
  the Codex review of the 511-breaks change. Planned for the release after 0.4.0.0, with a
  4Kn QEMU namespace in the tests.

## 0.4.0.0 plan (maintainer, 2026-10-16; first planned as 0.3.0.2)

Revised the same day (maintainer): **the release after 0.3.0.1 is 0.4.0.0 (released
2026-10-16). It carries the NT
receiver of the xhci98 link-state channel (item 0) together with item 1 (the 9x 512 KiB
transfer); item 2 (4Kn) moves to the release after it.** Each item gets its own Codex
review loop and a 9x/NT requalification before the release.

0. **NT receiver of the link-state table** (interface 2 of `include/stor98cm.h`, version 4;
   MD-6, evidence 2 of 0.10). Until 0.3.0.1 the NT build had a stub that never opened the
   table, while xhci98 3.0.0.0 publishes it on 98 SE / ME and on NT, so with those
   versions the NT side of the channel was unused until 0.4.0.0 (and on 98 SE / ME only
   a drive whose entry was posted before the find was bound: item 5). `src/cmlinknt.c` opens `\Callback\Stor98LinkState` once in the first
   `HwFindAdapter` (never in a dump or hibernation instance; a resume re-binds on the
   table it already has, with no object access), accepts only a version 4 table and only
   the first table of the image's life, binds the entry of its (bus, slot), and polls it
   from the timer (every 100 ms in every started state while a statement can still come)
   and from `HwStartIo`. The decisions are in `src/cmlink.h` (host test
   `tests/unit/cmlink_ut.c`): `DOWN_QUIESCED` for the bound generation or a newer one is
   evidence 2, applied through the engine's existing path (K1); `AckGeneration` is written
   last and only for the bound generation; a newer generation without `DOWN_QUIESCED` is
   not evidence on NT (interface 3's R8b is a 9x rule: xhci98 withholds the statement
   where it cannot prove the endpoint reset). The REMOVE_PENDING transaction is defined for
   9x only (interface 3), so NT has no clean-eject handshake. Without a CM, after a CM
   unload or reload, or with a table of another interface version, there is no channel and
   the MD-1 gone deadline applies as before. Refusal cases are tested with the stand-in
   `tests/cmnt`; the joint legs with xhci98 3.0.0.0 are in TESTING.md ("CM notification").

1. **9x maximum transfer of 512 KiB. Done (branch `xfer512`, 2026-10-16).** On Windows 9x
   only, MaximumTransferLength is 512 KiB (`STOR98_MAX_TRANSFER` 128 pages,
   NumberOfPhysicalBreaks 129); NT stays at 256 KiB and its images are byte-identical to
   the previous build apart from the PE timestamp, checksum and PDB identity. The PRP list
   pages, bounce pages and the DMA block (9x pool block, boot-wide budget, six-adapter cap)
   are counted per child, not per transfer, and are unchanged; one PRP list page (512
   entries) covers the 129-page child, checked at compile time with the 16-bit NLB.
   **Measured rule (98 SE emulator, VWIN32 7305h absolute I/O, 2026-10-16):** IOS sends a
   request to the miniport whole only if it is no larger than MaximumTransferLength and no
   larger than 512 KiB, its buffer is page-aligned, and the buffer is physically
   contiguous enough (about 16-17 physical runs at most). Anything else arrives as
   126-sector (63 KiB) SRBs at queue depth 1; misaligned requests above 64 KiB are copied
   through IOS's own buffers in those pieces. Neither NumberOfPhysicalBreaks (65, 129, 257,
   513) nor MaximumTransferLength (256 KiB to 2 MiB) changes that for misaligned or
   fragmented buffers. The port supplies no limits (both arrive `SP_UNINITIALIZED_VALUE`).
   The earlier statement that misaligned requests up to 512 KiB arrived whole with a
   2 MiB maximum came from one morning run (0.3.0.0 code with the perf hooks and an
   earlier perf tool); it did not reproduce with 0.4.0.0 code and the same 2 MiB /
   513-break limits, and it is unexplained.
   **Before/after (98 SE, TCG, 32 MiB per point, scratch perf hooks
   (`vms\perf\perf-hooks-3e9f51b.patch`, local, not committed); MB/s are emulator-bound, compare within
   a row only):** aligned, contiguous 512 KiB reads went from 576 SRBs of 63 KiB
   (28.1 MB/s) to 65 whole SRBs (87.4 MB/s, +210%), the fastest point of the sweep
   (256 KiB: 54-57 MB/s in both builds). Every other point kept its request pattern:
   requests of 1-8 MiB (63 KiB pieces, 30-43 MB/s), misaligned buffers from 128 KiB up,
   and freshly allocated (fragmented) buffers from 128 KiB up, aligned or not. The gain is
   for aligned, physically contiguous requests above 256 KiB and up to 512 KiB only, not a general speed-up.
   absw data compare (a pattern per sector and pass, restore verified) passed at every
   size from 64 KiB to 8 MiB, aligned and +512, with both builds. File I/O through VFAT
   with `FILE_FLAG_NO_BUFFERING` is served by VCACHE on 98 SE (almost no SRBs reach the
   driver during the timed loops). ME (16 MiB per point, sizes 64 KiB to 2 MiB): the test buffers were physically fragmented (16-17 runs per 63 KiB piece), so no request above 64 KiB arrived whole with either build; the request pattern was identical, throughput the same within noise, and every data compare passed.
   **Removal with 512 KiB SRBs in flight (98 SE and ME, debug-port 512 KiB image):**
   surprise (endpoint reset) at the 30th 512 KiB read fetch. With this tree's cm9x
   (98 SE), DOWN_QUIESCED was taken as evidence 2; the held SRB completed NO_DEVICE after
   the evidence and before AckGeneration (the CM had its acknowledgement in 28 ms) and
   nothing was quarantined. With the older cm9x of the base images (rendezvous refused,
   98 SE and ME), the SRB stayed held until the 5 s MD-1 deadline, then completed
   NO_DEVICE; its child and PRP list page were quarantined and the next arrival took a
   fresh pool block. Gate closed (no reset, no statement) at the 20th 512 KiB write
   fetch: held to the MD-1 deadline, the same outcome. absw 512 KiB compare passed after
   every re-arrival. NT regression: 2000 and 7 x64 (debug-port images, 256 KiB / 65
   breaks negotiated): copy/compare and `eio` at QD8 passed, and surprise removal during
   `eio` gave the gone state with every request completed and no hang.
   **Other 9x limits (not changed):** queue depth 1 at the SRB level; IOS's 63 KiB pieces
   for fragmented, misaligned and over-512 KiB requests; in QEMU (MDTS 512 KiB) one child
   per SRB, so no child parallelism. To do: 4Kn interaction once item 2 lands; an
   SRB-size trace and ATTO numbers on the P14s (real hardware) with the debug-port build.
2. **4Kn support (K3, phase 1.7, "4 KiB-sector namespaces" above); after 0.4.0.0.** Present 512-byte
   sectors to the OS on a 4096-byte-LBA namespace and translate, with read-modify-write
   for writes that are not 4 KiB aligned (maintainer decision 2026-10-14: never refuse
   4Kn). Tests: a QEMU nvme namespace with `logical_block_size=4096` on 98 SE, ME, 2000
   and 7 x64 (install, aligned and unaligned read/write with data compare, partition and
   format, surprise removal during a read-modify-write), plus host unit tests for the
   LBA and RMW arithmetic.

3. **Polling mode, in 0.4.0.0 (maintainer, 2026-10-16).** When the OS gives the
   controller no usable interrupt, Stor98 runs without one, polling its completion queues
   at a bounded rate that is raised while I/O is outstanding and lowered when idle. See
   "Polling mode (0.4.0.0)" below.
4. **Settings (owner decisions 2026-10-16; branch `maxxfer`).** (a) **Maximum transfer
   size setting:** 9x `StorMaxTransferKB` (REG_DWORD or decimal REG_SZ, Services\stor98),
   NT `maxtransferkb=` in DriverParameter; 64 KiB to the build's maximum
   (`STOR98_MAX_TRANSFER`: 512 on 9x, 256 on NT) in multiples of 4, default the maximum.
   Anything else (out of range, off the step, not a whole decimal number, unreadable) is
   refused, the maximum stands, and the refusal is logged (debug-port stages
   0x160-0x161). Read once at HwFindAdapter (`src/xfertun.h`, host test
   `tests/unit/xfer_ut.c`; an NT resume keeps it) and reported as MaximumTransferLength
   and NumberOfPhysicalBreaks = pages + 1. It only lowers the limit, so the compile-time
   PRP list and NLB checks still cover it. Bounds: the port limits (NegotiateDma) and the
   child size (`MaxChildBytes`, LcBringupDone) use the setting; the PRP list check in
   EngMapCaller, the compile-time checks and the DMA sizing (PRP and bounce pages, 9x
   pool block) stay at the compile-time maximum, which is safe because the setting is
   never above it. (b) **"Stor" prefix for the user-settable 9x values**, as xhci98's
   `Xhci*`: `StorWriteThrough` (only that name, owner decision 2026-10-16; it was
   recorded with the reason that no released build read the earlier `WriteThrough`, but
   0.3.0.0 and 0.3.0.1 did, so a `WriteThrough` value set for them is ignored from
   0.4.0.0 and the user must rename it: README "Settings"), `StorPollMode`, `StorPollBusyUs`,
   `StorPollIdleMs`, `StorPollSpinUs`, and the report value `StorInterrupt
   <bus>.<device>.<function>`. NT DriverParameter keys and the xhci98 interface names
   (`LinkState`, `\Callback\Stor98LinkState`, `include/stor98cm.h`) are unchanged. The
   INFs write none of the settings (D3's "the INF writes the default" was never done):
   an absent value is the built-in default, and an update keeps what the user set
   (apart from the `WriteThrough` rename above).
   README "Settings" is the single reference. Tests: TESTING.md "Settings".

5. **9x late lookup of the link-state entry (owner decision 2026-10-16).** On Windows 98
   SE and ME, Stor98 looks its entry up again after HwFindAdapter. Cause (xhci98 NV9X.1
   on 98 SE, `xhcicm.sys` publishing interface 3's proof key and locator): a drive
   hot-plugged behind a Thunderbolt tunnel after Windows started does not answer
   configuration reads until Windows enumerates it, so the CM publishes the proof and the
   table, calls `Reenumerate_DevNode`, Windows starts Stor98, and only then (about 200 ms
   after the find) can the CM post the entry's UP. The find saw a valid table but no
   entry (rendezvous rc 8) and the load ran with no channel (MD-1 only); xhci98's record
   21 section 7.2 names the dependency. **Design** (`src/p9bind.h`, `src/pool9x.c`
   P9Bind / P9LateBind; host test `tests/unit/p9bind_ut.c`): when the find accepts the
   table but binds no entry, it keeps the table, and `PlatCmLinkPoll` (the engine timer,
   kept at the 250 ms link poll period while the lookup runs, and HwStartIo) scans the
   table again, one R6c snapshot per entry and call, no spinning, for at most 10 s of VMM
   elapsed time from the find (`P9_LATE_BIND_MS`: the measured gap with a wide margin for
   a slow CM writer and 1.5 s timer lateness on Me; afterwards a function that is not
   tunnelled stops costing a 250 ms idle timer and MD-1 applies as before). The rules are
   the find's: identity (bus, slot); two IN_USE entries naming the function, legal or
   not, are a duplicate and a malformed one is refused, both ending the channel for the
   load (and its table for E1); an entry being updated leaves the scan undecided; the
   table header is checked against the find's cookie each time (a change ends the
   lookup). Only an UP entry with a generation above a floor is bound; DOWN_QUIESCED and
   IN_USE alone are refused and raise the floor, so only a newer generation can be bound
   later (safe by R8c: the CM posts UP for a new tunnel only after the previous devnode
   was removed). A late bind sets the generation, the RemoveSequence at binding (R8d) and
   the pool record's generation exactly as a bind at the find, so the evidence rules,
   AckGeneration and the MD-5 transaction work unchanged. Interface 3 is unchanged; its
   R8d ("binds once, in HwFindAdapter") is read as "binds once per load". NT keeps its
   single bind: interface 2 obliges the CM to post UP before enumeration, and the NT
   joint legs bound at the find. **Defect found and fixed during 0.4.0.0 development
   (same work):** the 9x find bound any IN_USE entry with a nonzero generation, including
   the previous tunnel's: after a Safely Remove or surprise unplug xhci98 leaves the entry
   IN_USE alone (statement withheld) or DOWN_QUIESCED, and the re-plug's find bound that
   old generation; the new tunnel's UP then read as R8b evidence against the new load,
   which failed the adapter, so a drive returning in the same boot got no disk until a
   restart (98 SE, debug-port stages 0x122 bound gen 1, 0x124 gen 2 over 1, then gone and
   failed). The UP-only rule above fixes it. Debug-port stage 0x132 (late lookup result),
   rendezvous rc 15 (DOWN at the find), 16 (an entry being updated), 17 (malformed), 18
   (IN_USE alone). Legs: TESTING.md "CM notification, 9x late lookup".

### Polling mode (0.4.0.0)

Decided by the maintainer on 2026-10-16, with the settings controllable (registry values
with defaults, ranges and clamping). Branch `poll`, merged with `int-0400` (the NT
receiver and the 9x 512 KiB transfer) before its emulator legs. The design was reviewed
and revised.

**When it polls (`HwFindAdapter`).** The adapter polls exactly when the interrupt it would
report to the port is none, or when `StorPollMode` / `pollmode=` = 1 forces it (`PollDecide`,
`src/polltun.h`). The reasons, in order (a natural reason is reported ahead of "forced"):

1. Interrupt Pin 0: the function has no INTx, for example an MSI-only controller. No
   system in scope gives a SCSIport miniport MSI.
2. NT 2000 and later: the port supplied no interrupt (`BusInterruptLevel` and
   `BusInterruptVector` both 0 on entry), meaning the OS assigned no interrupt resource.
3. Interrupt Line 0 or FFh: no IRQ routed. The base reported 0/0 to the port in this case
   and then had no interrupt at all; it now polls.
4. `StorPollMode` / `pollmode=` = 1, for testing.

Pin and Line are read from the length-checked configuration header that `HwFindAdapter`
reads (PLAN 3.4). A header too short to hold them counts as "configuration unreadable" and
also polls.

*Finding (measured, debug-port stage 0x153):* both port families fill `ConfigInfo`'s
interrupt before `HwFindAdapter`.
- NT: 2000, XP x64 and 7 x86 all set level = vector = the IRQ (9/9 or 10/10).
- 98 SE: sets the level to the IRQ (10) and leaves the vector 0.
- The 9x decision still takes Pin and Line, not the port's values. The 9x build uses the
  NT4 ABI, and a function 98 cannot give an IRQ is not started at all (below).

**SCSIport without an interrupt.** In the polling mode `HwFindAdapter`:
- reports `BusInterruptLevel` = `BusInterruptVector` = 0;
- writes INTMS all-ones;
- leaves INTx disabled in the PCI command register.

The engine never unmasks a vector (`LcBringupDone`, `LcStormUnmaskTrial`). `HwInterrupt`
stays registered. If something calls it (a port that polls it, as the NT dump port does,
PLAN 4.2, or another device's interrupt on a shared line), it consumes completions with
the phase bit, as the ISR does. It never claims an empty call and does no storm
accounting.

*Finding:* with 0/0 reported, `HwInterrupt` was called 0 times on 98 SE, 2000, XP x64 and
7 x86 (stage 0x156 never logged). Both ports start the adapter and send it I/O normally.

**Completion paths.** There are two. Both consume the completion queues only through the
phase bit, exactly as the ISR does; nothing in 0.10 changes.

*The timer.* The single miniport timer (`EngTimer`) is also the one dispatcher of every
periodic duty, in this order:
1. the CM link statement (the 9x interface 3 receiver, and the NT receiver of item 0
   through `PlatCmLinkPollUs`);
2. the state's step, which polls the queues where they are live (running, gone,
   resetting, quarantined);
3. the hold check and dispatch.

The period is the shortest any duty wants (`LcWantedPeriod`, `PollCombine`):
- **Work outstanding while running:** `StorPollBusyUs` / `pollbusyus=`. NT never gets less than the 10 ms
  fast tick (`LcPollBusyUs`), because its port rounds a timer period up to the OS clock
  tick (10-15.6 ms). A shorter request there would change nothing, and every callback
  would look late against its nominal period (see "Clock").
- **In gone or quarantined with work outstanding:** the 100 ms watchdog tick
  (`LcPollFast`); with none, the 1 s slow tick as before. Completions there
  are rare, and coarse ticks keep the OS-clock cross-check running while the MD-1
  deadline is measured.
- **Unflushed writes and nothing outstanding:** the watchdog tick, for MD-7 (a)'s idle
  Flush.
- **Idle and clean:** `StorPollIdleMs` / `pollidlems=`, or no timer when it is 0.
- **Bring-up, the SHST wait and resets:** they keep the 10 ms fast tick.

`EngTimerEnsure` shortens a request to the busy period only while work is outstanding
(`PollEnsurePeriod`), so an idle or CM-only period is kept. After an interrupt storm,
without the polling mode, the cadence is main's.

*`HwStartIo`.* After handling the request, `HwStartIo` polls the queues once. While
children are issued, it then keeps polling for at most `StorPollSpinUs` / `pollspinus=`, in 10 us stalls (at
most 500 us, half the per-callback stall budget of 0.10). It does this only in running and
gone, where the ISR would consume, and never inside a 9x removal transaction past its
drain. A request completed there goes through the normal `EngCompleteSrb` and readiness
notifications; its tag is read before it is handled.

*Finding: the 9x port does not replace a pending `RequestTimerCall`; it cancels and
re-arms only on request.* Measured on 98 SE in the polling mode: a 10 ms request made
while a 1 s idle tick was armed waited for that tick. Reads ran at 0.07 MB/s and writes,
with the 100 ms dirty-idle tick armed, at 0.63 MB/s. On 9x, `EngTimerEnsure` now cancels
the pending request (period 0) before re-arming a shorter one, and the same run gave
8.4 MB/s reads. This also applies to the interrupt mode. Its existing shortening (for
example the fast tick after a slow one) was silently ineffective on 9x before 0.4.0.0.
NT's port replaces a pending request, as documented.

**Settings.** They are read once in `HwFindAdapter` and kept in the device extension, so
an NT resume, which brings no `ArgumentString`, keeps them (0.5).
- **NT:** `DriverParameter` keys `pollmode=`, `pollbusyus=`, `pollidlems=` and
  `pollspinus=`. The names avoid every older key as a suffix, because the parser finds a
  key anywhere in the string.
- **9x:** REG_SZ (decimal) or REG_DWORD values `StorPollMode`, `StorPollBusyUs`,
  `StorPollIdleMs` and `StorPollSpinUs` in `HKLM\System\CurrentControlSet\Services\stor98`
  (the key of D3's write-through value), read through VMM at every load. The legs below
  ran before the 9x values took the `Stor` prefix (item 4); the names here are the
  released ones.
- **Clamping:** an absent value takes its default. A present value outside its range is
  clamped to the nearer bound, and the clamp is logged (debug-port stage 0x151 with the
  raw values, 0x150 with the result).
- **Host tests:** `tests/unit/poll_ut.c`, built for both families.

| Value | Unit | Default | Range | 0 means | Why the bounds |
|---|---|---|---|---|---|
| `StorPollMode` / `pollmode=` | - | 0 | 0-1 | automatic | above 1 clamps to 1 |
| `StorPollBusyUs` / `pollbusyus=` | us | 1000 (NT: at least the 10 ms fast tick) | 1000-100000 | the default | 1 ms is the finest period the 9x port expresses, and 98 SE delivered it on time; 100 ms is the watchdog tick, so polling is never slower than the interrupt mode's lost-interrupt check |
| `StorPollIdleMs` / `pollidlems=` | ms | 1000 | 100-60000 | no idle polling (the timer stops as with an interrupt; the idle Flush and the CM link poll can still run it) | 60 s caps how long a removal goes unseen while idle |
| `StorPollSpinUs` / `pollspinus=` | us | 500 on NT, 0 on 9x | 0-500 | no spin | half the 1 ms per-callback stall budget |

The defaults are the maintainer's, set from the measurements below (2026-10-16):
- **Busy 1 ms:** on 98 SE, 10 ms gave about half the interrupt throughput at QD1 in the
  first runs, 1 ms about the same.
- **Spin 500 us on NT:** there the timer cannot go below the OS tick. Without the spin,
  forced polling on 2000 ran at about a tenth of the interrupt throughput at QD1; with it,
  at about the same.
- **Spin 0 on 9x:** the 1 ms timer works there, the CPU is scarcer, and the TCG
  differences were too noisy to justify a busy-wait by default.

**Clock.** The OS-clock cross-check (`ClockTick`) is main's rule: any 2 s window in which
the OS clock is frozen, or more than 4x plus 1 s ahead of the nominal periods, rejects it.
Earlier revisions in review tried three things, and each was found to weaken release
timing or to stall deadlines:
- exempting short-period windows;
- widening the window by one clock interrupt per callback;
- a SUSPECT state.

The polling mode instead never asks for periods the OS cannot deliver:
- NT gets the 10 ms floor above;
- on 98 SE, 1 ms periods were delivered on time, and the OS clock qualified in every run
  (stage 0xD4 state 2).

What one callback adds to the safe and fallback clocks is capped at the slow tick (1 s),
because the idle period can be 60 s and `ClockResolutionMs` allows one slow tick. If a 9x
machine's VMM timer delivers 1 ms periods more than 4x late, main's rule rejects the OS
clock, and deadlines then run on the nominal periods. In gone those are the 100 ms
watchdog ticks, so the MD-1 deadline comes late by those ticks' lateness and never early.
`tests/unit/clock_ut.c` (9x headers, simulated TSC, OS clock and stall) covers:
- on-time and late short ticks;
- the NT floor;
- frozen and fast OS clocks;
- slow TSC calibration;
- gone deadlines stamped at entry into gone, which are never early;
- the 60 s credit cap.

**Shutdown, removal, crash dump, hibernation.**
- **Stop** (`ScsiStopAdapter`, `HwAdapterState(FALSE)`): unchanged. It already polls its
  Flush, SHST and RDY within the stall budget and cancels the timer, which cannot re-arm
  after it. The restartable SHUTDOWN, the idle Flush and resets run from the same timer as
  before.
- **Surprise removal while polling:** the timer's running step and `HwStartIo` read CSTS.
  All-ones enters `gone` as with an interrupt. Held SRBs wait for evidence 2 or the MD-1
  deadline.
- **Crash dump and hibernation (phase 4, NT only):** the dump port polls `HwInterrupt`,
  which consumes completions in the polling mode as well. The planned dump engine is
  synchronous and needs no interrupt; the polling mode adds nothing there.

**Interrupt report (maintainer request, 2026-10-16).** At every start `HwFindAdapter` writes
a REG_SZ value `StorInterrupt <bus>.<device>.<function>` in
`HKLM\System\CurrentControlSet\Services\stor98`. Examples from the legs:
- 2000: `pin 1 line 9 msi no msix no port 9/9 polling (forced by pollmode)`;
- 98 SE: `pin 1 line 10 msi no msix no port 10/0 polling (forced by StorPollMode)`;
- XP x64 and 7 x86: `pin 1 line 10 msi no msix yes port 10/10 polling (forced by pollmode)`.

The value holds:
- the Interrupt Pin and Line;
- MSI and MSI-X presence, for the report only. They come from the driver's own bounded
  walk of the capability list in the same header read (`src/pcicap.h`), with constants
  from the PCI Local Bus Specification: Status bit 4 is Capabilities List, ID 05h is MSI
  and ID 11h is MSI-X. The walk visits at most 48 entries, masks the pointers, and ends at
  a pointer below 40h.
- the port's interrupt level and vector;
- `interrupt` or `polling (<reason>)`.

How it is written:
- 9x: VMM `_RegCreateKey` (014Ah) and `_RegSetValueEx` (0152h), documented services in the
  table D1 already uses.
- NT: `ZwOpenKey`, `ZwSetValueKey` and `ZwClose` at PASSIVE_LEVEL (the decision next to
  MD-6). Never at a resume, whose `HwFindAdapter` IRQL is unmeasured, and never in a dump
  instance.
- A failure only leaves the value unwritten.

The README and the release readme say how a user reads it.

**Specifications (maintainer request).** Neither question can be answered from text in
this repository, `docs/`, xhci98's `docs/references` (chipset datasheets, xHCI, USB4 and
USB 3.2 only) or `tools/`. No NVM Express or PCI Express base specification is available
there, so both answers are **not verified**:
- (a) whether NVMe over PCIe requires the controller to support pin-based (INTx)
  interrupts;
- (b) whether PCI Express allows a native Endpoint to omit INTx (Interrupt Pin = 0).

The driver does not depend on either answer: it polls whenever the pin is 0.

**Windows 9x without an IRQ.** Windows 98 does not start a function it cannot give an IRQ.
Starting one without an IRQ is xhci98's roadmap item NOIRQ.1, not part of 0.4.0.0. On 9x
the polling mode therefore covers `StorPollMode` / `pollmode=` = 1, and a started function with Pin 0 or no
Line.

**Emulator limits.** QEMU's `nvme` device always presents Interrupt Pin 1. usb4-emu has no
property to remove the pin or the interrupt resource, and no documented means was found to
make 2000 or XP assign none. So the natural cases (Pin 0, no resource, no line) are covered
by the host tests of `PollDecide` and exercised in the emulator only through `StorPollMode` / `pollmode=` =
1. Forcing takes the same path: the port is given no interrupt, and INTx stays disabled.

**Legs (emulator, merged tree `poll` = `int-0400` + polling, debug-port images, forced
`StorPollMode` = 1 on 98 SE, `pollmode=1` on NT, unless stated).**
- **98 SE**
  - Install and report value checked.
  - absw with compare at 64-512 KiB passed at every setting.
  - Surprise removal during I/O: gone entered from `HwStartIo`, new requests fast-failed,
    cm9x retired the device, and re-arrival I/O verified.
  - Idle CPU at the 1 s idle period was not measurable.
  - Shutdown: the stop path ran clean (Flush skipped as clean, SHST complete, RDY=0).
- **2000**
  - Install (`devq force`) passed, with the report value above.
  - Raw read/write with compare passed at QD1 and QD8.
  - Surprise removal during I/O: gone, then the MD-1 deadline about 5 s later (no CM on
    that image), the held SRB completed NO_DEVICE, then `BusChangeDetected`.
  - Shutdown: two `SRB_FUNCTION_SHUTDOWN`s, each with Flush, CC.SHN and SHST complete,
    with the re-enable between them.
- **XP x64 (xhci98 3.0.0.0 CM installed)**
  - Install passed (the CM takes a driver change as an eject; the enclosure was replugged
    by QMP).
  - Raw read/write with compare passed at QD1 and QD8.
  - Surprise removal during I/O: gone, then the MD-1 deadline at about 5 s with one held
    SRB, then `BusChangeDetected`.
  - Shutdown: SHUTDOWN with Flush, CC.SHN and SHST complete.
- **7 x86 (CM installed)**
  - Install passed.
  - Raw reads with compare passed. Raw writes to the volume's sectors are refused by
    Windows 7 (expected), so writes were verified with a 12 MB file copy and `fc /b`.
  - Surprise removal during I/O: gone entered with nothing held, the deadline marker, then
    `BusChangeDetected`.
  - Shutdown: SHUTDOWN with Flush, CC.SHN and SHST complete.
- **Interrupt-mode regression (finding above), merged release image against 0.3.0.1**
  - 98 SE: absw with compare at 64-512 KiB passed. Surprise removal during I/O passed.
  - ME (the guest's port-driver file still under its pre-rename name, fragmented test buffers): absw with compare at 64-512 KiB
    passed with both builds, at 15-89 MB/s for 0.4.0.0 and 16-69 MB/s for 0.3.0.1, which
    is the same within the emulator's noise (two orders). Surprise removal during I/O
    passed.

**Measurements (QEMU TCG: emulator-bound, compare within a table only).**

98 SE, merged tree (512 KiB transfers), absw 16 MiB at QD1, 64-512 KiB requests:

| Mode | MB/s |
|---|---|
| Interrupt, 0.3.0.1 | 15-50 |
| Interrupt, 0.4.0.0 (512 KiB + timer re-arm) | 26-76 |
| Polling, `StorPollBusyUs` 1000 | 11-15 |
| Polling, `StorPollBusyUs` 10000 | 8-34 |
| Polling, `StorPollBusyUs` 50000 (rate check) | 1.0-1.6 |

The earlier 98 SE run (before the 512 KiB merge, 8 MiB, 64 and 128 KiB) gave:
- interrupt 13-24 MB/s;
- polling at 1 ms 12-21 MB/s;
- polling at 10 ms 5.4-8.3 MB/s;
- polling at 10 ms with a 500 us spin 14.5-16.6 MB/s;
- polling at 50 ms 1.3-2.7 MB/s.

The time per 64 KiB request follows the interval: about 4.9, 7.7 and 47 ms at 1, 10 and
50 ms. That is the rate leg: changing the interval changes the measured rate. CPU (the
share an idle-priority counter lost) tracks throughput in a TCG guest. Per MB, polling at
1 ms cost about what interrupts did.

Windows 2000, raw disk, 16 MiB with compare, 64 and 256 KiB requests:

| Mode | MB/s, QD1 | MB/s, QD8 |
|---|---|---|
| Interrupt | 69-229 | 73-267 |
| Polling, no spin (each request waits for a 10 ms tick) | 5.9-21 | 39-132 |
| Polling, `pollspinus=500` (NT default) | 76-178 | 84-229 |

XP x64 with the NT defaults (polling, 500 us spin): 49-205 MB/s at QD1, 64-205 MB/s at
QD8.
