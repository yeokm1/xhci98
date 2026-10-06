# Selective suspend for the HCD

Design record for roadmap-hcd's planned selective suspend work (tasks
`SUSP.*`, `SUSPa.*`, `SUSPb.*`; drawn up as Phase 36, unnumbered since
2026-10-06 until the owner opens it, and its review history keeps the old
phase numbers). DRAFT, revision 10,
2026-10-05: written at the owner's request to solve the known limitation "the
driver never initiates selective suspend" (roadmap-hcd decisions table, row
"The idle power policy (28.3)"; release notes, "Known limitations"). Codex
reviewed revisions 1 to 9 the same day (13, 12, 10, 9, 3, 4, 1, 2 and 1 findings, all taken;
section 9 maps each to where it is answered). Not yet a roadmap phase;
nothing is built.

## 1. What is asked, and what is not

The bus suspends a device when Windows asks for it - a function's D-state
request in S0, or the idle-notification protocol on XP onward - and wakes it
when Windows asks for D0 or the device signals remote wake. It never suspends
on a timer of its own.

Not in this design: hub idle (a hub suspended because every child is),
controller D3 when the root hub idles (issue 05: an idled controller hid
hot-plug on 98), U1/U2, and system wake from S3. Each device's ancestors stay
active and monitored.

The work is split in two stages, because SuperSpeed wake is a different
mechanism (section 8). Both ship in one release (owner, 2026-10-05); stage A
is designed and built first.

- **Stage A (SUSPa): USB 2.0 devices** - High, Full and Low Speed, on a root
  port or behind a USB 2.0 hub (a USB 3 hub's USB 2.0 half included).
- **Stage B (SUSPb): SuperSpeed devices** - Function Suspend, Function Wake
  Device Notifications, SS hub ports. Until SUSPb is built, a SuperSpeed
  device's PDO reports no D1/D2 and no wake, and its Dx is recorded only
  (today's behaviour).

## 2. State today (read 2026-10-05)

- `hcd_urb.c:866-956`: the idle notification is held, its callback never
  called; cancel and drain complete it `STATUS_CANCELLED`. `IdlePending`
  covers completion, not a callback (there is none).
- `hcd_pdo.c:2632` `HcdDevicePdoPower`: SET_POWER records the D-state and
  calls `PoSetPowerState`; WAIT_WAKE gets `STATUS_NOT_SUPPORTED`. The
  capabilities (`hcd_pdo.c:2416`, `2449-2454`) report `DeviceD1` and
  `DeviceD2` FALSE and `DeviceWake`/`SystemWake` unspecified. A class driver
  can still send D3 or an idle notification; neither reaches the link.
- Root port builders `XhciPortscSuspend`/`ResumeSignal`/`ResumeDone`
  (`xhci_port.c:137-175`) are host-tested and unused. `xhci_link.c:118`
  classifies PLS=Resume on a USB3 port as unknown, and `WANT_RESUME`
  (line 217) accepts only U3.
- Root PLC is acknowledged with the other change bits (`hcd_enum.c:2209`)
  and then nothing services a wake; a wake is not taken as a disconnect
  (`xhci_link.c:264` disconnects on CSC, lost CCS or SS errors only).
- The hub port look resumes any port it finds suspended (`hcd_hub.c:827`),
  and `hcdHubPortResume` does its own subtree pause and release and uses
  SP=0 stops (`hcd_hub.c:463`, `595`).
- Transfer mapping and doorbells are published under the controller lock
  (`hcd_io.c:1156`, `1208`).
- DNCTRL enables no notification (`xhci_init.c:385`); Device Notification
  events fall through (`xhci_evt.c:261`).
- The thread holds `PowerGate` across `HcdEnumService` (`hcd_ctl.c:492`).

## 3. The device's link machine (pure core, `xhci_susp.c`)

Three separate states, never conflated: the device's **link** state (this
section, one per USB device), each function PDO's **power** state (section
4), and the device's **wake episode** (section 5). A function PDO can be D2
while the link is ACTIVE because a sibling is D0.

Link states, owned and written only by the enum thread, read under the
controller lock, host suite `test_susp`: `ACTIVE -> SUSPENDING -> SUSPENDED
-> RESUMING -> ACTIVE`, plus `RECOVERING` (handed to the existing recovery)
and `GONE`. The machine owns the device's port in every state but ACTIVE,
RECOVERING included, until the recovery re-establishes the device (ACTIVE)
or invalidates it (GONE).

### 3.1 Level-triggered requests

No request object is queued to the thread. A PDO writes only its own slot
(section 4) under the controller lock and sets `hc->PowerDirty`, then
`HcdThreadWake`. Each service pass the thread walks the devices it owns and,
for each, the chain of its PDOs (`dev->Pdo`, `Sibling`; the thread is their
writer, record 13 section 5.4), reads the slots under the controller lock and
computes what the link should be. So nothing the thread holds can name a PDO
or a device that has gone: a gone device is not in the thread's walk, and its
PDOs' slots were settled when they were unlinked (section 4.4).

### 3.2 The admission gate: off the ring, with its own pause

Entering SUSPENDING takes one `HcdIoPipePause` on every open pipe of the
device, EP0 and every stream's pipe included, before the first Stop
Endpoint, and sets the pipe's `SuspendPause` flag (controller lock), so the
machine releases exactly its own pause and no other owner's (`Paused` is a
bare count). A request reaching `HcdIoMapped` on a paused pipe is held
mapped on `pipe->Held` (`hcd_io.c:1157-1170`), never published. The gate
opens only in ACTIVE: for each pipe whose `SuspendPause` is set, clear it and
`HcdIoPipeResume`, which decrements under the lock and releases the held
queue. Held requests stay cancellable as today.

Requests already on a ring when the gate closed - and cancellation survivors,
which stay on the stopped ring (`hcd_cfg.c:2755`), not on `Held` - are not
released by `HcdIoPipeResume`. The machine restarts them explicitly at
ACTIVE: a doorbell for every endpoint whose ring holds TDs, unless the
endpoint is Halted or in error, or another pause owner still holds it (its
owner rings it when it lets go, as today). `hcd_cfg.c`'s survivor restart
(`hcd_cfg.c:2803`) skips its doorbell while the link is not ACTIVE.

