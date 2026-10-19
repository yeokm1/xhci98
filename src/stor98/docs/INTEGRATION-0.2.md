<!-- SPDX-License-Identifier: BSD-3-Clause -->
<!-- Copyright (c) 2026, Yeo Kheng Meng -->

# Integration 0.2 (trial, branch `int-0.2`)

An early trial of the 0.2.0.0 integration: `main` (the 0.2.0.0 release layout), then
`engine`, then `qual-9x`, then a few integration commits on top. Not merged to `main`, not
released. When `engine` or `qual-9x` gets more fixes the two merges are redone at the new
heads and the integration commits are replayed on top; they are kept separate for that.

| Step | Commit | What |
|---|---|---|
| merge | `523c809` | `engine` at `b01fc59` (cleared by Codex: round 5 plus D3 write-through on Me, the Me gone-deadline fix, and the two hooks taken from here as `80fe1ee` and `63b6aac`) |
| merge | `177f604` | `qual-9x` at `cf56ccd` (interface 3 cleared by Codex, adoption-ready; its `nvme98cm.h` and `cm9x`); conflicts below |
| own | `a9a552a` | stage codes: the 9x stand-ins moved above the engine's (`q9log.py` of `cf56ccd` renumbered the same way) |
| own | `e626aea` | the 9x MD-4 pool provider (`src/pool9x.c`, `NVME98_DMA_POOL` on 9x) |
| own | `d420eff` | the 9x interface 3 link-state receiver (evidence 2) |
| own | `414c500` | the receiver's side of the idle-poll hook (`PlatCmLinkPollUs`; the engine side is `63b6aac`) |
| own | `27c3855` | this note |
| own | `70d785c` | the 9x removal transaction (REMOVE_PENDING, RemoveAck; MD-5) |
| own | `330402d` | this note: the removal transaction and its first tests |
| own | `b697254` | removal transaction: fixes for the Codex review of `9732ca8` (P1-1 to P2-6) |
| own | `fe12163` | cm9x `Overlap 3` (identity change during an open request), on the `cf56ccd` cm9x |
| own | `37bc81f` | test option `ShnPrime` |
| own | `c969b25` | the RemoveAck stage marks an outcome that was not published |
| own | `3818b6c` | this note: rebuild on engine `b01fc59` and the review fixes |
| own | `b02126d` | interface 3 as cleared (`cf56ccd`): AcctRef/AcctBoot accounting, R10 thresholds 8500/5500 ms |

Earlier states are kept as branches: `int-0.2-try1` (engine `c8504c6`, qual-9x `f40f6d1`),
`int-0.2-try2` (engine `105064a`, qual-9x `f40f6d1`), `int-0.2-try3` (engine `105064a`,
qual-9x `a79d154`: the state Codex reviewed at `9732ca8`, plus the first fix round) and
`int-0.2-try4` (engine `b01fc59`, qual-9x `a79d154`, the review fixes). `int-0.2` is
rebuilt from `main` each time with the merges redone and the own commits replayed. The
int-only copies of the engine hooks were dropped when `engine` took them (`81d845a`
entirely, the engine side of `de9fa23`); the three removal hooks in `lifecycle.c` merged
without conflict; the engine's `PlatCmLinkPollUs` stub sits under the receiver's `#if`
guard. On `cf56ccd` the `q9log.py` and `cm9x.c` conflicts were resolved by taking the
qual-9x versions and redoing the renumbering and `Overlap 3` on them (`Overlap 3` now
logs diagnostics 0xF8/0xF9, since `cf56ccd` uses 0xBC00/0xBD00 for late
acknowledgements).

## Conflicts and resolutions

