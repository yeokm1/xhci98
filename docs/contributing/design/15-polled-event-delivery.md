# Polled event delivery for the HCD

Design record for roadmap-hcd Phase 34, stage C (tasks 34c.*). DRAFT,
revision 1, 2026-10-05: written at the owner's request so the driver can run
a controller that delivers no legacy interrupt - an MSI- or MSI-X-only xHCI
controller, which neither primary target can serve today - by polling the
event ring, adaptively, instead of waiting for an interrupt. Nothing is
built.

## 1. What is asked, and what is not

A registry switch chooses how the bus learns that the controller has written
an event: the legacy line interrupt (today's only way, the default), or a
poll of the event ring on a timer whose period adapts to the work
outstanding - long when the bus is idle, short when it is busy - with the
ring also looked at whenever the driver is entered anyway. One value is
reserved for an MSI path.

Not in this design: MSI or MSI-X itself (the switch's value 2, reserved and
not scheduled; "What is not on this roadmap"), interrupters other than
Interrupter 0, and any change to the line-interrupt path, which at the
switch's default must read as `2.2.0.0` without stage C.

## 2. State today (read 2026-10-05)

- `hcd_ctl.c:248-271`: the start's resource parse takes the first
  `CmResourceTypeInterrupt` and fails the start with
  `STATUS_INSUFFICIENT_RESOURCES` when there is none, so a controller with
  `Interrupt Pin = 0` shows Code 10 on every target.
- `hcd_ctl.c:281-324`: `hcdIsr` claims on `USBSTS.EINT` through `XhciIsr`,
  counts `DpcsInFlight` and queues `IsrDpc`; `hcdIsrDpc` drains through
  `XhciEventDpc(&hc->Hc, TRUE)` and wakes the thread on a port event.
  `DpcClosed` and `DpcsInFlight` are the teardown's handle on it.
- `hcd_svc.c:163-212`: the frame timer is the pattern a poll timer follows:
  a one-shot `KeSetTimer` re-armed by its own DPC under `TimerLock` until
  `TimersClosed`. A periodic timer is not available: `KeSetTimerEx` has no
  Windows 98 export (`docs/usb-xhci-info/win98-wdm.md`, the export table).
- `hcd_svc.c:19-29`: `HcdRelativeMs` holds any period to 400,000 ms, since
  ms x 10,000 must fit a ULONG under the no-64-bit-arithmetic rule.
- The import allowlists carry no timer-resolution call
  (`ExSetTimerResolution`), and this repository has no Windows 98 export
  evidence for one, so a `KeSetTimer` period is quantised to the system clock
  tick on every target (section 7).
- `implementation-invariants.md`, "Interrupt Delivery", requires a pin-0
  controller to be refused; the release notes' "Controller" row, the
  acceptance test's step 3 and `XHCIQUAL`'s quick classification
  (`xhciqual/mmiodiag.c`, `quick_classify_mmio`) all say a pin-0 controller
  cannot be driven.
- `XHCISNAP`'s tear detector (`xhcisnap/xhcisnap.c`, the coherence
  paragraph) sums the health-check calls, the interrupt DPCs and the log's
  producer accounting; a polled controller's interrupt DPC count never moves.

## 3. The three values

Read from the controller's driver key at each start, as `REG_DWORD`, and
written by both INFs on every install path with `FLG_ADDREG_NOCLOBBER`
(`0x00010003`) under task 34.1's rule: each is where a user looks for it, and
an update keeps a value the user set. Text-mode Setup's `txtsetup.oem` writes
none, so F6 Setup runs at the defaults. The names are proposed.

| Value | Default | Meaning | Out of range |
|---|---|---|---|
| `XhciInterruptMode` | 0 | 0 the legacy line interrupt; 1 polling; 2 reserved for MSI | 2 and anything above run as 0, with one log line naming the value |
| `XhciPollIdleMs` | 50 | The poll period when nothing is outstanding (section 6) | 0 runs as 50; above 1000 held to 1000 (proposed bound, section 3.2) |
| `XhciPollActiveMs` | 1 | The poll period while work is outstanding | 0 runs as 1; above the idle period held to it |