### 3.3 Slow IRPs while the link is not ACTIVE: per-device deferral

`HcdCfgService` serves one controller-wide FIFO (`hc->SlowIrps`,
`hcd_cfg.c:3551`) and must never block on one device. SUSPENDING and
RESUMING are never seen by it: the thread runs each of those sequences to its
end inside one call, and `HcdCfgService` runs on the same thread. So the
states it meets are SUSPENDED and RECOVERING (which stops the service for a
tainted controller, today's rule). For a device SUSPENDED, every endpoint is
stopped (SP=1), and a slow IRP dequeued for it is split:

- **Host side, served now:** ABORT_PIPE; the cancel and drain of a pipe (a
  ring edit and Set TR Dequeue on a stopped endpoint need no link); the
  host-side half of a function release - its endpoints' cancel, drain and
  drop (Configure Endpoint). A client quiescing itself never forces a
  resume.
- **Device side, deferred:** the device-side half of a function release
  (SET_INTERFACE to alternate 0, `hcd_cfg.c:1653`), SELECT_CONFIGURATION,
  SELECT_INTERFACE, RESET_PIPE's clear-halt, GET_STATUS and every other
  request the thread sends on EP0 move to `dev->SlowDeferred`, in order, the
  release's half ahead of anything queued after the release (the
  release-before-reconfigure order `hcdCfgService` keeps today). Deferred
  IRPs stay cancellable. They do not ask for a resume. At ACTIVE the list is
  spliced back to the head of `SlowIrps` in order; on GONE it completes
  `DEVICE_GONE` as the dequeue does today.

### 3.4 Suspend sequence (thread)

(1) gate closed (3.2); (2) if the episode says armed (section 5),
SET_FEATURE(DEVICE_REMOTE_WAKEUP) through `HcdThreadControlOutcome` while the
link is U0; (3) Stop Endpoint with SP=1 on every running endpoint, EP0
included, state-aware, with stopped-transfer bookkeeping (xHCI 1.2c
4.15.1.1); (4) the port to U3 (root: `XhciPortscSuspend`; USB 2.0 hub:
SetPortFeature(PORT_SUSPEND)); (5) observe it, bounded 50 ms: at a root port
PLS=U3, or a valid Resume (the device entered U3 and is already waking -
suspension happened; take the wake branch); at a USB 2.0 hub port
PORT_SUSPEND set, or PORT_SUSPEND clear **with C_PORT_SUSPEND** (a remote wake
that already finished - the wake branch); (6) note the U3 time, SUSPENDED.

### 3.5 Outcomes of a failed step

- **Refusal** - hardware provably usable: (2) answered STALL (the device
  declines wake); (2) returned NOT_SENT because client TDs published before
  the gate closed kept EP0 busy (`HcdThreadControlOutcome`, `hcd_enum.c:855`,
  no recovery requested) - one retry after the ring drains, bounded 100 ms;
  (3) or (4) failed before anything left U0 with every endpoint's state read
  back. Restart the stopped endpoints after SP=1's 10 ms minimum, open the
  gate, ACTIVE. **A refusal ends the idle episode** (6.5: every held idle IRP
  of the batch completes `STATUS_SUCCESS` once its callback has returned, so
  every function that went to Dx is driven back to D0 by its own completion
  routine) - no client is left in Dx on a link that will not suspend. A
  STALL also completes the held WAIT_WAKEs `STATUS_NOT_SUPPORTED` (the
  device cannot wake); after NOT_SENT they stay pending. The machine does not
  retry until the slots change again.
- **Uncertain** - an EP0 or command timeout (which sets
  `ScratchTainted`/`Ep0Stuck` and asks for a reset, `hcd_enum.c:937`), U3
  neither observed nor refuted, a resume not verified in its bound: the gate
  stays closed, RECOVERING, and the existing reset/readdress path owns the
  device; it reaches ACTIVE (gate opens, 3.2's restart, the idle episode
  ends as for a refusal) or GONE.
- **Disconnect** - CSC, lost CCS or a port error at any step: an ordinary
  unplug, GONE.

A Dx completes `STATUS_SUCCESS` in every outcome (logical Dx is satisfied
whether or not the link moved); counters and the log name the outcome.
Control traffic is never sent on a suspended link; disarm follows a verified
U0.

### 3.6 Resume sequence

Host-initiated (a D0, or a rearm, 5.4): not before 10 ms after the observed
U3 (xHCI 1.2c 6.4.3.8). Root USB 2.0: PLS=Resume, 20 ms (TDRSMDN), PLS=U0,
verify U0 within 50 ms. USB 2.0 hub: ClearPortFeature(PORT_SUSPEND), then
C_PORT_SUSPEND or a poll that reads PORT_SUSPEND clear within 50 ms.
Device-initiated: no dwell; at a root port the thread waits out TDRSMDN from
the PLC and writes U0; at a hub the hub has already timed it. Then TRSMRCY
10 ms, disarm if armed, 3.2's restart, open the gate, ACTIVE. Never wait to
*read* PLS=Resume after writing it (QEMU ignores the write; task 22.12(b)).
A resume not verified is the uncertain outcome.

### 3.7 Wake evidence

The root port's acknowledgement (`hcd_enum.c:2209`) hands the machine the
**whole sample**: the PORTSC read before the acknowledgement, the change mask
it acknowledged, and the device generation the port held. The machine
classifies in this precedence: CSC, lost CCS, a port error -> disconnect
(Resume with CSC is a disconnect); a different device generation -> stale;
PLC with U3 -> suspend entry; PLC with Resume while SUSPENDING or SUSPENDED
-> device wake; PLC with U0 while RESUMING -> host resume complete; anything
else -> stale, ignored. Evidence arriving while SUSPENDING is kept and acted
on at (5) or (6).

