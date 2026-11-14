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

It was drawn up as Phase 35, and the revision history in section 13 keeps the
old phase numbers. Section 4.2 and decision 6 are the 2026-10-08 amendment
for issue 12. Section 4.3 and decision 7 add MSI on Vista and 7 (36.15).
That reverses section 1's "not in this design" for MSI on NT6.
Single-message MSI-X is admitted with it (owner, 2026-10-09); more than one
message or interrupter stays out.

Revision 4, 2026-10-05: written at the owner's request so the driver can
run a controller that delivers no legacy interrupt - an MSI- or MSI-X-only
xHCI controller, which neither primary target can serve today - by polling
the event ring, adaptively, instead of waiting for an interrupt. Revisions
1 to 3 were reviewed and revised the same day (section 13 maps each
change); revision 4 is the result.

**Revision 5, 2026-10-09 (task 36.5).** The issue 12 amendment made
concrete before code: the watcher of section 4.2 (its predicate, period,
count and window, and the move), WATCHING as a role in section 5, the
mode rules reconciled with 36.15's decisions (sections 1, 3, 4 and 11),
section 4.1 checked against the spec, the inputs and hooks of sections 6
and 7.1 named, and section 4.3 designed in full for 36.15, so the MSI code
has a reviewed design. Reviewed and revised in task 36.5 (section 13.5).

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

**Revision 8, 2026-10-13 (task 38.3, designed before code).** Section
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
and H2 at `e2c01854` (section 12, decision 9): `XhciPollIdleMs`
keeps its default of 50, measured against 500 by the legs (section
7.2.6a). **The code, 2026-10-14**: section 7.2.14, as built; its legs
are owed.

**Revision 10, 2026-11-11 (roadmap-usb4 task 49.24, designed before
code; a design only).** Section 4.5: `XhciInterruptMode` 4, the
controller's events through a message-signalled interrupt on the systems
that have none of their own - through the MSI stability improvements on
2000, XP and XP x64 - with the owner's decisions of 2026-11-10 and
2026-11-11 (section 12, decision 10). The 98 SE and ME message path is
not part of `3.5.0.0` (owner, 2026-11-13). Nothing of it is built: the value 4 still runs as 0,
and sections 3 and 8 describe the driver as it is. The helper's part is
record 18 section 15.16. Built and merged since (sections 4.5.16 to
4.5.18). Since 2026-11-13 the 98 SE and ME code is out of the tree: there
a stored `4` runs as `0` (automatic), as a reserved value does.

**Revision 11, 2026-11-11 (task 49.24, the design carried on; still a
design only).** The owner's answers of that afternoon to section
4.5.13's eleven open points are written into the sections they touch
(section 4.5.13 now lists them as decided), and section 4.5.15 is new.
The owner's answers
to the nine points that revision first left open are in as well
(section 4.5.13, second table).

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

## 2. State today (read 2026-10-05, and checked again on revision 1)

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
  lost-interrupt backstop (`hcd_ctl.c`, `HcdCtlBackstopTick`), a step of
  the controller thread's own under the power gate until task 36.55 moved
  it to the tolerance clock's DPC, peeks the event TRB at the
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

Value 4 is section 4.5's (task 49.24): a message-signalled interrupt on
2000, XP and XP x64. On 98 SE and ME it runs as 0, as a reserved value
does: the 98 SE and ME message path is not part of `3.5.0.0` (owner,
2026-11-13), and its code left the tree.

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

