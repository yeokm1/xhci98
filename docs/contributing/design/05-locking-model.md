# 05 - Locking, IRQL, and Lock Order

Phase 4 task 9, consumed by Phases 5-8. This is the miniport's complete
synchronization design: which lock covers what, in which order, which contexts
are excluded from which, and what the port, endpoint and transfer state that
arrived in later phases does under it.

It exists because `usbport.sys` hands the miniport four mutually unsynchronized
execution contexts and no lock of its own to combine them with. The facts about
those contexts are in `docs/usb-xhci-info/usbport-miniport-abi.md` section 7
and are not restated as claims here; this document is the derivation from
them, plus the static review of the driver as it stands.

The normative summary lives in `docs/contributing/implementation-invariants.md`
under "Command Ring" and "Interrupt Ordering". Where the two differ, the
invariants file wins and this one is stale.

## 1. What usbport gives us, and what it does not

Four contexts reach this miniport, and no two of them are serialized by the
same thing:

| Context | IRQL | usbport lock held | Reaches |
|---|---|---|---|
| Lifecycle (`StartController`, `StopController`, `Suspend`/`ResumeController`) | PASSIVE | none for start; power paths vary | everything |
| Callbacks (endpoint open/close/state, submit/abort, root-hub status queries, `CheckController`, `Get32BitFrameNumber`, `Enable`/`DisableInterrupts`) | DISPATCH | `MiniportSpinLock` | command state, flags, MMIO |
| `InterruptDpc` | DISPATCH | `MiniportInterruptsSpinLock` | event ring, command completion, ERDP, IMAN |
| `InterruptService` (ISR) | DIRQL | none; usbport's own ISR gate only | USBSTS, IMAN |
| Async timer callbacks (`UsbPortRequestAsyncCallback`) | DISPATCH | neither | command state, CRCR |
| Root-hub `RH_Set/ClearFeature*` | DISPATCH | none (ReactOS `roothub.c:170-285`) | port state (Phase 5) |
| `FlushInterrupts` | <= DISPATCH | none, arbitrary thread | nothing today |

Three consequences follow, and they are the whole reason a miniport lock
exists at all:

1. `MiniportSpinLock` and `MiniportInterruptsSpinLock` are different locks. A
   submit and the DPC can run at the same time on two CPUs. Anything both
   touch needs the miniport's own lock.
2. Two of the contexts hold neither (the async timer callback and the
   root-hub feature callbacks), so "usbport serializes it somewhere" is never
   an argument on its own.
3. The ISR runs at DIRQL, and no DISPATCH-level lock excludes it. That is not
   a gap to be closed by choosing a better lock; see section 4.

## 2. The lock

One lock: `xhciControllerLock`, a plain `KSPIN_LOCK` in `src/xhci_cmd.c`,
reached only through `XhciControllerLockAcquire`/`XhciControllerLockRelease`.

It is in the driver image, not the miniport extension. usbport
`RtlZeroMemory`s the extension before every `StartController`, so a lock kept
there is a lock a restart can re-initialize under a callback already spinning
on it. Checking an epoch first narrows that window; it does not close it,
because a check is not synchronization. One lock created once in `DriverEntry`
has no such window at all.

It is one lock for every controller this driver serves. A machine has one or
two xHCI controllers, the command engine allows one outstanding command, and
the longest critical section is a bounded event-ring drain with no waiting in
it. Buying per-controller granularity back would mean creating a lock
somewhere other than `DriverEntry`, which is the hazard above.

### The four rules

1. Innermost. No usbport service and no bounded wait may occur while it is
   held. Decide under the lock, act after dropping it. That is why
   `XhciCommandEvent` returns a reset action for `XhciEventDpc` to invoke after
   the release, and why `xhciArmCommandTimer` is called from outside.
2. Every non-ISR read-modify-write of `XHCI_EXTENSION.Flags` happens under
   this lock, and takes one of two forms. A standalone transition uses
   `XhciControllerUpdateFlags`, which brackets itself. A transition that is
   part of a larger locked transaction (quiesce admission clearing
   `INITIALIZED` after the mask, `Enable`/`DisableInterrupts` moving
   `INTERRUPTS` beside the enables) is written inline while the lock is
   already held, and must be: the helper acquires, so calling it from inside a
   locked region is a recursive acquisition, which is rule 1's hazard in
   section 3 rather than a style question. The greppable form of the rule is
   therefore "`ext->Flags` is only assigned in `XhciControllerUpdateFlags` or
   between an acquire and a release", not "only through the helper".
3. Created in `DriverEntry` and nowhere else.
4. The ISR never takes it (section 4).

### What it currently covers

Command state and the command ring's MMIO; the whole bounded event-DPC drain
including every `ERDP` publication and the `IMAN` re-arm; interrupt
enable/disable and the mask/unmask read-modify-writes; the terminal
`ControllerFailed` transition; lifecycle quiesce admission; the `USBSTS` health
poll; and every `Flags` transition listed above.

### What is outside it

The init and reinit sequence (`XhciInitController` and everything it calls)
runs at PASSIVE_LEVEL holding nothing, and touches the extension freely. That
is safe on a stated precondition rather than by omission: the function clears
`XHCI_EXT_FLAG_INITIALIZED` under the lock as its first act, and every
DISPATCH-level path that could touch controller state (DPC, health poll,
command submit, interrupt enable) tests that flag under the lock before doing
anything. So the sequence runs with every other context already refusing. The
ISR's separate `HcInfoStatus` gate covers the same window for the one context
that has no admission flag; `XhciInitController` sets `HcInfoStatus` bad on
entry for that reason.