### 3.8 Hub ports

For a port whose device the machine owns (any state but ACTIVE), both
branches of the hub port look change: the `d->Resume` auto-resume
(`hcd_hub.c:827`) is skipped, and the `d->Suspended` branch (`hcd_hub.c:837`,
a finished resume or a USB 2.0 remote wake, reached after the change
acknowledgement at 807) hands its sample (port status, the change mask
acknowledged, the device generation) to the machine instead of running its
own quiesce, recovery wait and release. A port the bus did not suspend keeps
today's handling, and connection and error processing are unchanged in every
case. The machine's hub resume reuses `hcdHubPortResume`'s requests but not
its subtree pause and release (a suspended device here is a leaf).

## 4. Function power state at the device PDO

### 4.1 Fields, writers and locks

All under the **controller lock** (innermost, record 13 section 5.4):

| Field | Meaning | Writers |
|---|---|---|
| `pdo->PwrIrp` | the one pended D-IRP | dispatch (set), whoever claims it (clear) |
| `pdo->PwrWanted` | the D-state asked | dispatch |
| `pdo->PwrState` | the D-state the bus acts on | dispatch (a Dx: at once), thread (D0: at ACTIVE) |
| `pdo->PwrClosed` | admission closed | teardown |
| `pdo->IdleState`, `IdleBatch` | section 6 | dispatch, cancel, callback thread, teardown |
| `dev->Link`, `dev->Episode`, pipes' `SuspendPause` | sections 3, 5 | thread |
| `hc->PowerEpoch` | controller session | power path, recovery |

