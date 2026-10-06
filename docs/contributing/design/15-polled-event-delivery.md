# Polled event delivery for the HCD

Design record for roadmap-hcd's planned polled event delivery (tasks
`POLL.*`; drawn up as Phase 35, unnumbered since 2026-10-06 until the owner
opens it, and the review history in section 13 keeps the old phase
numbers). DRAFT, revision 4, 2026-10-05: written at the owner's request so the driver can run
a controller that delivers no legacy interrupt - an MSI- or MSI-X-only xHCI
controller, which neither primary target can serve today - by polling the
event ring, adaptively, instead of waiting for an interrupt. Codex reviewed
revisions 1 to 3 the same day (10, 8 and 5 findings, all taken; section 13
maps each) and converged at round 4, on revision 4. Nothing is built.

## 1. What is asked, and what is not

A registry switch chooses how the bus learns that the controller has written
an event: automatically (the default), the legacy line interrupt, or a poll
of the event ring on a timer whose period adapts to the work outstanding -
long when the bus is idle, short when it is busy - with the ring also looked
at whenever the driver is entered anyway. Automatic takes the line interrupt
where the controller has one and polls where it has none.

Not in this design: MSI or MSI-X (a value of 3 or above is reserved, and no
phase schedules it), interrupters other than
Interrupter 0, and any change to the line-interrupt path. On a controller
with a line interrupt, the default must read as the Phase 34 build does.

## 2. State today (read 2026-10-05, and by Codex on revision 1)

The start:

- `hcd_ctl.c:248-271` (`hcdParseResources`): the first
  `CmResourceTypeInterrupt` is taken, and with none the start returns
  `STATUS_INSUFFICIENT_RESOURCES`. What Device Manager shows for that on each
  target has not been read.
- `hcd_ctl.c:704` and `723`: the start sets `ResourcesTypes` to
  `XhciResourcesRequired` whatever the resource list held - it manufactures
  the mask rather than describing the resources.
- `xhci_init.c` keeps two gates of its own: the start requires the interrupt
  bit in that mask (near line 4551), and refuses a controller whose PCI
  `Interrupt Pin` is 0 (near line 4626, "controller is MSI/MSI-X only").
- `hcd_ctl.c:737-748`: the ISR is connected before the sequence. The start's
  No Op self-test is submitted asynchronously (`xhci_cmd.c:1106`,
  `xhci_init.c:5091`), so nothing in the start waits on an interrupt before
  a poller could run.

The drain and its accounting:

- `hcd_ctl.c:281-324`: `hcdIsr` claims on `USBSTS.EINT` through `XhciIsr`,
  counts `DpcsInFlight` **before** queueing `IsrDpc`, and `hcdIsrDpc` drains
  through `XhciEventDpc(&hc->Hc, TRUE)`, waking the thread on a port event.
- `xhci_evt.c:903`: the drain re-arms the interrupter
  (`XhciRearmInterrupter`) only when asked to and only while
  `XHCI_EXT_FLAG_INTERRUPTS` is set; that flag is set at `xhci_evt.c:1048`
  and cleared at `1154`, and resume and recovery restore interrupts through
  it (`xhci_init.c:3806`, `3911`, `4162`).
- `xhci_evt.c:774`: `XhciEventDpc` counts every drain in `DpcCount`,
  whatever called it, and the driver's tear detector sums it
  (`hcd_door.c:836`).
- `hcd_ctl.c:612-642` (`hcdRelease`): the interrupt is disconnected first,
  which proves no new producer of `IsrDpc` can appear, then `DpcClosed` is
  set and `DpcsInFlight` waited to zero, then the timers drained.
- Recovery and D3 close nothing of this: `hcdRecover` runs the in-place
  recovery at DISPATCH_LEVEL (`hcd_ctl.c:412`), the power paths suspend and
  resume the controller directly (`hcd_power.c:64`, `110`, `117`, `302`),
  and the core keeps the drain out by its own state (`ControllerFailed`,
  `INITIALIZED`) under the controller lock.
- `xhci_ring.c:1373-1376`: `XhciEventRingDequeue` increments `Dequeue`
  before it wraps it, and `XhciEventRingPending` (`:1344`) indexes with it,
  so a reader without the controller lock can see an out-of-range index.

Timers:

- `hcd_svc.c:163-212`: the frame timer is a one-shot `KeSetTimer` re-armed
  by its own DPC under `TimerLock` until `TimersClosed`. `KeSetTimerEx` has
  no Windows 98 export (`docs/usb-xhci-info/win98-wdm.md`, the export table),
  so there is no periodic timer.