The diagnostic counters, per the note in `XHCI_EXTENSION`: nothing branches
on them and a torn count costs nothing.

`FrameNumber` is the one counter that is also functional, and the frame axis
is under the controller lock. It has two writers, `XhciFrameNumber` (which
takes the lock itself, for `Get32BitFrameNumber`) and `XhciFrameSample`
(called with the lock already held, from the health poll in
`XhciControllerHealthPoll`, `src/xhci_cmd.c`), and both write under it. The
second exists because bounding the gap between two readings of MFINDEX's
eleven-bit Frame Index cannot rest on "usbport calls `Get32BitFrameNumber`
often", which is an observation about two binaries rather than a bound.

A third MFINDEX reader is not a third writer of that axis.
`XhciPollClockAdvance` (`src/xhci_init.c`) is called from the same health
poll, under the same lock, and reads the same register, but it keeps its own
two fields, `PollClockMs` and `PollClockFrame`, because the published axis is
not a clock: its stall path advances one per call so that usbport's uncapped
post-open wait terminates (batch 6-0), and its congruence claim is about
isochronous Frame IDs. A watchdog measured on it would age a command out
because usbport asked the time, and would stop timing one because a Frame ID
became unclaimable. Same lock, same register, two axes with different
admission gates, and the poll clock is what every age and stall threshold in
this driver is measured on.

## 3. Lock order

There is one miniport lock, so an order exists only against usbport's:

```
  MiniportSpinLock  ─┐
                     ├─►  xhciControllerLock  ─►  (nothing)
  MiniportInterruptsSpinLock  ─┘
```

Acquired inside either usbport lock, never around one, and never held across a
call back into usbport. Being the leaf of every hierarchy is what removes
lock-order deadlock: there is no second miniport lock to disagree with about
order, and nothing is ever acquired after it.

It does not remove recursive acquisition, and that is the one way a single
lock still hangs. A DISPATCH-level spin lock taken twice on one CPU spins
against itself forever; the harness says so in as many words at
`XhciHostAcquireSpinLock`, and `commandLockErrorsTotal` exists to catch it. So
"one lock" is not a licence to acquire it wherever the state is touched: a
helper that brackets itself may only be called from an unlocked context, which
is the constraint rule 2 in section 2 is written around. Nesting is the
mistake this design is most likely to make, and it is the mistake a reader who
takes "innermost" to mean "always safe to take" will make first.

## 4. The DIRQL exception

`XhciIsr` runs at DIRQL. It cannot take `xhciControllerLock`: a DISPATCH-level
spin lock held on a CPU that is then interrupted by its own device's ISR
deadlocks against itself. The two primitives that would exclude a DIRQL context
are both unavailable. `KeAcquireInterruptSpinLock` is XP-era and on the import
deny list, and `KeSynchronizeExecution` needs the `PKINTERRUPT`, which usbport
owns and does not expose.

So the ISR is excluded from nothing, and the design has to make exclusion
unnecessary rather than pretend to it. It does that in three ways.

It is stateless in the sense that matters. It reads `USBSTS`, writes
`USBSTS.EINT`, read-modify-writes `IMAN`, and writes three diagnostic fields
(`InterruptCount`, `LastIsrStatus`, `InterruptsClaimed`) which nothing
branches on. It reads `Flags`, `ControllerFailed`, `HcInfoStatus` and
`InterruptDeliverySuppressed` as gates, and writes none of them; it touches no
ring and no command state.

Its only read-modify-write moves both bits in the same direction every other
context moves them. `IMAN` is `IP` (RW1C) in bit 0, `IE` (RW) in bit 1, RsvdP
above, so clearing `IP` means deciding `IE`; there is no partial write. An
earlier ISR carried `IE` through from its read, which meant a
`XhciMaskInterrupts` landing between that read and that write was undone by
the ISR: `DisableInterrupts`, quiesce, suspend and the terminal failure
transition all mask under the controller lock, and the one context none of
them exclude could put the enable back. The ISR now writes `IE` as 0. It can
lower that bit and never raise it, so no interleaving exists in which it
undoes a mask, and the two contexts do not need to exclude each other.

And when it cannot read a valid operand at all, it writes a literal. Every
`IMAN` read-modify-write in this driver refuses an all-ones operand; the ISR
retries a small bounded number of times (it runs at DIRQL and may not wait)
and then writes a bare `XHCI_IMAN_IP`, counting it in `IsrImanLiteralAcks`.
That is the one documented exception to the RsvdP-preservation rule
(`docs/contributing/implementation-invariants.md`, "Interrupt Ordering"), and
it does not weaken the property this section is about: the literal carries
`IE` as 0, so it still moves that bit only downwards and still cannot undo a
mask.

