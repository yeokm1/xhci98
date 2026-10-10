# Selective suspend for the HCD

Design record for roadmap-hcd's selective suspend work (tasks
`SUSP.*`, `SUSPa.*`, `SUSPb.*`; drawn up as Phase 36, unnumbered from
2026-10-06, and since 2026-10-09 (owner) Phase 36's tasks 36.28 to 36.41:
SUSP.1 is 36.28, SUSPa.1 to SUSPa.6 are 36.29 to 36.34, SUSPa-V and
SUSPa-E 36.35 and 36.36, SUSPb.1, SUSPb.2, SUSPb-V and SUSPb-E 36.37 to
36.40, SUSP.2 is 36.41, and SUSP.3 and SUSP.4 joined 36.20 and 36.final.
This record keeps the old ids, and its review history the old phase
numbers). DRAFT, revision 10,
2026-10-05: written at the owner's request to solve the known limitation "the
driver never initiates selective suspend" (roadmap-hcd decisions table, row
"The idle power policy (28.3)"; release notes, "Known limitations"). Reviews
covered revisions 1 to 9 the same day (13, 12, 10, 9, 3, 4, 1, 2 and 1 findings, all taken;
section 9 maps each to where it is answered); the review of revision 10
was clean (`b2d03d5`). Revision 11, 2026-10-09: section 3.9, the pure
core's split and the rules implementing it settled (36.29), reviewed with
its code. Stage A is being built: 36.29 and 36.30 done; section 3.11,
dispatch's executor, with 36.31; section 3.12, the idle notification's
executor, with 36.32.

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
  events fall through (`xhci_evt.c:261`). (History since 36.38: with
  `XhciSelectiveSuspend` 1 DNCTRL enables FUNCTION_WAKE and the event has
  its own arm, section 8.7; at 0 both read as this row says.)
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
  drop (Configure Endpoint); 3.10 keeps the whole release owed instead. A
  client quiescing itself never forces a resume.
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

### 3.9 The pure core and the executor (36.29)

`src/xhci_susp.c` (host suite `test_susp`) holds every decision of sections
3 to 6 that is a function of values, so it is driven with no controller, no
IRP and no lock; the executors (`hcd_enum.c`, `hcd_hub.c`, `hcd_pdo.c`,
`hcd_urb.c`, 36.30 to 36.32) read the values under the controller lock,
ask, and perform the one step asked. What it decides:

- **The device's want**, from the slots the thread reads in one
  controller-lock hold (3.1): suspend when every PDO of the chain has a
  `PwrState` below D0; resume when the link is not ACTIVE and some PDO wants
  D0; the arming intent (5.4); and which pended IRPs that same pass
  completes - a pended Dx whose epoch is not the controller's (4.4), a
  pended Dx that is no longer the last (a sibling returned to D0 while the
  link stayed ACTIVE: logical Dx holds, 3.5), every pended D0 once the link
  is ACTIVE and the controller not `PowerDown` (4.3, 4.4; a D0 pended
  across the controller's transition is never completed by the walker
  that retires old-epoch Dx). A PDO that is closed or not started keeps
  its `PwrState`, D0 for one never powered down, so it holds the device
  awake: the conservative reading of 4.3's "any sibling" (a function
  without a driver, or disabled while D0, keeps today's behaviour for the
  device). The thread writes SUSPENDING in the **same** hold in which it
  read the slots that asked for it, so a D0 accepted after that hold sees
  a link that is not ACTIVE and pends (4.3), and one accepted before it is
  in the slots the thread read. A START that reopens a PDO (4.4) while its
  device's link is not ACTIVE - a function disabled and enabled on the
  same device, whose driver sent D3 before its REMOVE and so let the link
  suspend - writes `PwrWanted` D0 and leaves `PwrState` to the thread:
  the thread resumes for it as for a pended D0, writing `PwrState` D0 at
  ACTIVE with no IRP to complete, and its first device-side slow IRP waits
  in `SlowDeferred` meanwhile (3.3). 4.4's "`PwrState` = D0" at START is
  what this reads as when the link is ACTIVE, the revived PDO's case.
- **The admission of a D-IRP** at dispatch (4.2 to 4.4): complete at once or
  pend, and the `PwrState` written. Closed (`PwrClosed`, the controller
  released): complete at once. The switch at 0, a hub or a SuperSpeed PDO:
  today's (record, complete). A Dx while `PowerDown`, while a sibling is
  D0, or while the link is not ACTIVE (the PDO is already Dx there, 4.3's
  D2->D3): complete at once; otherwise pend, the last to leave D0. A D0:
  complete at once only when the link is ACTIVE and the controller is not
  `PowerDown`; otherwise pend. Every D-IRP, WAIT_WAKE and START dispatch
  accepts, and every cancel it settles, adds one to the slot's change
  count `PwrSeq` (controller lock); the machine's own completions do not.