- `hcd_svc.c:19-29`: `HcdRelativeMs` holds any period to 400,000 ms, since
  ms x 10,000 must fit a ULONG under the no-64-bit-arithmetic rule.
- The import allowlist admits `KeCancelTimer` and `KeQuerySystemTime`, and
  no timer-resolution call (`ExSetTimerResolution`).

The tools and the documents:

- `XHCIQUAL` disqualifies a pin-0 controller in `quick_classify`
  (`xhciqual/mmiodiag.c:342`) and says why in `quick_reason` (`:361`); the
  MMIO classification, `quick_classify_mmio`, deliberately does not ask.
  Its C4 test fails for several reasons, only one of them "no interrupt
  arrived" (section 10).
- `implementation-invariants.md`, "Interrupt Delivery", requires a pin-0
  controller to be refused; the release notes' "Controller" row and the
  acceptance test's step 3 say it cannot be driven.

## 3. The three values

Read from the controller's driver key at each start, as `REG_DWORD`, and
written by both INFs on every install path with `FLG_ADDREG_NOCLOBBER`
(`0x00010003`) under task 34.1's rule: each is where a user looks for it, and
an update keeps a value the user set. Text-mode Setup's `txtsetup.oem` writes
none, so F6 Setup runs at the defaults - automatic, which polls a controller
with no interrupt.

| Value | Default | Meaning | Out of range |
|---|---|---|---|
| `XhciInterruptMode` | 0 | 0 automatic; 1 the line interrupt; 2 polling; 3 and above reserved | 3 and above run as 0, the value read recorded (section 8) |
| `XhciPollIdleMs` | 50 | The poll period when nothing is outstanding (section 6) | 0 runs as 50; above 5000 held to 5000 |
| `XhciPollActiveMs` | 1 | The poll period while work is outstanding | 0 runs as 1; above the idle period held to it |

Owner's decisions of 2026-10-05 (section 12): the values, their names and
defaults, the numbering, that neither period may be 0, and the 5000 ms
bound.

### 3.1 The defaults

- **0, automatic.** On every machine with a line interrupt it is exactly
  today's driver; on one without, it polls instead of failing the start. The
  numbering lets a future MSI path (a reserved value) become what automatic
  prefers without asking users to change anything.
- **50 ms idle.** A connect is debounced for 100 ms by the USB 2.0
  specification before a driver acts on it, so a 50 ms look at an idle ring
  adds no delay a user can see to a hot-plug, and costs about 20 timer DPCs
  a second, each a look at one TRB when nothing has happened.
- **1 ms active.** A request for "as often as the kernel will run the
  timer". Expected to be the clock tick in practice, not 1 ms (section 7).

### 3.2 The bound

5000 ms for the idle period, with the active period held to the idle one
(owner, 2026-10-05, choosing it over 1000 ms and over no bound). Without a
bound, a large idle value makes the controller look dead rather than slow:
a device plugged in, an external hub's status change, or a remote wake waits
a whole period before anything sees it, and `HcdRelativeMs` would cap it at
400 s anyway, silently. 5000 ms lets a user trade hot-plug latency for fewer
wake-ups and still keeps a plug-in within five seconds. It is the same "held
to" form `XhciFirstEnumWaitMs` uses (held to 30 s).

## 4. Choosing the delivery at start

The start reads the values (section 3) and the resource list, then settles
the **delivery** - line or poll - and its **reason**, once:

The pin is read once, and its read status is kept apart from its value:
today a failed read with an interrupt resource present is accepted, the pin
recorded as `0xFFFFFFFF` and the resource trusted (`xhci_init.c:4605-4617`),
and that stays.

| `XhciInterruptMode` | Interrupt resource | Pin | Delivery | Reason |
|---|---|---|---|---|
| 0, or 3 and above | present | not 0 | line | automatic |
| 0, or 3 and above | present | unreadable | line, as today | automatic, pin unreadable |
| 0, or 3 and above | present | 0 | poll | automatic: interrupt pin 0 |
| 0, or 3 and above | absent | any | poll | automatic: no interrupt resource |
| 1 | present | not 0, or unreadable | line, as today | forced (pin unreadable, if so) |
| 1 | present and pin 0, or absent | | none: the start fails as today | forced, no interrupt |
| 2 | any | any | poll | forced |

A value of 3 and above also records "reserved value N, run as automatic".