- **INFs.** No conflict: neither branch edits `inf\`. `main`'s `inf\nvme98-x86.inf`
  (`$CHICAGO$`, 9x and NT x86) and `inf\nvme98-x64.inf` stand as they are.
- **`scripts\build.cmd`.** No conflict: only `engine` changed it (the `NVME98_DBGPORT`
  output folder).
- **`src\sources`.** Conflict in `SOURCES`: the engine's list (`engine.c`, `lifecycle.c`,
  `clock.c`, `platform.c`, `dbgport.c`, `sense.c`) is kept; `qual-9x`'s `g9dbg.c` and
  `q9x.c` are not built. `qual-9x`'s `NVME98_G9` levels and `NVME98_Q9X=1` are replaced by
  an `!ERROR` if either is set: both are prototype layers hooked into the pre-engine request
  path.
- **`src\nvme2k.c`, `nvme2k.h`, `nvme2k_cpl.c`, `nvme2k_nvme.c`.** Conflicts everywhere
  `qual-9x` hooked the G9 instrumentation and the Q-9x layer (`Q9FindAdapter`,
  `Q9PoolInstall`, `Q9HwInitialize`, `Q9StartIo`, the queue gate in the completion paths,
  `Q9BuildBounce`, `Q9AdapterStop`, the `Q9*`/`G9*` device-extension fields) into the
  inherited request path, which the engine replaced (its own deferred bring-up, hold, gone
  state, MD-1 deadline, truthful reset, stop and queue gate). Resolved by taking the engine
  side of all four files. What the 9x product needs from `qual-9x` comes in through the
  engine's platform hooks instead (below). `src\q9x.c`, `q9x.h`, `g9dbg.c`, `g9dbg.h` and
  `tests\cm9x\` stay in the tree, unchanged except for the stage codes, so later re-merges
  of `qual-9x` stay mechanical. `src\utils.h` auto-merged (the `NVME98_G9` debug-print
  redirect, inert without `NVME98_G9`).
- **`include\nvme98cm.h`, docs.** Auto-merged: interface 3 from `qual-9x`; the `engine`
  and `qual-9x` additions to `docs\PLAN.md` and `docs\TESTING.md` do not overlap.

## Stage-code map

The usb4-emu debug port takes 32-bit codes (`usb4_dbg_stage code=0x%x`, harness rules
`code=` up to `UINT32_MAX`), so the test-only stand-ins moved up by 0x100 rather than into
the few free byte values. Codes are never reused.

| Codes | Owner | Before `int-0.2` |
|---|---|---|
| 0x01-0x7F, 0x81-0x8D, 0xA1-0xB4 | shared layer (engine, `src\dbgport.h`) | same |
| 0xB5-0xBF, 0xC7-0xDC | request engine (`src\dbgport.h`) | same |
| 0xC0-0xC6 | NT CM stand-in `cmstub.sys` (`phase0-g3g6`) | same |
| 0xE0-0xF9 | Q-9x layer (`src\q9x.h`, not built here) | same |
| 0xFA-0xFD | `cm9xctl bench` | same |
| 0x120-0x129 | 9x pool provider and link-state receiver (`src\pool9x.h`) | new |
| 0x1B5-0x1B6 | `cm9x.sys`: teardown port 1, diagnostics | 0xB5-0xB6 |
| 0x1C7-0x1CF | `cm9x.sys`: error ... teardown port 0 | 0xC7-0xCF |
| 0x1D0-0x1DF | G9 instrumentation (`src\g9dbg.h`, not built here) | 0xD0-0xDF |
| free | 0xDD-0xDF, 0xFE-0xFF | |

Consequences: harness rules for the CM teardown are `at=stage code=0x1cf act=surprise`
(port 0) and `code=0x1b5` (port 1); `tests\cm9x\q9log.py` decodes the new values and
accepts the receiver's receipt and acknowledgement as either 0xE9/0xEA (q9x) or
0x124/0x125 (pool9x); the debugcon lines (`n98 <TSC> <code> <arg>`, `cm9x ...`) print
three hex digits for codes above 0xFF. Traces made before `int-0.2` keep the old values.

## Pool wiring

`src\pool9x.c` implements `PlatDmaPoolProvider` (`src\platform.h`) on the nvme98 boot
block of interface 3 (R7), following the Q-9x prototype: one 2-page `_PageAllocate` block
per boot, found again through `HKLM\Software\nvme98\Pool` `Acct` and accepted only if its
pages are present, map the recorded physical pages, carry the signature and cookie and
start a 2-page VMM allocation; instance tokens with a per-function binding (a new
`HwFindAdapter` of a function ends its previous instance, R8c); eight records; a
boot-wide budget (6 blocks of 23 pages; `NVME98_POOL_CAP_BLOCKS` in test builds). Its key
and signature differ from the prototype's, so prototype and product records never mix. The
9x build sets `NVME98_DMA_POOL`; NT is unchanged (MD-3 (a)).

Each adapter gets one block: the engine's region (`UNCACHED_EXTENSION_SIZE`, 22 pages)
plus one metadata page, contiguous, fixed, below 4 GiB. The engine carves its seven
allocation records out of it as before.

| Hook | Behaviour |
|---|---|
| Acquire (HwFindAdapter, after the DMA negotiation, before any CC write) | Reads the CM proof, gets the boot block (fail closed if it is this boot's but no longer checks out), runs the rendezvous (needed for E1, and the link-state channel), takes a token, settles records whose owner is gone, then claims a **FREE** record (fresh zeroed block, budget charged) or a **RELEASED** one that passes validation (fields, PTEs, metadata). It never takes a record that a previous load may still reach, and writes only the metadata page. The record is left **RESERVED**. No boot block, lock, token, record or budget: the find fails before CC.EN; there is no fallback to the uncached extension. |
| PagePa | The page's own PTE (`_CopyPageTable`), which must be the page the contiguous record names; the metadata page is never translated. |
| Note | The engine notes per carved block; the record follows the most exposed block: any block **OWNED** -> record OWNED (from RESERVED or ISOLATED); no block OWNED and one **ISOLATED** -> ISOLATED; every block relinquished -> **QUARANTINED** if any block is QUARANTINED, else **RELEASED**, then the instance ends. Relinquish comes only from `PlatDmaRelinquish` at the end of `EngStop` (round-3 N2), so RELEASED is never published while a block is held. |
| HoldsPastRemoval | TRUE: the boot block outlives the `.mpd`. |

Records whose owner is gone (found by the next load of any function): OWNED ->
QUARANTINED; ISOLATED or RESERVED (never reachable) -> RELEASED; blocks that failed
validation stay out for the boot. QUARANTINED -> RELEASED only on evidence: **E1**, this
load's CM table shows a later generation or DOWN_QUIESCED for the record's generation on
its entry; **E2**, the record is of this load's function and this load's controller has
been shown disabled, which the first OWNED note marks (`LcEnable` runs only after this
load's own acknowledged disable or on a controller found with CC.EN=0 and RDY=0).

The receiver (`PlatCmLinkPoll`/`Acknowledge`, 9x with the pool only) reads the bound entry
with the R6c snapshot and the R5e/R8d checks and reports DOWN_QUIESCED for the bound or a
later generation (or a later generation alone, R8b) once; the acknowledgement writes
`AckGeneration` after the engine applied it, only for DOWN_QUIESCED. `PlatCmLinkPollUs`
makes the engine keep a 250 ms timer while a channel is bound (R10); without it an idle
adapter missed the CM's 3 s wait. The MD-5 removal transaction (REMOVE_PENDING, RemoveAck)
is described below ("Removal transaction").

This closes the round-2 P1-1 blocker (9x DMA quarantine held only until the `.mpd`
unload) for the 9x build: quarantined memory now stays owned for the boot.

## Smoke test (trial, 2026-10-15)

Private QEMU copy `E:\nvme98-vm\int\bin` (from `E:\nvme98-vm\9x\bin`), overlays under
`E:\nvme98-vm\int`, QMP 48800 (98 SE), 48810 (Me), 48820 (2000). 9x images: copies of
the Q-9x prep overlays with the `int-0.2` `cm9x.sys` and the debug-port build of
`nvme98.mpd` (stage codes in the debugcon log); script `smoke9x.sh` there. Run on the
rebuilt branch (`9dec03d`); the first trial (`int-0.2-try1`) gave the same results except
where noted.

| OS | Case | Result |
|---|---|---|
| 98 SE, Me | bring-up, 2 MiB write + verify (`cm9xctl`) | pass; record 0 FREE -> RESERVED (fresh block, 23 pages, budget 23) -> OWNED at `LcEnable` |
| 98 SE, Me | surprise removal, cm9x DOWN_QUIESCED | pass: GONE, statement seen and applied (evidence 2), record OWNED -> ISOLATED, `AckGeneration` written, CM acknowledged in 0.1-0.3 s. 98 SE: `HwAdapterState(FALSE)` -> RELEASED. Me (no stop callback, MD-5): the next load finds the owner gone, ISOLATED -> RELEASED. In `try1`, before the idle timer commit, an idle adapter missed the CM's 3 s wait and the block went QUARANTINED |
| 98 SE, Me | re-arrival | pass: the RELEASED block validated and reused (metadata loads 1, 2, 3), data verified |
| 98 SE, Me | surprise without QUIESCED (cm9x `NoFlag`) | pass: no evidence, 98 SE stop -> QUARANTINED (Me: OWNED, owner gone -> QUARANTINED at the next load); re-arrival: E1 (later generation on the entry) -> RELEASED and reused, data verified |
| 98 SE, Me | eject (`cm9xctl eject`) | as expected without MD-5 in the engine: no `RemoveAck`, CM outcome E3FF, CM isolates the endpoint, then the surprise path above (ISOLATED -> RELEASED) |
| 98 SE (`try1`) | orderly stop at shutdown, controller present | Flush not within the stop budget (MD-7), SHST complete, RDY=0 acknowledged: OWNED -> ISOLATED -> RELEASED |
| 2000 | `eio` 128 MiB, qd 16, 3 passes (debug-port build) | PASS, 9552 requests, 0 problems (also 32 MiB qd 8 on `try1`) |

Observations for the owners (not changed here):

- Windows Me: after a surprise removal, VFAT's lazy write of data committed a minute earlier
  fails and Me shows the "Disk Write Error" screen; the CM's `Query_Remove_SubTree` blocks
  until a key is pressed. Write-through on Me (D3, MD-5) is not in the engine yet.
- Me: the engine logged the MD-1 deadline (0x83, no SRB completed) about 11 s after
  evidence 2 had already been applied for that gone episode; TESTING expects the deadline
  cancelled and the counter unchanged when evidence arrives first.
- D3 (write-through on Me) is now in the engine (`b01fc59`).

## Removal transaction (MD-5, interface 3 R9-R11)

In `src\pool9x.c` (the 9x receiver), ported from the Q-9x prototype (`q9x.c` `Q9Rm*`)
onto the engine, with three engine hooks (`platform.h`; stubs on NT):
`PlatCmRemoveTimer` at the top of the engine's timer step, `PlatCmRemoveStartIo` at the
top of `EngStartIo`, `PlatCmRemoveActive` in `EngResetBus`.

- **Acceptance (R9b).** The first request after binding whose RemoveSequence differs from
  the one at binding and whose snapshot shows the instance's own generation, seen from the
  timer hook (every state, at least every 250 ms) or HwStartIo. One per instance; a CM
  retry is the same request and changes nothing; a later RemoveSequence is an overlap,
  logged (0x12E) and ignored. Accepting makes the instance terminal (R9d).
- **Steps** (timer hook at 10 ms, VMM elapsed time): from `running`, drain (admitted
  requests finish, 2 s), an ordered Flush through the engine's internal stop Flush (2 s;
  counts only if it succeeded and covers every write), CC.SHN and SHST (2 s), CC.EN=0 and
  RDY=0 (min(CAP.TO, 5 s)). From a restartable `shutdown` its result counts and the disable
  follows; from `failed` (already disabled) it is isolated at once; from `starting`,
  `resetting` or `quarantined`, disable only; from `gone`, FAILED. The overall budget (R10)
  starts the disable by 5750 ms whatever step runs and makes an unacknowledged disable
  FAILED by 8750 ms.
- **Isolated (RDY=0 acknowledged).** Children settled with isolation evidence (blocks
  ISOLATED), held requests completed NO_DEVICE, the engine in `failed`, the blocks
  relinquished (RELEASED, the instance ends), and only then RemoveAck = seq << 8 | outcome
  by one interlocked store (R6d, R9c): **CLEAN (1)** with drain, Flush, SHST and RDY=0,
  otherwise **ISOLATED_NOT_CLEAN (2)**.
- **FAILED (3).** No acknowledged disable, or presence lost: requests stay held, blocks stay
  OWNED; the instance waits for presence loss (the CM isolates first, R11 O5), then hands
  over to the engine's `gone` state (evidence 2 or the MD-1 deadline).
- **Requests.** BUSY from acceptance to the acknowledgement, NO_DEVICE after it;
  SRB_FUNCTION_SHUTDOWN completes with success; HwResetBus returns FALSE and does nothing.
- Stage codes 0x12A (request seen), 0x12B (step), 0x12C (RemoveAck: outcome, sequence,
  detail bits drain 01, Flush 02, SHST 04, RDY=0 08, budget 10, gone 20, from state 80),
  0x12D (refused after the acknowledgement), 0x12E (overlap).

### Removal tests (2026-10-16, `638c460`, 98 SE and Me, cm9x of `a79d154`)

Script `E:\nvme98-vm\int\rm9x.sh` (local). Same results on both systems.

| Case | Result |
|---|---|
| Clean eject right after writing 1 MiB | **C1EA**, RemoveAck outcome 1, detail 0F (drain, Flush, SHST, RDY=0), 155 ms (98 SE) and 183 ms (Me) after the CM's request; record OWNED -> ISOLATED -> RELEASED before the acknowledgement; Remove_SubTree; re-arrival reuses the block; both files verify |
| Veto (a file held open) | Query_Remove_SubTree vetoed (E117), no request published; the disk keeps working (verify OK) |
| Retry (cm9x `Overlap 2`: the same request re-published after the acknowledgement) | C1EA; RemoveAck unchanged (cm9x check: 0201 before and after) |
| Overlap (cm9x `Overlap 1`: a second sequence 300 ms later) | C1EA; RemoveAck names the first request (0301); the second came after the acknowledgement |
| FAILED (RDY never clears after CC.EN=0) | outcome 3 at the 5 s disable cap (detail 07); the CM **isolates first** (E303): teardown, DOWN_QUIESCED applied and acknowledged (decoder PASS), record ISOLATED -> RELEASED; re-arrival verifies |
| CM timeout (AckTimeoutMs 300, SHST delayed 1.5 s) | the CM gives up (E3FF) and isolates first; nvme98 sees presence lost in the SHST wait and writes FAILED late (logged by the CM, verdict unchanged); DOWN_QUIESCED applied (decoder PASS), RELEASED; re-arrival verifies |

## Review of `9732ca8` (Codex) and the fixes (`8877684` and after)

| Finding | Fix |
|---|---|
| P1-1 the shutdown shortcut credited a SHUTDOWN whose SHST timed out (`SHN_DONE` is set either way) | The engine keeps this shutdown episode's explicit results: `ShnFlushOk` (its Flush succeeded and covered every write) and `ShnShstOk` (SHST complete seen), cleared at re-enable and at every start. The transaction credits drain and SHST only with `ShnShstOk` and SHST complete in CSTS at its decision, and Flush only with `ShnFlushOk` and no write since; otherwise ISOLATED_NOT_CLEAN after the disable. |
| P1-2 the ISR kept consuming and dispatching after the drain, and after FAILED | New hook `PlatCmRemoveGate`, checked in `EngDispatchEx` and `EngInterrupt`: during the drain admitted work may finish; from the Flush on nothing is dispatched (undispatched work stays frozen and completes NO_DEVICE at isolation); from CC.SHN on, and in FAILED until presence is lost, the ISR consumes nothing, so requests stay held until isolation. |
| P1-3 (hypothesis) a claim was published before its owner identity, so an overlapping find of another function could take the claimant for gone | A claim now carries the full owner identity (token and function) from its first publication under the boot-block lock (`P9Claim`), and installation rechecks ownership under the lock (`P9TakeLocked`). Whether the 9x ports can overlap two functions' `HwFindAdapter` was not established; the fix does not depend on it. |
| P2-4 a budget-forced disable underflowed `now - RmStepMs` and failed at once | `now` is refreshed after the forced step change, as `q9x.c` did. |
| P2-5 invalid entry observations did not invalidate an open transaction | One validating read (`P9EntryRead`: R6c, R5e, R8d) shared by the link-state poll and the transaction; identity loss (or another generation) during a transaction suppresses the acknowledgement. The transaction still isolates the controller. |
| P2-6 pre-acknowledgement requests could get ERROR at the engine's BUSY limit | The transaction completes them BUSY itself, without a limit (R9d). |
| N2 contract | Relinquish happens at the end of `EngStop` or at a removal transaction's acknowledged disable, after the queue gates close (accepted). `platform.h`, `platform.c`, `pool9x.c` and PLAN updated. |

Internal choices taken while the maintainer was away (recommended options, logged here):

1. **Shutdown results live in the engine** (two device-extension flags set in
   `EngShutdownFlushed` and `LcShnStep`, cleared at re-enable and start) rather than being
   inferred in `pool9x.c`: only the engine sees the SHUTDOWN Flush status and the SHST
   outcome. For the `engine` owner to adopt with the hooks.
2. **Freeze, not cancel**, undispatched work after the drain: it never reaches the
   controller, and the isolation completes it NO_DEVICE (CLEAN or ISOLATED_NOT_CLEAN) or
   keeps it held (FAILED), which is what R9d says.
3. **The ISR claims nothing** past the drain (returns FALSE): this controller's vector is
   masked from CC.SHN on, so only another device on a shared line calls it then.
4. **Identity loss mid-transaction:** the controller is still isolated and the blocks
   released, but nothing is published; the CM's wait ends and it isolates first.
5. **ShnPrime test option** (debug-port builds): 98 SE never sends SRB_FUNCTION_SHUTDOWN,
   Me only at system shutdown, and a level-0 volume lock sends neither, so the
   shutdown-then-remove test writes CC.SHN and sets the engine's episode flags as
   `LcShnStep` leaves them (1: SHST timed out, 2: complete); the decision then runs on the
   real controller state.
6. **Test options from `HKLM\Software\nvme98\Q9x`** in debug-port builds only (9x has no
   ArgumentString): `ShnPrime`, `IsrProbe`, `Frag`, `ShnFlush`.

## Interface 3 as cleared (`cf56ccd`) in the product receiver (`b02126d`)

| Rule | In `pool9x.c` |
|---|---|
| R10 thresholds | every decision due by ACK_BOUND - 2 * (POLL + LATE) = 8500 ms from first sight; the disable requested from the first check at or after 5500 ms (was 8750/5750) |
| AcctRef/AcctBoot accounting (R7, Q-9x) | nvme98's own boot block now uses `AcctRef` (REG_BINARY with a check word) and `AcctBoot` (REG_DWORD, written before the reference) under `HKLM\Software\nvme98\Pool`. With the CM's proof present a load fails closed on a lookup error other than not-found, a wrong type or size, a check-word mismatch, this boot's reference whose block does not check out, or a missing reference while `AcctBoot` names this boot or cannot be read cleanly; without a CM an unusable reference still means a new boot (the residual Q-9x documents) |
| TableLinear (R3a, R5b) | unchanged: the proof's TableLinear is the address authority and must equal the locator |
| R6f worker handoff, O6 elapsed deadline (a late call is never clean), late acknowledgements | CM-side (cm9x of `cf56ccd`). The receiver writes RemoveAck once (R9c) and never rewrites it; nothing in it depends on the CM's worker or deadlines |
| Test switches | `BudgetSkewMs` and `CorruptAcct` 1-4 in debug-port builds, as `q9x.c` |

## Handover (2026-10-16): state of the requalification when the work moved to stor98

0.2.0.0 is not cut from this branch; the work moves to the stor98 repository (renamed),
to be requalified there. Branch `int-0.2` is the source: engine `b01fc59`, qual-9x
`cf56ccd` (interface 3 adopted, D4), the 9x pool provider, receiver and removal
transaction with the review fixes and the interface 3 accounting. Never pushed.

### Requalification on the final code (`b02126d`/`5249604`; 98 SE and Me, cm9x of `cf56ccd`)

| Set | 98 SE | Me | Notes |
|---|---|---|---|
| A clean eject after a write | pass (C1EA, outcome 1, detail 0F) | pass | data verified after re-arrival |
| B veto | pass (E117, disk keeps working) | pass | |
| C1 retry / C2 overlap | pass (C1EA, RemoveAck unchanged / first request) | pass | |
| D FAILED (RDY never clears) | pass (outcome 3, E303, CM isolates first) | pass | |
| E CM timeout | pass (E3FF; FAILED written late, verdict unchanged) | pass | decoder: PASS for the link-down statements of D and E |
| F shutdown episode then remove (`ShnPrime`) | partial | partial | 1 (SHST timed out) gave outcome 2 without SHST credit on the `try4` round; on the final round both cases ran with the wrong option loaded (the D2 re-plug read the registry before the script changed it) and with the eject's own writes outside the episode's Flush; `5249604` makes the prime run the episode's Flush and `rm9x5.sh` orders the option changes; **rerun F** |
| G shared-line ISR during and after the drain | pass: drain timed out with a read held, then SHN and disable; no child submitted after the drain; the probe ISR returned FALSE in steps 4 and 5 | pass | the read was held at fetch; no fragment was waiting for dispatch, so the dispatch gate was not challenged |
| H budget-forced disable | not reached (the drain timed out, so no Flush step: 4.5 s < 5.5 s); outcome 2 with RDY=0 | same | the budget path is covered by S (`BudgetSkewMs`) below |
| S `BudgetSkewMs` 5600 | pass: outcome 2, detail 18 (budget, RDY=0), not FAILED (P2-4) | pass | |
| I identity loss mid-transaction | pass: 0x126 reason 20, outcome decided but not published (0x12C bit 7), CM E3FF isolates first | pass | |
| J BUSY without a limit | final round: no I/O landed in the window (0 BUSY); `try3`/`try4` rounds of the same code: 138 and 144 BUSY, one SRB 96 times, no BUSY-limit error | same | rerun J with a longer transaction |
| K overlap during an open transaction | pass: 0x12E logged, the first request acknowledged (C1EA) | pass | |
| C3 accounting (`CorruptAcct` 3) | pass: next loads fail closed (0x120 reason 36, check word) | pass | the cold-restart recovery step was not reached (stopped for the hand-over) |
| C4, C1 accounting | **not run** | **not run** | |
| Windows 2000 `eio` | pass on `try1`/`try2` builds (9552 requests, 0 problems) | | **not rerun** on the final code |

Decoder (`tests/cm9x/q9log.py`) verdicts and what they mean here:

- **NO-INSTANCE** for every clean eject (and K): expected. After CLEAN the CM removes the
  devnode first and only then tears the path down and publishes DOWN_QUIESCED (R11 O5), so
  no instance remains to acknowledge it.
- **TIMEOUT** for the I statement: expected. The instance lost its identity (R8d) and so
  acknowledges nothing on that entry, the link-down included; the CM's wait ends and it
  removes the devnode.

### The f4 accounting stall

`rmacct.sh` (f4) stalled on both systems after the `BudgetSkewMs` eject: that eject ended
E202 (ISOLATED_NOT_CLEAN), whose D2 recovery re-enumerates the device by itself; the
re-plug and bring-up (0x131, 0x120, 0x43) had already happened when the script's `up` took
a new log mark and waited for a bring-up after it, which never came. `wl.sh` counted
iterations, not time, so under the host load its 240-iteration bound ran far longer. Fixed
in `rmacct2.sh` (`back` waits from the eject's own mark and raises the tunnel only after a
torn-down eject) and in `wl.sh` (wall-clock bound); the cold restarts there shut the guest
down and quit QEMU (no warm restart of 9x).

### Still unrun

F (rerun with `rm9x5.sh`), J (with I/O in the window), C4 and C1 accounting and the
cold-restart recovery after each, H as a true budget path (it needs a drain that finishes
and a Flush and SHST that both run long), and `eio` on Windows 2000 with the final build.

### Running the harness

Scripts are in `tests/int9x/` (copies of `E:\nvme98-vm\int\*.sh`, local paths inside):

- `E:\nvme98-vm\int\bin`: private QEMU (from `E:\nvme98-vm\9x\bin`); DLLs from
  usb4-emu's `toolchain\msys64\ucrt64\bin`. VMs open an SDL window (maintainer rule).
- `98\base.qcow2`, `me\base.qcow2`: copies of the Q-9x prep overlays (backed read-only by
  usb4-emu `vms\win98-g9`, `vms\winme-g9`); `prep.sh <os> NVINTD` installs the current
  `cm9x.sys`, `CTL.EXE` and the debug-port `.mpd` into them (needed after a cm9x change).
  Each run makes `sys-<tag>.qcow2` and `nvme-<tag>.qcow2` overlays.
- `mkxfer.sh`: builds `xfer98.img` / `xferme.img` (D: in the guest) from `out\` of this
  worktree (`NVINTD.MPD` debug-port, `NVINT.MPD` release, `CM9X.SYS`, `CTL.EXE`, `INT.BAT`).
  Run only while no VM uses them.
- Ports: 98 SE QMP 48800 (VNC 48801), Me 48810, Windows 2000 48820 (`runw2k.sh`; its
  keyboard is Dvorak: drive it with `E:\nvme98-vm\engine\vm.py`).
- Sets: `rm9x.sh <os> <tag>` (A-E), `rm9x4.sh` (F-H), `rm9x5.sh` (F, ordered),
  `SECTIONS='I J K' rm9x2.sh` (I-K), `rmacct2.sh <os> <tag> "3 4 1"` (accounting;
  `SKEW=1` adds S). Results go to `logs\`; guest logs are `<os>\debugcon-<tag>.log`
  (the debug-port text lines) and `<os>\trace-<tag>.log` (QEMU trace, for `q9log.py`).
- The scripts grep the debugcon lines by their prefixes: `n98 <TSC> <code> <arg>` (the
  driver's debug port, `src\dbgport.c`), `cm9x <TSC> <code> ...` (the CM stand-in) and
  `ctl ...` (`cm9xctl`). **A rename (stor98) that changes these prefixes, the `nvme98`
  registry keys (`Software\nvme98\Pool`, `Software\nvme98\Q9x`, the locator under
  `Services\nvme98`) or the image names (`NVME98.MPD`) must change the harness the same
  way.** Stage codes: `src\pool9x.h` (0x120-0x131), `src\dbgport.h`, the table in
  docs/TESTING.md.
- Never start more than two VMs; never touch QEMU processes the harness did not start.