That costs no interrupt delivery, which is what makes it a fix and not a
trade. The xHC set `EHB` when it set `IP` (EHB "shall be set to `1` when the
IP bit is set to `1`", 5.5.2.3.3, p.394), and `IP` cannot be set again while
`EHB` is set: "when IMODC transitions to `0`: if EHB = `0` and IPE = `1`, then
IP shall be set to `1`" (4.17.5, p.270). Throughout the window between the
ISR and its DPC, therefore, no interrupt can be generated whatever `IE` holds.
`IE` clear is this driver's normal state between an interrupt and its DPC.

The one write that raises `IE` again is the DPC's, under the lock, and it
takes two conditions rather than one. usbport passes its own
interrupt-enabled state as a BOOLEAN, but it sampled that before queuing the
DPC and holds `MiniportInterruptsSpinLock` here, which does not exclude the
`MiniportSpinLock` its `DisableInterrupts` runs under. So the argument can be a
stale `TRUE` while a disable has already masked both enables on another CPU.
The DPC therefore also requires `XHCI_EXT_FLAG_INTERRUPTS`, the same fact
recorded by this driver under the lock the mask holds. Whichever context
acquires first, the other sees the finished state.

Making the ISR monotone promoted that re-arm from a convenience to a single
point of failure, which it was not written to be. A review caught it: the
re-arm was a bare read-modify-write guarded by `if ((iman & IE) == 0)`, and
an all-ones read has IE set, so one transient undecoding read looked like
"already armed", wrote nothing, and left the controller permanently silent,
with no counter, no trace and no escalation, because usbport does not call
`EnableInterrupts` again while a controller runs.

It is now
`XhciRearmInterrupter`, which carries the same contract as the mask and
unmask paths: all ones is refused as an operand and as evidence, the write
comes from the validated read, success is a read back, retries are bounded and
stall-free, and a failure escalates through the DPC's existing post-lock reset
request.

The transferable rule is in the invariants: a path that becomes the
only writer of an enable inherits the whole operand/read-back/escalation
contract, not just the write.

What remains true of the ISR's decline gates is documented under "Interrupt
Ordering": a gate that makes the ISR decline may only be acted on once
delivery is provably suppressed (`InterruptDeliverySuppressed`, confirmed by
read back), because a decline at DIRQL does not acknowledge, and an
unacknowledged level-triggered shared INTx livelocks the line.

## 5. Every ERDP writer is serialized

`ERDP` carries both the software dequeue pointer and the `EHB` release, and the
drain relies on its own intermediate publications carrying `EHB` = 0. A writer
that was not serialized against the drain could publish a pointer the DPC has
already moved past, and could clear `EHB` mid-pass.

The rule: any `ERDP` writer holds the controller lock, or holds the section 2
precondition instead. There are three:

| Writer | What serializes it |
|---|---|
| `XhciEventDpc` | the controller lock, for the whole drain |
| `XhciEnableInterrupts` | the controller lock, with both enables still clear |
| `XhciEventDiscardStale` | no lock; the section 2 precondition, below |

(The init sequence programs `ERDP` too, under the same precondition.)

The third is reached only from `xhciRestoreState`, and `XhciResumeController`
calls that without acquiring anything. What makes it sound is not the lock but
the window: the resume runs with `INITIALIZED` clear and the controller
halted, so every usbport callback that would take the lock declines, the DPC
is not queued, and the controller produces nothing. That is the
reinitialization precondition section 2 states, applied to a path that
reinitializes less.

The ISR is excluded by what it does, not by the mask. "Interrupts masked" is
not a property this window can assert: `XhciMaskInterrupts` reports delivery
proven suppressed or not, and its out-of-attempts exit returns with
`InterruptDeliverySuppressed` = 0 and neither enable proven clear; the suite
proves an ISR can run after such a quiesce with `INITIALIZED` still clear.
There is no resulting race, because the ISR reads neither the event ring nor
`ERDP`. That is a property this window depends on, so an ISR that ever learns
to touch either has to re-establish this section rather than inherit it.

The ISR is not stateless in the wider sense: it reads `HcInfoStatus`,
`Flags`, `ControllerFailed` and `InterruptDeliverySuppressed`, and writes
`InterruptCount`, `LastIsrStatus`, `InterruptsClaimed` and
`IsrImanLiteralAcks`. None of that is a hazard here, since each counter has
the ISR as its only writer and nothing in the restore can observe a value
being built. The narrow property (no event ring, no `ERDP`) is the one this
section rests on, and it is the one stated.

Do not read the third row as permission to write `ERDP` from anywhere else. A
fourth writer that is not inside that window needs the lock, and a fourth
writer inside it needs the window re-established rather than assumed, because
what closes the window is a single flag any future step could set earlier.

`XhciFlushInterrupts` remains a counter-only callback. The reason is not the
number of ERDP writers (the lock settles that); it is that there is nothing
to do at the call site all three shipping builds use. It runs from the D0
power completion, before resume processing, on a miniport that suspend has
already halted and masked, and the pending state an acknowledgement would
clear is cleared moments later by the resume's `HCRST`. It is also the one
callback usbport makes holding neither of its own locks, so acting would make
it the only caller acquiring the controller lock from an arbitrary thread:
legal, but worth naming before someone does it.

## 6. The static review

Every entry point in the driver, its context, and what it does about
synchronization. Derived from `grep -n "static .*NTAPI" src/xhci_dispatch.c`
plus the three non-callback entry points, not from recall.

### Entry points

| Entry point | Context | Shared state touched | Synchronization |
|---|---|---|---|
| `DriverEntry` | PASSIVE, load | the lock itself, the packet | creates the lock; nothing else can run yet |
| `StartController` | PASSIVE | everything | `XhciCommandInit` before the signatures are published; `XhciInitController` runs with `INITIALIZED` clear (section 2); `Flags` set through the helper |
| `StopController` | PASSIVE | ports, quiesce, `Flags` | port pass then `XhciQuiesceController`; quiesce admission is one locked transition; final `Flags` through the helper |
| `SuspendController` | PASSIVE | quiesce, `Flags` | as above, minus the port pass |
| `ResumeController` | PASSIVE | full reinit, `Flags`, the event ring and `ERDP` | `SUSPENDED` test-and-clear is one locked transition; reinit per section 2. The restore's `XhciEventDiscardStale` walks the event ring and publishes `ERDP` without the lock, under that same precondition (section 5) |
| `ResetController` | DISPATCH inside a usbport lock | mask, `ControllerFailed` | one locked transition: mask, then publish failure. Cannot wait, cannot reinitialize |
| `CheckController` | DISPATCH, `MiniportSpinLock` | `USBSTS`, `MFINDEX`, command age | `XhciControllerHealthPoll` reads under the lock, escalates outside it; it also advances the poll clock every age and stall threshold is measured on (`XhciPollClockAdvance`, section 2) |
| `InterruptService` | DIRQL | `USBSTS`, `IMAN` | section 4: stateless, monotone, no lock |
| `InterruptDpc` | DISPATCH, `MiniportInterruptsSpinLock` | event ring, command state, `ERDP`, `IMAN` | whole drain under the lock; reset request after the release |
| `EnableInterrupts` | DISPATCH, `MiniportSpinLock` | `ERDP`, enables, `Flags` | under the lock; escalation after the release |
| `DisableInterrupts` | DISPATCH, `MiniportSpinLock` | enables, `Flags` | under the lock; escalation after the release |
| `FlushInterrupts` | <= DISPATCH, no usbport lock | none | counter only (section 5) |
| `Get32BitFrameNumber` | DISPATCH, `MiniportSpinLock` | `FrameNumber`, `MFINDEX` | takes the controller lock around the read and the publish; the health poll's `XhciFrameSample` is the axis's second writer and holds it too (section 2). `XhciPollClockAdvance` reads the same register under the same lock and writes a different axis; section 2 says why the separation matters |
| `xhciCommandTimeout` (async) | DISPATCH, no usbport lock | command state, `CRCR` | two pointer checks before the lock; epoch, generation, `INITIALIZED` and `ControllerFailed` all validated under it |
| `InterruptNextSOF` | DISPATCH, `MiniportSpinLock` (derived, task 9-A.3: both builds acquire it for this call alone) | none | counter only; nothing waits on the callback and its state-change list is drained by usbport's own 500 ms timer DPC regardless (`docs/usb-xhci-info/usbport-miniport-abi.md` section 4) |
| `PollController`, `TakePortControl` | DISPATCH / any | none | trace only |
| Root-hub status queries (`RH_GetRootHubData`, `RH_GetStatus`, `RH_GetPortStatus`, `RH_GetHubStatus`) | DISPATCH, `MiniportSpinLock` | port shadow, `PORTSC` | the controller lock around the read, the shadow update and the change acknowledgement; `RH_GetStatus` is a constant and takes nothing |
| Root-hub feature callbacks (the twelve `RH_Set/ClearFeature*`) | DISPATCH, no usbport lock established either way | port shadow, `PORTSC` | the controller lock; the write is composed and issued inside it, and nothing waits |
| `RH_SetFeaturePortReset` / `RH_ClearFeaturePortSuspend` | DISPATCH, as above | port shadow, `PORTSC`, the armed generation | as above, plus `XhciRootHubDeferredWork` after the release; the timer arm and the announcement are both usbport services |
| `xhciRhPortTimeout` (async) | DISPATCH, no usbport lock | port shadow, `PORTSC` | two pointer checks before the lock; epoch, hub port and generation all validated under it, and the generation is claimed before any register is read |
| `RH_DisableIrq` / `RH_EnableIrq` | DISPATCH | `Flags` | one `XhciControllerUpdateFlags` transition; touches no register |
| `RH_ChirpRootPort` | DISPATCH | a counter | no register, no lock |
| `OpenEndpoint` / `ReopenPipe` / `SetEndpointState` / `PollEndpoint` | DISPATCH, `MiniportSpinLock` | endpoint record, its ring and queue, the quiesce state | the controller lock; the Configure/Stop/Set TR Dequeue commands are issued under it and nothing waits |
| `SubmitTransfer` | DISPATCH, `MiniportSpinLock` | transfer queue, ring, `SubmitEpoch` | the controller lock; the completion is deferred out of the submit bracket (section 7) rather than made inside it |
| `SubmitIsoTransfer` | DISPATCH, `MiniportSpinLock` | as `SubmitTransfer` | reached through the same routine as `SubmitTransfer`, under the same lock at the same IRQL, so it follows that row's rules rather than needing its own (task 9-A.1). Listed separately so its absence from the rules is not read as an omission |
| `AbortTransfer` | DISPATCH, `MiniportSpinLock` | transfer queue, completion list, quiesce state | the controller lock; searches the completion list as well as the queue (batch 7a-B) |
| `InterruptDpc` transfer completions | DISPATCH, `MiniportInterruptsSpinLock` | queue, ring, extension totals | inside the bounded drain, which already holds the controller lock, so no acquire of its own |
| `XhciSlotDrainSettled` (task 9-0.2) | DISPATCH, `MiniportInterruptsSpinLock` | every live device's queues and rings, extension totals | after the drain and before the `ERDP` publish, with the controller lock still held from the drain, so no acquire of its own. It calls no usbport service: completions are owed through `xhciDevOweCompletion` and posted by `XhciSlotDeferredWork` once the lock is dropped, as an event's are. It sits before the publish on purpose: EHB is still set, so no new interrupt can be generated while the walk runs |

### Findings

Four, all fixed:

1. The ISR could undo a mask. Section 4. Fixed by direction rather than by a
   lock.
2. The DPC could re-arm `IMAN.IE` off a stale BOOLEAN. Section 4. Fixed by
   adding the flag the mask maintains under the same lock.
3. One `Flags` transition had a hand-rolled acquire/release around it
   (`xhciStopController`). It was the only standalone one that did; the three
   inline sites (`XhciControllerBeginQuiesce`, `Enable`/`DisableInterrupts`)
   are inside larger locked transactions and cannot use the helper without
   nesting, so it was the one site where the two spellings of the same thing
   were a choice. Routed through `XhciControllerUpdateFlags`, which leaves two
   legitimate shapes rather than three.
4. The "no usbport service under the lock" check was written per-service, and
   only for the three services that had vectors. `UsbPortWait`,
   `UsbPortReadWriteConfigSpace` and `KeStallExecutionProcessor` had no check
   at all, and `UsbPortWait` is `KeDelayExecutionThread` on the target, so
   holding a DISPATCH-level spin lock across it is a hang on Win98 and a
   Verifier bugcheck on Win2000. All of them now report through one function
   in `test/test_init.c` whose never-reset total is asserted once at the end of
   the suite, so the property is checked mechanically for every site,
   including ones added later.

The review's principal negative result is worth recording as such: with those
four fixed, no acquire/release pair in the driver encloses a usbport service or
a bounded wait, and no state written from two contexts is written outside the
lock from either of them.

## 7. Port, endpoint and transfer state - the decision, and what implemented it

Task 9's remit was to decide where Phase 5 and Phase 6 state would go before
it was written. The decision was that all of it joins the controller lock,
with the consequences named below because they constrained the designs rather
than following from them.

That state now exists, so this section is a record of a decision and its
outcome rather than a plan: Phase 5's port state landed, Phase 6's transfer
metadata in batch 6-0, and the endpoint records, quiescence machine and
cancellation paths in Phases 7a and 8. The entry-point table in section 6
carries their rows.

### Port state and reset generations (Phase 5)

Phase 5's six tasks landed, so the paragraphs below are description rather
than a decision waiting to be applied: the shadow, its change bits and the
per-port generations are in `XHCI_EXTENSION.RootHub`, every reader and writer
of them takes the controller lock, and `XhciRootHubPortEvent` is the one that
does not. It is called from inside the event DPC's bounded drain, which
already holds it, so acquiring there would be the recursive acquisition
section 3 warns about.

Two consequences of the rule showed up as soon as there was code. The port
operations compose and issue their `PORTSC` write inside the lock, which is
allowed (MMIO is not a usbport service and takes no wait), and they read back
nothing, so the section is short and bounded. And the two deferred actions,
arming a port timer and calling `UsbPortInvalidateRootHub`, follow the pattern
the DPC's reset request already uses: decided under the lock, performed by
`XhciRootHubDeferredWork` after it is dropped.

The announcement is the one place in this driver where that rule is a hang
rather than a discipline. `USBPORT_InvalidateRootHub`'s first act is
`RH_DisableIrq(MiniPortExt)`, straight back into this miniport, into a callback
that takes the very lock the caller would be holding, and a `KSPIN_LOCK` is
not recursive (`docs/usb-xhci-info/usbport-miniport-abi.md` section 6). The
host model performs that re-entry for this reason, so the double acquire would
land in the suite's never-reset `commandLockErrorsTotal` rather than in an
argument.

The device-initiated resume arms from inside the drain: `XhciRootHubPortEvent`
decides it, sets `ArmPending`, and the DPC drains it after releasing the lock.
Two further asymmetries came out of writing it. The claim of an armed
generation happens before the timeout callback reads any register, because the
read it would otherwise take first is a refresh, and a refresh claims whatever
is armed on that port; a stale timer would complete an operation belonging to
somebody else. And the health poll got a root-hub half (`XhciRootHubPoll`),
which sweeps link states rather than reacting to an event: PLC is acknowledged
by whichever refresh sees it first, so a status query racing the Port Status
Change Event consumes the notification and the event path finds nothing to
arm.

Two facts force the per-logical-port shadow, the change bits, and the reset
and resume generations under the controller lock:

- The DPC updates the shadow from Port Status Change events under the lock.
- The root-hub `RH_Set/ClearFeature*` callbacks run at DISPATCH holding no
  usbport lock at all, so nothing else would serialize them against that DPC,
  or against each other on SMP.

`RH_GetPortStatus` and the other status queries do run under `MiniportSpinLock`,
but that is a different lock from the DPC's, so they take the controller lock
too. `UsbPortInvalidateRootHub` is a usbport service: decide under the lock,
call after releasing.

### Transfer metadata (Phase 6)

Batch 6-B landed with this layout. Per-endpoint ring
producer/consumer state, the TD bookkeeping, the device records and the
address-to-Slot-ID map are all in `XHCI_EXTENSION.Devices` under the
controller lock. `SubmitTransfer` runs under `MiniportSpinLock` and the
completion runs in the DPC under `MiniportInterruptsSpinLock`; those are the
two halves of one ring.

The consequence that had to be designed for: `UsbPortCompleteTransfer` is a
usbport service, so it cannot be called from inside the drain. The drain
classifies and retires under the lock, threads the completed transfers onto a
list, releases, and completes them afterwards. The list is threaded through
storage usbport already allocated per transfer, because this driver has no
private pool (`AGENTS.md`). Completing inline by dropping and re-taking the
lock per event was the alternative, and it was rejected: it would make every
invariant the drain relies on (the dequeue pointer, the outstanding count, the
`EHB` state) re-derivable mid-pass by another context, which is a much larger
surface than one list.

Writing it added four things.

The list is not the only thing owed. `XhciSlotDeferredWork` drains three kinds
of deferred work, not one: completions, the `UsbPortInvalidateEndpoint` that
asks usbport to re-offer a transfer refused while a command chain was running,
and the next command of a chain, because `XhciCommandSubmit` takes this same
lock. So the rule is not "complete outside the lock" but the root hub's rule
again: decide under it, act after it.

When is a second question the lock does not answer. Getting a completion out
from under the lock says nothing about whether it may happen at all:
`UsbPortCompleteTransfer` hands the transfer's mapped buffer back, usbport
unmaps its scatter/gather list, and the pages return to whoever owned them. So
a transfer whose TRBs are still on a ring the controller can execute must not
be completed anywhere, inside the lock or outside it. A teardown that cannot
prove the controller stopped therefore leaves its transfers queued, and they
are answered on the far side of `HCRST` by `XhciSlotInit`. This is the one
place where "decide under the lock, act after it" is not sufficient on its
own, which needs saying because the discipline reads as though it were.

On the cancellation path the "answer later" escape does not exist. Batch 7a-0
read `AbortTransfer` out of both shipping builds
(`docs/usb-xhci-info/usbport-miniport-abi.md` section 4): usbport frees the
transfer record, and unmaps its buffer, in the same endpoint-worker pass that
made the callback, whether or not the miniport did anything. So leaving a
cancelled transfer queued is not an option the way it is for a teardown; the
buffer goes back regardless. That is why task 7a-B.2 had to decide, before it
was written, what the miniport does when it cannot prove the endpoint stopped.

What task 7a-B decided: the lock is not what closes it and could not be.
`AbortTransfer` runs at DISPATCH under usbport's `MiniportSpinLock` and may not
wait for a command, so no discipline available here makes the stop
synchronous.

What the batch does instead is move the guarantee out of the
abort and into the pause. `SetEndpointState(PAUSED)` precedes the abort by at
least one of usbport's own frame gates, and from it the endpoint is stopped
and left stopped (the Set TR Dequeue Pointer is programmed but the doorbell
is not rung) until `SetEndpointState(ACTIVE)`, a submission, or a health-poll
net ends the pause. So the whole cancellation pass runs against an endpoint the
xHC is not executing, rather than a race the abort has to win.

The first draft rang the doorbell at the placement and was strictly worse than
not stopping at all: it put the endpoint back into Running one command round
before the abort arrived.

Two ordering rules follow from that and belong here rather than in the file:

- `XHCI_EPQ_STOPPED` is a claim about the hardware and must die with the
  doorbell. It is what licenses rewriting a cancelled TD's TRBs as No Ops, and
  a stale one would edit TRBs the xHC is executing. Every path that rings an
  endpoint doorbell clears it in the same lock hold.
- A forced Stopped Transfer Event is a statement about the ring, not about a
  transfer, and it is taken out at the slot layer before any queue sees it,
  the same shape as an event naming a slot nobody has open. Routed into the
  queue it would complete whichever transfer owned the TRB the endpoint stopped
  on, which for a pre-emptive stop is a transfer nobody withdrew.

`UsbPortCompleteTransfer` re-enters no miniport slot; the 7a-V binary read
established it. Both shipping builds unlink the transfer, insert it on the FDO
done list with `ExfInterlockedInsertTailList`, queue a DPC and set a worker
event, then return (SP4 `0001A4EA` -> `000183C0`/`000154B0`, NUSB `0001A0D4`
-> `00017FA4`/`000152EE`; `docs/usb-xhci-info/usbport-miniport-abi.md` carries
the derivation). The next submission arrives later, from usbport's own worker
context, not on this stack.

The drain's busy flag is still required, for
reasons that were always independent of the re-entry: a callback that drains
on entry can be reached from one that is already draining, and on SMP one CPU
must decline while another is inside the loop, at the cost of a window in
which work queued by the decliner is not picked up, closed by usbport's 500 ms
`CheckController` reaching the same drain.

The host model still performs a
completion-time re-entry as a labelled defensive vector: the guard must hold
even against a caller the binaries say does not exist, and what runs after the
callback returns is still capable of racing this driver.

The command engine gets an owner rather than a comparison. The engine allows
one command outstanding driver-wide, so the device layer records which record
owns it before submitting and reads that back in the completion, which runs
under this lock after the engine has already matched the event to its own
outstanding TRB. A TRB address recorded after the submit returned would be too
late: the DPC can complete the command on another CPU first. The outstanding
command's TRB type is checked beside the owner, because the init sequence's No
Op self-test can complete inside the window between recording the owner and
submitting.

Every path that can observe a port change owes the drain. A connect change
tears a device down, and the sites that observe one are the event DPC,
`RH_GetPortStatus` and the health poll's sweep. Two of the three reach
`XhciRootHubDeferredWork`, which now ends by calling the device layer's; the
third is `RH_GetPortStatus`, which does not (its change goes back in the
answer rather than into an announcement) and therefore calls the device
layer's drain itself. The suite found that missing rather than review.

### Endpoint records and the quiescence machine (Phases 7a and 8)

Written after this section was, and they inherited its rule without amending
it: the non-default endpoint records, their pooled rings, each queue's transfer
FIFO and the per-endpoint `XHCI_EP_QUIESCE` state all live under the
controller lock, and every command the quiescence machine issues (Stop
Endpoint, Reset Endpoint, Set TR Dequeue Pointer) is composed and submitted
inside it.

Two things about them are locking facts rather than transfer facts, so they
belong here. `AbortTransfer` must search the completion list as well as the
queue, because a transfer can be threaded for deferred completion and then
cancelled before the drain reaches it; the list is state the lock protects,
not a private detail of the drain (batch 7a-B). And the submit bracket is a
lifetime rule, not an ordering preference: usbport writes to the transfer
record after `SubmitTransfer` returns, so a completion made inside that
callback arms a timeout on memory usbport is about to free. `SubmitEpoch` is
what defers it out of the bracket (batch 7a-V).

Rules of the machine itself that are recorded nowhere else:

- A failed quiescence must not mark the endpoint record failed. That state
  says what a Configure Endpoint achieved; a stop this driver could not
  complete says nothing about it. Conflating them made task 7a-B.3's
  reset-pipe unable to recover the very pipe it exists for, because every later
  submission failed on the record's state before the recovered quiescence state
  was consulted (batch 7a-B).
- The Configure Endpoint is never stored in `PendingOp`. That field holds one
  operation and the EP0 chain owns it; usbport opens the interrupt pipe while
  the Max Packet Size correction is still in flight, so an endpoint open
  writing into it would silently discard an `EVALUATE_MPS` the reopen had just
  owed. The need is derived from endpoint-record state instead, which cannot
  collide with a stored operation (task 7a-A.1).
- The forced Stopped Transfer Event's residual is the only measurement a
  cancelled transfer ever gets. It is latched onto the owning transfer without
  completing or retiring anything, so `AbortTransfer` reports a real length
  rather than the zero the record was created with. A transfer that was not on
  the queue at abort is counted in `AbortsUnmatched` and answered with the zero
  usbport pre-set; one that was is never completed by this driver at all, since
  usbport completes it itself (batch 7a-B).
- The queue is the record of what was cancelled. After the stop, every
  outstanding TRB from the oldest surviving transfer forward that no surviving
  transfer owns is a leftover and is rewritten as a No Op (4.6.9 p.119);
  leftovers ahead of the oldest survivor need no rewrite, because moving the
  dequeue pointer past them reclaims them in one store. `XHCI_EPQ_PAUSED` holds
  the endpoint Stopped from `SetEndpointState(PAUSED)` until usbport says
  ACTIVE, so the cancellation pass runs against a stopped endpoint instead of
  racing it (task 7a-B.2).
- An endpoint already idle in 4.6.4's own sense (empty queue, dequeue caught
  up with enqueue, not halted) is given up without a command, which is what
  keeps every enumeration's EP0 reopen on the path both targets already pass
  (task 7a-B.1).