- **The sequences of 3.4 and 3.6 as steps**: the executor begins a suspend
  or a resume, performs the step it is given and reports its result; the
  machine answers the next step or the end and its outcome (3.5). A
  refusal after the stop asks for the restart no sooner than 10 ms after
  the Stop Endpoint (SP=1's minimum, as 3.5). NOT_SENT at the arm is
  retried once: the repeated arm step carries a 100 ms bound on EP0's
  drain, which the executor's EP0-quiet wait must keep to, and a ring not
  drained by then is NOT_SENT again, a refusal. Before the U3 request the
  executor acknowledges a stale PLC (root) or C_PORT_SUSPEND (hub), and
  only wake evidence sampled once that request is issued belongs to this
  suspension - before it the link is U0 and a wake read then is an
  earlier suspension's, ignored. Such evidence is kept and taken when the
  observation reports U3, a wake, or its bound run out with nothing seen
  - its change may be acknowledged already, so it is not waited for
  again. A transport timeout (EP0, a hub request, a command) stays the
  uncertain outcome whatever was kept: the evidence settles whether the
  device suspended, not a control transfer left outstanding. A wake the
  observation itself reads is taken the same way. Either is
  the device-initiated resume: at a root port TDRSMDN from the PLC and
  then U0, at a hub the hub has timed it. A disconnect
  reported while a sequence runs is kept too, and ends the sequence GONE
  at its next step, ahead of any wake (3.7's precedence). The disarm
  after a verified U0 is best effort - a STALL, NOT_SENT or failure is
  counted and the resume goes on, the link being usable, but only a
  disarm the device took clears `Armed`, so the next resume tries again;
  a timeout is the uncertain outcome. The dwell's start is the time U3
  was observed. A refusal after a successful arm leaves the feature set:
  the link never left U0 and EP0 may be stopped, so no disarm is sent; the
  record keeps `Armed`, the next resume disarms, and a wake meanwhile is
  5.4's unclaimed one. A pended last Dx completes when the decision after
  the sequence finds the link not ACTIVE, or ACTIVE and not suspending
  again; a suspend that ended in a wake without a claim suspends again on
  the same pass and its Dx completes when that one ends.
- **After a wake that claimed**: each PDO whose WAIT_WAKE the wake
  completed gets `WokeHold` (set by the executor at the claim, in the same
  hold), and the device is not suspended again while any PDO holds it; a
  PDO's hold ends when its `PwrState` is D0 again - cleared by dispatch in
  the hold in which it writes D0, and by the thread when it does, so a D0
  and a fresh Dx between two passes still end it - or when it is closed
  or its device GONE. So the woken function's own D0, which its completion
  routine sends, finds the link ACTIVE instead of racing a re-suspend that
  would hold its input (5.4's "a remote wake ends it through the woken
  function's D0"), whatever its siblings do meanwhile. A function whose
  driver never sends that D0 leaves the device awake, which is today's
  behaviour. A wake that claimed nothing (`WakeUnclaimed`) and 5.4's rearm
  are internal cycles and suspend again on the same pass.
- **After a refusal** (3.5: "does not retry until the slots change"), and
  after an uncertain outcome that ended ACTIVE: the slots are latched, in
  the same controller-lock hold in which that ACTIVE became visible to
  dispatch (`XhciSuspLatch`), after the refusal's own slot edits in that
  hold (a STALL's WAIT_WAKEs claimed for NOT_SUPPORTED), by a digest of
  each PDO's
  `PwrSeq`, `PwrState`, `PwrWanted`, pended D0, held WAIT_WAKE and
  closure - a pended Dx is not in it, since the machine completes that
  itself - and no suspend is begun until the digest moves. The change
  count makes a D0 and a fresh Dx between two passes a change though the
  states read as before.
- **Wake evidence** (3.7, 3.8): the root port's sample and a USB 2.0 hub
  port's (status, the change mask acknowledged, the generation) classified
  in 3.7's precedence; the observation at (5); the verification at 3.6.
- **WAIT_WAKE** (5.3): the admission and its status, and the claim by stamp
  (signed 32-bit difference against the watermark, `PwrState` D1 or D2).
- **Capabilities** (5.2), for the switch, the device's speed, a hub, the
  configuration's remote-wake bit and 9x wake.
- **The idle notification** (section 6): the submission's answer, a
  batch's readiness, the claim at call time with its three branches, the
  settlement table of 6.5 and which events end an episode. A batch is not
  formed while a PDO outside the group (not started, or closed) is D0:
  that PDO holds the device awake (above), and a callback would send its
  siblings to Dx on a link that cannot suspend, with nothing to end their
  episode.
- **The callback context's phases** (4.4): which compare-exchanges each
  actor may make, and teardown's handoff step for a phase it observed.
  Teardown runs its step (1) once more after it wins EXITING or
  ABANDONED: the thread may have put an entry back as unclaimed after
  teardown's first (1) and gone IDLE before teardown read the phase, and
  once teardown has won, the thread touches the queue no more.
- **Finalization of a PDO** (4.4 (b) finish and (e)): its prerequisites
  are `UrbsPending`, the idle rundown and `PowerPending` (a D-IRP or
  WAIT_WAKE whose completion has not returned, a cancel routine's
  included, 4.2) all at 0. Owed with the buffer pinned (the DMA-halt
  proof failed, `CommonBufferPinned`, the thread alive or not):
  quarantine, since the pinned URBs never end. Otherwise owed: defer (the
  enum thread as caller), quarantine (the thread exited and no proof), or
  wait (a PnP caller).
- **Publication and claim of a cancellable IRP** (4.2): what the submitter
  does with the `irp->Cancel` test's two answers, and what a claimer owns
  for each answer of `IoSetCancelRoutine(irp, NULL)`; the callback
  thread's re-publication after a callback returns is the submitter's two
  steps again.

The pure core returns status codes of its own (`XHCI_SUSP_ST_*`), which the
executor maps to `NTSTATUS`; the D-states are `DEVICE_POWER_STATE`'s values.

### 3.10 The thread's executor (36.30)

36.30 builds everything the enum thread does (sections 3, 3.9 and the
thread's half of 5.4): the gate, the restart, the sequences at a root port
and a USB 2.0 hub port, the decision pass over the slots with the
completions it owes, the wake event's claim, the evidence at the root
port's acknowledgement and in the hub port look, and the slow-IRP
deferral. 36.31 builds the dispatch's half (D-IRP, WAIT_WAKE and START
admission, capabilities, teardown, the controller's transitions). Until
36.31 nothing fills a slot, and until 36.33 nothing sets the switch, so
36.30 is inert. In `src/hcd_susp.c`, with hooks in `hcd_enum.c`,
`hcd_hub.c`, `hcd_cfg.c` and `hcd_io.c`:

- **The pass** runs at the end of each powered service pass, with the
  switch on and the root hub started, over every addressed device that is
  not a hub, not SuperSpeed, and has a PDO. A device's chain has at most
  `XHCI_FUNC_MAX` (16) PDOs; the pass copies them into the controller's
  own scratch, not onto the enum thread's stack. In one controller-lock hold it
  copies the PDO chain's slots, decides (`XhciSuspDecide`), writes back
  `WokeHold` and the D0s, claims every IRP the decision completes and, for
  a suspend, begins it (`XhciSuspBegin`, SUSPENDING); the claimed IRPs are
  completed after the hold. A device that ended a sequence is decided
  again in the same pass, at most four times, so its last Dx, its D0s and
  an unclaimed wake's re-suspend are not left for a later wake-up.
- **Time.** The executor keeps its own clock rather than read the system
  time (`KeQuerySystemTime` is allowlisted, but a step's minimum is what
  matters, not a date). The machine is
  given a controller-wide millisecond count that advances only by the
  waits the protocol requires (the dwell, TDRSMDN, TRSMRCY, SP=1's
  minimum), and those are kept by `KeStallExecutionProcessor` in 50 us
  steps, since before Windows 8 a relative `KeDelayExecutionThread` may
  end up to a clock period early; they are tens of milliseconds and rare.
  Pauses between polls use `KeDelayExecutionThread` and are not counted.
  Time that passes elsewhere is not counted either, so every wait computed
  from the count is at least as long as asked. A bounded poll is bounded by a timer,
  as `hcdEp0Quiet` is: the observation and the verification report
  `EXPIRED` when it runs out, the repeated arm's drain NOT_SENT.
- **The arm and the disarm** go through `HcdThreadControlOutcome`. The
  first arm is not sent while EP0's queue holds a client's transfer or EP0
  is halted (NOT_SENT at once, no 5-second quiet wait, no Reset Endpoint
  inside the bound); the repeated arm waits for both to clear, polled,
  within its bound, and is NOT_SENT again if they have not. Outcomes: DONE is OK, STALLED is STALL, NOT_SENT and FAILED are
  themselves unless the device's EP0 is now stuck or the controller halted
  (a timeout, `HCD_CTL_NOT_SENT`'s other meaning), and ABANDONED is a
  timeout. `HcdThreadControlOutcome` refuses (NOT_SENT) any request on a
  SUSPENDED, RECOVERING or GONE device: no control traffic on a suspended
  link, on one whose state is unknown, or on one the machine saw leave,
  whoever asks.
- **The stop** is a Stop Endpoint with SP=1 for every endpoint the device
  context reads Running, EP0 included; Context State Error (already
  stopped) is success; a command that never answers is a timeout (the
  reset it requested follows); any other completion a failure.
- **U3.** At a root port: a stale PLC is acknowledged, then PLS=U3 with
  LWS; at a hub port: a stale C_PORT_SUSPEND is cleared, then
  SetPortFeature(PORT_SUSPEND). `XhciSuspU3Issued` precedes the request.
  The suspend request itself failing is a timeout - it may have taken
  effect, so nothing proves the link still U0 - and a hub request before
  it (the stale clear) failing is a timeout when the hub's EP0 is now
  stuck or the controller halted, else a failure.
- **The observation and verification** poll every millisecond (root
  PORTSC) or every 10 ms (hub GET_STATUS); an all-ones PORTSC or a GET_STATUS
  that fails is a timeout; a C_PORT_SUSPEND read is cleared.
- **The resume** at a root port is PLS=Resume, the 20 ms, PLS=U0 - QEMU
  ignores the first write and moves on the second (lessons.md); at a hub
  port ClearPortFeature(PORT_SUSPEND).
- **The uncertain outcome's recovery** is the device cycle
  (`HcdEnumCycle` on the device's PDO group): the device is dropped as an
  unplug drops it and enumerated afresh, a new device. So is a device the
  machine saw leave (a disconnect classified from a sample, or a sequence
  ended GONE): the port's own processing may not drop it - a USB 2.0 port
  that lost PED with CCS kept, at `XhciTolerance` 0 - and a cycle of a
  device the port also dropped is no change, the group naming it alone. That is the
  existing path that re-establishes a device whose state is unknown; the
  old device is GONE and its slots settled at the unlink. A controller
  timeout leaves the device to the controller's recovery, which
  invalidates it.
- **The restart** (3.2) rings, under the lock, every endpoint that the
  context reads Stopped, is not Halted, holds TDs and whose pipe holds no
  pause but the machine's (`Paused` 1), once per stream with work, then
  opens the gate. Then the device's deferred slow IRPs go back to the head
  of `SlowIrps`, in order, and the thread is woken. Every other doorbell
  that could restart a held device's survivors respects the gate: EP0's
  recovery (`hcdResetEp0`) rings nothing while the link is SUSPENDED,
  RECOVERING or GONE, and a hub's release of its subtree (`hcdHubReleaseBelow`,
  an ancestor port's resume) rings no endpoint whose pipe holds the
  machine's pause; the publish paths, the isochronous restart, a cancel's
  stream kick and the soft retry test `Paused` already.
- **Root-port evidence.** `hcdPortChanged` hands the sample - PORTSC as
  read, the change bits it acknowledged, the location's `ConnectGen` - to
  the machine after the acknowledgement, for a device the machine owns. A
  wake event's claim runs in that hold; the wake sequence runs in the
  pass. Connection processing goes on unchanged after it.
- **The hub port look** (3.8). For a USB 2.0 hub port whose device the
  machine owns: the auto-resume is skipped, and a finished resume
  (`Suspended`) hands its sample (status, the change bits the hub took
  back, and `ConnectGen`) to the machine instead of the quiesce, wait and
  release. A C_PORT_SUSPEND whose clear failed is no evidence, and since
  the hub may have taken it and report no more, the device is uncertain:
  RECOVERING at once (`XhciSuspUncertain`), so the pass neither resumes
  nor rearms it, then cycled, its gate kept closed.
- **Slow IRPs** (3.3). A slow IRP dequeued for a device whose link is not
  ACTIVE (SUSPENDED, or RECOVERING, whose device the cycle drops) is
  served if it is ABORT_PIPE, else moved to `dev->SlowDeferred` in order,
  cancellable by 4.2's handshake there (its cancel routine completes it
  CANCELLED; the undeferral and the flush take only what they cleared).
  A function release owed to such a device is kept owed whole until the
  device is ACTIVE - this replaces 3.3's split, whose host-side half (the
  endpoints' cancel, drain and drop) is not run while suspended: simpler,
  it forces no resume, and the release's endpoints are stopped meanwhile.
  A deferred IRP of a device that leaves completes DEVICE_GONE with the
  queued ones (`HcdCfgFlushDevice`).
  The survivor restart after a cancel rings no doorbell while the link is
  not ACTIVE; the machine's restart does.

### 3.11 Dispatch's executor (36.31)

The dispatch's half of sections 4 and 5 (`src/hcd_susp.c`, called from
`hcd_pdo.c`, `hcd_power.c`, `hcd_ctl.c` and `hcd_enum.c`); the idle
notification's parts of 4.4 and 6 are 36.32's. With the switch at 0, or for
a hub or SuperSpeed PDO, every answer is today's.

- **Reaching the controller** (4.2): the PDO's `Busy` count is raised
  before `pdo->Controller` is read, as the URB dispatch does, and the
  release waits it out; the power dispatch holds it until the IRP is
  completed or pended, so a STOP or REMOVE, which wait it out too, cannot
  pass a completion still to run. A WAIT_WAKE at a PDO whose controller is
  released is refused NO_SUCH_DEVICE when the switch was on at the PDO's
  creation or last START. The slot is written under `PdoListLock` then the
  controller lock (4.1's order), so `pdo->Device` and the device's PDO
  chain (for the composite's sibling test) cannot be unlinked meanwhile.
- **A D-IRP** (4.2 to 4.4): `XhciSuspAdmit` over the slot, the device's link
  state (GONE when the PDO is unlinked, which is also closed), `PowerDown`
  and the siblings. Every accepted D-IRP writes `PwrWanted`, `PwrState` as
  admitted, and one more `PwrSeq`; one that leaves `PwrState` D0 clears
  `WokeHold`. A pended IRP takes the slot, the epoch and a `PowerPending`
  count, and wakes the thread. A Dx is effective at once: its
  `PoSetPowerState` is made before the slot is filled, so no completion
  can overtake it. A D-state outside D0 to D3 is today's.
- **WAIT_WAKE** (5.3): `XhciSuspWaitWakeAdmit` with the configuration's
  remote-wake bit and the IRP's system state; accepted under the
  controller lock with `hc->WakeSeq`'s next value as its stamp, one more
  `PwrSeq` and a `PowerPending` count, then published by 4.2's handshake.
  Its cancel routine clears the slot under the controller lock and hands
  the completion to a system work item whose storage is the IRP's
  `DriverContext`, since a power IRP is completed at PASSIVE_LEVEL on
  Windows 98 (win98-wdm.md) and a cancel routine runs at DISPATCH_LEVEL.
  Wake on 9x is offered until 36.35 reads 5.5's defect.
- **Capabilities** (5.2): `XhciSuspCaps` over today's values; the
  latencies stay 0 until a target's usbhub values are read (5.2's "read
  per target", 36.35).
- **Teardown** (4.4 (a), (c)): STOP, SURPRISE_REMOVAL and REMOVE close
  admission and claim the slot's IRPs after the dispatches inside are
  waited out (`hcdPdoQuiesce`): the pended D-IRP completes SUCCESS, the
  WAIT_WAKE CANCELLED at a stop and NO_SUCH_DEVICE at a removal, a
  surprise one included. A REMOVE also waits for `PowerPending` 0, so a
  cancel routine's work item has returned before the client may unload.
  The idle rundown and 4.4 (b)'s split are 36.32's.
- **GONE at the unlink** (4.4): `HcdDevicePdoGone` claims and closes each
  PDO's slot in its `PdoListLock` hold and completes the claims after it
  (D-IRP SUCCESS, WAIT_WAKE NO_SUCH_DEVICE).
- **START** (4.4, 3.9): after a teardown (admission closed), once its
  power rundown has finished (`PowerPending` 0: a WAIT_WAKE its stop left
  to a cancel routine has completed); a START with admission open waits
  for nothing, since a live WAIT_WAKE may be pending. Then, in one
  `PdoListLock` and controller-lock hold, a PDO still listed with its
  device has admission reopened, D0 asked, `PwrState` D0 at once on a
  powered ACTIVE link and otherwise the thread's; one unlinked meanwhile
  stays closed and its START is refused. A START owes no D0 IRP, so for a
  stage A PDO the bus reports D0 itself (`PoSetPowerState`) once the
  device is powered: at once on an ACTIVE link, else by the thread when the
  resume it makes for the START reaches ACTIVE. A D-IRP's admission with
  its report, a START's report and the thread's START-driven report run
  while the PDO's report flag is owned (a test-and-set by
  `InterlockedExchange`, polled at 1 ms - not an event, whose wait Windows
  98 can end unsatisfied), so an older D0 report never lands after a newer
  Dx's; a pended D0's report is made at its completion, before
  `PoStartNextPowerIrp`, so the next D-IRP follows it.
- **The controller's transitions** (4.4): the D3 and the direct transition
  (`hcd_power.c`), the in-place recovery and the stop (`hcd_ctl.c`) move
  `PowerEpoch` and set `PowerDown` in one controller-lock hold; the
  thread's first powered service pass clears it. After the suspend, the
  power path itself completes every Dx pended in an older epoch
  (`HcdSuspSettleStale`), at PASSIVE_LEVEL with the controller quiesced;
  after a recovery the thread's decision pass does (`XhciSuspDxStale`);
  the stop's teardown settles every slot at its unlink.

### 3.12 The idle notification's executor (36.32)

Sections 6 and 4.4's idle parts, in `src/hcd_susp.c`, called from
`hcd_urb.c` (the submission), `hcd_pdo.c` (teardown, deletion, revival,
the system power IRP), `hcd_ctl.c` (the callback thread's start and stop)
and `hcd_entry.c` (the unload). With the switch at 0, or at a hub or
SuperSpeed PDO, the idle IRP is held as before (`hcd_urb.c`, its own slot
`IdleIrp` under the cancel spin lock) and no callback thread exists.

- **The slot** is the PDO's `IdleSlot` with `IdleState`, the callback and
  its context copied at submission, `IdleBatchCur`, the queue entry
  (`IdleEntry`, with its batch and epoch, `IdleQueued` while on the queue,
  `IdleEntryOwned` while queued or claimed) and `IdleOwed`, all under the
  controller lock. A submission while the slot, the entry or today's
  `IdleIrp` is owned is BUSY. `PwrStarted` (set at START's reopening,
  cleared at teardown) is 6.2's "started".
- **Triggers** (6.5) are applied where the state they name is written:
  the cancel routine (PdoListLock, then the controller lock; a cancel of a
  QUEUED or RETURNED IRP ends the episode); teardown and the unlink
  (`hcdSuspSlotCloseLocked`: CANCELLED, the PDO's own queued entry
  detached); the system SET_POWER at the PDO for a state other than S0
  (CANCELLED, the episode ends); D3 accepted at dispatch (POWER_STATE_
  INVALID, completed after the PDO's report flag is let go, since a
  completion routine may send a D-IRP at once); `PwrState` written D0
  from a Dx by dispatch or by the thread (SUCCESS, the episode ends). A
  trigger that finds the IRP RUNNING is kept in `IdleOwed` (the strongest,
  by the table's order) for the callback's return; a cancel then comes
  first.
- **The episode's end** (`hcdSuspIdleEndLocked`) moves every PDO's
  `IdleBatchCur` to 0 and, the link ACTIVE and no callback of the device
  running, settles every RETURNED sibling in D1 or D2 SUCCESS; with one
  running, its return does it. The refusal and the uncertain outcome that
  end ACTIVE end it in the sequence's last hold.
- **The batch** (`hcdSuspIdleBatchLocked`) is formed by the thread's pass
  for a device it decided nothing to run for, when 6.2's readiness holds
  (`XhciSuspIdleBatchReady`), every PDO in S0, the controller up and the
  callback thread running; each entry takes an `IdlePending` reference.
  The composite logic - readiness, the episode's end and its sibling
  settlement - lives in those two functions, so the composite-parent
  devnode of task 36.43 (owner, 2026-10-10) can take it over.
- **The callback thread** (one per controller, started before the enum
  thread while the switch is on) wins CLAIMING from IDLE, takes the
  queue's head under PdoListLock and the controller lock and claims it
  (`XhciSuspIdleClaim`); RUN calls the callback in IN_CALLBACK; at its
  return RETURNING settles the IRP (its cancel, else `IdleOwed`) or
  re-publishes it RETURNED, then settles the siblings of an episode that
  ended meanwhile; IRPs are completed and references dropped after the
  phase is IDLE again. The phase is moved under the context's own spin
  lock, since no compare-exchange import is admitted (the same single
  winner). An abandoned thread completes its IRP CANCELLED, drops the two
  references and leaves; its context, with its thread object on NT, stays
  on a driver-wide list that the unload waits out and frees (4.4's
  "driver-global list"; the context is not freed by the thread itself).
  A join waits on the thread object on NT and, on 9x, polls a flag the
  thread sets just before `PsTerminateSystemThread`, since an event wait
  there may return unsatisfied (`win98-wdm.md`). The system state a batch
  and a claim read is published under the controller lock in the hold
  that applies the system trigger (`HcdSuspIdleSystem`).
- **Teardown** of a PDO keeps 4.4's order: (a), (c) and (d) together
  (`HcdSuspPdoClose`), then (b) (the URB drain as before, after the PDO's
  slow IRPs deferred behind a suspended link are detached and completed
  CANCELLED, `HcdCfgFlushPdoDeferred`; a Closing PDO's IRP is never
  deferred again), then (e)
  (`HcdSuspIdleRundown`): a PnP caller waits for `IdlePending` 0 unless a
  callback of the PDO's still runs without the DMA-halt proof
  (`CommonBufferPinned` now, or `IdlePinned`, the verdict of the stop that
  abandoned it) - then the PDO is
  quarantined (`IdleQuarantined`, counted): kept, never revived
  (`hcdDormantFindLocked`), never started again. 4.4 (b)'s three-way split
  of the URB drain is not needed by this tree: every deletion
  (`hcdDeletePdo`) follows a REMOVE whose quiesce ran the rundown, or is of
  a PDO PnP never saw, with no client to submit anything - so the enum
  thread, which deletes only the latter, never waits on a callback, and a
  rundown found unfinished at a deletion is the quarantine (the object is
  kept) rather than a deferred-delete list.
- **The thread's completions** (36.35's fix): the enum thread, and the
  power path holding the power gate, complete no power or idle IRP
  themselves. A completion runs the client's code, which on Windows 98
  may ask for another power IRP and wait for it - and that IRP may need
  the thread (98 SE: a wake's WAIT_WAKE completed in the pass, the
  client's callback asked D0 and waited, and the thread never finished
  the resume). Every IRP they claim is completed at PASSIVE_LEVEL by a
  system thread of its own (`hcdSuspDefer`), never a shared worker: a
  completion callback that asks for a D0 and waits, whose D0 callback asks
  for a Dx and waits, and so on, holds only its own thread, and the
  completion it waits for always has one (a single worker, then the two
  system work queues, could each be held by the callback waiting on them).
  The stop joins every completion thread (`HcdSuspDoneDrain`) - on NT by
  its thread object, on 9x by the flag it sets last - before the
  controller's storage goes or the image can unload. The claimed status
  is fixed at the claim; the IRP keeps its `PowerPending` or `IdlePending`
  count until its completion has returned.
- **The controller's stop** runs the handoff (`HcdSuspCbStop`) after the
  enum thread has exited and the devices are dropped (their slots settled
  at the unlink): steps (1) to (5) as 4.4 has them.

## 4. Function power state at the device PDO

### 4.1 Fields, writers and locks

All under the **controller lock** (innermost, record 13 section 5.4):

| Field | Meaning | Writers |
|---|---|---|
| `pdo->PwrIrp` | the one pended D-IRP | dispatch (set), whoever claims it (clear) |
| `pdo->PwrWanted` | the D-state asked | dispatch |
| `pdo->PwrState` | the D-state the bus acts on | dispatch (a Dx: at once; a D0 on a powered ACTIVE link: at once), thread (D0: at ACTIVE) |
| `pdo->PwrClosed` | admission closed | teardown |
| `pdo->PwrSeq` | the slot's change count (3.9) | dispatch, cancel |
| `pdo->WokeHold` | a wake completed its WAIT_WAKE (3.9) | thread (set, clear), dispatch (clear, when it writes D0) |
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

The functions are the function PDOs under the device's composite parent
(record 13 section 10.12, task 36.43): the parent heads the chain, carries
no client and stays D0, and is no voter here or in 6.2 - every walk that
reads the functions' states skips it, and the chain cap counts functions
only. A function's Dx completes at once while any sibling function's
`PwrState` is D0. The last function to leave D0 has its D-IRP pended; the thread suspends and
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
    entry it put back as unclaimed is then drained). (5) Having won in (2)
    or (3), run (1) once more: an entry put back after the first (1) by a
    thread that went IDLE before teardown read the phase is drained then,
    the thread touching the queue no more (3.9).
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
configuration sets bmAttributes bit 5. Hub PDOs (task 33.4) and
composite-parent PDOs (task 36.43) are excluded from capabilities,
WAIT_WAKE, idle callbacks and suspend requests, so every ancestor stays
active; the devices behind them, and a composite parent's functions, are
not excluded. A composite parent's D-IRPs are recorded and completed at
once; nothing above its FDO asks one, so its devnode reads D0 while its
device is suspended (the batches stay the bus's - orchestrator decision,
owner to confirm).

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
  whose `PwrState` is D1 or D2, and **completes them `STATUS_SUCCESS`** -
  the claim and its status fixed in that hold, the completion itself made
  by a system thread of the IRP's own (3.12, the thread's
  completions), so it may land before, during or after the resume - a WAIT_WAKE completes when the wake is
  signalled, as usbhub's do. Between the claim and that completion the
  IRP is its completion thread's alone, its status already fixed, and a resume
  that then fails or a GONE that follows changes nothing for it: the function's D0 pends until ACTIVE or GONE
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

A non-hub USB 2.0 device PDO in stage A, and a SuperSpeed device PDO with
stage B (section 8.8; task 36.38). A hub PDO's idle IRP is held exactly as
today, its callback never called - and so is a SuperSpeed PDO's until stage
B's code is built. Callbacks run on a
thread of the bus's own, one per controller, separate from the enum thread
and from the system worker pool (a callback may block on its D-IRP; pool
threads are few on 98), one at a time, at PASSIVE_LEVEL, holding no bus
lock or gate. Its queue holds PDO entries pinned by `IdlePending`, never a
device pointer. The enum thread never waits on it except through the
bounded, deferring rundown of 4.4.

### 6.2 Batches (composite readiness, as the composite parent does)

The thread forms a **batch** for a device when every started function PDO
under the device's composite parent (record 13 section 10.12; the parent
itself is skipped) - or the device's one PDO when it is not split - holds
an idle IRP and has `PwrState` D0: it increments the
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
start like `XhciFirstEnumWaitMs`. **Default 0** (owner, 2026-10-09, when
the work joined Phase 36; it was default 1 from 2026-10-05, over review
round 2's advice of 0): 0 is today's behaviour bit for bit (capabilities,
WAIT_WAKE refused, idle callbacks never called, Dx recorded only), so the
release that carries sections 3 to 6 changes nothing for a user who does
not set the value, and 1 turns them on - what the release notes tell a user
who wants it. The INF writes nothing; the
INF gate's SUSP-* rules stand. Built in 36.33: read in the start's value
pass, before `XhciInitController`, so stage B's controller setup (DNCTRL)
follows it as well; only a REG_DWORD of 1 is on, and it turns stages A and
B on together; absent, 0, any other number or another type is off. The
read and the value in effect are counters (`SuspSwitchRead`,
`SuspSwitch`) and notes (`susp.switch.value`, `susp.switch`).

## 8. Stage B: SuperSpeed devices (36.37 to 36.40)

Stage B extends sections 3 to 6 to a SuperSpeed device - one whose PDO's
`SpeedClass` is `XHCI_SPEED_SUPER` (SuperSpeed and SuperSpeedPlus share it)
- on a USB3 protocol root port or behind a SuperSpeed hub. It is built on
stage A's machine as it stands after 36.31 (`xhci_susp.c` and `hcd_susp.c`
at `1194d9e`): the slots, the admission, the decision pass, the gate, the
deferral, the episode, the idle notification and every teardown rule are
stage A's unchanged. What differs is how the link is suspended and resumed,
how wake is armed and disarmed, and where wake evidence comes from. A
device that enumerated on a USB 2.0 port, the USB 2.0 half of a USB 3 hub
included, is stage A's whatever it could do at SuperSpeed. Hubs stay
excluded (5.1), so every ancestor stays in U0.

### 8.1 What USB 3 changes (the references)

- **Device Suspend is the link in U3 and nothing else** (USB 3.2 9.1.1.6,
  printed p.322; 9.2.5, p.325). There is no device-level wake
  feature: DEVICE_REMOTE_WAKEUP "is ignored and not used by Enhanced
  SuperSpeed devices", and remote wake is enabled "when any function within
  a device is enabled for function remote wakeup" (9.2.5.4, p.326). The
  device status's Remote Wakeup bit is reserved at SuperSpeed (9.4.5,
  p.337).
- **Function Suspend and function remote wake are one feature**:
  SetFeature(FUNCTION_SUSPEND), selector 0 with an interface recipient
  (bmRequestType 01h; Table 9-7, p.333), wIndex's low byte the **first
  interface of the function**, its high byte the Suspend Options of Table
  9-9 (p.343): bit 0 Low Power Suspend state, bit 1 Function Remote Wake
  enabled, bits 7:2 reserved (9.4.9, p.342-343). SetFeature with options
  0 returns the function to normal operation and disables its remote wake;
  the bus never sends ClearFeature(FUNCTION_SUSPEND). Function Suspend is
  kept through Device Suspend's entry and exit (9.2.5.3, p.326), so a
  function put there stays there after U0 until the bus takes it out.
- **The capability is per function**: GetStatus() to the function's first
  interface (bmRequestType 81h, wValue 0, wIndex the interface, wLength 2)
  answers Function Remote Wake Capable in bit 0 and Function Remote Wakeup
  (enabled) in bit 1; any other interface of a function answers zeros, and
  the enable resets to 0 when the function is reset (9.4.5, Figure 9-5,
  p.338). Only a Configured device answers an interface recipient (Address
  state: Request Error, p.337). The configuration descriptor's
  bmAttributes D5 still says whether the configuration supports remote
  wakeup (9.6.3, p.361).
- **A function** is an IAD's interfaces or a single interface; a function
  of more than one interface must have an IAD (9.6.4, p.361-362).
- **The wake**: a suspended device drives the link out of U3 (LFPS, 7.5.9.2,
  p.193: a handshake not answered in 10 ms leaves it in U3 and it tries
  again 100 ms later) and then sends a FUNCTION_WAKE Device Notification
  TP whose Interface field is "the first interface in the function that
  caused the device to perform a remote wake" (8.5.6.1, Table 8-19,
  p.238; 8.5.6.4, p.239). A function repeats the notification every
  tNotification until it is accessed, and each function that wakes sends
  its own (9.2.5.2, p.325; 9.2.5.4, p.326; 8.13, Table 8-36, names
  tNotification, p.314).
- **The xHC reports the notification only when asked**: DNCTRL (xHCI 1.2c
  5.4.4, p.366-367) enables one Device Notification type per bit, N1 being
  FUNCTION_WAKE; "only under debug conditions would software write the
  DNCTRL register with a value other than 0002h"; after reset all are
  disabled. A received FUNCTION_WAKE TP then raises a Device Notification
  Event TRB (type 38; 6.4.2.7, p.447-448): Slot ID in DW3 31:24,
  Notification Type in DW0 7:4, and DW0 15:8 (TRB byte 01h = the TP's byte
  05h) the Interface field; its completion code is always Success. It goes
  to the event ring the slot's Interrupter Target names (4.13, p.229-230;
  4.13.2, p.232).
- **A USB3 root port's resume is one write.** Host-initiated: PLS=U0 with
  LWS from U3, no Resume write and no TDRSMDN (4.15.2.2, p.256-257).
  Device-initiated: the port enters Resume (PLS 15) with PLC and the xHC
  holds the LFPS handshake "until directed by software", which writes U0 at
  once - no TDRSMDN (4.15.2.1, p.255-256). Either ends in U0 with PLC.
  Software resumes a port it suspended only once it reads PED 1 and PLS 3
  (4.15.2.2), and not sooner than 10 ms after it did (6.4.3.8, p.454).
- **Stop before Function Suspend.** "Software shall stop the endpoints of a
  device associated with the function by using the Stop Endpoint Command
  and setting the Suspend (SP) flag to '1' prior to issuing a
  SetFeature(FUNCTION_SUSPEND)", and ring them again after the function
  resumes (4.15.1.2, p.255); an endpoint stopped with SP=1 "shall be
  stopped for at least 10 ms" (6.4.3.8, p.454).
- **A SuperSpeed hub port** suspends with SetPortFeature(PORT_LINK_STATE)
  and U3 in wIndex 15:8, and resumes with the same request and U0; the hub
  drives the U3 exit (USB 3.2 10.10, p.423). C_PORT_LINK_STATE is set when
  a U3 -> U0 the request asked for completes, and **not** for one a
  device's remote wake caused (10.16.2.6.2, p.449; record 13 section
  10.2). A hub whose upstream link is not in U3 - the bus never suspends a
  hub - answers its device's wake signalling on the downstream link itself
  (10.10, p.423). So behind a SuperSpeed hub the device's Function Wake
  notification is the only wake evidence the bus receives. The hub's
  PORT_REMOTE_WAKE_MASK concerns the hub's own wake on connect, disconnect
  and over-current and is not used (data-structures section 11.7).