The delivery is held in the controller (`hc`) and copied into the core's
extension after the start zeroes it, so the in-place recovery and every
resume keep it; only a new start settles it again. For a poll delivery:

- **`ResourcesTypes` tells the truth.** Both sites (`hcd_ctl.c:704`, `723`)
  set the interrupt bit only when an interrupt resource was taken, and the
  core's two gates in `xhci_init.c` - the interrupt bit and the pin-0 refusal
  - apply to a line delivery only. Mode 1 keeps both, so "forced" fails
  exactly as today.
- **The ISR is not connected**, even when a resource exists (mode 2 on a
  machine with a pin).
- **No interrupt is ever enabled.** `XHCI_EXT_FLAG_INTERRUPTS` is never set
  for a poll delivery, so neither the drain (`xhci_evt.c:903`) nor resume
  nor recovery (`xhci_init.c:3806`, `3911`, `4162`) re-arms `IMAN.IE` or
  sets `USBCMD.INTE`, and every poll drain passes `FALSE` for the re-arm as
  well. A controller with a pin therefore never asserts a level-triggered,
  possibly shared, line that no ISR of this driver claims.

`XhciImodInterval250ns` is still programmed as read; it has no effect
without an interrupt.

### 4.1 What the hardware needs (Codex, revision 1)

With `USBCMD.INTE` and `IMAN.IE` clear, cycle-bit dequeue and the `ERDP`
write remain valid: `IMAN.IP` and `USBSTS.EINT` may stay pending without
stopping the controller from writing events, and `ERDP.EHB` governs
interrupt generation, not consumption of the ring. If the poll drain
acknowledges them, it keeps `EINT` before `IP`, as the ISR does, and it keeps
the drain's final `EHB`-clearing write. The "no interrupt, no DPC" chain in
the comment near `xhci_evt.c:990` describes interrupt-driven progress; an
independently scheduled poll does not depend on it. Each statement is
checked against `docs/usb-xhci-info/xhci-data-structures.md` in task POLL.0
before code.

## 5. The poller's lifecycle

### 5.1 The objects

- `PollTimer` and `PollTimerDpc`: a `KTIMER` and the `KDPC` given only to
  `KeSetTimer`. The timer's DPC drains, decides the next deadline and
  re-arms.