### Later lifecycle flags

They are `Flags` bits, so rule 2 already covers them: through the helper, under
the lock. Two of them carry the second half of a port disable or power-off:
`DisownPending`/`DisownWantsPp` record that `XhciSlotPortDisowned` has run
(address and EP0 binding given up, unconditionally) while `XhciSlotPortDisabled`,
the teardown that hands mapped buffers back, is still owed to a port that has
not yet confirmed the write in its own bit (`PP` for a power-off, `PED` for a
disable). The health poll collects it, the same mechanism `PpPending` uses, so
there is no second sweep (batch 6-V; the rule and the measurements are in
`implementation-invariants.md`, "Device Addressing").

## 8. When to split the lock, and into what

Not yet, and not on a hunch. The criteria for adding a sibling lock, all three
of which must hold:

1. A measured hold time that matters. The bounded drain at
   `XHCI_DPC_MAX_EVENTS` with Phase 6 completion work in it is the candidate,
   and it is measurable on the SMP VM rather than arguable.
2. Real contention: two CPUs, two endpoints, sustained traffic. Win98 is
   uniprocessor and has no preemption at all, so this can only ever be a Win2000
   SMP finding.
3. A partition of the state with no cross-edges, which in practice means
   per-endpoint transfer metadata and nothing else. The controller-wide state
   (command ring, event ring, `Flags`, interrupt enables) is not partitionable.

