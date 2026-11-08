# 05 - Locking, IRQL, and Lock Order

> **Status: the `1.2.0.0` miniport's record** (branch `1.2.0.0`): the
> contexts it excludes are `usbport.sys`'s. The host controller driver's lock
> model is design record 13 section 5.

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
| Callbacks (endpoint open/close/state, submit/abort, `CheckController`, `Get32BitFrameNumber`, `Enable`/`DisableInterrupts`) | DISPATCH | `MiniportSpinLock` | command state, flags, MMIO |
| Root-hub status queries (`RH_GetRootHubData`, `RH_GetStatus`, `RH_GetPortStatus`, `RH_GetHubStatus`) | **PASSIVE on NT 6.x**, DISPATCH on NT 5.x | **none on NT 6.x**, `MiniportSpinLock` on NT 5.x | port shadow, `PORTSC` |
| `InterruptDpc` | DISPATCH | `MiniportInterruptsSpinLock` | event ring, command completion, ERDP, IMAN |
| `InterruptService` (ISR) | DIRQL | none; usbport's own ISR gate only | USBSTS, IMAN |
| Async timer callbacks (`UsbPortRequestAsyncCallback`) | DISPATCH | neither (on NT 6.x usbport holds its own timer-list lock across the call - FDO+0xFE0 on Windows 7 x64, static, task 24.3.4) | command state, CRCR |
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
runs holding nothing (at PASSIVE_LEVEL on a start, at DISPATCH_LEVEL from the
in-place recovery's DPC), and touches the controller registers and the
controller-side state freely. That is safe on a stated precondition rather
than by omission: the function clears `XHCI_EXT_FLAG_INITIALIZED` under the
lock as its first act, and every DISPATCH-level path that touches controller
state (DPC, health poll, command submit, interrupt enable) tests that flag
under the lock before doing anything. So the sequence runs with those
contexts already refusing. The ISR's separate `HcInfoStatus` gate covers the
same window for the one context that has no admission flag;
`XhciInitController` sets `HcInfoStatus` bad on entry for that reason.

The precondition does not extend to the device table, and the 2026-09-05
audit (roadmap Phase 20, F8) is why that has to be said. The slot callbacks
(`SubmitTransfer`, `SetEndpointState`, `AbortTransfer`, `XhciSlotDeferredWork`)
read and write `Devices[]` under the controller lock with no admission gate:
`xhciDevAdmitted` is consulted only after the record has been read, and
recovery itself calls the deferred drain to deliver owed completions while
`INITIALIZED` is clear. The in-place recovery runs `XhciSlotInit` from a DPC
with the controller still live from usbport's point of view, so on SMP a
callback on another CPU is not refusing while the table is reset. That reset
is therefore not part of the lockless sequence: `XhciSlotInit` cancels the
queued work, zeroes the table and resets the owner and cursor fields inside
one hold of the controller lock, so a callback sees the old table or the new
one and never a half-zeroed record. The single-drainer guard `DeferredBusy`
is not written there at all. Its owner is the `XhciSlotDeferredWork` call
that set it, which drops the lock around every usbport service and re-takes
it afterwards; clearing the flag under it would admit a second drainer, and a
set flag at reinitialisation names a drainer that is still running and will
clear it itself.

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
  MiniportSpinLock  -----------+
                               +-->  xhciControllerLock  -->  (nothing)
  MiniportInterruptsSpinLock  -+
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

The second lock, `xhciDeliveryLock`, added for issue 7 (section 7, "Where a
completion may be handed over"), sits outside this order rather than in it:
it is taken only after the controller lock has been released, holds no
state, and the one call made under it takes no usbport lock and re-enters
no miniport slot. The controller lock may be taken inside it - the
suite's defensive re-entry vector does - and it may never be taken inside
the controller lock; the host model counts either inversion as an error.

### On NT 6.x: usbport's own locks

On Vista and Windows 7 two services put this driver inside usbport's lock
order, so the order has to be read against usbport's locks as well as against
the controller lock. The names below are the FDO-extension locks the static
read found on all four builds (this record's appendix A, A.1, has the
offsets and addresses; `legal-provenance.md` section 4 indexes it): the MP lock, the MP-call lock, EpList, the ISR-DPC
lock, the timer-list lock, the I/O-count lock, and the root-hub
interrupt-endpoint lock (RH-IntrEp).

The orders usbport takes itself that matter here (static, all four builds):

- RH-IntrEp -> ISR-DPC. `MPRH_EnableIrq` and `MPRH_DisableIrq` take RH-IntrEp
  (unless their caller already holds it) and then the ISR-DPC lock around the
  miniport's `RH_EnableIrq` / `RH_DisableIrq`. They run from the root-hub
  status queue's insert and remove callbacks (usbhub posting its
  status-change IRP, or the queue emptying) and from the tail of usbport's
  root-hub DPC, `Ev_Rh_IntrEp_Dpc`, on every run with an IRP queued.
- RH-IntrEp -> EpList, the I/O-count lock and the RH-PDO pointer lock, in
  `Ev_Rh_IntrEp_Invalidate`, the body of `USBPORTSVC_InvalidateRootHub`.
- EpList -> MP, through `iSetGlobalEndpointStateTx` -> `MPf_PollEndpoint`'s
  flag test.
- timer-list -> I/O-count, and no other usbport lock under the timer-list
  lock.
- usbport never takes RH-IntrEp while it holds another spin lock of its own:
  `IsrDpc` invalidates after releasing the ISR-DPC lock, and the other takers
  hold nothing.

What this driver adds to that:

- MP -> timer-list and ISR-DPC -> timer-list. It takes the timer-list lock
  only through `UsbPortRequestAsyncCallbackEx` with its lock byte 0, with the
  controller lock released, from a context usbport reaches holding nothing,
  its MP lock (a root-hub status query from `RootHub_Endpoint1_Peek`) or its
  ISR-DPC lock (the event DPC); section 7, "Where a timer may be armed". It
  never arms under EpList.
- Nothing into RH-IntrEp. On the Version 300 tier it calls
  `UsbPortInvalidateRootHub` only from a context in which usbport holds no
  spin lock (section 7, "Where the root hub may be announced"). RH-IntrEp
  stays a lock nothing is held under, and none of the orders above can close
  a cycle through it.

That last rule is what prevents three cycles. Each needs the service called
from a context that holds a usbport lock, and all three were live in the build
that carried 24.4 and 24.5 to the guests (static; `runs/run-24.md`, "Round 5
on the fixed build"):

| Cycle | Edges | CPUs | What breaks it |
|---|---|---|---|
| C1 | ISR-DPC -> RH-IntrEp (the event DPC announcing), then RH-IntrEp -> ISR-DPC (usbport's `MPRH_*`) | two: an event DPC, and a root-hub DPC's tail or queue removal | the event DPC reports the change through `InterruptDpcEx`'s port bit and never calls the service; `IsrDpc` makes the same invalidate with both locks released |
| C2 | timer-list -> RH-IntrEp (a feature or timer callback announcing), then RH-IntrEp -> ISR-DPC, then ISR-DPC -> timer-list (the event DPC's Ex arm) | three | no DEFER context announces on the Version 300 tier: the change is latched for the health poll or the next event DPC |
| C3 | MP -> timer-list (an Ex arm from `RH_GetPortStatus` under `Endpoint1_Peek`), then timer-list -> RH-IntrEp, RH-IntrEp -> EpList, EpList -> MP | four | the same change: timer-list -> RH-IntrEp no longer exists |

C1 is the one observed. Windows 7 x86 at value 0 and Vista x86 at value 1,
four vCPUs each, hung with one CPU in `IsrDpc` -> this driver's event DPC ->
`USBPORTSVC_InvalidateRootHub`, spinning on RH-IntrEp, and another in
`Ev_Rh_IntrEp_Dpc` -> `MPRH_DisableIrq`, spinning on the ISR-DPC lock (stacks
read from guest memory through the QEMU monitor). C1 is as old as the Version
300 tier. C2 and C3 came with 24.4's Ex arms, which put the timer-list lock
under the ISR-DPC and MP locks; the announcement from feature and timer
callbacks that completes them is older, and 24.4's announcement of owed arms
made it more frequent. None of them can hang on one CPU, where a
DISPATCH-level spin lock is never contended.

The argument this section gave for 24.4 was that its orders are acyclic
because no usbport function nests the timer-list lock with the MP, MP-call,
ISR-DPC or EpList lock. That holds for the timer-list lock and is not enough:
it did not follow RH-IntrEp's successors, which reach the ISR-DPC lock
directly and the MP lock through EpList, and this driver took the timer-list
lock under both.

Which service call would reopen which cycle, so that none is added in
passing:

- `UsbPortInvalidateRootHub` from the event DPC on the Version 300 tier
  reopens C1;
- from a root-hub feature callback or a timer callback, C2 and C3;
- from `RH_GetPortStatus` or `RH_GetHubStatus`, a shorter one,
  MP -> RH-IntrEp -> EpList -> MP, since usbport's peek holds the MP lock
  across both;
- from an endpoint callback it takes EpList under EpList, the hang design
  record 12 section 11 records.

No NT 5.x analogue was found (static; high confidence on XP SP3 and XP x64,
which have public symbols, medium on Windows 2000 SP4, NUSB and SweetLow's
build): there `USBPORT_InvalidateRootHub` calls `RH_DisableIrq` holding no
usbport lock, and on the two XP builds the lock `IsrDpc` holds across
`InterruptDpc` is taken elsewhere only by the interrupt enable and disable
wrapper; on the other three it was not followed as far (Windows 2000 SP4's
lock has holders that call helpers nobody traced, and NUSB's offset was not
pinned). The Version 200 tier announces from the event DPC as before.

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
`USBSTS.EINT`, read-modify-writes `IMAN`, and writes four diagnostic fields
(`InterruptCount`, `LastIsrStatus`, `InterruptsClaimed`,
`IsrImanLiteralAcks`) which nothing branches on - the same four section 9
lists, where this said three. It reads `Flags`, `ControllerFailed`, `HcInfoStatus` and
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

One kind of lost read-back is not a failure. The DPC re-arms after its final
`ERDP` write has released `EHB`, so an event already waiting raises the
interrupt the moment `IE` lands, and the ISR, which the controller lock does
not exclude, clears `IE` again before the read-back. The escalation that
followed reset a working controller on 32-bit XP with one vCPU, during
isochronous audio behind a virtual hub (run-24's second defect, the interrupter re-arm on one CPU; `runs/run-24.md`,
"Round 5 on the fixed build"). A claim makes usbport queue its interrupt DPC
again, and that pass re-arms, so `XhciRearmInterrupter` now takes an advance
of `InterruptsClaimed` since a snapshot read before the write as delivery
handed to that pass, and counts it in `InterruptArmsTakenByIsr`. It says
nothing about the write itself - a shared-line claim of a pending `EINT` can
come between the snapshot and a swallowed write - only that a pass that
re-arms is coming, unless usbport disables interrupts first, in which case
`IE` = 0 is what it asked for.

That premise is a fact about usbport, read per build (static;
`legal-provenance.md` section 4): every build queues its DPC on every claim
(NT 6.x bar a rundown count that blocks only after `StopDevice`), and reads
its interrupt-enabled state under the same lock its `EnableInterrupts` and
`DisableInterrupts` wrapper holds. On Windows 98 SE
and ME it also rests on NTKERN's DPC queue behaving as the WDM contract says,
which was not read in the binary.

`XhciUnmaskInterrupts` does not take the rule. On all four NT 6.x builds
usbport enables interrupts in two power paths, `TurnUsbControllerOn` and the
restart after a failed `ResumeController`, while a "not operational" bit its
`IsrDpc` tests is still set, so the DPC a claim queues there skips the
miniport and nothing re-arms. That window is also a hazard of its own, older
than the rule and not fixed: a claim inside it leaves `IE` clear with `EHB`
set, and the interrupter silent until the next power transition (static;
whether a claim lands there at runtime is not known).

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
precondition instead. There are three. (A fourth, the bench candidate
`XHCI_FIX_EVT_REARM`, was retired by task 35-T.1, whose lost-interrupt
backstop queues the drain and writes no register.)

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
| `InterruptDpc` | DISPATCH, `MiniportInterruptsSpinLock` | event ring, command state, `ERDP`, `IMAN` | whole drain under the lock; reset request after the release. On NT 6.x usbport holds its ISR-DPC lock here, so a root-hub change is reported through the return's port bit and never announced (section 3) |
| `EnableInterrupts` | DISPATCH, `MiniportSpinLock` | `ERDP`, enables, `Flags` | under the lock; escalation after the release |
| `DisableInterrupts` | DISPATCH, `MiniportSpinLock` | enables, `Flags` | under the lock; escalation after the release |
| `FlushInterrupts` | <= DISPATCH, no usbport lock | none | counter only (section 5) |
| `Get32BitFrameNumber` | DISPATCH, `MiniportSpinLock` | `FrameNumber`, `MFINDEX` | takes the controller lock around the read and the publish; the health poll's `XhciFrameSample` is the axis's second writer and holds it too (section 2). `XhciPollClockAdvance` reads the same register under the same lock and writes a different axis; section 2 says why the separation matters |
| `xhciCommandTimeout` (async) | DISPATCH, no usbport lock | command state, `CRCR` | two pointer checks before the lock; epoch, generation, `INITIALIZED` and `ControllerFailed` all validated under it |
| `InterruptNextSOF` | DISPATCH, `MiniportSpinLock` (derived, task 9-A.3: both builds acquire it for this call alone) | none | counter only; nothing waits on the callback and its state-change list is drained by usbport's own 500 ms timer DPC regardless (`docs/usb-xhci-info/usbport-miniport-abi.md` section 4) |
| `PollController`, `TakePortControl` | DISPATCH / any | none | trace only |
| Root-hub status queries (`RH_GetRootHubData`, `RH_GetStatus`, `RH_GetPortStatus`, `RH_GetHubStatus`) | **PASSIVE with no usbport lock on NT 6.x** (issue 7, 2026-09-13: `USBPORT_RootHub_ClassCommand` calls `RH_GetPortStatus` and `RH_GetHubStatus` directly, static on Windows 7 x86 and x64, and this driver's callback log reads `irql=00` for all four on both); DISPATCH under `MiniportSpinLock` on NT 5.x as ReactOS documents it | port shadow, `PORTSC` | the controller lock around the read, the shadow update and the change acknowledgement; `RH_GetStatus` is a constant and takes nothing. The drain `RH_GetPortStatus` runs afterwards may not hand a completion to usbport on the 300 tier (section 7, "Where a completion may be handed over") |
| Root-hub feature callbacks (the twelve `RH_Set/ClearFeature*`) | DISPATCH, no usbport lock established either way | port shadow, `PORTSC` | the controller lock; the write is composed and issued inside it, and nothing waits |
| `RH_SetFeaturePortReset` / `RH_ClearFeaturePortSuspend` | DISPATCH, as above | port shadow, `PORTSC`, the armed generation | as above, plus `XhciRootHubDeferredWork` after the release; the timer arm and the announcement are both usbport services, and on the Version 300 tier both are owed to a context that holds no usbport lock (section 7) |
| `xhciRhPortTimeout` (async) | DISPATCH, no usbport lock | port shadow, `PORTSC` | two pointer checks before the lock; epoch, hub port and generation all validated under it, and the generation is claimed before any register is read |
| `RH_DisableIrq` / `RH_EnableIrq` | DISPATCH; on NT 6.x under usbport's RH-IntrEp and ISR-DPC locks (section 3) | `Flags` | one `XhciControllerUpdateFlags` transition; touches no register and calls no usbport service |
| `RH_ChirpRootPort` | DISPATCH | a counter | no register, no lock |
| `OpenEndpoint` / `ReopenEndpoint` / `SetEndpointState` / `PollEndpoint` | DISPATCH, `MiniportSpinLock`; `SetEndpointState` and `PollEndpoint` also under usbport's EpList lock on NT 6.x (reached through `USBPORT_SetGlobalEndpointState` and the HcInt worker's `iSetGlobalEndpointStateTx`, static), which is why they are two of the three callbacks the 300 tier delivers completions from (section 7) | endpoint record, its ring and queue, the quiesce state | the controller lock; the Configure/Stop/Set TR Dequeue commands are issued under it and nothing waits. A handle the record is bound to a different extension than is declined under the same lock (`xhciEpHandleSuperseded`, roadmap Phase 20, F1), except a `PAUSED` from a handle that still owns queued work (`xhciEpHandleOwnsWork`), which starts the stop that handle's abort needs |
| `CloseEndpoint` / `GetEndpointState` / `QueryEndpointRequirements` | DISPATCH, `MiniportSpinLock` | the probe's counters only | the controller lock, taken inside `XhciProbeEndpoint` (`src/xhci_probe.c`); no record is read or written, and neither shipping build calls the first two |
| `GetEndpointStatus` / `SetEndpointStatus` / `SetEndpointDataToggle` | DISPATCH, `MiniportSpinLock` | the record's quiesce state (the status pair); a counter (the toggle) | the controller lock; the reset-pipe chain is armed under it and driven by the deferred pass, and a superseded handle is declined |
| `RebalanceEndpoint` | DISPATCH, `MiniportSpinLock` | a counter and a trace line | no register, no record, no lock needed |
| `StartSendOnePacket` / `EndSendOnePacket` | **PASSIVE, assumed rather than measured** - neither shipping build has been seen to call either, and the code says so where it declines them | **nothing at all**: they trace and return `MP_STATUS_NOT_SUPPORTED`, and carry no counter | no register, no record, no lock needed |
| `PassThru` | <= DISPATCH, the caller's context | the whole extension and the PORTSC array, read only | the controller lock across the whole snapshot (design record 08 section 13); acknowledges nothing |
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

That is NT 5.x's service. On NT 6.x the service does not call
`RH_DisableIrq`: usbport's root-hub queue does, under its RH-IntrEp and
ISR-DPC locks. The rule still holds there, and a second one joins it, about
which context may make the call at all (section 3, and "Where the root hub
may be announced" below).

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

`RH_GetPortStatus` and the other status queries take the controller lock too.
This paragraph used to say they "do run under `MiniportSpinLock`"; on NT 6.x
they do not - `USBPORT_RootHub_ClassCommand` calls `RH_GetPortStatus` and
`RH_GetHubStatus` directly at the URB dispatcher's IRQL, measured as 0 by
this driver's callback log on Windows 7 x86 and x64 (issue 7 section 7).

**`RH_GetHubStatus` is the second callback that takes no lock of this
driver's either, and the list above named only `RH_GetStatus`** (the
audit of 2026-09-16). It writes `ext->RhHubStatusQueries++` outside the
controller lock, and the argument that makes that sound is in its own comment
rather than here: this driver has exactly ONE writer of that counter - that
line - and every other reference is a read. What the rule forbids is a counter
with two disciplines, not one without a lock. `RH_GetStatus` is a constant and
touches nothing at all.
Either way nothing else serializes them against the DPC.
`UsbPortInvalidateRootHub` is a usbport service: decide under the lock,
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

### Where a completion may be handed over (issue 7, 2026-09-13)

"Decide under the lock, act after it" said where the completion service may
not be called from - inside the controller lock - and nothing about where it
may. Issue 7 is the cost of that gap: an NT 6.x usbport's
`USBPORTSVC_CompleteTransfer` takes no lock of its own and runs
`USBPORT_Core_iCompleteTransfer`, whose `i` means the caller holds usbport's
EpList lock; that function queues the done DPC before storing its queued
state, and the EpList lock, held by the caller and taken by the DPC's
worker, is the only thing that orders the two. usbport holds it across the
callbacks it makes through `iSetGlobalEndpointStateTx` - `PollEndpoint`,
`SubmitTransfer`, `AbortTransfer`, `SetEndpointState` - and Microsoft's
usbehci completes transfers from `PollEndpoint` alone. Delivered from
`RH_GetPortStatus` at PASSIVE with no lock, the queued DPC ran on the same
CPU before the store, read idle, and the done list was marked queued for
ever. On NT 5.x the same service's done-list insert is interlocked, which
was read as "safe from anywhere" and is why the discipline was never
contradicted before Windows 7. **Issue 8 refutes that for XP x64**: the
service unlinks the transfer from its endpoint's list before that insert
with no lock of its own, while usbport walks that list under the endpoint's
lock, so on XP x64 a completion is delivered only from a callback for its
own endpoint (`XHCI_EXTENSION.DeliverPerEndpointOnly`, below). The 32-bit
NT 5.x services **have since been read for the same unlink, and they have it**
(issue 8 section 4d, 2026-09-15): XP SP3, NUSB 3.3 = 3.6, SweetLow's and
Windows 2000 SP4's, all static.

The rule the driver now carries (`XHCI_EXTENSION.DeliverUnderUsbportLockOnly`,
`src/xhci_slot.c` `XhciSlotDeferredWork`):

- **The service is always called at DISPATCH_LEVEL**, on every tier and
  from every context, under `xhciDeliveryLock` (section 11). The lock
  guards nothing; it is the raise, and the import ceiling has spin locks
  and no `KeRaiseIrql`. It is taken only with the controller lock released
  and never the other way round (section 3); the service takes no usbport
  lock and re-enters no miniport slot (batch 7a-V), so it is innermost too.
- **On the Version 300 tier, a completion leaves the list only from inside
  `PollEndpoint`, `AbortTransfer` or `SetEndpointState`** (their drains are
  `XhciSlotDeferredWorkForEndpoint`; the admission is an argument of the call
  rather than a field, because a per-controller counter would admit another
  CPU's event DPC for as long as one CPU sat inside an abort;
  `SubmitTransfer`'s own hold already parks everything until it returns).
  The event DPC retires the TD, threads the completion, and reports
  `USBPORT_DPC_EX_TRANSFER_WORK`; usbport's HcInt pass, signalled on every
  interrupt, polls every active endpoint under its lock and `PollEndpoint`
  delivers. Any other context - the event DPC itself, the root-hub
  callbacks, the health poll - counts `completions held for PollEndpoint`
  and leaves it.
- **Two overrides**, both through `XhciSlotDeferredWorkForced`: the
  lifecycle drains (suspend, stop, resume, recovery), where usbport's own
  deliverers are gated off and a parked completion would sit until the far
  side; and the health poll's fallback, which delivers anything parked for
  `XHCI_COMPLETION_FALLBACK_MS` and counts the poll that did. The fallback
  is the contract's transient hazard (an unlocked signal can race
  `Xdpc_End`'s state-3 read and lose one signal until the next completion)
  accepted over a request that never completes; a nonzero
  `completion fallback polls` during ordinary traffic means usbport stopped
  polling an endpoint that still owed work.
- **The WHOLE Version 200 tier sets the gate and `DeliverPerEndpointOnly`
  with it**, in both architectures (issue 8): a locked callback delivers only
  its own endpoint's oldest completion, any other pass leaves it parked and
  calls `UsbPortInvalidateEndpoint` for the endpoint that owes it, and the
  fallback and forced drains are as above. `src/xhci_dispatch.c` sets both
  flags for every Version 200 load with no `_WIN64` guard.

  It was amd64-only for one day. Read out of XP x64 first and guarded
  `_WIN64`; then on 2026-09-15 XP SP3 x86 livelocked on the same race (issue
  7 section 7.9) and the other 32-bit builds - NUSB's, SweetLow's, Windows
  2000 SP4's - were read to share the unlocked mover and the locked reader
  (issue 8 section 4d), so the guard was lifted onto the whole tier that
  night. This entry and the summary table said amd64-only until the
  2026-09-16 audit.

The suite holds it with two never-reset nets - the service is never called
outside the delivery lock, and the delivery lock is never nested or taken
under the controller lock - and three vectors: the event DPC, a root-hub
query, a bare drain and the first poll all leave a completion parked and
`PollEndpoint` delivers it; the fallback fires once at the threshold and
not again; `SetEndpointState`, `AbortTransfer` and the forced drain each
deliver from their own context, and the 200 tier still delivers from the
DPC.

### Where a timer may be armed (roadmap 24.4, 2026-09-28)

The completion service's lesson, again for the timer service. On NT 6.x the
legacy `UsbPortRequestAsyncCallback` forwards to
`USBPORTSVC_RequestAsyncCallbackEx` with its seventh argument 1, which skips
the Ex function's own acquisition of usbport's timer-list lock and assumes
the caller holds it; usbport holds it around the timer DPC's callback and
around the root-hub feature callbacks it makes from
`USBPORT_RootHub_PortRequest`. Every other arm this driver made - the command
watchdog from the submit pump, the event DPC and the health poll, the
device-initiated resume, the recovery, the virtual hub's submit - could race
the timer DPC's unlink and free on another CPU (static, found in 24.3.4,
never observed). The locking branch cannot simply be taken everywhere: Ex
with its lock byte 0 from a context that already holds the lock spins on it
for ever, and from `SubmitTransfer` it would put the timer-list lock under
EpList, the reverse of the timer DPC's order through
`USBPORTSVC_InvalidateRootHub`.

The static read of all four builds settled the rest, and one part of it
decided the design: **no callback of this driver can know whether usbport
holds the timer-list lock**. The root-hub feature callbacks arrive with it
from `RootHub_PortRequest` and without it from the USB 2.0 port-power detour
(`SetFeaturePortPower` with `MiniPortFlags` 0x10, which this driver sets) and
the User* IOCTL paths; the timer callbacks arrive with it from the timer DPC
and, on Windows 7 only, without it from `USBPORT_CancelAllAsyncTimerCallbacks`,
which `MPf_StopController` runs before the miniport's StopController and which
calls each pending callback synchronously.

So on the Version 300 tier (`XHCI_EXTENSION.ArmThroughExOnly`, set in
StartController from the version presented and only if usbport wrote the
slot) the legacy service is never called, and every arm site passes its
context as an argument (`XHCI_ARM_*`, `src/xhci_hw.h`) - never a field, for
the reason the delivery gate's admission is an argument:

| Mode | Contexts | What happens |
|---|---|---|
| `XHCI_ARM_UNLOCKED` | the event DPC (usbport holds its ISR-DPC lock); `CheckController` - `XhciRootHubPoll`, `xhciArmRecovery`, `XhciSlotPoll` (nothing held); the root-hub status queries `RH_GetPortStatus` / `RH_GetHubStatus` (nothing, or the MP lock from `RootHub_Endpoint1_Peek`); Start/Stop/Suspend/ResumeController (nothing, or the suspend/resume semaphore) | armed now: `UsbPortRequestAsyncCallbackEx(ext, ms, ctx, len, cb, NULL, 0)`, and first every arm a DEFER context left owed |
| `XHCI_ARM_DEFER` | every endpoint callback - `OpenEndpoint`, `SubmitTransfer` (the virtual hub's included), `SetEndpointState`, `SetEndpointStatus`, `PollEndpoint`, `AbortTransfer` (the MP-call lock and EpList); every root-hub feature callback; every timer callback - the command watchdog's re-arm, the port timer, the recovery (and with it the recovery's No Op self-test and stop helper, `XHCI_INIT_ARM_MODE`) | nothing armed: a port's `ArmPending` stays set, the command watchdog is latched in `CommandArmOwed` with its context by value |

On the Version 200 tier both modes arm at once through the legacy service,
as every NT 5.x build always has, so nothing below NT 6.x moves (AGENTS.md's
tier rule).

**How an owed arm gets made.** Every UNLOCKED context drains: the slot
layer's UNLOCKED pass makes the owed command watchdog first
(`XhciCommandDrainOwedArm`, ahead of its re-entry guard, so a pass another
CPU runs inside `SubmitTransfer` cannot hide it), and
`XhciRootHubDeferredWork(UNLOCKED)` and the status queries
(`XhciRootHubDrainOwedArms`) make the ports'. The watchdog is armed only if
the command it watches is still the outstanding one in the phase it
watches; one that has completed or moved on is dropped. Nothing else gets an
owed arm made. The first build of 24.4 also had a DEFER context that already
announced - a root-hub feature callback, a port timer, the recovery -
announce whenever an arm was owed, so that usbport's root-hub peek would make
it at once. That announcement ran under the timer-list lock and closed C2
and C3 (section 3), so it went with round 5's fix: an arm a DEFER context owes
now waits for the next event DPC, root-hub status query or health poll. A
reset's watchdog waits for little, because the reset's own completion raises
a Port Status Change Event, and the event DPC that drains it makes the arm.

**What it costs.** An arm owed from an endpoint callback waits for the next
event DPC (every interrupt), root-hub peek or health poll: a resume through
a virtual hub's port 1 may end up to one poll interval (~500 ms) late on
NT 6.x at switch 1 or 2, and a command watchdog pumped from `OpenEndpoint` or
`SubmitTransfer` starts up to that much late (a 5 s net). The abort-wait
re-arm from a timer callback waits the same; `XHCI_COMMAND_AGE_MS`,
`XHCI_PORT_AGE_MS` and `XHCI_RECOVERY_DELIVERY_POLLS` all clear it. Since
round 5 an arm a root-hub feature callback owes waits the same way: on an
idle bus a host-driven resume's 20 ms timer can start up to one poll
interval late.

**What Ex adds.** It answers an NTSTATUS - 0, `0xC000009A` when its pool
allocation fails - so on this tier a lost arm is reportable where the legacy
service's 0 meant nothing. A refusal is counted in `AsyncArmsRefused` and
treated as a lost arm at once: the command watchdog is re-owed; a port's
arm is owed again (`RhTimerFailures`), never given up, because the PORTSC
write has already started the reset or resume and giving it up would orphan
it - `XHCI_PORT_AGE_MS` retires it with its report if Ex keeps refusing; the
recovery's arming is released, re-requested and charged exactly as its
age-out would have. A failure touches only the operation it was for, and a
watchdog context is latched only while it still watches the outstanding
command, so a stale writer on another CPU cannot overwrite a newer owe
(the review's three findings, `runs/run-24.md`, "24.4"). The handle out-pointer and
`UsbPortCancelAsyncCallback` are not used: a stale callback is still the
rule, and Windows 7's StopController runs a pending one early whatever the
driver holds - with a live epoch, so an abort issued on a pending command or
a resume ended early at that moment is what the stop path already tolerates.

Not chosen: arming only where the timer-list lock is known held (no such
context exists, and a heartbeat re-armed from its own callback could be lost
silently and meets Windows 7's cancel-all), and Ex from `SubmitTransfer`
(the ABBA above).

The suite holds it with three never-reset nets - on a Version 300 start the
legacy service is never called; Ex is never called from a timer delivery, a
root-hub feature callback or an endpoint callback (the model is told which
it is standing in for, and `SubmitDepth` marks `SubmitTransfer`); Ex never
with its lock byte set - and six vectors (`test_nt6_arm_*`): the tier choice;
a deferred command watchdog made by the next UNLOCKED drain and dropped for
a command that ended first; the timeout's re-arm owed and made by the poll,
and a refused arm re-owed; a feature callback's reset deferred, announced by
nothing, and armed by the next status query, the closed-gate case armed by
the event DPC, a
port timer that does not make another port's owed arm, and a refused port
arm; the recovery armed from the poll and its self-test's watchdog owed; a
port-1 resume and an `OpenEndpoint` and `SET_ADDRESS` behind a virtual hub.
Eighteen mutations, each flipping a context's mode or
removing a drain, were run against them (`runs/run-24.md`, "24.4").

### Where the root hub may be announced (round 5 of 24.3.4, 2026-09-28)

The rule on the Version 300 tier: `UsbPortInvalidateRootHub` is called only
from a context in which usbport holds no spin lock. usbport's own takers of
RH-IntrEp hold nothing, so this keeps RH-IntrEp a lock nothing is held under
(section 3). `XHCI_EXTENSION.RootHubReportThroughDpc` carries it, set in
StartController from the version presented alone: the lock orders are
usbport's whether or not it wrote the Ex slot, and `InterruptDpcEx`'s port
bit exists on every Version 300 registration.

| Context | usbport holds | What it does with a latched change |
|---|---|---|
| the health poll (`CheckController`, including the call usbport makes from its own root-hub DPC) | nothing | announces: `XhciRootHubDeferredWork` in UNLOCKED mode |
| `ResumeController` | its suspend/resume semaphore or event only | announces, the same way |
| the event DPC (`InterruptDpcEx`) | the ISR-DPC lock | makes the owed arms, takes the change under the same gate and bookkeeping as an announcement and returns it (`XhciRootHubDeferredReport`); `xhciInterruptDpc` sets `USBPORT_DPC_EX_PORT_CHANGE`, and usbport's `IsrDpc` calls `Ev_Rh_IntrEp_Invalidate` after releasing its lock. Counted in `RootHubChangesReported` |
| root-hub feature callbacks, port timers, the recovery (DEFER) | the timer-list lock or nothing, and it cannot tell which | latches it in `RootHubInvalidatesOwed` and announces nothing |
| `RH_GetPortStatus` / `RH_GetHubStatus` | the MP lock from `Endpoint1_Peek`, or nothing | drains the owed arms and never announces: the change goes back in the answer |
| endpoint callbacks | the MP-call lock and EpList | never announce (design record 12 section 11) |

On the Version 200 tier nothing moves: the event DPC and the DEFER contexts
announce as they always have, and no NT 5.x usbport reads `InterruptDpcEx`'s
return.

A reported change costs nothing, since usbport makes the identical call a
few hundred instructions later in the same DPC pass. A latched one waits for
the next event DPC, which any interrupt brings, or for the health poll: up to
one poll interval, about 500 ms. Most root-hub changes are followed by a Port
Status Change Event and are reported at once. The ones that wait are those no
hardware event follows: a change a virtual hub makes up at 1 or 2, and the
end of a host-driven resume when no Port Link State Change follows the U0
write (design record 12 section 11). The static read ranked two faster
alternatives above this one - a driver-owned DPC queued by the DEFER
contexts, which needs `KeInitializeDpc` and `KeInsertQueueDpc` on the import
list and departs from the ABI record's "no private DPCs", and a No-Op command
whose completion interrupt would run the event DPC - and the owner chose the
latch.

The notification gate rules a report as it rules a call. On NT 6.x usbport's
root-hub queue drives it, not the service: it is open while usbhub has its
status-change IRP queued (`docs/usb-xhci-info/usbport-miniport-abi.md`,
section 4, "The root hub on NT 6.x").

The suite holds it with one never-reset net - on a Version 300 start the
service is never called from inside the event DPC, a timer delivery, or a
modelled root-hub feature or endpoint callback - and `test_nt6_rh_report`: a
Port Status Change Event reported through the bit and counted, a change a
DEFER context latched reported by the next event DPC with no event of its
own, the bit turned into the invalidate by the model of `IsrDpc`, the health
poll announcing, a closed gate reporting nothing, and the Version 200 tier
announcing from both contexts as before. Six mutations of the fix were each
killed (`runs/run-24.md`, "Round 5 on the fixed build").

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
| The re-arm proves IE came back up | a transient all-ones read (must not read as "already armed"), a swallowed write caught by the read back, and an exhausted retry budget that escalates to a RESET request; an ISR claim after the write taken as delivery owed to the next pass, and the unmask taking no claim as proof |
| The lock is created once, in `DriverEntry` | `commandLockInits`, plus the "a start creates no lock" vector |
| Actual mutual exclusion | not checkable here. The host suite is single-threaded: it sees the shape of the code around the lock, never a race. This is a review property plus the Phase 2d SMP VM |

That last row is the honest limit of this document. A single-threaded model can
prove that a call happens under a lock and that a service does not; it cannot
prove that the lock is the right one. The Phase 4 checkpoint's SMP run, with
Driver Verifier enabled, is what tests the design rather than its shape.

## 10. Open against the SMP checkpoint

- The ISR/mask interleaving is closed by argument and by the direction of a
  single bit, and answered on the SMP VM: batch 7a-V's guest 2d reading below,
  and Phase 11's stress runs on the same guest, show neither an interrupt
  storm nor a lost interrupt following a `DisableInterrupts` under load.
- The drain's hold time is unmeasured. Phase 6 adds work inside it; section 8
  is the trigger.
- `FlushInterrupts` is counted, never acted on, and the check written here
  cannot be run: it wants a D0 transition, and neither target can produce one.
  `ResumeController` has never run on Windows 2000 - no QEMU configuration
  delivers a sleep state, and that is a published limitation - while Windows
  98's idle "suspend" of this controller is a software halt with the device
  left in D0, so nothing writes a power register at suspend time
  (`lessons.md`). A zero `InterruptFlushes` on either target is therefore the
  expected reading and says nothing about whether the call site was read
  right; `usbport-miniport-abi.md`, "`FlushInterrupts`: the call site the
  mirror does not have", is where that question is actually settled, out of
  all three shipping binaries.
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

Measured at the batch 11-A tip: the release build had exactly three writable
process-global objects, and its whole `.data` section read as zero (348
bytes: 324 of objects, the rest section padding). Zero-filled is the stronger
statement of the two. It means no initialised writable global exists anywhere
in the image, so there is no table, cache or default that a second controller
could find already populated by the first.

**That measurement is out of date in both halves, and the table below now
carries six objects** (the audit of 2026-09-16). Three post-date the sweep,
and one of them is initialised and nonzero - `XhciResourcesRequired` - so the
"`.data` reads as zero" claim no longer holds as written. Re-run the
`link -dump -symbols` sweep before quoting a count or a section size from this
section; what the table argues about each object is independent of the sweep
and stands.

| Object | Bytes | What it is | Why process-global is correct |
|---|---|---|---|
| `xhciControllerLock` (`src/xhci_cmd.c`) | 4 | The one `KSPIN_LOCK`, section 2 | It carries no controller information at all. It is a mutual-exclusion primitive, and the alternative (a lock in the extension) is the race section 2 exists to remove. Two controllers serialize against each other; that is a throughput cost, not shared state |
| `xhciStartEpoch` (`src/xhci_cmd.c`) | 4 | The start-token allocator | A token source, not a count of anything. No controller's behaviour depends on its value; each extension records the token it was handed, and every comparison is for equality. Allocated with `InterlockedIncrement` because it is shared and PnP can start two controllers at once |
| `XhciRegPacket` (`src/xhci_dispatch.c`) | 316 | The registration packet | Write-once, before registration, and read-only for the life of the load. Every assignment to it is inside `xhciFillPacket`, whose only caller in a shipping image is `DriverEntry` (the second caller is `XhciFillPacketForTest`, compiled only under `XHCI_HOST_TEST`). It is also the service table the miniport calls through, so the ABI record says to keep it global |
| `xhciDeliveryLock` (`src/xhci_cmd.c`), added 2026-09-13 after the sweep above was taken | 4 | The second `KSPIN_LOCK`, section 7 "Where a completion may be handed over" | The same argument as the first: a mutual-exclusion primitive carrying no controller information, held across one usbport service call so that the call is made at DISPATCH_LEVEL from every context. Two controllers serialize their completion hand-overs against each other, which is a throughput cost and not shared state |
| `XhciResourcesRequired` (`src/xhci_dispatch.c`), added after the sweep | 4 | `MEMORY \| INTERRUPT`, the resource mask handed to usbport at registration | **The one INITIALISED writable global, so it is why the ".data reads as zero" claim above no longer holds.** It is non-const because M10 found the NT 6.x bit values differ from the NT 5.x ones, so the mask is chosen at `DriverEntry` from the interface version and cannot be a compile-time constant. Written once, in `DriverEntry`, before any controller exists; read by the packet fill. It carries no controller information - it is a statement about the usbport this image registered with, which is one thing per load |
| `xhciInterfaceVersionPresented` (`src/xhci_dispatch.c`), added after the sweep | 4 | Which miniport interface version this load registered under | Write-once in `DriverEntry`, on the same line of reasoning as the object above and for the same reason: the version is a property of the `usbport.sys` this image bound to, not of a controller, and every controller started by this load registered through it. It is branched on at `StartController` to set the two delivery gates, which is a read of a value that cannot change after the write that precedes registration. **The write-once argument is the whole of its safety**, and it is stated here because a second writer would make it per-controller state in a process-global slot |

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

## Appendix A. The binary readings behind `legal-provenance.md` section 4

Each item is a reading of a third-party binary, in the words of the
`docs/contributing/legal-provenance.md` section 4 row that indexes it by
method; that row keeps the fact, the method tag, the binary and a pointer
to the item here.
References inside an item ("as above", "the next row", "the same",
"section 2") are to that table and that file as the row stood there; read
them from the row that points here and its neighbours.

| Item | Fact | Method | Where recorded |
|---|---|---|---|
| A.1 | **NT 6.x `usbport.sys` keeps its spin locks at fixed offsets of the FDO extension, and the miniport extension at a fixed delta from it, per build**: every address in this row and the ten after it is an RVA (the kd address at image base `0x10000`, minus `0x10000`). Vista SP2 x86 (6.0.6002.18005): miniport ext = FDO + `0x1084` (`add esi,0FFFFEF7Ch`, `USBPORTSVC_RequestAsyncCallbackEx` `0x17F86`), timer-list lock `+0xD04` (`add esi,0D04h` `0x17FAA`, acquire `0x17FB2`), timer list head `+0xD74` (`mov edx,dword ptr [eax+0D78h]` `0x18219`), EpList `+0xCD0` (`lea ecx,[esi+0CD0h]` `0x109C`), MP lock `+0x798` (`lea edi,[esi+798h]` `0x1156`, `MPf_CheckController`), ISR-DPC lock `+0xD28` (`lea ecx,[ebx+0D28h]` `0x1F02`, `USBPORT_IsrDpc`), MP-call lock `+0xD00` (`lea edi,[esi+0D00h]` `0x5416`, `MPf_SubmitTransfer`), I/O-count lock `+0x1E4` (`lea ebx,[esi+1E4h]` `0x4F58`), RH-IntrEp lock `+0x2F8` (`lea ecx,[ebx+2F8h]` `0x13D28`, `USBPORT_Ev_Rh_IntrEp_Invalidate`), MpSuspendResume semaphore `+0x46C` (address not read); Windows 7 SP1 x86 (6.1.7601.17514): FDO + `0xF00` (`add esi,0FFFFF100h` `0xFD15`), timer-list `+0xB94` (`add esi,0B94h` `0xFD38`, acquire `0xFD40`), list head `+0xC04` (`add eax,0C04h` `0xFEF5`), EpList `+0xB68` (`lea ecx,[esi+0B68h]` `0x10ED`), MP `+0x638` (`lea edi,[esi+638h]` `0x1629`), ISR-DPC `+0xBB8` (`lea ecx,[ebx+0BB8h]` `0x2DAC`), MP-call `+0xB90` (`lea eax,[esi+0B90h]` `0xB9D7`, `MPf_OpenEndpoint`), I/O-count `+0x1EC` (`lea ebx,[esi+1ECh]` `0x3574`), RH-IntrEp `+0x310` (acquire `0xEAB3`), MpSuspendResume event `+0x484` (`lea eax,[edi+484h]` `0xDE5C`, `MPh_AcquireMpSuspendResumeLock`); Vista SP2 x64 (6.0.6002.18005): FDO + `0x16E0` (`lea r12,[rcx-16E0h]` `0x33927`), timer-list `+0x11C0` (`lea rcx,[r12+11C0h]` `0x33952`), list head `+0x12A0` (`lea rdx,[r12+12A0h]` `0x33B98`), EpList `+0x1160` (`lea rcx,[rdi+1160h]` `0x7071`), MP `+0xA40` (`add rcx,0A40h` `0x1712`), ISR-DPC `+0x1208` (`lea rcx,[rdi+1208h]` `0x376AF`), MP-call `+0x11B8` (`add rcx,11B8h` `0x1F2D`, `MPf_SubmitTransfer`), I/O-count `+0x1F8` (`lea rcx,[r14+1F8h]` `0x1C307`), RH-IntrEp `+0x3F0` (acquire `0x2A738`), MpSuspendResume semaphore `+0x5E0` (`add rcx,5E0h` `0x108E`); Windows 7 SP1 x64: FDO + `0x1500` (`lea r12,[rcx-1500h]` `0x29447`), timer-list `+0xFE0` (`lea rcx,[r12+0FE0h]` `0x29474`), list head `+0x10C0` (`lea rdx,[r12+10C0h]` `0x2961C`), EpList `+0xF88` (`lea rcx,[rbx+0F88h]` `0x5F4D`), MP `+0x868` (`add rcx,868h` `0x166F`), ISR-DPC `+0x1028` (`lea rcx,[rbx+1028h]` `0x2D4E1`), MP-call `+0xFD8` (`add rcx,0FD8h` `0x1EF5`, `MPf_SubmitTransfer`), I/O-count `+0x200` (`lea rcx,[r12+200h]` `0x15DCF`, `USBPORT_Core_IncIoRequestCount`), RH-IntrEp `+0x3E0` (acquire `0x20753`), MpSuspendResume `+0x5D0` (`add rcx,5D0h` `0x108A`, then `KeWaitForSingleObject` `0x1096`; object type not read). The field 8 bytes after the timer-list lock is the `USBPORT_IsrDpc` KDPC, not a lock: Windows 7 x64 passes `lea rcx,[rbx+0FE8h]` to `KeInsertQueueDpc` in `USBPORT_InvalidateController` (`0x5330`, call `0x533B`) and in `USBPORT_InterruptService` (`0x2D3BA`, call `0x2D3C6`); Vista x64 `+0x11C8` the same (`KeInsertQueueDpc` `0x608B`); Windows 7 x86 `+0xB98` (`lea eax,[esi+0B98h]` `0x1777D`, `USBPORT_InitStateVars`, then `KeInitializeDpc` with `USBPORT_IsrDpc`) | static (`kd -z` whole-`.text` dumps of the four builds with the public PDBs, 2026-09-28; four agents' reports in `out\t24-4`; every cited instruction re-checked against its dump line the same day, and the Windows 7 x64 `+0x10C0`, `+0x200`, `+0x5D0` and `+0xFE8` instructions read from the dump directly, the RIP-relative import slots resolved by hand as next instruction + disp32) | design 05 "Where a timer may be armed"; `src/xhci_hw.h` `XHCI_ARM_*`; abi section 6; run-24 "24.4" (roadmap 24.4) |
| A.2 | **NT 6.x `usbport.sys` calls the miniport's `CheckController` holding no usbport spin lock**: `MPf_CheckController` takes the MP lock only to test a flag and releases it before the call. Windows 7 x64 `MPf_CheckController` `0x1648`: `add rcx,868h` `0x166F`, `KeAcquireSpinLockRaiseToDpc` `0x1676`, `test byte ptr [rbx+4F0h],2` `0x167C`, flag clear: `KeReleaseSpinLock` `0x168E` (`dl` = the saved IRQL), then `call qword ptr [r11+0E0h]` `0x16A2` with `rcx = [rbx+400h]`; flag set: release `0x16AB` and no call (kd labels the two import slots `USBPORT_RootHub_ClassCommand+0x2fc` / `+0x5ec`; next instruction + disp32 gives `_imp_KeAcquireSpinLockRaiseToDpc` / `_imp_KeReleaseSpinLock`). Vista x86 `0x113D`: acquire `+0x798` `0x115E`, release `0x1171`, `call dword ptr [eax+84h]` `0x1183`; Windows 7 x86 `0x1607`: acquire `+0x638` `0x1631`, release `0x1644`, `call dword ptr [eax+84h]` `0x1656`; Vista x64 `0x16F4`: `add rcx,0A40h` `0x1712`, acquire `0x1719`, release `0x1731`, `call qword ptr [r11+0E0h]` `0x1745` | static (Windows 7 x64 read first-hand, `kd -z ... -c "uf usbport!MPf_CheckController; q"`, 2026-09-28; the other three from the whole-`.text` dumps by the agents' reports in `out\t24-4`, each instruction re-checked against the dump) | design 05 "Where a timer may be armed"; `src/xhci_hw.h` `XHCI_ARM_*`; abi section 6; run-24 "24.4" (roadmap 24.4) |
| A.3 | **NT 6.x `usbport.sys` calls the miniport's `InterruptDpcEx` under its ISR-DPC lock only**: `USBPORT_IsrDpc` takes the MP lock for a flag test, releases it, then holds the ISR-DPC lock across the call. Windows 7 x64 `USBPORT_IsrDpc` `0x2D3EC`: MP `KeAcquireSpinLockAtDpcLevel` `0x2D4C1`, `KeReleaseSpinLockFromDpcLevel` `0x2D4DB`, `lea rcx,[rbx+1028h]` `0x2D4E1` + acquire `0x2D4E8`, `mov rcx,qword ptr [rbx+400h]` / `call r8` `0x2D561` (`r8 = [[rbx+408h]+2D0h]`, packet offset `0x298` = InterruptDpcEx), release `0x2D5D1`; after it, with no lock, `USBPORT_Ev_Rh_IntrEp_Invalidate` `0x2D5EE` (when `test sil,1` or `test sil,2` hits, `0x2D5DF`-`0x2D5E9`) and `USBPORT_Xdpc_Signal` `0x2D600`. Vista x86: MP acquire `0x1EE7`, release `0x1EFC`, ISR-DPC acquire `0x1F08`, `call usbport!MPf_InterruptDpcEx` `0x1F9B`, release `0x2039`; Windows 7 x86 `0x2CD7`: MP `0x2D91` / `0x2DA6`, ISR-DPC acquire `0x2DB2`, `call usbport!MPf_InterruptDpcEx` `0x2E15` (whose `call edx` at `0x1850` is packet `+0x194`), release `0x2E83`, then `USBPORT_Ev_Rh_IntrEp_Invalidate` `0x2E96`; Vista x64 `0x37570`: MP `0x3767E` / `0x376A9`, ISR-DPC acquire `0x376B6`, `call r8` `0x3775B` (`mov r12d,eax` `0x3775E`, bits 0/1 tested before `USBPORT_Ev_Rh_IntrEp_Invalidate` `0x37823`), release `0x37802` | static (Windows 7 x64 read first-hand, `kd uf usbport!USBPORT_IsrDpc`, 2026-09-28; the other three by the agents' reports in `out\t24-4`, re-checked against the dumps) | design 05 "Where a timer may be armed"; `src/xhci_hw.h` `XHCI_ARM_*`; abi section 6; run-24 "24.4" (roadmap 24.4) |
| A.4 | **NT 6.x `usbport.sys` calls a miniport timer callback from `USBPORT_AsyncTimerDpc` holding the timer-list lock, and on Windows 7 also from `USBPORT_CancelAllAsyncTimerCallbacks` holding no lock**: the DPC acquires the timer-list lock, unlinks the entry, calls the callback, calls `USBPORT_Core_DecIoRequestCount` (the I/O-count lock, nested), releases, and frees the entry after the release. Windows 7 x64 `0x2931C`: `lea rcx,[rsi+0FE0h]` `0x29350`, acquire `0x29357`, unlink `0x293A9`-`0x293BB`, `call qword ptr [rbx+0D8h]` `0x293CE`, Dec `0x293E9`, release `0x293F8`, `jmp` to `ExFreePoolWithTag` `0x29420`; Vista x86 `0x18255`: acquire `0x18283`, `call dword ptr [esi+78h]` `0x1831A`, Dec `0x1832A`, release `0x1833C`, free `0x18347`; Windows 7 x86 `0xFF2B`: acquire `0xFF58`, unlink `0xFF9E`-`0xFFA6`, `call dword ptr [esi+78h]` `0xFFBB`, Dec `0xFFCB`, release `0xFFDD`, free `0xFFE8`; Vista x64 `0x337DC`: acquire `0x33817`, unlink `0x3388C`-`0x3389E`, `call qword ptr [rbx+0D8h]` `0x338B1`, release `0x338DB`, free `0x33901`. Windows 7 only, both architectures: `MPf_StopController` calls `USBPORT_CancelAllAsyncTimerCallbacks` before the miniport's `StopController`; it pops each entry under the timer-list lock, releases the lock, calls `KeCancelTimer`, and when that returns TRUE raises to DISPATCH_LEVEL and calls the miniport's callback synchronously with the lock not held, then decrements, frees, and re-takes the lock for the next entry. Windows 7 x86: `MPf_StopController` `0x1CA9A` calls it at `0x1CABE`, before `call dword ptr [eax+58h]` `0x1CACE`; the function `0x25003`: acquire `0x25063`, release `0x25083`, `KeCancelTimer` `0x2508C`, `KfRaiseIrql` `0x250DD`, `call dword ptr [esi+78h]` `0x2512B`, re-acquire `0x25160`, final release `0x25174`. Windows 7 x64: `MPf_StopController` `0x1880` calls it at `0x18AE`, before `call qword ptr [r11+88h]` `0x18C4`; the function `0x29914`: acquire `0x2998E`, release `0x299B4`, `KeCancelTimer` `0x299BE`, `mov cr8,rax` (2) `0x29A1C`, `call qword ptr [rdi+0D8h]` `0x29A6B`, re-acquire `0x29AA8`, final release `0x29AC0`. Neither Vista build has the function: neither public-PDB symbol list names `USBPORT_CancelAllAsyncTimerCallbacks`, and Vista x64 calls `StopController` from `USBPORT_StopDevice` (`call rax` `0x3692`) | static (Windows 7 x64 `USBPORT_AsyncTimerDpc` read first-hand with `kd uf`, 2026-09-28; the other DPCs and both `CancelAllAsyncTimerCallbacks` readings from the whole-`.text` dumps by the agents' reports in `out\t24-4`, re-checked against the dumps; the Vista absence from the four builds' symbol lists, the same PDBs) | design 05 "Where a timer may be armed"; `src/xhci_hw.h` `XHCI_ARM_*`; abi section 6; run-24 "24.4" (roadmap 24.4) |
| A.5 | **NT 6.x `usbport.sys` calls the root-hub feature callbacks (`RH_SetFeaturePort*`, `RH_ClearFeaturePort*`) with the timer-list lock from some paths and without it from others**: with it from `USBPORT_RootHub_PortRequest` (usbhub's port requests through the root hub's control endpoint): Vista x86 acquire `0x13E73`, release `0x14084`; Windows 7 x86 `0xEBB0`, acquire `0xEBE3`, the twelve callbacks through a jump table (`call dword ptr [ecx+0BCh]` `0xEC64` to `call dword ptr [ecx+0E8h]` `0xEDA5`), release `0xEDCD`; Vista x64 `0x27478`, acquire `0x274EA`, release `0x2773C`; Windows 7 x64 `0x1E064`, acquire `0x1E0AF`, release `0x1E2D6`. The exception is `SetFeaturePortPower` when the registration packet's `MiniPortFlags` has bit `0x10`: the function releases the lock, calls `USBPORT_RootHub_PowerUsb2Port`, and re-takes it (Windows 7 x86 `test byte ptr [ecx+20h],10h` `0xEC77`, release `0xEC8A`, re-acquire `0xECAA`; Vista x64 `test byte ptr [rax+3Ch],10h` `0x2761A`, `0x2762B`, `0x27646`; Windows 7 x64 `test byte ptr [rax+3Ch],10h` `0x1E1B7`, `0x1E1C7`, `0x1E1E2`; Vista x86 release `0x13F3F`, re-acquire `0x13F61`, flag test not read). With it also from `USBPORT_SyncPowerAndChirpUsb2Ports` (`SetFeaturePortPower`, `ClearFeaturePortOvercurrentChange`: Windows 7 x86 acquire `0x1B0E9` / `call dword ptr [eax+0C0h]` `0x1B0FD`; Vista x64 `0x26B79` / `call qword ptr [r8+158h]` `0x26B93`; Windows 7 x64 `0x1DAB3` / `0x1DACD`; Vista x86 acquire `0x2A9DF`) and `USBPORT_HardResetRootPdo` (`ClearFeaturePortEnable`: Windows 7 x86 `0x22D09` / `call dword ptr [eax+0CCh]` `0x22D1D`; Vista x64 `0x2BA00` / `0x2BA19`; Windows 7 x64 `0x21475` / `call qword ptr [r8+170h]` `0x2148E`; Vista x86 acquire `0x2E016`). Without it from `USBPORT_UserSetRootPortFeature`, `USBPORT_UserClearRootPortFeature`, `USBPORT_UserRawResetPort`, `USBPORT_RootHub_HubRequest` and `USBPORT_RootHub_PowerUsb2Port` (Vista x64 `call qword ptr [rax+158h]` at `0x29B46`, `0x35993`, `0x2A379`, `call qword ptr [rax+1A8h]` `0x2785F`; Windows 7 x86: none of those functions references `+0xB94`; Vista x86 and Windows 7 x64 not read) | static (whole-`.text` dumps, 2026-09-28, by the agents' reports in `out\t24-4`, each cited instruction re-checked; the "without" paths rest on a scan of those functions for the lock offsets, which would miss a lock taken inside a helper) | design 05 "Where a timer may be armed"; `src/xhci_hw.h` `XHCI_ARM_*`; abi section 6; run-24 "24.4" (roadmap 24.4) |
| A.6 | **NT 6.x `usbport.sys` calls `RH_GetPortStatus` and `RH_GetHubStatus` under its MP lock from `USBPORT_RootHub_Endpoint1_Peek`, and the root-hub status callbacks under none of its MP, ISR-DPC or timer-list locks from every other caller**: `Endpoint1_Peek` is the root hub's status-change pipe service; the x64 dumps show `USBPORT_Ev_Rh_IntrEp_Dpc` -> `USBPORT_Rh_IntrEp_Worker` -> `USBPORT_RootHub_Endpoint1_Peek` (Windows 7 x64 `0x20983`, `0x20DA0`; Vista x64 `0x2A9F9`, `0x2AF95`), the DPC queued by `USBPORT_Ev_Rh_IntrEp_Invalidate`, which `USBPORTSVC_InvalidateRootHub` and `USBPORT_IsrDpc` call. Under the MP lock: Vista x86 acquire `0x141CD`, `call dword ptr [eax+0B4h]` `0x14207`, `call dword ptr [eax+0B8h]` `0x142C0`, release `0x142DB`; Windows 7 x86 `0xF20E`, `0xF248`, `0xF2DA`, `0xF300`; Vista x64 `0x289ED`, `call qword ptr [rax+140h]` `0x28A32`, `call qword ptr [rax+148h]` `0x28B2A`, `0x28B49`; Windows 7 x64 `lea rcx,[rbp+868h]` `0x1F0A2`, `0x1F140`, `0x1F1FF`, `lea rcx,[rbp+868h]` `0x1F21F`. None of those locks at `RH_GetStatus`, `RH_GetRootHubData` or the other `RH_GetPortStatus`/`RH_GetHubStatus` sites (`USBPORT_RootHub_ClassCommand`, `USBPORT_RootHub_StandardCommand`, `USBPORT_CreateRootHubPdo`, `SyncPowerAndChirpUsb2Ports`, `HardResetRootPdo` before its timer-list hold, the User* paths): e.g. Windows 7 x86 `RootHub_ClassCommand` `call dword ptr [ecx+0B4h]` `0xD7F5`, Vista x64 `0x283FB`, Windows 7 x64 `HardResetRootPdo` `call qword ptr [rax+140h]` `0x21413` before its acquire at `0x21475` | static (the agents' per-slot caller lists over the whole-`.text` dumps, 2026-09-28, in `out\t24-4`, re-checked; the DPC -> worker -> peek calls read from the two x64 dumps directly) | design 05 "Where a timer may be armed"; `src/xhci_hw.h` `XHCI_ARM_*`; abi section 6; run-24 "24.4" (roadmap 24.4) |
| A.7 | **NT 6.x `usbport.sys` calls the miniport's endpoint entry points under its MP-call lock, `StartController`, `StopController` and `PollController` under no spin lock, and `SuspendController` / `ResumeController` under only a wait object**: `SubmitTransfer` (Vista x86 `MPf_SubmitTransfer` acquire `0x5420` / `call dword ptr [ecx+6Ch]` `0x544E` / release `0x545A`; Windows 7 x86 `0x8CE5` / `0x8D13` / `0x8D1F`; Vista x64 `0x1F34` / `call qword ptr [r10+0B0h]` `0x1F6D` / `0x1F80`; Windows 7 x64 `0x1EFC` / `0x1F35` / `0x1F48`) and `OpenEndpoint` (Vista x86 `MPf_OpenEndpoint` `0x565E` / `call dword ptr [ecx+44h]` `0x567F` / `0x5696`; Windows 7 x86 `0xB9E1` / `0xBA02` / `0xBA19`; Vista x64 `MPx_OpenEndpoint` `0x2182` / `call qword ptr [rax+60h]` `0x21B0` / `0x21C3`; Windows 7 x64 `0x214A` / `0x2178` / `0x218B`); the reports find the same lock around `SetEndpointState`, `PollEndpoint`, `CloseEndpoint`, `AbortTransfer`, `SubmitIsoTransfer`, `QueryEndpointRequirements`, `Get/SetEndpointStatus`, `SetEndpointDataToggle`, `Get32BitFrameNumber`, `InterruptNextSOF` and `ResetController` (`Open/CloseEndpoint` skip it when `MiniPortFlags` bit 16 is set), and EpList held by the callers of the endpoint-state family (read on Vista x86, `USBPORT_iSetGlobalEndpointStateTx`; taken from the reading brief on the other three, not re-derived). `StartController` (Vista x86 `call dword ptr [eax+54h]` `0x1B6C7`; Windows 7 x86 `0x1317F`; Vista x64 `call qword ptr [rax+80h]` `0x18B1`; Windows 7 x64 `0x1815`) and `StopController` (Vista x86 `0x20A93`; Windows 7 x86 `0x1CACE`; Vista x64 `0x3692`; Windows 7 x64 `0x18C4`) with no usbport spin lock; `PollController` from `USBPORT_DM_TimerDpc` after it releases the MP lock (Vista x64 `call rdx` `0x13ACE`; the other builds not read); `SuspendController` (Vista x86 `0x207A9`; Windows 7 x86 `0xE661` after `MPh_AcquireMpSuspendResumeLock` `0xE578`; Vista x64 `0x13D4` after `0x12DE`; Windows 7 x64 `0x1393` after `0x128B`) and `ResumeController` (Vista x86 `0x2094C`; Windows 7 x64 `0x150F`) holding only the MpSuspendResume semaphore (Vista) or event (Windows 7 x86), with `DisableInterrupts` inside Suspend under the ISR-DPC lock | static (the agents' per-wrapper tables over the whole-`.text` dumps, 2026-09-28, in `out\t24-4`, each cited instruction re-checked) | design 05 "Where a timer may be armed"; `src/xhci_hw.h` `XHCI_ARM_*`; abi section 6; run-24 "24.4" (roadmap 24.4) |
| A.8 | **No NT 6.x `usbport.sys` function takes the MP, MP-call, ISR-DPC or EpList lock while holding the timer-list lock, or the timer-list lock while holding the MP or ISR-DPC lock**: the timer-list lock's holders are `USBPORT_AsyncTimerDpc`, `USBPORT_RootHub_PortRequest`, `USBPORT_SyncPowerAndChirpUsb2Ports`, `USBPORT_HardResetRootPdo`, `USBPORTSVC_RequestAsyncCallbackEx`, `USBPORTSVC_CancelAsyncCallback`, on Windows 7 `USBPORT_CancelAllAsyncTimerCallbacks`, and the zeroing in `USBPORT_InitStateVars`; inside their holds they call only the miniport, `USBPORT_Core_Inc/DecIoRequestCount` (the I/O-count lock, the one lock nested inside the timer-list lock), the kernel timer, DPC and pool routines, `USB_ZAP_LINK` and the log ring. The MP lock's regions are flag tests and sets, the two `Endpoint1_Peek` callbacks, the DM timer and I/O-count work in `USBPORT_StopDevice` / `USBPORT_DM_TimerDpc` / `USBPORT_StartDM_Timer`, and on Vista x64 the `ReadCfgFlag` slot from `ReadUsb2CfgFlag` (`call rdx` `0x13CF9`, only when that slot is non-NULL); the ISR-DPC lock's regions are single miniport calls (`InterruptDpcEx`, `RH_EnableIrq`/`RH_DisableIrq`, `EnableInterrupts`/`DisableInterrupts`). EpList and the timer-list lock are never nested either way: `USBPORT_IsrDpc` takes EpList only after releasing the ISR-DPC lock (Vista x64 `USBPORT_TxAcquireLock` `0x37835` after `0x37802`), and Vista x64's `SyncPowerAndChirpUsb2Ports` releases the MP-call lock (`0x269B4`, `0x269CE`, `0x26CBE`) before its timer-list holds. Windows 7 x86: a script over every `+0x638`, `+0xB94`, `+0xBB8`, `+0xB90` and `+0xB68` operand in `.text`, 109 hits, listed by enclosing function, and a two-level call-graph walk between the EpList and timer-list holders, empty both ways | static (the agents' census of each build's whole-`.text` dump, 2026-09-28, in `out\t24-4`: Windows 7 x86 by script over all hits, Vista x64 and Windows 7 x64 by per-holder tables, Vista x86 by the agent's summary without per-site addresses; the MP-call-lock half rests on the callee lists inside the timer-list holds) | design 05 "Where a timer may be armed"; `src/xhci_hw.h` `XHCI_ARM_*`; abi section 6; run-24 "24.4" (roadmap 24.4) |
| A.9 | **The NT 6.x services a timer callback of this driver may call take none of usbport's MP, ISR-DPC or timer-list locks**: `USBPORTSVC_InvalidateController` -> `USBPORT_InvalidateController` takes only the I/O-count lock (`USBPORT_Core_AllocIoRequest`) and queues a DPC (Vista x86 `USBPORT_InvalidateController` `0x2195A`; Windows 7 x86 `0x1DBA8` -> `0x1DAF8`; Vista x64 `0x621C` -> `0x6008`, `KeInsertQueueDpc` on the IsrDpc KDPC `0x608B`; Windows 7 x64 `0x546C`, `KeInsertQueueDpc` on `+0xFE8` `0x533B`). `USBPORTSVC_InvalidateRootHub` -> `USBPORT_Ev_Rh_IntrEp_Invalidate` takes the RH-IntrEp lock and, under it, EpList through `USBPORT_ReferenceEndpoint`, then queues the `Ev_Rh_IntrEp` DPC (Vista x86 `+0x2F8` acquire `0x13D31`, `ReferenceEndpoint` `0x13D9C`; Windows 7 x86 `0xF9E3` -> `0xEA5C`, `+0x310` `0xEAB3`, `0xEB19`; Vista x64 `0x2990C` -> `0x2A678`, `+0x3F0` `0x2A738`, `0x2A7B5`; Windows 7 x64 `0x1FC84` -> `0x206E4`, `+0x3E0` `0x20753`, `USBPORT_ReferenceEndpoint` `0x207D2` whose `call usbport!USBPORT_AcquireEpListLock` is at `0x714C`, release `0x20827`). `USBPORTSVC_CompleteTransfer` / `USBPORTSVC_CompleteIsoTransfer` take no FDO lock of their own (`USBPORT_Core_iCompleteTransfer`; Windows 7 x86 `0x8D32` / `0x958C`, Vista x64 `0x1B6D8` / `0x138A0`, Windows 7 x64 `0x154DC` / `0xF12C`), and the split-child path reaches the MP-call lock through `MPf_NotifyTransferQueueState` only when that packet slot is non-NULL. `USBPORTSVC_InvalidateEndpoint` is an `AssertSig` stub (Windows 7 x86 `0x2CA9`, `ret 8` `0x2CC4`; Vista x86 `0xA435`; Vista x64 `0x1B6BC`; Windows 7 x64 `0x154C0`). The legacy `USBPORTSVC_RequestAsyncCallback` takes only the I/O-count lock. The Windows 7 x64 agent's walk did not list the EpList take inside `Ev_Rh_IntrEp_Invalidate`; the call at `0x207D2` was read from the dump | static (the agents' depth-3 call walks over the whole-`.text` dumps, 2026-09-28, in `out\t24-4`, re-checked; Windows 7 x64 `USBPORTSVC_InvalidateController` and `USBPORTSVC_CompleteTransfer` also read first-hand with `kd uf`) | design 05 "Where a timer may be armed"; `src/xhci_hw.h` `XHCI_ARM_*`; abi section 6; run-24 "24.4" (roadmap 24.4) |
| A.10 | **NT 6.x `USBPORTSVC_RequestAsyncCallbackEx` takes seven arguments `(MiniportExt, ms, ctx, ctxLen, callback, PVOID *outHandle /* optional */, BOOLEAN skipLock)` and returns an NTSTATUS**: x86 `ret 1Ch` (Vista x86 `0x180A4`, Windows 7 x86 `0xFDD6`), skipLock `cmp byte ptr [ebp+20h],0` (Vista x86 `0x17F9D`, Windows 7 x86 `0xFD2B`); x64 (Windows 7 x64 `0x29430`) pushes seven registers and does `sub rsp,30h`, so the callback is `[rsp+90h]` (`0x295BE`, stored at `[rbp+0D8h]` `0x295CD`), the out-handle `[rsp+98h]` (`0x29654`, `mov qword ptr [r11],rbp` `0x29661`) and skipLock `[rsp+0A0h]` (`cmp byte ptr [rsp+0A0h],bpl` `0x2946A`; Vista x64 `cmp byte ptr [rsp+0A0h],0` `0x33943`). skipLock 0 takes the timer-list lock (Windows 7 x64 `lea rcx,[r12+0FE0h]` `0x29474`, `KeAcquireSpinLockRaiseToDpc` `0x2947C`; Vista x64 `0x3395A`; Windows 7 x86 `0xFD40`; Vista x86 `0x17FB2`) and, inside it, the I/O-count lock (`USBPORT_Core_IncIoRequestCount`, Windows 7 x64 `0x295E9`, Vista x64 `0x33B65`, Windows 7 x86 `0xFEB7`, Vista x86 `0x181E1`). Returns `0xC000000D` when ctxLen is 0 (Windows 7 x64 `cmp r13d,ebp` `0x294C5`, `mov ebx,0C000000Dh` `0x294CA`; Vista x64 `0x339DD`; Windows 7 x86 `0xFD7B`; Vista x86 `0x1801B`), `0xC0000095` when the size overflows on x64 (Windows 7 x64 `0x294EA`, Vista x64 `0x339FD`), `0xC000009A` when the pool allocation fails (Windows 7 x64 `0x2958F`, Vista x64 `0x33B0B`, Windows 7 x86 `0xFE78`, Vista x86 `0x1819F`), otherwise 0. The entry is `0xE8 + ctxLen` bytes on x64 (`lea edi,[r13+0E8h]` `0x294D4`) and `0x80 + ctxLen` on x86 (`RtlULongAdd`, Windows 7 x86 `0xFDE3`, Vista x86 `push 80h` `0x180AE`), from `UsbAllocPool_Z` with tag `'usbp'` (Windows 7 x64 `mov r8d,70627375h` `0x1036`); x64 layout: +0 LIST_ENTRY, +0x10 `'mTIM'`, +0x18 device object, +0x20 KTIMER, +0x60 KDPC, +0xA0 I/O-request record (tag `'Astm'`), +0xD8 callback, +0xE0 length, +0xE8 context copy. The entry is tail-inserted in the timer list and armed with `KeSetTimer` (Windows 7 x64 `0x2964E`, Vista x64 `0x33BCA`) whatever skipLock is, so a nonzero skipLock inserts it unlocked. The legacy `USBPORTSVC_RequestAsyncCallback` passes `outHandle = NULL, skipLock = 1` and returns Ex's value: Windows 7 x64 `0x296D4` (`mov byte ptr [rsp+30h],1` `0x296DD`, `call` `0x296ED`), Vista x64 `0x33C88`, Windows 7 x86 `0xFFFA` (`push 1` `0xFFFF`, `ret 14h` `0x10018`), Vista x86 `0x304F6` (`push 1` `0x304FB`, `ret 14h` `0x30514`) | static (Windows 7 x64 read first-hand, `kd uf usbport!USBPORTSVC_RequestAsyncCallbackEx`, 2026-09-28; the other builds by the agents' reports in `out\t24-4`, re-checked against the dumps; the x64 argument slots follow from the seven pushes and `sub rsp,30h`, which corrects the Windows 7 x64 report's "arg5" label for `[rsp+0A0h]`: it is the seventh) | design 05 "Where a timer may be armed"; `src/xhci_hw.h` `XHCI_ARM_*`; abi section 6; run-24 "24.4" (roadmap 24.4) |
| A.11 | **NT 6.x `USBPORTSVC_CancelAsyncCallback (MiniportExt, entryHandle, BOOLEAN lockAlreadyHeld)` cancels one armed timer under the timer-list lock and returns whether it did**: with the third argument 0 it takes the timer-list lock (Windows 7 x64 `0x29700`: `cmp r13b,r14b` `0x29744`, acquire `0x29750`; Vista x64 `0x33CB4`: `0x33CF5`, `0x33D01`; Windows 7 x86 `0x24E45`: `cmp byte ptr [ebp+10h],0` `0x24E6D`, `0x24E8A`; Vista x86 `0x3051C`: acquire `0x30561`), walks the list for the entry (FALSE if it is absent), calls `KeCancelTimer` (Windows 7 x64 `0x29821`, Vista x64 `0x33E3E`, Windows 7 x86 `0x24F3D`, Vista x86 `0x306B0`), and only when that returns TRUE unlinks the entry, calls `USBPORT_Core_DecIoRequestCount` and frees it (`ExFreePoolWithTag`: Windows 7 x64 `0x2989E`, Vista x64 `0x33EE0`, Windows 7 x86 `0x24FA4`, Vista x86 `0x3074F`) before releasing (Windows 7 x64 `0x298E8`, Vista x64 `0x33F4E`, Windows 7 x86 `0x24FEF`, Vista x86 `0x307CE`); it returns the `KeCancelTimer` BOOLEAN, so FALSE once `USBPORT_AsyncTimerDpc` owns the entry. This driver does not call it | static (the agents' reports in `out\t24-4`, re-checked against the whole-`.text` dumps, 2026-09-28) | design 05 "Where a timer may be armed"; `src/xhci_hw.h` `XHCI_ARM_*`; abi section 6; run-24 "24.4" (roadmap 24.4) |
| A.12 | **NT 6.x `usbport.sys` calls the miniport's `RH_EnableIrq` / `RH_DisableIrq` only from `MPRH_EnableIrq` / `MPRH_DisableIrq`, which take the RH-IntrEp lock (unless their caller holds it) and then always the ISR-DPC lock across the call; the root-hub status queue and the root-hub DPC reach them holding RH-IntrEp, and usbport takes RH-IntrEp itself only while holding no other spin lock**: every code address in this row and in the first, second and fourth after it is an RVA (the `kd` or `dumpbin` address at image base `0x10000`, minus `0x10000`); the third carries runtime addresses. The two wrappers set or clear FDO flag `0x400` beside the call. Windows 7 x86: Enable 0xDCAD (RH-IntrEp 0xDCE0, ISR-DPC 0xDCEE, `call [eax+0F0h]` 0xDCFF), Disable 0xDD7F (`lea ecx,[esi+310h]`, acquire 0xDDB2; `lea ebx,[esi+0BB8h]`, acquire 0xDDC0; `call [eax+0ECh]` 0xDDD1); Vista x86: Enable 0x144E0 (0x1450A, `lea ebx,[esi+0D28h]` 0x1453C, 0x1454D), Disable 0x14615 (0x1463F, 0x14671, 0x14682); Vista x64: Enable 0x273A0 (0x273D1, `lea rcx,[rsi+1208h]` 0x27415, `call [rdx+1B8h]` 0x2742B), Disable 0x272C8 (0x272F9, 0x2733D, `call [rdx+1B0h]` 0x27353; kd labels the import slots wrongly on this build, and next instruction + disp32 lands on `_imp_KeAcquireSpinLockRaiseToDpc`); Windows 7 x64: Enable 0x1DFB0 (0x1DFEA, 0x1E001, 0x1E017), Disable 0x1DEFC (0x1DF36, 0x1DF4D, 0x1DF63). Their callers: the root-hub status queue's insert callback `USBPORT_RhIntrEp_Insert_TxqIrp` -> `MPRH_EnableIrq(fdo, 1)` (Windows 7 x86 `push 1` 0xF3BA, Vista x86 0x144B2, Vista x64 0x2B33B; inlined on Windows 7 x64, ISR-DPC 0x20FFC, `call [rdx+1B8h]` 0x21012) and its remove callback `USBPORT_RhIntrEp_Remove_TxqIrp` -> `MPRH_DisableIrq(fdo, 1)` when the queue becomes empty (Windows 7 x86 0xDD6B-0xDD70, Vista x86 0x145E7, Vista x64 0x2B400; inlined on Windows 7 x64, 0x210B1, 0x210C7), the queue's lock callbacks taking RH-IntrEp (`USBPORT_RhIntrEp_Acquire_TxqLock` / `_Release_TxqLock`: Windows 7 x86 0xDBAC / 0xDC1D, Vista x86 0x13B6A / 0x13C51, Windows 7 x64 0x2121F / 0x2129F, Vista x64 0x2B58C); the tail of `USBPORT_Ev_Rh_IntrEp_Dpc`, after it has run `MPf_CheckController` and `USBPORT_Rh_IntrEp_Worker` with nothing held (Windows 7 x86 DPC 0xEE11: CheckController 0xEEF9, worker 0xEF02, RH-IntrEp 0xEF0F, `MPRH_EnableIrq(fdo, 1)` 0xEF1F when the queue is not empty; Vista x86 0x14800, 0x14809, 0x14816, 0x14826; Vista x64 0x2A9EE, 0x2A9F9, 0x2AA05, 0x2AA1A; Windows 7 x64 0x20978, 0x20983, 0x2098F, inlined ISR-DPC 0x209BD, `call [rdx+1B8h]` 0x209D3); and, with nothing held, `MPf_SuspendController` (Enable), `USBPORT_ResumeUsbController`, `USBPORT_StopRootHubPdo` and `USBPORT_TurnUsbControllerOn` (Disable; Windows 7 x86 0xE5D0, `push 0` 0x19E8B, `push 0` 0x22071, 0x2237A; Vista x86 0x2073C, 0x29CD1, 0x29683, 0x2BB07). The queue's inserter `USBPORT_RH_NeoQueueAsyncTransfer` is reached with nothing held (Windows 7 x86 `IoCsqInsertIrp` 0xD936, from `USBPORT_Core_QueueTransferUrb` 0x731A), and no `KeSetTargetProcessorDpc` appears in any of the four images | static (`kd -z` whole-`.text` dumps with the public PDBs, `out\t24-4\<build>-text.txt`, 2026-09-28, by an agent's read in `out\t24-4\static-24-4-deadlock.md`; the Windows 7 x86 `MPRH_DisableIrq` sites, its four callers and the `IsrDpc` and `Ev_Rh_IntrEp_Invalidate` sites re-checked against the dump the same day) | design 05 section 3 and "Where the root hub may be announced"; abi section 1 "The `InterruptDpcEx` contract" and section 4 "The root hub on NT 6.x"; `src/xhci.h` `RootHubReportThroughDpc`; run-24 "Round 5 on the fixed build" |
| A.13 | **NT 6.x `USBPORT_IsrDpc` calls `USBPORT_Ev_Rh_IntrEp_Invalidate` after releasing its ISR-DPC lock, if and only if bit 0 or bit 1 of `InterruptDpcEx`'s return is set; `USBPORTSVC_InvalidateRootHub` is that call plus a log record; and `Ev_Rh_IntrEp_Invalidate` never calls `RH_DisableIrq`**: `IsrDpc` - Vista x86 ISR-DPC acquire 0x1F08, `call usbport!MPf_InterruptDpcEx` 0x1F9B, `test al,3` 0x1FA0, `and dword ptr [ebx+3D8h],0FFFEFFFFh` 0x1FA7, release 0x2039, `test byte ptr [ebp-8],3` 0x2045, invalidate 0x204C; Windows 7 x86 acquire 0x2DB2, call 0x2E15 (the thunk returns the miniport's `eax` unchanged, 0x1850), `test al,3` 0x2E1D, `and dword ptr [ebx+3F0h],0FFFFFBFFh` 0x2E21, release 0x2E83, `test byte ptr [ebp-4],3` 0x2E8F, `call usbport!USBPORT_Ev_Rh_IntrEp_Invalidate` 0x2E96; Vista x64 acquire 0x376B6, `call r8` 0x3775B (`mov r12d,eax`), release 0x37802, `test r12b,1` / `test r12b,2` 0x37814 / 0x3781A, invalidate 0x37823; Windows 7 x64 acquire 0x2D4E8, `call r8` 0x2D561 (`mov esi,eax`), release 0x2D5D1, `test sil,1` / `test sil,2` 0x2D5DF / 0x2D5E5, invalidate 0x2D5EE. The service: Windows 7 x86 `USBPORTSVC_InvalidateRootHub` 0xF9E3, a log record, then `call usbport!USBPORT_Ev_Rh_IntrEp_Invalidate` 0xFA35; Vista x86 the call at 0x17008; Vista x64 0x299C9; Windows 7 x64 a tail `jmp` 0x1FCE3. `Ev_Rh_IntrEp_Invalidate` under RH-IntrEp takes the RH-PDO pointer lock (leaf), `USBPORT_EndpointFromHandle`, `USBPORT_Core_AllocIoRequest` (the I/O-count lock), `USBPORT_ReferenceEndpoint` (EpList) and `KeInsertQueueDpc` on the root-hub DPC only when the endpoint's state word is 2, which it sets to 3 (Windows 7 x86 0xEA5C: acquire 0xEAB3, 0xEABD, 0xEAFF, `call usbport!USBPORT_ReferenceEndpoint` 0xEB19, 0xEB2F, release 0xEB56; Vista x86 0x13D31 to 0x13DD9; Vista x64 0x2A738 to 0x2A80A; Windows 7 x64 0x20753 to 0x20827); no ISR-DPC, MP, MP-call or timer-list lock, and no call through the packet's `RH_DisableIrq` slot (`+0xEC` x86, `+0x1B0` x64), which only the `MPRH_*` sites of the row above use | static (`kd -z` whole-`.text` dumps with the public PDBs, `out\t24-4\<build>-text.txt`, 2026-09-28, by an agent's read in `out\t24-4\static-24-4-deadlock.md`; every Windows 7 x86 address in this row re-checked against the dump, the two in-lock flag clears read from the Vista x86 and Windows 7 x86 dumps directly) | design 05 section 3 and "Where the root hub may be announced"; abi section 1 "The `InterruptDpcEx` contract" and section 4 "The root hub on NT 6.x"; `src/xhci.h` `RootHubReportThroughDpc`; run-24 "Round 5 on the fixed build" |
| A.14 | **NT 6.x `usbport.sys` takes its MP lock while holding EpList, through `USBPORT_iSetGlobalEndpointStateTx` -> `MPf_PollEndpoint`, whose flag test takes the MP lock**: Windows 7 x86 0x2150 -> 0x1C64, Vista x86 0x33E1 -> 0x4071, Vista x64 0x7E2E -> 0x1E76, Windows 7 x64 0x69CC -> 0x1E3B (as the read lists them: the call in the EpList holder, then the MP acquire in the callee); also `SetEp_PAUSE` / `SetEp_REMOVE` -> `MPf_Get32BitFrameNumber`. With the row above, RH-IntrEp reaches the MP lock (RH-IntrEp -> EpList -> MP) | static (`kd -z` whole-`.text` dumps with the public PDBs, `out\t24-4\<build>-text.txt`, 2026-09-28, by an agent's read in `out\t24-4\static-24-4-deadlock.md`; not re-checked site by site) | design 05 section 3 (cycle C3) |
| A.15 | **The build of 2026-09-28 deadlocked Windows 7 SP1 x86 and Vista SP2 x86, four vCPUs each, with this driver's event DPC calling `USBPORTSVC_InvalidateRootHub` from inside `InterruptDpcEx`**: Windows 7 x86 (usbport FDO extension at `854de0e0`): CPU0 spinning on FDO+0x310 in `USBPORT_IsrDpc+0x143` -> `MPf_InterruptDpcEx+0x25` -> `xhci98!xhciInterruptDpc+0x4e` -> `XhciEventDpc+0x132` -> `XhciRootHubDeferredWork+0x2c` -> `xhciRhAnnounce+0x75` -> `USBPORTSVC_InvalidateRootHub+0x57` -> `USBPORT_Ev_Rh_IntrEp_Invalidate+0x5d`, holding FDO+0xBB8; CPU2 spinning on FDO+0xBB8 in `USBPORT_Ev_Rh_IntrEp_Dpc+0xf6` -> `USBPORT_Rh_IntrEp_Worker+0xe7` -> nt -> `USBPORT_RhIntrEp_Remove_TxqIrp+0x3c` -> `MPRH_DisableIrq+0x43`, holding FDO+0x310. Vista x86: CPU0 on FDO+0x2F8 through `USBPORT_IsrDpc+0x1a6` and the same driver frames to `USBPORT_Ev_Rh_IntrEp_Invalidate+0xaa`, CPU1 on FDO+0xD28 through `USBPORT_Ev_Rh_IntrEp_Dpc+0x152` to `MPRH_DisableIrq+0x5e`. No bugcheck on either; the other two vCPUs halted idle | debugger (registers and memory of the hung guests read through the QEMU monitor and symbolised with `kd` and the public PDBs, 2026-09-28; `out\t24-3-4\r5-win7-r5b-hang-*.txt` and `r5-vista-r5c-hang-*.txt`; the Vista FDO extension address and the two locks' roles were taken from the Windows 7 read of the same functions, not re-derived) | design 05 section 3 (cycle C1); abi section 4 "The root hub on NT 6.x"; run-24 "Round 5 on the fixed build" |
| A.16 | **No NT 5.x or 9x-era usbport was found to take the lock `IsrDpc` holds across `InterruptDpc` under a lock that `USBPORT_InvalidateRootHub` takes, and there the service calls `RH_DisableIrq` holding no usbport lock**: XP SP3 x86: `USBPORT_IsrDpc` holds FDO+0x4C8 across `InterruptDpc` (0x1B2C6 to `call [eax+60h]` 0x1B340 to 0x1B349); that lock's only users are `IsrDpc` and `USBPORT_MpInterrupts` (0x1B443 / 0x1B53B, plus its initialisation in `StartDevice`), and `MpInterrupts` is called only from the start, stop, turn-on, turn-off, restore, resume and FDO power paths; `USBPORT_InvalidateRootHub` 0x11A3E calls `RH_DisableIrq` (`[eax+0E4h]` 0x11ADC) with no usbport lock, then takes FDO+0x124 (the RH-PDO pointer, a leaf) and calls `USBPORT_InvalidateEndpoint` (FDO+0x324) after releasing it. XP x64 the same shape: the ISR-DPC lock FDO+0x600 only in `IsrDpc` (0x2589A / 0x25929, `call [rax+98h]`) and `MpInterrupts`; the service calls `[rax+1A0h]` unlocked, then FDO+0x1C0. Windows 2000 SP4: `IsrDpc` holds FDO+0x484 across `call [eax+60h]` (0x1A216 to 0x1A293); the service's `RH_DisableIrq` site (0x11C56) and the `RH_EnableIrq` site (0x115F4) take no acquisition of it in their functions; the lock has about twelve users, mostly around the interrupt enable and disable slots, and a few call helpers under it (0xDD43 -> 0xEC00 / 0xF0CC) that were not traced. SweetLow's `IsrDpc` holds FDO+0x480 (0x19A2E). NUSB's lock offset was not pinned | static (XP SP3 and XP x64 with `kd -z` and the public PDBs, high confidence; Windows 2000 SP4, SweetLow's build and NUSB with no PDB, medium; the NT 5.x code sections re-dumped for this read into a session scratchpad and not kept; by an agent's read in `out\t24-4\static-24-4-deadlock.md`, 2026-09-28, not re-checked site by site) | design 05 section 3 (why the Version 200 tier keeps its in-DPC announcement) |
| A.17 | **On every NT 5.x and 9x-era usbport the targets run, an interrupt the miniport claims queues `IsrDpc` unconditionally, and `IsrDpc` reads its interrupt-enabled flag under the same spin lock that its `EnableInterrupts` / `DisableInterrupts` wrapper holds, setting the flag before the enable and clearing it after the disable; `IsrDpc` calls `InterruptDpc` with no other gate**: addresses in this row and the next are RVAs (the address at image base `0x10000` minus `0x10000`). NUSB 3.3 = 3.6 (5.00.2195.5652, SHA-256 `EEC79B5A...9B55`): ISR 0x19690 (`push 29690h` 0xF65, `IoConnectInterrupt` 0xF6B), gates 0x1969B / 0x196A1 / 0x196A4 (flag `0x400`), 0x196A6 / 0x196AB (`0x20000`, set by a surprise-remove invalidate 0x1A38), 0x196B1 / 0x196B8 (the started bit), `call [eax+58h]` 0x196C6, on TRUE `inc` 0x196CF and `KeInsertQueueDpc(fdo+0x520, 0, 0)` 0x196D5-0x196E0, result ignored; DPC 0x19A66 (`KeInitializeDpc` 0x8A1-0x8AD), lock fdo+0x480 0x19AD6, flag 0x19B2C / 0x19B32 / 0x19B35, `call [ecx+5Ch]` 0x19B4A, release 0x19B50, the only jumps in between skipping log records; five enable sites 0x1191, 0xD8B9, 0xE76D, 0xE96B, 0xED16, each under fdo+0x480 with `or byte ptr [esi+1C1h],4` before `call [eax+84h]` (e.g. 0x116F / 0x1175, 0x118A, 0x1191, release 0x11A5), the disable clearing it after (0xD6A0 / 0xD6A6). SweetLow's 5.1.2600.2180: ISR 0x1956C (`push 2956Ch` 0x1C21, 0x1C27), gates 0x19577, 0x1957F, 0x19584, 0x1958F, `call [eax+5Ch]` 0x195A4, `inc` 0x195AD, `KeInsertQueueDpc(fdo+0x530, 0, 0)` 0x195BE; DPC 0x1994E (`KeInitializeDpc` 0x156C), lock 0x199BA (fdo+0x480), flag 0x19A10 / 0x19A16 / 0x19A19, `call [ecx+60h]` 0x19A2E, release 0x19A3A; enable sites (`[eax+88h]`) 0x1E4C, 0xDC23, 0xDE1D, 0xE1B9, 0xFCD9, e.g. lock 0x1E2A / 0x1E30, `or` 0x1E45, release 0x1E60. Windows 2000 SP4 5.00.2195.6681: ISR 0x19DD4 (0xFF9, 0xFFF), gates 0x19DDF, 0x19DE5, 0x19DEA, 0x19DF5, `call [eax+5Ch]` 0x19E0A, `inc` 0x19E13, `KeInsertQueueDpc(fdo+0x528, 0, 0)` 0x19E24; DPC 0x1A1AE (`KeInitializeDpc` 0x906), lock 0x1A21C (fdo+0x484), flag 0x1A273 / 0x1A279 / 0x1A27C, `call [eax+60h]` 0x1A290, release 0x1A299; enable sites 0x123B, 0xDF42, 0xEE3B, 0xF039, 0xF42E, e.g. 0x1219 / 0x121F, `or` 0x1234, release 0x124F. XP SP3 5.1.2600.5512: `USBPORT_InterruptService` 0x1AE54 (gates 0x1AE61 / 0x1AE69 / 0x1AE6E, 0x1AE79; call 0x1AE8E; `inc` 0x1AE97; `KeInsertQueueDpc(fdo+0x56C, 0, 0)` 0x1AEA8); `USBPORT_IsrDpc` 0x1B25C (lock fdo+0x4C8 0x1B2CC, flag 0x1B323 / 0x1B329 / 0x1B32C, `call [eax+60h]` 0x1B340, release 0x1B349); `USBPORT_MpInterrupts` 0x1B41A, the only caller of `[eax+88h]` in the image (lock 0x1B449, `or byte ptr [edi+205h],4` 0x1B458, `call` 0x1B4BF; disable `call [eax+8Ch]` 0x1B527 then `and` 0x1B52D; release 0x1B541). XP x64 SP2: `USBPORT_InterruptService` 0x25300 (0x25312, `bt eax,0Ah` 0x2531B, `bt eax,11h` 0x25321, 0x25338, `call [rax+90h]` 0x2534F, `inc` 0x2535C, `lock inc` of a pending count 0x25362, then `KeInsertQueueDpc(fdo+0x6D8, 0, 0)` 0x25375); `USBPORT_IsrDpc` 0x25810 (lock fdo+0x600 0x258A6, flag 0x25909 / 0x2591D / 0x25920, `call [rax+98h]` 0x25923, release 0x25930); `USBPORT_MpInterrupts` 0x25A50 (lock 0x25A98, `or dword ptr [rbx+2B0h],400h` 0x25AAF, `call [rax+0E8h]` 0x25B28; disable 0x25B9F then `and` 0x25BA5; release 0x25BC9). This driver's `MiniPortFlags` leave `NOT_LOCK_INT` (0x40) clear, so every build takes the lock around its enable and disable calls | static (XP SP3 and XP x64 with `kd -z` and the public PDBs in `tools\symbols`, `uf` and `ln`; NUSB, SweetLow's build and Windows 2000 SP4, which have no PDB, from whole-file `DUMPBIN /disasm /imports` (`tools\MSVC600`), with IAT slots computed from the import list and checked against the ABI record's known `KefAcquireSpinLockAtDpcLevel` call at 0x19AD6 in NUSB, the ISR found from its `IoConnectInterrupt` push and the DPC from `KeInitializeDpc` on the object the ISR queues; by an agent's read in `out\t24-3-4\static-rearm-isr-dpc.md`, 2026-09-28, not re-checked site by site) | design 05 section 4; abi section 1 (the "not operational" gate) and section 7; `src/xhci_init.c` `XhciRearmInterrupter` and `XhciUnmaskInterrupts`; run-24 "Round 5 on the fixed build" |
| A.18 | **The same holds on NT 6.x, except that `IsrDpc` skips the miniport while bit 1 of the FDO extension's "not operational" word is set, and two power paths call `USBPORT_MpInterrupts(TRUE)` while it is still set**: ISR gate and queue - Vista x86 `test byte ptr [esi+3D8h],20h` 0x1C26, `test byte ptr [esi+3D4h],1` 0x1C33, `InterlockedIncrement(fdo+0x650)` 0x1C57 and `test eax,eax / je` 0x1C5D (a rundown count set to 0 at `StartDevice` and dropped by one at `StopDevice`, so it blocks only a claim after `StopDevice`), `KeInsertQueueDpc(fdo+0xD08)` 0x1C6C; Vista x64 0x374E5, 0x374F2, `lock xadd [rbx+830h]` 0x3752E, 0x37539, 0x37547; Windows 7 x86 `test byte ptr [esi+3F0h],4` 0x17C7, `test byte ptr [esi+3ECh],1` 0x17D4, fdo+0x530 0x17F8 / 0x17FE, `KeInsertQueueDpc(fdo+0xB98)` 0x180D (the count set at 0x15479, dropped at 0x1D98E, decremented at the end of `IsrDpc` 0x2F53); Windows 7 x64 0x2D365, 0x2D372, `lock xadd [rbx+6C0h]` 0x2D3AD, 0x2D3B8, 0x2D3C6. The DPC gate under the MP lock - Vista x86 `lea ecx,[ebx+798h]` 0x1EE1, `test byte ptr [ebx+3DCh],2` 0x1EE9, `jne` to the release 0x1EF6; Vista x64 `lea rcx,[rdi+0A40h]` 0x37677, `test byte ptr [rdi+500h],2` 0x37684, release 0x3769E and `jmp` 0x376A4; Windows 7 x86 `lea ecx,[ebx+638h]` 0x2D8B, `test byte ptr [ebx+3F4h],2` 0x2D93, `jne` 0x2DA0; Windows 7 x64 `lea rcx,[rbx+868h]` 0x2D4BA, `test byte ptr [rbx+4F0h],2` 0x2D4C7, `jne` 0x2D4D5. `USBPORT_MpInterrupts` (Vista x86 0x1BD79, Vista x64 0x37980, Windows 7 x86 0x10223, Windows 7 x64 0x2D6F0) takes the ISR-DPC lock (Vista x86 0x1BDAD, Vista x64 0x379C5, Windows 7 x86 0x10259, Windows 7 x64 0x2D73E) and sets the enabled flag before the miniport call (Vista x86 0x1BDC8 -> 0x1BE36; Vista x64 0x379E0 -> 0x37A4B; Windows 7 x86 `or dword ptr [esi+3F0h],4` 0x1026E -> `call [eax+90h]` 0x102BD; Windows 7 x64 0x2D753 -> 0x2D7A8). The gate bit's setters (Windows 7 x86): `USBPORT_FdoDevicePowerState` 0x1A2A3 and `USBPORT_StopDevice` 0x1D909, each after `MpInterrupts(FALSE)`; the restart branch of `USBPORT_ResumeUsbController` 0x19E9D; `USBPORT_TurnUsbControllerOn` 0x22293, on entry under the MP lock; cleared in `USBPORT_PoRequestCompletionD0` 0x1B646, at the end of the restart branch 0x19F5D and at the end of TurnUsbControllerOn 0x2238D. Vista x86 setters 0x21C0A, 0x2AF44, 0x29CE6, 0x2B9BC, clears 0x2C920, 0x29DDD, 0x2BB1A; Vista x64 setters 0x365C, 0x235A6, 0x24B8E, 0x244F7, clears 0x2269E, 0x24C83, 0x2463F; Windows 7 x64 setters 0x3214, 0x1B64C, 0x1C641, 0x1C110, clears 0x1AA2B, 0x1C704, 0x1C20C. The two windows, `MpInterrupts(TRUE)` with the bit set - TurnUsbControllerOn: Windows 7 x86 0x22373 (set 0x22293, cleared 0x2238D after `MPRH_DisableIrq` 0x2237A), Vista x86 0x2BAFF (0x2B9BC, 0x2BB1A), Vista x64 0x24623 (0x244F7, 0x2463F), Windows 7 x64 0x1C1F0 (0x1C110, 0x1C20C); the resume restart: Windows 7 x86 0x19F08 (0x19E9D, 0x19F5D), Vista x86 0x29D55 (0x29CE6, 0x29DDD), Vista x64 0x24BFC (0x24B8E, 0x24C83), Windows 7 x64 0x1C6AF (0x1C641, 0x1C704). `USBPORT_PowerUpPhase1` (Windows 7 x86 0x1AB43) takes TurnUsbControllerOn when the controller was turned off (0x1AB9B), and the restart branch runs only when `MPf_ResumeController` fails; the only other queuers of `IsrDpc` are reached from `InvalidateController(SOFT_INTERRUPT)` (Windows 7 x86 0x1DB29 / 0x1DB31) | static (the four NT 6.x builds' whole-`.text` dumps with the public PDBs, `out\t24-4\<build>-text.txt`; high confidence for the ISR, the DPC gate and the windows, medium-high that no further setter exists (a register-form grep over the dumps); by an agent's read in `out\t24-3-4\static-rearm-isr-dpc.md`, 2026-09-28; the Windows 7 x86 DPC gate re-checked against the dump). Whether a claim lands in either window at runtime, and how often the two paths run, was not read | design 05 section 4; abi section 1 (the "not operational" gate) and section 7; `src/xhci_init.c` `XhciRearmInterrupter` and `XhciUnmaskInterrupts`; run-24 "Round 5 on the fixed build" |
| A.19 | **Windows 2000 SP4's `KeInsertQueueDpc` returns FALSE only when the DPC is already queued; Windows 98 SE's `NTKERN.VXD` exports no `KeInsertQueueDpc` by name, and its implementation was not located**: `ntkrnlmp.exe` `KeInsertQueueDpc` swings `Dpc->Lock` from 0 (`lea eax,[edi+1Ch]` at `0x42D4EA`, `lock cmpxchg [ecx],edx` at `0x42D4FB`, `jne` to the FALSE return at `0x42D504`, as the dump prints them); that the NT retire loop dequeues a DPC and clears its lock before calling the routine is the standard behaviour and was not read (`KiRetireDpcList` is not exported). `NTKERN.VXD`'s LE objects and fixups were parsed; its export name table carries `KeSetImportanceDpc` and the `Kef*SpinLock*` names but not `KeInsertQueueDpc`, `KeInitializeDpc` or `KeSetTimer`. So the Windows 98 SE and ME half of the ISR-claim premise - that an ISR preempting a running DPC can queue it again - rests on the WDM contract, not on a reading | static (Windows 2000 SP4 `ntkrnlmp.exe` read by export and disassembly; `NTKERN.VXD` parsed by script, nothing executed; by an agent's read in `out\t24-3-4\static-rearm-isr-dpc.md`, 2026-09-28) | design 05 section 4; `src/xhci_init.c` `XhciRearmInterrupter`; run-24 "Round 5 on the fixed build" |
