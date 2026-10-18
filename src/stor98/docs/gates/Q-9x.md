<!-- SPDX-License-Identifier: BSD-3-Clause -->
<!-- Copyright (c) 2026, Yeo Kheng Meng -->

# Qualification before dependent product code: Windows 9x side (Q-9x)

> Prototype source is not on `main`. Branch `qual-9x` (worktree `nvme98-wt-9x`): `phase0-g9`
> (G9 instrumentation, b555ff6) with `main` merged (d1e87d7), then the 9x qualification layer
> `src/q9x.c`/`q9x.h` (9832437 and later), the 9x CM stand-in `tests/cm9x` (7d300fd and later),
> and the 9x half of the interface header [`include/nvme98cm.h`](../../include/nvme98cm.h)
> ("interface 3"). Test builds: `set NVME98_Q9X=1 && scripts\build.cmd 9x`;
> `tests\cm9x\build.cmd`. Evidence: [Q-9x/](Q-9x/).
>
> **Interface 3 was revised after a Codex review and requalified on 2026-10-15**: see
> [Interface 3 revision](#interface-3-revision) at the end. The sections before it record the
> first prototype; where they describe the withdrawn v1 interface (dynamic key `Nvme98Cm`, the
> 1056-byte table, `RemoveCancel`, `CANCELLED`, `INVALIDATED`, pool records in the CM table,
> the wall-clock leak counter), the revision supersedes them.

PLAN "Qualification before dependent product code" (R6), 9x items, with the designs of
maintainer decisions MD-4 (the `_PageAllocate` pool) and MD-5 (CM-driven removal handshake and
write-through on Me, no VxD, no undocumented APIs):

1. A 9x CM stand-in that writes the per-boot record, owns the link-state table, publishes `UP`
   and `DOWN_QUIESCED` (after the emulator's stage-2 cutoff), runs the MD-5 `REMOVE_PENDING`
   handshake, calls CONFIGMG `Remove_SubTree` only after a clean acknowledgement and honours the
   `Query_Remove` veto.
2. The nvme98 9x receiver: the record read once in `HwFindAdapter` with documented VMM
   registry services; the table polled from `RequestTimerCall` and `HwStartIo` with an idle
   poll and elapsed-time deadlines; the handshake (drain, ordered Flush, CC.SHN, SHST, CC.EN=0,
   RDY=0, pool kept, acknowledgement with identity and outcome); `DOWN_QUIESCED` as evidence 2.
3. The MD-4 pool with operational queues, PRP list pages and bounce buffers, its records in the
   CM-owned table, surviving `.mpd` unload and reload, reclaimed only with isolation evidence,
   leaked under an accounting cap otherwise.
4. Write-through on Me (FUA on every write, or the volatile write cache disabled and verified)
   and its throughput cost.
5. G1 (deferred bring-up, RDY late or never) and G4 (`HwResetBus` returning FALSE) on 98 SE
   and Me.

Date: 2026-10-15. usb4-emu `src/qemu/build` binary of 2026-10-15 06:20 (QEMU 11.1.2 with the
usb4-emu patches), copied to `E:\nvme98-vm\9x\bin`. TCG, i440FX, `pentium3,-apic`, 512 MiB,
generic profile, `usb4-tunnel-bridge` (SHPC, `shpc-autoenable=on`) at 00:05, `usb4-host` at
00:04, `usb4-debug-port`, `-debugcon`. Guests: Windows 98 SE (overlay of usb4-emu
`vms/win98-g9/win98-prep.qcow2`, QuickInstall IOS set, as G9) and Windows Me (overlay of
`vms/winme-g9/winme-prep.qcow2`, stock Me, as G9); NVMe disk: overlays of the G9 1 GiB FAT32
image. Base images untouched. QMP 48210 (98 SE) and 48220 (Me).

## Summary of verdicts

| Item | Windows 98 SE | Windows Me |
|---|---|---|
| 1. CM stand-in (WDM, no VxD) | **pass**: record in a CM-hosted `HKEY_DYN_DATA` dynamic key; table in NTKERN non-paged pool; SHPC slot polled through CONFIGMG's PCI enumerator; `UP` before `Reenumerate_DevNode`; `DOWN_QUIESCED` after the cutoff in every run (10/10 ordering checks pass; the two negative tests fail as designed); handshake and `Remove_SubTree` only on CLEAN; vetoes honoured | **pass**, same (5/5 ordering checks pass) |
| 2. Receiver and handshake | **pass**: record read at `HwFindAdapter` in 0-1 ms; `DOWN_QUIESCED` seen 10-152 ms after the CM wrote it, held SRBs completed `NO_DEVICE`, acknowledged 1-3 ms later; handshake CLEAN in about 200 ms guest time (0.7-0.9 s host); no I/O after the acknowledgement in 2/2 clean ejects | **pass**: seen 9-196 ms after the write; handshake CLEAN in 7/7 ejects (0.2-0.8 s host); no I/O after the acknowledgement; **the controller is shut down and disabled before Me's port unloads the image with no callback** |
| 3. MD-4 pool | **pass**: 33-page `_PageAllocate` block (fixed, aligned, contiguous, below 4 GiB) carries queues, PRP list pages and a bounce buffer; survives every removal and re-arrival (same linear and physical address, metadata intact, one block reused across 5 loads); reclaimed only with evidence (acknowledged disable, `DOWN_QUIESCED`, newer generation); quarantined otherwise; cap refuses a new block before CC.EN; `_PageFree` after a clean stop works | **pass**: same; a removal that bypasses the CM (no callback, controller left enabled on the block) is quarantined until the next load's acknowledged disable, then reused |
| 4. Write-through on Me | n/a (98 SE keeps its native stop) | **pass for function, cost not measurable in the emulator**: FUA on 1550/1550 writes; VWC off by Set Features, verified by Get Features (and QEMU's trace); write+commit of 16 MiB: 1381 ms (cache on), 1365 ms (FUA), 1428 ms (VWC off): within noise. QEMU's NVMe model ignores FUA |
| 5. G1 deferred bring-up | **pass with a 9x rule**: scan INQUIRY held during bring-up (6.4 s and 40.3 s RDY delays) and the disk appears; RDY never / identify failing: adapter started, `failed`, memory owned, no disk. **A BUSY scan INQUIRY is never retried (no disk)** | **pass with the same rule**; also: completing the held INQUIRY with `BUS_RESET` after `HwResetBus` TRUE makes the port fail `ScsiPortInitialize`, call `HwAdapterState(FALSE)` and unload |
| 5. G4 failed reset | **pass**: FALSE keeps the SRB held; the port calls `HwResetBus` again every 7.5 s, never completes the SRB itself and starts nothing else; a late completion is accepted; a truthful reset (RDY=0 within 1 ms) recovers transparently | **pass**, identical behaviour |

Verdict for MD-5 and MD-4 on 9x: **the documented, VxD-free design works on 98 SE and Me in the
emulator**, with three design corrections (below): the CM's `Query_Remove_SubTree` is the
file-system quiescence step and cannot be cancelled in place; bring-up must hold the scan
INQUIRY (never BUSY, never complete it from a reset); and a veto that outlasts a link-down leaves
a stale devnode the next tunnel must not reuse. Real-hardware items stay open (end).

## Setup

| Item | Value |
|---|---|
| VM area | `E:\nvme98-vm\9x\` (local, not committed): `run.sh <98\|me> <tag> [new\|keep\|prep]`, `g.sh` (QMP typing, tunnel, screendumps), `mkxfer.sh` (FAT16 transfer disk D:), `prepupd.sh`, `g1.sh`, `bench.sh`, `w4.sh`; `98/`, `me/` with `prep.qcow2` (our prep overlay on the G9 prep image), per-run `sys.qcow2`, `nvme-<tag>.qcow2`, `debugcon-<tag>.log`, `trace-<tag>.log`, screenshots; `NOTES.md` run journal |
| Prep (our overlay only) | `D:\PREP.BAT`: `CM9X.SYS` to `SYSTEM32\DRIVERS`, `CTL.EXE` (cm9xctl) to `C:\`, the q9x image over `NVME98.MPD` (and `NVME2K.MPD` on 98 SE), then `C:\CTL install`, which makes `cm9x.sys` the WDM function driver of the usb4-host devnode (`HKLM\Enum\PCI\VEN_1B36&DEV_00F4...` gets `Driver=System\0077`; that class key gets `DevLoader=*ntkern`, `NTMPDriver=cm9x.sys`), as an INF would. One restart. Later images are copied with `D:\Q9 NVQ9X` before the tunnel comes up (the `.mpd` loads at arrival, so no restart) |
| Tunnel | The emulator's generic profile has no CM in the guest: the host raises the tunnel (`qom-set tb0 tunnel up`); `cm9x.sys` sees the SHPC slot occupied, publishes `UP` and re-enumerates the bridge. Link-downs: `tunnel surprise`, harness `x-faults` (`at=fetch op=read nth=N act=surprise\|hold`). The CM's own teardown after an orderly eject is the stage code 0xCF, which the harness rule `at=stage code=0xcf act=surprise` turns into a tunnel down (stand-in for the CM closing the path) |
| Workloads | `copy /b F:\BIG.BIN NUL`; `CTL write\|verify F:\Tn.BIN <KiB> <seed>`; `CTL bench F:\Bn.BIN 16`; `CTL hold F:\HOLD.TXT <s>` (open file, veto); `CTL eject [F:]` (optional level-0 volume lock and unlock, then an eject request); `CTL set q9\|cm <name> <n>` (options in `HKLM\Software\nvme98\Q9x` and `...\Cm9x`) |
| Ordering check | `tests/cm9x/q9log.py <trace>`: per tunnel down, `usb4_tp_cutoff` < CM `DOWN` (0xCB) < nvme98 receipt (0xE9) < `AckGeneration` (0xEA) < CM sees it (0xCC) < CONFIGMG removal and the first `HwAdapterState(FALSE)`; host ns from the trace. `tests/cm9x/ejects.py`: handshake timings |
| Stage codes | [TESTING.md](../TESTING.md) unified table: 0xC7-0xCF cm9x, 0xD0-0xDF G9 (moved from its colliding values), 0xE0-0xF8 q9x, 0xFA-0xFD `CTL bench` |

**Clocks under TCG.** The guest's VMM system time (`Get_System_Time`, used for every q9x
deadline) ran up to 3.1 times slower than host time while the guest was idle (100 timer ticks of
100 ms = 10 s guest = 31 s host) and at about host speed while it was busy (the write benchmark's
`GetTickCount` matched the host trace). A 5 s guest deadline therefore took 5-16 s of host time;
all the "ms" in nvme98 logs are guest time, all trace times are host time. Measured relations
inside the guest (deadline before or after another guest event) are unaffected; absolute numbers
are TCG numbers.

## Item 1: the 9x CM stand-in

`tests/cm9x/cm9x.sys`, a WDM function driver (Windows 2000 DDK, `DRIVERTYPE=WDM`; its imports are
12 WDM `ntoskrnl` names NTKERN provides on 98 SE and Me). One PASSIVE_LEVEL system thread does
everything; VMM, CONFIGMG and PCI-enumerator services are called through the `INT 20h` dynalink
(thunks built in non-paged pool), the route usb4-emu `REENUM-EXPERIMENT.md` measured for
xhci98's CM.

| Step | How (documented Windows 98 DDK services) | Result (98 SE and Me) |
|---|---|---|
| Table | `ExAllocatePoolWithTag(NonPagedPool)`, 1056 bytes, zeroed, signed, random cookie, never freed | at `C18A2000` (98 SE) and `C18A5000`/`C18A5060` (Me): system arena |
| Per-boot record | **`_RegCreateKey` under `HKEY_DYN_DATA` returned `ERROR_ACCESS_DENIED` (5)**, so the record is a **dynamic key**: VMM `_RegCreateDynKey("Nvme98Cm")` with a value `LinkState` (`REG_BINARY`, 32 bytes) answered by the CM's own query provider (`provider_info` with `PROVIDER_KEEPS_VALUE_LENGTH`; the provider sets the value length itself, VMM does not fill it with that flag) | created at boot (rc 0) on both; nvme98 reads it with `_RegOpenKey`/`_RegQueryValueEx` (rc 0, 32 bytes, 0-1 ms). A dynamic key exists only while its provider is loaded during this boot, so no record crosses a boot; the CM is never unloaded (its code answers the queries) |
| Bridge and SHPC | CONFIGMG `Locate_DevNode`, `Get_Child`/`Get_Sibling`/`Get_Device_ID` to find `PCI\VEN_1B36&DEV_0001...\BUS_00&DEV_05&FUNC_00`; config space through `CONFIGMG_Call_Enumerator_Function` with `PCI_ENUM_FUNC_GET/SET_DEVICE_INFO` (PCI.H): secondary bus 1, SHPC capability at 0x40, slot status every 10 ms (DWORD select 9, presence bits 11:10) | works on both; no raw config cycles (CF8/CFC is used only by the Mode 1 negative test) |
| `UP` | `LinkGeneration++`, `IN_USE \| UP` (odd/even `Sequence`), then `Reenumerate_DevNode(bridge)` | nvme98's rendezvous found the entry with the new generation in every load |
| `DOWN_QUIESCED` | slot empty (usb4-emu stage 3, after the stage-2 cutoff): `IN_USE \| DOWN_QUIESCED`, wait for `AckGeneration` (5 s guest), then `Query_Remove_SubTree` on the endpoint devnode and `Remove_SubTree` only if not vetoed; a veto is retried every 2 s, never forced | all 15 link-downs with an nvme98 instance bound and a valid statement pass the ordering check ([Q-9x/ordering.txt](Q-9x/ordering.txt)): cutoff 0.2-0.5 ms after T0 (once 10.8 ms), CM `DOWN` 2-31 ms after T0, always after the cutoff; the two negative tests (no `QUIESCED` flag; a statement with no stage 2) fail it as designed |
| Orderly eject | `CTL eject` writes `EjectRequest`; the CM: `Query_Remove_SubTree` (a veto ends it, no request), `RemoveGeneration`, `RemoveRequest++`, `REMOVE_PENDING`; wait for `RemoveAckRequest` (5 s guest); CLEAN: `Remove_SubTree`, re-read the outcome, then the teardown (0xCF); otherwise: `RemoveCancel`, clear `REMOVE_PENDING`, wait for `CANCELLED`, then `Remove_SubTree` + `Reenumerate_DevNode` (below) | 9 clean ejects (2 on 98 SE, 7 on Me), 2 vetoed (`CR_REMOVE_VETOED` with a file open: no request made), 3 cancelled after a clean acknowledgement (test switch), 1 not clean (SHST never), 3 FAILED (adapter already failed), 2 timeouts (nvme98 test switch `NoAck`) |
| Veto | `CM_QUERY_REMOVE_UI_NOT_OK`, retried, never forced | an open file (`CTL hold`) and a DOS box whose failed `copy` kept the file open each vetoed every query until they ended; the CM never called `Remove_SubTree` while vetoed |

What a WDM driver on 98 SE and Me **cannot** do through WDM interfaces, and what the stand-in
used instead (record for the maintainer, MD-5 constraint "no undocumented APIs"):

- **No WDM interface for CONFIGMG** (devnode walk, `Query_Remove_SubTree`, `Remove_SubTree`,
  `Reenumerate_DevNode`), for **`HKEY_DYN_DATA`** or for **another device's config space** (the
  NVMe and the bridge are not in the CM's stack; a WDM filter on the bridge breaks PCI.VXD's
  enumeration, usb4-emu `REENUM-EXPERIMENT.md`). All of these are documented VxD services
  (CONFIGMG, VMM, the PCI enumerator through CONFIGMG); the stand-in calls them through the
  `INT 20h` dynalink from its system thread. The services are documented; calling them from a
  WDM driver is the mixed model xhci98's CM already uses and the DDK headers do not describe it
  for WDM drivers. **Maintainer to confirm** this counts as "documented" under MD-5.
- **No static key under `HKEY_DYN_DATA`** (VMM refuses it): the documented alternative is a
  dynamic key with a provider, which ties the record's lifetime to the CM's code.
- **No cancel for a successful `Query_Remove_SubTree`** (item 2, "Cancellation").
- **No volume flush from ring 0** short of the query itself: Int 21h 710Dh (Reset Drive) is not
  passed by `VWIN32_DIOC_DOS_IOCTL` (carry set, AX=1); a level-0 Lock Logical Volume (Int 21h
  440Dh CX=084Ah, then 086Ah) through VWIN32 flushes from ring 3 (6-35 ms) but is refused with
  files open (AX=5). In every clean eject, `Query_Remove_SubTree` itself left nothing to write
  (below), so the CM's query is the file-system quiescence step.

## Item 2: the nvme98 receiver and the MD-5 handshake

`src/q9x.c` (`NVME98_Q9X=1`), on the inherited data path:

- **Rendezvous** (`HwFindAdapter`, once): VMM `_RegOpenKey(HKEY_DYN_DATA, "Nvme98Cm")`,
  `_RegQueryValueEx("LinkState")`, `_RegCloseKey`; accepted only if the record's signature,
  version, size and table size match, the table address is in the system arena (>= C0000000) and
  the table's signature, version, size, counts and cookie match the record. 0-1 ms on both OSes.
  A missing record (no CM, or the `IgnoreCm` test switch) means "CM channel unavailable".
- **Polling**: one `RequestTimerCall` tick (100 ms guest), always re-armed while the adapter is
  not stopped, so an idle adapter sees the table too; the inherited fallback timer is not used
  (one timer per adapter). `HwStartIo` polls first. Every deadline is VMM `Get_System_Time`
  elapsed time.
- **Evidence 2**: `DOWN_QUIESCED` for the recorded generation or a newer one: everything
  outstanding (`ScsiPortCompleteRequest`) completes `SRB_STATUS_NO_DEVICE`, the pool record
  becomes CLEAN, state `LINKDOWN`, then `AckGeneration`. Seen 10-152 ms (98 SE) and 9-196 ms (Me)
  after the CM wrote it (one tick plus TCG lateness), acknowledged 1-3 ms later, the CM saw the
  acknowledgement 1-17 ms after that. Runs with a read held at its fetch (98 SE b1, Me m1): the
  held read completed `NO_DEVICE` on the statement, IOS retried and reported "General failure"
  (98 SE also showed VFAT's "Disk Write Error" screen), and no `HwAdapterState(FALSE)` (98 SE)
  came before the acknowledgement ([Q-9x/ordering.txt](Q-9x/ordering.txt)).
- **Negative tests** (98 SE): `DOWN` without `QUIESCED` (CM `NoFlag`) was ignored and the MD-1
  deadline (5 s guest, 15.7 s host) released the held read with the pool QUARANTINED; a statement
  with the gate closed and no stage 2 (CM `Mode 1`, `x-gate closed`) was accepted by nvme98 (it
  cannot tell) and the host-trace check fails it, as on NT (G3 run E).
- **Handshake** (`REMOVE_PENDING` with a request number and generation): new SRBs get BUSY;
  drain; an internal NVMe Flush (CID 7Fxx, 1-6 ms); CC.SHN=01b and SHST=10b (or `ShstMs`); CC.EN=0
  and RDY=0 (or CAP.TO); pool record CLEAN; then `RemoveOutcome`, `RemoveDetail`,
  `RemoveAckGeneration` and last `RemoveAckRequest`. Steps advance one per tick, so CLEAN came
  about 200 ms guest after the request (0.6-0.9 s host on 98 SE, 0.2-0.8 s on Me,
  [Q-9x/handshake-timings.txt](Q-9x/handshake-timings.txt)). Outcomes seen: CLEAN (detail 7:
  flush, SHST, RDY=0), ISOLATED_NOT_CLEAN (`x-shst-delay-ms -1`: SHST not complete in 2 s, RDY=0
  acknowledged; the CM aborted), FAILED (request to an adapter already in `failed`), no
  acknowledgement (`NoAck`, CM timeout, abort).
- **After Remove_SubTree**: 98 SE called `HwAdapterState(FALSE)` inside `Remove_SubTree`, which
  found the controller already disabled ("controller already disabled", pool CLEAN). Me called
  nothing, as G9 measured: the image was unloaded with the controller already shut down (CC.SHN,
  SHST complete) and disabled by the handshake, which is what MD-5 needed on Me. The CM re-read
  `RemoveOutcome` after `Remove_SubTree` returned: CLEAN in 9/9; no I/O reached nvme98 after a
  clean acknowledgement in any eject, with or without the ring-3 volume lock. The INVALIDATED path
  (I/O after the acknowledgement re-enables the controller) is implemented but was never
  exercised.
- **Re-arrival** after a clean eject: a fresh load found CC=0 and CSTS=0, reused the pool, and
  files written before the eject verified (98 SE `T1.BIN` 1 MiB, Me `T1.BIN` 1 MiB).
- **Me shutdown** (`CachesData` TRUE): `SRB_FUNCTION_SHUTDOWN` after the last FAT and FSInfo
  writes ran the same machine without the disable: Flush (5 ms), CC.SHN, SHST complete in 53 ms
  guest, SRB completed, then the ACPI power-off. 98 SE shutdown: `HwAdapterState(FALSE)` after
  the last writes; the bounded stop (stall budget 1 ms) did not see its Flush complete, SHST and
  RDY=0 were acknowledged.

### Cancellation and the veto (correction to MD-5)

- **Veto before the request**: with a file open, `Query_Remove_SubTree` returned 0x17 and no
  `REMOVE_PENDING` was ever set; nvme98 never noticed. This is the "veto resumes" case, and it
  costs nothing.
- **Cancel after a successful query** (CM switch `CancelAfterAck`): nvme98 resumed correctly
  (CANCELLED; re-enable through the deferred bring-up, 625 ms guest), **but the volume stayed
  dead: IOS failed every later request ("General failure", `open failed (31)`) without sending
  any SRB to the miniport** ([Q-9x/98-cancel-after-query-volume-dead.png](Q-9x/98-cancel-after-query-volume-dead.png)).
  A successful `Query_Remove_SubTree` commits IOS to the removal, and 9x has no documented call
  that undoes it. The stand-in therefore recovers a cancelled or not-clean eject with
  `Remove_SubTree` followed by `Reenumerate_DevNode(bridge)`: the device comes back as a fresh
  load (pool reused) and the volume works (98 SE: `T2.BIN` verified). On 98 SE that removal
  gives `HwAdapterState(FALSE)` (bounded stop: flush, SHST, RDY=0 seen); on Me it gives nothing.
- **A veto that outlasts a link-down** (Me m2): a DOS box kept the failed file open for more
  than the CM's 5-minute retry window; the CM gave up; when the tunnel came back, the CM published
  `UP` and re-enumerated, and PCI.VXD **reused the stale devnode**, so the new endpoint was served
  by the old `LINKDOWN` instance, which fast-failed its INQUIRY. Exiting the DOS box, a link-down
  and a new `UP` recovered it.

## Item 3: the MD-4 pool

- **Allocation** (`HwFindAdapter`, before any CC.EN write): VMM `_PageAllocate(33, PG_SYS, 0, 0,
  0, 0xFFFFF, &phys, PAGEFIXED | PAGEUSEALIGN | PAGECONTIG | PAGEZEROINIT)`; EAX and EDX both
  return the linear address, which is also the handle `_PageFree` takes. Layout: pages 0-15
  replace the port's uncached extension (admin and I/O queues, the 40 KiB PRP list and identify
  pool), pages 16-31 a 64 KiB bounce buffer, page 32 metadata (signature, physical and linear
  address, load count). Physical addresses seen: 1FE15000-1FE94000 (512 MiB guests), page
  aligned. The same cache treatment as the base gives the uncached extension. No
  `ScsiPortGetUncachedExtension` call at all.
- **Operational**: every load's queues and PRP lists lived in the pool (47 loads; 2 more were refused by the cap). With
  `Bounce=1` 109 transfers went through the bounce buffer (PRPs pointing into the pool, copies
  on write submission and read completion); `T4.BIN` (2 MiB) and `T2.BIN` verified.
- **Records** in the CM table (`NVME98LS9X_POOL`): FREE, IN_USE, CLEAN, QUARANTINED. At each load
  nvme98 scans them under an interlocked lock:
  - a CLEAN block is reused after its metadata checks out (98 SE: block 0 reused across 5
    loads, linear E45D5000 and physical 1FE94000 unchanged; Me: E4653000/1FE52000);
  - a block of its own function left IN_USE or QUARANTINED becomes CLEAN when the entry's
    generation is newer than the block's (the endpoint was replaced, MD-2) - 98 SE b2: two
    quarantined gen-1 blocks reclaimed at gen 2;
  - otherwise it stays QUARANTINED (counted) and the load allocates a new block; this load's
    own acknowledged disable of the same function then makes the old block CLEAN - Me m1: after a
    CONFIGMG remove that bypassed the CM (no callback on Me), the next load found
    `CC=00460001 CSTS=1` with the old queues in block 0, quarantined it, disabled the controller
    (acknowledged in 3 ms) and released block 0 to CLEAN.
- **Release with evidence**: handshake (acknowledged disable), `HwAdapterState(FALSE)` with RDY=0
  (98 SE), `DOWN_QUIESCED` (evidence 2). **Without evidence**: MD-1 deadline and stops that saw no
  RDY=0 quarantine the block. With `PoolFree=1`, `_PageFree` of a CLEAN block at a clean
  `HwAdapterState(FALSE)` returned 1 and the record went FREE (98 SE).
- **Cap**: with `CapPages=20` and no reusable block, `HwFindAdapter` refused before CC.EN
  ("cap reached (0 + 33 > 20)", `DriverEntry` status 1, no disk).
- **Without a CM table** (`IgnoreCm`): an `.mpd` cannot host a dynamic key (it is unloaded at
  every removal) and VMM refuses static `HKEY_DYN_DATA` keys, so nvme98 counts leaked pages in
  `HKLM\Software\nvme98\Q9x "Leak"` with a boot epoch (VTD `VTD_Get_Date_And_Time` minus
  `Get_System_Time`) and the uptime. **A 3 s epoch tolerance failed under TCG** (the epoch moved
  43-65 s between loads of one boot, b4); the kept rule is conservative: a new boot only when the
  uptime went backwards or the epoch moved by more than 3x the elapsed uptime + 10 s, so errors
  only over-count. Measured (b5): 33, 66, 99 pages, the 4th load refused (99 + 33 > 128), and
  after a guest restart "new boot", counter back to 33.

## Item 4: write-through on Me

| Mode | Implementation | Evidence | 16 MiB write + commit, host ms (3 runs) |
|---|---|---|---|
| 0: volatile write cache on | none | | 1498, 1362, 1284 (mean 1381) |
| 1: FUA | CDW12 bit 30 on every write command (the only physical writes the inherited path issues; there is no RMW write-back yet) | 1550 of 1550 writes carried FUA | 1546, 1279, 1269 (mean 1365) |
| 2: VWC off | Set Features FID 06h, WCE=0 in every bring-up (first load, resume, re-enable after a cancel or a SHUTDOWN), then Get Features FID 06h | Set status 0, Get dw0=0: "volatile write cache OFF (verified)"; QEMU trace `pci_nvme_getfeat_vwcache ... result=disabled` | 1524, 1333, 1427 (mean 1428) |

[Q-9x/writethrough-bench.txt](Q-9x/writethrough-bench.txt). **The emulator cannot measure the
cost**: QEMU 11.1.2's `hw/nvme` ignores the FUA bit (no `BDRV_REQ_FUA`), and with the cache off
its block layer writes through to a host SSD whose flushes are cheap, while TCG CPU time dominates
the transfer. The difference (3% for VWC off) is within run-to-run noise. Real SSD numbers need the
P14s with an enclosure. VWC off is the variant whose effect the driver can verify (Get Features),
and it covers writes the driver issues on any path; FUA is per command and relies on the device
honouring it.

## Item 5: G1 and G4 on 9x

### G1: deferred bring-up

`HwInitialize` writes CC.EN (after disabling a controller found enabled) and returns TRUE; the
tick carries RDY (deadline CAP.TO, 7.5 s for QEMU), queue creation, identify and the VWC feature;
the first SRB (the scan INQUIRY, sent inside `ScsiPortInitialize`, i.e. inside `DriverEntry`) is
held. **Timer callbacks run while `DriverEntry` waits for that INQUIRY** on both OSes.

| Case | 98 SE | Me |
|---|---|---|
| RDY 6 s, hold until bring-up ends | INQUIRY held 6.4 s, disk present | held 6.7 s, disk present ([png](Q-9x/me-g1-pure-hold-disk-present.png)) |
| RDY 40 s (test switch `BringupMs` beyond CAP.TO) | `HwResetBus` at about 15 s and 30 s during the hold; FALSE: hold kept; disk present after 40.3 s | same (40.5 s) |
| Same, `HwResetBus` completing the held INQUIRY `BUS_RESET` and returning TRUE | not run | **the port called `HwAdapterState(FALSE)` and `DriverEntry` returned 1: adapter unloaded** (and the prototype's stop left CC.EN=1 in that state; fixed in 0d18dbd) |
| RDY 6 s, INQUIRY completed BUSY after a 2 s hold | **never retried**: `DriverEntry` returned, no TEST UNIT READY, no disk | same ([png](Q-9x/me-g1-busy-inquiry-no-disk.png)) |
| RDY never (or later than CAP.TO) | `failed` at CAP.TO, INQUIRY completed `NO_DEVICE`, `DriverEntry` 0, no disk, disable acknowledged, memory owned; later SRBs fast-fail | same |
| Identify failing (`status=0x6`) | `failed` after 5 s, same outcome | same |
| Previous controller never clears RDY (`x-rdy-clear-delay-ms -1`) | `failed` at CAP.TO, memory quarantined at the stop | not run |

Pass condition (bus scan completes with or without a disk, no port reset completes anything
during bring-up, a failed bring-up ends `failed` with the adapter started and its memory owned):
**met on both, with the hold rule below**. The hybrid hold-then-BUSY of PLAN 0.3 does not apply to
9x's scan INQUIRY.

### G4: the 9x port after `HwResetBus` returns FALSE

Stimulus: a read held at its fetch (`at=fetch op=read act=hold`) with the controller alive.

| Case | 98 SE | Me |
|---|---|---|
| First `HwResetBus` after the fetch | 7.3 s | 7.1-7.4 s (G9 saw 13.2 s and 22.7 s with the base driver: the 9x port's timeout is not a fixed number) |
| Truthful reset (CC.EN=0, RDY=0 within 1 ms, `BUS_RESET`, TRUE, re-bring-up) | re-bring-up 499 ms, the retried read served, copy continued | 593 ms, copy completed ([png](Q-9x/me-g4-truthful-reset-copy-completed.png)) |
| Always FALSE (`ResetMode=1`) | `HwResetBus` every 7.5 s (8 calls), nothing else sent; after release the late completion (SUCCESS) was accepted and I/O continued | same (9 calls in 70 s, then 7 more) |
| Failed disable (`x-rdy-clear-delay-ms -1`): FALSE, timer waits CAP.TO, `failed` | `HwResetBus` every 7.5 s (14+ calls), never completes the SRB itself; the application hangs; evidence 2 later released it (`NO_DEVICE`) | same (25+ calls) ([png](Q-9x/me-g4-failed-reset-released-by-evidence2.png)) |

Pass condition (the port never completes a held SRB while the device may still reach its
buffer): **met on both**. The cost of FALSE is a hung LU (as on 2000 and XP x64, G4) until
evidence, the gone deadline (if presence is lost) or a later acknowledged disable; the 9x port
keeps calling `HwResetBus`, which gives the driver a natural point to re-check RDY (the prototype
stops re-checking once `failed`).

## Findings outside the items

- **Me does call `HwAdapterState(FALSE)` when `ScsiPortInitialize` fails** (G1 table): G9's "Me
  never calls a stop" holds for removal and shutdown, not for an initialisation failure.
- **Reopen without endpoint reset after an early statement** (98 SE b3): after the negative test
  the CM re-published `UP` on the never-reset endpoint; the next load found `CC=00460001 CSTS=1`
  (old queues) and disabled it (acknowledged in 1 ms) as the reopen invariant requires, but it had
  already picked and zeroed the block that the bogus statement had marked CLEAN, before that
  disable. Only a CM that breaks its ordering obligation (MD-2) exposes this; product code should
  zero and program pool memory only after the load's own acknowledged disable, and log the
  violation.
- **The 9x port reuses a stale devnode** whose miniport instance is `LINKDOWN` (item 2,
  "Cancellation").

## Consequences for the plan

1. **MD-5 (decided) is qualified in the emulator on 98 SE and Me** with the stand-in. Proposed
   PLAN wording changes:
   - The CM's sequence is `Query_Remove_SubTree` (file-system quiescence and veto) ->
     `REMOVE_PENDING` -> CLEAN acknowledgement -> `Remove_SubTree` -> path teardown. "Veto
     resumes" holds before the request; **after a successful `Query_Remove_SubTree` there is no
     in-place resume on 9x**: a cancel, timeout or non-clean outcome ends in `Remove_SubTree` plus
     `Reenumerate_DevNode` (a fresh load), and the eject is reported not clean.
   - The CM must not raise or re-enumerate a tunnel while an earlier endpoint's devnode still
     exists (veto still pending), or nvme98 needs an in-place reopen (PLAN 2.6) for an `UP` with a
     newer generation while `LINKDOWN`. Either is an open design item for xhci98's CM.
   - 0.10 evidence 2 on 9x: carrier = interface 3 of [`include/nvme98cm.h`](../../include/nvme98cm.h)
     (dynamic-key rendezvous); G3's 9x row can move from "designed" to "prototype pass" (the
     ordering stays a host-trace check per OS).
2. **MD-4 (decided) is qualified**: the pool contract of PLAN 0.5 / MD-4 can be written from item
   3 (allocation flags and limits, record states, evidence rules, the cap, the `HwFindAdapter`
   failure before CC.EN). The "no CM" accounting is a heuristic (boot epoch); with xhci98's CM
   present the table is exact.
3. **G1 on 9x (0.3, 0.10 bring-up)**: on 9x the scan INQUIRY is held until bring-up completes or
   fails (bounded by CAP.TO; INQUIRY's `TimeOutValue` is 30 and the port's resets during the hold
   are answered FALSE); never BUSY, never completed from a reset during bring-up. The
   "Qualification" row "9x runs of G1 and G4" can be marked done (emulator).
4. **G4 on 9x (0.10 reset rule)**: the NT rule carries over (truthful reset; FALSE keeps SRBs held;
   a late completion is accepted). Product code should re-check RDY at each repeated `HwResetBus`
   and recover on a late acknowledged disable.
5. **Write-through on Me (MD-5)**: both mechanisms work; prefer VWC off with Get Features
   verification at every bring-up, with FUA as the fallback where Set Features VWC fails. The cost
   is not known until it is measured on a real SSD.
6. **Clock**: `Get_System_Time` is usable for elapsed-time deadlines on 9x but lags host time by up
   to 3x under TCG; deadlines in TESTING should be read as guest time when run in the emulator.
7. **Stage codes**: the G9 table moved to 0xD0-0xDF, the 9x CM stand-in uses 0xC7-0xCF and the
   9x layer 0xE0-0xF8 ([TESTING.md](../TESTING.md)); 0xB5-0xBF, 0xF9 and 0xFE-0xFF remain free.

## Decisions needed (maintainer)

- **Interface 3** (revised; see "Interface 3 revision"): adopt the revised
  [`include/nvme98cm.h`](../../include/nvme98cm.h) interface 3 as the 9x interface after the
  second Codex review, or change it before xhci98 copies it.
- **"Documented APIs" for the CM on 9x**: the stand-in reaches CONFIGMG, VMM registry and PCI
  enumerator services from a WDM driver through the `INT 20h` dynalink (no WDM equivalents
  exist). Confirm this satisfies MD-5's constraint; otherwise the CM's removal and rendezvous
  half has no documented route on 9x without a VxD.
- **Cancel semantics** (consequence 1): accept "no in-place resume after a successful
  `Query_Remove_SubTree`; remove and re-enumerate instead".
- **Write-through mechanism on Me** (consequence 5).

## Limits

- Emulator only (TCG, QuickInstall's 98 SE IOS set, stock Me); A1/MD-2 and real-SSD Flush, SHST
  and write-through costs need the P14s with an enclosure.
- The CM stand-in detects "teardown complete" from the SHPC slot going empty (usb4-emu stage 3);
  xhci98's CM knows it because it tears the path down itself. Its own teardown after an orderly
  eject is a harness stage rule.
- One adapter; no Device Manager Disable, standby or hibernate on 9x (Me's standby did resume
  once by accident, untested otherwise); no RMW write-back (no 4 KiB namespace) in these runs.
- The INVALIDATED outcome and drain with admitted I/O were not exercised (no I/O arrived after a
  successful query in any run).

## Interface 3 revision

Date: 2026-10-15. A Codex review of the first interface 3 (11 findings, "not ready") led the
maintainer to choose: revise everything and requalify. During the revision the maintainer also
approved two xhci98 proposals, which changed the layout: (a) the 9x locator is
`HKLM\System\CurrentControlSet\Services\nvme98` `LinkState` = {`'N98L'`, 1, table linear address,
272, cookie}, accepted only with a current-boot proof (a CM-hosted `_RegCreateDynKey` key holding
the same cookie); (b) `REMOVE_PENDING` is Flags bit 3, the CM's 24-bit request sequence is the
former `Reserved[0]` and nvme98's `seq << 8 | outcome` the former `Reserved[1]`. The 9x table is
therefore the interface 2 table (272 bytes); there is one handshake. Commits on `qual-9x`:
0c28e86 (header), 1b79401 and de6f723 (receiver), 1e9aa5f, 60fae79 and 497a80a (CM stand-in),
plus this record. Evidence: [Q-9x/rev/](Q-9x/rev/).

### Contract for xhci98 (from `include/nvme98cm.h`)

| Item | Value |
|---|---|
| Locator | `HKLM\System\CurrentControlSet\Services\nvme98`, value `LinkState`, `REG_BINARY` 20 bytes `NVME98LS9X_LOCATOR` {`0x4C38394E`, 1, `TableLinear`, 272, `Cookie`} |
| Current-boot proof | dynamic key `HKEY_DYN_DATA\Nvme98LinkState`, value `BootCookie`, `REG_DWORD` = `Cookie` (CM's own provider; the CM refuses to publish if the key already exists; it deletes the key if it ever unloads) |
| Table | `NVME98LS_TABLE` (272 bytes), `Cookie` (was `Reserved`) = the per-boot cookie; entry `RemoveSequence` (CM) and `RemoveAck` (nvme98) were `Reserved[0..1]` |
| Order (R11) | file-system quiescence + `Query_Remove_SubTree` (a veto is final) -> `REMOVE_PENDING` -> acknowledgement -> `Remove_SubTree` only on CLEAN; ISOLATED_NOT_CLEAN: `Remove_SubTree` + `Reenumerate_DevNode`; FAILED / none / cancel: the CM isolates first (teardown, `DOWN_QUIESCED`, link acknowledgement), then removes; new tunnel = fresh device (D2) |
| nvme98 poll period | at least every `NVME98LS9X_POLL_MS` = 250 ms, idle or busy (the prototype uses 100 ms) |
| Removal acknowledgement bound | `NVME98LS9X_ACK_BOUND_MS` = 12000 (drain 2 s, Flush 2 s, SHST 2 s, disable <= 5 s, polling); measured 203-213 ms guest for CLEAN, 2.27 s for SHST never |
| CM wait for it | `NVME98LS9X_CM_ACK_WAIT_MS` = 12000 + 2 x 250 + 1500 = 14000 ms (stand-in: timeouts at 14013-14285 ms) |
| Link-down acknowledgement | AckGeneration written last, after the cleanup, idle or not; seen 1-247 ms host after the statement on 98 SE, 50-176 ms on Me |
| CM wait for it | at most the gone deadline; not below `NVME98LS9X_CM_LINK_WAIT_MS` = 2 x 250 + 1500 = 2000 ms. **xhci98's provisional 3000 ms fits** |
| 9x gone deadline (MD-1) | `NVME98LS9X_GONE_MS` = 5000 ms default (tunable `GoneMs`) |

xhci98 input (1), idle acknowledgement: the 9x receiver already polls from its timer whether idle
or not and acknowledges `DOWN_QUIESCED` with nothing held (measured: idle instances acknowledged
in every surprise run). Order is cleanup (held SRBs completed, gone deadline cancelled, pool
released, an open request answered) then `AckGeneration` last. On 9x nvme98 does not report
`BusChangeDetected`: removal is the CM's CONFIGMG call, and a 9x port rescan in that window is
unmeasured. The NT receiver (interface 2) is not changed here.

### Per finding

| # | Finding | Change (header rule; receiver `src/q9x.c`; CM `tests/cm9x/cm9x.c`) | Qualified by |
|---|---|---|---|
| 1 | Pool ownership | Records moved out of the CM table into nvme98's own boot block (R7): states FREE, RESERVED, OWNED, ISOLATED (evidence, still owned), RELEASED (isolated and given up), QUARANTINED; owner token per load; per-function liveness (a new `HwFindAdapter` for a function ends the old token; `HwAdapterState(FALSE)` ends its own); claims, releases and frees under the boot-block lock; owner-only transitions check the owner; reuse only of RELEASED; DMA area zeroed only after the load's own acknowledged disable | 98 SE r3 / Me m3: two adapters with separate records, RELEASED blocks reused across functions only after release (evidence-2 or terminal), OWNED blocks of live owners never touched; E1 and E2 releases logged (r5); a corrupted RELEASED record taken out of circulation (`CorruptRec`, r5) |
| 2 | Terminal D2 removal | R9d: an accepted request is terminal (never re-enables, BUSY until the acknowledgement, then every request fails `NO_DEVICE` except SHUTDOWN); "cancel" = abandon the clean claim and D2 recovery; FAILED/timeout -> CM isolates first; `Remove_SubTree` failure: retry 30 s, keep devnode, tear down, no UP (R11 O6); `CANCELLED`/`INVALIDATED` withdrawn | I/O after a clean acknowledgement (`SkipQuery`, 20 s window): 144 (98 SE) and 150 (Me) requests refused, controller never re-enabled, `Remove_SubTree` then succeeded; cancel after CLEAN -> E201 D2 recovery on both; NoAck -> E3FF -> teardown, link acknowledgement, removal on both; O6 implemented, not provoked (corrected in the re-review: O6 was missing from the isolated branch; now provoked) |
| 3 | Transaction identity | R9: one request per instance named by (entry, generation, 24-bit sequence); the sequence at binding is never accepted; overlapping sequences ignored; `RemoveAck = seq << 8 \| outcome` written once by one locked store; CM validates sequence and generation and re-reads | Overlap (second request 300 ms later) ignored on both, `RemoveAck` kept naming the first; retry (same request re-published) left `RemoveAck` unchanged on both; D2 re-loads on the same generation did not take the old request |
| 4 | Entry identity, retirement | R8: one entry per port for the boot, (bus, slot) immutable and unique, duplicates rejected; generations never reset; UP only after the previous devnode's `Remove_SubTree` (R8c); identity re-checked at every poll; instance tokens nvme98-internal (no field in the approved layout) | Long veto (98 SE about 4 min, 120+ vetoed queries; Me about 60 queries): `UP` deferred while the old devnode existed, then a fresh instance on the next generation (the prototype's stale-devnode failure did not recur); duplicate entry -> table rejected (rc 9) on both |
| 5 | Synchronization | R6: field ownership, one CM writer, locked increments around payloads, bounded two-attempt snapshots, single-word acknowledgements by exchange, boot-block lock with one try from timer/StartIo (deferred work) and bounded retries elsewhere; publication-point accesses in inline assembly (compiler barriers) | All runs; deferred pool work exercised by the terminal release path; no lock-busy event occurred |
| 6 | Validation | R5: locator type/length/fields, proof type/length/cookie, table alignment, system-arena range and per-page presence (`_CopyPageTable`) before any dereference, header, legal-flag snapshot checks with identity loss; pool records: fields, range, presence, frame = recorded physical (provenance), metadata; trust boundary stated | Hostile modes on 98 SE: unaligned (rc 4), not-present reserved page (rc 5, no fault), private-arena address (rc 4), short locator (rc 2), wrong type (rc 2), proof mismatch (rc 11), stale locator (rc 11), header count (rc 6), duplicate (rc 9), illegal flags after binding (identity lost, no evidence taken, MD-1 deadline on the next link-down); Me: not-present (rc 5), duplicate (rc 9), illegal flags (identity lost) |
| 7 | Leak cap | R7/R11: one boot-wide budget in the nvme98 boot block (2-page `_PageAllocate` block found through `HKLM\Software\nvme98\Q9x "Acct"`, accepted only if present, mapping the recorded frame, signature + 64-bit cookie, and `_PageGetSizeAddr` = 2 pages); charged before `_PageAllocate`, uncharged only after `_PageFree`, whether or not a CM exists; no record slot or no budget -> `HwFindAdapter` fails before CC.EN; overflow-checked; no clock | `CapPages` 70 with 66 charged: second adapter refused before CC.EN, then accepted at 264 (98 SE r5); every boot found a stale reference and created a new block (no wall-clock rule); the first build used `_PageGetAllocInfo` and created a new block per load (fixed, de6f723) |
| 8 | Provider lifetime | R4: boot-resident CM, no `DriverUnload`, `QUERY_REMOVE` refused, a non-PnP pin device keeps the image; rundown defined if a CM unloads anyway; NT interface 2 lifetime clarified (no behaviour change) | `CTL unload` on both: `CM_Query_Remove_SubTree` refused (23), forced `CM_Remove_SubTree` succeeded, CM kept polling, ejecting and answering the proof. Without the pin (r3) the stand-in stopped polling about 4 minutes after the forced removal |
| 9 | Versioning | R2: frozen locator v1 / table v1, exact sizes, producers zero, new major = new value name and table, withdrawn prototype named, combinations listed, missing acknowledgement never means support | New CM + prototype receiver: no channel, E3FF, CM isolated first (both); prototype CM + new receiver after a restart: stale locator rejected (no proof, rc 10), no channel, the old CM's eject was not clean (E2FF) (both) |
| 10 | ABI | R1: 32-bit x86 only, little-endian, 4-byte fields, sizes/offsets asserted at compile time in every target, locator under `pack(4)`; NT keeps interface 2 | `scripts\build.cmd all` (9x, x86, amd64) and the Q9X 9x build pass with the assertions |
| 11 | Timing | R10-R12: bounds, transition tables for orderly eject and surprise removal, idle polling, late results, terminal timeouts; query-before-request order fixed; PLAN MD-5 history marked superseded | Ordering check (`q9log.py`): 12 statement-to-removal sequences on 98 SE and Me pass (superseded: recomputed in the re-review) ([Q-9x/rev/ordering.txt](Q-9x/rev/ordering.txt)); link-downs after a completed removal have no instance and are labelled so |

### Results by scenario

| Scenario | 98 SE | Me |
|---|---|---|
| Clean eject, re-arrival, files verify | pass (r1, r3) | pass (m3) |
| Surprise removal during I/O, evidence 2, ordering | pass (tunnel 1 during `copy`, r3) | pass (m3) |
| Two adapters (two tunnels, two NVMe) | pass: concurrent surprise on one and clean eject of the other, separate tokens and records, both files verified after re-arrivals | pass, same |
| Veto before the request | pass (E117) | pass (E117) |
| SHST never -> ISOLATED_NOT_CLEAN -> D2 | pass (E202, fresh load on the same generation) | pass |
| NoAck -> timeout -> CM isolates | pass (E3FF, 14.0 s) | pass |
| Cancel after CLEAN -> D2 | pass (E201) | pass |
| I/O after a clean acknowledgement | pass (refused, not re-enabled) | pass |
| Stale entry after a long veto | pass (UP deferred about 4 min) | pass |
| Malformed / hostile rendezvous and entries | pass (10 modes) | pass (3 modes) |
| Overlapping / retried requests | pass | pass |
| Mixed-version peers (both directions) | pass | pass |
| CM unload attempt | pass with the pin device | pass |
| Budget exhaustion; corrupt record | pass | not run (same code) |
| G4 truthful reset (rerun: reset path changed) | pass (BUS_RESET, re-bring-up, record OWNED -> ISOLATED -> OWNED) | not rerun |
| G1, write-through, Me SHUTDOWN SRB | not rerun (code paths unchanged) | not rerun |

Two 98 SE restarts did not complete (r5 after a long test sequence with DOS sessions open, r6
with a `copy` still running in a DOS box when the shutdown started); idle shutdowns of the same
binaries with two bridges (r7) and with an adapter present (r8) completed, and the mixed-version
runs restarted both OSes cleanly. Not investigated further (cause found in the re-review: an Explorer window of a vanished tunnel drive).

Emulator: usb4-emu `src/qemu/build` of 2026-10-15 13:46 copied to `E:\nvme98-vm\9x\bin`
(unaffected by later rebuilds); `run.sh TWO=1` adds `usb4-tunnel-bridge` tb1 at 00:06 with
`usb4-host tunnel-port2=tb1` and a second NVMe. Runs: 98 SE r1, r3-r9; Me m3, m4. QMP 48210/48220
(r1-r8, m3) and 48030/48040 (r9, m4).

### Limits

- The approved layout has no field for refused requests after the acknowledgement, so the CM
  cannot tell a clean eject that refused late I/O from one that did not; nvme98 logs them.
- Owner liveness is nvme98-internal; on Me a removed instance stays "alive" for its blocks until
  the same function loads again (its blocks are then released by E1/E2, or stay charged).
- R11 O6 (`Remove_SubTree` failing after a successful query) is implemented in the stand-in but was
  not provoked (provoked in the re-review, S3 and S4); no lock contention occurred, so the deferred paths ran only in their uncontended
  form.
- Emulator only; real-hardware items as before.

## Interface 3 re-review

Codex re-reviewed `qual-9x` at 531648a: not adoption-ready (3 P1, 6 P2). It confirmed the
sizes and offsets, the separation from NT interface 2, the rejection of a stale HKLM locator
without the proof, and the single-writer/exchange design. Fix commits on `qual-9x`: 13e2787,
49927d6, 075e830, dcb9f0f, 08c92e0, 386fb85, 84c4600, 2861a76, f40f6d1, 3d1433e, 074b74b, 44119ce, a79d154 and this record.
Evidence: [Q-9x/rerev/](Q-9x/rerev/).

### Decisions (2026-10-15)

- **P1-2: option A, chosen by the user.** The table address comes from the CM's resident
  provider. The proof key `HKEY_DYN_DATA\Nvme98LinkState` carries a second value,
  `TableLinear`, beside `BootCookie`. nvme98 requires it to equal the HKLM locator's address,
  size and cookie, or the channel is unavailable (rc 14).
- **P2-4: option 1, accepted.** Without a CM there is no independent current-boot authority.
  An invalid boot-block reference counts as a new boot, recorded as a residual. With a CM, a
  reference recorded under this boot's proof cookie whose block no longer checks out fails
  closed.

### For xhci98: the proof value `TableLinear`

| Item | Value |
|---|---|
| Key | `HKEY_DYN_DATA\Nvme98LinkState` (dynamic key, CM's own provider, `_RegCreateDynKey`), the same key as `BootCookie` |
| Value name | `TableLinear` (`NVME98LS9X_PROOF_TABLE_VALUE`) |
| Type, length | `REG_BINARY`, exactly 12 bytes: `NVME98LS9X_PROOF_TABLE` {`TableLinear`, `TableSize` = 272, `Cookie`}, little-endian ULONGs |
| Contents | the linear address of the `NVME98LS_TABLE`, 272, and the per-boot cookie (the same as `BootCookie`, the locator's `Cookie` and the table's `Cookie`) |
| When written | answered by the provider from the CM's resident memory for the whole boot. It is created in the same `_RegCreateDynKey` call as `BootCookie` (two values), after the table is filled and before the HKLM locator is written (R3a, then R3b); never changed in that boot |
| nvme98 checks | `BootCookie` == locator `Cookie` (rc 11 otherwise); `TableLinear` value present, 12 bytes, and each field equal to the locator's (rc 14 otherwise); then the address checks of R5c |

### Per finding

| # | Finding | Change | Qualified by (98 SE / Me) |
|---|---|---|---|
| P1-1 | The ISR could still use queues after the block was released | `Q9DetachQueues` closes the queue gate with a locked exchange and only then clears every queue, PRP, bounce and uncached pointer. This happens before any RELEASED or `_PageFree` (terminal handshake, evidence 2, `HwAdapterState(FALSE)`). `HwInterrupt` and both completion processors return at a closed gate, which is counted. All of these run only from the timer, `HwStartIo` or `HwAdapterState`, which SCSIport never runs concurrently with the ISR | S1: instance A acknowledges CLEAN, detaches and releases. Its devnode stays registered for 40 s (`SkipQuery`, `DelayRemoveMs`). B, the other tunnel's first load, claims A's block (record 0, same linear and physical address), then writes and verifies 1 MiB. A's own `HwInterrupt` was called 391 (98 SE) / 350+ (Me) times during that window (`IsrProbe`): every call returned FALSE with null queue pointers. 9x left the endpoint behind a bridge at 00:09 (same PIRQ link) disabled (CONFIGMG problem 0x1D), so the shared-line invocation is simulated by `IsrProbe` |
| P1-2 | Table address authority and residency | Option A (above). nvme98 also requires every table page present and writable, and locks them with `_LinPageLock` once per boot and table. R5 states the trust boundary | S7: hostile 11 (the provider's address differs) and 12 (locator rewritten with the right cookie) are both rejected with rc 14, and the disk still works without the channel |
| P1-3 | DriverEntry succeeded without the pin | The pin device is created in DriverEntry; if that fails, DriverEntry fails and nothing is published. The publisher thread starts only with the pin (R4) | 98 SE v3: with `FailPin=1`, DriverEntry failed, nothing was published and no enumeration happened. After `FailPin=0` and a restart, the CM published normally |
| P2-4 | An invalid boot-block reference reset the budget | The reference records the proof cookie (`BootCookie`). A reference of this boot whose block does not check out fails closed. Otherwise this is a first initialisation; without a CM that is the documented residual | S8 (`CorruptAcct` damages this boot's block): the next load reports "the reference was recorded in this boot ... accounting lost or corrupt, fail closed" and `HwFindAdapter` fails before any CC.EN write. Every fresh boot initialises a new block |
| P2-5 | No overall acknowledgement budget | One budget measured from first sight of the request: disable forced by 5750 ms, decisions due by 8750 ms (12000 - 250 - 2 x 1500). R10 calls 250 ms a requested interval and adds `NVME98LS9X_LATE_MS`. **A fall-through bug was found and fixed** (2861a76): after a step change inside one call, the elapsed time underflowed, so a disable that needed 4 s was reported FAILED at once | S5: see the results table and the clock note |
| P2-6 | One port's wait blocked the other's statements | Every CM wait is a per-port state of the one polling loop: link and removal acknowledgements, overlap/retry windows, delay before removal, `Remove_SubTree` retries, D2 re-enumeration, teardown. The CONFIGMG calls that can wait (`Query_Remove_SubTree`, `Remove_SubTree`, `Reenumerate_DevNode`) run on a per-port worker thread (074b74b). Header R6e, with the residual below | S2 on 98 SE: port 1's `DOWN_QUIESCED` came about 1 ms after its slot emptied while port 0 was still in its 14 s acknowledgement wait. Port 1 was acknowledged and removed before port 0 timed out. S3: same during port 0's `Remove_SubTree` retries ([Q-9x/rerev/98-v1-S2-S3-cm.txt](Q-9x/rerev/98-v1-S2-S3-cm.txt)) |
| P2-7 | O6 missing in the isolated branch | One O6 path for CLEAN, ISOLATED_NOT_CLEAN and cancel: after 30 s the CM tears down, keeps the devnode and retries the removal until it succeeds; the next UP gets a new generation. `FailRemove=N` injects failures | S3 (CLEAN) and S4 (ISOLATED_NOT_CLEAN) on 98 SE: E517 after about 30 s, teardown, removal succeeded on the next real attempt, then UP with a new generation |
| P2-8 | FAILED never detected a presence loss | Presence is polled in FAILED; `HwResetBus` enters GONE on all-ones from any non-terminal state; QUIESCED and STOPPED never enter GONE | S6 on 98 SE: a read was held and the reset not acknowledged (FAILED with the SRB held). Link loss with `NoFlag` (no evidence) gave FAILED -> GONE, then the MD-1 deadline after 5055 ms completed the held SRB with the block OWNED |
| P2-9 | The ordering checker over-counted | `q9log.py` correlates port, bus and generation (new stage arguments carry them). An acknowledgement and a timeout are distinct. NO-INSTANCE needs the port's retirement between its last UP and the statement. Statements without QUIESCED or before the cutoff are NEGATIVE | Recomputed from the raw traces ([Q-9x/rerev/ordering.txt](Q-9x/rev/ordering.txt)): no FAIL. The earlier "12/12 pass" counted only true acknowledgements, but the decoder that produced it would also have passed timeouts; with the new decoder the hostile-mode and old-receiver statements are TIMEOUT, as they must be |

### Corrections to the "Interface 3 revision" record

- "R11 O6 ... implemented ... not provoked" was wrong twice: O6 was missing from the
  isolated/cancel branch (P2-7), and it is now provoked (S3, S4).
- The ordering count is superseded by the recomputation above.
- The two 98 SE restarts that "did not complete" (r5, r6) and the same symptom in prep4 have a
  cause in the test environment, not the driver. Explorer restores the folder window of a
  tunnel drive that existed at the previous shutdown. When that drive is gone the window stops
  responding, and shutdown waits on it ("This program is not responding", "F:\ is not
  accessible", seen on screen). The preps now disable AutoPlay folder windows
  (`NoDriveTypeAutoRun` = 0xFF). As a precaution the CM also stops its CONFIGMG polling once
  the shutdown power IRP or `IRP_MJ_SHUTDOWN` arrives.

### Results by scenario

Runs: 98 SE v1 (S1-S6), v3 (FailPin), v4 (S7, S8, second half of S5), v6 (S5), v7 (S1-S4 and
S6 with the per-port worker), v9 (S3 with a79d154); Me v2 (S1), v8 (S1-S6), v10 (S7, S8). Outputs
are in [Q-9x/rerev/](Q-9x/rerev/). Emulator: the usb4-emu build of 2026-10-15 13:46 (unchanged).

| Scenario | 98 SE | Me |
|---|---|---|
| S1 (P1-1): A terminal and released, B reuses its block, A's ISR invoked | pass (v1, v7: 392 gate-closed calls, all FALSE, null pointers; B's 1 MiB write and verify OK) | pass (v2, v8: 600+ calls, same) |
| S2 (P2-6): port 1 surprise while port 0 waits (NoAck) | pass (v1, v7: port 1 `DOWN_QUIESCED` 3-4 ms after the slot emptied, acknowledged, removed; port 0 E3FF at 14.1 s) | port 1 statement and acknowledgement pass; port 0's timeout was delayed by the system-modal residual (below) |
| S3 (P2-7, P2-6): CLEAN, `Remove_SubTree` failing (O6), port 1 surprise during the retries | pass (v7, v9: E517 at 28.6 s, port 1 published and removed during the retries, then UP generation +1) | pass (v8) |
| S4 (P2-7): ISOLATED_NOT_CLEAN with O6 | pass (v7: outcome 2 after 2256 ms, E517, teardown, retired) | pass (v8) |
| S5 (P2-5): Flush 1.9 s + SHST 1.9 s + RDY 4 s | CLEAN after 2883 ms guest (v6) | CLEAN after 2763 ms guest (v8) |
| S5 (P2-5): SHST never + RDY never | FAILED after 7315 ms (v6) and 7326 ms (v4), E303 | FAILED after 7254 ms, E303 (v8) |
| S6 (P2-8): FAILED with a read held, link lost without a statement | pass (v1, v7: FAILED -> GONE on all-ones, MD-1 after 5018 ms, held SRB completed, block OWNED) | pass (v8: MD-1 after 5001 ms) |
| S7 (P1-2): hostile 11 and 12 | pass (v4: rc 14 for both; hostile 0 binds) | pass (v10: rc 14 for both) |
| S8 (P2-4): this boot's block damaged | pass (v4: fail closed before CC.EN) | pass (v10: same) |
| FailPin (P1-3) | pass (v3) | not run (same code) |

Ordering (`q9log.py`, raw traces): 98 SE v7 PASS 5, NO-INSTANCE 1, NEGATIVE 1 (`NoFlag`);
v9 PASS 2; Me v8 PASS 6, NO-INSTANCE 2, NEGATIVE 1; Me v10 PASS 1, NO-INSTANCE 1, TIMEOUT 2 (the
hostile 11/12 loads, which have no channel by design); no FAIL.

**Clock note (S5).** nvme98 and the CM both measure with VMM `Get_System_Time`. Under host
load (other agents' VMs on the same host) the guests' clock ran up to about 3 times slower than
QEMU's virtual clock. For example, 98 SE v6 CLEAN took 2883 ms guest but 8.80 s virtual, and
Me v8 FAILED took 7254 ms guest but 22.5 s virtual. The budget and the CM's wait hold
consistently in guest time, which is the time both parties see. This emulator cannot show
how the budget relates to wall time.

### Known limits (re-review)

- **System-modal messages (9x).** If a volume's device goes away with unwritten data, Me
  shows the system-modal "Disk Write Error" message (S1 with `SkipQuery`, S2 surprise). While
  it is up, no ring-0 thread of the system VM runs, the CM's writer and its CONFIGMG worker
  included: no heartbeat, and the worker's 5 s notice was logged only after dismissal. A
  statement or timeout due in that window is published after it: port 0's 14 s timeout was
  recorded at 282 s. nvme98's VxD timer keeps running throughout, so polling, all-ones
  detection and MD-1 do not depend on the CM. Recorded as residual R6e. The per-port worker
  still keeps a slow CONFIGMG call from stalling other ports when no such message is shown;
  no such call occurred in these runs.
- P2-4 without a CM: first initialisation (decision above).
- Shared-IRQ invocation of a detached instance is simulated (`IsrProbe`), because 9x left
  the endpoint behind a bridge at 00:09 disabled (CONFIGMG problem 0x1D) on both OSes.
- Me typing through QMP loses keystrokes under host load. Test switches are now confirmed
  in the log (`tset`, `hostile.sh`), and lost runs were repeated (Me v2 -> v5 -> v8, S7/S8 -> v10).

## Interface 3 second re-review

Codex re-reviewed `qual-9x` at e95e93b. It found P1-1, P1-2, P1-3 and P2-8 fixed and the
rest not adoption-ready: a P1 in the CM's worker handoff, plus P2-4, P2-5, P2-7, the CM's
handling of late acknowledgements, and P2-9. Fix commits: 0e33818 (receiver, header,
CM), 0a7aca9 (decoder, recomputed ordering), 3ce3f52 (`BudgetSkewMs` test switch), and this record. Evidence:
[Q-9x/rerev2/](Q-9x/rerev2/).

### Changes that xhci98 builds against (for the maintainer)

| Rule | Change | Effect on xhci98's CM |
|---|---|---|
| R6f (new) | Worker handoff pattern: one state word per worker (IDLE -> SUBMITTED by the writer; SUBMITTED -> RUNNING by the worker with a locked compare-exchange; RUNNING -> DONE by the worker; DONE -> IDLE by the writer), plus an operation id. A result is accepted only for the id submitted. Separate busy and done flags are not allowed. An operation still running when its state is abandoned stays owned by its port. | Must follow this if it runs CONFIGMG calls on a worker, as the stand-in does |
| R11 O6 | "30 s" is an elapsed deadline from the first `Remove_SubTree` attempt, also while a call is still running. The running call stays the port's, and its result is taken before anything else is submitted. | Track the deadline by elapsed time, not by attempt count |
| R11 (late acknowledgements), R12 S4 | The CM checks its deadline before reading the acknowledgement. An acknowledgement first seen after T_ack (or after the link-down wait) is late and never changes the verdict, also when the CM itself ran late. | Order the check this way |
| R10 | nvme98-internal thresholds: decisions due by `ACK_BOUND - 2 * (POLL + LATE)` = 8500 ms (was 8750), disable requested at 5500 ms (was 5750). | None: the published 12000 ms (receiver) and 14000 ms (CM wait) bounds are unchanged, and now hold with a polled last check |
| R6e | The residual is narrowed to what was observed: the stand-in CM's threads stalled while the Me system-modal message was shown. | None |

### Internal choices (taken on the recommended option, recorded here)

- **P2-5:** chose thresholds that allow for both polling intervals (8500/5500 ms). The
  alternative, arming the timer at the transaction deadline, was not used: the 9x timer
  is periodic in this receiver, and the thresholds keep one code path.
- **P2-4:** the reference gets a check word and moves to a new value, `AcctRef` (the
  unchecked `Acct` of earlier builds is ignored). A second value, `AcctBoot` (REG_DWORD),
  records the proof cookie of the boot that last initialised accounting, and is written
  before `AcctRef`. With a CM proof present, the load fails closed on any of these:
  - a lookup error other than "not found";
  - a wrong type or size;
  - a check word that does not match (this includes a corrupted cookie);
  - a reference of this boot whose block does not check out;
  - no reference while `AcctBoot` names this boot.
  
  New accounting starts only from a valid reference of another boot, or, with no reference, from an
  absent marker or a well-formed marker naming another boot.
  - **Residual with a CM:** deleting both values within a boot cannot be told apart from a
    first installation. A registry-only record cannot detect its own complete removal.
  - **Residual without a CM:** unchanged (first decision).
- **P2-7:** O6 with a call still running reports E5FF ("still running") and keeps the
  call. Its success later retires the devnode as "removed after link down".
- **P2-9:** a transaction ends at the port's next statement or UP. The verdicts are:
  - INCOMPLETE: a needed event is missing (CM wait result, receipt, acknowledgement or
    removal).
  - FAIL: a receiver acknowledgement before a CM timeout, or events out of order.
  - TIMEOUT: requires a timeout event and a removal after it.

### Per finding

| Finding | Change | Qualified by |
|---|---|---|
| P1 worker handoff | R6f state word and operation id (`CS_IDLE/SUBMITTED/RUNNING/DONE`, `CallId`/`DoneId`). The worker moves SUBMITTED -> RUNNING with `lock cmpxchg`, so it cannot run an operation twice. A DONE result whose id, operation or devnode differs is discarded and logged (0xBB). | W1 on both OSes (below): every result taken was the one submitted, no 0xBB discard, two concurrent slow calls on two ports. The interleaving Codex modelled can no longer happen by construction: the writer never writes the state while the worker owns it. |
| P2-5 budget | 8500/5500 ms (R10). | S5D and S5E (below). |
| P2-4 accounting | As above; `CorruptAcct` 2 (reference truncated) and 3 (reference cookie damaged). | S8B: fails closed on both OSes for both variants. The earlier S8 (block damaged) is unchanged. |
| P2-7 O6 | Elapsed deadline (`O6At`); a running call is kept (`RmPhase` 1). `SlowRemoveMs` test switch (the worker delays each `Remove_SubTree`). | W1. |
| Late acknowledgements | `EjAckStep` and `LinkAckStep` check the deadline first; a late sighting is logged (0xBD, 0xBC). | By construction. No run produced a late sighting. |
| P2-9 decoder | Transaction boundaries, INCOMPLETE, a TIMEOUT that requires a removal. | Codex's three in-memory cases now give INCOMPLETE. All gate traces were recomputed ([Q-9x/rev/ordering.txt](Q-9x/rev/ordering.txt)): no FAIL, no INCOMPLETE. |
| R6e wording | Narrowed. | n/a |

### Results

Every run is a cold boot of the refreshed prep:

| OS | Run | Scenarios |
|---|---|---|
| 98 SE | w1 | W1, S5C, S8B (variant 2) |
| 98 SE | w2 | S8B (variant 3) |
| 98 SE | w3 | S5E |
| 98 SE | w6 | S5D |
| Me | w1 | W1, S5C |
| Me | w2 | S8B (variant 2) |
| Me | w3 | S5E |
| Me | w4 | S8B (variant 3) |
| Me | w6 | S5D |

Three runs were not counted. In w2 the S5D/S5E sections ran after S8B had already made
the boot fail closed. The first S5D design (w3, w5) depended on the timing of a held read,
and a DOS read loop vetoed the query, so it was replaced by the `BudgetSkewMs` switch
(3ce3f52).

| Scenario | 98 SE | Me |
|---|---|---|
| W1: clean eject of port 0 with `Remove_SubTree` taking 40 s (`SlowRemoveMs`); port 1 surprise 8 s after the acknowledgement | pass: port 1's `DOWN_QUIESCED` was published while port 0's call ran, and it was acknowledged and its own slow removal started. O6 came at 30.5 s elapsed with the call still running (E5FF, "kept for the retries", id 3). Then teardown, `DOWN_QUIESCED` and the link acknowledgement. Port 0's call returned after 40.0 s and its result was taken: "removed after link down"; port 1's likewise. No result was discarded | pass, same sequence (E5FF at 30.5 s, both calls returned after 40.0 s) |
| S5C: Flush held 7 s | outcome 2 after 2145 ms (Flush capped at 2 s, budget not reached) | outcome 2 after 2170 ms |
| S5C: Flush held 7 s, RDY never | FAILED after 7206 ms (step timeout before the budget) | FAILED after 7175 ms |
| S5D: first sight 4000 ms late (`BudgetSkewMs`), Flush held 7 s | "overall budget: 5539 ms elapsed in step 2: disabling now"; outcome 2 at 5546 ms counted from (simulated) first sight; E202 | 5533 ms, outcome 2, E202 |
| S5E: Flush held 7 s, SHST never, RDY never | "overall budget: 8526 ms elapsed, RDY still set: FAILED"; acknowledged after 8526 ms; E303 | 8531 ms, FAILED, E303 |
| S8B variant 2 (reference truncated) | "reference has the wrong type or size ... CM proof present ... fail closed"; `HwFindAdapter` fails before CC.EN | same |
| S8B variant 3 (reference cookie damaged) | "reference check word does not match (corrupted) ... fail closed" | same |
| Ordering (new decoder) | w1: PASS 4, NO-INSTANCE 1; w3: PASS 1; w6: no statement (corrected in the third re-review: an ISOLATED_NOT_CLEAN eject takes D2 without a link-down statement) | w1: PASS 4, NO-INSTANCE 1; w3: PASS 1; w4: PASS 1, NO-INSTANCE 1; w6: no statement (same correction) |

**Budget, measured.** With the decision thresholds at 8500 and 5500 ms, the measured
decision points were:
- the forced disable at 5533-5539 ms (S5D);
- FAILED at 8526-8531 ms (S5E).

Each decision came one 25-40 ms poll after its threshold. The 1500 ms lateness allowance
is still an assumption measured on Me (R10), not a proven maximum. The guest clocks again
ran about 3x slower than QEMU's virtual clock under host load: S5E took 26.4 s (98 SE) and
26.5 s (Me) of virtual time for 8.5 s of guest time. Both parties measure guest time.

**Not shown by these runs:**
- A late acknowledgement sighting (0xBC, 0xBD): the CM never ran late here.
- A real `CONFIGMG` call that blocks without a system-modal message. W1's slow call is
  simulated by the worker sleeping before a real `Remove_SubTree`.

## Interface 3 third re-review

Codex re-reviewed `qual-9x` at 06721f2. It found these fixed: the P1 worker handoff, P2-5
(conditional on the 1500 ms lateness allowance), the late-acknowledgement rule and R6e.
No P0 or P1 remain. Five P2 items were left, plus an evidence-table error. The fixes are
in commit 7f25071, plus this record. Evidence: [Q-9x/rerev3/](Q-9x/rerev3/).

**Header (`nvme98cm.h`):** one clarifying clause in R11 O6: "a call that completes after
the deadline counts for the O6 retries, never as a clean removal". This is a CM behaviour
xhci98 must follow. Nothing else in the header changed.

| # | Finding | Change | Qualified by |
|---|---|---|---|
| 1 | With `AcctRef` absent, an `AcctBoot` query error or malformed marker restarted accounting | With a CM proof present and `AcctRef` absent, accounting starts only if `AcctBoot` is also absent ("not found"), or is a well-formed REG_DWORD naming another boot (corrected in the fourth re-review: the third re-review said "only when both are not found"). A marker lookup error, wrong type or wrong size fails closed, and so does a marker naming this boot. New test switch `CorruptAcct` 4 deletes `AcctRef` (documented `_RegDeleteValue`, VMM 0x14F) and rewrites `AcctBoot` as 8 bytes of REG_BINARY. | S8B variant 4 on both OSes: "reference missing and the boot marker lookup failed ... fail closed", and `HwFindAdapter` fails before CC.EN. The marker query returns an error for the oversized value, so the "lookup failed" branch fired rather than the "malformed" one; both fail closed. The logged rc is the `AcctRef` lookup's (2, not found). |
| 2 | A removal that completes after the O6 deadline but before the writer's next poll could be taken as clean | The expired deadline wins. The worker records `DoneAt`, and a result is taken as this attempt's only if it completed before the deadline. A later one goes to the O6 retries. New test switch `WriterStallMs` stalls the writer once across the deadline. | W2 on both OSes: `SlowRemoveMs` 30100 and `WriterStallMs` 1000. The call completed during the stall, after the deadline. Result: O6, E5FF, "kept for the retries", then "removed after link down"; never C1EA. |
| 3 | The decoder's removal search started at the CM's result | Every removal inside the transaction is checked: a successful `Remove_SubTree` of the port, or the port's retirement. One before the CM's acknowledgement or timeout is FAIL. Old traces without port fields are matched by time after the CM's result only, and tagged "(by gen)". | Synthetic case: a `Remove_SubTree` before the CM's acknowledgement gives FAIL. |
| 4 | The bus mapping was the final one | The bus is snapshotted with each statement. | Synthetic case: a later UP on another bus does not change the verdict. |
| 5 | A legitimate late acknowledgement was reported as FAIL | When the CM logged 0xB6 0xBC between the receiver's acknowledgement and its own timeout, the verdict is TIMEOUT (late acknowledgement). An acknowledgement before the timeout without that log is still FAIL. | Synthetic cases for both. |
| - | Q-9x.md claimed PASS for w6 | w6 has no statement: an ISOLATED_NOT_CLEAN eject takes D2 without one. The table row is corrected. | Decoder rerun |

Ordering was recomputed over every gate trace with this decoder
([Q-9x/rev/ordering.txt](Q-9x/rev/ordering.txt)): no FAIL and no INCOMPLETE. In the W2
runs (x2), the one statement is TIMEOUT with a removal after it. When the CM published
it, the late Remove_SubTree had already removed the devnode, so no instance could
acknowledge it.

Runs, all on cold boots of the refreshed prep: 98 SE and Me x1 (S8B variant 4), x2 (W2).
The W2 section of x1 ran after S8B had already made that boot fail closed, so it was
repeated alone as x2.

## Interface 3 fourth re-review

Codex re-reviewed `qual-9x` at 97f0050 and found no P0 or P1. These are fixed: the
`AcctBoot` handling, the bus snapshot, accepting late acknowledgements, and the w6 row.
The header diff is the O6 clause only. Four P2 items remained, all in the cm9x stand-in or
the decoder; the nvme98 receiver needed no change. Fix commit: 73560cc, plus this record.
Evidence: [Q-9x/rerev4/](Q-9x/rerev4/). **`nvme98cm.h` is unchanged in this round.**

| # | Finding | Change | Qualified by |
|---|---|---|---|
| 1 | The O6 deadline was sampled before the result was taken, so a writer preempted in between could still report a late success as clean | Every `Remove_SubTree` result is checked against the deadline at the moment it is taken (`DoneAt` against `O6At`, logged as 0xB6 0xAE00\|port). One completed at or after the deadline goes to O6, and the retries apply it ("removed after link down"); never C1EA. New test switch `WriterStall2Ms` stalls the writer between the deadline check and taking the result. | W3 on both OSes, with `SlowRemoveMs` 30100 and `WriterStall2Ms` 1000. The result was taken with DoneAt 135 ms (98 SE) and 125 ms (Me) after O6At. Then: "completed after the O6 deadline", O6, E5FF, "late result, applied after O6", "removed after link down". |
| 2 | `early` was computed before the legacy removals were added | Early removals are computed over every candidate. An old trace's portless `Remove_SubTree` before the CM's result gives AMBIGUOUS. | Synthetic case |
| 3 | The legacy fallback credited a port with another port's removal | An old trace's portless `Remove_SubTree` is never attributed. If it is the only removal, the verdict is AMBIGUOUS. The debug-port text carries the TSC, not host time, so it cannot be aligned with the trace and is not parsed. | Synthetic case. In the recomputation, r3 and m3 statement #1 (port 1) are AMBIGUOUS; they were PASS. |
| 4 | Another port's late-acknowledgement marker suppressed FAIL | The CM's markers carry the port (0xB6 0xBC00\|port, 0xBD00\|port), and the decoder matches by port. An old portless 0xBC marker gives AMBIGUOUS. None occurs in any trace. | Synthetic cases: own port gives TIMEOUT (late); another port gives FAIL; portless gives AMBIGUOUS |
| - | Q-9x.md said accounting starts "only when both values are not found" | Corrected: with a CM proof and no `AcctRef`, accounting starts if `AcctBoot` is absent, or is a well-formed REG_DWORD naming another boot. A lookup error, malformed marker or marker naming this boot fails closed. | n/a |

Ordering was recomputed over every gate trace ([Q-9x/rev/ordering.txt](Q-9x/rev/ordering.txt)),
30 traces in all:

| Verdict | Count |
|---|---|
| PASS | 50 |
| TIMEOUT | 33 |
| NO-INSTANCE | 29 |
| NEGATIVE | 3 |
| AMBIGUOUS | 2 (r3 and m3, both old traces without port fields) |
| FAIL | 0 |
| INCOMPLETE | 0 |

This supersedes the earlier ordering counts in this document.

Runs: 98 SE and Me y1 (W3), cold boots of the refreshed prep.

## Reproduction

Build: `set NVME98_Q9X=1&& scripts\build.cmd 9x` (copy `out\free\win9x\nvme98.mpd` to
`out\q9x\NVQ9X.MPD`), `tests\cm9x\build.cmd`. Local scripts in `E:\nvme98-vm\9x` (not
committed): `mkxfer.sh` (transfer disk with `CM9X.SYS`, `CTL.EXE`, `NVQ9X.MPD`, `PREP.BAT`,
`Q9.BAT`), `run.sh 98|me <tag> new`, `g.sh 98|me dos|t|k|tun|qom|s|off|restart`, `g1.sh`,
`bench.sh`; prep once per OS with `run.sh <os> prep0 prep` and `D:\PREP.BAT`, then a restart.
Decode with `python tests/cm9x/q9log.py <trace>` and `tests/cm9x/ejects.py <trace>...`. Runs:
98 SE a1, a2, b1, b2, b3, b4, b5; Me m1, m2 (`NOTES.md` has the journal).