- `IsrDpc`, which no ISR queues under a poll delivery, is the DPC the
  opportunistic checks queue explicitly, with `DpcsInFlight` counted before
  the queue as `hcdIsr` does. One `KDPC` is never handed to both the timer
  and `KeInsertQueueDpc` (Microsoft's `CustomTimerDpc` guidance).
- `PollLock`, a spin lock, guarding the poller's state (section 5.2), its
  deadline `PollDue`, and `PollOwed`, the earliest deadline asked for while
  the timer's DPC owned the poller.
- `PollService`, in the core's extension and written only under the
  controller lock: 1 while the controller is running and its event ring may
  be drained by a poll, 0 otherwise (section 5.3).

**Lock order: the controller lock, then `PollLock`; never the reverse.**
Neither is held across a drain, and `TimerLock` is never taken with
`PollLock` held.

### 5.2 One owner at a time

Exactly one party owns the timer at any moment, recorded in `PollState`
under `PollLock`:

| State | Owner | Meaning |
|---|---|---|
| PARKED | nobody | no arm, no DPC; the controller is not in service, or the poller has not started |
| ARMED | the timer | `KeSetTimer` has been called and the DPC has not yet taken ownership |
| FIRING | the DPC | the DPC has taken ownership and is draining or deciding |
| CLOSING | the DPC | as FIRING, but close has been asked; the DPC ends it |
| CLOSED | nobody | closed; nothing touches the poller until a start reopens it |

- **Initial state, and reopening.** The controller object outlives a stop:
  its synchronisation objects are initialised once
  (`HcdControllerInitObjects`, `hcd_ctl.c:330`), every start reopens the
  timer service and the DPC admission (`hcd_ctl.c:734-735`), and the start's
  zeroing of the core's extension (`hcd_ctl.c:667`) does not reach fields
  held in `hc`. So `PollTimer`, `PollTimerDpc` and `PollLock` are
  initialised there with the other objects, and the state starts CLOSED. A
  start under a poll delivery **opens** the poller beside
  `HcdTimersOpen`: under `PollLock`, CLOSED becomes PARKED and `PollDue` and
  `PollOwed` are marked invalid; it is the only way out of CLOSED, and it is
  taken only after the previous release has seen CLOSED (the close below),
  so a disable and enable, or a start that failed and is retried,
  begins from a drained poller. The kick at the end of a successful start
  (section 5.3) then arms it. A start under a line delivery leaves it
  CLOSED. A failed start runs the same release as a stop, which closes it
  from whatever state it reached.
- **The DPC takes ownership** as it starts: under `PollLock`, ARMED becomes
  FIRING (CLOSING stays CLOSING), and `PollOwed` is cleared.
- **The DPC gives it up** as it ends, under `PollLock` (section 5.3 for the
  park): CLOSING becomes CLOSED; otherwise it arms at the earlier of its own
  decision and `PollOwed` and the state becomes ARMED.
- **Only the owner, or a party that has taken the arm back, calls
  `KeSetTimer`.** A promotion (section 6.2) in ARMED takes the arm back with
  `KeCancelTimer`: TRUE means the timer was still queued and no DPC will
  run, so the promotion may set the new deadline; FALSE means the timer has
  expired and its DPC is queued or running and is about to take ownership,
  so the promotion records its deadline in `PollOwed` instead. In FIRING it
  records in `PollOwed`. No external `KeSetTimer` ever runs while a DPC owns
  the poller, so there is never a second callback.
- **Close** (release, at PASSIVE_LEVEL, where the interrupt disconnect is
  today), under `PollLock`: PARKED becomes CLOSED; ARMED with `KeCancelTimer`
  TRUE becomes CLOSED; ARMED with FALSE, and FIRING, become CLOSING, and the
  DPC finishes the close. Release then waits for CLOSED, acquires and
  releases `PollLock` once more so the DPC's last release of it has
  happened, and only then goes on to `DpcClosed`, the `DpcsInFlight` wait
  and the timer and DMA drains. What the DPC does after that last release is
  return - the same residual the `DpcsInFlight` decrement already accepts.
- **A peek's queueing of `IsrDpc`** increments `DpcsInFlight` under
  `PollLock` only while the state is neither CLOSING nor CLOSED, before
  `KeInsertQueueDpc`, and undoes the count if the DPC was already queued.

`KeCancelTimer` is taken on its documented contract only - it removes a
timer still queued, and it does not stop a DPC already queued or running.
No stronger behaviour of Windows 98's NTKERN is assumed; there is no
evidence here for one.

### 5.3 In service, parked and kicked

`PollService` is the poller's admission to the hardware, separate from the
core's own flags. `INITIALIZED` is published before the controller is run
(`xhci_init.c:4999`, then `xhciRunController` at `:5004`), and `RUNNING`
before the run bit is written (`:1362-1363`), so neither proves the
controller runs.

- **Set** under the controller lock after the controller is running: at the
  end of a successful start, of the restoring resume (near
  `xhci_init.c:3805`), of the reinitialising resume (near `:3911`) and of a
  successful in-place recovery (near `:4162`) - outside the
  `XHCI_EXT_FLAG_INTERRUPTS` branches at those sites, since that flag is
  clear under a poll delivery.
- **Cleared** under the controller lock before anything quiesces, resets,
  suspends or stops the controller.
- **Checked inside the drain's own locked admission**, under a poll delivery
  only: `XhciEventDpc` refuses a poll drain while it is 0, under the same
  controller-lock hold in which it would drain, not in a wrapper that drops
  the lock first. The peek checks it the same way (section 7.1).

**Park and kick are serialised by the controller lock.**

- The timer DPC, finding `PollService` 0 under the controller lock, takes
  `PollLock` inside that hold and parks: FIRING becomes PARKED. It drains
  nothing.
- Each `PollService` set site, in the same controller-lock hold that sets
  it, takes `PollLock` and **kicks**: PARKED becomes ARMED at the active
  period. In ARMED or FIRING the kick does nothing, since the owner will see
  `PollService` 1.

Both decisions are taken inside one hold of the controller lock, so either
the DPC parks first and the kick finds PARKED and arms, or the kick sets
`PollService` first and the DPC does not park. A restart cannot be lost.
`KeSetTimer` is callable at DISPATCH_LEVEL, so the recovery path can kick.
The timer service's own close (`TimersClosed`) is not used for any of
this: the controller's initialisation needs the timer service during
recovery.

## 6. The adaptive period (pure core, `xhci_poll.c`)

### 6.1 The states

A pure function, host-tested in `test_poll`, from the bus's state to the
next period:

| State | When | Period |
|---|---|---|
| ACTIVE | a command on the command ring; any control, bulk or isochronous TD outstanding; a port in reset or enumeration; an event drained within the last idle period; or the last drain stopped at its bound with events left | the active period |
| PERIODIC | nothing above, but interrupt TDs pending, IN or OUT | the shortest pending interrupt endpoint's service interval, rounded down to whole ms, held between the active and idle periods |
| IDLE | nothing pending | the idle period |

PERIODIC is what makes the poll adaptive rather than two-speed. A HID device
and every external hub keep an interrupt-IN transfer pending all the time,
so with ACTIVE defined as "anything pending" a machine with a mouse would
never leave the active period. A mouse at an 8 ms interval is looked at
about every 8 ms; a hub's status endpoint at the idle period, since its
interval is longer.

The bounded-drain clause replaces what an interrupt did for a drain that
stopped at its bound: the interrupt would have fired again; here the next
poll comes at the active period.

### 6.2 Promotion

A submission whose need would bring the next poll earlier brings it
earlier, and never later. Deadlines, not periods, are compared.

- A control, bulk or isochronous TD, or a command, needs the active period;
  an interrupt TD needs its endpoint's interval. Its deadline is now plus
  the need, in the low word of `KeQuerySystemTime`, compared by signed
  difference (no 64-bit arithmetic; a deadline the poller makes is within
  5 s, far inside the word's lap of about 429 s, and one a clock step has
  displaced is caught where it is turned into an arm, below).
- Under `PollLock` (taken inside the submission's controller-lock hold,
  which the lock order allows): in ARMED, if the new deadline is earlier
  than `PollDue`, take the arm back (section 5.2) and set the new deadline;
  if not earlier, nothing - so a stream of submissions never pushes the
  deadline later, and a short need near an armed deadline that is sooner
  leaves it alone. In FIRING, `PollOwed` becomes the earlier of itself and
  the new deadline. In PARKED, CLOSING or CLOSED, nothing: a parked poller
  is restarted by its kick.
- The DPC's own re-arm takes the earlier of its decision's deadline and
  `PollOwed`, so a promotion made while it ran is never postponed by a stale
  decision.

PERIODIC to ACTIVE is a promotion like IDLE to ACTIVE: a bulk transfer on a
machine with a mouse attached gets an earlier poll, not the mouse's
interval.

**Turning a deadline into an arm.** `KeSetTimer` is given a relative time,
which a change of the system time does not move; only the stored deadlines
are wall-clock, so every conversion of one into an arm is checked, in one
pure function in `xhci_poll.c`:

- the remaining time is the deadline less now, by signed difference of the
  low words;
- remaining time at or below zero - an expired deadline, including a
  `PollOwed` that fell due while the DPC ran - is served at once, as an arm
  of the active period;
- remaining time above the idle period, or a difference whose magnitude is
  beyond any deadline the poller can make (more than twice the idle bound),
  is a displaced deadline - a clock step - and is replaced by an arm of the
  active period, counted;
- otherwise the arm is the remaining time, held between the active and idle
  periods.

So every arm the poller makes, whatever the clock did, is at least the
active period and at most the idle period away, and the bound on how long
an event waits for a poll is the idle period. A clock step can make a
comparison in a promotion wrong: it then either skips the promotion, which
leaves the armed timer at most the idle period away, or arms early, which
costs one poll. Neither is a defect.

## 7. What the periods mean on these kernels

`HcdRelativeMs` gives `KeSetTimer` a relative due time. What the kernel does
with it is not a bound: a short relative timer can expire early, even
immediately, or late, and the DPC then runs when the processor reaches it.
On an NT uniprocessor HAL the clock tick is commonly about 10 ms and on an
ACPI or multiprocessor HAL about 15.6 ms, and other software can change it;
Windows 98's has not been measured in this repository. So an active period
of 1 ms is expected to run at roughly the tick, and these are expectations
the readings measure, not guarantees the design relies on.

What is recorded (section 8) is the interval between successive poll runs,
from the low word of `KeQuerySystemTime` (as the first-answer settle clock
does, no 64-bit arithmetic), as an observation quantised to the system
clock: a negative interval or one longer than twice the idle bound (a clock
adjustment) is discarded and counted, not recorded.

What follows from it:

- **Latency.** A transfer the controller has completed waits for the next
  poll or peek before its IRP completes. Interactive HID is expected to be
  unaffected at a tick of 10 to 16 ms. A storage request at queue depth 1
  completes at most about once per poll per stage, so polled storage
  throughput is expected to be bounded by the tick, and the opportunistic
  checks (section 7.1) are what recover part of it. Isochronous audio must
  be kept fed across a poll; whether each target's `usbaudio.sys` queues far
  enough ahead is read, not assumed.
- **A finer timer** (`ExSetTimerResolution` on NT) is not part of this
  design: it has no allowlist row, no Windows 98 export evidence, and changes
  the tick for the whole system. If the readings show polled throughput
  unusable, it is the next question, under the import gate's evidence rule.

### 7.1 Opportunistic checks

Whenever the driver is entered anyway, it looks at the event ring for an
event not yet drained.

- **Under the controller lock, never without it.** The peek is
  `XhciEventRingPending` taken under the controller lock and only while
  `PollService` is 1 (section 5.3), so it reads a consistent `Dequeue` and
  cycle state of a ring that exists and a controller that runs. A peek without the lock was rejected: the
  dequeue index can be seen unwrapped (`xhci_ring.c:1373`), and the ring's
  lifetime across a reset is not otherwise guaranteed.
- **Where**: on the URB submission path while the controller lock is still
  held for the doorbell (`hcd_io.c:1208`); at the end of each drain, for
  transfers a completion routine resubmitted; on each controller-thread pass
  and health poll; and in the frame timer's DPC.
- **On a hit**: queue `IsrDpc` under the admission rule (section 5.2). Never
  drain inline: a drain completes IRPs, a class driver's completion routine
  resubmits, the submission would peek again, and the recursion would run
  down a kernel stack - Windows 98's is small - with no bound.
- **On a submission**: promote (section 6.2).

The checks run under a poll delivery only, so a line delivery runs exactly
today's code.

## 8. Counters and log

In the counter block: the value of `XhciInterruptMode` as read, the
delivery in effect and its reason (section 4), the pin and its read status,
the two periods as read and in effect, the period last armed and the state
that chose it, poll runs,
poll runs that found an event, peeks and peek hits, promotions, parks and
kicks, and the shortest and longest interval observed between two poll runs
with the count of discarded intervals (section 7). One bounded log line at
start with the delivery and its reason; nothing per poll.

**The delivery in effect must be capturable** (owner, 2026-10-05): a user
on a machine that polls automatically and one forced to poll must be told
apart, by `XHCISNAP` and by the DebugView log. Three routes, each with what
it does and does not guarantee:

- **The counter block, through `XHCISNAP`** - the reliable route. The
  delivery, its reason, the pin's read status and the values in effect are
  persistent fields, set once at start; `XHCISNAP` reads them whenever the
  log channel is on (`XhciLogVerbosity` 1 and above; `src/xhci_log.h`, the
  ladder).
- **The counter dump at each stop, through DebugView.** The fields are
  added to `hcdLogCountersLocked` (`src/hcd_log.c`). `HcdLogFlush` is gated
  on `XhciLogDebugView` and publishes past the verbosity's recording
  suppression (`hcd_log.c:142-162`, `xhci_log.c:167`), so with
  `XhciLogDebugView` set the dump carries them at every verbosity, 0
  included.
- **The start note in the log ring.** Recorded from verbosity 2; the
  DebugView sink, when set, emits it from the controller thread at
  PASSIVE_LEVEL with the rest of the ring, adding no `DbgPrint` site. It is
  available to `XHCISNAP`'s log read only until the DebugView sink drains it
  (`XhciLogDrain`, `hcd_log.c:172`) or the bounded ring overwrites it, so it
  is a convenience, not the capture.

With the log channel off and `XhciLogDebugView` clear, nothing is captured,
as for every other diagnostic.

The tear detector (`hcd_door.c:836`) needs no change: `DpcCount` counts
every drain, polled or not (`xhci_evt.c:774`).

## 9. `XHCISNAP` (task POLL.4)

- The companion reports the three values as it does `XhciImodInterval250ns`
  (`write_companion_imod`): status, value as read, value in effect, and the
  range note, with the ranges duplicated in the tool for the reason the
  `SNAP_IMOD_*` constants are.
- It states the delivery in effect and its reason, and under a poll delivery
  says the interrupt moderation value has no effect.
- It reports the poll counters of section 8.
- The coherence paragraph's wording says "interrupt DPCs"; it becomes
  "event drains, interrupt or polled", since `DpcCount` counts both.
- The header grows under the snapshot's existing schema rule, and the tool
  still reads a `2.1.0.0` or earlier header.

## 10. `XHCIQUAL` (task POLL.5)

A controller is no longer disqualified for `Interrupt Pin = 0`:
`quick_classify` and `quick_reason` (`mmiodiag.c:342`, `361`) report it as
"needs polling: no legacy interrupt (the driver's automatic mode polls it,
or set `XhciInterruptMode` to 2)", and the other disqualifiers - BAR0 above
4 GB, unassigned, I/O space - keep their verdicts.