If it happens, the order is declared now so it cannot be invented under
pressure: `xhciControllerLock` -> per-endpoint lock, never the reverse, and
the per-endpoint lock is subject to the same four rules, including living in
the driver image rather than in an endpoint extension usbport zeroes on
`ReopenPipe`.

## 9. What is checked, and how

| Property | How it is checked |
|---|---|
| Every acquire has exactly one release; the lock is never nested | `commandLockErrorsTotal`, asserted once at the end of `test_init` |
| No usbport service and no bounded wait under the lock | `serviceUnderLockTotal`, one report function behind every service stub and wait hook, asserted once at the end |
| The ISR never raises `IE` | vectors over both entry values of `IE`, plus the RsvdP-preservation vector |
| The DPC re-arms only when both conditions hold | a stale-`TRUE` vector and a both-set vector |
| The re-arm proves IE came back up | a transient all-ones read (must not read as "already armed"), a swallowed write caught by the read back, and an exhausted retry budget that escalates to a RESET request |
| The lock is created once, in `DriverEntry` | `commandLockInits`, plus the "a start creates no lock" vector |
| Actual mutual exclusion | not checkable here. The host suite is single-threaded: it sees the shape of the code around the lock, never a race. This is a review property plus the Phase 2d SMP VM |

That last row is the honest limit of this document. A single-threaded model can
prove that a call happens under a lock and that a service does not; it cannot
prove that the lock is the right one. The Phase 4 checkpoint's SMP run, with
Driver Verifier enabled, is what tests the design rather than its shape.

