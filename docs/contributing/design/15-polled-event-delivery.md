# Polled event delivery for the HCD

Design record for roadmap-hcd Phase 34, stage C (tasks 34c.*). DRAFT,
revision 2, 2026-10-05: written at the owner's request so the driver can run
a controller that delivers no legacy interrupt - an MSI- or MSI-X-only xHCI
controller, which neither primary target can serve today - by polling the
event ring, adaptively, instead of waiting for an interrupt. Codex reviewed
revision 1 the same day (10 findings, all taken; section 13 maps each).
Nothing is built.

## 1. What is asked, and what is not

A registry switch chooses how the bus learns that the controller has written
an event: automatically (the default), the legacy line interrupt, or a poll
of the event ring on a timer whose period adapts to the work outstanding -
long when the bus is idle, short when it is busy - with the ring also looked
at whenever the driver is entered anyway. Automatic takes the line interrupt
where the controller has one and polls where it has none.

Not in this design: MSI or MSI-X (a value of 3 or above is reserved, and
"What is not on this roadmap" carries it), interrupters other than
Interrupter 0, and any change to the line-interrupt path. On a controller
with a line interrupt, the default must read as `2.2.0.0` without stage C.

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

| `XhciInterruptMode` | Interrupt resource and pin | Delivery | Reason |
|---|---|---|---|
| 0, or 3 and above | resource present, pin not 0 | line | automatic |
| 0, or 3 and above | no resource, or pin 0 | poll | automatic: no interrupt resource / interrupt pin 0 |
| 1 | resource present, pin not 0 | line | forced |
| 1 | no resource, or pin 0 | none: the start fails as today | forced, no interrupt |
| 2 | either | poll | forced |

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
checked against `docs/usb-xhci-info/xhci-data-structures.md` in task 34c.0
before code.

## 5. The poller's lifecycle

### 5.1 The objects

- `PollTimer` and `PollTimerDpc`: a `KTIMER` and the `KDPC` given only to
  `KeSetTimer`. The timer's DPC drains, decides the next period and re-arms.