**`XhciMsiAvoid` (task 38.10, design record 20 sections 4.1 and 13).** A
further input of the decision: `XhciPollDecideEx`, which `XhciPollDecide`
calls with it 0, takes whether Linux's `XHCI_BROKEN_MSI` quirk applies to
this controller (Fresco Logic 1B73:1000 and 1B73:1400 at value 1, every
controller at 2). `hcdSettleDelivery` reads the value and the controller's
PCI identity, through the configuration service, before it decides
(`hcd_quirk.c`, `HcdQuirkMsiAvoid`). With it set, mode 0's message row
never connects the message: the delivery is a poll with the new reason 19,
`XHCI_POLL_R_MSI_AVOIDED` (`XHCISNAP`: "automatic: a message granted,
refused by XhciMsiAvoid (Linux's XHCI_BROKEN_MSI), so polled"), and the
requirements filter has already asked for the line on
such a controller where it could (record 18 section 4.2), so a granted line
takes mode 0's line rows. Modes 1, 2 and 3 answer as above; under mode 3
the user's message stands and the quirk is recorded as overridden.

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

**Who looks** (at `7567dec`; decided by the owner, 2026-10-09). The
backstop then ran on `hcdThread` (it is `HcdCtlBackstopTick`, in the
tolerance clock's DPC, since 36.55), and `hcdCommand` blocks
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
3. **The line is retired, with the ISR excluded** (revision 5).
   `LineMoved` is set to 1 in the extension
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
   goes on to its configuration-space step without touching MMIO.
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

   **A retirement owed is retired at every chance**.
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
      revocation no new obligation can appear behind the check.
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

**Timing against issue 12's boot-time no-slot refusal.** The start's No Op is submitted
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

**How each architecture reaches `IoConnectInterruptEx`** (task 36.5;
owner, 2026-10-09). There is no way for the one x86 binary to reach it
by itself that is safe without an import Windows 98 lacks: a walk of the
running kernel's memory cannot prove the pages it reads are mapped, and
a parse of the kernel's file, tied to the running image by the driver's
own imports, proves where those imports are but not where
`IoConnectInterruptEx` is (section 13.5). So:

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
(revision 5). Windows permits shared message
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

### 4.4 A line another device holds asserted, named at the start (revision 9, 2026-11-03; task 46.6)

**What it is for.** Task 45.33 (`runs/run-45.md`, "45.33") read on 98 SE
that a device holding a shared, level-triggered line asserted with no
driver to acknowledge it costs nothing while Windows keeps the line
masked, and storms as soon as any driver connects to the line; on the
reporter's board the system crawled once `xHCI98` connected to such a
line. The answer kept was documentation (`XhciInterruptMode` `2`,
`README.md`'s known issues). What this section adds is diagnostic only:
the start says which device holds its line, so a user's snapshot shows
the case and the device without a Device Manager hunt. Nothing else
changes: the delivery, the connection, the poller and the watcher are
section 4's and 4.2's, whatever the walk finds (no move to polling by
itself; owner, 2026-10-29, task 46.6's line).

**When.** At each start on 98 SE and ME (`IoIsWdmVersionAvailable(1,
0x10)` false) that was given an interrupt line (`HaveInterrupt`), after
`hcdSettleDelivery` and before `hcdConnect`, whatever the delivery
settled: under the line (`XhciInterruptMode` 0 or 1) a holder is read
before this controller's own connection unmasks the line; under a poll
(mode 2, the documented way round, or no line at all) nothing is
connected and the walk names the holder the user went round. Not on
2000 and later (the line runs under the I/O APIC or their own HAL, not
read in 45.33).

**The line compared.** This controller's own Interrupt Line, read from
its configuration space (the IRQ 98 SE and ME write there when they
route it), must name an IRQ of the PC's interrupt controllers, 1 to 15,
and must be the IRQ the start was given (the interrupt resource's raw
level); otherwise nothing is walked and the outcome is "not known" (an
`FFh` or a reserved value matched against another function's would
claim a sharing that is not there, PCI 2.3 section 6.2.4).

**The walk** (a pure function over a read callback, `XhciPollHeldScan`
in `xhci_poll.c`, host vectors): every bus 0 to 255 through
`HalGetBusData`, every device, function 0 and, when its header says
multi-function, functions 1 to 7; the first 64 bytes of each, read only.
A read that gives fewer than 64 bytes, or a vendor of `0` or `FFFFh`, is
an absent function; no bus is skipped on one absent read (whether 98 SE's
and ME's `HalGetBusData` answers an absent bus and an absent slot alike
is not read), and bus 0, device 0 answering nothing (every PC has its
host bridge there) is a walk that could not run. A function is **a
candidate** when it is not this controller (the four dwords
`hcdPirqQemuProbe` already identifies it by: id, BAR0, BAR1,
subsystem), its header type is 0 or 1, its Interrupt Pin is 1 to 4, its
Interrupt Line equals this controller's, its Status has Interrupt Status
(bit 3, PCI 2.3 section 6.2.3) set and its Command has Interrupt Disable
(bit 10) clear.

**The confirmation** (`hcd_ctl.c`, the caller's): a candidate is named
only when it still reads so at four more reads of its first 64 bytes,
each after a requested 10 ms wait (`KeDelayExecutionThread`,
PASSIVE_LEVEL). Interrupt Status is the state at each read, not a
record of what happened between them, so what is named is **a function
seen asserting the line at every one of five reads**: most likely a
holder (no driver acknowledges it), possibly a busy device that was
pending each time it was read; the log, the counters and `XHCITR` say
"seen asserting at every read", never more. The walk resumes past a
candidate not confirmed; the first confirmed ends it. At most eight
candidates are tried in one start: a ninth ends the walk "inconclusive",
an outcome of its own (the ninth, or a function after it, may be the
holder). The cost: one walk (8,192 reads of function 0 and the further
functions of multi-function devices) and at most eight confirmations of
four requested 10 ms waits each - 320 ms requested; the waits are
rounded up to the system's clock (up to 55 ms each on 98 SE), so the leg
reads the start's elapsed time (the log's notes before and after).

**What is recorded.** Six counter words appended at word 258 of
`XHCIHC_COUNTERS` (`xhci_counters.h`), written by the start before the
connection: `HeldScan` (0 not looked for, 1 none, 2 found, 3 not known:
bus 0 or this controller's own configuration could not be read, or its
line is not 1 to 15 or not the IRQ given, 4 inconclusive: eight
candidates tried, none confirmed),
`HeldFunctions` (functions read), `HeldCandidates` (candidates not
confirmed), `HeldBdf` (bus 15:8, device 7:3, function 2:0), `HeldId`
(vendor 15:0, device 31:16) and `HeldIrq` (the Interrupt Line compared);
the note `intr.held` with the outcome or the holder's bus, device and
function, and `intr.held.id` with its ids. The snapshot carries the
counter block whole (`XHCI_SNAPSHOT_HCD_COUNTER_WORDS` 264), and
`XHCITR`'s interrupt summary prints a line for it ("line held by
another"): not looked for, none (with the functions read and the IRQ),
the function seen asserting at every read (its bus, device, function,
ids and IRQ), inconclusive, or not known; a
block from a driver before it prints nothing there.

**What it decides wrongly, and what it does not see.** A device whose
own driver starts after this controller and that holds its interrupt
until then is named although it will be served (the note says what was
read, nothing acts on it). A function without Interrupt Status (PCI 2.3
and every PCI Express function have it), a holder whose Interrupt Line
does not carry the IRQ Windows routed, and a holder that asserts only
now and then (45.33's inference for the board) are not seen. A holder
appearing after the start is not seen either: the walk is once a start.

**Verification.** Host vectors for the walk (a bus answering nothing,
a populated bus whose device 0 is absent with a later device found, bus
0 device 0 answering nothing, multi-function headers, this controller
left out, pin 0 and pin 5, another line, Interrupt Status clear,
Interrupt Disable set, a type 2 header, a resumed walk past a
candidate, the confirmation's per-read test) and the counter layout.
Legs on 98 SE and ME (q35, 45.33's holder: a `pci-serial`
beside the `qemu-xhci` on its INTA, armed from `AUTOEXEC.BAT`): the
holder named by `XHCITR` (bus, device, function, `1B36:0002`, the IRQ)
under mode 2 and the start otherwise unchanged; under mode 0 the notes on
the `qemu` flavour's debug console before the storm 45.33 read (the line
connected, as before); without the holder, "none" with the functions
read; on 2000 or later, "not looked for"; on each, the start's elapsed
time with and without a holder.

### 4.5 Mode 4: a message through the MSI stability improvements (2000, XP, XP x64) (revision 10, 2026-11-11; roadmap-usb4 task 49.24; a design, not built)

**Status.** This section is the design, written before the code and
before the interface it uses is final (the roadmap takes
the MSI tasks last: 49.22 and 49.23 first, then this). Nothing of it is
in the driver: `XhciInterruptMode` `4` still runs as `0` (section 3), no
INF writes its values, and `docs/using/tuning.md` does not describe it.
Every statement about the interface is pinned to the state read
(the MSI header's branch `xirq-2` in its state A; section 4.5.11
lists what it depends on),
and is read again when the header is taken in; the code is built against
the MSI header's version 1 (read 2026-11-12), and section
4.5.16 (revision 12) says what that changes. It replaces the design
the parked branch `w48-mode4` carries as its section 4.5 (2000 and XP
only, the line borrowed only); section 4.5.12 says what of that branch
stands.

**What it is for.** Windows 2000, XP, Server 2003 and XP x64 have no
message-signalled interrupts of their own, so a controller there has
its line or a poll (sections 4 to 4.2). The MSI stability improvements,
one header compiled into the driver (below, the library), give a driver
a message on those systems. `XhciInterruptMode` `4` asks for the
message through it. The 98 SE and ME message path is not part of
`3.5.0.0` (owner, 2026-11-13).

#### 4.5.1 The decisions as they stand

The owner's, 2026-11-10 and 2026-11-11 (section 12, decision 10):

1. **Opt-in.** Mode 0 never picks 4, on any system; the INF keeps
   writing 0.
2. **No fallback**, as modes 1 and 3. A connection that fails or is
   refused fails the controller's start (Code 10), with the reason
   recorded (section 4.5.9). A connection lost while running - a tripped
   brake, a message that stops arriving, a re-arm that fails - fails the
   controller, logged; it never carries on by the line or by a poll.
3. **`XhciMsiStrategy`** says where the vector comes from on 2000, XP and
   XP x64, and no value turns into another: `0`, the default, claims a
   free ISA interrupt for the controller; `1` borrows the controller's
   own line, as the user's assertion that the line is private, with this
   driver's veto when it sees another device on the interrupt. `1` is
   never the default. A third source once numbered 2 is parked in
   the library and is not a value here.
4. **`XhciMsiIsaIrq`** forces the IRQ strategy 0 claims. The installer
   offers it beside mode 4 when strategy 0 is chosen: automatic, the
   default, or one of the seven IRQs (section 4.5.10).
5. **A shared claim** (two controllers of this driver on one claimed
   interrupt) is a user value, **`XhciMsiShare`**: `0` off, `2` to `8`
   on with that many controllers to a claim at most. Its default is on,
   at `2`, but only once the emulator legs and the owner's bench have
   passed; until then it is written `0`. The policy is "an interrupt of
   its own first, then share": a controller shares only when no
   interrupt of its own is left. Only this driver's own controllers
   share. **Superseded 2026-11-13** (the owner; section 4.5.19): sharing
   on by default, one IRQ filled up to `XhciMsiShare` controllers (1 to
   16, absent 8) before another is taken.
6. **`XhciMsiAvoid`** (design record 20 section 4.1) is obeyed: a
   controller it applies to is refused under mode 4. Mode 3 keeps its
   override.
7. **The quarantine.** A message that cannot be proven off at a teardown
   keeps its vector held and `xhci98.sys` loaded until Windows restarts,
   and that controller is refused at every later start, under any mode,
   until then. A controller that was unplugged and is plugged in again
   is admitted on 2000, XP and XP x64 when the library proved the
   function absent at its removal.
8. **98 SE and ME.** The 98 SE and ME message path is not part of
   `3.5.0.0` (owner, 2026-11-13).
9. **Licence and the signature.** The MSI header is MIT, and every
   client image carries the library's author-and-version signature
   string, with no macro to leave it out (section 4.5.10).
10. **The brake's numbers.** The library's own defaults stay 1000 unclaimed
    entries and a 500 ms listen; this driver passes its own values
    explicitly: 256 unclaimed entries, for the library's brake and for
    the count this driver keeps itself under strategy 1, and a 500 ms
    listen. The bench reading may move them.
11. **The watchdog and the No Op.** The stranded-event watchdog is the
    existing lost-interrupt backstop with a judge added, not a timer of
    its own: eight stranded events within 60 s fail the controller. The
    start's No Op is waited for by interrupt for 500 ms, on every system
    (section 4.5.6).
12. **A message lost while running** is reported to Windows as a failed
    device on 2000, XP and XP x64; which code Device Manager then shows
    is read on a leg, and it need not be Code 10 (section 4.5.4).
13. **The header is taken in once.** The MSI header is vendored only
    when its two lines (`dev-1` and `xirq-2`) are one (section
    4.5.11, item 2).
14. **Windows 2000, strategy 1:** no exclusive-device request is tried
    in the requirements filter. Strategy 0 is the path for a line that
    is shared.

Mode 4 is marked experimental wherever a user meets it (the owner,
2026-11-10). Recovery for the user: set `XhciInterruptMode` to another
value, then disable and enable the controller or restart; Last Known
Good on 2000 and XP if a start hangs.

#### 4.5.2 The values and the decision

| Value | Read | Meaning | Out of range |
|---|---|---|---|
| `XhciInterruptMode` `4` | each start (section 3) | the message of this section, on 2000, XP, Server 2003 and XP x64 (the 98 SE and ME message path is not part of `3.5.0.0`, owner, 2026-11-13) | `5` and above run as `0`, the value read recorded; on 98 SE and ME `4` runs as `0` as well (the system class is taken first) |
| `XhciMsiStrategy` | each start under mode 4, on 2000, XP and XP x64 | `0` a claimed ISA interrupt; `1` the controller's line borrowed | any other value refuses the start, reason 30 |
| `XhciMsiIsaIrq` | each start under mode 4 and strategy 0 | `0` the library's choice; `3` to `7`, `10` or `11` that IRQ and no other | any other value refuses the start, reason 30 (the library would refuse it as well; the driver decides first so the reason is its own) |
| `XhciMsiShare` (the sharing switch and the member cap in one value; since 2026-11-13 section 4.5.19's meaning) | each start under mode 4 and strategy 0 | the most controllers on one claimed IRQ before another is taken: `1` every controller an IRQ of its own; `2` to `16` that many on one IRQ, the fullest line joined first; absent `8` | `0` (not offered) runs as `1`, above `16` as `16`, the value read recorded |
| `XhciMsiAvoid` | each start (record 20) | a matched controller is refused under mode 4 | as record 20 |

Under the registry rule each
value is written by both INFs on every install path at its default with
the don't-overwrite flag, with its INF-gate row and its `tuning.md` row,
when the code lands; `XhciMsiShare` was written at `0` until decision 5's
condition was met, and since 2026-11-13 is written at `8` (section
4.5.19). The values of the second
claim pool (section 4.5.11, item 11), which now exists, are section
4.5.16 item 1's: read on 32-bit 2000 and XP only, written by the x86 INF
at their defaults, the pool off unless a user sets it.

**The decision** (pure, `XhciPollDecideEx` and a mode-4 step of its own,
host vectors). Under the effective mode 4, in this order, the first row
that applies:

| Inputs | Delivery | Reason |
|---|---|---|
| Vista or later (`IoIsWdmVersionAvailable(6, 0)`) | none | 22 `MSG4_NO_SYSTEM` |
| this function entered in the image's refusal table this boot (section 4.5.4) | none | 27 `MSG4_QUARANTINED` |
| `XhciMsiAvoid` applies | none | 24 `MSG4_AVOIDED` |
| 2000, XP, XP x64: `XhciMsiStrategy` neither 0 nor 1, or under strategy 0 an `XhciMsiIsaIrq` outside its set | none | 30 `MSG4_VALUE_INVALID` |
| strategy 1: no interrupt resource, a message resource, or Interrupt Pin read 0 | none | 23 `MSG4_NO_LINE`, "no line to borrow" |
| otherwise | a message, its capability not read yet (`XHCI_POLL_DELIVERY_MESSAGE`) | 21 `FORCED_MSG4` |

The second row is not mode 4's alone: it stands at every start on these
systems, whatever the mode (section 4.5.4, "The refusal table"). The
connection then either stands (the delivery becomes
`XHCI_POLL_DELIVERY_MSI` or `_MSIX`, reason 21 kept) or refuses the
start with one of:

| Reason | When |
|---|---|
| 25 `MSG4_SHARED` | strategy 1: the veto found another device on the interrupt, or could not tell |
| 26 `MSG4_REFUSED` | the connect returned anything but `OK` (the library's status kept in `MsiConnectStatus`), the library could not be initialised (x86: the helper's routine absent), or the function's PCI location could not be established |
| 31 `MSG4_NO_INTERRUPT` | connected, but the start's No Op did not complete through the message (section 4.5.6) |
| 32 (reserved) | not produced: it was the 98 SE and ME path's, which is not part of `3.5.0.0` (owner, 2026-11-13); the number is kept, as 28 and 29 are |
| 33 `MSG4_NO_IRQ` | strategy 0: the claim ended with `MSGIRQ_STATUS_NO_VECTORS` - no free ISA interrupt, the forced one refused, or with sharing on every claim that could be joined full |
| 34 `MSG4_CLAIM_REFUSED` | strategy 0: the claim returned any other status (kept in `MsiConnectStatus`) |

Reasons 28 and 29 belonged to builds of the parked branch (a build with
no strategy, and "strategy 0 not available"); they were never released
and are not produced. These are
the start's reasons only: what ends a delivery after the start is
recorded apart (`Msg4Lost`, section 4.5.9), so a snapshot still says how
the start was decided. An unreadable Interrupt Pin is let through under
strategy 1: a configuration space that cannot be read fails the connect
with its own status. On Windows 2000 on the boards of record 18 section
16.4 (`hc->NoRouteStrip`) no interrupt is asked for, so strategy 1
refuses there (reason 23) and strategy 0 does not need one.

**The requirements filters** (record 18 sections 4.2 and 16). Under
strategy 1 mode 4 asks for what mode 1 asks: the line. Under strategy 0
the start needs no interrupt resource at all - the
message's vector is not the device's resource - so the filter does what
it does under mode 0 on that system, and a line Windows assigns anyway
is left unconnected. The core's own gate follows: today
`XhciInitController` waives the interrupt resource for a poll delivery
alone (the test on `ResourcesTypes` against `XhciResourcesRequired`),
and it waives it as well for a message connected under mode 4 by
strategy 0. The resource mask itself stays what Windows
gave - no interrupt bit is set for a resource that was not there. On
Vista and later the question does not arise: the start refuses.

#### 4.5.3 2000, XP and XP x64: the connection through the MSI stability improvements

At `IRP_MN_START_DEVICE` after the lower drivers, from `hcdConnect`,
where mode 1 would connect the line, with the readiness flag clear
(section 4.5.5) and the controller's causes off. That last is
established, not assumed: `hcdConnect` runs before `XhciInitController`,
whose firmware handoff and HCRST have therefore not happened, and a
controller the firmware still runs need not be masked. So mode 4, on
every system, takes a step of its own before the claim, the listen and
the connect (**the quiet step**). Nothing of the controller is decoded
at that point - the start has just zeroed the extension - so the step
begins with the capability decode and its validation that
`XhciInitController` makes at its own beginning (the same routine, its
result kept; the later call does not decode twice): the handoff needs
the extended-capability offset and the mask needs the operational and
runtime offsets, and neither is attempted on offsets not validated.
Then the firmware handoff that `XhciInitController` makes is made (the
same routine; the later call finds the controller owned), then
`USBCMD.INTE` and interrupter 0's `IMAN.IE` are cleared and read back
clear. A decode that is refused, a handoff that fails or a mask that
does not read back refuses the start (reason 26, a status of this
driver's own) with nothing claimed or connected.
The start's HCRST comes later and enables nothing. The seam is one
file,
`hcd_msgnt.c` (section 4.5.16 item 6), the only one that compiles the
header's implementation.
In order, the first refusal ending the start:

1. **The library, once per image, at the first start that can resolve.**
   The library asks for `MsgIrqInitialize` once from `DriverEntry`. This
   driver cannot call it there, and says so as a deliberate exception:
   in state A the call marks the library initialised before it
   resolves its routines, and every later call returns at once, so a
   call made before the resolver can answer would leave the library
   without its routines for the image's life; and on x86 the resolver is
   the helper's routine, which a first install has only from the next
   boot (record 18 section 15.16). So the call is made exactly once per
   image, under a driver-global event, at the first mode-4 start on
   these systems at which the resolver is in hand - on x86 after the
   one-shot look of record 18 section 15.16 returned the routine, on
   amd64 always. Without the routine the start is refused (reason 26,
   the status recorded as `MSGIRQ_STATUS_UNSUPPORTED`, this driver's
   own) and the library is left uninitialised; nothing latches, so a
   later start looks again. The host table: `Allocate` and `Free` are
   `hcd_pool.c`'s non-paged site; `Resolve` is `HcdMsiFindRoutine`
   (`hcd_msi86.c`), whose context is the kernel's own
   `MmGetSystemRoutineAddress` and needs no reference to the helper
   after the look. A name the kernel does not answer leaves the library
   feature that needs it unsupported for the boot (the claim then
   refuses, reason 34). Once the call has run the image is kept for the
   boot (`DriverUnload` cleared under the boot ledger's lock, as a
   quarantine does): the library maps the local APIC for the image's
   life and has no call to end it.
2. **The library's mode.** `MSGIRQ_MODE_FAKE_IRQ` goes on; `NONE` is
   left to the claim's or the connect's status, which names why
   (`MSGIRQ_STATUS_HAL_UNSUPPORTED`: a PIC-mode HAL, or a local APIC in
   x2APIC mode).
3. **The vector's source**, by `XhciMsiStrategy`.

   **Strategy 0, the claim** (the library's design record 05 sections
   2.2 to 2.5 and 8). `MsgIrqIsaClaim(&Params, &Options, &Result)` in the PnP
   IRP's thread, with `Params` carrying the function's bus, device and
   function, and `Options`: `Size`; `DriverObject` this driver's;
   `Flags` `MSGIRQ_ISA_FLAG_OVERRIDE` with `CandidateCount` 1 and the
   one candidate when `XhciMsiIsaIrq` is not 0, and
   `MSGIRQ_ISA_FLAG_SHARE` when `XhciMsiShare` is not 0;
   `QuietMilliseconds` 500 and `BrakeEntries` 256, this driver's own
   numbers (decision 10); `Exclude` 0; `MaxMembers` the value of
   `XhciMsiShare` as it runs (2 to 8; 2 when it is 0, where the library
   wants a number in its range and no claim is joined; since 2026-11-13
   section 4.5.19's: `SHARE` with `JOIN_FIRST`, the cap 2 to 16, and with
   sharing off no flag and a cap of 0, the library's default). `Result` is logged
   whole whatever the status (section 4.5.9). The library picks, in its
   order 10, 11, 5, 7, 6, 3, 4, the first IRQ that no source of its
   picker excludes, that the PnP manager's arbiter grants and that stays
   quiet for the listen; with the flag and no IRQ of its own left it
   joins a claim another controller of this driver made with the flag,
   on an IRQ its own picker found nothing against but the arbiter's
   conflict, with room under both caps. The listen holds the start for
   up to the listen time per candidate tried (seven candidates at
   most; with the second pool chosen the upper inputs in scope are tried
   too, after the ISA IRQs and before a join, section 4.5.16 item 1). `OK`
   goes on to the connect with
   `MSGIRQ_CONNECT_FLAG_ISA_LINE` and an all-zero `LineInterrupt`;
   `NO_VECTORS` refuses with reason 33; `CLAIM_KEPT` refuses with reason
   34 and enters the function in the refusal table; any other status
   refuses with reason 34. The controller's own line, where it has one,
   is not connected and not asked about.

   **Strategy 1, the line borrowed.** The veto first: the PnP manager's
   resource map (`\Registry\Machine\HARDWARE\RESOURCEMAP\PnP
   Manager\PnpManager`, every `.Translated` value but this controller's
   own, walked by the bounded pure `XhciMsg4ShareScan`) and, when the
   map is clear, every present PCI function read through
   `HalGetBusData` (`HcdCtlLineShared`: Interrupt Pin 1 to 4, Interrupt
   Disable clear, Interrupt Line this controller's). Another device on
   the interrupt, a map or a walk that cannot be read, or a line
   register of this controller's that reads 0 or 255 refuses, reason 25:
   the veto does not pass by default. Then the connect with
   `MSGIRQ_CONNECT_FLAG_PIN_PRIVATE` and `LineInterrupt` from the
   translated descriptor (`Vector`, `Level`, `Affinity`, the mode from
   `Flags`, and its `ShareDisposition` as given). The veto's limits are
   the parked design's and stand: it does not see a device whose
   firmware routes it to the line under another Interrupt Line value,
   nor one assigned the interrupt after this start.
4. **The connect.** `MSGIRQ_CONNECT_PARAMS`: the PDO; `Bus`, `Dev`,
   `Func` from `IoGetDeviceProperty(DevicePropertyBusNumber,
   DevicePropertyAddress)`; `ConfigAccess` over `HcdSvcConfigBytes`
   (`hcd_svc.c`: the `IRP_MN_READ_CONFIG` and `IRP_MN_WRITE_CONFIG`
   route, `PASSIVE_LEVEL`, answering the bytes the PCI driver reports
   moved); `MSGIRQ_MESSAGE_ANY`, one message; the flag of step 3 and no
   other (the library refuses the two together); the mode-4 ISR of
   section 4.5.5 (`__stdcall`) with the controller as its context;
   `SpinLock` NULL (so a quarantine keeps no lock of this driver's
   alive), `SynchronizeIrql` 0, `FloatingSave` `FALSE`.

**The outcome.** `MSGIRQ_STATUS_OK`: the info block is kept
(`hc->Msg4Info`); `hc->Interrupt` is its `MessageInfo[0].InterruptObject`,
so every `KeSynchronizeExecution` caller has a real interrupt object;
the delivery becomes the capability read from configuration space as for
mode 3; `IntDelivery` becomes the message's (so task 36.3's route repair
leaves it alone); the readiness flag is set and the interrupter enabled
at the end of the start as for a message, with no write of this driver's
to the Command register (below); then the No Op of section 4.5.6. The
poller is not opened: no watcher under a message (section 4.1). Any
other status refuses the start, reason 26, the status kept.

**What a failed start holds, and how it is given back.** The claim and
the connection are held apart, and `hcdRelease` knows three states - a
claim with no connection, a live connection, neither - so each failure
has one way out:

| The start failed at | Held | Given back by |
|---|---|---|
| the decision, the library, the veto, or a claim that returned `NO_VECTORS`, `HAL_UNSUPPORTED`, `UNSUPPORTED`, `NO_MEMORY` or `INVALID_PARAMETER` | nothing | - |
| a claim that returned `CLAIM_KEPT` (an earlier claim of the function's kept, or this claim's own withdrawal failed) | a kept claim, the library's for the boot | nothing: no release and no second try; the function entered in the refusal table, the image kept |
| a claim that returned `IN_USE` | whatever this driver already held for the function: its record is left as it was, not overwritten | the release that record's own lifetime runs; the start fails as a fault of this driver's (reason 34) |
| the connect, no info block returned | strategy 0: the claim | `MsgIrqIsaRelease`; `CLAIM_KEPT` from it enters the function in the refusal table and keeps the image |
| the connect, `MSGIRQ_STATUS_CONFIG_ACCESS` with an info block (a live connection whose MSI could not be confirmed off) | the connection, and under strategy 0 the claim | the release of section 4.5.4, before the start returns: disconnect, once more, then the claim's release; or the quarantine, which keeps the claim with its vector |
| the No Op by interrupt, or any later step of the start | the connection, and under strategy 0 the claim | `hcdStopBody`, as a failed thread start is stopped: the same release |

**The Command register is the library's while a connection stands.** In
state A the arm (at connect and at every re-arm) sets Bus Master
Enable where it reads clear, sets Interrupt Disable where the function
implements it, reads the register back, and fails
(`MSGIRQ_STATUS_CONFIG_ACCESS`) when Bus Master Enable reads clear or
Interrupt Disable reads clear on a function that has it; the disarm
clears Interrupt Disable again. So between a successful connect or
re-arm and the disconnect this driver writes neither bit - no
`hcdIntxEnable`, no route repair, no resume step that restores an older
Command value after the re-arm - with one exception, the quiesce's clear
of Bus Master Enable before a quarantine. A start on the line after a
clean disconnect finds Interrupt Disable clear, as the library left it
(in state A; in the one line the bit stays set and the later start
clears it, section 4.5.16 item 5).
And because a disconnect gives the line back, **the interrupter stays
off from before every disconnect until a connection stands again** (the
bar on re-enabling, section 4.5.4): an interrupter enabled in between
could raise INTx on a line nobody serves.

#### 4.5.4 2000, XP and XP x64: the lifetime

**The claim's lifetime is not the connection's** (strategy 0). The claim
is made at the start, kept across a query-stop and its cancel, and
released at the stop itself and at a removal, each time only after the
disconnect returned `OK`. Claim and release run in the PnP IRP's thread:
the library requires the system process's context for both, and the
start, stop and removal IRPs arrive in it (the library's record 05
section 2.6; section 4.5.11, item 6).

**A start on a started controller.** The library's template for this case
(keep the claim and the connection, re-arm) is not taken. The HCD's own
rule is kept deliberately (`hcd_pnp.c`, `hcdStart`): such a start is not
served; the running lifetime is stopped as a surprise removal stops it
and the start fails, unforwarded. Under mode 4 that stop is the release
below, quarantine admitted, so no second claim is ever made over a live
one (`MSGIRQ_STATUS_IN_USE` from a claim is therefore a fault of this
driver's and refuses, reason 34).

**The bar on re-enabling, and the mask's proof.** Two things are kept
apart. The proof is the hardware's: `InterruptDeliverySuppressed`, set
only when a mask's read-back shows the interrupter off, and never
written on any other ground. The bar is this driver's own state
(`hc->Msg4Barred`, with `XHCI_EXT_FLAG_INTERRUPTS` cleared), set under
the controller lock by whoever takes delivery away for a mode-4
transition, and lifted only by the step that has a connection standing
and armed again. While the bar stands: the drain re-arms no interrupter
(the DPC calls `XhciEventDpc` with its re-arm off, as under a poll); no
submission's path and no resume enables one; and the in-place recovery
is deferred, left requested and not charged. An ordinary disconnect -
one that turns the message off and gives the line back - needs both,
the bar and the controller proven silent: the mask's proof, or a
controller that `XhciStopController` read back halted. The quarantine
is the exception, because it writes nothing to the device and gives no
line back. The bar stands from the query-stop's mask to the
cancel's reconnect or the stop, from a failed re-arm on, and for good
once the connection is lost - there whether or not a mask could be
written or proven.

**The release (`hcdRelease`).** Every final teardown - the stop that
follows no released query, the surprise removal, the removal that found
the controller started, a refused start after the connect, the stop of a
controller started twice - runs after the controller thread has been
stopped and joined, after `XhciDisableInterrupts` and
`XhciStopController` where the start reached them (the causes masked)
and after the readiness flag is cleared, and before the DPC drain:

1. Only with the controller proven silent (the mask's proof, or the
   stop read back as halted): `MsgIrqDisconnect(Info, 0)`. `OK`: the
   connection is gone. Without that proof - an unplugged controller
   gives none, and a present one whose mask did not take could raise
   INTx once the message is off - no plain disconnect is made. **At a
   surprise removal and at a removal, and only there** (decision 7),
   and only for a function this driver itself read as implementing
   Interrupt Disable (the Command register read back with the bit set
   after the connect's arm, noted then), the
   call is `MsgIrqDisconnect(Info, MSGIRQ_DISCONNECT_ABSENT_OK)`
   instead: `OK` from it means the library either confirmed the message
   off or proved the function absent, and the connection is gone with
   nothing held - no quarantine, no entry in the refusal table, the
   claim released by step 4 - so the same controller plugged in again
   starts. At any other teardown without the proof, and at a removal of
   a function without Interrupt Disable, steps 1 and 2 are skipped and
   step 3 is taken directly.
2. `MSGIRQ_STATUS_CONFIG_ACCESS`: a requested 10 ms wait, and once more
   (with the same flag where step 1 passed it).
3. Still not: Bus Master Enable cleared through the same configuration
   route (read, clear bit 2, write; a failure noted, not fatal), then
   `MsgIrqDisconnect(Info, MSGIRQ_DISCONNECT_QUARANTINE)`, which always
   returns `OK`; the function entered in the refusal table; the image
   kept; the event logged and counted. A function the library could
   not prove absent ends here: one whose configuration space merely
   reads all ones is not proven silent.

   The absence flag is the library's branch `dev-1`'s and is not in
   state A (section 4.5.11, item 2). Because the header is vendored
   only once the two lines are one (decision 13), no build of this
   driver exists without it, and the code is written once, with the
   flag. Passing it for a function that is in fact present and not
   proven masked lets the library turn that function's message off
   (in state B the flag does not stop the ordinary disarm of a
   function that answers). What such a function could then raise on
   its line is answered in `dev-1` by Interrupt Disable kept set at the
   disable - but only where the function has the bit, which the
   library does not require; hence this driver's own condition above.
   A function without the bit, unplugged or not, is quarantined at its
   removal as before decision 7, and is refused until the restart.
   Whether the one line keeps Interrupt Disable set is one of the two
   things read again when the lines are one; if it does not, the
   condition cannot be met and every such removal quarantines.
4. Strategy 0, with a claim held and no quarantine in step 3:
   `MsgIrqIsaRelease`. `OK`: the IRQ is free again (or, on a shared
   claim, the membership ended; the claim goes with its last member).
   `CLAIM_KEPT` - the claim was kept by a quarantine of another member,
   or its release failed: the function entered in the refusal table and
   the image kept. After a quarantine in step 3 no release is called:
   the claim is kept with its vector.

`hc->Interrupt` and `hc->Msg4Info` are NULL after either. The DPC drain
that follows waits out what the ISR queued before the disconnect.

**The refusal table.** A table of the image's own (eight functions by
bus, device and function, under its event; a full table, or a location
never read, sets a latch that refuses every start on these systems for
the boot). A function is entered when its connection was quarantined or
its claim kept, and the image is kept with it, so the table outlives the
function's device object. The check runs **at every start, whatever the
mode**, right after the resources are parsed and before anything is
mapped or connected: the function's MSI was never proven off and an
HCRST does not clear configuration space, so no mode may start it again
this boot - not 0, whose line connect would meet the held vector and
fall back to a poll, not 1 or 2. The delivery is then none with reason
27. The table is empty unless a quarantine happened, so the location is
read only then. This is decision 7.

**The stop and its query** (the library's integration notes for WDM
drivers and for this driver). A stop must release the interrupt, and a
release that cannot prove the MSI off may not be quarantined there (the
PnP manager would hand the resources on), so under a connected mode 4
the release moves to `IRP_MN_QUERY_STOP_DEVICE`, where it can still be
refused (`HcdMsg4QueryStop`). The query and the cancel each hold the
power gate (`HcdPowerGateEnter`) across the mask, the library's calls
and the state they publish, so no suspend, resume or recovery runs
between them. A query that finds the connection lost (below) is
refused (`STATUS_UNSUCCESSFUL`, completed here): a stop may only follow
a query that released the interrupt, a lost connection is still a
connected one whose message was never proven off, and the removal that
the failure report brings is where it is released or quarantined.

1. The bar set and the causes masked (`XhciDisableInterrupts` at
   `DISPATCH_LEVEL`). Only when the mask's read-back proves it
   (`InterruptDeliverySuppressed`) is the readiness flag cleared and the
   next step taken. A mask that is not proven disconnects nothing: the
   bar is lifted, the connection stays as it was, the interrupter is
   asked on again as after a drain, and the query fails
   (`STATUS_UNSUCCESSFUL`, completed here). It is not a lost
   connection: the message still arrives.
2. `MsgIrqDisconnect(Info, 0)`, retried once after 10 ms on
   `MSGIRQ_STATUS_CONFIG_ACCESS`. `OK`: `hc->Msg4Info` and
   `hc->Interrupt` NULL, `hc->Msg4QueryReleased` set. **The claim is
   kept.** The DPC is not closed; until the stop or the cancel the ring
   is drained only by the watchdog's detector (section 4.5.6), whose
   drains in this state are not counted against the controller. The
   query is then sent down **and waited for**: if a lower driver fails
   it, the reconnect below is made at once, before the query's failure
   is returned, and the cancel Windows may still send finds a connection
   standing and does nothing.
3. Still `MSGIRQ_STATUS_CONFIG_ACCESS`: never quarantined here. The
   connection stays live; `MsgIrqReArm`; on `OK` the flag is set, the
   bar lifted and the interrupter turned on again; on failure
   the connection is lost (below). Either way the query fails,
   `STATUS_UNSUCCESSFUL`, completed here and not passed down.

`IRP_MN_CANCEL_STOP_DEVICE` after a query that released the connection
connects again as at start with this start's resources - strategy 0 on
the claim it still holds, with `MSGIRQ_CONNECT_FLAG_ISA_LINE` and no new
claim; strategy 1 with the veto again - and then, in this order, sets
the flag, lifts the bar and turns the interrupter on, and only
then issues a fresh No Op and waits for it by interrupt (section
4.5.6); `hc->Msg4QueryReleased` is cleared. A refusal is no fallback
case: the connection is lost (below), and a refusal that returned a
live connection keeps it in `hc->Msg4Info` for the release. The cancel
itself succeeds, as Windows requires. `IRP_MN_STOP_DEVICE` after a
released query has no connection left: `hcdRelease` finds `hc->Msg4Info`
NULL and, under strategy 0, releases the claim (step 4 above). A
query-remove and its cancel change nothing: the removal releases, and
may quarantine, since a removed device's resources are not handed on
while its vector is held.

**Surprise removal.** The readiness flag is cleared first, under the
interrupt object's lock and without touching the controller (on a shared
claim the ISR is still entered for the other member's messages while
this controller's registers are gone); then the release above.

**Power.** Before the controller leaves D0 the causes are masked (the
suspend does it) and the readiness flag cleared. `XhciResumeController`
turns the interrupter on itself, on its restore path and on its
reinitialisation path alike, and only under `XHCI_EXT_FLAG_INTERRUPTS`.
So every path that reaches `hcdResume` with a live mode-4 connection -
the device D0's completion, the direct transition, and the clear of a
suspend that left the function in D0 (`hc->SuspendedInD0`) - first runs
one step, at `PASSIVE_LEVEL` under the power gate, in the power work
item: `hcdD0Done` defers the D0 to it whenever a mode-4 connection is
live, even at `PASSIVE_LEVEL` (the library binds and reverts the calling
thread's affinity, which a borrowed completion thread must not see).
The step: whatever this driver restores of the function's configuration
space on a resume, first; then `MsgIrqReArm`, once more on a failure;
`OK` - the flag set, then the resume, which enables the interrupter.
Still failing - `XHCI_EXT_FLAG_INTERRUPTS` cleared first, the resume
still run (it is what clears `XHCI_EXT_FLAG_SUSPENDED`), and the
connection lost (below). The suspend that left the function in D0
re-arms as well: the call is idempotent, and the flag was cleared on the
way down. `MSGIRQ_STATUS_BRAKED` from a re-arm writes nothing and is
the brake's case (section 4.5.6). Members of a shared claim resume in
whatever order Windows powers them; each re-arms its own function and
opens its own flag, and none waits for another. A shutdown changes
nothing on these systems: the D3 suspends and the connection is left as
it is. The in-place recovery's HCRST does not touch configuration space
and needs no re-arm of its own.

**A connection lost while running** (decision 2). Four things end
delivery on a started controller: a tripped brake, the watchdog's limit
(both section 4.5.6), a re-arm that failed twice (at D0, or at a refused
query), and a reconnect refused at a cancel. One routine serves all
four, `HcdMsg4Lost(cause)`, in two halves, acting once:

1. **The latch**, callable up to `DISPATCH_LEVEL` (the drain's brake
   latch and the watchdog's judge run in DPCs). Under the controller
   lock, in one hold: `hc->Msg4Lost` takes the cause and
   `ControllerFailed` is set with it, so the existing admissions that
   read that field (the drain's, the enumeration's) close at the same
   moment; no recovery is requested; the bar on re-enabling is set.
   Where the controller is reachable - started, in D0, not suspended,
   its registers mapped - the interrupter is masked as well, and the
   proof is whatever the read-back gives; where it is not (a brake
   found tripped after the controller went to D3), no register is
   touched and the bar alone stands. This half takes no interrupt
   object and makes no library call, so a DPC still in flight when a
   disconnect runs meets nothing that the disconnect frees.
2. **The rest, on the controller thread**, which the latch wakes: at
   `PASSIVE_LEVEL` under the power gate, with the connection read under
   it, the readiness flag is cleared under the ISR's exclusion (until
   then the routine may still claim an event and queue a drain, which
   re-arms nothing under the bar); then step 5. A stop or a removal
   that joins the thread first loses nothing: the release clears the
   flag itself.
3. The cause logged with its counters (section 4.5.9).
4. **Terminal at once.** The latch is an input of the terminal verdict
   (`XhciTolTerminal`, and `XhciTolTerminalRelease` admits the new
   verdict beside its two), so the thread's next pass runs the terminal
   release (`hcdTerminalRelease`) with no recovery sequence run. That
   release keeps its own rule: where the controller cannot be proven to
   have stopped mastering, transfers and their mappings are retained,
   not completed. While the latch stands the recovery's admission
   refuses, uncharged; every D0 path re-arms nothing and enables nothing; a query-stop is
   refused and a cancel reconnects nothing. Nothing opens the
   poller. The parked design's exception - the recovery's gate trying
   the re-arm again - is dropped: a re-arm that failed twice is a lost
   connection like the others.
5. **Windows is told.** `IoInvalidateDeviceState` on the PDO, and the
   FDO answers `IRP_MN_QUERY_PNP_DEVICE_STATE` with `PNP_DEVICE_FAILED`
   while the latch stands. The removal Windows then sends runs the
   ordinary release in the PnP IRP's thread: the disconnect, the claim's
   release, or the quarantine. The routine is not imported today; on
   x86 it is resolved by name through the helper's routine (2000 and XP
   only, which mode 4 needs there anyway), on amd64 at the image's one
   `MmGetSystemRoutineAddress` site, in both at the start, before the
   connection is made: a start that cannot resolve it is refused
   (reason 26), so no mode-4 lifetime runs without its way to report a
   failure. Until that removal the connection
   and the claim stand, masked: the library's calls stay in the PnP
   IRPs' threads.

That report is the owner's decision for 2000, XP and XP x64 (decision
12). What Device Manager then shows for a controller that failed after
its start is not Code 10's case in Windows' own terms: the code is read
on a leg and recorded, and no text of this project promises Code 10
for it. The next start of that controller is not refused on this ground
(the option of refusing it until a restart was not taken). A later start of the same controller - the
user's disable and enable - is a fresh start: the latch is the
lifetime's, not the boot's, and only the refusal table outlives it.

#### 4.5.5 The ISR and its readiness flag

A claim made with the sharing flag is connected shareable from its first
member on, so the message routine is
entered for interrupts that are not this controller's, at any time
between its connect and its disconnect - before the start has finished,
in D3, between a resume and the re-arm, after a surprise removal. So
under mode 4, on every system and under either strategy, the routine is
gated:

- **The flag** (`hc->Msg4Ready`) says "the controller's registers are
  reachable and its message is armed". It is written only under the
  ISR's own exclusion: `KeSynchronizeExecution` on the connection's
  interrupt object; and only at `PASSIVE_LEVEL` by a path serialised with the
  disconnect (a PnP IRP's thread, the power work item, or the controller
  thread under the power gate), never from a DPC, so no writer can hold
  an interrupt object that a disconnect is freeing.
- **The order is this driver's to keep.** Clear before the connect is
  called (the routine can be entered before the connect returns). Set
  after a successful connect or re-arm, with the controller in D0 and
  its registers mapped, and only then are the causes unmasked. Before
  the registers stop being reachable - a stop, a query-stop's release, a
  removal, the way to D3 - the causes are
  masked, then the flag cleared, and only then is the controller let
  go. A start or a re-arm that fails leaves it clear. At a surprise
  removal it is cleared first, with no register access.
- **The routine** (`hcdMsg4Isr`, message 0 only): flag clear - `FALSE`,
  nothing touched. Flag set: `USBSTS` read; all ones (a controller that
  vanished before any notice) - `FALSE`; else the line ISR's body, which
  claims on `USBSTS.EINT`, acknowledges, queues the DPC and returns
  `TRUE`, or returns `FALSE` with nothing of its own pending. It
  touches the controller's registers, its own non-paged state by
  interlocked operations, and `KeInsertQueueDpc`, and nothing else.

Under a message `IMAN.IP` clears itself when the message write completes
(xHCI 1.2c 4.17.5), so the claim is `USBSTS.EINT`'s, as the line's is:
an entry that finds `EINT` clear is "nothing of mine", which is what the
library's brake counts on a claimed interrupt and what ends the kernel's
pass on a shared one.

#### 4.5.6 The brake, the watchdog and the No Op by interrupt

**The brake.** Whose brake it is follows from who can tell a stray entry
from a neighbour's:

- **Strategy 0: the library's** (its record 05 section 2.7), per
  claim. It counts the run of entries no member claimed and trips at
  `BrakeEntries` times the members; once tripped it stops calling every
  member, writes nothing, schedules nothing and tells nobody. A trip
  can come with no event of this controller's standing, so this driver
  looks for it on its own clock: the controller thread calls
  `MsgIrqIsaQuery` (`Size` the grown structure's, so the member's own
  counts come back) when a second has passed since its last look, by
  the tolerance clock and not by a count of passes, and at once after a
  stranded event - the backstop's DPC cannot make the call itself, so
  it marks the look owed and wakes the thread. The look is admitted as
  the backstop is (started, in D0, not suspended, no transition under
  way); one owed while the controller is out of D0 is made after the
  next re-arm. It holds the power gate and reads `hc->Msg4Info` under
  it; the block is consumed only
  under the same gate (the query-stop's release) or after the thread is
  joined (the final release), so no look meets a freed block. `Braked` set is a connection lost (cause 1), the claim's
  and the member's counters logged; so is `MSGIRQ_STATUS_BRAKED` from a
  re-arm. On a shared claim one trip stops both members, and each fails
  by its own look. This driver keeps no stray count of its own here: on
  a shared vector an unclaimed entry is the ordinary case.
- **Strategy 1: this driver's own**, as the parked design has it (a
  borrowed line has no claim and so no library brake): an entry not
  claimed that finds no event at the ring's dequeue position is a stray
  one; `XHCI_MSG4_STRAY_LIMIT`, 256 (decision 10: the number passed to
  the library as well), in a row (`XhciMsg4StrayStep`, pure) trip
  it, latched in the drain, which calls `HcdMsg4Lost` (cause 1). The
  line is exclusive, so every entry is this controller's or nobody's.

A brake bounds the work per stray entry; it does not silence a source,
and a source fast enough to hold the processor at the device's IRQL is
answered by nothing in the library or here (its record 05 section 2.7,
"What the brake does not do").

**The watchdog for a stranded event.** An event standing on the ring
with no interrupt after it is what every failure of the message looks
like from the controller's side: a tripped brake, a message lost between
two members' edges. The detector exists:
the lost-interrupt backstop of record 17 section 4.1, per controller, in
the tolerance clock's DPC, which queues the drain for an event that has
stood unserved for its interval (100 ms, in practice 300 to 400 ms after
the event) and counts `BackstopDrains`. Mode 4 adds the judge, not a
second timer:

- **It does not depend on `XhciTolerance`.** Today the backstop's step
  is off when that value is 0. Under a connected mode 4 the caller
  passes it as on, and the tolerance clock runs for the controller
  whatever the value says: a user who turned the tolerance off has not
  asked for an unwatched message.
- Each backstop drain under a connected mode 4 with the flag set is a
  **stranded event**: counted (`Msg4Stranded`), noted with the time, and
  under strategy 0 followed by the brake look above.
- **The backstop is not the only drain that could hide a dead message.**
  A command's wait and a control transfer's wait drain the ring
  themselves (`hcd_enum.c`, through `hcd_ctl.c`'s drain for a waiter),
  sooner than the backstop's interval, and each drain restarts the
  backstop's observation; with the message dead, control traffic could
  go on that way and never strand an event for 100 ms. A single such
  drain proves nothing - the routine may have claimed the event already
  and its own drain be waiting for the lock the waiter's took first -
  so these are judged as a run, not one by one: drains queued by
  something other than the message routine that took at least one
  event are counted in a row, and any claimed entry of the routine
  (`InterruptsClaimed` moving) ends the run. A message that works
  claims again and again - at every new event the controller sends one,
  and the routine claims on `USBSTS.EINT`, which a software drain does
  not clear - so a run cannot grow under it; a dead one never claims.
  The drain that brings a run to `XHCI_MSG4_STRANDED_LIMIT` fixes a
  deadline one tolerance tick ahead; further unclaimed drains do not
  move it, so traffic that keeps draining cannot put the verdict off.
  A claimed entry before the deadline cancels the run and the deadline
  with it; the deadline reached with the run unbroken is a trip of the
  same judge (cause 2).
- `XhciMsg4StrandedStep` (pure, host vectors) trips when
  `XHCI_MSG4_STRANDED_LIMIT` stranded events of the backstop's fall
  within `XHCI_MSG4_STRANDED_WINDOW` (eight within 60 s, decision 11;
  constants, not registry values, confirmed or moved by the bench
  reading), whatever was claimed between them: an event that stood 100 ms
  unserved is evidence by itself, and a message that fails only now and
  then must not settle into being served late. A
  trip is a connection lost (cause 2), logged with the count and the
  window. It is never a mode: the backstop serves the events it finds
  until the trip, as it does for a line, and the trip ends the
  lifetime's delivery instead of settling into it.
- The count and the window start afresh at each start, each in-place
  recovery (the start generation), each successful re-arm at D0 and
  each reconnect at a cancel; the detector's own standing observation
  is dropped at the same points, so an event that stood through a
  released query is timed from the reconnect.
- Not counted: drains while the query-stop has released the connection
  (the backstop is the only drain there by design), while the flag is
  clear, and during the start before the No Op below has passed.

The rate is low by construction - one look a tolerance tick, one cached
read when nothing stands - and the same on every system.

**The No Op by interrupt.** The start's No Op is submitted with the
interrupter masked (`XhciInitController`). Under mode 4, once the flag
is set and the interrupter enabled, the start waits up to 500 ms (the
same on every system, decision 11) for
the No Op's witness (`NoOpWitnessFired`) together with at least one
claimed entry of the message routine (`InterruptsClaimed`): the
completion was announced through the message. The backstop may drain
the ring meanwhile (it has run in the tolerance clock's DPC since
36.55), which fires the witness but claims nothing, so the claimed entry
is what decides. Otherwise the start is refused, reason 31, `msg4.noop`
the wait and the claims, and stopped as a failed thread start is
(`hcdStopBody`), which releases the connection and the claim. A
cancel-stop's reconnect is tested the same way with evidence of its
own: the witness is cumulative, so after the interrupter is on a new No
Op is issued and both counts must move past what they read before it;
a failure there is a connection lost (cause 4). That controller is
running, and the command engine takes one command at a time
(`XHCI_CMD_BUSY`), so the test has an admission of its own: the No Op
is offered for up to 500 ms, and the 500 ms for its evidence starts
only when the engine has taken it. A No Op the engine never took in
that time proves nothing either way: the test is recorded as not made
(`msg4.noop`), the connection stands, and the watchdog is what judges
it. A return to D0 runs none: the re-arm's status and the
watchdog cover it.

#### 4.5.7 98 SE and ME

The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13).
Its code left the tree the same day: on 98 SE and ME the start takes the
system class first and runs a stored `4` as `0` (automatic), as a reserved
value does, instead of failing the start; the decision answers "no
system" (22) for every system but NT 5; reason 32 is kept as a number and
not produced. Setup and the USB Tree offer `4` on 2000, XP and XP x64
alone, and read a stored `4` on 98 SE and ME as the driver runs it.

#### 4.5.8 IRQL, thread and lock rules

| Call or step | IRQL and thread | Held | Excluded from |
|---|---|---|---|
| `MsgIrqInitialize` | `PASSIVE_LEVEL`, the start's thread | the driver-global event | each other and every other controller's start |
| `MsgIrqIsaClaim`, `MsgIrqIsaRelease` | `PASSIVE_LEVEL`, a PnP IRP's thread (the system process), no affinity of this driver's | the PnP manager's serialisation of the device | the controller thread's look (it reads the connection, never the claim) |
| `MsgIrqConnect` | `PASSIVE_LEVEL`, the start's thread or the cancel's | at a cancel, the power gate | every other library call on the controller |
| `MsgIrqReArm` | `PASSIVE_LEVEL`, the power work item (or the query-stop's thread) | the power gate | suspend, resume, recovery, the look |
| `MsgIrqDisconnect` | `PASSIVE_LEVEL`, a PnP IRP's thread, or the shutdown step | at a query-stop and at the shutdown step the power gate; at a final release nothing, the thread joined and the controller stopped | as above |
| `MsgIrqIsaQuery` (the brake look) | `PASSIVE_LEVEL`, the controller thread, woken by the backstop's DPC or by its own deadline | the power gate | the query-stop's release; the final release by the join |
| the readiness flag's write | `PASSIVE_LEVEL`: a PnP IRP's thread, the power work item, or the controller thread | the ISR's exclusion (`KeSynchronizeExecution`); on the power work item and the thread, the power gate | the routine; every disconnect |
| `HcdMsg4Lost`, the latch | up to `DISPATCH_LEVEL` | the controller lock; no interrupt object, no library call | itself (the latch is taken once) |
| `HcdMsg4Lost`, the rest (the flag, `IoInvalidateDeviceState`) | `PASSIVE_LEVEL`, the controller thread | the power gate | the query-stop's release; the final release by the join |
| the routine (`hcdMsg4Isr`) | the connection's IRQL, under its interrupt object's lock | nothing of this driver's | - |
| the watchdog's detector | `DISPATCH_LEVEL`, the tolerance clock's DPC | the controller lock for the peek and the count | - |

The library's calls on one controller are never concurrent: the PnP
manager serialises the start, the stop, the query, the cancel and the
removal of a device; the power gate serialises those against the D0
step, the shutdown step and the thread's look; and the final release
runs after the thread is joined. The lock order gains nothing: the
ISR's exclusion is innermost and is never held across a call that takes
the controller lock, and no library call is made under the controller
lock.

#### 4.5.9 What is recorded and shown

**The codes.** `IntrReason` 21 to 27 and 30 to 34 (section 4.5.2), the
start's decision only. `MsiRoutines` gains the routine value 4 (the
library). `MsiConnectStatus` holds the library's status under that
routine value - 0 to 10, 12 `CLAIM_KEPT` and 13 `BRAKED` - and for a
refused claim the claim's status. `MsiMessages` the count, 1.
`MsiCapability` the capability read.

**New counter words**, appended after the block's last word as section 8
has it, written by the start, the look and `HcdMsg4Lost`:

| Word | Holds |
|---|---|
| `Msg4Strategy`, `Msg4IsaIrq`, `Msg4Share` | the three values as read (`FFFFFFFFh` not found) |
| `Msg4Irq` | the IRQ claimed or joined, 0 none |
| `Msg4Members` | the functions on the claim after this start's claim |
| `Msg4ClaimStatus` | the claim's status |
| `Msg4Sci`, `Msg4Sources` | the claim's `Sci` and `Sources` |
| `Msg4IrqReason` (four words) | the picker's reason for each IRQ 0 to 15, a byte each |
| `Msg4Vector` | the system vector |
| `Msg4Stranded` | stranded events counted this lifetime |
| `Msg4Lost` | 0 none; 1 braked; 2 the stranded limit; 3 a re-arm failed; 4 a reconnect refused |
| `Msg4BrakeEntries`, `Msg4BrakeStray`, `Msg4BrakeRun` | the claim's counts at the last look |
| `Msg4MemberEntries`, `Msg4MemberStray` | this controller's own counts at the last look |
| `Msg4Refused` | functions in the image's refusal table |

**The notes**: `msg4.system` (which system), `msg4.mode` (the
library's mode), `msg4.strategy.read`, `msg4.isairq.read`,
`msg4.share.read`, `msg4.claim` (the status), `msg4.claim.irq` (the IRQ
and the members), `msg4.claim.reason` (one per IRQ with a reason other
than "not tried": the IRQ, the reason, the detail), `msg4.status`,
`msg4.vector`, `msg4.data` (the message data, the IDT vector in its low
byte), `msg4.bdf` (the location), `msg4.shared`, `msg4.shared.pci`, `msg4.line`, `msg4.noop`,
`msg4.rearm`, `msg4.disconnect`, `msg4.release` (the claim's release),
`msg4.querystop`, `msg4.cancelstop`, `msg4.quarantine`, `msg4.refused`
(a start the table refused), `msg4.stranded`, `msg4.brake` (the
counters) and `msg4.lost` (the cause). A refused start records itself
and flushes as section 8's "A start the delivery refuses" has it, so the
reason is in DebugView beside the Code 10.

**`XHCITR`'s report**, under "event delivery": the mode and reason by
name (the new texts are the owner's to word); the library's status by
name; under strategy 0 the IRQ, whether it
is the controller's own or shared and with how many, and for a refused
claim each IRQ's reason in words ("never claimed", "the ACPI interrupt",
"level-triggered", "used by a device", "heard during the listen",
"excluded", "full" and so on); the vector; the stranded count; the
brake's counts; and, when the delivery was lost, the cause as a line of
its own. A block from a driver before these words prints none of it.

#### 4.5.10 Build, licence and signature

- **The MSI header** is vendored under `src/` (the same text with CRLF
  line endings, its row in `legal-provenance.md` section 2a naming the
  commit and both hashes), never edited here, and compiled in once, in
  `hcd_msg4.c`, with its implementation switch; every file that
  includes it defines the same hosted, no-native and ISA-line switches
  alike. This driver keeps its own message path on Vista and later
  (mode 3), so the library's native path is compiled out.
- **The licence.** The header's own SPDX line reads MIT at the commits
  read; the row records that line and the notice kept in the file.
- **When the MSI header is taken in** (decision 13): only from the
  commit at which its branches `dev-1` and `xirq-2` are one line, so
  that the teardown is coded once (section 4.5.11, item 2). Until then
  nothing of mode 4 is built for 2000 and XP.
- **The signature.** The MSI header in state A defines, in the one file
  that compiles its implementation, a signature string (the name, the
  version and the copyright holder) in read-only data, with no macro to
  leave it out; both images that compile it carry it. A packaging or
  import-gate check that each built image holds the string keeps a
  later build change from dropping it.
- **Imports.** The hosted set is on both allowlists already; the
  routines the claim needs are resolved by name (record 18 section
  15.16), not imported.
- **The installer** offers mode 4 as a tick, marked experimental (the
  owner, 2026-11-11). This record says what each control does and what
  its text must convey; the texts are the owner's to word, and the
  pages, the console's questions and the switches are record 23's and
  record 26's to design. A text in quotation marks here is a draft.
  - *Two ticks, never one for both.* The message has two users in this
    package, with different consequences, so each tick names its
    hardware and what follows from it. **The controllers' tick**, in
    the installer's USB part, is this section's: the USB 3 controllers
    (`xhci98.sys`, mode 4) **require** the message - experimental, no
    fallback: a controller that cannot connect does not start, and a
    keyboard, a mouse or a disk on it is lost until the setting is
    changed. Draft: "USB 3 controllers: require MSI (experimental; no
    fallback)". **The host's tick**, in the Thunderbolt part, is
    roadmap task 49.27's: the Thunderbolt and USB4 host (`xhcicm.sys`)
    **tries** the message for its ring - experimental, and it stays on
    its polled ring when the message cannot be connected. Draft:
    "Thunderbolt / USB4 host: try MSI for the ring (experimental;
    falls back to polling)". It is not a networking setting:
    `XhciCmRingInterrupt` and `XhciCmNetInterrupt` keep their own
    roles, and what the host's value is and how it stands to those two
    is 49.27's design. The host's tick exists only once 49.27 is built
    - on 2000 and XP only if that task's first
    reading shows the host needs it there - and it then sits on the
    Thunderbolt settings (record 26 section 5.5). Neither tick sets or
    implies the other, each says so, no text offers "MSI" for the
    package as a whole, and a release that adds the host's tick leaves
    it off on a machine where the controllers' tick is on.
  - *The component chosen is respected.* The host's tick is disabled,
    with the reason shown (draft: "Requires Thunderbolt / USB4
    support"), when that support is not selected for the run; and no
    setting of a component left out of the run is written or changed,
    even where that component is installed. The same holds for the
    controllers' tick in a run that leaves the USB drivers out (the
    connection manager alone).
  - *The controllers' tick and the mode.* Ticked, it writes
    `XhciInterruptMode` `4`; it shows ticked when 4 is the mode in
    force. It and the mode's existing list (record 26 section 5.4) are
    one setting, so they cannot disagree: either the list has no entry
    for 4 and is disabled while the tick is set, or it has one and
    follows the tick - record 26's choice. Unticking a tick that was
    set writes `0`, automatic. It is offered on 2000, XP, Server 2003
    and XP x64, and nowhere else (the driver refuses mode 4 on Vista and
    later).
  - *Its scope is every controller.* The installer writes the
    machine-wide value (`HKLM\System\CurrentControlSet\Services\USB`,
    record 26 section 5.4), and the driver's rule (`hcdReadDword`,
    `hcd_ctl.c`) is: a value of that name in the machine-wide key, of
    any type, stands for every controller, and a controller's own
    driver key is read only when the machine-wide key has none (one of
    another type there gives the default, not the controller's own).
    So the tick means **every USB 3 controller this driver runs,
    those that arrive later included** - a card added next month, a
    dock's controller - and the page says so. A controller's own value
    is never rewritten or deleted by the tick: it is kept, and is
    simply not read while the machine-wide one exists; where
    controllers hold different values and no machine-wide one is set,
    the row shows the mixed state and writes nothing until the user
    chooses (record 26 section 5.4, unchanged). The installer offers
    mode 4 for all controllers only (the owner, 2026-11-11): one
    controller and not another is set by hand in the controllers' own
    keys with no machine-wide value, which `tuning.md` describes.
  - *On 2000, XP and XP x64 the tick installs nothing* - the library is
    compiled into the driver. Beside it, enabled only while it is set:
    the strategy (`0` claim a free ISA IRQ, the default; `1` borrow the
    controller's line), and, shown only while "claim a free ISA IRQ" is
    the strategy chosen, a list for `XhciMsiIsaIrq` with eight entries -
    Automatic (writes `0`, the default), 3, 4, 5, 6, 7, 10 and 11 (each
    writes its number). With the other strategy the list is not offered
    and the value is left as it is; the driver does not read it then.
    `XhciMsiShare` is not offered by the installer while its default is
    `0` (the owner, 2026-11-11): it is a registry value, described in
    `tuning.md`. (Since 2026-11-13 its default is `8`, sharing on; Setup's
    and the USB Tree's advanced settings rows take 1 to 16 and refuse 0,
    section 4.5.19.)
  - *What the page can and cannot know about free IRQs.* Under strategy
    0 (strategy 1 claims nothing: it borrows the controller's own line)
    each user of a
    message needs a claimed IRQ of its own: every controller this
    driver runs, and, if the host's tick comes to these systems, the
    host as well - which cannot share a controller's claim at any
    `XhciMsiShare`, because a claim is joined only by the same driver
    object, in its own copy of the library (section 4.5.11, item 3),
    and `xhcicm.sys` carries its own and claims without sharing. Which IRQs are free is decided
    in the kernel at the start: the library's picker reads the
    interrupt controllers' own state and the firmware's tables, asks
    the PnP manager's arbiter, and listens on the candidate (section
    4.5.3). The installer cannot run it and does not imitate it. What
    it can show instead, and no more: **how many** users of a message
    the choices on its pages make (the controllers it found bound or
    about to be bound to this driver, plus the host when its tick is
    set), against the seven IRQs that can ever be claimed; which of
    those seven Windows reports **assigned to a device now** (the
    devices' allocated resources, through the configuration manager's
    documented routines), marked in the list as in use - never the
    rest as free, since an IRQ no device holds can still be refused as
    level-triggered, as the ACPI interrupt, by a firmware override or
    by the listen; and, where two or more users are chosen, that each
    needs its own IRQ, that controllers beyond the IRQs left fail to
    start unless `XhciMsiShare` is turned on, and that the report says
    afterwards which IRQ each took or why none (section 4.5.9). (Since
    2026-11-13 sharing is on by default and controllers fill one IRQ
    first, section 4.5.19.)
  - *A forced IRQ is one value for the machine*, by the same scope
    rule: with two controllers the second joins the same claim (the
    default `8`; section 4.5.19), and with `XhciMsiShare` `1` it finds
    it taken and fails (reason 33). So
    with more than one controller present the list's entries other
    than Automatic carry that warning, and a different IRQ for each
    controller stays a matter of the controllers' own keys with no
    machine-wide value, by hand (`tuning.md`). The host's forced IRQ,
    if 49.27 has one, is a value of its own.
  - *On 98 SE and ME*: the 98 SE and ME message path is not part of
    `3.5.0.0` (owner, 2026-11-13).
  - *The console's switches mirror the ticks.* An explicit enable and
    an explicit disable for the controllers' tick, `/usbmsi` and
    `/nousbmsi`; the same pair for the host's when 49.27 brings it,
    `/tbmsi` and `/notbmsi`; and no switch that turns on more than one
    of them. Each is refused wherever its tick would not be shown. `/set XhciInterruptMode=4` stays what it
    is, the same write as the enable. On an update a switch that is
    not given keeps the setting as it is, and an experiment the
    release newly brings stays off. The names are the owner's choice
    of 2026-11-11; their rows are record 23's to write.
  - *The log* says, for each of the two users, in every run: the
    setting asked for (the tick or the switch, or "kept"); its scope
    (the machine-wide value, and the controllers it then stands for,
    with any controller's own value it overrides); and, read from the
    driver's snapshot where one is there, how each controller's events
    are in fact delivered now and the reason for a refusal or, for the
    host, for the fall back to its poll.

#### 4.5.11 What this depends on in the siblings

Each item is a statement of this section that holds only as long as the
header's source says what it said in the state read. The list is read
again, item by item, when a header is vendored.

The MSI header, branch `xirq-2` in state A.

The states of the MSI header this record names were read before its
publication and are named here by letter, each with the date read and
the SHA-256 of the header: A, the ISA IRQ claim's line (`xirq-2`), read 2026-11-11,
`32f42da97a9fd4ab50d5a94773ef68fe0ed1d42faca9608ddc981b3446edb99f`; B, the
other line (`dev-1`), read 2026-11-10,
`7aeb5ce060b5cb25a4dc8c9f3cbe7226934d9c98e9e3481cb073c679093c4ec3`; C, the
two lines' first merge on its `main`, read 2026-11-11,
`1642af5e9df2038d961d1059576c764afe316f756685c2fe93e4d2f96be042bd`; D,
`dev-1` merged into `xirq-2`, read 2026-11-11,
`630be29f68ba6a7dd7abaff2736afe9c9497b1e2f763748b68b1692e8e3712d1`; E,
`main` with one 32-bit access to the MSI-X table and the local APIC, read
2026-11-11, `799e7acb20397ce0c7d1414349696f36523edcc972c1fb9b5705ce2caf2fbaf0`;
F, `main` with the ISA listen's timeout in 32 bits, read 2026-11-12,
`65df3cbb196f946bbb6eb69c6d6a8e5bf596e2d8349b38dffc31c1922007eca2`; G, `main`
with the published state, read 2026-11-12,
`f06e6fb903247f69442ceb34f77e77991dca5591cbc1a48c8a2995d94fd572e2`. What this
tree vendors at last is the header's version 1 (`legal-provenance.md`
section 2a). In state A:

1. The header's licence line (MIT), its version 1, and the
   signature string defined with the implementation.
2. **No proof of absence.** `MSGIRQ_DISCONNECT_ABSENT_OK`, which the
   owner accepted on 2026-11-10 and the parked branch passes at a
   reported removal, exists only on the header's branch `dev-1` (in
   state B), which state A does not contain; `dev-1` also keeps
   Interrupt Disable set at a teardown, where state A clears it. The
   owner's decisions (2026-11-11; decisions 7 and 13): the header is
   vendored only once the two lines are one, and a controller proven
   absent may be plugged in again. So the release of section 4.5.4
   passes the flag at a surprise removal and a removal only, a function
   proven absent is not entered in the refusal table, and two things
   are read again at that commit before the code is written: the flag's
   meaning as section 4.5.4 uses it (`OK` for a message confirmed off
   or a function proven absent), and section 4.5.3's sentence on
   Interrupt Disable after a disconnect, which follows whichever of the
   two behaviours the one line keeps.
3. The claim's interface: `MsgIrqIsaClaim`, `MsgIrqIsaRelease`,
   `MsgIrqIsaQuery`, `MSGIRQ_CONNECT_FLAG_ISA_LINE` (refused together
   with `PIN_PRIVATE`); `MSGIRQ_ISA_PARAMS` with `Exclude` and
   `MaxMembers` (2 to 8, default 2) and its ranges for the listen (25 to
   5000 ms) and the brake (16 to 1000000); `MSGIRQ_ISA_FLAG_SHARE`
   meaning "may be joined, and joins only when no IRQ of its own is
   left", within one driver's copy; the picker's order and never-set;
   the reasons through `JOINED` 13, `FULL` 14 and `EXCLUDED` 15;
   `MSGIRQ_ISA_STATE` grown by the member's two counts behind `Size`;
   the statuses `CLAIM_KEPT` 12 and `BRAKED` 13.
4. The arm's read-back of Bus Master Enable and Interrupt Disable, and
   the disarm's clear of Interrupt Disable (section 4.5.3).
5. `MsgIrqInitialize` marking itself done before it resolves, and a
   first call from a start routine at `PASSIVE_LEVEL` being admitted
   although the documents ask for `DriverEntry`.
6. Claim and release enforcing the system process's context, and the
   statement that the PnP manager's start, stop and removal IRPs arrive
   in it. The legs read it for Device Manager's disable and for the
   installer's device restart; an IRP found elsewhere moves the two
   calls to a waited work item.
7. The names resolved through the client's resolver (record 18 section
   15.16).
8. The brake: per claim, the threshold times the members, final, with
   no notice to the client, and `MsgIrqIsaQuery` callable at
   `PASSIVE_LEVEL` beside a running routine.
9. The shared chain on a latched vector (the kernel calling every
   routine and repeating the pass): the library's own measurements, with
   its test devices in the emulator, on XP, 2000 and XP x64. None is a
   reading of this driver's, whose legs are owed (section 4.5.14).
10. `IoInvalidateDeviceState` as the report of a failed controller
    (the library's integration notes). That text also names this driver's dead-line
    watcher as what finds a braked interrupt silent; the watcher does
    not run under a message (section 4.1), and the detector is the
    backstop (section 4.5.6).
11. **Pending, not decided and not built: a second claim pool.** (Built
    since, in the header's `main` in state C, kept in state E; what
    this driver does with
    it is section 4.5.16 item 1.)
    The library's record 05 section 2.9 designs a pool of I/O APIC inputs
    beyond the ISA ones, opt-in per claim by a further flag
    (`MSGIRQ_ISA_FLAG_HIGH` in that design) and tried after the ISA
    pool and before a join; an experiment on inputs above 23 is running,
    and the owner wants the client to be able to choose the range of
    inputs scanned and which pools are scanned - the ISA IRQs only, the
    inputs above 23 only, or both (owner, 2026-11-11). Nothing of it is
    in state A and this section designs none of it. If it is built,
    mode 4 gains three values beside `XhciMsiStrategy` and
    `XhciMsiIsaIrq`, read with them under strategy 0 only, written by
    the install at their defaults under the registry rule with their
    gate and `tuning.md` rows: which pools are scanned (default the ISA
    IRQs only, until the pool is decided), and the lowest and the
    highest input the claim may consider. The names are decided (the
    owner, 2026-11-11): **`XhciMsiPools`** (`0` the ISA IRQs only, the
    default; `1` the inputs above 23 only; `2` both, the ISA IRQs
    first), **`XhciMsiInputFirst`** and **`XhciMsiInputLast`** (the
    lowest and the highest input considered; their defaults and ranges
    the library's). The pool would be enabled by the pools
    value, which makes a strategy-0 claim pass the library's flag or
    parameters for it - not by a new `XhciMsiStrategy` number: strategy
    0 already means "a claimed interrupt", and where it is looked for is
    a setting of that claim. The forced value stays `XhciMsiIsaIrq`,
    with a wider set: it then takes an input number as well, since only
    one forced value can be in force. A forced IRQ or input outside the
    pools chosen, or outside the range, refuses the start (reason 30),
    as an `XhciMsiIsaIrq` outside its set does today, and the
    installer's list for it (section 4.5.10) follows the set.
    `Msg4Irq` would then hold an input above 15,
    and the report says which pool. **Nothing of this is written, read
    or offered until the pool exists**: no INF row, no gate row, no
    `tuning.md` row and no installer control.

12. 98 SE and ME: the 98 SE and ME message path is not part of
    `3.5.0.0` (owner, 2026-11-13).

#### 4.5.12 The parked branch `w48-mode4`: what is kept and what is done again

The branch (tip `a80be3621`, the rebase of `441d9839`, on the `3.4.3.0`
commit `4b15fe696`; unbuilt beyond its host suites, with no legs) built
the line-borrowing strategy for 2000 and XP against the MSI header's `dev-1`
in state B. Its sibling `w48-mode4-borrow` is the same tree as that
branch's commit `6735b8d54` and holds nothing more. Against this
section:

| File or area | Kept | Done again |
|---|---|---|
| `docs/contributing/design/15-...md`, its section 4.5 and 13.8 | the veto's two walks, the query-stop and cancel order, the quarantine and its table, the D0 re-arm's placement, the No Op by interrupt | replaced by this section: strategy 0 built instead of refused (reason 29 gone), the claim's lifetime, the readiness flag, the brake split, the watchdog, the lost connection replacing the recovery gate's re-arm, the reasons 32 to 34, the counter words (it added none) |
| `docs/contributing/design/18-...md`, its section 15.16 | the one-shot look and `XhciHlprAcceptRoutine` | the names resolved (the claim's, and `IoInvalidateDeviceState`); the look made before the library's initialisation |
| `docs/contributing/design/20-...md`, section 4.1 | the sentence that the quirk refuses under mode 4 | - |
| `docs/using/tuning.md` | the mode's place in the table and list, "experimental", the borrowing warning and how to check, the recovery paragraph | strategy 0 as available and the default, with how it picks, what a wrong IRQ looks like and `XhciMsiIsaIrq`'s set (the library's record 05 section 8 lists what the text must say); `XhciMsiShare` |
| `docs/contributing/build-and-test.md`, the values table | the two rows' form | the meanings (0 no longer refused, the IRQ's set), `XhciMsiShare`'s row |
| `docs/contributing/legal-provenance.md`, section 2a | the row's form | the state read, both hashes and the licence line (MIT in state A; the branch recorded the earlier line) |
| `docs/contributing/source-files.md` | the rows for `xhci_msg4.c`, `hcd_msg4.c`, the MSI header, `test_msg4.c` | their texts |
| the MSI header under `src/` | - | vendored afresh from the state its two lines settle on (2084 lines in state B, 3860 in state A) |
| `src/hcd_msg4.c` | the host table and the configuration routines; the refusal table's use; the strategy step's shape; the borrowed line's veto and connect; the release's order and the quarantine; the query-stop and the cancel; the No Op wait | the library's initialisation after the look; strategy 0's claim, connect and release and the three held states; `MSGIRQ_DISCONNECT_ABSENT_OK` (not in state A: item 2 above) and `hc->Msg4Removal` with it; the statuses 12 and 13; the readiness flag at every transition; the bar on re-enabling apart from the mask's proof; the query forwarded and waited for; the brake look; `HcdMsg4Lost` in place of `hc->Msg4Unarmed` and the recovery gate's re-arm; the Command-register rule (the branch relies on Interrupt Disable left set) |
| `src/xhci_msg4.c`, `src/xhci_msg4.h`, `test/test_msg4.c` | the resource-map walk, the value-name suffix, the table, the stray step and their vectors | the IRQ value's check, `XhciMsiShare`'s reading, the stranded judge, the packing of the claim's reasons, and vectors for each |
| `src/xhci_poll.c`, `src/xhci_poll.h`, `test/test_poll.c` | mode 4 in `XhciPollValues`; the PCI-line walk's `Sharing` switch; reasons 21 to 27, 30 and 31 by number | the decision's inputs (the system as three classes, the IRQ value) and order; the names (`MSGNT` to `MSG4`); reasons 32 to 34; 28 and 29 dropped |
| `src/hcd.h` | the mode-4 fields and prototypes, the routine value 4's name, the two value names | the claim's state, `Msg4Ready`, `Msg4Lost`, `XhciMsiShare`'s name |
| `src/hcd_ctl.c` | the values read in `hcdParseResources` and settled in `hcdSettleDelivery`; `HcdCtlLineShared`; the connect's branch in `hcdConnect`; the release's branch; the No Op in `hcdStartBody` | `hcdMsgIsr`'s hook (the gate first; the stray count under strategy 1 only); the DPC's re-arm under the bar; the brake's latch through `HcdMsg4Lost`; the recovery's admission (the latch, no re-arm); the terminal verdict's input; the backstop's caller (on whatever `XhciTolerance` says, the stranded count); the thread's brake look |
| `src/hcd_pnp.c` | the query-stop and cancel hooks | the removal mark (with item 2); the query waited for; `IRP_MN_QUERY_PNP_DEVICE_STATE` |
| `src/hcd_power.c` | the D0 deferred to the work item, the re-arm before the resume | the flag; the suspended-in-D0 path; the lost connection |
| `src/hcd_msi86.c` | `HcdMsiFindRoutine` and its look | the filter under strategy 0 (as mode 0's); the further names |
| `src/hcd_quirk.c`, `src/hcd_svc.c`, `src/hcd_svc.h`, `src/hcd_dma.c`, `src/hcd_entry.c` | all of it (`XhciMsiAvoid` under mode 4, `HcdSvcConfigBytes`, the image kept under the ledger's lock, the globals' initialisation) | - |
| `src/xhci98.inf`, `src/xhci98-amd64.inf` | `XhciMsiStrategy` and `XhciMsiIsaIrq` at `0`, don't-overwrite, on every path | `XhciMsiShare`'s row, at `0` |
| `scripts/inf-gate/` (the rules, both footprints, the self-tests) | the two values' rows | `XhciMsiShare`'s rows |
| `scripts/import-gate/check-imports.ps1` | the guard that the MSI header is only ever compiled hosted | its ISA-line switch in the guard's set; the signature check of section 4.5.10; the amd64 row for `IoInvalidateDeviceState` if it is imported there |
| `xhcitr/tr_snap.c` | the texts for reasons 21 to 27, 30 and 31, the routine value 4, the statuses 0 to 10 | the texts for 22 and 30 (their meanings moved), 32 to 34, the statuses 12 and 13, the new words' lines; `SNAP_POLL_REASONS` 35 |

**"Task 48.41"** is cited in the branch's record 15 (the section's
heading and 13.8), record 18 section 15.16, record 20 section 4.1,
`source-files.md`, `legal-provenance.md`'s Task column, and comments in
the code, the import gate, the host tests and `XHCITR`. Every one becomes "task 49.24";
the owner's decisions keep their dates. Nothing in the branch cites
48.39 or 48.40.

The branch is 28 commits behind `3.5.0.0` as this is written; of the
files it changed they touch one, `build-and-test.md`.

#### 4.5.13 The owner's decisions on the open points

Revision 10 left eleven points to the owner, each with its options and
a recommendation. The owner answered them on 2026-11-11; the sections
named carry each answer, and the options not taken are kept here only
as far as they explain the answer.

| Point | Decided (the owner, 2026-11-11) | Carried in |
|---|---|---|
| 1. The sharing value and the member cap | One value, `XhciMsiShare`: `0` off, `2` to `8` on with that many controllers to a claim at most. (Not two values, and not a cap fixed in the code.) Superseded 2026-11-13: section 4.5.19. | 4.5.1 decision 5, 4.5.2, 4.5.3, 4.5.19 |
| 2. `XhciMsiIsaIrq`: registry only, or in the installer | Offered by the installer, beside mode 4 when "claim a free ISA IRQ" is the strategy chosen: a list Automatic, 3, 4, 5, 6, 7, 10, 11, Automatic the default. The texts are the owner's to word. | 4.5.1 decision 4, 4.5.10 |
| 3. The brake's number and the listen | 256 unclaimed entries, for the library's brake and for the driver's own count under strategy 1; the listen 500 ms. The bench reading may move them. | 4.5.1 decision 10, 4.5.3, 4.5.6 |
| 4. The watchdog's period and limit | The existing lost-interrupt backstop as the detector; eight stranded events within 60 s fail the controller; constants, not registry values. | 4.5.1 decision 11, 4.5.6 |
| 5. The No Op's wait | 500 ms on every system. | 4.5.1 decision 11, 4.5.6 |
| 6. What a controller that lost its message shows | On 2000, XP and XP x64 Windows is told the device failed (`IoInvalidateDeviceState`, `PNP_DEVICE_FAILED`); the code Device Manager shows is read on a leg and may not be Code 10. The next start is not refused on this ground. | 4.5.1 decision 12, 4.5.4 |
| 7. A controller unplugged under mode 4, plugged in again | Admitted on 2000, XP and XP x64 when the library proved the function absent at the removal. | 4.5.1 decision 7, 4.5.4, 4.5.11 item 2 |
| 8. Which line of the MSI header is vendored | None until `dev-1` and `xirq-2` are one line; the header is taken in once. | 4.5.1 decision 13, 4.5.10, 4.5.11 item 2 |
| 9. If the second claim pool is built | `XhciMsiPools` (`0` the ISA IRQs only, `1` the inputs above 23 only, `2` both), `XhciMsiInputFirst`, `XhciMsiInputLast`; the forced value stays `XhciMsiIsaIrq` with a wider set; nothing written until the pool exists. | 4.5.2, 4.5.11 item 11 |
| 10. 98 SE and ME | The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13). | 4.5.1 decision 8, 4.5.15 |
| 11. Strategy 1 on Windows 2000: an exclusive-device request in the requirements filter | Not tried. | 4.5.1 decision 14 |

**Revision 11 first left nine further points open**, all but the first
from section 4.5.15. The owner answered them the same day:

| Point | Decided (the owner, 2026-11-11) | Carried in |
|---|---|---|
| 1. `XhciMsiShare` in the installer | Not offered while its default is off: a registry value, described in `tuning.md`. (Since 2026-11-13 the default is on, `8`; section 4.5.19.) | 4.5.10, 4.5.19 |
| 2. A check after the restart that reports, or sets the mode back | The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13). | 4.5.15 |
| 3. A mode 4 the installer knows cannot work, or cannot confirm | The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13). | 4.5.15 |
| 4. The switches' names | `/usbmsi` and `/nousbmsi`; `/tbmsi` and `/notbmsi` with 49.27. | 4.5.10 |
| 5. A recovery file in the install directory | The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13). | 4.5.15 |
| 6. 98 SE and ME: the uninstall | The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13). | 4.5.15 |
| 7. 98 SE and ME: what the installer can read | The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13). | 4.5.15 |
| 8. Mode 4 for one controller and not another, in the installer | No: for all controllers only, machine-wide. One controller is set by hand, as `tuning.md` describes. | 4.5.10 |
| 9. The only keyboard and mouse behind a controller, and the one run | The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13). | 4.5.15 |

Nothing of section 4.5 is open with the owner as this is written. What
is still not known is a dependency (section 4.5.11) or a reading owed
(section 4.5.14).

#### 4.5.14 Legs (owed)

None has run for this design. In the emulator, with its xHCI as the
controller: 2000 SP4 and XP SP3 on an ACPI multiprocessor HAL, and XP
x64 - strategy 0 connecting, the IRQ in the report, a stick copied and
compared, a mouse, audio, sustained storage; `XhciMsiIsaIrq` forced onto
a free IRQ and onto one in use, each from the installer's list as well
as from the registry; no IRQ left (reason 33); two
controllers with two IRQs free (each takes its own, `XhciMsiShare` `0`
and `2` alike); two controllers with one IRQ eligible, `XhciMsiShare`
`0` (the second refused, reason 33) and `2` (the second joins; both
orders of leaving, standby and hibernation with either resuming first,
one quarantined); three with one IRQ eligible and `XhciMsiShare` `2`
(the third refused, "full") and `3` (the third joins);
strategy 1 on a private line and its refusals; the Standard PC HAL;
`XhciMsiAvoid` 2; the helper absent on x86; Vista ("no system"); the
brake by a driven input, at 256 entries for the library and for
strategy 1's own count; the stranded limit (eight within 60 s) and the
lost re-arm by `qemu`-flavour injections, and **which code Device
Manager then shows** (decision 12), with the controller's next disable
and enable starting it again; repeated
disable and enable; the query-stop refused and its cancel; removal;
a controller unplugged and plugged in again, admitted where the library
proved it absent and refused where it did not (decision 7);
shutdown; the quarantine and a start under mode 0 after it. Mode 0
unchanged on each. Then the
roadmap's gates for 49.24 on real hardware.

#### 4.5.16 Revision 12: what the code is built against (2026-11-11, before the code)

The code of this section is written against the MSI header's `main` in
state C (section 4.5.11 names the states; the merge of `xirq-2`, the same
tree; `dev-1` merged into `xirq-2` in state D, so the two lines are one),
and the header is vendored from `main` in state E (the merge of `xirq-2`,
the same tree), whose one change to the header over
state C that a client sees is that every MSI-X table and local APIC
register is read and written through one aligned 32-bit access (a
Visual C++ 6.0 test of a volatile register's bit was narrowed to one
byte); no structure, value or call changed. It is vendored again from
`main` in state G (the merge of `xirq-2`, the same
tree), which adds the published state (item 9 below) and changes no
structure, value or call a client uses; and again from `main` in
state F, whose ISA listen computes its timeout in 32 bits (no
`_allmul` import on x86); and again from its version 1, read
2026-11-12 (the header's SHA-256 `d5b4f45d7dda00eea11e7d85af80b12292c6e87db31686e379b6a1de986a5ac0`;
item 10 below); and again from its version 1 as read 2026-11-13, whose
member cap of one claimed IRQ is 16 and whose claim can ask for the
shared line first (SHA-256
`eaace8e6bda90cf0c62a95c0a3d10f48c90ba7ca387d94a6317b5c2b3e06be0c` with
LF endings, `b8bdded716cddd99f8750f5e84f60259a96164f7d0e895deae413acc2a812f73`
for this tree's CRLF copy; section 4.5.19). Where this section differs
from sections 4.5.1 to 4.5.14, this section stands; each point below
names what it changes.

1. **The second claim pool exists** (the decisions rows "the readings
   of the night of 2026-11-11" and the one on the second pool after it
   was built; the library's API notes, "The second pool"). Section
   4.5.11 item 11's "nothing of this is written, read or offered until
   the pool exists" no longer holds. On 32-bit 2000 and XP, under
   strategy 0, the driver reads four values beside the three of section
   4.5.2:

   | Value | Meaning | Out of range |
   |---|---|---|
   | `XhciMsiPools` | `0` the ISA IRQs only, the default; `1` the I/O APIC inputs above 23 only; `2` both, the ISA IRQs first (the library's `MSGIRQ_POOL_ISA`, `_HIGH` and both) | any other value refuses the start, reason 30 |
   | `XhciMsiInputFirst`, `XhciMsiInputLast` | the lowest and the highest upper input considered; both `0` the library's 24 to 255 | read only with the upper pool; outside 24 <= first <= last <= 255, or one of them 0, refuses, reason 30 |
   | `XhciMsiInputBudget` | how many different upper inputs this driver may have translated in one boot; `0` the library's 16, else 1 to 32 (the name the library's integration notes use; accepted by the owner 2026-11-12) | read only with the upper pool; above 32 refuses, reason 30 |

   The forced `XhciMsiIsaIrq` then takes an upper input as well: with
   the upper pool chosen, 24 to 255 within the range; an IRQ or input
   outside the pools chosen or the range refuses (reason 30), as
   section 4.5.11 item 11 had it. With the upper pool the claim passes
   `RegistryPath`, the copy of the path `DriverEntry` received that the
   image keeps (the library keeps the boot's charges in its registry
   state under the service's name), and the result's
   `HighBudget`, `HighNew` and `HighSpent` are logged. Without the upper
   pool none of its fields is set and `RegistryPath` is NULL, as the
   library requires. **Not on XP x64** (the owner: "this driver does not
   offer the pool there", where an edge on a claimed upper input reaches
   no routine): the amd64 image reads none of the four values, claims
   from the ISA IRQs alone, and its INF writes none of them. **The pool
   stays off unless a user sets it**: the x86 INF writes `XhciMsiPools`
   `0`, nothing offers it, and turning it on by default or in the
   installer waits for the emulator reading the decisions row names (a
   claim made before a device appears on the same upper input). Whether
   that row's "before this driver may turn the pool on" also bars a value
   a user sets by hand is **not decided**: it is put to the owner, and the
   code honours a hand-set value until the answer (the task's instruction
   of 2026-11-11), a single test in the decision that the answer can
   change.
   Section 4.5.2's sentence that no value of a second pool is read or
   written is replaced by this table, and section 4.5.10's installer
   offers none of them.
2. **The claim's structures grew**, each judged by its own `Size`,
   which in states C and E must be exactly the earlier size
   or at least the
   present one (any size between is `MSGIRQ_STATUS_INVALID_PARAMETER`
   with the result untouched). The driver zeroes the whole
   `MSGIRQ_ISA_PARAMS` and the whole `MSGIRQ_ISA_RESULT` and sets each
   `Size` to the present `sizeof`. Section 4.5.3 step 3's options gain
   `Pools`, `HighFirst`, `HighLast`, `HighBudget` and `RegistryPath`
   (the last four only with the upper pool); `HighIoApics` and
   `HighExclude` are never set.
3. **What is recorded of the upper pool** (section 4.5.9). New counter
   words: `Msg4Pools`, `Msg4InputFirst`, `Msg4InputLast`, `Msg4Budget`
   (as read; `FFFFFFFFh` not found, not read on XP x64);
   `Msg4PoolsScanned`, `Msg4HighFirst`, `Msg4HighLast`, `Msg4HighIoApics`,
   `Msg4HighIoApicCount`, `Msg4HighEnd`, `Msg4HighLastInput`, `Msg4HighDetail`, `Msg4HighBudget`,
   `Msg4HighNew`, `Msg4HighSpent` (the result's upper half);
   `Msg4HighBudgetRefused` (upper inputs refused for the budget) and
   `Msg4HighFirstRefused` (the first upper input refused in scope, the
   input in 15:8 and its reason in 7:0). New notes: `msg4.pools.read`,
   `msg4.input.read` (first and last), `msg4.budget.read`,
   `msg4.claim.high` (the scope and the budget's use) and
   `msg4.claim.high.reason` (the upper inputs with a reason other than
   "not tried", as runs: one note per run of consecutive inputs with the
   same reason, the first input in 23:16, the last in 15:8, the reason in
   7:0). The status `MSGIRQ_STATUS_NO_INPUTS`
   (14) from a claim refuses with reason 33, the status kept, and
   `MsiConnectStatus` holds 14 under routine value 4. `XHCITR` names
   the reason `MSGIRQ_ISA_BUDGET` (16) and the meanings the library's API notes give
   `MSGIRQ_STATUS_UNSUPPORTED` (with `HighDetail` a status: the
   charges' record not opened, read or written) and
   `MSGIRQ_STATUS_HAL_UNSUPPORTED` (the HAL's answer for the claim's
   interrupt changed, or its vector and IRQL disagree) from a claim or
   a connect.
4. **Two names more**, for the upper pool's charges: `ZwCreateKey` and
   `ZwSetValueKey`, resolved through the same routine as the others
   (record 18 section 15.16), never imported. Without them a claim that
   chooses the upper pool is refused (reason 34) and the ISA pool is
   unaffected.
5. **Interrupt Disable stays set after a disconnect** (the header's
   `dev-1`, in the one line; the library's API notes, "Interrupt Disable
   stays set after a disconnect"). Section 4.5.3's last paragraph - the disarm
   clears Interrupt Disable again, and a start on the line after a clean
   disconnect finds it clear - is replaced: the library leaves the bit
   set, and a later start of the same function under mode 0 or 1 clears
   it before its line's interrupts are enabled, as every start with a
   line delivery already does (`hcd_ctl.c`, `hcdIntxEnable`). Section
   4.5.11 item 2's two readings are made: `MSGIRQ_DISCONNECT_ABSENT_OK`
   returns `OK` for a message confirmed off or for a function proven
   absent (three reads of its vendor and device ID, 10 ms apart, each
   succeeding with `FFFFFFFFh`), and the disable keeps Interrupt Disable
   set where the function has it, so section 4.5.4 step 1's condition
   can be met.
6. **The seams are two files.** So that the common lifetime is built
   before and apart from the library's seam, `hcd_msg4.c` holds what is common (the decision's glue, the
   quiet step's call, the readiness flag's one wrapper, the gated
   routine, the No Op by interrupt, the watchdog's judge, `HcdMsg4Lost`,
   the refusal table and the image kept, the notes and words);
   `hcd_msgnt.c` is the library's seam and the only file that includes
   the MSI header (with its implementation switch). Sections 4.5.3,
   4.5.10 and 4.5.12 read with these names.
7. **98 SE and ME.** The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13).
8. **98 SE and ME.** The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13).
9. **The published state** (the header in state G; the library's API
   notes, "The published state"; its record 07). The library writes one
   snapshot per PCI function in its registry state, a volatile key, at
   the claim, the connect, the
   re-arm, the disconnect and the release, at `PASSIVE_LEVEL`, and counts two
   things in its interrupt trampoline; the client is asked for nothing.
   This driver builds it on (the switch that leaves it out is not
   defined), so a test program shows each controller of this driver that mode 4 connects on
   2000, XP and XP x64: its claim (the IRQ or input, the members), its
   connection (vector, message), the last call and its status. Nothing
   is published under any other mode: the library is initialised only
   by a mode-4 start. The
   cost is the library's few registry writes at each of those calls of
   a mode-4 start, stop, query-stop, cancel, removal and resume (the
   re-arm publishes too). The hosted x86 build resolves the names
   it needs through this driver's resolver (`ZwCreateKey`,
   `ZwSetValueKey`, `ZwClose`, `KeQuerySystemTime`; record 18 section
   15.16), so the image imports nothing new; without one of the three
   registry routines nothing is published and the connection is the
   same, and without `KeQuerySystemTime` only the snapshot's load time
   is zero.
10. **The event history and the power-state guard** (the header's
   version 1, read 2026-11-12; its records 08, 04 section 7 and 01
   section 2; the library's integration notes for this driver). Three
   changes, none to a structure, value or call this driver uses. The
   library keeps a ring of its events per copy and copies it into the
   published state's key as a `History` value, which the test program
   reads; it is on
   by default and asks nothing of the client. Its time stamp comes from
   `KeQueryInterruptTime`: the hosted x86 build resolves it by name
   through this driver's resolver (record 18 section 15.16), and on
   amd64 it is inline, so neither image imports anything new; without
   the name the events have no time. The MSI-X table is touched only
   with Memory Space Enable set, the function in D0 and after a
   complete capability walk: a disconnect out of D0 disables MSI-X
   through configuration space alone, and a connect or re-arm out of D0
   returns `MSGIRQ_STATUS_CONFIG_ACCESS`; this driver connects at the
   start and re-arms on the return to D0 (`HcdMsg4Resume`) and after a
   query-stop's failed disconnect, all in D0, so neither arises. A
   malformed capability list (a loop, a pointer into the header) is now
   `MSGIRQ_STATUS_CONFIG_ACCESS` at the connect, before any write: the
   start is refused (reason 26) where the walk used to use what it had
   found. `MSGIRQ_DISCONNECT_ABSENT_ONLY` is for a client that cannot
   quiet its function before a removal's disconnect; this driver halts
   the interrupter first and keeps `MSGIRQ_DISCONNECT_ABSENT_OK`.

#### 4.5.15 98 SE and ME (revision 11, 2026-11-11)

The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13).

#### 4.5.17 98 SE and ME (revision 13, 2026-11-12; roadmap-usb4 task 49.24)

The 98 SE and ME message path is not part of `3.5.0.0` (owner, 2026-11-13).

#### 4.5.18 The owner's decision of 2026-11-12: a hidden beta

Mode 4 ships as a hidden beta for testers. The installer
never mentions mode 4 and offers it on no Windows (nothing in `scripts\package` or `xhciinst` refers to it). Only the Advanced settings rows of
Setup and of the USB Tree show the value. `docs/using/tuning.md` keeps the values in its table, each marked as an
unsupported test feature, and has no set-up section; the set-up text and
where the legs passed are in `build-and-test.md`, "Mode 4 for testers". The driver's behaviour is unchanged.

#### 4.5.19 Sharing by default: `XhciMsiShare` fills one IRQ first (the owner's decisions of 2026-11-13)

The owner's decisions of 2026-11-13, after the legs below: sharing is
on by default; `XhciMsiShare` keeps its name and changes its meaning (no
second value); the member cap is 16; a claim asks for the shared line
first, the fullest one; `0` is not offered. They supersede section
4.5.1 decision 5 (sharing on by default only after the emulator legs and
the owner's bench, "an interrupt of its own first, then share") and
every rule of sections 4.5.2, 4.5.3, 4.5.10 and 4.5.13 built on it; the
bench reading that decision waited for was dropped.

**The value, as every reader takes it** (`XhciMsg4ShareValue`, Setup and
the USB Tree): the most controllers on one claimed IRQ before another
IRQ is taken.

| `XhciMsiShare` | Runs as |
|---|---|
| absent | `8` |
| `0` (not offered by Setup or the USB Tree) | `1` |
| `1` | no sharing: every controller an IRQ of its own; one that finds none left does not start (reason 33), as before |
| `2` to `16` | controllers fill one IRQ up to that many, and the next takes a fresh IRQ |
| above `16` | `16` |

Both INFs write `8` on every install path under the don't-overwrite
rule, so a value a user set survives an update. As every value of this
section, the machine-wide value wins over a controller's own.

**The claim** (`hcd_msgnt.c`): with sharing on (a value of 2 or more as
it runs) the claim passes `MSGIRQ_ISA_FLAG_SHARE` with
`MSGIRQ_ISA_FLAG_JOIN_FIRST`, and `MaxMembers` the value as it runs, held
to the library's own maximum (16), since a cap above it would refuse
every claim, the first controller's too. The library then joins the
fullest claim of this driver's with room before it claims a fresh IRQ;
a controller that stops and starts again (a disable and enable, a
restart of its stack) rejoins the fullest line with room, and takes a
fresh IRQ only if its line is full. With sharing off no flag is passed
and no claim is joined. Sharing is of the ISA pool alone: a forced ISA
IRQ (`XhciMsiIsaIrq` 3 to 7, 10 or 11) is joined as any other ISA claim
when the value runs as 2 or more, and with 1 (or a stored 0) a second
controller on the same forced IRQ does not start; an input above 23
(the second pool, section 4.5.16 item 1) is never joinable, whatever the
flag (the library's upper-pool report makes its claim record unshared),
so a forced upper input set machine-wide leaves every controller after
the first not started. Only `xhci98.sys`'s own controllers share: the
Thunderbolt and USB4 host's ring-interrupt claim (record 21 section
4.5.14) is exclusive (no sharing flag, `cm_rirqnt.c`, `CmRirqNtClaim`)
and is made by another image, under another driver object, with its own
copy of the library, so it never joins this driver's shared line. When
`XhciInterruptMode` 4 is set machine-wide, as Setup sets it, the host's
`XhciCmRingInterrupt` 2 yields and uses its line instead (`cm_reg.c`
reads the machine-wide value alone; `cm_rirqos.c`'s yield; the USB
Tree's "on, by the line (2 works as 1 here)", read on XP x86, 2000 and
XP x64). So the two compete for an IRQ only when mode 4 is set in a
controller's own key and the host has 2: then whichever starts first
holds the IRQ, a host that loses polls its ring (record 21 section
4.5.14: after a failed message the ring is polled, never the line), and
controllers that lose do not start. The vendored header's version 1 as read 2026-11-13 carries
the cap of 16 and the join-first flag (section 4.5.16's head, and
`legal-provenance.md` section 2a).

**Legs** (2026-11-13, a 32-bit XP SP3 guest in the emulator, nine
controllers in mode 4, three free ISA IRQs): at the default, eight
controllers on IRQ 7 and the ninth on IRQ 6, IRQ 3 untouched; a stick
on each of the nine, 9 of 9 clean; a disable and enable rejoins; a
hibernation keeps the claims; with `XhciMsiShare` `1`, and with a stored
`0`, three controllers each on its own IRQ and the other six refused
cleanly (Code 10); the shutdown 9 s. The same guest and build with
sixteen controllers: at the default `8`, eight on IRQ 7 and eight on
IRQ 6, IRQ 3 untouched, all sixteen running; with `XhciMsiShare` `16`,
all sixteen on IRQ 7, a stick copy on each 16 of 16 clean, the power-off
in 9 s. Not covered: Windows 2000 and XP x64 with more than eight on one
IRQ, more than sixteen controllers, standby (only hibernation was read,
on XP), real hardware, the USB4 host beside mode-4 controllers in a
guest, and a user's own per-controller value surviving an update (only
the INF's `8` and the machine-wide value were read).

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

**The role** (2026-10-09; revised from `6cac234`) is not a state. The
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
  ownership until its epilogue**: the decision, made
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
  role's** (revision 5): a watcher arms
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
  is published, in one controller-lock hold that also kicks (revision
  5: `hcd_power.c` calls `XhciResumeController`
  before it publishes `Common.DevicePower = PowerDeviceD0`, so a kick from
  inside the core's resume could arm a DPC that runs first, fails the D0
  test and parks for good; and the start's own D0 is published by
  `hcdStart` in `hcd_pnp.c` only after `hcdStartBody` returns). **Every
  grant is made with the power gate held**, so no suspend or resume on
  another processor can publish its own state between the grant's checks
  and its kick. The sites, each through `HcdPollGrant(hc)`:
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
  controller thread publishes under the controller lock (revision 5:
  the port machines and the hub records are the
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
  **Answered by revision 8, section 7.2** (task 38.3): on Windows 2000 and
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
  `HcdCtlBackstopTick`) already takes this peek, under the same lock, on each
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

### 7.2 A finer system timer while polling (revision 8, task 38.3)

Designed 2026-10-13 for roadmap-hcd task 38.3, before any code; the
helper half (how the 32-bit image reaches the routine, the helper's
extension to Windows 2000 and XP and its rename to `xhcihlpr.sys`) is
design record 18 section 15. The owner's answers of 2026-10-13 are
written in (section 7.2.13), as revised by the owner at `e2c01854`
(section 13.6).

#### 7.2.1 What is asked, and what is not

Asked (roadmap 38.3): under a poll delivery, ask Windows for a finer
system clock while the poller is in its active state, and give it back
when the poller leaves it, the delivery leaves polling, the controller
stops or leaves D0, and at S3, S4 and S5; at most one request per
controller, every `TRUE` paired with its `FALSE`; the call made at
PASSIVE_LEVEL from a thread, never from the poll DPC; a registry value
that turns it off, on by default (owner, 2026-10-12; off by default
since the owner's amendment of decision T4 on 2026-10-14, section
7.2.4); and no power IRP whose completion depends, directly or
indirectly, on the call completing.

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
  is the increment, not the current resolution; roadmap 38.3).
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

`XhciPollFineTimer` (owner, 2026-10-13, decision T4; amended by the
owner 2026-10-14, below), a `REG_DWORD` in the controller's driver key,
read at each start with section 3's values and by the same strict
reader: 1 is on; 0, absent, unreadable or not a four-byte DWORD is off;
any other value runs as 1 and the value read is recorded. Both INFs write
it on every install path with `FLG_ADDREG_NOCLOBBER`, value 0, beside
`XhciPollActiveMs`, so it is where a user looks and an update keeps a 1 a
user set (task 34.1's rule; the INF gate's value-policy table,
`check-inf.ps1` ~1323, and its footprint gain the row). Windows 98 SE and
ME read it and do nothing with it. A change takes effect at the next
start (a disable and enable), as the other poll values do.

**Amended by the owner, 2026-10-14: off by default (opt-in).** As first
decided the value was on by default: 1 written by the INFs, and absent,
unreadable or another type read as on. The test matrix's reading on
QEMU guests under TCG (no hardware acceleration), polled
(`XhciInterruptMode` 2), on Windows 7 x64, 7 x86 and Vista x86: with the
finer timer held, the guest's clock ran at 0.1 to 0.7 of real time (a
guest ticker read against the host's clock), and keys typed through
QEMU's USB keyboard were lost (132 to 199 of 199 per round on 7 x64);
with the value 0, or under an interrupt delivery (mode 0), the clock ran
at real time and no key was lost, on the same guest and boot. The
driver's own poll stayed within its periods in guest time; the guest was
starved of timer ticks. Real hardware is unread. So both INFs write 0,
and absent, unreadable or another type is off; a non-zero value still
turns it on. The rest of this section stands. Whether the default flips
back is decided on a reading on real hardware.

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
  keep `xhci98.sys` loaded while the item runs (record 18 section 14.8),
  and a system worker blocked in the call is a worker
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
  controller's children is settled**: the stop sets
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
  resume, the recovery) clears `TmrBar` and sets the event, always.
  Because a boundary reset `TmrActive`, the first ACTIVE
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

The owner keeps `XhciPollIdleMs`'s default at 50 ms (2026-10-13, at
`e2c01854`, the case for 500 ms having been found incomplete) and asks for
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
the test environment**: QEMU generates no Device
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
IRP (roadmap 38.3; Microsoft's page "Calling ExSetTimerResolution while
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
- **phase observations**: **the observation epoch**, `TmrObsEpoch`, is advanced
  by the timer thread under the controller lock immediately before each
  call (to an odd value: a call in flight) and immediately after it
  returns (to the next even value), so every hold and every release moves
  it twice. `hcdPollDpc` reads the epoch with its entry timestamp (`now`,
  taken at entry today, `src/hcd_poll.c` ~728) and again in the
  controller-lock hold where it observes (`XhciPollObserve`, ~813). Each
  run is a **sample**: its entry timestamp, its entry and observation
  epochs, the period armed for it and the start. **The baseline**
  is the last coherent sample, kept apart from the
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
`FALSE` before the thread's exit); the two schedules of section 7.2.6's
grant signal with the executor delayed - a revoke, release and grant with
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
and observation resuming after each: after an
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
table (~1323, a `XhciPollFineTimer` row: type `0x00010003`, default 1;
0 since the owner's amendment of 2026-10-14, section 7.2.4)
and its mutations in `test-inf-checks.ps1`, and the footprint; `xhcisnap`
(the names); `test/test_poll.c`; and section 8's catalogue,
`source-files.md`, the release notes' poll rows, and `build-and-test.md`'s
log lines.

#### 7.2.12 Legs

On QEMU guests, the `release` package unless a switch needs `qemu`;
each leg records the kernel image that ran and that the boot was an
ordinary one, as record 18 section 10 asks. Phase distributions are read
as snapshot differences (section 7.2.9). Since the owner's change of
2026-10-14 (section 7.2.4) the value is 0 by default, so a leg that
reads the timer on (its thread, `TmrSource`, the calls, the held phase)
sets `XhciPollFineTimer` to 1 first, on every controller it reads, and
a leg's off case or baseline is the value 0 written explicitly.

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
  a child put in D2 does not keep its D-IRP pended
  (admission publishes the Dx and the executor completes it), so the
  `qemu` flavour gains `XhciQemuSuspHoldDx` 1. **Under it the executor
  does not claim a child's pended Dx**: its pass skips the slot before
  `hcdSuspTake` (which appends the IRP to the pass's taken list; its
  caller then clears `PwrIrp`, `src/hcd_susp.c` ~2227), so the IRP stays
  in `PwrIrp`. That preserves it against **normal executor completion
  only**: the other settlement paths stay as they
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
  completion, never read back from the completed IRP; the settlement
  paths themselves unchanged; `poll.timer.call` written by the
  timer thread **before the injected delay** and `poll.timer.return`
  after the call returns, each with the start's serial and the call's
  sequence number in that start; and
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
  **Read (2026-10-14, the Phase 38 VM matrix, 7 x86 on the `qemu`
  flavour; runtime):** `qemu.stop.dx` never appeared - Windows removed
  the children before the controller stopped - so this half of T3 is
  owed and point 3 rests on the static order. The stop itself released
  the timer (release cause 4, `TmrThread` 3, nothing held, no bugcheck,
  the next start from zero). `XhciQemuSuspHoldDx` 1 also holds the
  shutdown's Dx IRP, so a shutdown under it hangs: a `qemu`-only switch
  for the stop legs, cleared before a shutdown
  (`docs/contributing/runs/run-38.md`).
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

Answered before review, then revised by the owner at `e2c01854`
(section 13.6):

- **T1: (b)**, a timer thread per controller (section 7.2.5).
- **T2: (a)**, `XhciPollActiveMs` x 10000 (section 7.2.6).
- **T3:** the request is given back when the poller leaves ACTIVE, with
  no hold of its own: the idle period is the only linger (section
  7.2.6). **`XhciPollIdleMs` keeps its default of 50**; the legs measure
  50 against 500 on every target and the default is decided on that
  reading (section 7.2.6a). (The first answer moved it to 500; withdrawn
  at `e2c01854`.)
- **T4:** `XhciPollFineTimer`, 1 by default (section 7.2.4). **Amended
  by the owner 2026-10-14: 0 by default (opt-in)**, after the matrix's
  reading on QEMU guests (section 7.2.4's amendment).
- **T5: (a)**, an update keeps an existing value (section 7.2.6a).

The helper's decisions (H1 to H5) are record 18 section 15.13's.

#### 7.2.14 As built (code, 2026-10-14)

Written with the code (branch `p38-timer`); sections 7.2.1 to 7.2.13
stand except where this list says otherwise. Nothing of it had run in a
guest when it was written; the Phase 38 VM matrix then read most of
section 7.2.12's legs (`docs/contributing/runs/run-38.md`): the helper's
install and binding (N1 to N3, D2, D3, U3 to U6), the stop (T3, its
`qemu.stop.dx` half owed, above), hibernation and shutdown (T4; standby
not makeable on any NT guest), two controllers (T5) and the clock under
the finer timer, which made the default 0. T1 and T2 were not run, and
T8 is deferred.

- **The files.** `src\hcd_tmr.c` holds the thread (`hcdTmrThread`), its
  creation (`HcdTmrCreate`, from `hcdStart` after `HcdPollGrant`), the
  stop's early bar (`HcdTmrStopEarly`, first in `hcdStopBody`) and late
  join (`HcdTmrJoin`, after `HcdSuspDoneDrain`, before the release), the
  per-start reset (`HcdTmrStartReset`, right after `HcdCountersStart`,
  before the delivery is settled and the poller opened), and the
  producers' entries (`HcdTmrActiveLocked`, `HcdTmrBoundaryLocked`,
  `HcdTmrGrantLocked`, `HcdTmrSignalLocked`, and `HcdTmrBar` for the two
  power paths). The routine's lookup is record 18's `HcdHlprTimerRoutine`
  (`src\hcd_msi86.c`) for both images, so the amd64
  `MmGetSystemRoutineAddress` row keeps `SITES=hcd_msi86.obj` (section
  7.2.11 named `hcd_tmr.obj`). The pure half - `XhciPollTmrWant`,
  `XhciPollTmrStep`, `XhciPollTmrCause`, `XhciPollTmrObserve`,
  `XhciPollTmrElapsedMs` - is in `src\xhci_poll.c` with its vectors in
  `test\test_poll.c` (`test_tmr_want`, `test_tmr_step`,
  `test_tmr_schedules` with an executor model and 20 random sequences,
  `test_tmr_observe`).
- **The step's state is set by the step**, before the call is made: the
  thread calls with no lock held, and the state, the epoch and the
  counters are recorded after the call in one controller-lock hold. A
  boundary between the inputs' read and a `TRUE` is counted
  `TmrHoldsRaced` when the generation read after the call differs from
  the one held, and released on the next step, which the boundary's event
  guarantees.
- **The producers signal only while a thread runs** (`Live`, set before
  `PsCreateSystemThread` and cleared at the join), so Windows 98 SE and ME,
  where none is ever made, take no new call on any path. The generation and
  `TmrActive` are still written there, under the lock they already hold.
- **A promotion sets `TmrActive`** only when it moved the poller (a
  cancel and re-arm, or a deadline owed to the running DPC) and its need
  is the active period; a promotion that left an earlier arm alone changes
  nothing, the poller being ACTIVE already or not running.
- **The thread's event** is re-initialised at each start rather than
  reset (`KeResetEvent` has no import-gate row; no thread waits on it
  then).
- **The phase observer runs on every poll delivery**, Windows 98 SE and ME
  included, where the epoch never moves and every interval counts as
  RELEASED - the baseline the legs compare against on the same guest.
  The period "armed for the run" is `PollPeriodLast` read at the DPC's
  entry under `PollLock`. The entry epoch is read before the entry time,
  not after it (as the code was revised): a call landing between the
  two reads then moves the epoch past the value read, so the run is
  incoherent and counts nothing, where the other order dated a pre-call
  time under the post-call epoch.
- **`TmrHeldMs`** is summed at each release from the system time's two
  words (`XhciPollTmrElapsedMs`: 2^32 units are 429,496 ms and 7,296 units
  over, no 64-bit arithmetic), saturating.
- **`TmrSource` on 32-bit Windows 2000 and XP** stays 0 until the
  thread's look, then 1 (the helper's timer service accepted) or 3; Vista
  and 7 set it at the creation from the binding the start made.
- **Counters** at words 176 to 196 in section 7.2.9's order - `TmrSwitchRead`,
  `TmrSwitch`, `TmrSource`, `TmrThread`, `TmrHeld`, `TmrHolds`,
  `TmrReleases`, `TmrReleasesForced`, `TmrHoldsRaced`, `TmrObsCrossed`,
  `TmrDesired`, `TmrActualHeld`, `TmrActualReleased`, `TmrReleaseCause`,
  `TmrHeldMs`, then `TmrObsReleasedCount`, `SumMs`, `Late` and
  `TmrObsHeldCount`, `SumMs`, `Late` - and record 18's two service
  verdicts at 197 and 198, after 38.11's three at 173 to 175 (38.10's
  quirk words follow at 199 to 232 and its ring words at 233 to 240;
  241 words in all). `XHCISNAP` names them and
  prints a section of its own (`-selftest-timer`).
- **Notes**: `poll.timer.hold` and `poll.timer.release` (the cause in the
  top byte, the returned value in the low 24 bits) as section 7.2.9;
  `poll.timer.look` (the source after the 2000 and XP look),
  `poll.timer.source` and `poll.timer.thread.failed` at the creation,
  `poll.timer.join` (the holds made) after the join; `poll.timer.call` and
  `poll.timer.return` (the start's serial in the top 16 bits, the call's
  number in the low 16), `qemu.susp.hold`, `qemu.stop.dx` and
  `qemu.susp.done` (the serial in the top 16 bits, the D-state, then the
  claiming path - 1 the executor, 2 the PDO's close, 3 the stale settle, 4
  the unlink - or `PowerPending`; the claim and its D-state travel in the
  claimed IRP's `DriverContext[3]`, written as it is claimed, so a later
  claim of another IRP of the same PDO cannot rewrite them) in the `qemu`
  flavour; the start's
  `poll.timer.switch.read`, `poll.timer.switch` and `poll.timer.desired`;
  and the stop's dump carries every counter by its `poll.timer.*` name.
- **The `qemu` switches** `XhciQemuTmrDelayMs` (held to 10,000 ms) and
  `XhciQemuSuspHoldDx` are read at each start with the delivery's values
  (`hcd_ctl.c`, not `hcd_inj.c` as section 7.2.11 said); the executor's
  skip is in `hcd_susp.c` before its `hcdSuspTake` of a Dx, and
  `qemu.stop.dx` is `HcdSuspQemuStopNote`, the stop's first step.

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


### 8.1 The D0 overlap's trace (task 45.27, the `qemu` flavour)

Since `2f57cad2` a D0 that `hcdD0Done` defers from DISPATCH_LEVEL has its
own work item and slot (`PowerWorkD0`, `PowerWorkD0Irp`), apart from the
one the system IRP's deferrals share (`PowerWork`, `PowerWorkIrp`, kinds
SYSTEM, DIRECT and SHUT; `src/hcd_power.c`, `hcdPowerDefer`). The fix is
for a deferral of one kind made while the other's is still waiting, and a
resume that completes does not show whether that ever happened
(`runs/run-45.md`, "45.27"). 45.27 owes a reading of it on 2000, XP and 7.

The reading is one trace line, in the `qemu` flavour alone
(`XHCI_FLAVOUR_QEMU`; the line is `XHCI_DBG_VALUE`, port `0xE9` and
`DbgPrint`). The release image is unchanged, so no registry value,
counter, INF row or tuning row comes with it.

- **The count.** A `qemu`-only field, `QemuPowerDeferred`, is raised
  with `InterlockedIncrement` in `hcdPowerDefer` before the item is
  queued and lowered with `InterlockedDecrement` as the first act of
  each work routine (`hcdPowerWork`, `hcdPowerWorkD0`). Each kind has at
  most one deferral outstanding (the power manager's serialization, as
  the slots already assume), so the count is 0 to 2.
- **When.** The increment's own result is 2: this deferral was made while
  the other kind's work routine had not begun. The value comes from one
  interlocked operation, so it neither misses nor invents that case; the
  two items' queueing order is not changed. The line says "deferred
  together", the case the separate slots are for, not that both items
  sat in the queue at one instant.
- **What.** `"hcd: power defer overlap"` with the value
  `(kind << 16) | count`, `kind` being the deferral just made
  (`HCD_POWER_WORK_D0` 1, `SYSTEM` 2, `DIRECT` 3, `SHUT` 4). The other is
  the other class (the D0 item when `kind` is 2 to 4, a system item when
  it is 1). `XHCI_DBG_VALUE` is not bounded, which is right here: power
  IRPs come a few per transition.
- **IRQL.** `hcdPowerDefer` runs at DISPATCH_LEVEL or below; the
  interlocked routines and the trace channel accept that
  (`src/xhci_dbg.h`), and both routines are in the import allowlist for
  every flavour.
- **The leg.** The `qemu` flavour on 2000, XP and 7, hibernations and
  standbys repeated under load, the port `0xE9` log searched for the line.
  A line read with the resume completing and the devices back proves the
  case was met and survived. No line in many transitions says only that
  it was not met there; the leg then stays owed to hardware.

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

C4 is not converted wholesale. Its fail covers several causes (revision
1): the DPMI interrupt hook not installed (`bringup.c:580`), no
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
line's. One POLL differs in its advice: when `IP`
was never seen, at any look through both of C4's waits, the driver's
automatic mode cannot see the dead line - section 4.2's watcher moves on
`IP` asserted with nothing claimed - so the caveat names
`XhciInterruptMode` 2. Its message route is still named from the
capability list, said as untested; and since an enabled MSI or MSI-X
clears `IP` itself after its message (4.17.5), one found enabled (Message
Control, read only) is named as a reason `IP` may not have been seen. A
source that comes live late, `EINT` and `IP` both up after the first wait,
goes on to the delivery test as a prompt one does. Every
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
    (revision 5: the No Op is submitted and the
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

9. (2026-10-13, task 38.3; revision 8; revised by the owner at
   `e2c01854`) A finer system timer while polling, on Windows 2000 and later
   (section 7.2): T1, the call on a timer thread of each controller's; T2,
   the active period x 10000 asked for; T3, held while the poller is
   ACTIVE and given back when it is not, the idle period the only linger,
   `XhciPollIdleMs` kept at 50 and measured against 500 on every target
   before any change (section 7.2.6a); T4, `XhciPollFineTimer`, on by
   default (amended by the owner 2026-10-14: off by default, section
   7.2.4); T5, an update keeps an existing value. The helper's H1 to H5
   are design record 18 section 15.13's.

10. (2026-11-10 and 2026-11-11; task 49.24; revision 10; a design, not
    built) `XhciInterruptMode` 4, a message-signalled interrupt on 2000,
    XP and XP x64 through the MSI stability improvements: never chosen
    by 0; no fallback - a connection failed, refused or lost fails the
    start or the controller, logged; `XhciMsiStrategy` 0 a claimed ISA
    interrupt (the default), 1 the controller's own line borrowed on the
    user's assertion with the driver's veto; `XhciMsiIsaIrq` the forced
    IRQ; a shared claim a user value, on by default only after the legs
    and the owner's bench, an interrupt of the controller's own first,
    this driver's controllers only, with a member cap; `XhciMsiAvoid`
    obeyed; a quarantine keeps the driver loaded and refuses that
    controller's every later start until restart; the header MIT, each
    client image carrying the library's signature
    string. Section 4.5.1 has them in full, section 4.5.13 what is still
    the owner's to decide. **Amended 2026-11-11 (revision 11):** the
    eleven points revision 10 left open are answered (section 4.5.13's
    table: `XhciMsiShare`; `XhciMsiIsaIrq` in the installer; 256 and
    500 ms; the backstop as the watchdog, eight in 60 s; the No Op's
    500 ms; the lost message reported on 2000 and XP only; the replug;
    the header taken in once; the pool's names; no exclusive-device
    request), and the installer offers the message as two separate ticks
    (the USB 3 controllers now, the Thunderbolt and USB4 host with task
    49.27; section 4.5.10). The 98 SE and ME message path is not part of
    `3.5.0.0` (owner, 2026-11-13). **Amended 2026-11-13:** sharing on by
    default; `XhciMsiShare` repurposed, no second value: the most
    controllers on one IRQ before another is taken, 1 to 16, absent 8, 0
    not offered; the shared line first, the fullest one (section 4.5.19).

## 13. The revisions

### 13.1 Revision 1

| # | Problem | Where answered |
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

| # | Problem | Where answered |
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

| # | Problem | Where answered |
|---|---|---|
| 1 | CLOSED was terminal; a stop and start never reopened the poller | Section 5.2: initial state CLOSED, opened at each poll-delivery start |
| 2 | A clock step breaks the deadline bound | Section 6.2: every deadline checked where it becomes an arm, every arm between the active and idle periods |
| 3 | Phase 35's legs still waited on Phase 36's standby | Roadmap POLL-V, as section 11 already had it |
| 4 | Phase 36's checkpoint asked a `qemu`-only leg of the release package | Roadmap Phase 36 checkpoint |
| 5 | Record 14's `34-V`, its baseline and run sheet; the roadmap's docs task citing a list this record had lost | Record 14; section 14 below; roadmap SUSP.3 |

### 13.4 Revision 4

2026-10-05: one wording correction, the roadmap's Phase 35 status still
naming revision 3. Nothing else changed in the reopen, the deadline
conversion, the phase split or the cross-references.

### 13.5 Revision 5 (task 36.5)

Reviewed and revised 2026-10-09 in task 36.5, in eight commits; what each
answered is below.

At `a744811`: 4 P1, 5 P2, all answered.

| # | Problem | Where answered |
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

At `1bc3546`: 3 P1, 3 P2, 1 P3, all answered.

| # | Problem | Where answered |
|---|---|---|
| 1 | P1: the start's grant came before `hcdStart` publishes D0 | Section 5.3: the start's grant in `hcdStart`, after the D0 publication |
| 2 | P1: the retirement raced the DIRQL ISR, which declines on `EINT` 0 | Section 4.2, step 3: `KeSynchronizeExecution` around `XhciRetireLine`; `IP` before `EINT`, so an unproven step leaves the ISR able to claim; the poller closed before the disconnect (section 5.2) |
| 3 | P1: a failed header test sent the walk below the image base | Section 4.3: the walk runs only on Vista and 7 builds whose kernels were read; it stops at the first recognised header and never reads below it |
| 4 | P2: the grants' place against the power gate | Section 5.3: every grant inside the power gate; the recovery's on its final `ok`, before the gate is left; the D0 paths' only on a resume that succeeded |
| 5 | P2: N = 0 armed the latch after the No Op's interrupt was enabled | Section 11: the latch set before the connection for N = 0 |
| 6 | P2: a retry repeated the acknowledgement without the both-enables proof, and `IP` can rise again | Section 4.2, step 3.1 to 3.3: both enables proven each attempt; a re-set `IP` with `IE` 0 told from the standing one |
| 7 | P3: roadmap 36.15 still argued one walk and one message | Roadmap 36.15: amd64's call, x86's walk, a multi-message grant served as one |

At `b814ee3`: 2 P1, 1 P2, 1 P3, all answered.

| # | Problem | Where answered |
|---|---|---|
| 1 | P1: a failed `IP` step left `InterruptDeliverySuppressed` set, so a later suspend or recovery opened the ISR's old decline gate | Section 4.2: step 1 no longer sets it; `LineMoved` keeps the ISR claiming while a retirement is owed; the HCD retries before a suspend, a recovery and the disconnect; a completed HCRST retires it |
| 2 | P1: a build number does not prove the running kernel's layout | Section 4.3: x86 reads no kernel memory - it parses the kernel's file and proves the identity by every import resolving to one base; roadmap 36.15 |
| 3 | P2: a park against a close asked after the DPC took ownership | Sections 5.2 and 5.3: the park turns CLOSING into CLOSED; a revocation cancels no timer, stated; vectors |
| 4 | P3: the retirement's `EINT` write broke Table 5-21's order | Section 4.2, step 3.3: `EINT` is not acknowledged |

At `f4d8a28`: 2 P1, 2 P2, all answered.

| # | Problem | Where answered |
|---|---|---|
| 1 | P1: agreeing anchors do not prove the target's RVA | Section 4.3: amd64 through `MmGetSystemRoutineAddress`; x86 through an NT 6-only helper driver designed separately (owner, 2026-10-09, after the review), the line without it; roadmap 36.15 |
| 2 | P1: an unproven retirement and the ISR's lifecycle gates | Section 4.2: why the ISR path never acknowledges this assertion; the unproven outcome recorded and ended by the next HCRST |
| 3 | P2: CLOSED published before the DPC's last controller access | Section 5.2: CLOSING kept through the park; CLOSED in a `PollLock`-only epilogue |
| 4 | P2: the file parser's callable-address and short-read checks | Moot with problem 1: the file parser is withdrawn |

At `5508b61`: 2 P1, 1 P2, all answered.

| # | Problem | Where answered |
|---|---|---|
| 1 | P1: PARKED published before the DPC's last access; a grant could arm under a parking DPC | Sections 5.2 and 5.3: `ParkPending` and `KickOwed`; ownership given up only in the `PollLock`-only epilogue |
| 2 | P1: a finite verdict is not permanent unreachability; containment needed | Section 4.2: the ISR keeps its path; escalation to PCI INTx Disable, then the recovery's HCRST, then a recorded uncontained terminal |
| 3 | P2: the acceptance legs still asked MSI of x86 | Roadmap decisions table, 36.15 and 36.16; section 4.3's legs |

At `ded0bbf`: 1 P1, 2 P2, all answered.

| # | Problem | Where answered |
|---|---|---|
| 1 | P1: clearing Interrupt Disable before the reset could re-expose a pending `IP` | Section 4.2: cleared only after `XhciInitController` succeeded, just before `XhciEnableInterrupts`, read back; left set on a failed start; a stuck bit handled per mode |
| 2 | P2: stop and suspend could bypass the PCI step | Section 4.2: `HcdPollContain`, under the power gate in D0, called by the thread, both suspend paths, `hcdRecover` and the stop after the thread ends |
| 3 | P2: "latched failed" did not enter the recovery machinery | Section 4.2: the recovery asked through `XhciRequestControllerReset`, its existing bounded retries and terminal kept; `LineRetired` set at HCRST completion |

At `8cee483`: 1 P1, 1 P2, both answered.

| # | Problem | Where answered |
|---|---|---|
| 1 | P1: containment could write MMIO during a reset or on a bad layout | Section 4.2, step 3: `HcInfoStatus` admission, HCRST/CNR and operand guards; the PCI step taken without MMIO |
| 2 | P2: the watcher could create an obligation after a lifecycle check | Section 4.2, containment step 2: `HcdPollRevoke` before the check |

At `59e0959`: no P1 or P2 left, and the design judged sound enough to
implement. Two P3s answered: `HcdPollRevoke` named among the
revocation's writers (section 5.3), and what `LineRetired` proves stated
for all three of its proofs (section 4.2).

### 13.6 Revision 8 (task 38.3)

Drafted 2026-10-13 and put to the owner for section 7.2.13's decisions
before any review (the owner's rule for Phase 38's design tasks:
decisions first, review after); the answers written in the same day,
with the idle default's change (sections 3 and 7.2.6a) and the open T5.

At `e2c01854`: 2 P1, 8 P2, 3 P3, with an opinion on each owner answer;
the owner then revised T3 (the 500 ms default withdrawn, measured
instead), T5 (option (a)) and record 18's H2 (no forced restart). All
answered in that commit:

| # | Problem | Where answered |
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

At `50c0dfc3`: 1 P1, 3 P2, 2 P3. Problems 1, 3 to 5 and 7 to 13 of
`e2c01854` held; 2 and 6 were narrowed. All answered:

| # | Problem | Where answered |
|---|---|---|
| A | P1: the tolerance backstop is a stale-event detector (tolerance 1, four ticks), not a peek | Sections 7.2.2 and 7.2.6a |
| B | P2: an idle 50 ms request cannot show the clock; boundary-crossing intervals counted | Section 7.2.9: short-request phase observations, crossings discarded, a PERIODIC released-phase stimulus; legs T2 and T5 by windows, T5 with no stated expectation |
| C | P2: a child in D2 has no pended D-IRP | Leg T3: `XhciQemuSuspHoldDx`, the IRP shown outstanding, the ordered notes |
| D | P2: QEMU raises no Function Wake; no x64 test client | Section 7.2.6a and leg T8: Function Wake unread on QEMU, the x64 wakes owed |
| E | P3: WATCH creates a parked thread | Record 18 section 15.4 |
| F | P3: the header gate cannot show the target | Record 18 section 15.10: the claim limited; the build log's target line and library path checked |

At `7dde5165`: 1 P1, 3 P2, 1 P3. A, D and E held; B, C and F were
narrowed. All answered:

| # | Problem | Where answered |
|---|---|---|
| 1 | P1: `XhciIntervalCap` caps at 32 ms and cannot make a 2-5 ms probe | Section 7.2.9: an endpoint with a native short interval, named from the model's descriptors, the reading owed where none exists, acceptance needing a recorded PERIODIC period and a non-zero count |
| 2 | P2: equal phases at both ends miss an ABA and a transition during the drain | Section 7.2.9: the observation epoch, moved before and after every call, read at the DPC's entry and at its observation; vectors in section 7.2.10 |
| 3 | P2: the library gate rejected the CRT libraries subsystem 5.00 adds | Record 18 section 15.10: the CRT libraries enumerated and allowed; fixtures |
| 4 | P2: T3's injection cleared the slot; the stop's dump comes after the drain; unlink only hands off | Leg T3: the claim skipped before `hcdSuspTake`, the entry note, the completion worker's own note, the ordering rule |
| 5 | P3: stale "phase bins" in the test and integrator lists | Sections 7.2.10 and 7.2.11 |

At `5ba0b6f2`: no P1, 3 P2. Problems 1, 3 and 5 of `7dde5165` held; 2
and 4 were narrowed. All answered:

| # | Problem | Where answered |
|---|---|---|
| 1 | P2: a rejected sample left the baseline stale, so counting could stop for good | Section 7.2.9: coherent samples seed or count and always become the baseline, incoherent ones invalidate it; vectors showing observation resumes |
| 2 | P2: T3's order did not prove the delay overlapped the outstanding IRP | Leg T3: call and return notes with sequence numbers, the call note before the delay, the required order with the completion inside the call |
| 3 | P2: the skip does not make the unlink the only settlement path | Leg T3: the claim narrowed to normal executor completion, the claiming path recorded, the stop-entry condition a reading that may be owed |

At `9557fe74`: no P1, 1 P2, 1 P3. Problems 1 and 2 of `5ba0b6f2` held; 3
was narrowed. Both answered:

| # | Problem | Where answered |
|---|---|---|
| 1 | P2: a worker-only completion note misses the PDO close's inline completion | Leg T3: the note at `hcdSuspCompletePower`, for inline and worker callers, from metadata saved before the completion |
| 2 | P3: the recovery vectors added a seed after a coherent mismatch | Section 7.2.10: the incoherent and coherent cases split |

At `ffaaf528`: both problems of `9557fe74` held, and no P1, P2 or P3 was
left.

### 13.7 Revision 9 (task 46.6)

Section 4.4, 2026-11-03, reviewed and revised: the start's walk for a
device holding this controller's line on 98 SE and ME, recorded and
shown, diagnostic only. It takes step 2 of the draft task 45.33 kept
out of the tree (`runs/run-45.md`, "45.33"), without the move to
polling, and neither step 1 (this controller's own Interrupt Disable
before the connection) nor step 3 (the storm detector), which 45.33
read not to work.

### 13.8 Revision 10 (task 49.24)

Section 4.5, 2026-11-11, reviewed and revised: `XhciInterruptMode` 4
designed before its code, from the owner's
decisions of 2026-11-10 and 2026-11-11, against the MSI header in state A
(section 4.5.11 lists every dependency on it), with what stands of the parked branch `w48-mode4` (section
4.5.12) and the points left to the owner (section 4.5.13). A design
only: no value, INF row, gate row or user text follows from it yet.

### 13.9 Revision 11 (task 49.24, carried on)

(With the owner's answers of the same day to the nine points it first
left open: section 4.5.13's second table.)

Sections 4.5.1 to 4.5.14 and the new 4.5.15, 2026-11-11, reviewed and
revised: the owner's answers to revision 10's open points written into
the sections they touch; the installer's two ticks (section 4.5.10);
and section 4.5.15. Still a design only.

### 13.10 Revision 12 (task 49.24, the code)

Section 4.5.16, 2026-11-11, reviewed and revised, before the code: what
the code is built against (the MSI header's `main` in state C) and what that changes in sections 4.5.1 to 4.5.14 - the
second claim pool's four values on 32-bit 2000 and XP and what is
recorded of it, the grown structures, two names more, Interrupt Disable
kept set after a disconnect, and the seam files.

### 13.11 Revision 14 (task 49.24, 2026-11-13)

Section 4.5.19: the owner's decisions of 2026-11-13 on sharing (on by
default; `XhciMsiShare` the most controllers on one IRQ before another is
taken, 1 to 16, absent 8, 0 not offered; the shared line first, the
fullest one), the header's new copy, and the legs on XP behind them; the
places of sections 4.5.1 to 4.5.13 that described the old rule marked
as superseded. The 98 SE and ME code's removal of the same day is
recorded in sections 3 and 4.5.7.

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