## 10. Open against the SMP checkpoint

- The ISR/mask interleaving is closed by argument and by the direction of a
  single bit. Confirm on the SMP VM that no interrupt storm and no lost
  interrupt follows a `DisableInterrupts` under load.
- The drain's hold time is unmeasured. Phase 6 adds work inside it; section 8
  is the trigger.
- `FlushInterrupts` is counted, never acted on. If `InterruptFlushes` is still
  zero after a D0 transition on either target, the call site was read wrong and
  section 5 needs revisiting.
- The SMP reading that exists (batch 7a-V, guest 2d: WHPX, two vCPUs on
  distinct host threads, `info cpus` checked before and after):
  `AbortsDuringCompletion` stayed 0 across 14 aborts driven against live
  interrupt endpoints while the DPC ran on the other CPU, the only guest where
  it could ever be nonzero, so the expected zero is measured rather than
  assumed. `TransfersRefused` was nonzero for the first time on any target (8):
  the submit gate meeting a quiescing endpoint under real contention, not the
  livelock signature, since `transfers submitted` climbed throughout (1,115).
  `isr = claimed = dpc` held unbroken on all three guests (1231/1176/1251).

## 11. Process-global storage - the complete inventory

Roadmap task 11-V.2 carries the rule "no mutable controller state may live in
process-global storage", and batch 11-A is its host half. This section is the
audit; the run half (two controller instances, one failing without corrupting
the other) stays with batch 11-V, because a static inventory cannot show
independence, only the absence of shared mutable state to lose it to.