Owner's decisions of 2026-10-05: the three values, their defaults, and that
neither period may be 0.

### 3.1 The defaults

- **0, the line interrupt.** Every machine read so far has a pin, and the
  line path is what every leg of every release has read. Polling costs
  latency on every target (section 7), so it is opt-in, except where there
  is nothing else (section 4.1).
- **50 ms idle.** A connect is debounced for 100 ms by the USB 2.0
  specification before a driver acts on it, so a 50 ms look at an idle ring
  adds no delay a user can see to a hot-plug, and costs 20 timer DPCs a
  second, each a memory read of one TRB when nothing has happened.
- **1 ms active.** Right as a request: it means "as often as the kernel will
  run the timer". On these kernels that is the clock tick, not 1 ms
  (section 7), so the value seldom binds; it is the floor a finer timer
  would reach if one is ever admitted.

### 3.2 An upper bound

Proposed: 1000 ms for the idle period, with the active period held to the
idle one. Without a bound, a large idle value makes the controller look
dead rather than slow: a device plugged in, an external hub's status
change, or a remote wake waits a whole period before anything sees it, and
a value of a minute or more reads as "USB does not work". `HcdRelativeMs`
would cap it at 400 s anyway, silently. 1000 ms keeps a hot-plug within a
second, is twenty times the default, and is the same "held to" form
`XhciFirstEnumWaitMs` uses (held to 30 s). The owner asked whether to have
none; this is open (section 12).

## 4. Choosing the mode at start

1. Read the three values (section 3), and the resource list.
2. **Mode 0 with an interrupt resource**: today's path, unchanged.
3. **Mode 1**: the ISR is not connected even when a resource is present,
   and the controller's interrupt enables stay clear (section 5.1).
4. **Mode 0 with no interrupt resource** (the pin-0 controller): section
   4.1.

The mode in effect and the reason for it are recorded in the counter block
(section 8) and in one bounded log line at start.

### 4.1 Mode 0 and no interrupt: fall back, or refuse

Proposed: fall back to polling, with the reason "no interrupt resource"
recorded, rather than fail the start. A controller without a pin can never
be driven by mode 0, so refusing it gains nothing; and since F6 Setup writes
no value, only the fallback lets text-mode Setup reach a USB keyboard or
disk on such a machine. The alternative keeps today's Code 10 and tells the
user to set `XhciInterruptMode` to 1, which is what `XHCIQUAL` says either
way (section 9). Open (section 12).