C4 is not converted wholesale. Its fail covers several causes (Codex,
revision 1): the DPMI interrupt hook not installed (`bringup.c:580`), no
`IP` and no matching event (`:605-623`), and an ISR that fired with no
completion behind it (`:668`). Only the cause "the event reached the ring
and no interrupt arrived" reads "needs polling"; the tool's own failures
stay tool failures, and a controller that does not complete the command
even polled stays a failure. The polled tests then run only after the
tool's interrupt hook is removed. Downstream: `main.c:778` gates C6 and C8
on C4 or `--poll-only`, and `report.c:508-552` makes a poll-only run
provisional; both learn the "needs polling" outcome, so such a controller
runs C6 and C8 and gets a definite verdict, with the exit code to match.
Vectors at the classification level (`test_mmiodiag`) and for each C4
outcome through the verdict, which `test_mmiodiag` alone cannot reach.

## 11. Readings (task POLL-V)

Two kinds of evidence, both required, kept apart. The functional and
regression legs - every target in mode 2, line against poll, the defaults on
a pin, the races - run on the `release` package. The fault-injection legs -
no interrupt, the pin's read failing, a forced recovery where it needs a
switch - run on the `qemu` flavour, because the switches exist only there,
and that flavour is never published.

- **Every target, mode 2** (QEMU's `qemu-xhci` has a pin, so polling is
  forced): 98 SE and 2000 (the SMP guest, Driver Verifier) first, then ME,
  XP, Vista and 7, x86 and x64 - a HID mouse, Bulk-Only and UAS storage, a
  composite audio device playing, devices behind QEMU's hub, hot-plug,
  disable and enable, remove and rescan.