- **No resume recovery interval at SuperSpeed**: the device may be
  accessed at once (9.2.6.2, p.327).

### 8.2 Eligibility and the switch

`XhciSelectiveSuspend` 1 turns on both stages; there is no separate
SuperSpeed value (orchestrator decision, owner to confirm, 8.13). With the
switch at 0 nothing of this section runs: capabilities and WAIT_WAKE are
today's, DNCTRL keeps its reset value 0 (the restore writes 0, as today),
no GetStatus(interface) is sent, and a Device Notification Event, should
a controller raise one, is counted by type and dropped as today. With the
switch at 1, `XhciSuspEligible` admits a SuperSpeed device PDO that is
not a hub; `XHCI_SPEED_UNKNOWN` stays excluded (today `hcdSuspServes`
reads every class but Low, Full and High as SuperSpeed).

### 8.3 Functions, interfaces and the capability

The bus's function PDOs (record 13 section 10.8) and USB 3's functions do
not always coincide: a PDO split by IAD is one USB 3 function, but a PDO
grouped by the positional audio rule spans several single-interface
functions, and an unsplit device's one PDO spans every function of its
configuration. So the device keeps two interface masks (bit n =
bInterfaceNumber n, as `XHCI_FUNC.InterfaceMask`), written by the thread
under the controller lock:

- `dev->FwFirsts`: the first interface of every USB 3 function of the
  active configuration - each IAD's bFirstInterface, and every interface
  (alternate 0) no IAD covers. A pure helper beside `XhciFuncSplit`
  computes it from the configuration descriptor; an interface number above
  31 cannot be named by the mask, so its function is never armed and the
  configuration is counted (8.10).
- `dev->FwCapable`: the subset whose GetStatus(interface) answered bit 0
  set. **Read by the thread after every SET_CONFIGURATION with a nonzero
  value it completes** in a configuration transaction (below) - the
  client's SELECT_CONFIGURATION of an unsplit device and the bus's own
  configuration of a device it splits; RESET_PORT's replay keeps the
  masks unread (below) - one GetStatus per bit of `FwFirsts`, through
  `HcdThreadControlOutcome` while the device is ACTIVE. A STALL, a failure
  or a short answer is "not capable" and counted; a timeout follows the
  thread's ordinary EP0 rules. A new device starts with both masks 0.

**The change of configuration** is one transaction with one entry and
one exit, for each of the two paths that send SET_CONFIGURATION:

- the client's select of an unsplit device (`hcdCfgSelect`): its
  **entry** is the top of the function, before `hcdCfgDeconfigure` and
  before `ConfigValue` and `Selected` are cleared; its **exit** is every
  return, success or failure;
- the bus's configuration of a device it splits
  (`HcdCfgParentConfigure`), before any function PDO exists: entry and
  every return likewise.