Not proposed: falling back when a pin exists but no interrupt arrives
(`XHCIQUAL`'s C4 failure, broken PIC routing). The start's No Op self-test
could detect it - the completion lands in the ring and no interrupt follows
- but a fallback decided from one missed interrupt is a guess at a timing
the driver cannot see. Such a machine is served by setting mode 1 by hand.

## 5. The poller

A `KTIMER` and a `KDPC` in the controller, `PollTimer` and `PollDpc`,
initialised with the frame timer in `HcdTimersInit` and armed after the
controller runs, beside `HcdFrameTimerStart`.

- **One drain.** `PollDpc` drains through the same body as `hcdIsrDpc`
  (`XhciEventDpc`, the thread woken on a port event), counted in
  `DpcsInFlight` and refused once `DpcClosed`, so the teardown's existing
  wait covers it and there is never a second drain to keep in step.
- **One-shot, re-armed.** At the end of each run it computes the next period
  (section 6) and re-arms under `TimerLock` unless `TimersClosed`, as
  `hcdFrameDpc` does. A run that finds work re-arms at the active period at
  once.
- **Stopped with the other timers.** Stop, remove, D3 and the in-place
  recovery (design record 07) close it at the point they close interrupt
  delivery today, and open it where they reconnect it: a poll must never
  write `ERDP` to a controller that is halted, in D3 or being reset.

### 5.1 No line asserted in mode 1

In mode 1 the controller's interrupt enables (`USBCMD.INTE` and
Interrupter 0's `IMAN.IE`) stay clear, taken from
`docs/usb-xhci-info/xhci-data-structures.md` and not from memory, so a
controller that does have a pin never asserts a level-triggered, possibly
shared, line that no ISR of this driver will claim. Whether the drain's
handling of `IMAN.IP` and `ERDP.EHB` is correct with `IE` clear - it was
written for a drain that follows an interrupt - is checked against the same
document before code, and is a review point (task 34c.2).

`XhciImodInterval250ns` has no effect in mode 1 and is still programmed as
read, so switching back to mode 0 needs no other change.

## 6. The adaptive period (pure core, `xhci_poll.c`)

A pure function, host-tested in `test_poll`, from the bus's state to the
next period. Three states:

| State | When | Period |
|---|---|---|
| ACTIVE | a command on the command ring; any control, bulk or isochronous TD outstanding; a port in reset or enumeration; or an event drained within the last idle period | the active period |
| PERIODIC | nothing above, but interrupt-IN TDs pending | the shortest pending interrupt endpoint's service interval, rounded down to whole ms, held between the active and idle periods |
| IDLE | nothing pending | the idle period |

PERIODIC is the state that makes the poll adaptive rather than two-speed.
A HID device and every external hub keep an interrupt-IN transfer pending
all the time, so with ACTIVE defined as "anything pending" a machine with a
mouse attached would never leave the active period. A mouse at an 8 ms
interval is looked at every 8 ms; a hub's status endpoint at the idle
period, since its interval is longer than that.

The hysteresis in ACTIVE's last clause keeps a burst of bulk transfers from
flapping between periods.

## 7. What the periods mean on these kernels

A `KeSetTimer` DPC runs at the first clock tick after its due time. The
tick is typically 10 ms on an NT uniprocessor HAL and about 15.6 ms on an
ACPI or multiprocessor HAL; Windows 98's has not been measured in this
repository. So an active period below the tick runs at the tick, and the
counters (section 8) record the period achieved, measured from `KeQuerySystemTime`
between runs, per target, in task 34c-V.

What follows from it:

- **Latency.** A transfer completed by the controller waits up to a tick
  before its IRP completes. Interactive HID is unaffected at a tick of 10 to
  16 ms; a storage request at queue depth 1 completes at most once per tick
  per stage, so polled storage throughput is bounded by the tick, and the
  opportunistic checks (section 7.1) are what recover part of it.
  Isochronous audio must be kept fed across a tick; whether each target's
  `usbaudio.sys` queues far enough ahead is read, not assumed.
- **A finer timer** (`ExSetTimerResolution` on NT) is not part of this
  design: it has no allowlist row, no Windows 98 export evidence, and raises
  the tick for the whole system. If the readings show polled throughput
  unusable, it is the next question, under the import gate's evidence rule.

### 7.1 Opportunistic checks

Whenever the driver is entered anyway, it looks at the event ring: a read
of the TRB at the dequeue pointer, and its cycle bit against the consumer
cycle state. No MMIO and no lock.

- **Where**: after a doorbell is rung on the URB submission path, once the
  controller lock is released; at the end of each drain, for transfers a
  completion routine resubmitted; on each controller-thread pass and health
  poll; and in the frame timer's DPC.
- **What it does on a hit**: queues the drain DPC, with `DpcsInFlight`
  counted as `hcdIsr` does. It never drains inline. A drain completes IRPs,
  a class driver's completion routine resubmits, the submission would peek
  again, and the recursion would run down a kernel stack - Windows 98's is
  small - with no bound.
- **What it does on a submission while IDLE**: re-arms the poll timer at the
  active period, so a transfer submitted to an idle bus waits a tick, not
  the idle period.
- **A wrong peek is harmless.** Read without the controller lock, it can see
  a stale dequeue pointer and miss an event (the timer is the backstop) or
  see one already drained (an empty drain). Neither is a defect, and neither
  is counted as one.

The checks run in mode 1 only, so mode 0 runs exactly today's code.

## 8. Counters and log

In the counter block, and so in `XHCISNAP`: the mode in effect, its reason
(requested, or fallback with no interrupt resource), the three values as
read and as in effect, the period last armed and the state that chose it,
poll runs, poll runs that found an event, opportunistic peeks and hits,
promotions from IDLE, and the shortest and longest interval achieved between
two poll runs. One bounded log line at start with the mode and its reason;
nothing per poll.

## 9. The tools

**`XHCISNAP`** (task 34c.4):

- The companion reports the three values as it does `XhciImodInterval250ns`
  (`write_companion_imod`): status, value as read, value in effect, and the
  range note, with the ranges duplicated in the tool for the reason the
  `SNAP_IMOD_*` constants are.
- It states the mode in effect and why, and in mode 1 says the interrupt
  moderation value has no effect.
- **The tear detector adds the poll runs.** Its sum counts interrupt DPCs,
  which never move on a polled controller, so without them a dump taken
  while polling could read "unchanged" with the controller being serviced.
- The header grows under the snapshot's existing schema rule, and the tool
  still reads a `2.1.0.0` or earlier header.

**`XHCIQUAL`** (task 34c.5): a controller is no longer disqualified for
`Interrupt Pin = 0`. The quick classification (`quick_classify_mmio`) and
the verdict report it as qualified for polling only - "no legacy interrupt:
set `XhciInterruptMode` to 1" - and the run continues to the active tests
it can take without an interrupt. A C4 failure (the pin exists, no
interrupt arrives) reads the same way: not a disqualification, polling
required. The host suite (`xhciqual/test/test_mmiodiag.c`) carries both.

## 10. What changes in the documents

`implementation-invariants.md`, "Interrupt Delivery"; `architecture.md` line
309's statement of line-based delivery; the release notes' "Controller" row
and a new row for the switch; the acceptance test's step 3 and its stop on
the pin; `xhciqual/hardware-testing.md` and record 01's C4 note; record 13
where it describes the ISR; `source-files.md` for `xhci_poll.c`; the
locking record for `PollDpc`. Task 34.6.

## 11. Readings (task 34c-V)

QEMU's `qemu-xhci` has a pin, so every guest leg is mode 1 by setting:

- 98 SE and 2000 (the SMP guest, Driver Verifier) first, then ME, XP, Vista
  and 7, x86 and x64: a HID mouse, Bulk-Only and UAS storage, a composite
  audio device playing, devices behind QEMU's hub, hot-plug, disable and
  enable, remove and rescan, and D3 where 34.4 makes a guest sleep.
- The period achieved per target, from the counters: the tick each kernel
  actually gives.
- Mode 0 against mode 1 on the same guest: a storage copy rate, audio
  continuity, and the guest's idle CPU load.
- Mode 0 reading as `2.2.0.0` without stage C (the matrix unchanged).
- The pin-0 fallback (if taken): no guest here presents `Interrupt Pin = 0`
  that has been found; it is built from the resource list's absence and
  read on the bench if the owner holds an MSI-only controller.

Bench (34c-E, owner): mode 1 on the E460 and the P14s Gen 1 under Windows 98
SE, and an MSI-only controller if one is at hand.

## 12. Open decisions

1. The upper bound (section 3.2): 1000 ms proposed; the owner asked whether
   to have none.
2. Mode 0 with no interrupt resource (section 4.1): fall back to polling
   (proposed) or refuse.
3. The three value names (section 3).