A Dx is effective at once: dispatch writes `PwrState` = the Dx when it
accepts the IRP, so the last function's D2 counts when the thread computes
arming in the same pass. `PwrState` returns to D0 only when the link is
ACTIVE. `PdoListLock` is taken before the controller lock where both are
needed; the cancel spin lock is released before the controller lock is taken
(record 13 section 5.4's rule); a cancel routine releases the cancel spin
lock first, then takes the controller lock to mark.

### 4.2 Claiming, cancellation and reaching the controller

- **D-IRPs** have no cancel routine. Whoever completes one claims it under
  the controller lock - `PwrIrp` taken and cleared in the same hold - and
  completes it outside the lock at PASSIVE_LEVEL, `PoStartNextPowerIrp`
  first. No stale result exists to compare (3.1): the thread claims from the
  slots it walks, teardown from the PDO it is tearing down.
- **WAIT_WAKE and idle IRPs: one rule, the canonical handshake.** The IRP
  is cancellable exactly while its cancel routine is set, and **a cancel
  routine that runs always completes the IRP** - there is no "mark and
  complete later". Publication: under the controller lock, fill the slot,
  `IoSetCancelRoutine(irp, routine)`, then test `irp->Cancel`; if set and
  `IoSetCancelRoutine(irp, NULL)` returns non-NULL, the submitter takes the
  IRP back out of the slot and completes it `STATUS_CANCELLED` (a cancel
  before publication); if that returns NULL the routine is running and will
  complete it. A claimer (thread, callback thread, teardown), under the
  controller lock, calls `IoSetCancelRoutine(irp, NULL)`: non-NULL - the
  claimer owns it and clears the slot in the same hold; NULL - the routine
  owns it, and the claimer leaves the slot alone. The routine releases the
  cancel spin lock, takes the controller lock, clears the slot if it still
  names the IRP and completes it `STATUS_CANCELLED`. An owner that must give
  the IRP back to cancellability (the callback thread after a callback
  returns, 6.4) re-publishes it the same way, `irp->Cancel` test included, so
  a cancel that came while the routine was cleared is honoured then.
- **Lifetime counts.** `PowerPending` covers a D-IRP or WAIT_WAKE from
  acceptance until its completion returns. `IdlePending` takes **two**
  references for an idle IRP in a batch - one for the IRP, dropped after its
  completion returns, one for the callback-queue entry, dropped when the
  callback thread lets go of the entry - and one for an idle IRP not yet in
  a batch.
- **Reaching the controller.** A power, WAIT_WAKE or idle IRP reaches the
  controller's lock through `pdo->Controller` under the same `Busy`
  protection the internal-IOCTL dispatch takes against the parent's release
  (`hcd_pdo.c:2135`), extended to the power dispatch and to the cancel
  routines. A PDO whose controller is already released: a D-IRP completes at
  once `STATUS_SUCCESS`, a WAIT_WAKE or idle IRP is refused
  `STATUS_NO_SUCH_DEVICE`.

### 4.3 Composite (record 13 section 10.9)

A function's Dx completes at once while any sibling's `PwrState` is D0. The
last function to leave D0 has its D-IRP pended; the thread suspends and
completes it when the machine ends (any outcome). A function's D0 is pended
unless the link is ACTIVE; the thread resumes and completes every pended D0
of the device at ACTIVE. A D0 arriving during SUSPENDING waits for it to end,
then the dwell, then the resume. A D2->D3 change while SUSPENDED completes at
once (5.4 reconciles wake). D0 never completes before ACTIVE, except on GONE
(4.4).

### 4.4 GONE, teardown, START and the controller

- **GONE settles at the unlink.** Where the thread unlinks a device's PDO
  chain (`hcdDevicePdoGone`, `hcd_pdo.c:1704-1719`, which clears
  `pdo->Device`), it claims and completes each PDO's `PwrIrp`
  (`STATUS_SUCCESS`) and held WAIT_WAKE (`STATUS_NO_SUCH_DEVICE`), sets each
  PDO's `IdleBatchCur` to 0 (no batch, 6.2) and sets `PwrClosed`. It never
  waits for a REMOVE to do it.
- **Teardown of a PDO** (STOP, SURPRISE_REMOVAL, REMOVE - 98's direct REMOVE
  included - and `hcdDeletePdo` for an unreported PDO), in this order:
  (a) close admission: `PwrClosed` = 1, so a later D-IRP completes at once
  with SUCCESS, a later WAIT_WAKE or idle IRP is refused, and a later slow
  IRP from the PDO completes `DEVICE_GONE` at submission;
  (b) drain what the PDO already has queued anywhere, **ahead of** the idle
  rundown that `hcd_pdo.c:2197` now runs first. Today's `hcdPdoQuiesce`
  asks the enum thread to cancel and then waits for `UrbsPending` 0
  (`hcd_pdo.c:2223`, `2234`), which cannot run on the enum thread or after it
  exits, so it is split in three:
  *initiate* - under the controller lock, detach the PDO's entries from
  `SlowIrps` and `SlowDeferred` onto a local list and mark its held, mapping
  and on-ring transfers cancelled; release the lock, then complete the
  detached IRPs `CANCELED` (the pattern of `hcd_cfg.c:165-178`: nothing is
  ever completed under the controller lock, since a completion can re-enter
  the bus);
  *service* - the ring work (stop, Set TR Dequeue) is the enum thread's: a
  PnP-thread caller requests it and waits, as today; the enum thread as
  caller (`hcdDeletePdo`) runs it inline, being the executor; after the
  thread has exited, the controller is halted with the DMA-halt proof the
  stop already takes (`CommonBufferPinned` clear), so the rings are no longer
  mastered and the caller completes the on-ring transfers directly - with
  the proof absent they stay pending, as `HcdIoDeviceDrain` keeps them today;
  *finish* - `UrbsPending` 0 is a **prerequisite of finalization, never a
  wait on the enum thread**. A PnP-thread caller waits for it as today (the
  thread is alive and drains). The enum thread as caller does not wait: a
  PDO with `UrbsPending` not 0 joins the deferred-delete list, finalized on a
  later pass once `UrbsPending` is 0 and the idle rundown is done. A PDO
  already unlinked (`pdo->Device` NULL, `hcd_pdo.c:1719`) may still have
  URBs on rings: the thread unlinks before it drains (`hcd_enum.c:2817`,
  `2836`), so the drain that completes or parks them is the thread's own,
  later in the same pass, and the deferred finalization waits for it, not
  the teardown. After the enum thread has exited, the controller's teardown
  waits for `UrbsPending` 0 only when the DMA-halt proof holds; without it
  (`CommonBufferPinned`, `hcd_io.c:1901`) the PDO is **quarantined** - it,
  its pending URBs and their records stay allocated, as `HcdIoDeviceDrain`
  keeps them today - and the teardown proceeds without it;
  (c) claim and complete `PwrIrp` and the WAIT_WAKE;
  (d) cancel the idle entry (6.4);
  (e) wait for the idle rundown. With the DMA-halt proof, after (a) to (d)
  nothing a callback can wait on is left pending in the bus, so it returns
  without the thread; without it, quarantine (below) instead of a wait.
  The wait is bounded when the caller is the enum thread (`hcdDeletePdo`,
  `hcd_pdo.c:504`): a callback not yet returned moves the PDO's deletion to a
  deferred-delete list the thread rechecks every pass.
- **After the enum thread exits.** The controller's teardown stops the enum
  thread before device teardown (`hcd_ctl.c:797`); it then finishes the
  deferred-delete list itself at PASSIVE_LEVEL, and only then stops the
  callback thread. With the DMA-halt proof, every prerequisite completes
  (no thread is left to block) and it waits for them. Without it, the
  quarantine below.
- **Quarantine** (the DMA-halt proof absent, `CommonBufferPinned`; the
  policy `hcd_dma.c:145-157` already applies to buffers, extended to what a
  callback can still reach). A callback may be waiting, as the Windows
  idle-callback contract lets it, for cancelled URBs that will never
  complete. So:
  - The PDO's teardown keeps the existing escape (`hcd_pdo.c:2234`) and
    does not wait for a RUNNING callback either: the PDO is quarantined -
    its device object is not deleted, `pdo->Controller` is cleared under the
    `Busy` protection (so later dispatch answers per 4.2's "controller
    already released"), it is never revived (`hcdDormantRevive` skips it),
    and its pinned URBs and records stay allocated.
  - **The callback thread's context is its own**, allocated from nonpaged
    pool apart from the controller extension, with a reference count of 2
    (the thread's and the controller's); whoever drops the last frees it.
    Its `Phase` changes only by `InterlockedCompareExchange`:
    IDLE -> CLAIMING (the thread, **before** it dequeues anything; failing on EXITING, it exits without dequeuing),
    CLAIMING -> IN_CALLBACK (the thread, having dequeued and claimed an
    entry), CLAIMING -> IDLE (the thread, having found nothing to call: an
    empty queue, an empty or cancel-owned slot, or a skip it has completed),
    IN_CALLBACK -> RETURNING (the thread, after the callback returns),
    RETURNING -> IDLE (the thread, after its post-callback bookkeeping),
    IN_CALLBACK -> ABANDONED (teardown), IDLE -> EXITING (teardown). The
    thread dequeues and claims only while it holds CLAIMING, and CLAIMING
    and RETURNING are short and never block. So there is no interval in
    which the thread owns a claimed entry while teardown can win EXITING:
    teardown's IDLE -> EXITING fails while the thread is CLAIMING, and
    the thread's own transitions out of CLAIMING never fail, since only the
    thread moves the phase out of CLAIMING. Controller access by the thread
    happens only in CLAIMING and RETURNING, so a phase the thread won is the
    right to touch the controller.
  - **Teardown's handoff.** (1) Under the controller lock, detach every
    **unclaimed** entry from the callback queue; for each, settle its IRP by
    6.3's three-way claim (claimed: complete `STATUS_CANCELLED` after the
    lock; cancel-owned or empty: leave it) and drop the entry's
    `IdlePending` reference - so no PDO waits on an entry stuck behind a
    callback that never returns. (2) Try IDLE -> EXITING: won - signal and
    join the thread, the normal end. (3) Else try IN_CALLBACK -> ABANDONED:
    won - the claimed entry and its PDO stay with the context, the thread
    object reference moves to a driver-global list, the controller drops
    its context reference, and the teardown proceeds without joining. (4)
    Else the phase is CLAIMING or RETURNING: the thread is in a step that
    cannot block; wait for it to leave that phase and retry from (1) (an
    entry it put back as unclaimed is then drained).
  - **An abandoned thread's return** (if ever): its IN_CALLBACK ->
    RETURNING fails on ABANDONED, so it touches nothing of the controller -
    no lock, no slot, no re-publication. It completes the idle IRP it owns
    `STATUS_CANCELLED` (never before the callback returned), drops the
    quarantined PDO's references, drops its context reference (freeing the
    context) and exits.
  - Nothing quarantined points at the controller extension, so the
    controller's REMOVE deletes it as today. A restart on the same FDO
    creates a new callback thread and context (as `hcd_dma.c:73` opens
    fresh buffers); the quarantined PDO and thread stay apart from it.
  - The driver image stays loaded while a quarantined device object exists
    (the I/O manager does not unload a driver with device objects; to be
    confirmed on 98 SE's NTKERN with the fault-injection leg, 10).
- **START, and revival of a dormant PDO** (`hcdDormantRevive`,
  `hcd_pdo.c:1351`; 98 and ME reuse them). Once the earlier teardown's
  rundown has finished (a START that finds it unfinished waits for it, as
  the revival already waits for its other leftovers), a successful START
  reopens admission: `PwrClosed` = 0, `PwrWanted` = `PwrState` = D0 and
  `Common.DevicePower` D0 together, `IdleState` NONE, `IdleBatchCur` 0,
  `PwrIrp` NULL. The device it now stands for has its own `Link` ACTIVE and
  its own `IdleBatch`; nothing of the PDO's earlier device carries over.
- **The controller leaving D0, stopping, recovering in place or resetting**
  (`hcd_power.c`, before `XhciSuspendController`; the stop and the HCRST
  paths): in **one** controller-lock hold, `PowerEpoch` increments and
  `hc->PowerDown` is set (cleared when the controller is back in D0 and its
  thread's service runs again). **Admission while `PowerDown`**: dispatch,
  checking the flag in the same hold that would fill the slot, completes
  every Dx at once `STATUS_SUCCESS` itself (it is at PASSIVE_LEVEL: the
  PDOs are power-pageable), never pending it, so no Dx can arrive after the
  final walk and wait for a service that does not run unpowered
  (`hcd_enum.c:3411`); a D0 still pends in its slot. Each pended
  **Dx** carries the epoch it was accepted in, and one from an older epoch is
  claimed and completed `STATUS_SUCCESS` (the device is going unpowered or
  being re-established; logical Dx holds) by the next PASSIVE context that
  walks the slots - never inside the transition, since recovery runs
  `XhciRecoverController` at DISPATCH_LEVEL (`hcd_ctl.c:411`). For the
  controller's D3 and stop, that context is the power or PnP path itself,
  after the controller is quiesced and at PASSIVE_LEVEL (the enum service
  returns early while unpowered); for in-place recovery and HCRST it is the
  enum thread's next pass, the controller powered. Pended **D0**s stay in their slots -
  there is no separate list and no second owner. New D0s while the
  controller is down are pended in their slots the same way. When the
  controller is back and the thread's service runs (after `PowerGate` is
  released by the power path - nothing waits for the thread under it), the
  thread walks the slots as always: a device kept by the restore is resumed
  (its link may be U3) and its D0s completed at ACTIVE; a device the reset
  invalidated (`hcd_enum.c:3405`) is GONE, and its PDOs were settled at the
  unlink - a re-enumerated device is a new device with new or revived PDOs
  and inherits no IRP; the controller removed - every PDO's teardown settles
  its own slot.
- **Win98.** Every completion above at PASSIVE_LEVEL; the bus never calls
  `PoRequestPowerIrp` on a child's behalf.

## 5. Wake episode (stage A)

### 5.1 Eligibility

A USB 2.0 device that is not a hub, at any depth - on a root port or behind
any number of hubs, a USB 3 hub's USB 2.0 half included - whose
configuration sets bmAttributes bit 5. Hub PDOs (task 33.4) are excluded
from capabilities, WAIT_WAKE, idle callbacks and suspend requests, so every
ancestor stays active; the devices behind them are not excluded.

### 5.2 Capabilities

Eligible: `DeviceD1`/`DeviceD2` TRUE, `WakeFromD1`/`WakeFromD2` TRUE,
`WakeFromD3` FALSE, `DeviceWake` D2, `SystemWake` PowerSystemWorking (S0
only), latencies as usbhub's (read per target). A non-hub USB 2.0 device
without the bit: `DeviceD2` TRUE, no wake. Hubs, SuperSpeed devices and the
switch at 0: today's values exactly.

### 5.3 WAIT_WAKE

Refused `STATUS_NOT_SUPPORTED` if not eligible or `PwrClosed`;
`STATUS_INVALID_DEVICE_STATE` if the requested system state is deeper than
`SystemWake` or `PwrState` is D3; `STATUS_DEVICE_BUSY` for a second on the
same PDO. Accepted: pended with a cancel routine and the PDO's
`PowerPending`, and stamped, under the controller lock in the same hold that
fills the slot, with `hc->WakeSeq++` (a 32-bit per-controller counter).
Teardown completes it `STATUS_NO_SUCH_DEVICE` (removal, GONE) or
`STATUS_CANCELLED` (stop).

### 5.4 The episode

- **Armed intent**, recomputed by the thread before every suspend and every
  re-suspend: at least one function holds a WAIT_WAKE **and** its `PwrState`
  is D1 or D2. A D3 function contributes none and cancels none: A=D2 with
  WAIT_WAKE and B=D3 arms.
- **A wake is one event per suspension.** The first wake evidence that moves
  SUSPENDING or SUSPENDED to RESUMING is the event; evidence repeated while
  RESUMING is ignored. In the same controller-lock hold that records the
  event, the thread reads `hc->WakeSeq` as the event's watermark and claims
  (4.2's handshake) every WAIT_WAKE of the device's PDOs whose stamp
  precedes it (signed 32-bit difference, so wraparound is harmless) and
  whose `PwrState` is D1 or D2, and **completes them `STATUS_SUCCESS` at
  once**, before the resume - a WAIT_WAKE completes when the wake is
  signalled, as usbhub's do. There is no interval between claim and
  completion to own, and a resume that then fails or a GONE that follows
  changes nothing for them: the function's D0 pends until ACTIVE or GONE
  (4.3, 4.4). A WAIT_WAKE accepted while SUSPENDED and armed is stamped
  before the event and so is completed by it.
- **Unarmed but now wanted.** A WAIT_WAKE accepted while the device is
  SUSPENDED unarmed makes the thread rearm: resume, arm, and - every function
  still Dx - suspend again. This is an internal link cycle: it does not end
  the idle episode (6.5).
- **Armed but no longer wanted.** The last eligible waiter cancelled or gone
  to D3 leaves the device suspended and armed; a wake that then arrives
  claims nothing (`WakeUnclaimed`), the thread resumes, disarms, recomputes
  intent and - every function still Dx - suspends again: also an internal
  cycle.

### 5.5 Windows 98 SE and ME

Intel's WDM power paper (wdm_pm11.pdf p.19) reports a 9x defect where a
WAIT_WAKE requester's completion callback is not called. The SUSPa-V test
client reads it on both. If it holds, wake is **disabled on 9x**:
capabilities report no wake there and WAIT_WAKE is refused, so no class
driver suspends expecting a wake that cannot reach it; suspend on an explicit
Dx with resume on D0 remains.

## 6. Idle notification (XP onward)

### 6.1 Eligibility and the callback thread

A non-hub USB 2.0 device PDO in stage A. A SuperSpeed or hub PDO's idle IRP
is held exactly as today, its callback never called. Callbacks run on a
thread of the bus's own, one per controller, separate from the enum thread
and from the system worker pool (a callback may block on its D-IRP; pool
threads are few on 98), one at a time, at PASSIVE_LEVEL, holding no bus
lock or gate. Its queue holds PDO entries pinned by `IdlePending`, never a
device pointer. The enum thread never waits on it except through the
bounded, deferring rundown of 4.4.

### 6.2 Batches (composite readiness, as the composite parent does)

The thread forms a **batch** for a device when every started function PDO of
its group holds an idle IRP and has `PwrState` D0: it increments the
device's `IdleBatch` and writes the new number into **each PDO's**
`IdleBatchCur` and each entry's `Batch` (controller lock), then queues the
entries. A claim (6.3) compares the entry's `Batch` with its own PDO's
`IdleBatchCur` - no device is looked up. Unlink and teardown set
`IdleBatchCur` to 0, so an entry of a gone device never matches.

### 6.3 Claim at call time

Under the controller lock: the entry is QUEUED, `Batch` equals the PDO's
`IdleBatchCur`, the PDO is started and not `PwrClosed`, `PwrState` is D0,
the system is S0 and the epoch is the one queued. Then exactly one of three:

- **Slot empty** (cancelled while QUEUED and already completed): the thread
  only lets go of the entry.
- **Slot non-empty and `IoSetCancelRoutine(irp, NULL)` returns NULL**: a
  cancel owns it and will complete it (4.2); the thread leaves the slot
  alone and only lets go of the entry. The cancel ends the episode.
- **Slot non-empty and the call returns non-NULL**: the thread owns the IRP.
  If every eligibility check above held, RUNNING. If any failed, the thread
  clears the slot, releases the lock, completes the IRP as a skip (6.5) and
  the episode ends.

The eligibility checks never authorize a completion by themselves; only
the successful claim does.

### 6.4 States and ownership

`IdleState` under the controller lock; the IRP's ownership by 4.2's
handshake only.

| State | Cancel routine | Who owns the IRP | A cancel |
|---|---|---|---|
| HELD | set | cancellable | the routine completes it `STATUS_CANCELLED` |
| QUEUED | set | cancellable; the entry is the callback thread's | the routine completes it; the callback thread, reaching the entry, finds the slot empty and only lets go of the entry |
| RUNNING | cleared by the callback thread at claim (6.3) | the callback thread | `irp->Cancel` is set and nothing runs; when the callback returns the thread re-publishes (4.2) and the `irp->Cancel` test completes it |
| RETURNED | re-set after the callback returned | cancellable | the routine completes it |

A cancel of a QUEUED, RUNNING or RETURNED IRP also ends the episode (6.5).
A new submission while the PDO's previous IRP or entry is still owned:
`STATUS_DEVICE_BUSY`.

### 6.5 Settlement of every accepted idle IRP

Each accepted idle IRP is settled by the first of these that applies to
**its own PDO**, whether or not it is in a batch, by whoever claims it
(4.2); an IRP RUNNING is claimed only after its callback returns:

| Trigger | Status |
|---|---|
| its cancel | `STATUS_CANCELLED` (the routine) |
| its PDO torn down or its device GONE | `STATUS_CANCELLED` |
| a system power IRP to its PDO for a state other than S0 | `STATUS_CANCELLED` |
| its PDO accepts D3 | `STATUS_POWER_STATE_INVALID`, at acceptance |
| its PDO, having been Dx, reaches D0 at ACTIVE | `STATUS_SUCCESS` |
| its episode ends with the link ACTIVE while it is RETURNED and its PDO is in D1 or D2 | `STATUS_SUCCESS` (its completion routine then sends its D0) |
| a skip at claim (6.3) | `STATUS_CANCELLED` |

An IRP in HELD (waiting for its siblings) is reached by the first four and
the skip; nothing else settles it, so it stays held while its PDO is D0 and
no trigger comes, as the composite parent holds it.

**The episode** is the life of one batch. It ends - `IdleBatch` and every
PDO's `IdleBatchCur` move on, so no further callback of it runs - on any
of: a cancel of one of its IRPs; one function's own D0; a skip; a refusal or
an uncertain outcome that ends ACTIVE (3.5); a system power change; the
device GONE. A function's D3 settles that function's IRP but does not end
the episode for the others. The internal link cycles of 5.4 (rearm,
unclaimed wake) do not end it. When it ends with the link ACTIVE, the sixth
trigger settles the siblings, after every RUNNING callback of the batch has
returned. A remote wake ends it through the woken function's D0.

## 7. The switch

`XhciSelectiveSuspend`, REG_DWORD under the controller's driver key, read at
start like `XhciFirstEnumWaitMs`. **Default 1** (owner, 2026-10-05, over
Codex's round-2 advice of 0): sections 3 to 6 are on for every user of the
release that carries them. 0 is today's behaviour bit for bit (capabilities,
WAIT_WAKE refused, idle callbacks never called, Dx recorded only) and is the
escape a user is told about in the release notes. The INF writes nothing; the
INF gate's SUSP-* rules stand.

## 8. Stage B outline (SuperSpeed), designed after stage A's readings

USB 3 suspend is the link to U3 plus, for wake, SET_FEATURE(FUNCTION_SUSPEND)
with the wake-enable option per interface (USB 3.2 9.4.5) and the device's
function remote-wake capability (GET_STATUS). A device wakes by driving the
link to Resume (root: PLS=Resume with PLC; software writes U0 to complete the
LFPS handshake, xHCI 1.2c 4.15.2.1) and sending a FUNCTION_WAKE Device
Notification, which needs DNCTRL bit 1 and Device Notification event handling
with interface-to-function ownership; behind an SS hub the exit sets no
C_PORT_LINK_STATE (record 13 section 10.2), so the notification is the only
signal. `xhci_link.c`'s Resume classification and `WANT_RESUME` are fixed
there. QEMU has no SS hub, so SUSPb's hub half is bench-only.

## 9. Codex review of revision 1 (2026-10-05), where each finding is taken

| # | Finding | Answered in |
|---|---|---|
| 1 | Callback on the enum thread deadlocks | 6, first bullet |
| 2 | Idle cancel unsafe while a callback runs | 6, callback ownership |
| 3 | USB2 wake arming is the bus's | 5, arming |
| 4 | SS root wake goes via Resume; link builder wrong | 1 (stage B), 8 |
| 5 | SS hub wake needs Function Wake notification | 8 |
| 6 | Hub look undoes a deliberate suspend | 3, hub port look |
| 7 | Gate must close before the first stop; rollback | 3, admission gate and rollback |
| 8 | Bitmap + serial is not IRP ownership | 4, ownership and drain |
| 9 | `SystemPower` does not classify a Dx | 4, cause of a Dx |
| 10 | Idle completion and composite rules | 4 composite, 6 completion and composite |
| 11 | 98 WAIT_WAKE completion defect | 5, Windows 98 caveat; 10 |
| 12 | 10 ms after U3, wake classification, verify state | 3, resume and wake events |
| 13 | Whole capabilities contract | 5, capabilities |

Revision 2's review (same thread) found 12; revision 3 takes them:

| # | Finding | Answered in |
|---|---|---|
| 1 | Gate suppressed doorbells, TDs still published | 3, admission gate (off the ring via `Paused`) |
| 2 | Controller settlement circular | 4, controller transitions (restore list) |
| 3 | Blanket rollback unsafe | 3, failure classes |
| 4 | Work-item cancel/rundown | 6, callback thread and ownership |
| 5 | Generation check and slot take not atomic | 4, one request slot (claim under lock) |
| 6 | Privileged EP0 traffic, cancel restart | 3, privileged thread traffic |
| 7 | Hub remote-wake branch at 837 | 3, hub ports |
| 8 | Wake during SUSPENDING, resume bounds | 3, suspend step (5), resume |
| 9 | Composite wake aggregation | 5, aggregate and episode |
| 10 | Physical ACTIVE is not PDO D0; idle statuses | 3 intro, 6 claim and completion |
| 11 | 9x fallback does not restore input | 5, wake disabled on 9x if the defect holds |
| 12 | Hub and SS exclusions; WAIT_WAKE D-state | 5 eligibility and WAIT_WAKE, 6 eligibility |

Revision 3's review found 10; revision 4 takes them:

| # | Finding | Answered in |
|---|---|---|
| 1 | Queued results outlive PDO and device | 3.1 (level-triggered, nothing queued), 4.2 |
| 2 | Restore list had two owners | 4.4, the controller (no list; slots stay, thread walks) |
| 3 | Callback teardown can block the enum thread | 4.4 teardown order (a)-(d), deferred delete |
| 4 | Aborted composite batch strands a function in D2 | 6.2 batches and abort |
| 5 | Late WAIT_WAKE never serviced | 5.4 completion by identity, rearm |
| 6 | EP0 not quiet; survivors on the stopped ring | 3.5 NOT_SENT as refusal busy, 3.2 explicit restart |
| 7 | Deferral needs a scheduler | 3.3 per-device deferral |
| 8 | RECOVERING ownership, full sample, hub finished wake | 3 intro, 3.7, 3.4 (5), 3.8 |
| 9 | Hub children wrongly excluded | 5.1 |
| 10 | Lock contract and pause ownership | 4.1, 3.2 SuspendPause |

Revision 4's review found 9; revision 5 takes them:

| # | Finding | Answered in |
|---|---|---|
| 1 | Function release sends SET_INTERFACE | 3.3 host-side and device-side halves |
| 2 | Idle cancel after the callback returned | 6.4 table, RETURNED |
| 3 | Cancel-routine arbitration | 4.2 handshake |
| 4 | Teardown order; deferred delete after thread exit | 4.4 (b) before (e); after the enum thread exits |
| 5 | START and revival never reopen | 4.4 START |
| 6 | Refusal busy leaves an idle client asleep | 3.5 a refusal ends the episode |
| 7 | Batch lookup and controller lifetime | 6.2 per-PDO IdleBatchCur, 4.2 reaching the controller |
| 8 | Wake watermark | 5.3 stamp, 5.4 one event, completed at once |
| 9 | Idle completion precedence | 6.5 |

Revision 5's review found 3; revision 6 takes them:

| # | Finding | Answered in |
|---|---|---|
| 1 | Quiesce waits on the enum thread | 4.4 (b) initiate / service / finish |
| 2 | Cancel ownership contradictory | 4.2 one handshake, 6.3 claim, 6.4 table |
| 3 | Settlement of every idle IRP; D3 as a trigger | 6.5 settlement table |
| - | Recovery at DISPATCH cannot complete | 4.4 the controller (epoch-stamped Dx, PASSIVE walker) |

Revision 6's review found 4; revision 7 takes them:

| # | Finding | Answered in |
|---|---|---|
| 1 | Unlink precedes the drain; pinned URBs never drain | 4.4 (b) finish: prerequisite, deferred finalization, quarantine |
| 2 | Callback claim needs three branches | 6.3 |
| 3 | Completion under the controller lock | 4.4 (b) initiate: detach, then complete |
| 4 | Dx accepted after the controller went down | 4.4 the controller: PowerDown admission |

Revision 7's review found 1; revision 8 takes it:

| # | Finding | Answered in |
|---|---|---|
| 1 | Quarantine does not cover callback rundown | 4.4 quarantine (self-owned callback context, no join inside a callback, PDO kept), (e) |

Revision 8's review found 2; revision 9 takes them:

| # | Finding | Answered in |
|---|---|---|
| 1 | Callback return vs quarantine not synchronized | 4.4 the context's Phase (compare-exchange) and reference count |
| 2 | Quarantine strands queued entries | 4.4 teardown's handoff (1) |

Revision 9's review found 1; revision 10 takes it:

| # | Finding | Answered in |
|---|---|---|
| 1 | Claimed entry before IN_CALLBACK can lose to EXITING | 4.4 the CLAIMING phase, held across dequeue and claim |

## 10. Tasks and verification (stage A)

- SUSPa.1 `xhci_susp.c` machine + `test_susp` (states, rollback at each step,
  composite last-Dx, D0 during SUSPENDING, epoch-stamped Dx, cancel before publication, QUEUED and RUNNING cancel followed by teardown, unlink with outstanding URBs, rundown after the enum thread exits, D3 and system transitions with an idle IRP still HELD, wake
  classification vectors); `xhci_link.c`/`xhci_port.c` vectors as needed.
- SUSPa.2 the gate on `Paused` and `hcd_cfg.c`'s survivor restart; `hcd_enum.c`
  thread requests (per-PDO slots, not a bitmap), root-port execution and the
  PLS handed over at acknowledgement; `hcd_hub.c` USB 2.0 hub port suspend
  and both branches of the look (827, 837) routed to the machine.
- SUSPa.3 `hcd_pdo.c` power: D-IRP slot, composite rule, capabilities,
  WAIT_WAKE, controller-transition settlement (`hcd_power.c`).
- SUSPa.4 `hcd_urb.c` idle callbacks on the bus's callback thread, with
  composite readiness and the ownership of section 6.
- SUSPa.5 The switch; counters (requests, completions, rollbacks and reason,
  stale generations, wake armed/disarmed/events, callback entry/exit/cancel,
  drained IRPs, held transfers) in the counter block and XHCISNAP; bounded
  log lines naming group/function, epoch, old/new state, reason.
- SUSPa.6 A test client (`test\pmclient`, a tiny filter or function driver for
  the VM legs only, never shipped) that drives Dx/D0, WAIT_WAKE and idle
  submit/cancel on demand, because no stock class driver on 98 SE or 2000
  idles in S0 - without it the primaries would read only "no regression".
- SUSPa-V on QEMU (version pinned in the run sheet): every step read separately
  - arm request, U3 held for a sustained interval (PORTSC read in XHCISNAP),
  wake event, WAIT_WAKE completion, D0, transfers resumed - because QEMU
  changes U3/U0 at once and wakes an endpoint whether or not remote wake was
  enabled, so returning input alone proves nothing. Legs: the test client on
  98 SE, 2000 (SMP, Driver Verifier), ME (STOP/START reuse) and XP; hidusb's
  own idle on XP, Vista and 7 with each target's HID selective-suspend
  setting found and recorded per target; behind QEMU's usb-hub; composite
  audio interleavings; unplug at each state; controller disable/enable, D3
  and restart mid-transition; the switch at 0 reading as the build before it; the matrix
  unchanged; x86 and amd64 builds and every gate.
- Fault injection (the `qemu` flavour only): the DMA-halt proof forced to fail while a test-client idle callback waits on cancelled I/O, then the controller's STOP, REMOVE and a restart, with the callback's return raced against the teardown's handoff, a claim raced against EXITING, and other PDOs' entries queued behind it; the quarantine holds, nothing hangs, and on 98 SE the driver stays loaded while the quarantined device object exists.
- Bench: a real HID device with remote wake on a 98 SE machine and the
  E460's Windows 7, by the owner. The phase's checkpoint includes it unless
  the owner rules otherwise (the Phases 28-31 bench exception does not carry
  over by itself).
- Docs: record 13 sections 5, 6.5, 10.2, 10.9; release notes (limitation
  replaced; what is still not done: SS until SUSPb, hub and controller idle);
  roadmap-hcd's selective suspend section and the 28.3 row;
  `source-files.md`; the locking record for the gate and the slot; the
  phase's run record.

## 11. Decisions

- The switch defaults to 1 (owner, 2026-10-05; section 7).
- Stage A's checkpoint includes the owner's bench reading (owner,
  2026-10-05): a real remote-wake HID device on a 98 SE machine and on the
  E460's Windows 7.
- The test client (SUSPa.6) is tracked under `test\`, never shipped, and the
  packaging gate refuses it in a release (owner, 2026-10-05).
- Stage B ships in the same release as stage A (owner, 2026-10-05): the
  release is cut only after stage B is designed (section 8 grown into the
  record proper, Codex-reviewed), built and read, its bench included.