At the entry, both masks are cleared under the controller lock, so a
WAIT_WAKE that arrives while the configuration changes is refused
(below). At a successful exit after a nonzero configuration, the reads
are made and both masks written; at any other exit (a failure anywhere,
or SET_CONFIGURATION(0)) they stay 0 - the device's configuration is not
one the bus can vouch for. Either way, in the hold that writes (or
confirms) them, every PDO of the device that holds a WAIT_WAKE and is
left with no wake function has it claimed (4.2's handshake) and
completed `STATUS_NOT_SUPPORTED` after the hold - a machine completion,
which adds nothing to `PwrSeq` (3.9). A function PDO's own select
(`hcdCfgSelectFunction`) and SELECT_INTERFACE send no SET_CONFIGURATION
and leave the masks alone. **RESET_PORT** (`hcdCfgResetPort`) replays the
configuration on the same device - its device descriptor compared byte
for byte - so the masks are kept and not re-read: a function's remote-
wake capability is a property of the configuration, which the replay
does not change, and only its enable resets with the device (9.4.5). A
replay that fails cycles the device, and the new device starts with 0. A
configuration change and a RESET_PORT run only on an ACTIVE link (they
are device-side slow IRPs, deferred otherwise, 3.3), so neither meets an
armed function (8.5).

**The mapping.** A PDO's **ownership mask** is its `InterfaceMask` when it
is a function PDO (`pdo->Function`, set by the split), and every interface
(all 32 bits) when it is the PDO of an unsplit device, whose
`InterfaceMask` is 0 (`hcdPdoNew`). A PDO's functions are `FwFirsts` and
its ownership mask; its **wake functions** `FwCapable` and its ownership
mask. The split makes the function PDOs' masks disjoint, and an unsplit
device has one PDO, so an interface names at most one PDO. A PDO grouped
by the positional audio rule owns several single-interface functions and
arms each that is capable.

- **Capabilities** (5.2) are stage A's `XhciSuspCaps`, unchanged, from the
  configuration's D5, since QUERY_CAPABILITIES may come before any
  configuration: `DeviceD2` TRUE; with D5, `DeviceD1` TRUE and wake from
  D1 and D2 (`DeviceWake` D2, `SystemWake` S0), unless 9x's wake is off
  (5.5); without D5, `DeviceD1` FALSE and no wake. No GetStatus is sent
  for them.
- **WAIT_WAKE** (5.3) at a SuperSpeed PDO takes, for `XHCI_SUSP_WW_IN`'s
  `RemoteWake`, whether the PDO has a wake function - read in the
  admission's controller-lock hold - in place of D5: a PDO of an
  unconfigured device, of a device whose configuration is changing, or
  none of whose functions is capable, is refused `STATUS_NOT_SUPPORTED`.
  The mismatch this allows (D5 set, no function capable) answers a
  client's wake request with a legal refusal. 9x follows 5.5 unchanged.

### 8.4 The suspend sequence at SuperSpeed

The link record gains `Ss` (the device's link is a USB3 protocol link: a
SuperSpeed device is always on one), set with `Root` when a sequence
begins. 3.4's sequence becomes, at SuperSpeed:

1. the gate closed (3.2), as stage A;
2. **stop the functions' endpoints**: Stop Endpoint with SP=1 on every
   endpoint the device context reads Running **except EP0**, streams'
   endpoints included - a new step, `XHCI_SUSP_STEP_STOP_FUNCS`, appended
   after `XHCI_SUSP_STEP_END`'s value so no step is renumbered (4.15.1.2);
   it records `Stopped` and the stop's time as STOP does;
3. **arm**, when the decision's intent says so (5.4, per function below):
   for each PDO the decision marked `ArmNow` (a WAIT_WAKE held, D1 or D2,
   and at least one wake function, 8.8), and for each of its wake
   functions in ascending interface order,
   SetFeature(FUNCTION_SUSPEND) with options 03h (Low Power Suspend and
   Function Remote Wake enabled), through `HcdThreadControlOutcome` on EP0,
   which is still running. Each request the device takes sets that
   interface's bit in the PDO's `FwArmed` (a thread-only mask). At
   SuperSpeed `Armed` is derived, never assumed: before reporting the
   step's result the executor tells the pure core whether any PDO of the
   device has `FwArmed` set, and that alone sets or clears `Armed` - the
   step's OK does not, so an arm that found nothing to send (the masks
   changed since the decision) leaves the suspension unarmed and the next
   decision recomputes. The first request not taken ends the step with its
   result, mapped as stage A's arm (3.10): STALL, NOT_SENT (with the one
   retry after EP0 drains, bounded 100 ms), FAILED, or a timeout. A
   function without WAIT_WAKE is sent nothing: Function Suspend is used
   here only to carry the wake enable, and Device Suspend suspends every
   function anyway (9.2.5, p.325);
4. **stop EP0**: stage A's STOP step, which stops whatever the context still
   reads Running - EP0 at this point;
5. U3, the observation and SUSPENDED: at a root port stage A's steps
   unchanged (PLS=U3 with LWS after a stale PLC is acknowledged; the
   observation reads PLS U3, or Resume as a wake); at a SuperSpeed hub port,
   8.6.

An unarmed suspend runs steps 2 and 4 back to back; one stop path serves
both. The refusals (3.5) are stage A's with one addition: **a refusal at
SuperSpeed after some function was armed disarms before the restart.**
Stage A leaves DEVICE_REMOTE_WAKEUP set after such a refusal because the
feature is harmless on an awake device; Function Suspend is not - a function
left in it under an ACTIVE device holds its I/O. So the refusal's path is
DISARM (8.5's disarm, its control transfer waiting out SP=1's 10 ms from
the last stop, since it rings EP0) and then RESTART. A STALL at any
function's arm is stage A's STALL refusal for the device: every held
WAIT_WAKE of the device that a latched wake has not claimed first (below)
completes `STATUS_NOT_SUPPORTED` (the device said
in GetStatus that the function could wake, and then refused; it is not
offered again until the slots change, 3.9's latch). Function Wake
notifications latched during a suspension that ends in a refusal are
taken in the hold that makes ACTIVE visible (8.7, consumption (c)), in
this order: the latch is emptied and, if it held any, the wake's claim is
made as 5.4's (every WAIT_WAKE stamped before the watermark read then,
its PDO D1 or D2, `STATUS_SUCCESS`, `WokeHold` set); then a STALL's
claim takes what remains (`STATUS_NOT_SUPPORTED`); then `XhciSuspLatch`.
`hcdSuspEndLocked` gains the first of the three ahead of the two it has.
The device is awake, so nothing more is owed to the link. A suspension
that ends uncertain or GONE drops its latch: the cycle or the unplug
completes the device's WAIT_WAKEs `STATUS_NO_SUCH_DEVICE` at the unlink
(4.4).

### 8.5 Resume, wake and the disarm at a root port

- **Host-initiated** (a D0, or a rearm): the dwell (10 ms after U3 was
  observed), then the SIGNAL step writes PLS=U0 with LWS - `XhciLinkDecide`
  with `XHCI_LINK_WANT_RESUME` asks `XHCI_LINK_ACT_SET_U0` for a port in
  U3, or in Resume if the device began its own wake meanwhile (below) -
  and the sequence goes straight to
  VERIFY: there is no U0 step and no TDRSMDN at SuperSpeed.
- **Device-initiated**: a PLC with PLS=Resume is the wake event exactly as
  in stage A (3.7, the root sample), with its claim (5.4). **At SuperSpeed
  every wake branch is the host's resume**: whatever took the event - the
  root sample, the observation reading Resume, or a Function Wake
  notification (8.7) - the sequence goes to the SIGNAL step after what is
  left of the dwell since U3 was observed, then VERIFY. At a root port
  SIGNAL writes U0 whether the port reads U3 or Resume (WANT_RESUME,
  below): in Resume it completes the device's LFPS handshake (4.15.2.1
  step (d)), in U3 it is a host-initiated resume (4.15.2.2), and the
  dwell keeps 6.4.3.8's 10 ms in either case - a notification can be
  sent before U3 by an armed function (9.2.5.4), so it proves nothing
  about the link. The cost is at most 10 ms on a wake that follows U3 at
  once.
- **VERIFY** at a SuperSpeed root port reads PLS U0 with CCS and PED (the
  bus programs no U1 or U2 timeout, so U0 is the only enabled state a
  resumed link reads); its disconnect test is stage A's (a link that falls
  to SS.Inactive has CCS 0, data-structures 10.4).
- **RECOVERY** keeps stage A's 10 ms, a policy margin: USB 3.2 asks for none
  (9.2.6.2).
- **The disarm**: for every PDO of the device with `FwArmed`, for each set
  bit, SetFeature(FUNCTION_SUSPEND) with options 0; a request the device
  takes clears its bit, and `Armed` clears when no PDO has one left. Since
  Function Suspend outlives the U3 exit (9.2.5.3), **at SuperSpeed the
  disarm is not best effort**: any result but OK - STALL, NOT_SENT, FAILED
  or a timeout - is the uncertain outcome (RECOVERING, the device cycled,
  3.10), because a function left suspended under an ACTIVE device would
  hold its client's I/O with nothing to end it; a cycle re-enumerates it
  with every function in normal operation (Function Remote Wakeup resets
  with the function, 9.4.5). Stage A's best-effort disarm stays for USB
  2.0. With the disarm strict, no PDO keeps `FwArmed` while its device
  is ACTIVE, so a RESET_PORT or a SET_CONFIGURATION (which may reset a
  function's wake enable) never meets an armed function.
- **`FwArmed`'s lifetime.** It describes the device the PDO is linked to.
  Besides a disarm the device took, it and `ArmNow` are cleared when the
  PDO is unlinked from its device (GONE, in the hold that settles its
  slot, 4.4) - an unplug, a cycle, the controller's invalidation - since
  the hardware's wake enable did not survive the device; and a dormant
  PDO revived for a new device (98 and ME's `hcdDormantRevive`) has both
  zero: they are cleared in `hcdDormantKeep`'s detach loop, where
  `pdo->Device` is cleared at the controller's stop (the enum thread has
  exited, so nothing else writes them), and asserted zero where the
  revived PDO's admission reopens. A PDO's STOP, REMOVE or
  START on a device that stays linked clears nothing: the device may be
  SUSPENDED with that function armed, the hardware's enable stays set,
  and the device's next resume disarms it through the PDO's `FwArmed`
  whether the PDO is started or not (the disarm is the thread's, on the
  chain it owns, 3.1).
- Then RESTART, the gate opening at ACTIVE (3.2): 4.15.1.2's "ring an
  endpoint's doorbell to restart it" is 3.2's restart.

**`xhci_link.c`.** `XhciLinkClassify` reads PLS=Resume on a USB3 port as
`XHCI_LINK_UNKNOWN` (Resume was a USB2 encoding when it was written), and
`XHCI_LINK_WANT_RESUME` accepts only SUSPENDED. The classification stays
as it is: every caller that runs with the switch at 0 - the enumeration's
reset (`hcdUsb3ResetWrite`, WANT_RESET), the inspection
(`XhciLinkPortFeed`, WANT_SERVICE), the hold (WANT_HOLD, WANT_RELEASE) -
keeps today's answer for a port in Resume (none), and `LastState`, which
the logs and the snapshot read, keeps today's value. The one change is in
WANT_RESUME, which nothing calls before stage B: it also answers SET_U0
for a port whose PORTSC reads PLS=Resume with CCS and PED (a device-
initiated U3 exit the xHC holds for software, 4.15.2.1), read from the
PORTSC it is given, not from a new class. The driver's choice of a warm
reset for a link in U3 under the enumeration's reset is unchanged and is
policy: xHCI 1.2c 4.19.1.2.9 (p.286) lets PR or WPR start from any Enabled
substate.

### 8.6 SuperSpeed hub ports

The hub half follows stage A's USB 2.0 hub path (3.4 to 3.8, 3.10) with
the SuperSpeed hub's requests and status (data-structures sections 11.4
and 11.5; `xhci_sshub.h`):