- **No interrupt, deterministically.** No guest presents `Interrupt Pin = 0`
  that has been found, so the `qemu` flavour gets a test switch that drops
  the interrupt resource and reports pin 0 at the start, and mode 0 is read
  polling with the reason "automatic", and mode 1 failing as today.
- **Recovery and D3 under a poll delivery**: a forced in-place recovery
  (the `qemu` flavour's existing way to provoke one, or a new switch) with
  devices working after it, and the controller's D3 and back where a guest
  offers one without system standby: the park and kick read in the
  counters. Where no guest does, the recovery alone reads park and kick, and
  a Device Manager disable and enable reads the close (section 5.2). System
  standby and hibernate under a poll delivery are read in the planned
  selective suspend work, with task SUSP.1.
- **The races, on the Windows 2000 SMP guest under Driver Verifier**: a
  submission storm against the timer's re-arm (promotion never lost, never
  pushed later), close during a peek storm and a timer expiry (no DPC after
  release), and a drain stopped at its bound continued by the next poll.
- **The ownership vectors in `test_poll`**, driving section 5.2's state
  machine and section 6.2's deadlines with the kernel calls stubbed: expiry,
  then a promotion, then close; a running DPC, then a promotion, then close;
  `KeCancelTimer` returning TRUE and FALSE at each state; park against kick
  in both orders; a promotion just before expiry and during a long drain;
  the deadline comparison across the word's wrap; the conversion with the
  clock stepped forward and back, with a `PollOwed` already expired, and
  across the wrap; and stop and start, and a failed start retried, reopening
  from CLOSED.