The inventory is read out of the linked image, not out of the source. A grep
over `src/*.c` answers "what did I write"; the artifact answers "what is
actually there", and the two differ whenever something arrives via a header, a
macro expansion, or a library. Re-derive it in a fresh clone with the DDK's own
COFF dumper: `link.exe -dump -symbols` over every object of a flavour, keeping
symbols whose section is `.data` or `.bss` plus every COMMON symbol. An
uninitialised file-scope definition is COMMON, not `.bss`, so a `.bss`-only
sweep misses the registration packet entirely; that is the trap this paragraph
exists for.

```
link -dump -symbols src\objfre\i386\*.obj      (repeat for objchk)
link -dump -headers  src\objfre\i386\xhci98.sys
link -dump -rawdata:bytes -section:.data src\objfre\i386\xhci98.sys
```

Measured at the batch 11-A tip: the release build has exactly three writable
process-global objects, and its whole `.data` section reads as zero (348
bytes: 324 of objects, the rest section padding). Zero-filled is the stronger
statement of the two. It means no initialised writable global exists anywhere
in the image, so there is no table, cache or default that a second controller
could find already populated by the first.

| Object | Bytes | What it is | Why process-global is correct |
|---|---|---|---|
| `xhciControllerLock` (`src/xhci_cmd.c`) | 4 | The one `KSPIN_LOCK`, section 2 | It carries no controller information at all. It is a mutual-exclusion primitive, and the alternative (a lock in the extension) is the race section 2 exists to remove. Two controllers serialize against each other; that is a throughput cost, not shared state |
| `xhciStartEpoch` (`src/xhci_cmd.c`) | 4 | The start-token allocator | A token source, not a count of anything. No controller's behaviour depends on its value; each extension records the token it was handed, and every comparison is for equality. Allocated with `InterlockedIncrement` because it is shared and PnP can start two controllers at once |
| `XhciRegPacket` (`src/xhci_dispatch.c`) | 316 | The registration packet | Write-once, before registration, and read-only for the life of the load. Every assignment to it is inside `xhciFillPacket`, whose only caller in a shipping image is `DriverEntry` (the second caller is `XhciFillPacketForTest`, compiled only under `XHCI_HOST_TEST`). It is also the service table the miniport calls through, so the ABI record says to keep it global |