- **U3**: a stale C_PORT_LINK_STATE is cleared first (stage A clears a
  stale C_PORT_SUSPEND), the window opened if no arm opened it (8.7),
  `XhciSuspU3Issued`, then
  SetPortFeature(PORT_LINK_STATE) with U3 in wIndex 15:8. The request
  failing is a timeout, as stage A's.
- **The observation**, GET_STATUS(port) every 10 ms within 50 ms: the link
  state U3 with PORT_CONNECTION and PORT_ENABLE (which stays set in U3) is
  OK; no connection, C_PORT_CONNECTION, PORT_ENABLE clear, or the link in
  eSS.Inactive, Rx.Detect or eSS.Disabled is a disconnect; anything else
  polls again. A wake cannot be read here - the hub raises no change for
  it - so a wake during the observation is the Function Wake notification
  (8.7). The observation empties the device's latch at each poll (8.7,
  consumption (b)); any bit ends it with WAKE, which is the wake branch -
  at SuperSpeed the host's resume (8.5), so it is right whether the link
  is still in U3 (a notification an armed function sent before the U3
  request took effect) or back in U0. An observation that expires with
  the link in U0 and nothing latched is uncertain (RECOVERING, the
  cycle), as stage A's.
- **Host resume**: the dwell, then SetPortFeature(PORT_LINK_STATE) with U0
  (the SIGNAL step), then VERIFY: GET_STATUS until the link reads U0 with
  PORT_CONNECTION and PORT_ENABLE, with or without C_PORT_LINK_STATE (a
  device that woke itself meanwhile leaves the link in U0 and sets no
  change, and the hub ignores a U0 request on a U0 link); a
  C_PORT_LINK_STATE read is cleared, and its clear failing is a timeout,
  as stage A's C_PORT_SUSPEND. Then RECOVERY, the disarm (8.5), RESTART.
- **Device-initiated**: the notification is the event (8.7), and the wake
  branch is the host resume above (dwell, SetPortFeature(PORT_LINK_STATE)
  with U0, VERIFY): the hub ignores the request on a link the device's
  wake already brought to U0, and resumes one still in U3. Stage A's
  hub wake branch (RECOVERY at once) stays for USB 2.0.
- **The hub port look** (3.8 at SuperSpeed). `HcdHubPortLook`'s SuperSpeed
  branch gets stage A's owned-port rule: for a port whose device the
  machine owns, `XhciSsHubPortDecide`'s `Resume` (a link in U3 under a held
  device, record 13 section 10.2) is skipped, and its `Resumed` (a
  C_PORT_LINK_STATE with the link back in U0) hands the sample - the
  status, the change bits whose clear the hub took (`hcdSsHubClear` reports
  them), the generation - to the machine instead of the look's quiesce,
  wait and release. A new pure classifier, `XhciSuspSsHubEvidence`, reads
  it: a disconnect as above first; another generation stale; a
  C_PORT_LINK_STATE with the link in U0 while RESUMING is the host's
  resume complete, and is never a wake (10.16.2.6.2); a C_PORT_LINK_STATE
  whose clear failed is stage A's lost change (`XhciSuspUncertain`, then
  the cycle). Connection, over-current, configuration-error and link-error
  processing are unchanged; a warm reset on a held device's port is the
  existing recovery's and ends the device (the cycle), which is what
  `XhciSsHubResetKind` already sends from U3.
- QEMU has no SuperSpeed hub model, so this half is read on the bench only
  (36.40); its host vectors stand for it until then.

### 8.7 Function Wake Device Notifications

- **DNCTRL.** `XHCI_EXTENSION` gains `DnctrlEnables`, 0 unless the switch is
  1, when the controller start sets it to N1 (`XHCI_DNCTRL_FUNCTION_WAKE`,
  0002h) **before** `XhciInitController`, so 36.33's read of the switch
  must precede the controller's initialization. The init path writes it
  through `xhciWriteDnctrl` after the HCRST when it is nonzero (a zero is
  the reset value and is not written, so the switch at 0 adds no write);
  the restore path (`xhci_init.c`, which writes 0 today) writes
  `DnctrlEnables`; every reinitialization (the resume's, the recovery's
  HCRST) goes through one of the two.
- **The event.** `xhciHandleEvent` (`xhci_evt.c`) gains an arm for
  `XHCI_TRB_TYPE_DEVICE_NOTIFICATION` that calls a new hook,
  `XhciDeviceNotificationEvent(ext, trb)` (`xhci_hw.h`; the HCD's in
  `hcd_dev.c`), at DISPATCH_LEVEL with the controller lock the drain holds.
  The event stays counted by type first, as today. The hook decodes the
  Slot ID, the Notification Type and the Interface (8.1; `xhci.h` gains
  the field macros and `XHCI_DN_TYPE_FUNCTION_WAKE` 1). With the switch at
  0 it returns at once. A type other than FUNCTION_WAKE, a slot outside
  1 to MaxSlots, or a slot with no device record (`hc->SlotDevice`, the
  transfer event's resolution) is counted and dropped. Otherwise it reads
  the device's **window** (below) and, when it is open, sets the
  Interface's bit in `dev->FwPending` (an interface above 31 sets
  `dev->FwPendingFar` instead) and wakes the thread; when it is closed the
  notification is counted (ignored at receipt) and dropped. A polled drain
  (`XhciInterruptMode` 2) runs the same arm.
- **The window.** A field of the link record, written only by pure-core
  calls the executor makes under the controller lock and read by the hook
  under the same lock, says whether a notification arriving now belongs
  to the suspension in progress:
  - it **opens** (`XhciSuspFwOpen`) in a hold the executor takes just
    before it sends the first arm request of the ARM step (8.4) - before,
    so a notification the device sends while it is still answering that
    request is not lost - or, for a suspension with nothing to arm, just
    before the U3 request (8.4, 8.6);
  - it **closes**, and the latch is emptied (the bits counted as ignored),
    in the hold that begins any sequence (`XhciSuspBegin`: a suspend, a
    host resume, a wake), in the hold in which a wake event is taken from
    any evidence (5.4), and in the hold that ends a sequence in any state
    but SUSPENDED, after consumption (c) below.
  So the latch only ever holds notifications received between this
  suspension's first arm request (or U3 request) and its wake or end: one
  received while the device is RESUMING or ACTIVE, or before the next
  suspension's arm, is dropped at receipt and can never be consumed
  against a later suspension, whatever passes the thread runs in between.
- **Slot reuse.** The hook writes only into the record `SlotDevice[]`
  names at the drain, under the lock. A record leaves `SlotDevice[]`
  under this lock when it is freed (`hcdDeviceFree`, `hcd_enum.c`), after
  its slot is disabled or the HCRST has taken it, and a record for a new
  device is entered only once that device's Enable Slot has completed
  (`hcdDeviceNew`) - after which the old device, whose slot the controller
  disabled (xHCI 1.2c 4.6.4, p.97: doorbell disabled, slot activity
  terminated), sends nothing more through it. A notification for the slot
  drained in between finds no record and is dropped; one drained after
  finds the new device's record with its window closed (a new device is
  ACTIVE). Either way no notification reaches a device that did not send
  it.
- **Consumption.** The latch (`FwPending`, `FwPendingFar`) is emptied by
  the thread under the controller lock at three points, and nowhere else
  but the window's closing:
  - (a) **the decision's hold** (`hcdSuspDecideOne`), first, before the
    slots are read: bits for a device SUSPENDED are handed to the machine
    as one `XHCI_SUSP_EV_WAKE`, which `XhciSuspEvidence` takes as the
    suspension's wake event exactly as stage A's (the claim in this hold,
    5.4, the window closing) and the decision begins the wake branch in
    the same hold - so a D0 that arrived meanwhile does not begin a
    resume that would close the window over an unclaimed wake;
  - (b) **each poll of a SuperSpeed hub port's observation** (8.6): any
    bit ends the observation with WAKE;
  - (c) **the hold that ends a sequence** in any state but SUSPENDED: for
    a refusal, 8.4's claim; otherwise dropped.
  A root port's observation does not take the latch: bits latched during
  it are taken at (a) once the device is SUSPENDED, or at (c). At each
  point every bit is mapped to the PDO whose ownership mask (8.3) holds it
  - **the interface-to-function mapping**: the PDO named is the one whose
  function woke, which the log line names. A bit no PDO holds, a bit not
  in `FwFirsts` (a device naming a function's other interface), and
  `FwPendingFar` are counted (unattributed) and logged, but still count
  as the device's wake: the device is awake whoever it names.
  A device-initiated wake at a root port usually raises its PLC first,
  which takes the event and closes the window, so a notification sent
  after the bus's U0 write is dropped at receipt and only counted; one an
  armed function sends before the U3 request took effect is latched and
  taken at (a) or (c). Behind a SuperSpeed hub the notification is the
  only evidence. A repeat (every tNotification until the function is
  accessed) arriving after the window closed is dropped at receipt; one
  arriving after the next suspension's first arm request is
  indistinguishable from a new wake of that function and is taken as
  one.
- **A lost or never-sent notification** behind a hub leaves the device awake
  in U0 while the record reads SUSPENDED: its client I/O is held by the
  gate, its WAIT_WAKEs stay pending, and the device repeats the
  notification every tNotification until accessed. A host resume meanwhile
  verifies U0 at once (8.6). Nothing waits on the notification.

### 8.8 Composite rules

Stage A's composite rules (4.3, 5.4, 6.2) hold unchanged; at SuperSpeed:

- **The link is suspended only when every PDO is below D0** (4.3). A
  function that idles while a sibling stays in D0 is in logical Dx on an
  ACTIVE link, as in stage A: the bus does not put that one function into
  Function Suspend while the device runs, so it cannot signal a wake until
  its siblings are idle too and the device suspends (orchestrator decision,
  owner to confirm, 8.13: per-function suspend of a running device is left
  to a later phase).
- **Arming is per function**: 5.4's armed intent is computed per PDO - a
  WAIT_WAKE held, `PwrState` D1 or D2, and at least one wake function
  (8.3) - and only that PDO's wake functions are armed. `XHCI_SUSP_FUNC`
  gains `WakeFn` (in: the PDO has a wake function), `Armed` (in: the PDO
  has `FwArmed`) and `ArmNow` (out: arm it at this suspend); the device's
  `ArmIntent` is any `ArmNow`. The thread copies
  `ArmNow` into a thread-only per-PDO field in the decision's hold and the
  ARM step reads it. 5.4's rearm becomes, at SuperSpeed: SUSPENDED with
  some PDO's `ArmNow` set and its `Armed` clear - resume, arm, suspend
  again (an internal cycle); "armed but no longer wanted" is stage A's (a
  wake that claims nothing, resume, disarm, suspend again).