- `IsrDpc`, which no ISR queues under a poll delivery, is the DPC the
  opportunistic checks queue explicitly, with `DpcsInFlight` counted before
  the queue as `hcdIsr` does. One `KDPC` is never handed to both the timer
  and `KeInsertQueueDpc` (Microsoft's `CustomTimerDpc` guidance).
- `PollLock`, a spin lock, guarding `PollOpen` (admission),
  `PollArmed` (the timer is set or its DPC is queued or running),
  `PollPeriod` (the period it was set for) and `PollPromote` (a count bumped
  by each promotion, section 6.2).

### 5.2 Admission and retirement

- **Arming** - by the start, a re-arm at the end of the timer DPC, a
  promotion, a kick after resume or recovery - happens only under `PollLock`
  while `PollOpen`, and sets `PollArmed`.
- **The timer DPC retires itself.** It clears `PollArmed` under `PollLock`
  as it ends, unless it re-armed.
- **A peek's queueing of `IsrDpc`** increments `DpcsInFlight` under
  `PollLock` while `PollOpen`, before `KeInsertQueueDpc`, and undoes the
  count if the DPC was already queued.
- **Close** (release, at PASSIVE_LEVEL, where the interrupt disconnect is
  today): under `PollLock`, `PollOpen` = 0, then `KeCancelTimer`; a TRUE
  return means the DPC will not run, and `PollArmed` is cleared there. A
  FALSE return means the timer expired and its DPC is queued or running and
  will clear `PollArmed` itself, finding `PollOpen` 0 and not re-arming.
  Then `DpcClosed` is set and, as today, `DpcsInFlight` waited to zero, and
  `PollArmed` waited to zero beside it, before the timers and DMA are
  drained. After `PollOpen` = 0 there is no producer left: no ISR, no timer
  re-arm, no peek.

### 5.3 Recovery and D3: park and kick

Recovery and D3 do not close admission; nothing blocking can run in them,
and the recovery runs at DISPATCH_LEVEL. Instead:

- **The timer DPC parks.** Under the controller lock it asks the core
  whether the controller is running (`INITIALIZED`, not `ControllerFailed`,
  not suspended). If not, it drains nothing and does not re-arm: `PollArmed`
  is cleared and the poller is parked. A drain the core keeps out is not
  attempted.
- **A peek** is likewise taken only under the controller lock with the same
  test (section 7.1).
- **Kick** where interrupts are restored today: after a successful resume
  and a successful in-place recovery, the caller re-arms the poller at the
  active period if it is open and parked. `KeSetTimer` is callable at
  DISPATCH_LEVEL, so the recovery path can kick.
- The timer service's own close (`TimersClosed`) is not used for any of
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

A submission whose need is shorter than the period armed re-arms the timer
earlier:

- a control, bulk or isochronous TD, or a command, needs the active period;
  an interrupt TD needs its endpoint's interval;
- under `PollLock`, if `PollOpen` and the need is shorter than `PollPeriod`,
  `KeSetTimer` at the need, `PollPeriod` = the need, `PollPromote` bumped;
  if not shorter, nothing - so a stream of submissions never pushes the
  deadline later;
- the timer DPC reads `PollPromote` as it starts and again under `PollLock`
  as it re-arms; if it changed, its own decision is stale and is replaced by
  the shorter of it and the promotion's need. A stale idle decision can
  therefore never overwrite an earlier deadline a submission set on another
  processor.

PERIODIC to ACTIVE is a promotion like IDLE to ACTIVE: a bulk transfer on a
machine with a mouse attached is re-armed at the active period, not left to
the mouse's interval.

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
  `XhciEventRingPending` taken under the controller lock, after the core's
  running test (section 5.3), so it reads a consistent `Dequeue` and cycle
  state and a ring that exists. A peek without the lock was rejected: the
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
delivery in effect and its reason (section 4), the two periods as read and
in effect, the period last armed and the state that chose it, poll runs,
poll runs that found an event, peeks and peek hits, promotions, parks and
kicks, and the shortest and longest interval observed between two poll runs
with the count of discarded intervals (section 7). One bounded log line at
start with the delivery and its reason; nothing per poll.

**The delivery in effect must be capturable** (owner, 2026-10-05): a user
on a machine that polls automatically and one forced to poll must be told
apart, by `XHCISNAP` and by the DebugView log. By `XhciLogVerbosity`
(`src/xhci_log.h`, the ladder): at 0 the channel is off and nothing is
captured, as for every other diagnostic; from 1 the counter block - the
delivery, its reason and the values in effect - is read by `XHCISNAP`; from
2 the start line is in the log ring, so `XHCISNAP`'s log read has it and,
with `XhciLogDebugView` set, the DebugView sink emits it from the controller
thread at PASSIVE_LEVEL with the rest of the ring, adding no `DbgPrint`
site. The counter block's dump at each stop, which the DebugView sink also
emits, carries the same fields.

The tear detector (`hcd_door.c:836`) needs no change: `DpcCount` counts
every drain, polled or not (`xhci_evt.c:774`).

## 9. `XHCISNAP` (task 34c.4)

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

## 10. `XHCIQUAL` (task 34c.5)

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

## 11. Readings (task 34c-V)

- **Every target, mode 2** (QEMU's `qemu-xhci` has a pin, so polling is
  forced): 98 SE and 2000 (the SMP guest, Driver Verifier) first, then ME,
  XP, Vista and 7, x86 and x64 - a HID mouse, Bulk-Only and UAS storage, a
  composite audio device playing, devices behind QEMU's hub, hot-plug,
  disable and enable, remove and rescan, and D3 where 34.4 makes a guest
  sleep.
- **No interrupt, deterministically.** No guest presents `Interrupt Pin = 0`
  that has been found, so the `qemu` flavour gets a test switch that drops
  the interrupt resource and reports pin 0 at the start, and mode 0 is read
  polling with the reason "automatic", and mode 1 failing as today.
- **Recovery and D3 under a poll delivery**: a forced in-place recovery
  (the `qemu` flavour's existing way to provoke one, or a new switch) with
  devices working after it, and a D3 and back where a guest sleeps: the park
  and kick read in the counters.
- **The races, on the Windows 2000 SMP guest under Driver Verifier**: a
  submission storm against the timer's re-arm (promotion never lost, never
  pushed later), close during a peek storm and a timer expiry (no DPC after
  release), and a drain stopped at its bound continued by the next poll. The
  pure decisions also in `test_poll`.
- **The period observed** per target, from the counters.
- **Line against poll** on the same guest: a storage copy rate, audio
  continuity, and the guest's idle CPU load.
- **The default on a pin**: mode 0 reading as `2.2.0.0` without stage C
  (the matrix unchanged), and mode 1 the same.

Bench (34c-E, owner): mode 2 on the E460 and the P14s Gen 1 under Windows 98
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

## 13. Codex's review of revision 1

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
| 10 | Wrong `XHCIQUAL` function; an unread Code 10; 5.1's review owner; the checkpoint's switch | Sections 2, 4.1 and 10; roadmap 34c.0 and the checkpoint |