- **The pin's read failing**, by a `qemu`-flavour switch beside the
  no-interrupt one: with a resource present, modes 0 and 1 run the line
  interrupt as today, with the reason "pin unreadable".
- **The period observed** per target, from the counters.
- **Line against poll** on the same guest: a storage copy rate, audio
  continuity, and the guest's idle CPU load.
- **The default on a pin**: mode 0 reading as the Phase 34 build
  (the matrix unchanged), and mode 1 the same.

Bench (POLL-E, owner): mode 2 on the E460 and the P14s Gen 1 under Windows 98
SE, and an MSI-only controller if one is at hand. The synthetic no-interrupt
leg above does not depend on one.

## 12. Decisions

Taken by the owner on 2026-10-05, each from options with a recommendation:

1. The values and defaults: `XhciPollIdleMs` 50, `XhciPollActiveMs` 1,
   neither 0.
2. The upper bound (section 3.2): 5000 ms for the idle period, the active
   period held to it (1000 ms was recommended; no bound was the other
   option).
3. A controller with no interrupt (section 4): polled rather than refused,
   with the delivery and its reason captured by `XHCISNAP` and the DebugView
   log (section 8).
4. The numbering (section 3): 0 automatic, 1 the line interrupt, 2 polling,
   3 and above reserved - asked by the owner after decision 3, and taken as
   recommended, replacing revision 1's "0 line, 1 polling, 2 reserved for
   MSI".