- **One wake completes every armed function's WAIT_WAKE**, as in stage A
  (5.4: every WAIT_WAKE stamped before the watermark whose PDO is D1 or
  D2), not only the function the notification names (orchestrator
  decision, owner to confirm, 8.13). At a root port the event is the PLC,
  which names no function; using the notification only behind hubs would
  make the claim depend on the topology. A function woken this way
  without having signalled sends its D0, finds its device awake and idles
  again by its own driver's policy. `WokeHold` (3.9) is set on every
  claimed PDO as in stage A. The notification's function is logged.
- The idle notification (section 6) is stage A's for a SuperSpeed PDO:
  batches, claims and settlement do not depend on the speed.

### 8.9 What 36.38 builds, and where

36.38 builds the above on stage A's code; the switch, the counters' block
and XHCISNAP's records being 36.33's, stage B appends to them.

- `src/xhci_susp.c`, `xhci_susp.h` - the pure core: SuperSpeed eligibility;
  `Ss` in `XHCI_SUSP_LINK`; `XHCI_SUSP_STEP_STOP_FUNCS` (appended); the
  SuperSpeed order of 8.4 in `XhciSuspBegin`/`XhciSuspAdvance`; the arm's
  "some armed" report deriving `Armed`; the refusal's DISARM at
  SuperSpeed and its SP=1 wait; the strict disarm of 8.5; the root
  SIGNAL straight to VERIFY; every SuperSpeed wake branch as the host's
  resume (SIGNAL after the dwell's remainder, 8.5); `XhciSuspObserveSsHub`,
  `XhciSuspVerifySsHub`, `XhciSuspSsHubEvidence`; `WakeFn`, `Armed` and
  `ArmNow` per function in `XhciSuspDecide` and the per-function rearm;
  the notification window (8.7) as a field of the link record, opened by
  `XhciSuspFwOpen` and closed by `XhciSuspBegin`, by the wake event's
  taking in `XhciSuspEvidence`/`XhciSuspAdvance`, and by an end not
  SUSPENDED - all called under the controller lock.
- `src/xhci_link.c`, `xhci_link.h` - WANT_RESUME's SET_U0 for a port in
  Resume with CCS and PED (8.5); no new class.
- `src/xhci_func.c`, `xhci_func.h` - the first interfaces of a
  configuration's USB 3 functions (8.3).
- `src/xhci_evt.c`, `src/xhci_hw.h` - the Device Notification arm and the
  hook's prototype.
- `src/xhci_init.c`, `src/xhci.h` - DNCTRL from `DnctrlEnables` (init and
  restore); `XHCI_DNCTRL_FUNCTION_WAKE`, the Device Notification Event
  field macros, `XHCI_DN_TYPE_FUNCTION_WAKE`; the `DnctrlEnables` field
  appended to `XHCI_EXTENSION` (a layout XHCISNAP's extension window
  reads: appended at the end, never inserted).
- `src/hcd_dev.c` - `XhciDeviceNotificationEvent`.
- `src/hcd_susp.c` - the executor: `hcdSuspServes` and the PDO
  eligibility for SuperSpeed; the STOP_FUNCS step (EP0 excepted); the
  per-function arm and disarm (FUNCTION_SUSPEND, interface recipient);
  the root SIGNAL and U0 at SuperSpeed through `XhciLinkDecide`; the
  SuperSpeed hub port U3, observation, signal and verification
  (PORT_LINK_STATE, C_PORT_LINK_STATE); the notification's consumption;
  the WAIT_WAKE admission's `RemoteWake` from the PDO's wake functions; a
  SuperSpeed hub sample hook beside `HcdSuspHubSample`; the latch's three
  consumption points (8.7) - `hcdSuspDecideOne`'s hold, the hub
  observation's poll, and `hcdSuspEndLocked`, which gains the refusal's
  wake claim ahead of the STALL's claim and the latch (8.4).
- `src/hcd_hub.c`, `src/hcd_sshub.c` - the owned-port rule in the
  SuperSpeed branch of the port look; the clears the hub took reported.
- `src/hcd_cfg.c` - the configuration transaction of 8.3 in
  `hcdCfgSelect` and `HcdCfgParentConfigure` (masks cleared at entry,
  `FwFirsts` and the GetStatus(interface) reads at a successful exit, the
  settlement of WAIT_WAKEs left without a wake function at every exit);
  `hcdCfgResetPort` unchanged.
- `src/hcd_pdo.c` - the ownership mask of an unsplit PDO (8.3);
  `FwArmed`/`ArmNow` cleared in `hcdDormantKeep` and asserted zero at
  `hcdDormantRevive` (8.5); their clearing at the unlink is in
  `hcd_susp.c`'s `HcdSuspUnlinkLocked`.
- `src/hcd_ctl.c` - `DnctrlEnables` from the switch before
  `XhciInitController`.
- `src/hcd.h` - the device's `FwFirsts`, `FwCapable`, `FwPending`,
  `FwPendingFar`; the PDO's `FwArmed` and `ArmNow`.
- `src/xhci_counters.h`, `src/hcd_log.c` - 8.10's counters, appended
  after 36.33's, and their labels.
- `src/hcd_door.c`, `xhcisnap\` - 8.10's snapshot fields, appended.
- `test\test_susp.c`, `test\test_link.c`, `test\test_func.c` (and the
  test lists) - 8.11; `scripts\vm-matrix` offsets regenerated for the
  appended counters.
- Docs: this record (section 2's DNCTRL and Device Notification rows read
  as history), record 13 section 10.2 (a held device's U3 at a SuperSpeed
  hub port is now the machine's), release notes (SuperSpeed suspend, what
  is read on QEMU and what only on the bench), `build-and-test.md` (8.10's
  log lines), `source-files.md`.

### 8.10 Counters, log lines and XHCISNAP

Counters, appended to the counter block after stage A's (36.33), each a
ULONG, the thread's written plainly and the DPC's under the controller
lock, with `hcd_log.c` labels in the `superspeed suspend:` family:

| Field | Label | Counts |
|---|---|---|
| `SsSuspArms` | functions armed | FUNCTION_SUSPEND with options 03h the device took |
| `SsSuspArmRefused` | arms refused | an arm step's first request not taken (any result) |
| `SsSuspDisarms` | functions disarmed | FUNCTION_SUSPEND with options 0 the device took |
| `SsSuspDisarmFailed` | disarms failed | a disarm not taken: the device cycled (8.5) |
| `SsSuspHubU3` | hub ports suspended | PORT_LINK_STATE U3 observed at a SuperSpeed hub port |
| `SsSuspHubResumes` | hub ports resumed | a verified U0 after PORT_LINK_STATE U0 |
| `SsSuspCapReads` | capability reads | GetStatus(interface) answered |
| `SsSuspCapFailed` | capability reads failed | a STALL, failure or short answer |
| `SsSuspCapable` | capable functions | bits set in `FwCapable`, summed over reads |
| `SsSuspFarInterfaces` | interfaces past 31 | configurations with a function the masks cannot name |
| `FuncWakeEvents` | function wake notifications | FUNCTION_WAKE events received for a device (DPC) |
| `FuncWakeClosed` | function wakes outside the window | dropped at receipt, the window closed (8.7) |
| `FuncWakeAsEvent` | function wakes taken as the wake | a notification that was a suspension's wake event |
| `FuncWakeInSequence` | function wakes during a suspend | latched bits taken by a hub port's observation (b) or a refusal's end (c) |
| `FuncWakeIgnored` | function wakes ignored | latched bits emptied by a closing window, or taken for a device not served or not suspended |
| `FuncWakeUnattributed` | function wakes unattributed | an interface no PDO holds, not a first interface, or past 31 |
| `DevNotifyDropped` | device notifications dropped | with the switch at 1: another type, or a slot with no device (at 0 the hook returns at once and only today's per-type count moves) |
| `SsSuspCapSettled` | wake requests settled by a configuration | WAIT_WAKEs completed NOT_SUPPORTED when a configuration left their PDO no wake function |

The stage A counters (requests, outcomes, refusals by reason, wake events,
claims) count SuperSpeed devices too; stage B adds none of those twice.

Log lines (`XhciLogNote`, bounded as 36.33's are; a value packs its fields
high to low):

- `susp.ss.arm`: slot, interface, result (an `XHCI_SUSP_RES_*`) - each arm
  request;
- `susp.ss.disarm`: slot, interface, result - each disarm request;
- `susp.ss.cap`: slot, the count of first interfaces, the count capable,
  the count failed - after each configuration's reads;
- `susp.fw`: slot, interface, the link state when taken, and the
  disposition (event, observation, refusal, dropped, unattributed; closed
  at receipt) -
  each notification;
- `susp.ss.hub`: hub index, port, the link state read, and the step - each
  SuperSpeed hub port U3, signal and verification outcome.

XHCISNAP: the controller's DNCTRL as read (a register read in the HCD
region), `DnctrlEnables`, and per device record, appended after stage A's
fields: `Ss`, the window, `FwFirsts`, `FwCapable`, `FwPending` and the
union of its PDOs' `FwArmed`. The offsets are appended at the end of
their regions (the orchestrator reconciles them with 36.33's at merge).

**As built (36.38).** The counters are words 142 to 159 of the counter
block, in the table's order (filling the 18 words integration wave 3
reserved for them in place, so the composite parent's two stay at 160 and
the block 162 words), with `hcd_log.c` labels
`susp.ss.*`, `susp.fw.*` and `susp.dn.dropped`. Stage A had no per-device
snapshot record, so the HCD header grows instead (version unchanged, 306
words; appended after 36.24's stage 2 counters at 204 to 236 when the
branches met at integration wave 4 - built at 204 to 272 on its branch):
word 237 DNCTRL 15:0 as the door reads it - only with the switch at
1, `0xFFFFFFFF` (not read) at 0, so the switch at 0 adds no register
access - 238 `DnctrlEnables`, 239 `DnctrlReadback` (DNCTRL read after the
driver's own write, a second extension word appended beside the first so
the amd64 tail parity `TrailingPad` keeps is unchanged), 240 and 241 the
record count (8) and size (8 words), and from 242 to 305 eight records, the first
eight devices in Slot ID order: Slot ID; the link word (state 3:0, `Ss` 8,
the window 9, `Armed` 10, `Root` 11, the running step 23:16); `FwFirsts`;
`FwCapable`; the latch (`FwPending`, `FwPendingFar`); the union of the
PDOs' `FwArmed` (`dev->FwArmedAll`, kept by the thread, so the door walks
no PDO chain); and the last outcome, reason and kind. **The latch lives in
the link record** (`XHCI_SUSP_LINK.FwPending`, `FwPendingFar`, beside the
window `FwWindow`), not in `HCD_USB_DEVICE` as 8.7 and 8.9 have it, so the
pure core opens, closes and empties it with the window and its vectors
cover it; a closing window counts what it dropped in `FwDropped`, which the
executor moves to `FuncWakeIgnored` in the same hold. The log lines are
bounded by stage A's budget; `susp.fw` packs slot, interface (FFh past 31),
the PDO index (FFh unattributed), the link state and the disposition (1
event, 2 observation, 3 refusal, 4 dropped, 5 closed at receipt; 8 or'ed
unattributed). Two choices the text left open: a DNCTRL write the init
path cannot make (no valid read-modify-write operand) is not the start's
failure - only the readback stays 0; and the arm's first request is not
sent behind a client's EP0 transfer, with stage A's one bounded retry.

### 8.11 Host vectors

- `test_susp`: the SuperSpeed order (GATE, STOP_FUNCS, ARM, STOP, U3,
  OBSERVE) armed and unarmed; each step's results at SuperSpeed, a
  refusal before and after some function was armed (DISARM after SP=1's
  10 ms from the last stop, then RESTART; a disarm failure uncertain); the
  root host resume straight to VERIFY and the root wake branch as SIGNAL
  after the dwell's remainder, then VERIFY; the strict disarm;
  `XhciSuspObserveSsHub` and `XhciSuspVerifySsHub` over every link state,
  connection and enable bit and C_PORT_LINK_STATE; `XhciSuspSsHubEvidence`'s precedence
  (disconnect, stale, resume done, never a wake); a notification as
  evidence for a SUSPENDED device; the SuperSpeed wake branch from the
  root sample, the observation's WAKE and a notification, each going to
  SIGNAL after the dwell's remainder (0 long after U3, 10 ms at once
  after it); the window's opening and closing at every transition 8.7
  names, and a resume, a rearm and a re-suspend in one pass with a
  notification received in each phase (none reaches the next suspension
  unless received after its first arm request); `WakeFn`, `Armed`,
  `ArmNow`, an arm with nothing to send, and the per-function rearm (one
  PDO armed, a sibling's WAIT_WAKE arriving while SUSPENDED); USB 2.0
  vectors unchanged (a stage A regression set run with `Ss` 0).
- `test_link`: WANT_RESUME on Resume with and without CCS and PED; every
  other want on a port in Resume answering exactly as before (the
  switch-at-0 regression set); `XhciLinkClassify` of Resume unchanged.
- `test_func`: the first interfaces - IADs, lone interfaces, an IAD and
  lone interfaces mixed, alternates, interface numbers 31 and 32, a
  malformed configuration; the ownership mask of a function PDO, a
  positional-audio PDO and an unsplit PDO, and an interface mapped to
  each.
- The Device Notification Event's decode (type, interface, slot) as a
  pure helper's vectors, with a TRB of another notification type.
- Executor-level (36.39's test client on QEMU cannot raise a
  notification, so these are the bench's or a fault-injection hook's in
  the `qemu` flavour, which 36.38 adds if it is cheap): a notification
  between a taken and a failed arm request, ending in a refusal whose
  wake claim precedes the STALL's and the latch; a configuration change
  that fails after its entry, with a WAIT_WAKE held.

### 8.12 The legs

**36.39, QEMU** (version pinned in the run sheet; development host A).
What QEMU 11.1.0's `hw/usb/hcd-xhci.c` models, read for this design:
DNCTRL is stored and read back but never consulted, and no Device
Notification Event is ever generated; a PLS=U3 write moves the port to U3
**without PLC**; a PLS=U0 write from U3 moves it to U0 **with** PLC; a
PLS=Resume write is ignored; a device's wakeup moves a port in U3 to
Resume with PLC. QEMU has no SuperSpeed hub. So on QEMU, at a USB3 root
port with a SuperSpeed device model bound to the test client (36.34; the
device model and the client's INF match are chosen in 36.39):

- Dx/D0 unarmed: STOP_FUNCS and STOP, U3 observed and held (PORTSC read
  in XHCISNAP over a sustained interval), the host resume's single U0
  write and its PLC, transfers resumed;
- an armed suspend: what QEMU's device model answers
  SetFeature(FUNCTION_SUSPEND) is read and recorded - if it STALLs, the
  refusal path is the reading (every held WAIT_WAKE completed
  NOT_SUPPORTED, the latch, ACTIVE); if it accepts, the arm, the disarm
  after the resume, and a device-initiated wake if the model signals one;
- DNCTRL read back as 0002h with the switch at 1 and 0 with it at 0, and
  the function wake counters at 0 (QEMU sends none);
- the GetStatus(interface) reads and their answers;
- unplug at each state, controller disable, enable, D3 and restart
  mid-transition, as 36.35;
- on 98 SE, 2000 (SMP, Driver Verifier), XP and 7; once more under
  `XhciInterruptMode` 2 (the roadmap's checkpoint);
- `XhciSelectiveSuspend` at 0 reading as the build before it, and the
  matrix unchanged;
- x86 and amd64, every gate.

What QEMU cannot show - the Function Wake notification, a SuperSpeed hub
port, a real device's arm and wake - is named in the release notes as read
only on the bench.

**36.40, the bench (owner).** A SuperSpeed device whose driver idles and
arms wake - for example a USB 3 Ethernet adapter whose Windows 7 driver
uses selective suspend and wake - on the E460's Windows 7, at a USB3 root
port and behind a USB 3 hub, with `XhciSelectiveSuspend` 1: the arm
(`susp.ss.arm`), U3 held, a wake (the adapter's link event), the
notification (`susp.fw` and the counters) - at the root port after the
PLC, behind the hub as the event - the WAIT_WAKE's completion, D0, the
disarm and traffic resumed; and a composite or multi-function SuperSpeed
device if one is at hand. Owed until the owner reads it; never claimed
from a guest.

### 8.13 Decisions taken here (orchestrator decision, owner to confirm)

- `XhciSelectiveSuspend` 1 turns on stages A and B together; there is no
  value for stage A alone. The default stays 0 (owner, 2026-10-09), and 0
  changes nothing, DNCTRL included.
- Function Suspend is used only to carry the wake enable while the whole
  device suspends; a function idle under a running device stays in logical
  Dx (stage A's composite rule), not in Function Suspend.
- A device wake completes every armed function's WAIT_WAKE (stage A's
  claim), not only the function a notification names; the notification
  is evidence and log, and the wake event behind a SuperSpeed hub.
- At SuperSpeed a disarm the device does not take cycles the device,
  where stage A's disarm is best effort.
- A STALL at any function's arm refuses the device's suspend and completes
  every held WAIT_WAKE `STATUS_NOT_SUPPORTED`, as stage A's STALL - but
  for those a notification latched during that suspension claimed first
  (`STATUS_SUCCESS`, 8.4).

## 9. Review of revision 1 (2026-10-05), where each finding is taken

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

Stage B's design (section 8, task 36.37) was reviewed from 2026-10-10.
Round 1 found 8 (1 P1, 5 P2, 2 P3), all taken:

| # | Finding | Answered in |
|---|---|---|
| 1 | A new Resume link class changes WANT_RESET and WANT_HOLD with the switch at 0 | 8.5 `xhci_link.c`: no new class, only WANT_RESUME; 8.11 |
| 2 | An unsplit PDO's `InterfaceMask` is 0: no wake function | 8.3 the ownership mask |
| 3 | Latched notifications cross suspensions | 8.7 the window, opened at the arm or U3 request, closed at every sequence boundary |
| 4 | `FwArmed` has no lifetime across unlink and dormant revival | 8.5 `FwArmed`'s lifetime; 8.9 `hcd_pdo.c` |
| 5 | A configuration change leaves WAIT_WAKEs without capability; an empty arm sets `Armed` | 8.3 the change of configuration; 8.4 `Armed` derived |
| 6 | "A hot reset cannot start from U3" is wrong (xHCI 4.19.1.2.9) | 8.5, the claim withdrawn, the warm reset named policy |
| 7 | Slot-reuse ordering stated backwards | 8.7 slot reuse |
| 8 | D1 stated unconditional | 8.3 capabilities |

Round 2 found 6 (1 P1, 4 P2, 1 P3), all taken:

| # | Finding | Answered in |
|---|---|---|
| 1 | A notification sent before U3 taken as proof the link resumed: no dwell, no U0 restored | 8.5 every SuperSpeed wake branch is the host's resume after the dwell; 8.6 |
| 2 | The window opened after the first arm request loses a notification sent while the device answers it | 8.7 the window opens before the first arm request |
| 3 | A refusal's kept wake misses bits still in the latch; order against the STALL claim and the latch | 8.4 the refusal's order; 8.7 consumption (c) |
| 4 | The configuration change's failure exits and RESET_PORT interval undefined | 8.3 one transaction per path, entry and every exit; RESET_PORT keeps the masks |
| 5 | Dormant detachment bypasses the unlink that clears `FwArmed` | 8.5 cleared in `hcdDormantKeep` |
| 6 | `DevNotifyDropped` counted with the switch at 0 | 8.10 |

Round 3 found 3 (0 P1, 0 P2, 3 P3), all taken: RESET_PORT dropped from
the read rule (8.3), the root wake vector restated (8.11), the STALL's
settlement qualified by the wake's claim (8.4, 8.13). The review ended
there, the first round with no P1 or P2.

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

- The switch defaults to 0 (owner, 2026-10-09, replacing 2026-10-05's
  default 1; section 7).
- Stage A's checkpoint includes the owner's bench reading (owner,
  2026-10-05): a real remote-wake HID device on a 98 SE machine and on the
  E460's Windows 7.
- The test client (SUSPa.6) is tracked under `test\`, never shipped, and the
  packaging gate refuses it in a release (owner, 2026-10-05).
- Stage B ships in the same release as stage A (owner, 2026-10-05): the
  release is cut only after stage B is designed (section 8 grown into the
  record proper, reviewed), built and read, its bench included.
