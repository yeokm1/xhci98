# Polled event delivery for the HCD

Design record for roadmap-hcd's polled event delivery. It is now part of
Phase 36 (owner, 2026-10-08). The task ids in this record are the old
placeholders, which map as follows:

| Old | Phase 36 |
|---|---|
| `POLL.0` | 36.5 |
| `POLL.1` | 36.6 |
| `POLL.2` | 36.7 |
| `POLL.3` | 36.8 |
| `POLL.4` | 36.9 |
| `POLL.5` | 36.10 |
| `POLL-V` | 36.13 |
| `POLL-E` | 36.14 |
| (new) MSI on Vista and 7 | 36.15, 36.16 |

It was drawn up as Phase 35, and the review history in section 13 keeps the
old phase numbers. Section 4.2 and decision 6 are the 2026-10-08 amendment
for issue 12. Section 4.3 and decision 7 add MSI on Vista and 7 (36.15).
That reverses section 1's "not in this design" for MSI on NT6.
Single-message MSI-X is admitted with it (owner, 2026-10-09); more than one
message or interrupter stays out.

Revision 4, 2026-10-05: written at the owner's request so the driver can
run a controller that delivers no legacy interrupt - an MSI- or MSI-X-only
xHCI controller, which neither primary target can serve today - by polling
the event ring, adaptively, instead of waiting for an interrupt. Reviews
covered revisions 1 to 3 the same day (10, 8 and 5 findings, all taken;
section 13 maps each) and converged at round 4, on revision 4.

**Revision 5, 2026-10-09 (task 36.5).** The issue 12 amendment made
concrete before code: the watcher of section 4.2 (its predicate, period,
count and window, and the move), WATCHING as a role in section 5, the
mode rules reconciled with 36.15's decisions (sections 1, 3, 4 and 11),
section 4.1 checked against the spec, the inputs and hooks of sections 6
and 7.1 named, and section 4.3 designed in full for 36.15, so the MSI code
has a reviewed design. Reviewed in 36.5's loop (section 13.5).

**Revision 6, 2026-10-10 (task 36.15, as built).** Section 4.3 reconciled
with design record 18 - the NT 6 x86 MSI helper and the owner's decisions
of 2026-10-09 written into it (its section 12) - and with the code: x86
reaches `IoConnectInterruptEx` through `xhcimsi.sys`; the requirements
filter keeps only the line configurations where x86 has no helper bound,
and wherever the effective mode is 1, on both architectures; mode 1 on x86
with no line offered and no helper fails the start; the connection passes
no spin lock of its own. Sections 3, 4, 8 and 11 follow.

**Revision 7, 2026-10-11 (owner's decision of that day; roadmap task
36.53).** `XhciInterruptMode` 1 and 3 become strict, amending the
decision of 2026-10-09 (section 12, decision 8). **1 is the interrupt
line only**, on every system: on Vista and 7 a message grant under mode
1 no longer runs as mode 0, it fails the start, on both architectures
and whether or not the x86 helper is bound - record 18 section 4.3's
narrow "no helper and no line offered" condition goes, and reasons 11
and 13 are no longer produced. **3 is a message-signalled interrupt
(MSI or MSI-X) only**: on Vista and 7 the requirements filter keeps only
the message configurations (record 18 section 4.2), the start uses the
message where one is granted and connected, and fails otherwise - no
fallback to the line or to polling; on Windows 98 SE, ME, 2000 and XP,
32- and 64-bit, which have no message-signalled interrupts, mode 3
fails the start. 0 and 2 are unchanged; 4 and above still run as 0.
Sections 3, 3.1, 4, 4.3, 8 and 12 follow; reasons 14 to 18 are
appended.

**Revision 8, 2026-10-13 (task 38.4, designed before code).** Section
7.2: a finer system timer while polling, on Windows 2000 and later -
`ExSetTimerResolution` asked for while the poller is active and given
back when it idles, at a power transition and at the stop, from a timer
thread of the controller's, with a registry value to turn it off (on by
default). The 32-bit image reaches the routine through the helper of
design record 18, extended to Windows 2000 and XP and renamed
`xhcihlpr.sys` (record 18 section 15); where sections 4.3 and 10 name
`xhcimsi.sys` they describe `2.3.0.0` and `2.3.1.0`. Section 7's
"next question" is answered there. The owner answered section 7.2.13's
and record 18 section 15.13's decisions the same day, and revised T3, T5
and H2 after review round 1 (section 12, decision 9): `XhciPollIdleMs`
keeps its default of 50, measured against 500 by the legs (section
7.2.6a).

## 1. What is asked, and what is not

A registry switch chooses how the bus learns that the controller has written
an event: automatically (the default), the legacy line interrupt, or a poll
of the event ring on a timer whose period adapts to the work outstanding -
long when the bus is idle, short when it is busy - with the ring also looked
at whenever the driver is entered anyway. Automatic takes the line interrupt
where the controller has one and polls where it has none.

Also in this design since 2026-10-08: a line interrupt that exists but is
proven at run time never to arrive moves to polling in automatic mode
(section 4.2, issue 12), and on Vista and 7 a message-signalled interrupt -
MSI, or MSI-X with one message - is automatic mode's first choice where
Windows grants one (section 4.3, task 36.15).

Not in this design: MSI or MSI-X on Windows 98, ME, 2000 or XP (none of
them has a message interrupt to grant), more than one message,
interrupters other than Interrupter 0, and any change to the line-interrupt
path beyond section 4.2's watcher, which reads and never writes until it
moves. On a controller whose line interrupt works, the default must read
as the release before it does: the same delivery, the same ISR, the same
drains.

## 2. State today (read 2026-10-05, and by review on revision 1)

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
- **Since `2.2.0.0` (task 35-T.1, after this section was read) the ISR is
  not the only producer of `IsrDpc`.** Design record 17 section 4.1's
  lost-interrupt backstop (`hcd_ctl.c`, `hcdBackstop`), a step of the
  controller thread's own under the power gate, peeks the event TRB at the
  software dequeue with `XhciEventRingPending` under the controller lock and
  queues `IsrDpc` through `hcdIsr`'s admission - `DpcsInFlight` counted
  before the insertion and rolled back on a refusal, nothing once
  `DpcClosed` - when the same pending event (dequeue, cycle, drain
  generation, start generation) has stood for 100 ms on the tolerance clock
  (record 17 section 4.0). It writes no register and counts
  `BackstopDrains`; `XhciTolerance` `0` turns it off. Section 7.1's
  opportunistic checks reuse that peek with their own staleness rule, and
  under a poll delivery the backstop's queueing is one more producer the
  close sequence above already covers, since it goes through the same
  admission. **Since `2.3.0.0` (roadmap-hcd 36.55) the step runs in the
  tolerance clock's timer DPC, not on the controller thread** (record 17
  section 4.1, "Amended"). Windows 98 SE was seen to leave the thread
  unscheduled while the DPC kept running. The release now raises
  `DpcClosed` with an interlocked exchange, because that DPC can race
  the close.

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

Read from the controller's driver key at each start, as `REG_DWORD` (and,
on Vista and 7, `XhciInterruptMode` also at `AddDevice`, for the
requirements filter that runs before the start: design record 18 section
4.2), and
written by both INFs on every install path with `FLG_ADDREG_NOCLOBBER`
(`0x00010003`) under task 34.1's rule: each is where a user looks for it, and
an update keeps a value the user set. Text-mode Setup's `txtsetup.oem` writes
none, so F6 Setup runs at the defaults - automatic, which polls a controller
with no interrupt.

| Value | Default | Meaning | Out of range |
|---|---|---|---|
| `XhciInterruptMode` | 0 | 0 automatic; 1 the line interrupt only; 2 polling; 3 a message-signalled interrupt only (2026-10-11, section 4.3); 4 and above reserved | 4 and above run as 0, the value read recorded (section 8) |
| `XhciPollIdleMs` | 50 | The poll period when nothing is outstanding (section 6), and the window after the last event in which the poller stays ACTIVE (section 6.1), which also governs the finer timer's release (section 7.2.6) | 0 runs as 50; above 5000 held to 5000 |
| `XhciPollActiveMs` | 1 | The poll period while work is outstanding | 0 runs as 1; above the idle period in effect held to it |

A value that is absent, not a four-byte `REG_DWORD`, or unreadable takes
the default, as `XhciIntelIrqRoute`'s strict reader does; the read status
is recorded apart from the value (section 8), so a 0 somebody set is told
from a value never found. The **effective mode** is what the rules below
act on: 0 for a value of 0 or 4 and above, else the value (1, 2 or 3;
3 read as 0 from 2026-10-09 to 2026-10-11). Every rule
in this record that says "mode 0" means the effective mode 0.

Owner's decisions of 2026-10-05 (section 12): the values, their names and
defaults, the numbering, that neither period may be 0, and the 5000 ms
bound.

### 3.1 The defaults

- **0, automatic.** On every machine whose line interrupt works it reads
  as the release before it; on one with no interrupt, it polls instead of
  failing the start; on one whose line never arrives, it moves to polling
  (section 4.2); and on Vista and 7 it takes a message interrupt where
  Windows grants one (section 4.3). It is the only mode that falls back:
  1 and 3 each name one delivery and fail the start without it
  (2026-10-11), and 2 always polls.
- **50 ms idle.** A connect is debounced for 100 ms by the USB 2.0
  specification before a driver acts on it, so a 50 ms look at an idle ring
  adds no delay a user can see to a hot-plug, and costs about 20 timer DPCs
  a second, each a service pass when nothing has happened. Since revision
  8 the same value is how long the finer timer outlives the last event
  (section 7.2.6); whether 500 would be the better default is measured by
  section 7.2.6a's legs and decided on them (owner, 2026-10-13).
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

The start settles the **delivery** - line, message (MSI or MSI-X, section
4.3) or poll - and its **reason**, once, at PASSIVE_LEVEL in `hcdStartBody`,
after the values (section 3) and the resource list are read and **before**
anything is connected:

- **The resource list.** The memory window stays required. An interrupt
  descriptor no longer is: `hcdParseResources` records whether one was
  given and whether its raw form is a message (`CM_RESOURCE_INTERRUPT_MESSAGE`,
  `hc->IntDelivery` since 36.3), and the decision below says what its
  absence means. Mode 1 keeps today's refusal, so "forced" fails exactly as
  today.
- **The pin.** Read once by the HCD from configuration space (`Interrupt
  Pin`, offset 3Dh, through the same `XhciReadPciConfig` the core uses),
  before the connection, with its read status kept apart from its value.
  The core's own read in `XhciInitController` stays, and so does its rule
  that a failed read with an interrupt resource present is accepted, the
  pin recorded as `0xFFFFFFFF` and the resource trusted
  (`xhci_init.c` ~4705). The in-place recovery reads no configuration space
  and keeps the start's answer.
- **The decision** is one pure function in `xhci_poll.c`
  (`XhciPollDecide`, host vectors in `test_poll`), from the effective mode,
  whether an interrupt resource was given, whether it is a message, whether
  its routines were found, the pin's status and value, and whether the
  system can grant a message at all (`IoIsWdmVersionAvailable(6, 0)`,
  since revision 7), to the delivery and the reason.

**A line resource** - every target, and Vista and 7 without a message
grant:

| Effective mode | Interrupt resource | Pin | Delivery | Reason |
|---|---|---|---|---|
| 0 | present | not 0 | line | automatic |
| 0 | present | unreadable | line, as today | automatic, pin unreadable |
| 0 | present | 0 | poll | automatic: interrupt pin 0 |
| 0 | absent | any | poll | automatic: no interrupt resource |
| 1 | present | not 0, or unreadable | line, as today | forced (pin unreadable, if so) |
| 1 | present and pin 0, or absent | | none: the start fails as today | forced, no interrupt |
| 2 | any | any | poll | forced |
| 3 | any, on Windows 98 SE, ME, 2000 or XP (32- or 64-bit) | any | none: the start fails | forced message, no message interrupts on this system |
| 3 | present or absent, on Vista or 7 | any | none: the start fails | forced message, none granted |

**A message resource** - Vista and 7 only, where the INF asks for one
(section 4.3):

| Effective mode | Connection | Delivery | Reason |
|---|---|---|---|
| 0 | the message connected | MSI or MSI-X | automatic: message |
| 0 | `IoConnectInterruptEx` not found, or the connection refused | poll | message not connected |
| 0 | Windows fell back to the line | line | automatic |
| 1 | (nothing connected) | none: the start fails | forced line, a message granted |
| 2 | (nothing connected) | poll | forced |
| 3 | the message connected | MSI or MSI-X | forced message |
| 3 | `IoConnectInterruptEx` not found (x86 with no helper bound), the connection refused, or Windows fell back to the line | none: the start fails | forced message, not connected |

The pin plays no part under a message grant. With the requirements
filter enabled (design record 18 section 4.2), mode 1 asks Windows for
the line and mode 3 for the message before the start, so the failing
rows are where the list offered no such choice or was not replaced (an
unadmitted shape, the strip switched off, no memory, no list), or where
`MSISupported` 0 took the message away before the HCD saw the list.
Revision 7 replaced two rows of revision 6: mode 1 under a message grant
ran it as mode 0 ("line asked for, message granted", reason 11), and
failed only on x86 with no helper bound and no line offered ("forced, no
line offered", reason 13).

Every failing row ends the start before anything is mapped or connected,
with the status a forced line with no line has always given:
`STATUS_DEVICE_CONFIGURATION_ERROR` when an interrupt resource was
granted, `STATUS_INSUFFICIENT_RESOURCES` when none was - Code 10 either
way - except mode 3's "not connected" row, which can be known only after
the connection is tried and so fails there, after the release disconnects
whatever was connected (a line Windows fell back to), with
`STATUS_DEVICE_CONFIGURATION_ERROR`. Each failure is recorded before the
start returns: the delivery none and its reason in the counter block, the
start-decision event (design record 18 section 9), and the start's log
notes, flushed to DebugView when `XhciLogDebugView` is set (section 8).

A value of 4 and above is recorded as "reserved value N, run as
automatic".

The delivery and reason are held in the controller (`hc`) and copied into
the core's extension (`EventDelivery`) after the start zeroes it, so the
in-place recovery and every resume keep them; only a new start settles
them again, and only the move of section 4.2 changes them in between. For
a poll delivery:

- **`ResourcesTypes` tells the truth.** Both sites (`hcd_ctl.c`
  `hcdStartBody`, the extension's copy and the `USBPORT_RESOURCES` it hands
  `XhciInitController`) set the interrupt bit only when an interrupt
  resource was taken, and the core's two gates in `xhci_init.c` - the
  interrupt bit and the pin-0 refusal - apply to a line delivery only
  (they read `EventDelivery`). Mode 1 keeps both.
- **The ISR is not connected**, even when a resource exists (mode 2 on a
  machine with a pin).
- **No interrupt is ever enabled.** `XhciEnableInterrupts` is not called,
  so `XHCI_EXT_FLAG_INTERRUPTS` is never set for a poll delivery, and
  neither the drain (`xhci_evt.c`, the re-arm) nor resume nor recovery
  (`xhci_init.c`, their `XHCI_EXT_FLAG_INTERRUPTS` branches) re-arms
  `IMAN.IE` or sets `USBCMD.INTE`; every poll drain passes `FALSE` for the
  re-arm as well. A controller with a pin therefore never asserts a
  level-triggered, possibly shared, line that no ISR of this driver claims.
- **The routing quirk writes nothing** (36.3): `hc->IntDelivery` reads
  poll, at the start and at each return to D0.

`XhciImodInterval250ns` is still programmed as read; it has no effect
without an interrupt.

### 4.1 What the hardware needs (checked 2026-10-09)

With `USBCMD.INTE` and `IMAN.IE` clear, cycle-bit dequeue and the `ERDP`
write remain valid. Checked against `docs/usb-xhci-info/xhci-data-structures.md`
and the spec it transcribes:

- **The enables gate signalling, not the ring.** `IMAN.IE` 0 leaves the
  Interrupter "prohibited from generating interrupts" (Table 5-38, p.391),
  and `USBCMD.INTE` enables "host system interrupts generated by
  Interrupters" (5.4.1, p.360). Neither appears in the event ring's producer
  rules: the xHC writes an Event TRB and advances its enqueue by the Cycle
  bit and the advertised `ERDP` alone (4.9.4, p.162), and detects Event Ring Full
  from `ERDP` (the data-structures note on intermediate `ERDP` writes). So
  events are written, and consumed by a cycle-bit dequeue, with both
  enables clear.
- **`IP`, `EINT` and `EHB` may stand.** `IP` is set by `IPE`, `IMODC` and
  `EHB` (4.17.5, p.270), `EHB` with it (4.17.2, p.264; 5.5.2.3.3, p.394),
  and `EINT` on each `IP` 0-to-1 transition (5.4.2). Under a pin, `IP` is
  cleared only by software (4.17.5), so under a poll delivery it may stay
  1 for the whole run. With `IE` clear the Interrupter is prohibited from
  generating interrupts (Table 5-38), and with `INTE` clear no host system
  interrupt is issued (Table 5-20, p.360), so a standing `IP` asserts
  nothing; the INTx line that `IP` holds asserted (4.17.3, p.268) is one an
  enabled Interrupter raised. `EHB` governs whether `IP` may be set again,
  not whether the ring may be read.
- **The drain's writes are kept.** The poll drain is `XhciEventDpc` itself:
  it publishes `ERDP` every 32 events with `EHB` written 0 and ends with
  the `EHB`-clearing write (the data-structures notes on ERDP). It never
  acknowledges `EINT` or `IP` - only the ISR does, `EINT` before `IP`
  (Table 5-21's note) - so no order is at stake.
- **MSI.** "If MSI or MSI-X interrupts are enabled, IP shall be cleared to
  '0' automatically when the PCI Dword write generated by the Interrupt
  assertion is complete" (4.17.5, p.270). Hence section 4.2's predicate is
  a line's only. The ISR's acknowledgement of `IP` under a message is a
  write of 1 to an RW1C bit already 0, which changes nothing.

`xhci-data-structures.md` gains the 4.17.5 `IP` rules and the "prohibited
from generating interrupts" line beside its IMAN row, so the next reader
does not need the spec for them; its "pin-based interrupts (this driver's
only mode)" sentence is 36.20's.

The "no interrupt, no DPC" chain in the comment near `xhci_evt.c`'s
`XhciEnableInterrupts` describes interrupt-driven progress; an
independently scheduled poll does not depend on it.

### 4.2 A line that never arrives (amendment, 2026-10-08; issue 12)

Issue 12 found a controller whose line interrupt exists but never reaches
the ISR. On the Dell E7250 the BIOS routes the xHCI's INTA to a different
IRQ from the one ACPI gives the OS. The table above settles that
controller on "line, automatic", and the driver then lives on the
lost-interrupt backstop alone. With no other device on its line, it fails
outright. The routing quirk (roadmap 36.3) repairs the E7250, in APIC and
PIC mode. This rule covers what it cannot: an unlisted chipset, a quirk set
to 0, or another firmware fault.

**Mode 0 only, and a line delivery only:** a line delivery that is proven
dead at run time moves to polling, with the reason "automatic: line
interrupt not delivered". Mode 1 (the line, forced) never moves, so it
reads as today. Mode 2 polls from the start. A message delivery is not
watched.

**Who looks** (review of `7567dec`; decided by the owner,
2026-10-09). `hcdBackstop` runs on `hcdThread`, and `hcdCommand` blocks
that thread in a command wait for the very completion a dead line never
delivers (issue 12 section 2), so the backstop cannot be the observer. The
poller's own timer and DPC look instead: at a mode 0 start under a line
delivery the poller is opened in the **watch role** (section 5.2), and its
DPC runs whatever `hcdThread` is doing, command waits and `XhciTolerance` 0
included. Task 36.55 keeps this decision and carries its reason one step
further. Windows 98 SE was seen to leave `hcdThread` unscheduled
altogether while a copy waited on a dead line under mode 1. So the
backstop's own step, which is mode 1's only delivery on such a line, now
runs in the tolerance clock's DPC as well (record 17 section 4.1,
"Amended"). The same starvation was reached in mode 0 on a working line
by one lost interrupt during a copy to a drive in MS-DOS compatibility
mode, so the move is not about the dead line alone. The watcher is still the only observer of a dead line. The
backstop drains and decides nothing about the line.