5. The value names: `XhciInterruptMode`, `XhciPollIdleMs` and
   `XhciPollActiveMs`, as recommended.

## 13. Codex's reviews

### 13.1 Revision 1

| # | Finding | Where answered |
|---|---|---|
| 1 | The core's own interrupt gates and the manufactured resource mask | Sections 2 and 4 |
| 2 | Timer delivery cannot reuse the ISR's admission; one DPC for timer and queue | Sections 5.1 and 5.2 |
| 3 | No disconnect points in recovery and D3; the drain re-arms IE | Sections 4 and 5.3 |
| 4 | The lockless peek | Section 7.1: under the controller lock |
| 5 | Promotion from PERIODIC, the re-arm race, interrupt-OUT | Sections 6.1 and 6.2 |
| 6 | Timer behaviour stated as guarantees | Section 7 |
| 7 | The tear detector already counts polled drains | Sections 2, 8 and 9 |
| 8 | C4 fails for several causes; main and report paths | Section 10 |
| 9 | Legs do not exercise the lifecycle | Section 11 |
| 10 | Wrong `XHCIQUAL` function; an unread Code 10; 5.1's review owner; the checkpoint's switch | Sections 2, 4.1 and 10; roadmap POLL.0 and the checkpoint |

### 13.2 Revision 2

| # | Finding | Where answered |
|---|---|---|
| 1 | `KeCancelTimer` TRUE does not prove retirement when a promotion re-arms under a running DPC | Section 5.2: one owner at a time, `PollOwed`, close through CLOSING |
| 2 | Park and kick can lose the restart | Section 5.3: both inside one controller-lock hold; the kick sites named |
| 3 | `INITIALIZED` and `RUNNING` are published before the controller runs | Section 5.3: `PollService`, checked in the drain's own locked admission |
| 4 | Comparing periods can postpone a deadline | Section 6.2: deadlines compared; `PollOwed` preserved |
| 5 | An unreadable pin | Section 4: its own rows, today's behaviour kept |
| 6 | The capture routes overstated | Section 8: counter block, stop dump, start note, each with its limits |
| 7 | The `qemu`-flavour legs cannot run on the release package | Section 11; the roadmap's checkpoint |
| 8 | The roadmap called the record unreviewed | The roadmap's Phase 35 status |

### 13.3 Revision 3

| # | Finding | Where answered |
|---|---|---|
| 1 | CLOSED was terminal; a stop and start never reopened the poller | Section 5.2: initial state CLOSED, opened at each poll-delivery start |
| 2 | A clock step breaks the deadline bound | Section 6.2: every deadline checked where it becomes an arm, every arm between the active and idle periods |
| 3 | Phase 35's legs still waited on Phase 36's standby | Roadmap POLL-V, as section 11 already had it |
| 4 | Phase 36's checkpoint asked a `qemu`-only leg of the release package | Roadmap Phase 36 checkpoint |
| 5 | Record 14's `34-V`, its baseline and run sheet; the roadmap's docs task citing a list this record had lost | Record 14; section 14 below; roadmap SUSP.3 |

### 13.4 Revision 4

Converged (Codex, round 4, 2026-10-05): one wording finding, the roadmap's
Phase 35 status still naming revision 3, taken. No finding in the reopen,
the deadline conversion, the phase split or the cross-references.

## 14. What changes in the documents

Task SUSP.3, the docs of both planned pieces: `implementation-invariants.md`,
"Interrupt Delivery" (a pin-0 controller polled, not refused);
`architecture.md`'s statement of line-based delivery; the release notes'
"Controller" row and a row for the switch; the acceptance test's step 3 and
its stop on the pin; `xhciqual/hardware-testing.md` and record 01's C4
note; record 13 where it describes the ISR; `source-files.md` for
`xhci_poll.c`; the locking record for `PollLock`, `PollService` and the
lock order; `xhcisnap/README.md` for the new companion lines; and the
phase's run record, named when it is opened.