The verdict is pass, and it rests on a distinction worth stating because the
rule can be read as banning all three. What the rule forbids is controller
state: a value one controller writes and another reads, or that carries a
controller's identity, configuration or progress. A lock holds no such value,
a token allocator's value is meaningless to the controller that receives it,
and a table nothing writes after `DriverEntry` cannot be a channel between two
controllers in either direction.

One limitation is named rather than fixed, and it belongs to the `qemu`
flavour alone. The same sweep, run over `objchk_qemu`, finds the three above
and 901 further `.bss` symbols, every one of them an `xhciDbgSeen`/`xhciDbgLast`
budget counter from a trace macro. They are per macro expansion, therefore per
driver image rather than per controller, so on a two-controller machine the two
controllers share one budget at each site and can spend it on each other's
values.

Nothing branches on them, and the behaviour is already documented at
the macros in `src/xhci_dbg.h` and in
`docs/contributing/design/03-host-unit-tests.md`. It is a diagnostic limit on
multi-controller hardware, not a correctness one, but it is what a
multi-controller QEMU run will notice first, so read a thin trace there as
this before reading it as a driver fault.

Sweep the right tree, or the check passes for the wrong reason. The trace
macros compile to nothing without `XHCI_DBG_LIVE`, which only `qemu` defines,
so neither published binary contains any of these 901 objects:
`src\objchk\i386` and `src\objfre\i386` are both clean of them, and a sweep
over `objchk` finds only the three above. The advice about reading a thin
trace applies only where a trace exists, which is `qemu`; the shipping `debug`
build prints nothing as it runs, and a missing trace there is not this
limitation.

How to keep this true: the sweep above is the check, and there is no gate that
runs it. A new file-scope variable in `src/` is the thing to look for in
review, and the question to ask of it is not "is it `const`?" but "can one
controller's write of this be read by another?", which the trace budgets fail
and the three objects above pass.