**A look**, every `XHCI_POLL_WATCH_MS` (100 ms) - a fixed period of the
watch role's own, independent of `XhciPollIdleMs` and `XhciPollActiveMs`
and of section 6.2's conversion (section 5.2) - from the poller's timer
DPC with the controller lock held and only while `PollService` is 1
(section 5.3), so the window it reads is a running controller's:

1. Read `IMAN`. All ones, or `IP` or `IE` clear: the look is **clear**.
2. Read `USBCMD`. All ones, or `INTE` clear: **clear**.
3. Otherwise the look is **asserted**: `IP`, `IE` and `INTE` are all 1, so
   the controller is holding its INTx asserted (4.17.3, p.268). It records
   `InterruptsClaimed` as it stood.

The look reads two registers and writes none. It does not ask the event
ring: a backstop drain empties the ring without acknowledging `IP` (only
the ISR acknowledges it), so on a dead line `IP` stays 1 through every
drain, and a ring that reads empty says nothing about the line.

**The verdict.** A **stale pair** is an asserted look whose previous look
was also asserted, with `InterruptsClaimed` unchanged between them: `IP`
has stood unacknowledged with both enables up for a whole period, and no
interrupt of this controller was claimed meanwhile. The line is **proven
dead** at the third stale pair in a row (`XHCI_POLL_WATCH_PAIRS`, four
asserted looks), provided at least `XHCI_POLL_WATCH_DEAD_MS` (300 ms) of
system time, by the low word of `KeQuerySystemTime`, lies between the
first of those looks and the last, so a timer that expires early cannot
shorten the window. Any clear look, any claim, any park, and any change of
`PollServiceGen` (section 5.3) starts the count afresh: each look records
the generation it was made under, and every revocation and every grant of
`PollService` advances it, so looks from two runs of the controller are
never joined, even when an in-place recovery or a suspend and resume
completes entirely between two looks. The count, the generation and the
elapsed time are one pure function, `XhciPollWatchLook`, with host
vectors.

Why it cannot trip on a working line:

- On a line that delivers, `IP` set with `IE` and `INTE` up is a state of
  at most the moderation interval plus the ISR's latency: `IMODI` is held
  to 4000 (1 ms) by the start, and the ISR acknowledges `IP` and writes
  `IE` 0 when it claims. Four looks 100 ms apart that all find it, with no
  claim between, are three such episodes each lasting a whole period.
- Between the ISR and its DPC, `IE` is 0 (the ISR writes it so), so that
  interval reads clear, not asserted.
- A **shared** line is covered by the claim count, not by the entry count:
  an interrupt of another device on the line enters the ISR
  (`InterruptCount`) and is declined; one of this controller's is claimed
  (`InterruptsClaimed`) and its `IP` acknowledged. On the E7250 with the
  EHCI enabled, the EHCI's interrupts on the xHCI's assigned line do reach
  this ISR, which then finds `EINT` set and claims, so the count resets on
  each; the line is proven dead in any interval of 300 ms in which the
  EHCI is quiet and an xHCI event waits. That is the behaviour wanted:
  such a line is the "slow" one issue 12 described, delivering only at the
  EHCI's pace.
- A **slow** line that delivers late - but within a period - has a claim
  between any two looks and never makes a stale pair.
- The in-place recovery, a suspend and a stop clear `PollService` first
  and advance `PollServiceGen` (section 5.3): a look made meanwhile parks
  and looks at nothing, and one made after the controller is back carries
  a new generation, so the count starts afresh either way.

**The move**, made by the watcher's own DPC as the owner of the poller, in
the same controller-lock hold as the look that proved the line dead:

1. `EventDelivery` becomes poll and the reason "automatic: line interrupt
   not delivered", in the extension and in `hc`, with the counter block's
   delivery and reason (section 8).
2. `XHCI_EXT_FLAG_INTERRUPTS` is cleared, so no drain, resume or recovery
   re-arms.
3. **The line is retired, with the ISR excluded** (review of
   revision 5, rounds 1 to 3). `LineMoved` is set to 1 in the extension
   first: the delivery was a line, and its retirement is now owed. Masking
   forbids new interrupts, but the INTx a pin controller has already
   asserted "remains asserted until the device driver clears the Interrupt
   Pending (IP) flag" (4.17.3, p.268), and the controller lock does not
   exclude the DIRQL ISR. So the retirement runs inside
   `KeSynchronizeExecution(hc->Interrupt, ...)` - the interrupt object's
   own spin lock at DIRQL, which the ISR runs under on every processor -
   in a routine that takes no lock of the driver's and touches only the
   three registers and the extension's two words below (`XhciPollRetire`
   in `xhci_poll.c`, over register callbacks, with host vectors over a
   modelled register file). `KeSynchronizeExecution` is in Windows 98
   SE's `ntkern.vxd` and imported by its own `uhcd.sys` and `openhci.sys`,
   so it takes an `all` allowlist row with that precedent. Its steps, each
   a bounded number of attempts with no wait. It is entered only while
   the register layout is valid (`HcInfoStatus` OK, read under the
   controller lock by its caller), and it writes nothing while a reset is
   under way - `USBCMD.HCRST` or `USBSTS.CNR` read set, the guard
   `XhciMaskInterrupts` keeps, since 5.4.1 forbids Operational and Runtime
   writes then - nor derives a write from an operand that read all ones;
   either case answers "not proven", and the PASSIVE containment below
   goes on to its configuration-space step without touching MMIO (round
   7).
   1. **Both enables down, proven.** `USBCMD.INTE` written 0 and
      `IMAN.IE` written 0 (`IP` written 0, so it is kept), then both read
      back, up to three times, until `INTE` and `IE` both read 0 - both,
      not either, which is all `XhciMaskInterrupts`' own proof asks. This
      step does **not** set `InterruptDeliverySuppressed`: that word opens
      the ISR's existing decline gate, which must stay shut while a
      retirement is owed (below). Not proven: nothing else is touched.
   2. **`IP` acknowledged, proven.** `IMAN` written with `IP` 1 and `IE`
      0, then read back, up to three times, until `IP` reads 0. A second
      write is not wasted: with `IE` 0, `IP` may be set again at once by a
      pending event if a backstop drain had cleared `EHB` (4.17.5 does not
      ask `IE`), but setting it sets `EHB` too, so the next acknowledgement
      stays. That newly pending `IP` asserts nothing - `IE` is 0 - and is
      not the standing assertion this step retires. Not proven: `EINT` and
      the ISR's path to it are untouched.
   3. **`LineRetired` set to 1**, inside the routine, so an ISR either ran
      before it (and claimed, as ever) or runs after it and sees it.
      `EINT` is not acknowledged: Table 5-21's rule that software using
      `EINT` clears it before `IP` is about not losing an `IP` transition,
      and from here no interrupt is ever wanted again, so `EINT` is left
      as it stands and no ISR of this controller reads it.

   **A retirement owed is retired at every chance** (rounds 3 to 6).
   The verdict says only that no claim came in a 300 ms window, not that
   the ISR can never acknowledge later (on the E7250 an EHCI interrupt can
   still bring it there), so while the retirement is owed the ISR keeps
   its acknowledgement path (`LineMoved`, below), and the driver works
   through these, in order, until one proves the line can no longer be
   asserted:
   1. **The DIRQL retirement** above, at the start of every service pass.
   2. **The PASSIVE containment**, `HcdPollContain(hc)`: one helper, called
      with the power gate held and no spin lock held, only while the
      controller is started and in D0 (its power state read under the
      gate). **A lifecycle caller first revokes the poller's service** -
      `PollService` 0 and `PollServiceGen` advanced, under the controller
      lock (`HcdPollRevoke`) - and only then reads whether a retirement is
      owed: the watcher moves only inside an admitted pass, holding the
      controller lock from its look through the move, so after the
      revocation no new obligation can appear behind the check (round 7).
      It runs the DIRQL retirement once more and, if that does not
      prove, sets bit 10 of the PCI Command register (Interrupt Disable;
      PCI 3.0 6.2.2: "when set, the function is prevented from asserting
      INTx#", which on PCI Express sends Deassert_INTx), through the same
      configuration path and read-modify-write the Bus Master Enable
      restore uses (`XhciReadPciConfig`, `XhciWritePciConfig`), with no
      controller lock or `PollLock` held across the requests, and reads it
      back. Set on read back is a proof whatever `IP` holds, and
      `LineRetired` is then published inside `KeSynchronizeExecution` as
      in step 3.3. It is called by the controller thread on each pass while
      the retirement is owed; by the suspend paths in `hcd_power.c` before
      `XhciSuspendController`; by `hcdRecover` before
      `XhciRecoverController`; and by the stop, after `hcdThreadStop` and
      before the release disconnects the interrupt, each after its
      revocation, so a move made just before a stop or a suspend is
      contained by that path itself. Outside
      D0 it does nothing and records why (the controller is not powered,
      so it is not asserting).
   3. **The in-place recovery**, asked for through the existing
      `XhciRequestControllerReset` when the thread's containment does not
      prove: it enters the existing machinery whole - `ControllerFailed`
      and `RecoveryRequested`, the recovery's bounded retries and its
      window (35-T.6), and their existing terminal and terminal release.
      `XhciInitController` sets `LineRetired` as soon as its HCRST has
      completed while `LineMoved` is set, so the proof stands even if a
      later step of that initialisation fails. Through the
      reinitialisation window the ISR's `HcInfoStatus` gate stays - it may
      not touch a register whose base is being re-derived.
   4. **Uncontained**: a stop reached with the retirement still owed (the
      stop's own containment having failed) disconnects as PnP requires,
      and the record says so (`intr.retire.uncontained`, the counter
      block's retirement state 3). No new latch is added: a controller the
      recovery cannot restore is the existing terminal, and is recorded as
      such.

   **Interrupt Disable is the start's to undo, after the reset.** A later
   start whose delivery is a line clears the bit, if a configuration read
   finds it set, only after `XhciInitController` has succeeded - its
   HCRST having returned `IP`, `IE` and `INTE` to 0 - and immediately
   before `XhciEnableInterrupts`, with the ISR connected; it reads the bit
   back and records it (`intr.intxdisable.cleared`). A start that fails
   before then leaves the bit set. A bit that will not clear leaves the
   start running on a line that cannot assert: in mode 0 the watcher then
   proves it dead and moves to polling; in mode 1 it is recorded
   (`intr.intxdisable.stuck`) and runs as a dead line does today.

4. `hc->IntDelivery` becomes poll, so the routing quirk's run at a later
   return to D0 writes nothing. A write it made at the start is not undone
   (36.3).
5. Under `PollLock`, inside the same hold, the poller's role becomes
   service (section 5.2). The ownership does not change hands: the DPC
   owned the poller as a watcher and owns it as a server, and a close
   asked meanwhile is honoured by its CLOSING exactly as for any expiry.
6. With the controller lock released, the same DPC runs its first service
   pass (section 5.2) at once: the drain the dead line never delivered.

**The ISR after the move** claims nothing once the line is retired, so a
shared line's other devices are unaffected: `XhciIsr` declines, before it
reads `USBSTS`, while `LineRetired` is 1 - further INTx assertion proven
prevented, by a masked acknowledgement, a verified PCI Interrupt Disable
or a completed HCRST - which extends the existing rule that
declining is free only once this controller can no longer be asserting
the line. **While a retirement is owed** (`LineMoved` 1, `LineRetired`
0), the ISR skips its existing suppression gate - the one that declines
when `InterruptDeliverySuppressed` is set and the controller is not
initialized or has failed, which a suspend's or a recovery's own mask
would otherwise open - and claims on `EINT` and acknowledges as it always
has, as long as the window decodes (`HcInfoStatus`); its DPC drains with
the re-arm off (`XHCI_EXT_FLAG_INTERRUPTS` is clear). A resume or a
recovery after the move never raises the enables again (the flag is
clear). A poll delivery chosen at start has no ISR connected and sets
neither word; both belong to the move alone, and the start's zeroing of
the extension clears them. The ISR stays connected, because
`IoDisconnectInterrupt` is a PASSIVE_LEVEL call and the move is made at
DISPATCH_LEVEL; the stop disconnects it as ever, after its own retirement
attempt.

**Moves happen in one direction only.** A later interrupt does not move the
controller back; only a new start settles the delivery again. The move is
kept through the in-place recovery and every resume like any delivery,
since it lives in `EventDelivery` and `hc`.

**Timing against issue 12's Finding 1.** The start's No Op is submitted
before the first look; on the E7250 with the line dead and no EHCI
traffic, the move comes about 300 to 400 ms after the start, and its first
service pass drains the No Op's completion. 36.4's BUSY retry waits under
a 5000 ms deadline, so an Enable Slot that met the engine busy is
resubmitted once the drain has run.

### 4.3 MSI on Vista and 7 (amendment, 2026-10-08; designed 2026-10-09 for task 36.15)

**The decision.** The owner, 2026-10-08: on Vista and 7, x86 and amd64,
the controller may take a message-signalled interrupt. It needs one
message, for Interrupter 0. Decided 2026-10-09 (roadmap 36.15 has the full
list): mode 0 takes a message where Windows granted one, else the line,
else polling; 3 read as 0 until 2026-10-11 (below); **mode 1 forces the line by itself** - the
requirements filter of design record 18 section 4.2 keeps only the line
configurations, on x86 and amd64; the line is also forced by
`MSISupported` 0 in the controller's hardware key; a message connection
that fails polls for that start under mode 0; single-message MSI-X is
admitted; the dead-line rule is the line's only; the routing quirk is not
active under a message; XP x64 ignores the key and keeps the line.

**Amended 2026-10-11 (owner; revision 7, decision 8).** Mode 1 is the
line only: where a message is granted anyway (no line offered, the
filter disabled or not replacing the list) the start fails, on x86 and
amd64, helper or not; it no longer runs the message as 0. Mode 3, which
read as 0, is now the message only: on Vista and 7 the requirements
filter keeps only the message configurations (record 18 section 4.2), a
granted and connected message runs, and anything else - no message
granted, the routines not found (x86 with no helper bound), the
connection refused, or Windows falling back to the line - fails the
start, with no fallback to the line or to polling; on Windows 98 SE, ME,
2000, XP and XP x64 mode 3 fails the start, since they grant no message.

**How each architecture reaches `IoConnectInterruptEx`** (36.5's review;
owner, 2026-10-09). Four review rounds found no way for the one x86
binary to reach it by itself that is safe without an import Windows 98
lacks: a walk of the running kernel's memory cannot prove the pages it
reads are mapped (rounds 1 to 3), and a parse of the kernel's file, tied
to the running image by the driver's own imports, proves where those
imports are but not where `IoConnectInterruptEx` is (round 4). So:

- **amd64** calls `MmGetSystemRoutineAddress` (below).
- **x86** gets them from **`xhcimsi.sys`, a small NT 6-only x86 helper
  driver** (design record 18), installed only by the Vista and 7 x86 INF
  section, which imports `IoConnectInterruptEx` and
  `IoDisconnectInterruptEx` normally and hands them to `xhci98.sys` over
  a named device and a versioned internal device control (owner,
  2026-10-09, option A). `xhci98.sys`'s x86 image gains no import Windows
  98 lacks: the one import the channel adds, `IoGetDeviceObjectPointer`,
  has stock Windows 98 SE precedent (record 18 section 3.4; owner,
  2026-10-09). This record fixes only the seam the helper plugs into (the
  connection, below); record 18 is the helper's.
- **Without the helper** - not installed, not loaded, or its channel not
  answering - the requirements filter (record 18 section 4.2) keeps only
  the configurations offering a line, so Windows grants the line and Vista
  and 7 x86 keep the line, the routing quirk and polling exactly as on XP.
  Where the list is not replaced (no line offered, the filter disabled,
  an unadmitted shape, no memory) a message grant meets section 4's rows
  for a routine not found: mode 0 polls, and modes 1 and 3 fail the
  start.
- **After a fresh install or an update** on Vista and 7 x86 the INF asks
  for a restart (`Reboot`; owner, 2026-10-09): the helper is boot-start
  and active from the next boot (record 18 section 5).

**Why it is safe on the down-level targets.** Windows 98, ME, 2000 and XP
(and XP x64) never grant a message: their PnP managers do not read the
key, and no message descriptor reaches the parser. Every step below is
taken only when `IoIsWdmVersionAvailable(6, 0)` is TRUE, the test the
driver already uses for NT 6.x's other paths, and the x86 image's one new
import, `IoGetDeviceObjectPointer`, is a Windows 2000 export with stock
Windows 98 SE precedent (`1394bus.sys`), called on NT 6 only. A message
descriptor that reaches the x86 image with no helper answering takes
section 4's "message not connected" row and polls under mode 0, and fails
the start under modes 1 and 3. Mode 3 on those systems fails the start
before anything is connected (section 4, the first table's mode-3 rows):
the decision reads `IoIsWdmVersionAvailable(6, 0)`, so a message is never
looked for there.

**The INF.** On the `Xhci.Dev6.NTamd64` path of `src/xhci98-amd64.inf`
(Vista and 7 x64), and on the `Xhci.Dev6.NTx86` path of `src/xhci98.inf`
together with the helper's boot-start service and `Reboot` (design record
18 section 7), in the hardware key:

```
[Xhci.Dev6.NTamd64.HW]
AddReg = Xhci.Msi.AddReg

[Xhci.Msi.AddReg]
HKR,"Interrupt Management",,0x00000010
HKR,"Interrupt Management\MessageSignaledInterruptProperties",,0x00000010
HKR,"Interrupt Management\MessageSignaledInterruptProperties",MSISupported,0x00010003,1
HKR,"Interrupt Management\MessageSignaledInterruptProperties",MessageNumberLimit,0x00010003,1
```

`FLG_ADDREG_NOCLOBBER` keeps a user's `MSISupported` 0 across an update,
under 34.1's rule; it also keeps a larger `MessageNumberLimit` a user set,
which is why the driver, not the INF, is what holds the delivery to one
message (below). The XP x64 path (`Xhci.Dev.NTamd64`) and the 98, ME,
2000 and XP paths get no `.HW` section, and the gate refuses these lines
anywhere but the NT 6 controller sections (`MSI-PLACE`, `MSI-VALUES`, and
on the 32-bit file `MSIHLP-HW`, which refuses them without the helper;
with self-test mutations), and models the `.HW` writes as hardware-key
rows in the footprint.

**Going back to an older package.** `MSISupported` 1 stays in the hardware
key when a package older than 2.3.0.0 is installed over this one, and an
older HCD does not know a message resource. Uninstalling the device with
its driver software, or setting `MSISupported` 0 first, is the way back
(design record 18 section 6; the release notes).

**The resources.** `hcdParseResources` already keeps the first raw
interrupt descriptor and its message flag (36.3). For a message grant it
now walks the whole raw list and records how many message descriptors
there are and the message count the first carries (WDK 7.1's
`u.MessageInterrupt.Raw`: a reserved `USHORT`, then `MessageCount`, read
as the upper half of the interrupt member's `Level` word, which that
layout overlays - the Windows 2000 DDK's headers lack the message
member); the translated list's interrupt member is used as before.

**Finding `IoConnectInterruptEx`.** The amd64 image imports
`MmGetSystemRoutineAddress` and asks it for `IoConnectInterruptEx` and
`IoDisconnectInterruptEx`; the x86 image asks the helper (above).
**The seam:** both architectures hand the pair to one function,
`hcdMsgConnect(hc, connect, disconnect)`, which makes the connection
below and keeps `disconnect` for the release; the amd64 caller passes
what `MmGetSystemRoutineAddress` returned, the x86 caller what the
helper's channel returned, and a NULL in either is "not found". Nothing
else in the delivery path knows where the pair came from. On amd64: Every amd64 target exports it (NT 5.2 and
later; XP x64 answers NULL for both names, so it keeps the line), so the
import gate takes an `amd64` row for it with that evidence. When either
is not found, a line resource connects through `IoConnectInterrupt` as
today, and a message resource polls with "message not connected". WDK
7.1's `iointex.lib` (`WdmlibIoConnectInterruptEx`) was considered and not
taken: `MmGetSystemRoutineAddress` is the documented call it wraps, with
no further third-party object in the image.

**The connection.** Under a message grant, `IoConnectInterruptEx` with
`Version = CONNECT_MESSAGE_BASED` (3): the PDO; the message ISR; the
controller as context; `SpinLock` NULL, so each message's interrupt
object has its own lock, and `SynchronizeIrql` 0, so each runs at its own
message's IRQL - safe because
the message ISR declines every message ID but 0 before it touches the
controller, and nothing synchronizes with a message's ISR (as built: a
driver lock would need the unified IRQL, known only after the
connection);
`FloatingSave` FALSE; and `FallBackServiceRoutine` the line ISR, for a
grant Windows turned back into a line. On success `Version` says which was
connected: `CONNECT_MESSAGE_BASED` is a message delivery, with
`ConnectionContext.InterruptMessageTable` kept in `hc` for the disconnect;
`CONNECT_LINE_BASED` (2) is a line delivery with the reason "automatic",
`ConnectionContext.InterruptObject` kept, and the line rules of sections 4
and 4.2 apply, watcher included. A failure status polls with "message not
connected", and nothing is kept. The structures
(`IO_CONNECT_INTERRUPT_PARAMETERS`, `IO_DISCONNECT_INTERRUPT_PARAMETERS`,
`IO_INTERRUPT_MESSAGE_INFO`, `IO_INTERRUPT_MESSAGE_INFO_ENTRY`) are
declared from WDK 7.1's `wdm.h` with compile-time size and offset checks
for both architectures, since the Windows 2000 DDK has none of them. The
release disconnects with `IoDisconnectInterruptEx` - the `Version` and the
`ConnectionContext` the connection returned - in place of
`IoDisconnectInterrupt`, wherever the line's disconnect runs today: the
stop, and every later step of the start that fails and runs the release.

**One message.** `CONNECT_MESSAGE_BASED` connects every message Windows
assigned the PDO and takes no count, so the driver cannot ask for the
first alone. It serves the grant as one instead: the controller enables
only Interrupter 0, and an Interrupter asserts only if it is enabled
(4.17, p.262); under MSI its vector is its index modulo the enabled
message count, and under MSI-X its index names its table entry (4.17,
pp.262-263), so only message 0 is ever written; only message ID 0 runs the
ISR body, which reads Interrupter 0's state, and any other ID is declined
before it touches the controller (`hcdMsgIsr`, since 7212604), so no two
bodies run under different messages' interrupt locks; and the record keeps the
`MessageCount` the connection returned. A grant of more than one message is
therefore recorded, not refused. More than one Interrupter stays out.

**MSI or MSI-X.** The message resource does not say which capability
Windows enabled. After the connection the start walks the controller's PCI
capability list (bounded at 48 entries, each pointer checked to lie in
40h-FCh and be DWORD-aligned) for MSI (ID 05h, Message Control bit 0, MSI
Enable) and MSI-X (ID 11h, Message Control bit 15, MSI-X Enable), and
records which is enabled: MSI-X when its bit is set, else MSI when its bit
is set, else "message, capability not read" - the delivery is still the
message, and the record says the read did not settle which. The walk is a
pure function over the configuration bytes, host-tested (`test_msi`).

**The ISR.** `hcdMsgIsr(Interrupt, Context, MessageId)` does what the line
ISR does - `XhciIsr`: EINT then IP acknowledged and `IE` written 0, on a
claim - and returns `XhciIsr`'s answer, queueing the drain only on a claim
(review of revision 5, round 1). Windows permits shared message
interrupts and may call a message ISR for an interrupt this controller did
not raise, so its contract is the line ISR's: FALSE for an interrupt not
handled. `XhciIsr` claims on `USBSTS.EINT`, which the xHC sets on each
`IP` 0-to-1 transition whatever the signalling (5.4.2); under a message
the xHC has already cleared `IP` (4.17.5), so the acknowledgement of `IP`
writes 1 to an RW1C bit already 0. The drain re-arms `IE` as under a line;
the moderation interval applies unchanged. (Roadmap 36.15's "an MSI is
never shared" and issue 12 section 5.3's "always its own" are corrected
with this revision.)

**What follows from a message delivery:**
- The dead-line rule does not apply (section 4.1: `IP` is cleared by the
  message write itself); the poller stays CLOSED.
- The routing quirk writes nothing (36.3; `hc->IntDelivery` is message).
- The core's interrupt-bit gate treats a message as an interrupt resource,
  and its pin-0 refusal is skipped: a controller with pin 0 that Windows
  gave a message is served by it.

**What is recorded** (section 8): the delivery (MSI, MSI-X, line or poll)
and its reason in the counter block and the start's log note; how the
routines were found; the message descriptors and the `MessageCount` granted; the
capability read. `XHCISNAP` names each (36.9).

**Legs** are 36.16's, on Vista and 7 x64, and on x86 with the helper: QEMU's `qemu-xhci` with `msi=on,msix=off`,
`msi=off,msix=on` and the launchers' default, under modes 0 and 3, read
as MSI, MSI-X and whatever Windows granted; `MSISupported` 0 read as the
line; a
`qemu`-flavour failed connection read as polling with its reason; a
`MessageNumberLimit` raised by hand, read as a grant of several served as
one; mode 1 with `MSISupported` 1 read as the line, x86 and x64, once the
requirements filter is enabled (record 18 leg F); and the down-level
targets, and Vista and 7 x86 without the helper, reading as the release
before it (the filter's line), with record 18's legs H, L, U, D and R.
Revision 7's legs (task 36.53): mode 3 read as the message (the filter's
outcome "replaced: the message kept"), mode 1 read as the line, and a
failed start (Code 10) with its reason wherever the named delivery cannot
be had - mode 3 on 7 x86 without the helper, on XP and on 98 SE; mode 1
where no line is offered.

## 5. The poller's lifecycle

### 5.1 The objects

- `PollTimer` and `PollTimerDpc`: a `KTIMER` and the `KDPC` given only to
  `KeSetTimer`. The timer's DPC drains (or, in the watch role, looks),
  decides the next deadline and re-arms.
- `IsrDpc`, which no ISR queues under a poll delivery, is the DPC the
  opportunistic checks queue explicitly, with `DpcsInFlight` counted before
  the queue as `hcdIsr` does. One `KDPC` is never handed to both the timer
  and `KeInsertQueueDpc` (Microsoft's `CustomTimerDpc` guidance).
- `PollLock`, a spin lock, guarding the poller's state (section 5.2), its
  role, its deadline `PollDue`, and `PollOwed`, the earliest deadline asked
  for while the timer's DPC owned the poller.
- `PollService`, in the core's extension and written only under the
  controller lock: 1 while the controller is running and its event ring may
  be drained or watched by the poller, 0 otherwise (section 5.3).
- The pure state of all of this - the state, the role, the deadlines, the
  watcher's count and the counters of section 8 - is one structure in
  `xhci_poll.h` (`XHCI_POLL_STATE`) driven by pure functions in
  `xhci_poll.c`, so `test_poll` runs every transition below with the kernel
  calls stubbed; `hcd_poll.c` holds the timer, the DPC and the locks and
  calls them.

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
| FIRING | the DPC | the DPC has taken ownership and is draining, looking or deciding |
| CLOSING | the DPC | as FIRING, but close has been asked; the DPC ends it |
| CLOSED | nobody | closed; nothing touches the poller until a start reopens it |

**The role** (2026-10-09; review of `6cac234`) is not a state. The
states say who owns the timer; the role, `PollRole` under `PollLock`, says
what the DPC does while it owns it:

| Role | Opened by | The DPC | Period |
|---|---|---|---|
| WATCH | a start whose effective mode is 0 under a line delivery | looks (section 4.2), never drains | `XHCI_POLL_WATCH_MS` |
| SERVE | a start under a poll delivery, or the move of section 4.2 | drains (section 6) | section 6's |

The role is set when the poller is opened and changed only by its owner,
the DPC, from WATCH to SERVE at the move; nothing changes it back. Every
rule of this section and of section 5.3 applies to both roles unchanged -
the opening, the ownership, the promotion's arm-taking, the park and kick,
the close - except that promotions (section 6.2) and the opportunistic
checks (section 7.1) run under a poll delivery only, so a watcher is never
promoted and never peeked for.

- **Initial state, and reopening.** The controller object outlives a stop:
  its synchronisation objects are initialised once
  (`HcdControllerInitObjects`), every start reopens the timer service and
  the DPC admission (`HcdTimersOpen`, `DpcClosed` 0), and the start's
  zeroing of the core's extension does not reach fields held in `hc`. So
  `PollTimer`, `PollTimerDpc` and `PollLock` are initialised there with the
  other objects, and the state starts CLOSED. A start whose delivery is
  poll, or whose effective mode is 0 under a line delivery, **opens** the
  poller beside `HcdTimersOpen`: under `PollLock`, CLOSED becomes PARKED
  with the start's role, the watcher's count is cleared, and `PollDue` and
  `PollOwed` are marked invalid. It is the only way out of CLOSED, and it
  is taken only after the previous release has seen CLOSED (the close
  below), so a disable and enable, or a start that failed and is retried,
  begins from a drained poller. The kick at the end of a successful start
  (section 5.3) then arms it. A start in mode 1, a start under a message
  delivery, and a start that fails before it opens it, leave it CLOSED. A
  failed start runs the same release as a stop, which closes it from
  whatever state it reached.
- **The DPC takes ownership** as it starts: under `PollLock`, ARMED becomes
  FIRING (CLOSING stays CLOSING), and `PollOwed` is cleared. A DPC that
  finds CLOSED or PARKED - which no arm can leave behind, since only the
  owner or a party holding the arm back sets the timer - does nothing.
- **The DPC's pass.** In CLOSING it does nothing more and ends. Otherwise,
  under the controller lock, it reads the admission of section 5.3; not
  admitted, it decides to park (section 5.3) and ends. **The DPC keeps
  ownership until its epilogue** (rounds 3 to 5): the decision, made
  under the controller lock and then `PollLock`, only marks the pass
  `ParkPending`; the state stays FIRING (or CLOSING, if a close came
  meanwhile). After **both** locks are released the DPC takes `PollLock`
  alone once more for its epilogue, which is the only place it gives
  ownership up: CLOSING becomes CLOSED; a FIRING pass marked
  `ParkPending` becomes ARMED at the role's first period if a kick came
  while it was pending (`KickOwed`), and PARKED otherwise; any other
  FIRING pass arms as below. It then releases `PollLock` and returns, so
  no state another party can act on - PARKED, CLOSED - is published
  before the DPC's last touch of the controller or the poller, and the
  release's wait for CLOSED and its `PollLock` handshake cover the whole
  pass. Admitted: in the watch role it
  looks (section 4.2) and, if the line is proven dead, makes the move and
  continues as a server in the same pass; in the service role it releases
  the controller lock and drains with `XhciEventDpc(ext, FALSE)`, wakes the
  thread on a port event as `hcdIsrDpc` does, and takes section 6's
  decision under the controller lock again.
- **The DPC gives it up** as it ends, under `PollLock`: CLOSING becomes
  CLOSED; otherwise it arms and the state becomes ARMED. **The arm is the
  role's** (review of revision 5, round 1): a watcher arms
  `XHCI_POLL_WATCH_MS` exactly, with no deadline, no `PollOwed` and no
  conversion, since nothing promotes a watcher and the registry periods do
  not govern it; a server arms at the earlier of its own decision and
  `PollOwed`, converted as section 6.2 says.
- **Only the owner, or a party that has taken the arm back, calls
  `KeSetTimer`.** A promotion (section 6.2) in ARMED takes the arm back with
  `KeCancelTimer`: TRUE means the timer was still queued and no DPC will
  run, so the promotion may set the new deadline; FALSE means the timer has
  expired and its DPC is queued or running and is about to take ownership,
  so the promotion records its deadline in `PollOwed` instead. In FIRING it
  records in `PollOwed`. No external `KeSetTimer` ever runs while a DPC owns
  the poller, so there is never a second callback.
- **Close** (release, at PASSIVE_LEVEL, first - **before** the interrupt
  is disconnected, so the move's `KeSynchronizeExecution` never meets a
  disconnected interrupt object (section 4.2); an ISR that runs between
  the close and the disconnect queues `IsrDpc` through its own admission,
  which the `DpcsInFlight` wait still covers), under `PollLock`: PARKED becomes CLOSED; ARMED with
  `KeCancelTimer` TRUE becomes CLOSED; ARMED with FALSE, and FIRING, become
  CLOSING, and the DPC finishes the close. Release then waits for CLOSED,
  in 1 ms delays as it waits `DpcsInFlight`, acquires and releases
  `PollLock` once more so the DPC's last release of it has happened, and
  only then goes on to `DpcClosed`, the `DpcsInFlight` wait and the timer
  and DMA drains. What the DPC does after that last release is return -
  the same residual the `DpcsInFlight` decrement already accepts. The
  watcher closes the same way: a close against a watcher's expiry is the
  ARMED-with-FALSE row.
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
(`XhciInitController`, then `xhciRunController`), and `RUNNING` before the
run bit is written, so neither proves the controller runs.

- **Set** by the HCD alone, once everything the poller's admission reads
  is published, in one controller-lock hold that also kicks (review
  of revision 5, round 1: `hcd_power.c` calls `XhciResumeController`
  before it publishes `Common.DevicePower = PowerDeviceD0`, so a kick from
  inside the core's resume could arm a DPC that runs first, fails the D0
  test and parks for good; and the start's own D0 is published by
  `hcdStart` in `hcd_pnp.c` only after `hcdStartBody` returns). **Every
  grant is made with the power gate held**, so no suspend or resume on
  another processor can publish its own state between the grant's checks
  and its kick (round 2). The sites, each through `HcdPollGrant(hc)`:
  - **the start:** in `hcdStart`, after `HcdStartController` succeeded
    and `PnpState` and `DevicePower = PowerDeviceD0` are published, before
    the root hub's PDO is created, with the power gate taken around it;
  - **a return to D0:** in `hcdD0Finish` after `DevicePower` is published
    and before `HcdPowerGateLeave`, and in `hcdDirectTransitionGated`'s
    resume after `SuspendedInD0` is cleared, inside the gate its caller
    holds - each only when the resume succeeded and was not contained
    (`hcdResumeContained`), a failed one having latched
    `ControllerFailed`, which the grant's own check refuses anyway;
  - **the in-place recovery:** in `hcdRecover`, on the final successful
    `ok` - after the Bus Master Enable restore and its second
    `XhciRecoverController`, when that ran - and before
    `HcdPowerGateLeave`.

  `HcdPollGrant` sets `PollService` only if the poller's whole admission
  holds (`INITIALIZED` set, `ControllerFailed` and `SUSPENDED` clear, D0
  published, `hcdDrainAdmitted`), read in the same controller-lock hold,
  advances `PollServiceGen`, and kicks. The core sets nothing.
- **Cleared** under the controller lock before anything quiesces, resets,
  suspends or stops the controller: wherever `INITIALIZED` is cleared, in
  the same hold, with `PollServiceGen` advanced -
  `XhciControllerUpdateFlags` when it clears `INITIALIZED`, and the one
  site that clears it directly (`xhci_cmd.c`'s quiesce,
  `XhciControllerBeginQuiesce`) - which covers the start's and the
  recovery's `XhciInitController` entry, the quiesce of a suspend and of
  the recovery, a stop and a failed recovery. The core's clear needs no
  HCD call: the poller sees it at its next admission, and the generation
  keeps the watcher's looks apart (section 4.2). **The HCD revokes
  earlier**, through `HcdPollRevoke` (the same two writes under the
  controller lock), at the start of each lifecycle containment - a
  suspend, a recovery, the stop - so the watcher cannot move behind the
  containment's check (section 4.2); the core's clear that follows is
  then a defensive repeat.
- **The admission** of the poller's DPC and of a peek is `PollService` 1,
  `ControllerFailed` 0 and the drain admission the backstop uses
  (`hcdDrainAdmitted`: the window decoded, D0, the DPC open, the event ring
  present), read in one controller-lock hold. A failed controller is not
  served: a DPC that runs while the failure stands parks, and the recovery
  that clears the failure kicks it. A failure recovered before any DPC
  reads it under the controller lock leaves nothing parked and nothing to
  kick (section 11).
- **Checked inside the drain's own locked admission**, under a poll
  delivery only: `XhciEventDpc` refuses a drain while `PollService` is 0,
  under the same controller-lock hold in which it would drain, not in a
  wrapper that drops the lock first. Under a line delivery the drain's
  admission is unchanged.

**Park and kick are serialised by the controller lock.**

- The timer DPC, finding itself not admitted under the controller lock,
  takes `PollLock` inside that hold and marks the pass `ParkPending`,
  clearing the watcher's count; it drains and looks at nothing, and its
  epilogue (section 5.2) publishes PARKED - or CLOSED, or ARMED for a kick
  owed - once both locks are released.
- A revocation cancels no timer: an arm made before a suspend or a
  recovery still expires, and its DPC finds itself not admitted and
  parks, as above.
- `HcdPollGrant`, in the same controller-lock hold that sets
  `PollService`, takes `PollLock` and **kicks**: PARKED becomes ARMED at
  the role's first period (the active period for a server, the watch
  period for a watcher). In FIRING with `ParkPending` the kick is recorded
  as `KickOwed` and arms nothing - the DPC still owns the timer, and its
  epilogue arms. In ARMED, or FIRING without `ParkPending`, it does
  nothing, since the owner reads `PollService` at its next admission, or
  read it as 1 already. In CLOSING or CLOSED it does nothing.

Both decisions are taken inside one hold of the controller lock, so either
the DPC decides to park first and the kick finds `ParkPending` and leaves
`KickOwed` for the epilogue, or the kick sets `PollService` first and the
DPC does not park. A restart cannot be lost, and no arm is made while the
DPC owns the timer.
`KeSetTimer` is callable at DISPATCH_LEVEL, so the recovery path can kick.
The timer service's own close (`TimersClosed`) is not used for any of
this: the controller's initialisation needs the timer service during
recovery.

**Host vectors** (`test_poll`, beside section 11's list): a watcher's close
against its own expiry (ARMED with `KeCancelTimer` FALSE, then the DPC
finishing CLOSING); a watcher across the in-place recovery (cleared
`PollService`, the park, the count cleared, the kick re-arming at the watch
period) and across D3 and back; a recovery, and a suspend and resume,
completed wholly between two looks (the generation changed, the count
started afresh); a timer armed before a suspend that expires during the
resume, between `INITIALIZED` and the D0 publication (not admitted,
parked; the grant after the publication kicks it back), and the start's
own window, where no grant has been made and nothing is armed; a park
against a close asked after the DPC took ownership (CLOSING kept through
the park, CLOSED only in the `PollLock`-only epilogue, nothing armed); a
close and a grant each arriving after the parking decision and before
the epilogue (CLOSED; ARMED by `KickOwed`, never armed twice); a watcher
whose move would come between a suspend's containment check and its
quiesce (the revocation first: no move, nothing owed missed); a
containment after a recovery's HCRST timed out (HCRST or CNR read set:
no MMIO written, the configuration step taken) and after a failed
re-decode (`HcInfoStatus` bad: the same); a retirement whose `IP` step
fails followed at once by a suspend and by a recovery (the ISR still
claiming through it, the HCD's attempt before each, and the HCRST's
proof); a
move made in the same pass as a close asked (the move recorded, CLOSING
ended, nothing re-armed); the watcher's arm at its own period under the
registry's extremes (idle 1, active 1; idle 5000, active 5000); and a kick
in each state.

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

**The inputs**, gathered by the service DPC in the controller-lock hold
that follows its drain, so each is the drain's own state (`hcd_poll.c`,
then handed to the pure `XhciPollDecidePeriod`):

- **A command:** `CommandState` not `XHCI_CMD_STATE_IDLE`.
- **TDs outstanding:** a walk of `hc->SlotDevice[1..MaxSlots]`. For each
  device, its EP0 queue (the thread's and the URBs' control transfers);
  each open pipe in `Pipes[]` with records on its queue, by its transfer
  type - control, bulk or isochronous is ACTIVE, interrupt contributes its
  service interval; and for an endpoint with streams open, each live
  stream's pipe the same way. The hub status transfer contributes nothing
  shorter than the idle period. The walk reads pointers and counts that
  the event path and the submissions write under the same lock; it is
  bounded by `MaxSlots` x 31 pipes and stops at the first ACTIVE input.
- **A port in reset or enumeration:** `PollPortsBusy`, a count the
  controller thread publishes under the controller lock (review of
  revision 5, round 1: the port machines and the hub records are the
  thread's, written without the lock, so the DPC cannot scan them). The
  thread recounts it at the end of each enumeration service pass (the
  root and hub port objects whose machine is between
  `XHCI_ENUM_DEBOUNCE` and `XHCI_ENUM_CONFIG_FULL`, or in
  `XHCI_ENUM_BOS5` or `XHCI_ENUM_BOS_FULL`) and writes it in one short
  hold; and before it starts a port reset it promotes the poller to the
  active period (section 6.2's need, with no doorbell), since a reset's
  end is a Port Status Change Event with no doorbell before it. The count
  is as of the thread's last pass; every enumeration step after that
  rings a doorbell or starts a reset, each of which promotes, so the
  staleness costs no step.
- **An event drained within the last idle period:** `EventsTotal`
  compared with the value the previous pass saw; a change stamps the time
  (the low word of `KeQuerySystemTime`), and the clause holds while less
  than the idle period has passed since the stamp.
- **A drain stopped at its bound:** `DrainBoundHits` moved during this
  pass's drain and `XhciEventRingPending` still reads an event.

An interrupt endpoint's service interval is its Endpoint Context
`Interval` as programmed (`pipe->Ep.Interval`, after any fast polling or
interval cap): `2^Interval` x 125 us (Table 6-12), in whole milliseconds
rounded down; below 1 ms it is the active period.

### 6.2 Promotion

A submission whose need would bring the next poll earlier brings it
earlier, and never later. Deadlines, not periods, are compared.

- **Where.** Every doorbell this driver rings goes through
  `XhciWriteDoorbell` (`xhci_pci.c`), and every caller holds the
  controller lock: the URB path (`hcd_io.c`), the thread's EP0 and hub
  transfers (`hcd_enum.c`, `hcd_hub.c`), the endpoint restarts
  (`hcd_cfg.c`), the isochronous underrun (`hcd_dev.c`), the `qemu`
  flavour's injection (`hcd_inj.c`) and the command submit
  (`xhci_cmd.c`). After the write that function calls one HCD service,
  `HcdSvcDoorbell(ext, slot, value)`; under a poll delivery it promotes
  and then peeks (section 7.1), under any other it returns at once. Its
  IRQL contract becomes its callers' own: DISPATCH_LEVEL, controller lock
  held.
- **The need.** Doorbell 0 (a command), a control endpoint (DCI 1), or a
  bulk or isochronous endpoint needs the active period; an interrupt
  endpoint's needs its service interval (section 6.1), found from
  `hc->SlotDevice[slot]` and the DCI in the doorbell value. A slot or DCI
  the lookup cannot place needs the active period, which costs at most one
  early poll. Its deadline is now plus
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
- **A finer timer** (`ExSetTimerResolution` on NT) was not part of this
  design: it has no allowlist row, no Windows 98 export evidence, and changes
  the tick for the whole system. If the readings show polled throughput
  unusable, it is the next question, under the import gate's evidence rule.
  **Answered by revision 8, section 7.2** (task 38.4): on Windows 2000 and
  later, while the poller is active, through the helper on 32-bit systems
  and `MmGetSystemRoutineAddress` on 64-bit ones; never on Windows 98 SE
  or ME.

### 7.1 Opportunistic checks

Whenever the driver is entered anyway, it looks at the event ring for an
event not yet drained.

- **Under the controller lock, never without it.** The peek is
  `XhciEventRingPending` taken under the controller lock and only while
  `PollService` is 1 (section 5.3), so it reads a consistent `Dequeue` and
  cycle state of a ring that exists and a controller that runs. A peek without the lock was rejected: the
  dequeue index can be seen unwrapped (`xhci_ring.c:1373`), and the ring's
  lifetime across a reset is not otherwise guaranteed. Task 35-T.1's
  lost-interrupt backstop (record 17 section 4.1, `hcd_ctl.c`
  `hcdBackstop`) already takes this peek, under the same lock, on each
  controller-thread pass (since 36.55, on each tick of the tolerance
  clock's DPC instead); these checks reuse the helper with their own
  staleness rule, which is none: a hit queues the drain at once, since
  under a poll delivery nothing else will come for it before the next
  poll. A hit while `IsrDpc` is already queued costs nothing
  (`KeInsertQueueDpc` answers FALSE and the count is undone).
- **Where**: after every doorbell, in `HcdSvcDoorbell` (section 6.2), with
  the controller lock still held for it - which covers the URB submission
  path, a completion routine's resubmission inside the drain's deferred
  work, the thread's own transfers and the command submit; at the end of
  each drain `IsrDpc` runs, in its own controller-lock hold after
  `XhciEventDpc` returns (the service DPC needs none: its decision sees
  the bounded clause); on each controller-thread pass (`hcdPoll`; the
  backstop that stood beside it moved to the tolerance clock's DPC in
  36.55); and in the frame timer's DPC (`hcdFrameDpc`, in the hold
  it already takes).
- **On a hit**: queue `IsrDpc` under the admission rule (section 5.2). Never
  drain inline: a drain completes IRPs, a class driver's completion routine
  resubmits, the submission would peek again, and the recursion would run
  down a kernel stack - Windows 98's is small - with no bound.
- **On a submission**: promote (section 6.2).

The checks run under a poll delivery only, so a line delivery runs exactly
today's code.

### 7.2 A finer system timer while polling (revision 8, task 38.4)

Designed 2026-10-13 for roadmap-hcd task 38.4, before any code; the
helper half (how the 32-bit image reaches the routine, the helper's
extension to Windows 2000 and XP and its rename to `xhcihlpr.sys`) is
design record 18 section 15. The owner's answers of 2026-10-13 are
written in (section 7.2.13), as revised by the owner after review round
1 (section 13.6).

#### 7.2.1 What is asked, and what is not

Asked (roadmap 38.4): under a poll delivery, ask Windows for a finer
system clock while the poller is in its active state, and give it back
when the poller leaves it, the delivery leaves polling, the controller
stops or leaves D0, and at S3, S4 and S5; at most one request per
controller, every `TRUE` paired with its `FALSE`; the call made at
PASSIVE_LEVEL from a thread, never from the poll DPC; a registry value
that turns it off, on by default (owner, 2026-10-12); and no power IRP
whose completion depends, directly or indirectly, on the call completing.

Not asked: Windows 98 SE and ME. Their kernel has no `ExSetTimerResolution`
(the roadmap's "no Windows 98 export"), the 32-bit image may not import
it or `MmGetSystemRoutineAddress`, and no helper loads there; on them the
poller keeps the clock tick, as today, and `XhciPollFineTimer` is read
and has no effect. Nothing changes under a line or message delivery,
under the watch role, or on a controller that never polls. The tick on
Windows 98 is still unmeasured (section 7). `XhciPollIdleMs` keeps its
default of 50 (owner, 2026-10-13); section 7.2.6a is the measurement
that will decide whether it changes.

#### 7.2.2 State today (read 2026-10-13)

- **What the tick does to polling** (`runs/run-36.md`, task 36.13's P1
  rows, mode 2, `release`; runtime, through this driver's counters):
  `PollIntervalMinMs` 10 on Windows 2000, 15 on XP, XP x64 and 7 x86, 5
  on Vista x86 and 1 on Vista x64, against an active period of 1 ms.
  That the Vista guests' smaller minima came from other software on the
  guest having raised the system's resolution is an inference, not a
  reading: the counters observe poll intervals and say nothing of who set
  the clock. It is why section 7 calls the tick something "other software
  can change", and why the legs below compare interval observations of
  their own phases, never the kernel's increment (`KeQueryTimeIncrement`
  is the increment, not the current resolution; roadmap 38.4).
- **Where the period is decided.** The poll timer's DPC
  (`hcdPollDpc`, `src/hcd_poll.c`) drains, gathers the bus's state
  (`hcdPollBus`) and takes `XhciPollDecidePeriod`'s state (ACTIVE,
  PERIODIC or IDLE; `XHCI_POLL_BUS_*`, `src/xhci_poll.h`) under the
  controller lock, at DISPATCH_LEVEL. A promotion (`HcdPollPromoteLocked`)
  arms the active period from a doorbell, also at DISPATCH_LEVEL.
- **The other paths that look at the ring under a poll delivery**
  (section 7.1). Two peek and queue the drain on any hit: the controller
  thread's pass (`HcdPollThreadPass`, about every `HCD_POLL_MS`, 100 ms,
  or sooner when woken) and the frame timer's DPC (`hcdFrameDpc`,
  `HCD_FRAME_SAMPLE_MS`, 500 ms). The tolerance clock's DPC (`hcdTolDpc`,
  `XHCI_TOL_TICK_MS`, 100 ms) is not a peek of that kind: its backstop,
  `XhciTolBackstop` (`src/xhci_tol.c`), does nothing at `XhciTolerance`
  0, returns nothing on its first sight of a pending event, and asks for a
  drain only once the same index, cycle, drain generation and start
  generation have stayed stale for `XHCI_TOL_BACKSTOP_TICKS`
  (`XHCI_TOL_TICKS(100)`, four ticks; `src/xhci_tol.h`) - a stale-event
  detector, not a scheduler. So besides the poll timer, an event is
  bounded by the next controller-thread pass or frame-timer DPC, and the
  poll timer's period is not the driver's whole idle activity.
- **The controller thread** (`hcdThread`, `src/hcd_ctl.c`) is created by
  `hcdThreadStart` inside `hcdStartBody` before `ControllerStarted` is
  set and before the PnP path's grant (`hcdStart`, `src/hcd_pnp.c`), and
  joined by `hcdThreadStop` first in `hcdStopBody`. On a reference
  failure `hcdThreadStart` falls back to an event wait whose window is
  documented as not closed (`hcd_ctl.c` ~2399); the completion workers
  keep the handle and retry the reference at the join instead
  (`hcdSuspDoneCreate`, `src/hcd_susp.c` ~649).
- **The controller thread is on the completion path of power IRPs.**
  With `XhciSelectiveSuspend` 1 a child PDO's D-IRP is pended into its
  slot by dispatch and settled by the thread's pass (`src/hcd_susp.c`,
  the executor; `src/hcd.h`, `PwrIrp` and `PowerPending`), and the
  controller FDO's own device SET_POWER waits on the power gate the
  thread's pass holds (`HcdControllerPower`, `src/hcd_power.c`).
- **The stop's order** (`hcdStopBody`): selective suspend's epoch marked
  down (`HcdSuspControllerDownLocked`, after which a new child Dx is
  completed at admission), the controller thread joined, the poller
  revoked, closed and contained under the power gate, the controller
  stopped, the devices dropped (`HcdEnumDrop`, which settles each device
  slot's pended D-IRP at its unlink), the idle-callback hand-off stopped
  (`HcdSuspCbStop`), the completion workers drained (`HcdSuspDoneDrain`),
  then the release (`hcdRelease`). Surprise removal and the remove reach
  it through `HcdStopController`, which holds the door gate; no power path
  takes the door gate.
- **The power paths.** A device SET_POWER to a lower state revokes the
  poller's service (`HcdPollRevoke`) under the power gate before
  `hcdSuspendSettled`; the fallback when no device IRP could be asked
  (`hcdDirectTransitionGated`) does the same; a D0 grants it again
  (`HcdPollGrant` in `hcdD0Finish`). The system IRP asks for D3 for every
  sleep state and for shutdown (`hcdSystemDone`).
- **Neither `HcdPollGrant` nor `hcdPollMove` wakes any thread today.**

#### 7.2.3 The routine, per image

| Image and system | Where `ExSetTimerResolution` comes from | Without it |
|---|---|---|
| 32-bit, Windows 98 SE and ME | nowhere: no look, no thread | the clock tick, as today |
| 32-bit, Windows 2000 and XP | `xhcihlpr.sys`'s channel, its timer service (record 18 section 15.3): the address the loader wrote into the helper's import table | the clock tick, recorded "not found" |
| 32-bit, Vista and 7 | the same channel, the same answer that carries the MSI routines | the same |
| 64-bit, XP x64, Vista x64, 7 x64 | `MmGetSystemRoutineAddress(L"ExSetTimerResolution")`, as for the MSI pair (section 4.3) | the same |

The routine is called only through a pointer the image holds; neither
image imports it. The 32-bit image keeps the import set it has
(`IoGetDeviceObjectPointer` is already in it, record 18 section 3.4);
the 64-bit image's `MmGetSystemRoutineAddress` row widens its `SITES` to
the new object (section 7.2.11). The typedef is the Windows 2000 DDK's
declaration, `ULONG ExSetTimerResolution(ULONG DesiredTime, BOOLEAN
SetResolution)`, `NTAPI`; the 100 ns unit and the returned value (the
resolution in effect after the call) are the documented contract.

#### 7.2.4 The switch

`XhciPollFineTimer` (owner, 2026-10-13, decision T4), a `REG_DWORD` in
the controller's driver key, read at each start with section 3's values
and by the same strict reader: 1, absent, unreadable or not a four-byte
DWORD is on; 0 is off; any other value runs as 1 and the value read is
recorded. Both INFs write it on every install path with
`FLG_ADDREG_NOCLOBBER`, value 1, beside `XhciPollActiveMs`, so it is where
a user looks and an update keeps a 0 a user set (task 34.1's rule; the
INF gate's value-policy table, `check-inf.ps1` ~1323, and its footprint
gain the row). Windows 98 SE and ME read it and do nothing with it. A
change takes effect at the next start (a disable and enable), as the
other poll values do.

#### 7.2.5 Who calls: a timer thread of the controller's

**The call is made on a system thread of its own, one per controller,
never on the controller thread and never on a system work queue** (owner,
2026-10-13, decision T1: option (b); the roadmap's "from the controller
thread" reads "from a thread of the controller's").

- **Why not the controller thread.** It is on the completion path of
  power IRPs (section 7.2.2): with selective suspend on it settles child
  D-IRPs, and the FDO's own SET_POWER waits on the power gate its pass
  holds. A call that blocks there makes a power IRP's completion depend
  on the call.
- **Why not a work item.** `ExQueueWorkItem` (the only work-item routine
  the 32-bit image has; `IoQueueWorkItem` is not on Windows 98) does not
  keep `xhci98.sys` loaded while the item runs (record 18 section 14.8,
  round 7's finding), and a system worker blocked in the call is a worker
  the D0 completion's own deferral (`hcdPowerDefer`, `DelayedWorkQueue`)
  may need, an indirect dependency.
- **Why not the helper.** On 32-bit systems the helper could run the
  call on a thread of its own and never be unloaded, but the 64-bit image
  has no helper (owner, 2026-10-13: "the amd64 image asks
  `MmGetSystemRoutineAddress`"), so one executor in `xhci98.sys` serves
  both images, and the helper stays a resolver that hands out kernel
  addresses and runs nothing the HCD calls.

**The timer thread** (`hcdTmrThread`, in a new `src/hcd_tmr.c`; the
pure half in `src/xhci_poll.c`). Its block, `hc->Tmr`, lives in the
controller's extension, which outlives every stop.

- **Initialised once** with the controller's other objects
  (`HcdControllerInitObjects`): its event (a synchronization event), its
  values zero, no thread.
- **Reset at each start**, under the controller lock, in `hcdStartBody`
  before the poller is opened (`HcdPollOpen`), so before any producer of
  section 7.2.6 can run: `TmrStop` 0, `TmrBar` 0, `TmrActive` 0, `TmrGen`
  0, the state RELEASED, the routine NULL, the per-start look latch
  clear, the thread fields empty, the counters of section 7.2.9 zeroed
  with the block (`HcdCountersStart`).
- **Created by the start, after it has succeeded**: in `hcdStart`
  (`src/hcd_pnp.c`), after `HcdStartController` returned success, D0 was
  published and `HcdPollGrant` ran, at PASSIVE_LEVEL in the PnP manager's
  start thread - not by the controller thread, so its creation does not
  stand in that thread's pass and the "after a successful start" holds by
  construction. Only on NT (`IoIsWdmVersionAvailable(1, 0x10)`), only
  with the switch on, and only when the start's delivery is poll or its
  role WATCH (so a later move to polling finds it there); a start under a
  line in mode 1, or under a message, makes none. Before creating it the
  start takes the routine where it can without a look: on 64-bit,
  `MmGetSystemRoutineAddress`; on 32-bit Vista and 7, the timer pointer of
  the binding the filter and the start already made (record 18 section
  15.4), copied into `hc->Tmr` here, inside the PnP manager's
  serialisation of the filter and the start, so no later filter's look
  races the timer's read. On 32-bit Windows 2000 and XP the routine is
  left for the thread's own look (below). A routine already known to be
  absent (64-bit `NULL`; 32-bit Vista or 7 with no timer service
  accepted) makes no thread.
- **The creation, and its failure.** `PsCreateSystemThread`, then on NT
  the object referenced by handle; if the reference fails the handle is
  kept and the reference retried at the join, as `hcdSuspDoneCreate`
  does - never the controller thread's event fallback. A creation that
  fails is counted (`TmrThread` 2) and the start still succeeds: the
  controller polls at the tick, and no retry is made until the next
  start. Nothing waits for any call on this path.
- **Its first pass reconciles** without waiting: it reads the inputs at
  once, so a `TmrActive` already 1 when it starts is taken without a new
  ACTIVE edge.
- **The look on 32-bit Windows 2000 and XP** is the thread's own, at
  PASSIVE_LEVEL, at most once per start (the latch, `TmrLooked`, in
  `hc->Tmr` - the binding machine itself does not count looks per start),
  made the first time it would hold and has no routine: record 18
  section 15.4's look through the binding machine, whose filter and
  start never run on NT 5. Its result is copied into `hc->Tmr`;
  absent, the thread stays and never holds.
- **Stopped and joined by the stop, after every power IRP of the
  controller's children is settled** (round 1, finding 1): the stop sets
  `TmrStop` and `TmrBar` under the controller lock and signals the event
  first of all, before `hcdThreadStop`, so the thread gives a held
  request back at once and takes none; it joins the thread (its object,
  or the kept handle's retried reference) only after `HcdEnumDrop`,
  `HcdSuspCbStop` and `HcdSuspDoneDrain` - when every child D-IRP the
  controller held has been completed and every completion worker has
  left - and before `hcdRelease`, outside the power gate. A child Dx that
  arrives after the epoch was marked down is completed at admission
  (`HcdSuspControllerDownLocked`), so none can come to wait behind the
  join. `hc->Tmr` stays valid throughout (the extension).
- **Its whole world** is `hc->Tmr`: the event, the thread object or
  handle, the routine, the request and the values below. It takes the
  controller lock only to read its inputs and record what it did, never
  across the call, and holds no gate, no spin lock, no IRP and no count
  anyone waits on while it calls.

#### 7.2.6 The request and its state machine

**The value** (owner, 2026-10-13, decision T2): `XhciPollActiveMs` x
10000, in 100 ns units, the active period in effect - 10000 (1 ms) at the
default. Every `TRUE` is paired with a `FALSE`, whatever value the `TRUE`
returned (an already-fine clock included); the returned values are
recorded; the kernel's own minimum and rounding are its business.

**Held while the poller is ACTIVE, given back when it is not** (owner,
2026-10-13, decision T3: no hold of its own after the active state ends;
the idle period governs it). ACTIVE is `XhciPollDecidePeriod`'s state of
section 6.1: a command, a control, bulk or isochronous TD, a port in
reset or enumeration, an event drained within the last idle period, or a
bounded drain - so the request outlives the last event by up to
`XhciPollIdleMs` (50 ms at the default) and no constant of the timer's
adds to it. A PERIODIC decision is not ACTIVE and gives it back.

**The inputs and who signals.** All are written under the controller
lock; every write that can change "wanted" sets the timer thread's event
(`KeSetEvent`, callable at DISPATCH_LEVEL with the lock held):

- `TmrActive`: set to 1 by an ACTIVE decision in `hcdPollDpc` and by a
  promotion whose need is the active period (`HcdPollPromoteLocked`), set
  to 0 by a PERIODIC or IDLE decision; the event set when the value
  changes.
- **A boundary**: the DPC's park (`ParkPending`), `HcdPollRevoke`, and
  each bar (below) set `TmrActive` to 0 and advance `TmrGen`, and set the
  event, always.
- **A grant** (`HcdPollGrant`, every site: the start, D0, the direct
  resume, the recovery) clears `TmrBar` and sets the event, always (round
  1, finding 3). Because a boundary reset `TmrActive`, the first ACTIVE
  decision after the grant is a change and signals too.
- `hcdPollMove` sets the event (a move makes the role SERVE and the
  delivery poll).

**Wanted** is a pure function (`XhciPollTmrWant`, `test_poll`) over
values read in one controller-lock hold: the switch on, the routine
found, `TmrStop` 0; the delivery poll and the role SERVE; `TmrBar` 0;
`PollService` 1, `DevicePower` D0 and not `SuspendedInD0`; `TmrActive` 1.

**The step** (`XhciPollTmrStep`, pure) over the state, "wanted", the
generation read now and the generation stored with the hold
(`TmrHeldGen`):

- HELD and (not wanted, or `TmrGen` != `TmrHeldGen`): RELEASE - call
  `FALSE`, then RELEASED. **A boundary crossed since the hold forces the
  release even when "wanted" reads 1 again** (a revoke and grant, or a
  bar and grant, both before the thread sampled), so no hold ever
  crosses a power transition, a recovery or a stop unreleased.
- RELEASED and wanted: HOLD - call `TRUE`, then HELD, `TmrHeldGen` =
  the generation read with the inputs.
- Otherwise nothing.

After a RELEASE forced by a generation the thread loops at once and may
HOLD again under the new generation. There is no third state, so two
`TRUE`s in a row, or a `FALSE` with nothing held, cannot be produced.
The thread loops: wait on its event (unbounded: every input that can
change the step signals it), read the inputs, step, call, record, repeat.
On `TmrStop` "wanted" is 0: it calls `FALSE` if HELD and leaves.

**The bar** is set by the paths that take the controller away and must
not wait for the call: the device SET_POWER to a lower state and the
direct transition toward sleep (each beside its `HcdPollRevoke`), and the
stop (first, with `TmrStop`). It is a boundary. A grant clears it. So a
release at a power transition is a bit, a generation and an event: the
IRP goes on at once, and the call happens later on the timer thread.

**Every release point**, and what makes it:

| Point | Site | How it releases |
|---|---|---|
| The poller leaves ACTIVE (PERIODIC or IDLE) | `hcdPollDpc`'s decision | `TmrActive` 0 and the event; "not wanted" |
| The poller parked or revoked: a recovery, a failed controller, a containment | the DPC's park, `HcdPollRevoke` | a boundary: `TmrActive` 0, `TmrGen` advanced, the event; a recovery gives the request back and the next ACTIVE after the grant takes it again; nothing is left held behind a terminal failure whose poller no longer decides |
| The controller leaves D0 (device SET_POWER, D1 to D3) | `HcdControllerPower`, under the power gate, beside `HcdPollRevoke` | `TmrBar` 1, a boundary; released by the thread, asynchronously |
| The direct transition toward sleep (no device IRP) | `hcdDirectTransitionGated` | the same |
| S3 and S4 | the system IRP asks for D3 (`hcdSystemDone`), so the row above; where the D-IRP could not be asked, the direct transition | the same |
| S5 (shutdown) | the same D3, or the shut work item where the function is already out of D0 (then already released) | the same; a call still blocked when power goes is harmless, since nothing waits for it |
| The delivery leaves polling | only by a stop or a failed start: no run-time path leaves a poll delivery (the move goes to it) | the stop's row |
| The controller stops, is surprise-removed or removed | `hcdStopBody`, first step (all three reach it) | `TmrStop` and `TmrBar`, a boundary; the thread calls `FALSE` and leaves; joined late (section 7.2.5) |
| A start that fails | no thread is created unless the start succeeded | nothing to release |
| The switch | read at start only | a 0 makes no thread |

**What "ACTIVE" covers, read plainly.** Every event drained counts,
including a HID report: a mouse that is moving keeps the poller ACTIVE
and so keeps the request held, until it has been still for the idle
period. A mouse at rest with its interrupt-IN pending is PERIODIC and
holds nothing. That is the existing meaning of ACTIVE (section 6.1),
which T3 takes as it is: the cost is the system's finer clock while any
polled device produces events, and the legs count how often the request
toggles (section 7.2.6a).

A request taken just before a boundary - the thread read "wanted", a
power path barred, the thread then called `TRUE` with the old generation
- is released on the thread's next pass, which the boundary's event
guarantees; it is counted (`TmrHoldsRaced`) and is a request held for a
moment across the start of a transition, which the rule allows because
nothing waits for it.

#### 7.2.6a The idle period: kept at 50 ms, and what the legs measure

The owner keeps `XhciPollIdleMs`'s default at 50 ms (2026-10-13, after
review round 1, which found the case for 500 ms incomplete) and asks for
the readings on which a change would be decided. Since the idle period
is also the ACTIVE window after the last event (section 6.1), it governs
how long the finer timer outlives a burst; the measurement is therefore
part of this task. What a different value would change, and what is to
be read, kept apart:

- **The poll timer's own frequency** on an idle bus: about 1000 / idle
  DPCs a second, each a service pass (a drain attempt, the bus's state
  gathered under the controller lock, the decision), not a look at one
  TRB. It is one of several periodic paths (section 7.2.2): the
  controller thread (~100 ms), the tolerance clock (~100 ms) and the
  frame timer (500 ms) run whatever the idle period is. So the guest's
  total idle CPU is measured, not derived from the poll timer's rate.
- **Latency.** A Port Status Change at a root port, an external hub's
  status-change transfer (whose interval contributes nothing shorter
  than the idle period, section 6.1), a SuperSpeed device's Function
  Wake (a Device Notification Event, `hcd_susp.c` ~2435, with
  `XhciSelectiveSuspend` 1), and a USB 2.0 remote wake each reach the
  driver through the first path that both sees the event and schedules a
  drain: the poll timer, the controller thread's peek or the frame
  timer's peek (section 7.2.2), or - at `XhciTolerance` 1 and only after
  the event has stayed undrained for the backstop's four ticks - the
  tolerance clock. The nominal periods are scheduling intentions,
  quantised to the clock, not latency bounds. A remote wake of a device while the
  controller stays in D0 is distinct from the controller's own resume
  from D3 (section 7.2.6's D0 path), and is measured apart.
- **The ACTIVE window and the timer's toggles.** A longer idle period
  keeps the poller ACTIVE longer after each event: more polls per burst
  tail (at a 1 ms achieved cadence about one per millisecond of the
  window), the finer timer held longer, and fewer holds and releases per
  minute under bursty work. On Windows 98 SE and ME the longer window
  costs polls at the tick with no finer timer to show for it.
- **PERIODIC.** An interrupt endpoint's interval is held between the
  active and idle periods (section 6.1), so a larger idle period lets an
  endpoint whose interval exceeds 50 ms be checked at its own interval,
  which changes when the driver sees its completions.
- **Unchanged by the value**: section 6.2's displaced-deadline bound and
  section 7's discarded-interval bound (twice the 5000 ms idle bound),
  the 5000 ms bound itself, and the watcher's 100 ms.

**The measurement** (leg T8, section 7.2.12): on every target in mode 2 -
98 SE and ME included - at `XhciPollIdleMs` 50 and 500, the idle guest's
CPU over a fixed minute; the time from a plug to a working device at a
root port and behind an external hub; and the finer timer's holds and
releases per minute (`TmrHolds`, `TmrReleases`) under a moving mouse and
under intermittent storage bursts. **The wakes are not all readable on
the test environment** (round 2, finding D): QEMU generates no Device
Notification Event (record 14 section 8.12, read from QEMU 11.1.0's
`hcd-xhci.c`), so a Function Wake's latency cannot be read on a guest,
and a zero Function Wake counter is not a reading; a USB 2.0 remote wake
can be (QEMU moves a port in U3 to Resume with PLC on a device's
wakeup, the same section), with `XhciSelectiveSuspend` 1 and record 14's
test client, on the targets where that client exists - none is
provisioned for x64, so the x64 wake readings are owed unless one is
built. Function Wake stays a bench reading or unread, and is said so. The
default is decided by the owner on what was read, with what was not read
named beside it.

**An update keeps an existing value** (owner, 2026-10-13, decision T5:
option (a)). Both INFs write `XhciPollIdleMs` under
`FLG_ADDREG_NOCLOBBER` (task 34.1's rule), so whatever an earlier INF or
a user wrote stays; if the default ever changes, the change reaches fresh
installs, and the release notes tell an updated machine how to take it.
Nothing changes in either INF for this task's idle value.

#### 7.2.7 The power-IRP rule, argued

Microsoft warns that `ExSetTimerResolution` can deadlock against a power
IRP (roadmap 38.4; Microsoft's page "Calling ExSetTimerResolution while
processing a power IRP", whose text the implementation commit quotes in
the code's comment and here). The design holds the rule by construction:

1. Only the timer thread calls the routine.
2. **Nothing on a power path waits for the timer thread.** The power
   paths set the bar, a generation and an event; the D-IRP, the system
   IRP and the D0 completion take only the power gate, which the timer
   thread never holds. The controller thread, which settles child D-IRPs,
   never waits for the timer thread: it neither creates nor joins it.
3. **The one wait on the timer thread, the stop's join, waits behind no
   power IRP and holds none up**: it comes after the children's D-IRPs
   are settled and the completion workers are gone (section 7.2.5),
   while a new child Dx is completed at admission; the stop holds the
   door gate, which no power path takes, and not the power gate.
4. The timer thread holds nothing a power path needs while it calls: no
   gate, no spin lock, no IRP, no remove-lock count, no work item.
5. The lifetime: the thread runs `xhci98.sys`'s code and reads
   `hc->Tmr`, both alive until the stop has joined it; the stop
   precedes the remove that can unload the image (WDM's order, which
   `HcdStopController`'s callers keep). The routine is the kernel's.

**Not established here**, and asked of the legs: whether a PnP stop or
removal can itself be held behind a system power transition that the
call is waiting for. If one can, the join waits for the transition, and
by point 2 and 3 the transition waits for nothing the join holds: a
delay, not a cycle. Leg T3 reads a stop with a child D-IRP pended and the
timer call delayed (a `qemu` switch, `XhciQemuTmrDelayMs`, that sleeps
the timer thread before each call); leg T4 a removal around a transition
where a guest offers one.

#### 7.2.8 Several controllers

Each controller holds at most one request, from its own timer thread,
and pairs it. Their sum is the kernel's: the record relies on the
documented pairing of each `TRUE` with a `FALSE` and on nothing about how
the kernel combines requests from one driver. Leg T5 reads it with phase
observations (section 7.2.9): A and B both active, A idled (B's
observations after A's release), B idled; with the two asking different
values (`XhciPollActiveMs` 1 on A, 4 on B). If T5 shows the kernel not
combining requests from one image, the record is reopened (the
alternative is one request per image, counted across controllers).

#### 7.2.9 Counters and log

Appended to `XHCIHC_COUNTERS` after the last word (section 8's rule, the
start zeroing them; `XHCISNAP` names them, as task 36.9 did):

- `TmrSwitchRead` (`0xFFFFFFFF` not found) and `TmrSwitch` in effect;
- `TmrSource`: 0 none (Windows 98 SE and ME, or the switch off), 1 the
  helper, 2 the system (`MmGetSystemRoutineAddress`), 3 not found;
- `TmrThread`: 0 not created, 1 running, 2 creation failed, 3 joined;
- `TmrHeld` (0 or 1), `TmrHolds`, `TmrReleases`, `TmrReleasesForced`
  (by a generation), `TmrHoldsRaced`, `TmrObsCrossed`;
- `TmrDesired` (100 ns), `TmrActualHeld` (what the last `TRUE`
  returned), `TmrActualReleased` (what the last `FALSE` returned);
- `TmrReleaseCause` of the last release: 1 the poller left ACTIVE, 2
  parked or revoked, 3 barred (a power transition), 4 the stop;
- `TmrHeldMs`, the time held in this start, in whole ms, saturating;
- **phase observations** (round 1, finding 6; round 2, finding B; round
  3, finding 2): **the observation epoch**, `TmrObsEpoch`, is advanced
  by the timer thread under the controller lock immediately before each
  call (to an odd value: a call in flight) and immediately after it
  returns (to the next even value), so every hold and every release moves
  it twice. `hcdPollDpc` reads the epoch with its entry timestamp (`now`,
  taken at entry today, `src/hcd_poll.c` ~728) and again in the
  controller-lock hold where it observes (`XhciPollObserve`, ~813). Each
  run is a **sample**: its entry timestamp, its entry and observation
  epochs, the period armed for it and the start. **The baseline**
  (round 4, finding 1) is the last coherent sample, kept apart from the
  counting:
  - a sample is **coherent** when its entry and observation epochs are
    one even value; an incoherent one (an odd epoch, or a call landing
    between the run's entry and its observation) **invalidates** the
    baseline and counts nothing;
  - a coherent sample with no valid baseline, or whose epoch, period
    armed or start differs from the baseline's, **seeds** a new baseline
    and counts nothing - so a release, a hold, a change of period or a
    new start costs exactly one run and never stops the counting;
  - a coherent sample matching the baseline in epoch, period and start
    counts the interval from the baseline's timestamp when it is a
    **short request** (the period armed at most 5 ms), and in every case
    becomes the new baseline.

  So an interval that touches a call, crosses any hold or release (a
  HELD-RELEASED-HELD ABA included, since each moves the epoch), or sees
  one land during its own drain is never counted; each such run is
  counted in `TmrObsCrossed`. The phase it counts in is the phase of that
  even epoch (HELD or RELEASED, recorded beside the epoch by the timer
  thread). Per phase, three words: `ObsCount`, `ObsSumMs`
  (whole ms, saturating) and `ObsLate` (observed at least 3 ms longer
  than the period armed). Monotonic within a start; a leg reads a phase
  as the difference of two snapshots around it, so an interval from
  before a release never counts after it, which the start-long minimum
  and maximum (`PollIntervalMinMs`, `PollIntervalMaxMs`) cannot. Only a
  short request shows the clock: on an idle bus the poller asks 50 ms,
  which a fine and a coarse clock both serve within a tick.
- **The released-phase stimulus.** A request is RELEASED while the
  poller is PERIODIC, so a short PERIODIC period is the controlled way to
  read the clock with this controller's request given back: a resting
  device whose pending interrupt-IN endpoint's own programmed interval
  converts (section 6.1) to 2 to 5 ms - a high-speed interrupt endpoint
  of `Interval` 4 or 5 (2 or 4 ms), for instance - so the poller sits
  PERIODIC at that period with no event arriving. `XhciIntervalCap` is
  not a way to get there: it only caps an encoded interval at 8 (32 ms;
  `XhciTolCapInterval`, `src/xhci_tol.c`). Which QEMU device model, or
  which configuration of one, offers such an endpoint has not been read;
  the leg names it in its run sheet from that model's descriptors before
  it runs, and where none is found, T2's and T5's released readings are
  owed and said so. A reading is accepted only with a non-zero
  `ObsCount` in the window and `PollStateLast` PERIODIC with
  `PollPeriodLast` between 2 and 5 recorded. The same stimulus with the
  switch 0 is the baseline. It needs no ACTIVE state and so never takes
  the request itself.

The log's notes: `poll.timer.hold` (the value returned) and
`poll.timer.release` (the cause in the top byte, the value returned below
it), written by the timer thread through `XhciLogNote` - a producer, so
no `DbgPrint` site is added (AGENTS.md); the stop's counter dump
(`hcdLogCountersLocked`) carries the counters by the same `poll.timer.*`
names. The notes are one per call, so they are bounded by the request's
own transitions; an ACTIVE spell can be as short as one decision (a
promotion or an outstanding TD needs no recent event), so a burst of
short spells is a burst of notes, which the ring's bound and the
counters' totals absorb, and leg T8 counts.

**What the legs read.** The driver's balance is `TmrHolds` against
`TmrReleases` and `TmrHeld` 0 at the stop. Whether the system's clock
came back is another question - other software may hold a fine
resolution - and is read against a baseline with the switch 0 on the
same guest in the same boot, never from this driver's counters alone.
`TmrActualHeld` is what the kernel said it set, recorded beside the
observations, not instead of them.

#### 7.2.10 Host tests

In `test_poll`: `XhciPollTmrWant` over each input alone;
`XhciPollTmrStep` from both states with the generation equal and
advanced, with the invariants checked over random sequences of inputs
and boundaries (never `TRUE` while HELD, never `FALSE` while RELEASED,
no HELD state surviving a generation change, every `TRUE` followed by a
`FALSE` before the thread's exit); the two schedules of round 1's
finding 3 with the executor delayed - a revoke, release and grant with
the next decision still ACTIVE (one release, then one hold after the
grant's signal), and a revoke and grant before the thread samples (a
forced release, then a hold); a recovery; a PERIODIC decision after
ACTIVE (a release); D3 and back (barred release, no hold until the next
ACTIVE); the stale hold against a bar (counted raced); the stop while
HELD, while RELEASED and while a bar is set; the first pass with
`TmrActive` already 1 (a hold without a new edge); the observer
(`XhciPollTmrObserve`, pure): the 5 ms short-request cutoff at its edge,
the 3 ms lateness threshold at its edge, `ObsSumMs` saturating, a
HELD-RELEASED-HELD and a RELEASED-HELD-RELEASED sequence between two
runs, a call landing between a run's entry and its observation, an odd
epoch, a change of armed period and a change of start - each rejected,
and observation resuming after each (round 5, finding 2): after an
incoherent sample (an odd epoch, or a call between entry and
observation) the baseline is invalid, the next coherent sample seeds and
the one after it counts; after a coherent sample that changed epoch,
period or start, that sample has already seeded, so the next matching
sample counts; a long run of samples in one epoch counting every
interval. The channel's vectors for the timer service are record 18
section 15.3's; the binding machine's `XHCI_HLPR_EV_TIMER` and the
per-start latch are record 18 section 15.4's.

#### 7.2.11 What changes, for the integrator

`src/hcd_tmr.c` (new: the thread, its creation in the start and join in
the stop, the routine's lookup per image, the 32-bit NT 5 look);
`src/xhci_poll.c` and `src/xhci_poll.h` (the two pure functions, the
cause constants, the observer and its epoch); `src/hcd_poll.c` (`TmrActive`, the
boundary and the event in `hcdPollDpc`, `HcdPollPromoteLocked`, the
park, `HcdPollRevoke`, `HcdPollGrant` and `hcdPollMove`; the epoch read
at the DPC's entry and the observer beside `XhciPollObserve`); `src/hcd_power.c` (the bar beside each
`HcdPollRevoke` of a power path); `src/hcd_pnp.c` (the creation after a
successful start); `src/hcd_ctl.c` (the switch's read, the per-start
reset before `HcdPollOpen`, the stop's early bar and late join);
`src/hcd.h` (`hc->Tmr`); `src/xhci_counters.h` and `src/hcd_log.c` (the
counters, the dump and the `poll.timer.join` note); `src/hcd_inj.c` and
`src/hcd_susp.c` (`XhciQemuTmrDelayMs` and `XhciQemuSuspHoldDx`, the
skip before `hcdSuspTake`, and the `qemu.*`, `poll.timer.call` and `poll.timer.return` notes,
`qemu` only); both INFs (the switch's row on every path); `src/sources` (the
new object); `scripts/import-gate/xhci98-imports-amd64.allow` (the
`MmGetSystemRoutineAddress` row's `SITES` gains `hcd_tmr.obj`; the 32-bit
allowlist is unchanged); `scripts/inf-gate/check-inf.ps1`'s value-policy
table (~1323, a `XhciPollFineTimer` row: type `0x00010003`, default 1)
and its mutations in `test-inf-checks.ps1`, and the footprint; `xhcisnap`
(the names); `test/test_poll.c`; and section 8's catalogue,
`source-files.md`, the release notes' poll rows, and `build-and-test.md`'s
log lines.

#### 7.2.12 Legs

On QEMU guests, the `release` package unless a switch needs `qemu`;
each leg records the kernel image that ran and that the boot was an
ordinary one, as record 18 section 10 asks. Phase distributions are read
as snapshot differences (section 7.2.9).

- **T1, the effect.** Polled UAS `XFERTEST VERIFY` and the 20 s
  `TONE.WAV` under mode 2 on Windows 2000 (the SMP guest, and under
  Driver Verifier on `xhci98.sys` and `xhcihlpr.sys`), XP, XP x64 and 7
  (x86 and x64), with `XhciPollFineTimer` 1 and then 0 in the same boot
  (a disable and enable between), read against task 36.13's readings
  (`runs/run-36.md`, the P rows): the copy rate, the tone's span and
  gaps, the HELD phase's distribution against the switch-0 baseline,
  `TmrActualHeld`, and the guest's idle CPU.
- **T2, the release.** After T1's copy, the released-phase stimulus
  (section 7.2.9) for a minute: `TmrHeld` 0, `TmrReleaseCause` 1,
  `TmrHolds` equal to `TmrReleases`, and the RELEASED phase's `ObsLate`
  share and mean (`ObsSumMs` / `ObsCount`) against the same stimulus with
  the switch 0 in the same boot; where the two disagree, the baseline is
  taken again to tell other software holding the clock from this driver.
  Then a mouse moved (held while it moves) and left still (released one
  idle period later).
- **T3, the stop.** A disable while a copy runs, and a surprise
  removal (QEMU `device_del`, as record 18's L3): the stop's dump with
  `TmrHeld` 0 and cause 4, no bugcheck, and the next start's counters
  from zero. Then the stop with a child's power IRP really outstanding
  (round 2, finding C): a child put in D2 does not keep its D-IRP pended
  (admission publishes the Dx and the executor completes it), so the
  `qemu` flavour gains `XhciQemuSuspHoldDx` 1. **Under it the executor
  does not claim a child's pended Dx**: its pass skips the slot before
  `hcdSuspTake` (which appends the IRP to the pass's taken list; its
  caller then clears `PwrIrp`, `src/hcd_susp.c` ~2227), so the IRP stays
  in `PwrIrp`. That preserves it against **normal executor completion
  only** (round 4, finding 3): the other settlement paths stay as they
  are - the PDO's close (`HcdSuspPdoClose`, ~3035, from the PDO's
  teardown), the controller's suspend (`HcdSuspSettleStale`, ~3424, from
  `hcd_power.c` ~116) and the stop's unlink (`HcdSuspUnlinkFinish`'s
  hand-off to a completion worker) - and the completion note says which
  of them claimed the IRP.

  Notes, `qemu` only: `qemu.susp.hold` when the executor first skips a
  slot (the PDO's serial, the D-state); `qemu.stop.dx` written by the
  stop's very first step, before the bar, for each PDO whose `PwrIrp` is
  a non-NULL Dx (serial, D-state, `PowerPending`); `qemu.susp.done`
  written at the one point every D-IRP completion of the executor's
  passes through, `hcdSuspCompletePower` (`src/hcd_susp.c`), whether it
  is called inline - the PDO's close, `HcdSuspPdoClose` through
  `hcdSuspSlotFinish` (~3087, ~3015) - or by a completion worker (the
  unlink's hand-off, the stale settle): after `IoCompleteRequest`
  returns and before `PowerPending` is dropped, from the serial, D-state
  and claiming path (unlink, PDO close or stale settle) saved before the
  completion, never read back from the completed IRP (round 5, finding
  1); the settlement paths themselves unchanged; `poll.timer.call` written by the
  timer thread **before the injected delay** and `poll.timer.return`
  after the call returns, each with the start's serial and the call's
  sequence number in that start (round 4, finding 2); and
  `poll.timer.join` after the join returns.

  The leg: `XhciSelectiveSuspend` 1, record 14's test client asking D2,
  `XhciQemuTmrDelayMs` 2000, a hold taken by a copy, then a stop of the
  controller. It passes only on this order in the log, for the held PDO
  and one call sequence number n: `qemu.stop.dx` (the IRP outstanding at
  the stop's entry), then `poll.timer.call` n (the stop's bar starts the
  delayed release), then `qemu.susp.done` (the child completed **while
  call n is still outstanding**), then `poll.timer.return` n, then
  `poll.timer.join`. A `qemu.susp.done` after `poll.timer.return` n is a
  run in which the delay did not overlap the completion: retaken with a
  longer delay, or the overlap reported owed. A child already settled in
  D2 does not count.

  **Which stop reaches its entry with the child's IRP still in its slot
  is not read.** A Device Manager disable may remove the child PDOs
  before the controller's stop, so that `HcdSuspPdoClose` settles the IRP
  first and `qemu.stop.dx` names nothing; a surprise removal (QEMU
  `device_del`) may order them otherwise. The leg tries both and records
  the order it saw; if neither reaches the stop's entry with the IRP
  outstanding, this half of T3 is owed, and section 7.2.7's point 3 then
  rests on the stop's static order (section 7.2.5) alone, said so.
- **T4, S3, S4 and S5.** Where a guest sleeps (XP, Vista and 7 hibernate
  and the guests that stand by, as task 36.28's legs): a copy running,
  then the transition; after the resume `TmrReleaseCause` 3 from before
  it, the delivery still poll, the hold taken again at the next copy.
  With `XhciQemuTmrDelayMs` 2000 too, so the release is still in flight
  when the D-IRP completes. Shutdown with a copy running: the system
  powers off, read on 2000 and 7. Under Driver Verifier on 2000 SMP and
  7 x86 for one hibernate each. A removal attempted around a transition
  where a guest offers one (section 7.2.7).
- **T5, two controllers.** Two `qemu-xhci` controllers in mode 2 on 7
  x86 and 2000, A asking 1 ms and B 4 ms (`XhciPollActiveMs` 1 and 4),
  A carrying the released-phase stimulus whenever it is not copying.
  Read, each a window of its own: B alone holding (A released: A's
  RELEASED observations calibrate what B's 4 ms request leaves the clock
  at); A and B both holding (A's HELD observations); A released while B
  holds (A's RELEASED again); both released (against the switch-0
  baseline). How the kernel combines two requests from one image is
  unread, so the leg records what each window shows and states no
  expectation as a fact; section 7.2.8's reopening rule applies to it.
- **T6, without the routine.** A cold boot with the helper's service
  disabled before the boot, on 32-bit 2000, XP and 7: `TmrSource` 3, the
  tick, nothing else changed. (Deleting the file of a running helper
  changes nothing until the next boot, and a binding persists until the
  remove.) Windows 98 SE and ME in mode 2: `TmrSource` 0, the release
  before it's readings.
- **T7, the switch off** reads as task 36.13 did: no thread
  (`TmrThread` 0).
- **T8, the idle period** (section 7.2.6a): every target in mode 2, 98
  SE and ME included, at `XhciPollIdleMs` 50 and 500 - the idle guest's
  CPU over a minute; a plug to a working device at a root port and
  behind an external hub; a USB 2.0 remote wake with
  `XhciSelectiveSuspend` 1 and the controller in D0, on the targets
  where record 14's test client runs (x64 owed); a Function Wake marked
  unread on QEMU (section 7.2.6a); holds and releases per minute under a
  moving mouse and under intermittent copies; and, on an update from
  `2.3.1.0`, the value kept.

#### 7.2.13 The owner's decisions (2026-10-13)

Answered before review, then revised by the owner after review round 1
(section 13.6):

- **T1: (b)**, a timer thread per controller (section 7.2.5).
- **T2: (a)**, `XhciPollActiveMs` x 10000 (section 7.2.6).
- **T3:** the request is given back when the poller leaves ACTIVE, with
  no hold of its own: the idle period is the only linger (section
  7.2.6). **`XhciPollIdleMs` keeps its default of 50**; the legs measure
  50 against 500 on every target and the default is decided on that
  reading (section 7.2.6a). (The first answer moved it to 500; withdrawn
  after review round 1.)
- **T4:** `XhciPollFineTimer`, 1 by default (section 7.2.4).
- **T5: (a)**, an update keeps an existing value (section 7.2.6a).

The helper's decisions (H1 to H5) are record 18 section 15.13's.

## 8. Counters and log

In the counter block (`XHCIHC_COUNTERS`, `src/xhci_counters.h`, appended
after its last word, so an older `XHCISNAP` walks past them by the HCD
region's own sizes and prints them as numbered counters): the value of
`XhciInterruptMode` as read (`0xFFFFFFFF` when not found) and the effective
mode; the delivery in effect and its reason (section 4); the pin and its
read status; the two periods as read and in effect; the poller's role; the
period last armed and the state that chose it; poll runs, and poll runs
that found an event; peeks and peek hits; promotions, parks and kicks;
arms replaced as displaced (section 6.2); the shortest and longest interval
observed between two poll runs with the count of discarded intervals
(section 7); and the watcher's looks, asserted looks, stale pairs, the move
(0 or 1), whether the mask at the move was proven, the attempts to retire
the standing assertion, and the retirement's state (0 none owed, 1
proven by acknowledgement, 2 by INTx Disable, 3 uncontained, 4 by an
HCRST). The delivery and the
reason are written at start and again at the move, under the controller
lock; the poller's counters under the controller lock or `PollLock`, as
each site holds.

The codes, in `xhci_poll.h`: delivery 0 none (a failed start), 1 line, 2
poll, 3 MSI, 4 MSI-X, 5 a message whose capability was not read; reason 1
automatic, 2 automatic with the pin unreadable, 3 automatic: interrupt pin
0, 4 automatic: no interrupt resource, 5 forced line, 6 forced line with
the pin unreadable, 7 forced, no interrupt (the start fails), 8 forced
poll, 9 automatic: line interrupt not delivered (the move), 10 automatic:
message, 11 line asked for, message granted (revision 6 only: no longer
produced), 12 message not connected, 13 forced, no line offered (revision 6
only: x86 with no helper bound, the start failed; no longer produced), 14
forced line, a message granted (the start fails), 15 forced message (mode
3, the message connected), 16 forced message, none granted (the start
fails), 17 forced message, not connected (the start fails), 18 forced
message, no message interrupts on this system (98 SE, ME, 2000, XP, XP
x64: the start fails); pin
status 0 not read, 1 read, 2 unreadable.

The log ring's notes: at start `intr.mode.read`, `intr.mode`,
`intr.delivery`, `intr.reason`, `intr.pin`, `poll.idle.ms` and
`poll.active.ms`; at the move `intr.moved` (the stale pairs that proved it)
and `intr.moved.masked`. Nothing per poll or per look. The stop's counter
dump (`hcdLogCountersLocked`) carries the delivery, the reason and the
poller's and watcher's counters by the same `intr.*` and `poll.*` names.

**A start the delivery refuses** (revision 7; mode 1 or 3 without its
delivery, and mode 1 with no interrupt as before) never reaches the stop
that flushes the ring, since a failed start is never stopped, and its FDO
goes at the remove that follows, so neither the counter block nor the
ring could be read afterwards. The refusal therefore records itself: the
log switches are read, the start's notes above are written with
`start.refused.delivery` (the reason), and the ring is flushed with the
counter dump through `HcdLogFlush` - the stop's own flush, from the same
PASSIVE_LEVEL PnP thread, adding no `DbgPrint` site - before the start
returns its failure. With `XhciLogDebugView` set a user therefore reads
the reason in DebugView beside the Code 10 - in the counter dump's
`intr.reason` at every verbosity, and from verbosity 2, where notes are
recorded, in the start's notes and `start.refused.delivery` too; the `qemu` flavour's
start-decision event (record 18 section 9) carries it too.

**The delivery in effect must be capturable** (owner, 2026-10-05): a user
on a machine that polls automatically and one forced to poll must be told
apart, by `XHCISNAP` and by the DebugView log. Three routes, each with what
it does and does not guarantee:

- **The counter block, through `XHCISNAP`** - the reliable route. The
  delivery, its reason, the pin's read status and the values in effect are
  persistent fields, set at start, and the delivery and its reason set again
  at a run-time move to polling (section 4.2); `XHCISNAP` reads them whenever the
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

**As built (task 36.9, 2026-10-10).** The values, delivery, reason, pin
and poller counters are the counter block's words 63 to 95, which the HCD
region already carries whole; `XHCISNAP` names them in its counter list and
decodes them in a section of its own, "event delivery". The window header
(schema 5) is unchanged. The HCD region's header grows two words, version
unchanged, as 35-T.8's tolerance words did: 202 is `EventDelivery`'s byte
offset in the extension image and 203 the count of the six delivery words
(`EventDelivery` to `LineRetireState`, asserted contiguous), so the tool
names them from the `.BIN`'s own bytes. Older layouts still read: a 63-word
counter block is a driver from before polling, reported as taking the line;
a header of 202 words places no extension words.

## 10. `XHCIQUAL` (task POLL.5)

A controller is no longer disqualified for `Interrupt Pin = 0`:
`quick_classify` and `quick_reason` (`mmiodiag.c:342`, `361`) report it as
"needs polling: no legacy interrupt (the driver's automatic mode polls it,
or set `XhciInterruptMode` to 2)", and the other disqualifiers - BAR0 above
4 GB, unassigned, I/O space - keep their verdicts.

C4 is not converted wholesale. Its fail covers several causes (review,
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

**The verdict "QUALIFIED WITH CAVEATS" (owner, 2026-10-09).** "Needs
polling" is not folded into plain QUALIFIED, nor into "QUALIFIED (with
warnings)", nor made a failure or a provisional result: a controller with
no working interrupt that passes the rest gets a verdict of its own,
`QUALIFIED WITH CAVEATS`, with its own exit code, **3** (0 stays all
qualified, 1 any not qualified or a read-only run, 2 a usage error or no
controller; 1 wins over 3 across controllers). The verdict prints its
caveats: there is no working line interrupt, so `xhci98.sys` polls the
controller (`XhciInterruptMode` 0, automatic, or 2), at the cost of more CPU
wake-ups and an event latency of up to the poll period; or an MSI-capable
OS takes a message interrupt. For the second route the tool reads the PCI
capability list it already walks (05h MSI, 11h MSI-X) and says, when one
is present, that Windows Vista and 7 can take a message interrupt instead
(x64 directly; 32-bit through the `xhcimsi.sys` helper, design record 18),
and when none is, that polling is the only route; a controller in the
quirk table as `XHCI_BROKEN_MSI` is told polling. Warnings, if the run also
has any, are said beside the caveats. The quick scan's pin-0 outcome is
`LOOKS QUALIFIED WITH CAVEATS`, ranked above `LOOKS QUALIFIED` and below the
other outcomes; its reason names whether an MSI capability is offered.

**As built (task 36.10, 2026-10-10).** The decisions are pure and
host-tested in `xhciqual/verdict.c` (`test/test_verdict.c`), moved out of
`report.c`: `c4_verdict` turns what C4 saw into PASS, **POLL** or FAIL;
`c4_lets_ports` lets C6 and C8 run after PASS or POLL; `final_verdict`
prints and returns the verdict; `run_exit_code` makes the exit code. C4
reads POLL when there is no line to arm - `Interrupt Pin = 0`, an Interrupt
Line DOS cannot use (0, 2, above 15) or PCI Interrupt Disable stuck set -
and a No-Op issued with the interrupter's enable clear and no hook installed
completes polled; or when the hooked No-Op's completion reaches the event
ring and no interrupt arrives, whether `IP` never came up with the line
masked or came up and the ISR never entered (issue 12's shape). The
unusable-line and stuck-disable cases are this task's reading of "no
working interrupt" beside the two the section names: in each no interrupt
can arrive, and the polled completion is the same evidence. FAIL stays for
a DPMI step that failed, an ISR entry the controller's status did not own,
an ISR with no completion behind it, and a No-Op that never completed even
polled, which the verdict words as the command's failure rather than the
line's. One POLL differs in its advice (review rounds 1 and 2): when `IP`
was never seen, at any look through both of C4's waits, the driver's
automatic mode cannot see the dead line - section 4.2's watcher moves on
`IP` asserted with nothing claimed - so the caveat names
`XhciInterruptMode` 2. Its message route is still named from the
capability list, said as untested; and since an enabled MSI or MSI-X
clears `IP` itself after its message (4.17.5), one found enabled (Message
Control, read only) is named as a reason `IP` may not have been seen. A
source that comes live late, `EINT` and `IP` both up after the first wait,
goes on to the delivery test as a prompt one does (review round 3). Every
path that installed the hook removes it before C4 returns, so C6 and C8 run
with no live ISR. `--irq-selftest` gives the same outcome
as "IRQ SELF-TEST: NO WORKING INTERRUPT", exit 3. The QEMU matrix reaches
it through two test programs that rewrite the xHCI's Interrupt Line before
the run (`xhciqual/README.md`, "What the matrix covers").

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
  counters where they happen. Where no guest offers D3, the recovery is
  read instead, and a Device Manager disable and enable reads the close
  (section 5.2). System standby and hibernate under a poll delivery are
  read in the planned selective suspend work, with task SUSP.1.

  **A recovery parks only when the timer's DPC sees it** (corrected
  2026-10-10 from "the recovery alone reads park and kick", after 36.13's
  legs and the wave 3 matrix). The poller parks only when its DPC, under
  the controller lock, reads the service failed or revoked: from the
  moment the controller is marked failed (`ControllerFailed`, raised after
  the controller thread's own health poll) until the recovery's grant.
  `hcdRecover` runs in that same thread pass, `XhciRecoverController` at
  DISPATCH_LEVEL, and `KeLowerIrql` comes before the grant, so a DPC
  already pending on that processor can read the revoked service there. A
  DPC that does parks, and the grant kicks it (`PollParks` and
  `PollKicks` each up by one). A DPC that takes the lock before the
  failure or after the grant does not park: the armed timer, or a DPC
  already FIRING, keeps ownership and serves or re-arms without a kick
  (section 5.3: the grant does nothing in ARMED, or in FIRING without
  `ParkPending`), and neither counter moves. Which one happens depends on when the DPC takes the lock, which
  no leg controls; both readings are correct. No timing of the window has
  been measured; that it is short beside the poller's period on QEMU is a
  likely reason, not a reading, for the recoveries that parked nothing.
  What the leg proves either way is the service restored: `PollServiceGen`
  advanced past the revocation and the grant, `PollService` 1, `PollRuns`
  advancing after the recovery, and the devices working. Read so far:
  parks and kicks on 2000 and XP (36.13); none on 98 SE and 7 x64 (36.13)
  and on 2000 in the wave 3 matrix (`PollServiceGen` 2 to 6, runs 3482 to
  41577). A park and kick across a recovery is deterministic only in
  `test_poll` (section 5.3's vectors).
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
- **The dead line (section 4.2)**, by a third `qemu`-flavour switch,
  `XhciQemuDeadLine` N (N + 1 in the value, so 0 is off: a value of 1 is
  N = 0). From N ms after the start's grant a latch is set,
  independent of what the ring holds: while it is set the ISR still
  acknowledges what it claims (a level line left asserted would storm and
  stop the guest) but queues no drain and counts no claim, and every
  watcher look reads as asserted, as a real pin's standing `IP` would
  through every backstop drain. It is a stand-in, said as one, and the
  record names it (`qemu.inj.deadline`). Read under mode 0 as the move,
  with its reason and `intr.moved`, and devices working after it; under
  mode 1 as no move, the backstop carrying the bus and, while the
  controller thread waits for a completion of its own (a command's, an
  EP0 transfer's), the thread queuing the drain itself between the
  wait's slices at `XhciTolerance` 1 (`enum.wait.drains`, the drains
  queued, not the events they took; added by 36.13's legs, which found
  such a wait otherwise expiring into a reset, since the backstop runs on
  the waiting thread). Two legs, each
  with its expected evidence stated for the drains that compete:
  - **N = 0, a line dead from the first No Op**, a device present at
    boot. For N = 0 the latch is set before the interrupt is connected
    (review of revision 5, round 2: the No Op is submitted and the
    enables raised before the grant, so a latch set at the grant would let
    its completion be claimed and drained normally); the watcher's timing
    still begins at the grant, and the leg reads `qemu.inj.deadline.isr`
    (claims the latch withheld) to show the No Op had no ISR-driven drain.
    The move is read about 300 to 400 ms after the grant. The No Op
    may be drained by whichever comes first - the backstop, 36.4's BUSY
    retry's drain (`hcdCommandBusy` asks for one), or the move's first
    service pass - and the leg records which (`enum.cmd.busy.drains`,
    `tol.backstop.drains`, `poll.runs.events`); the pass is the device
    enumerating with no `cmd.timeout`, whichever drained it. The
    commands after it may also be drained by the thread's own wait
    (`enum.wait.drains`), which counts drains queued, not the No Op's.
  - **N inside a command wait**: N chosen to fall after the grant and
    before a hot-plugged device's Enable Slot completes, with
    `XhciTolerance` 0 so the backstop cannot drain it. The pass is the
    wait ending inside the 5000 ms command wait, by the move's drain
    (`poll.runs.events` 1 or more, `cmd.timedout` unchanged).
- **The watcher on a working line**, every target in mode 0: looks counted,
  no asserted look held into a stale pair, no move, and the matrix
  unchanged from the release before it.
- **The switches' names** (the `qemu` flavour only, written by no INF, read
  at every start): `XhciQemuNoInterrupt` 1 drops the interrupt resource and
  reports the pin 0, `XhciQemuPinUnreadable` 1 fails the HCD's pin read,
  and `XhciQemuDeadLine` as above.
- **The period observed** per target, from the counters.
- **Line against poll** on the same guest: a storage copy rate, audio
  continuity, and the guest's idle CPU load.
- **The default on a pin**: mode 0 reading as the release before it (the
  matrix unchanged; on Vista and 7, where the INF now asks for
  MSI, with `MSISupported` 0), and mode 1 the same, on a target without
  MSI and on Vista and 7 with `MSISupported` 0 or, with the requirements
  filter enabled, `MSISupported` 1 (section 4.3); with a message granted,
  modes 0 and 3 read as the message, and mode 1 fails the start where
  no line was offered (revision 7).

Bench (POLL-E, owner): mode 2 on the E460 and the P14s Gen 1 under Windows 98
SE, and an MSI-only controller if one is at hand. The synthetic no-interrupt
leg above does not depend on one.

## 12. Decisions

Taken by the owner on 2026-10-05, each from options with a recommendation:

1. The values and defaults: `XhciPollIdleMs` 50, `XhciPollActiveMs` 1,
   neither 0. (Kept on 2026-10-13, decision 9, pending section 7.2.6a's
   measurement.)
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
6. (2026-10-08, after issue 12) Automatic mode moves a line proven dead at
   run time to polling (section 4.2). This replaces a narrower
   stalled-event watchdog the issue 12 plan had held as task 36.4.
7. (2026-10-08) MSI on Vista and 7, both architectures (section 4.3;
   roadmap 36.15 and 36.16).
8. (2026-10-11, amending the 2026-10-09 mode rules of section 4.3 and
   design record 18 sections 4.2 to 4.3a) `XhciInterruptMode` 1 is the
   interrupt line only on every system: a message grant fails the start
   (Code 10) rather than running as mode 0. 3 is a message-signalled
   interrupt (MSI or MSI-X) only: on Vista and 7 the requirements filter
   keeps only the message configurations, and a message not granted or
   not connected (32-bit without the helper bound included) fails the
   start, with no fallback to the line or to polling; on Windows 98 SE,
   ME, 2000 and XP, 32- and 64-bit, mode 3 fails the start. 0 and 2 are
   unchanged, and 4 and above still run as 0 (revision 7; task 36.53).

9. (2026-10-13, task 38.4; revision 8; revised by the owner after review
   round 1) A finer system timer while polling, on Windows 2000 and later
   (section 7.2): T1, the call on a timer thread of each controller's; T2,
   the active period x 10000 asked for; T3, held while the poller is
   ACTIVE and given back when it is not, the idle period the only linger,
   `XhciPollIdleMs` kept at 50 and measured against 500 on every target
   before any change (section 7.2.6a); T4, `XhciPollFineTimer`, on by
   default; T5, an update keeps an existing value. The helper's H1 to H5
   are design record 18 section 15.13's.

## 13. The reviews

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

Converged (review round 4, 2026-10-05): one wording finding, the roadmap's
Phase 35 status still naming revision 3, taken. No finding in the reopen,
the deadline conversion, the phase split or the cross-references.

### 13.5 Revision 5 (task 36.5)

Reviewed 2026-10-09 in 36.5's loop, eight rounds, converged at round 8.

Round 1 (`a744811`): 4 P1, 5 P2, all taken.

| # | Finding | Where answered |
|---|---|---|
| 1 | P1: the export walk touched pages it had not proven resident | Section 4.3: amd64 calls `MmGetSystemRoutineAddress`; x86 walks only the header page and nonpaged `.text` below a routine in it (static reading of both kernels, PAE kernels before code), every header and export range checked against a non-discardable section, a refusing reader in the vectors |
| 2 | P1: a kick inside the core's resume runs before the HCD publishes D0, and the DPC parks for good | Section 5.3: `PollService` set by the HCD alone (`HcdPollGrant`) after D0 is published, at start, D0 return and recovery |
| 3 | P1: the watch period ran through the service conversion, which the 50 ms idle default turns into 1 ms | Sections 4.2 and 5.2: the watcher arms its own 100 ms, no conversion |
| 4 | P1: a message ISR returning TRUE always can claim another device's interrupt | Section 4.3: `XhciIsr`'s answer; roadmap 36.15 and issue 12 section 5.3 corrected |
| 5 | P2: masking forbids new interrupts but does not end an INTx already asserted | Section 4.2, the move's step 3: acknowledge `EINT` then `IP` with `IE` 0, read back, `LineRetired`; the ISR declines only on it |
| 6 | P2: `CONNECT_MESSAGE_BASED` takes no count; the context and teardown | Section 4.3: the grant served as one (only Interrupter 0 is enabled), its count recorded; `ConnectionContext` kept, `FloatingSave` FALSE, `IoDisconnectInterruptEx` on every release |
| 7 | P2: a recovery between two looks could join their history | Sections 4.2 and 5.3: `PollServiceGen`, advanced at every grant and revocation |
| 8 | P2: the dead-line injection vanished with an empty ring, and the BUSY leg's drain competes | Section 11: a latch independent of the ring; each leg's competing drains and expected evidence |
| 9 | P2: the port machines are the thread's, not the lock's | Section 6.1: `PollPortsBusy` published by the thread; a port reset promotes |

Round 2 (`1bc3546`): 3 P1, 3 P2, 1 P3, all taken.

| # | Finding | Where answered |
|---|---|---|
| 1 | P1: the start's grant came before `hcdStart` publishes D0 | Section 5.3: the start's grant in `hcdStart`, after the D0 publication |
| 2 | P1: the retirement raced the DIRQL ISR, which declines on `EINT` 0 | Section 4.2, step 3: `KeSynchronizeExecution` around `XhciRetireLine`; `IP` before `EINT`, so an unproven step leaves the ISR able to claim; the poller closed before the disconnect (section 5.2) |
| 3 | P1: a failed header test sent the walk below the image base | Section 4.3: the walk runs only on Vista and 7 builds whose kernels were read; it stops at the first recognised header and never reads below it |
| 4 | P2: the grants' place against the power gate | Section 5.3: every grant inside the power gate; the recovery's on its final `ok`, before the gate is left; the D0 paths' only on a resume that succeeded |
| 5 | P2: N = 0 armed the latch after the No Op's interrupt was enabled | Section 11: the latch set before the connection for N = 0 |
| 6 | P2: a retry repeated the acknowledgement without the both-enables proof, and `IP` can rise again | Section 4.2, step 3.1 to 3.3: both enables proven each attempt; a re-set `IP` with `IE` 0 told from the standing one |
| 7 | P3: roadmap 36.15 still argued one walk and one message | Roadmap 36.15: amd64's call, x86's walk, a multi-message grant served as one |

Round 3 (`b814ee3`): 2 P1, 1 P2, 1 P3, all taken.

| # | Finding | Where answered |
|---|---|---|
| 1 | P1: a failed `IP` step left `InterruptDeliverySuppressed` set, so a later suspend or recovery opened the ISR's old decline gate | Section 4.2: step 1 no longer sets it; `LineMoved` keeps the ISR claiming while a retirement is owed; the HCD retries before a suspend, a recovery and the disconnect; a completed HCRST retires it |
| 2 | P1: a build number does not prove the running kernel's layout | Section 4.3: x86 reads no kernel memory - it parses the kernel's file and proves the identity by every import resolving to one base; roadmap 36.15 |
| 3 | P2: a park against a close asked after the DPC took ownership | Sections 5.2 and 5.3: the park turns CLOSING into CLOSED; a revocation cancels no timer, stated; vectors |
| 4 | P3: the retirement's `EINT` write broke Table 5-21's order | Section 4.2, step 3.3: `EINT` is not acknowledged |

Round 4 (`f4d8a28`): 2 P1, 2 P2, all taken.

| # | Finding | Where answered |
|---|---|---|
| 1 | P1: agreeing anchors do not prove the target's RVA | Section 4.3: amd64 through `MmGetSystemRoutineAddress`; x86 through an NT 6-only helper driver designed separately (owner, 2026-10-09, after the review), the line without it; roadmap 36.15 |
| 2 | P1: an unproven retirement and the ISR's lifecycle gates | Section 4.2: why the ISR path never acknowledges this assertion; the unproven outcome recorded and ended by the next HCRST |
| 3 | P2: CLOSED published before the DPC's last controller access | Section 5.2: CLOSING kept through the park; CLOSED in a `PollLock`-only epilogue |
| 4 | P2: the file parser's callable-address and short-read checks | Moot with finding 1: the file parser is withdrawn |

Round 5 (`5508b61`): 2 P1, 1 P2, all taken.

| # | Finding | Where answered |
|---|---|---|
| 1 | P1: PARKED published before the DPC's last access; a grant could arm under a parking DPC | Sections 5.2 and 5.3: `ParkPending` and `KickOwed`; ownership given up only in the `PollLock`-only epilogue |
| 2 | P1: a finite verdict is not permanent unreachability; containment needed | Section 4.2: the ISR keeps its path; escalation to PCI INTx Disable, then the recovery's HCRST, then a recorded uncontained terminal |
| 3 | P2: the acceptance legs still asked MSI of x86 | Roadmap decisions table, 36.15 and 36.16; section 4.3's legs |

Round 6 (`ded0bbf`): 1 P1, 2 P2, all taken.

| # | Finding | Where answered |
|---|---|---|
| 1 | P1: clearing Interrupt Disable before the reset could re-expose a pending `IP` | Section 4.2: cleared only after `XhciInitController` succeeded, just before `XhciEnableInterrupts`, read back; left set on a failed start; a stuck bit handled per mode |
| 2 | P2: stop and suspend could bypass the PCI step | Section 4.2: `HcdPollContain`, under the power gate in D0, called by the thread, both suspend paths, `hcdRecover` and the stop after the thread ends |
| 3 | P2: "latched failed" did not enter the recovery machinery | Section 4.2: the recovery asked through `XhciRequestControllerReset`, its existing bounded retries and terminal kept; `LineRetired` set at HCRST completion |

Round 7 (`8cee483`): 1 P1, 1 P2, both taken.

| # | Finding | Where answered |
|---|---|---|
| 1 | P1: containment could write MMIO during a reset or on a bad layout | Section 4.2, step 3: `HcInfoStatus` admission, HCRST/CNR and operand guards; the PCI step taken without MMIO |
| 2 | P2: the watcher could create an obligation after a lifecycle check | Section 4.2, containment step 2: `HcdPollRevoke` before the check |

Round 8 (`59e0959`): **converged** - "the design is now sound enough to
implement", no P1 or P2. Two P3s taken: `HcdPollRevoke` named among the
revocation's writers (section 5.3), and what `LineRetired` proves stated
for all three of its proofs (section 4.2). Review thread
`01a12062-0080-7c92-a609-9e2b765333c4`.

### 13.6 Revision 8 (task 38.4)

Drafted 2026-10-13 and put to the owner for section 7.2.13's decisions
before any review round (the owner's rule for Phase 38's design tasks:
decisions first, review after); the answers written in the same day,
with the idle default's change (sections 3 and 7.2.6a) and the open T5.

Round 1 (`e2c01854`, review thread
`01a135b1-e223-7ab1-95c0-686c23dc9d01`): 2 P1, 8 P2, 3 P3. With them the
review gave an opinion on each owner answer; the owner then revised T3
(the 500 ms default withdrawn, measured instead), T5 (option (a)) and
record 18's H2 (no forced restart). All findings taken in revision 8's
round-1 commit:

| # | Finding | Where answered |
|---|---|---|
| 1 | P1: the stop's join came before the children's pended D-IRPs were settled, so a child power IRP could wait on the call | Section 7.2.5: the stop bars and signals first, joins after `HcdEnumDrop`, `HcdSuspCbStop` and `HcdSuspDoneDrain`; section 7.2.7 point 3; leg T3 with a pended child and a delayed call |
| 2 | P1: the 500 ms rationale ignored the other peeks and Function Wake's Device Notification Event | Section 7.2.6a rewritten as a measurement; section 7.2.2 names the paths |
| 3 | P2: a grant did not signal; a revoke and grant before the thread sampled let a hold cross the boundary | Section 7.2.6: grants and moves signal, a boundary resets `TmrActive` and advances `TmrGen`, a hold from an older generation is released first; vectors |
| 4 | P2: creation and startup ownership underspecified; the controller thread's unsafe reference fallback | Section 7.2.5: per-start reset before `HcdPollOpen`, creation in `hcdStart` after success, the kept-handle join, the first pass reconciling, the NT 5 look latch |
| 5 | P2: `DelFiles` of an in-use file needs the deferred flag | Record 18 section 15.7 |
| 6 | P2: start-long min and max cannot show a release or two controllers apart | Section 7.2.9's phase bins; legs T2 and T5 by snapshot differences against a switch-0 baseline, two values asked |
| 7 | P2: the absence and injection legs did not exercise their conditions | Leg T6 by a cold boot; record 18 section 15.12's N3 with an MSI case and a polled case each |
| 8 | P2: a missing baseline set only warns | Record 18 section 15.11: every set fatal for the profile and for a release |
| 9 | P2: the historical upload-set promise contradicted `make-release.ps1`'s version gate | Record 18 section 15.11: reconstructed from their cut commits |
| 10 | P2: the value-policy table and the binding machine's real files missing | Section 7.2.11; record 18 sections 15.4 and 15.11 |
| 11 | P3: the logging bound was false | Section 7.2.9 |
| 12 | P3: an inference stated as a reading | Section 7.2.2 |
| 13 | P3: the kit substitutes a target rather than refusing | Record 18 section 15.10 |

Round 2 (`50c0dfc3`, same thread): 1 P1, 3 P2, 2 P3. Round-1 findings 1,
3 to 5 and 7 to 13 confirmed closed; 2 and 6 reopened narrower. All
taken:

| # | Finding | Where answered |
|---|---|---|
| A | P1: the tolerance backstop is a stale-event detector (tolerance 1, four ticks), not a peek | Sections 7.2.2 and 7.2.6a |
| B | P2: an idle 50 ms request cannot show the clock; boundary-crossing intervals counted | Section 7.2.9: short-request phase observations, crossings discarded, a PERIODIC released-phase stimulus; legs T2 and T5 by windows, T5 with no stated expectation |
| C | P2: a child in D2 has no pended D-IRP | Leg T3: `XhciQemuSuspHoldDx`, the IRP shown outstanding, the ordered notes |
| D | P2: QEMU raises no Function Wake; no x64 test client | Section 7.2.6a and leg T8: Function Wake unread on QEMU, the x64 wakes owed |
| E | P3: WATCH creates a parked thread | Record 18 section 15.4 |
| F | P3: the header gate cannot show the target | Record 18 section 15.10: the claim limited; the build log's target line and library path checked |

Round 3 (`7dde5165`, same thread): 1 P1, 3 P2, 1 P3. A, D and E
confirmed closed; B, C and F reopened. All taken:

| # | Finding | Where answered |
|---|---|---|
| 1 | P1: `XhciIntervalCap` caps at 32 ms and cannot make a 2-5 ms probe | Section 7.2.9: an endpoint with a native short interval, named from the model's descriptors, the reading owed where none exists, acceptance needing a recorded PERIODIC period and a non-zero count |
| 2 | P2: equal phases at both ends miss an ABA and a transition during the drain | Section 7.2.9: the observation epoch, moved before and after every call, read at the DPC's entry and at its observation; vectors in section 7.2.10 |
| 3 | P2: the library gate rejected the CRT libraries subsystem 5.00 adds | Record 18 section 15.10: the CRT libraries enumerated and allowed; fixtures |
| 4 | P2: T3's injection cleared the slot; the stop's dump comes after the drain; unlink only hands off | Leg T3: the claim skipped before `hcdSuspTake`, the entry note, the completion worker's own note, the ordering rule |
| 5 | P3: stale "phase bins" in the test and integrator lists | Sections 7.2.10 and 7.2.11 |

Round 4 (`5ba0b6f2`, same thread): no P1, 3 P2. Round-3 findings 1, 3
and 5 confirmed closed; 2 and 4 reopened narrower. All taken:

| # | Finding | Where answered |
|---|---|---|
| 1 | P2: a rejected sample left the baseline stale, so counting could stop for good | Section 7.2.9: coherent samples seed or count and always become the baseline, incoherent ones invalidate it; vectors showing observation resumes |
| 2 | P2: T3's order did not prove the delay overlapped the outstanding IRP | Leg T3: call and return notes with sequence numbers, the call note before the delay, the required order with the completion inside the call |
| 3 | P2: the skip does not make the unlink the only settlement path | Leg T3: the claim narrowed to normal executor completion, the claiming path recorded, the stop-entry condition a reading that may be owed |

Round 5 (`9557fe74`, same thread): no P1, 1 P2, 1 P3. Round-4 findings 1
and 2 confirmed closed; 3 reopened narrowly. Both taken:

| # | Finding | Where answered |
|---|---|---|
| 1 | P2: a worker-only completion note misses the PDO close's inline completion | Leg T3: the note at `hcdSuspCompletePower`, for inline and worker callers, from metadata saved before the completion |
| 2 | P3: the recovery vectors added a seed after a coherent mismatch | Section 7.2.10: the incoherent and coherent cases split |

## 14. What changes in the documents

Task 36.20 (SUSP.3 until 2026-10-09, when Phase 36 took the cut that ships
polling): `implementation-invariants.md`,
"Interrupt Delivery" (a pin-0 controller polled, not refused);
`architecture.md`'s statement of line-based delivery; the release notes'
"Controller" row and a row for the switch; the acceptance test's step 3 and
its stop on the pin; `xhciqual/hardware-testing.md` and record 01's C4
note; record 13 where it describes the ISR; `source-files.md` for
`xhci_poll.c`; the locking record for `PollLock`, `PollService` and the
lock order; `xhcisnap/README.md` for the new companion lines; and the
phase's run record, named when it is opened.
