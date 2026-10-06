# Controller tolerance

Design record for roadmap-hcd tasks 35-T.0 to 35-T.9 (Phase 35, release
`2.2.0.0`). Revision 2, 2026-10-06, written before code. Revision 0
(`60f8f64`) drafted the findings and the shape; revision 1 (`4a63a35`)
answered review round 1's eleven findings; review round 2 found five of
them resolved, four in part and two not, and raised thirteen findings of
its own. This revision rests the recoveries on a reading of the recovery
machinery as it is (section 4.0), and section 10 maps every finding of
both rounds to where it is answered. Nothing below is converged.

## 1. What is asked, and what is not

A tester on an AMD AM5 board reports that a USB mouse randomly stops
working: "basically unusable" under `2.0.0.0`, and under the `2.1.x` builds
it stops less often, and after being moved to another port it works for
longer. Which controller sits behind each port, and what the driver saw,
are not known: no controller id and no `XHCISNAP` dump has been sent. A
third-party report from before `2.0.0.0` (the README's tested table) has the
miniport failing on an X570 board while B550 and X670 worked.

What Linux does with these controllers is a hypothesis here, not a reading.
Of the AMD ids an AM5 or X570 board may carry, the local mirror's
`xhci-pci.c` gives a quirk flag to none but `43F7` (runtime power
management only), and routes `43FC` and `43FD` (Promontory 21) to a
separate glue driver when `CONFIG_USB_XHCI_PCI_PROM21` is set
(`xhci-pci.c`, the table at lines 701 and 702 and the check at 712), which
the mirror does not carry. Which of these ids the tester has, and how Linux
behaves on that machine, nobody here has observed. The hypothesis is that
Linux's robustness on them comes from behaviour it applies to every
controller, on the paths section 3 lists; the design does not rest on it,
since each behaviour stands on the specification and on what this driver
does today.

Asked (owner, 2026-10-06): the faults that leave a device silently dead
until it is replugged are tolerated, on every controller, wherever the
tolerant behaviour meets section 2's rule; what does not meet it is gated,
Linux's way, by controller id with a registry value to override the gate;
and a user's dump can tell the faults apart.

Not in this design: suspend, resume and runtime power quirks (Linux's
`XHCI_SUSPEND_DELAY`, `XHCI_SNPS_BROKEN_SUSPEND`, `XHCI_U2_DISABLE_WAKE`
and their kin); the isochronous AMD PLL quirk (`XHCI_AMD_PLL_FIX`, SB700 to
Bolton only); link power management, which this driver never enables;
MSI; ASMedia's flow-control register writes
(`XHCI_ASMEDIA_MODIFY_FLOWCONTROL`); and Intel's `XHCI_MISSING_CAS`, which
is Phase 35's own task 35.3.

## 2. The rule: tolerant by default, gated by exception

A behaviour is unconditional when all of these hold:

- it is legal under xHCI 1.2c, and every command it issues meets that
  command's state preconditions;
- on a controller that never raises the fault it costs nothing beyond a
  bounded register or memory read on a path already taken, and it changes
  no field the controller is told on a healthy device's path (no context
  field the controller uses for admission or scheduling);
- the controller never loses a TRB or a buffer it still owns: a transfer is
  completed only once its TRBs are retired, by the controller's own event
  or by a stop the driver issued and the controller confirmed;
- it is bounded by an episode budget (section 4.9), so a fault that
  persists ends in a stated terminal state and nothing loops;
- it does not change what a class driver sees from a healthy device, and
  after a fault it shows a class driver only what that driver already sees
  today - an error completion the driver already gets for the same fault,
  or a device departure and arrival (section 4.10).

A behaviour that fails any of these is gated - a pure-core decision on the
PCI vendor and device id and a REG_DWORD in the controller's driver key
with `XhciMissingCas`'s three values (1 the default, the list; 0 off; 2
every controller), written by both INFs under 34.1's rule - or offered as
a switch that is off by default (section 4.7), or not done.

## 3. The findings

Read in this tree at `08df41f` on 2026-10-06, corrected at revisions 1 and
2. Each is a path where a fault leaves a device dead and nothing notices.

| # | Path | Today | Linux (mirror) |
|---|---|---|---|
| T1 | Event delivery | The event-ring drain is queued only by the ISR (`hcd_ctl.c`, `hcdIsr`, which counts `DpcsInFlight` before `KeInsertQueueDpc` and rolls it back on a refused insertion); the 100 ms health poll checks HCE, HSE and command age. A lost line interrupt, or EHB or IE left stuck outside an ISR-to-DPC handoff, silences the bus. The health poll's re-arm is the miniport's `XHCI_FIX_EVT_REARM`, built in no flavour (`xhci_cmd.c`) | No backstop; nothing to copy |
| T2 | USB Transaction Error | Completes the TD at once with `USBD_STATUS_DEV_NOT_RESPONDING` (`xhci_xfer.c`, `XhciXferCodeInfo`) | Soft retry: Reset Endpoint with TSP 1 and the TD kept, while the endpoint-wide `err_count++ > MAX_SOFT_RETRY` (3) is false - four retries from zero - not behind a TT, not on `XHCI_NO_SOFT_RETRY` controllers (`xhci-ring.c`, `process_bulk_intr_td`, line 2531) |
| T3 | Codes nothing claims | Vendor information codes 224 to 255 already complete as success and vendor error codes 192 to 223 as a fatal Undefined Error (`xhci_xfer.c`, `XhciXferCodeInfo`, line 103): both kept. What is refused is the rest - Endpoint Not Enabled, Ring Underrun and Overrun and Missed Service outside the isochronous path, Bandwidth Overrun, Event Ring Full in a Transfer Event, and unassigned codes - most of them carrying no TRB pointer: `BadCodes` is counted and nothing else. The refusal is a recorded deviation whose recovery was usbport's URB timeout and the Stop Endpoint its cancellation issued (the comment at `xhci_xfer.c` line 270; `implementation-invariants.md`, "Fatal Errors"); the HCD has no usbport, so only a cancel the class driver happens to send reaches a Stop Endpoint | An unrecognised code is logged and the handler returns before its halted-endpoint check (`xhci-ring.c`, `handle_tx_event`, line 2769) |
| T4 | A halt with no TD | A halting event the queue cannot match leaves the endpoint Halted with no URB failed: `ForeignEvents` counts a slot or DCI mismatch and any pointer `XhciRingIndexFromPA` refuses, zero included; `UnmatchedEvents` a pointer on the ring inside no queued TD (`xhci_xfer.c`, lines 1906, 1949, 1967). Submissions to the Halted endpoint are published and rung and the controller ignores the doorbell. Client control transfers on EP0 have no timeout in the HCD. The one handled case is a streams endpoint's Prime Pipe STALL (`hcd_dev.c`, `XhciSlotTransferEvent`) | `check_endpoint_halted`, for an event that names a live endpoint, with Transaction, Babble and Split errors qualified as non-isochronous (`xhci-ring.c`, line 2191); `prepare_ring` admits submissions to a Halted endpoint (line 3260) |
| T5 | Root port disabled or over-current | A USB 2.0 root port with PED cleared and CCS set is acknowledged and nothing more (`hcd_enum.c`, `hcdPortChanged`; `xhci_link.c`, `XhciLinkPortFeed`); `GET_PORT_STATUS` answers enabled from the PDO's state (`hcd_urb.c`, `hcdPortStatus`). OCA and OCC are never read by the HCD, and PP is written only at start and teardown (`xhci_init.c`, `xhciDrivePortPower`), so an over-current that cleared PP leaves the port unpowered. The external hub path re-enumerates on both (`xhci_hub.c`, `XhciHubPortDecide`) | `hub.c`, which is not in the mirror |
| T6 | Controller halted or unreadable | An unexpected HCH is not looked at (`xhci_cmd.c`, `XhciControllerHealthPoll`). An all-ones USBSTS is counted (`HealthPollsDead`) and read as a window that stopped decoding, not a fatal report - a recorded invariant (`implementation-invariants.md`, "Fatal Errors"), kept - and nothing else follows, so I/O waits. `RecoveryFailuresConsecutive` is cleared by every completed recovery (`xhci_init.c`, line 4190), so it bounds a run of failed recoveries, not a recover-and-fail-again cycle | - |
| T7 | Long interrupt intervals | Interval from bInterval, as Linux's base rule (`xhci_pipe.c`) | `XHCI_LIMIT_ENDPOINT_INTERVAL_9` caps it at 8 (32 ms) on older AMD ids (`xhci-mem.c`, `xhci_endpoint_init`) |
| D | Diagnosability | `XHCIHC_COUNTERS` lies outside the snapshot `XHCISNAP` copies (`hcd_door.c`); the per-queue `Errors`, `BadCodes`, `UnmatchedEvents` and `ForeignEvents` are published nowhere; `XhciLogErrorBudget` has no HCD caller | - |

## 4. The behaviours

### 4.0 The machinery the recoveries are built on

Read on 2026-10-06; the design uses these as they are, or says where it
changes them.

- Every recovery command runs on the controller thread, which serializes
  them; a cancel, ABORT_PIPE, RESET_PIPE, SYNC_RESET_PIPE and RESET_PORT
  each pause the pipe, and so does the thread's own control transfer.
- `Paused` is a nesting counter with no owner (`hcd.h`, line 240; `hcd_io.c`,
  `HcdIoPipePause` and `HcdIoPipeResume`, lines 1323 and 1337); resume
  releases held submissions in order. A submission while paused is held;
  one while not paused is published and rung, Halted or not.
- `hcdCfgQuiesce` (`hcd_cfg.c`, line 2224) brings an endpoint to Stopped
  and relocates the controller to the **software** dequeue, assuming the
  engine has already retired the failed TD (lines 2246 to 2250).
- `hcdCfgCancelPipe` (line 2717) is the one path that keeps survivors: a
  cancelled head moves the dequeue to the first survivor with Set TR
  Dequeue, a cancelled TD elsewhere becomes No-Ops in place, and the
  survivors are restarted by a doorbell (lines 2804 to 2811).
- No helper reads the Endpoint Context's TR Dequeue Pointer, and nothing
  maps a hardware dequeue to a queued TD; the field's masks exist
  (`xhci.h`, lines 1354 and 1355).
- `XHCI_EPQ_NO_CONTEXT` has no consumer in the HCD; `hcdCfgRecycle` (line
  2365) is the Drop and Add Configure Endpoint that puts a context back,
  with the rings emptied.
- There is no configuration generation; an event is mapped to a pipe by
  slot and DCI alone (`hcd_dev.c`, `hcdEventPipe`, line 231).
- A device the bus drops completes its transfers `DEVICE_GONE`, which are
  parked on the PDO and later completed `STATUS_CANCELLED`, unless the
  common buffer is pinned (`hcd_io.c`, `HcdIoDeviceDrain`, line 1893, and
  `HcdIoPark`, line 2028); a refusal is completed by a 1 ms DPC, never
  inline. Windows 98 SE's `hidclass.sys` answers a read failing
  `STATUS_DEVICE_NOT_CONNECTED` by resubmitting at once while its device is
  started (`hcd_io.c`, lines 1947 to 1953), so this design never returns
  that status for a device that is still listed.
- `HcdEnumCycle` re-enumerates a device on its port: the PDO departs and a
  new one arrives, which every target's class drivers handle as a removal
  and an arrival. It is what a user's replug does.

### 4.1 The lost-interrupt backstop (35-T.1, unconditional)

Each health poll that its existing admission lets read (`HcInfoStatus`
good, `INITIALIZED` set, the controller not failed, D0, `DpcClosed` clear)
reads, under the controller lock, the event TRB at the software dequeue
pointer, and compares it with what the previous poll saw. The drain is
queued only when an event was already pending at the previous poll, at the
same dequeue pointer and cycle: the controller wrote it at least one poll
period ago and nothing has drained it since. A pending event seen for the
first time is left to the interrupt, so moderation and an ordinary
interrupt in flight are not raced. A lost interrupt is therefore recovered
in 100 to 200 ms.

The queue goes through the ISR's admission: `DpcsInFlight` counted before
`KeInsertQueueDpc` and rolled back on a refusal, nothing once `DpcClosed`
is set. The insertion can succeed while a drain is executing on another
processor, since a running DPC is no longer queued; the second drain
serializes on the controller lock and finds the ring empty. That costs one
DPC, and only after an event has waited a whole poll period. It writes no
register: ERDP and EHB stay the drain's, and IE, clear between the ISR's
acknowledgement and the DPC's re-arm, stays the DPC's to restore.
`XHCI_FIX_EVT_REARM` is retired. One helper serves the peek; record 15's
opportunistic checks reuse it with their own staleness rule.

Its count is `BackstopDrains`: drains the backstop queued. Under the
two-poll rule a nonzero count is strong evidence of a lost or late
interrupt, not proof of one. Cost on a compliant controller: one cached
memory read per 100 ms.

### 4.2 The soft retry (35-T.2, unconditional but for an exclusion list)

**Scope.** A USB Transaction Error on a bulk or interrupt endpoint that has
no streams, whose device is not behind a TT, on a controller not on Linux's
`XHCI_NO_SOFT_RETRY` list (`1022:43B9`, `1022:43BB`, `1B6F:7023`,
`1B6F:7052`). EP0, isochronous and stream endpoints keep today's handling.

**The engine.** For such an error the transfer engine does not retire or
complete the TD: the TD stays the queue's head, the software dequeue stays
on it, and the pipe is marked `RetryWanted` with the thread woken. The
endpoint is Halted; submissions arriving now are published and wait behind
the doorbell the controller ignores, as today.

**The owner.** The retry is a pipe state, `Retry`, owned by the thread:
NONE, then WANTED (set by the engine), then RESETTING (the thread has taken
one pause and issued the command), then NONE. On the thread, before
issuing: if a cancel, ABORT_PIPE, RESET_PIPE, SYNC_RESET_PIPE or RESET_PORT
is pending on the pipe or its device, or `DrainPending` is set, the retry
is abandoned without a command - the TD is completed with its error through
the drain machinery, as today, and the pending request proceeds from the
Halted endpoint it already handles. Otherwise the thread pauses the pipe
once, sets RESETTING and issues Reset Endpoint with TSP 1, which xHCI 1.2c
4.6.8.1 provides so the host's sequence state is kept and the device's data
toggle stays aligned without a CLEAR_FEATURE. The retry path issues no Set
TR Dequeue Pointer and no CLEAR_FEATURE, and keeps the TD's mapping.

When the command completes the endpoint is Stopped with its dequeue on the
halted TD. Under the pipe lock the thread checks the same pending set again
and decides in one step: none pending, it rings the doorbell, the controller
resumes at the halted TRB, and the thread sets NONE and resumes the pipe -
the one pause it took, so held submissions are released after the doorbell;
something pending, it leaves the endpoint Stopped, sets NONE and resumes,
and the pending request runs next from Stopped, which every one of them
already handles. A failed Reset Endpoint goes to `hcdCfgFault`, as any
recovery command does. A multi-TRB TD resumes at the TRB that failed, as
Linux's does.

**The bound** is this project's: three retries per TD, counted on the TD and
gone with it (Linux allows four, endpoint-wide). The fourth Transaction Error
completes the TD with its error as today, and is charged to the endpoint's
episode (section 4.9).

Cost on a compliant controller: nothing, since no Transaction Error arrives.

### 4.3 The endpoint recovery (35-T.3 and 35-T.4, unconditional)

One sequence, `hcdCfgRecoverEndpoint`, serves both a code nothing claims
(T3) and a halt with no TD (T4). It is driven by the endpoint's state, not
by the event, so an event cannot make it act on a TD it does not own. On
the thread, under one pause of the pipe:

1. **Stop.** Read the Endpoint Context. Running: Stop Endpoint, Context
   State Error accepted and the state read again, as `hcdCfgQuiesce` does.
   The endpoint is now Stopped, Halted, Error or Disabled.
2. **Capture.** Read the Endpoint Context's TR Dequeue Pointer and DCS (a new
   pure helper over the output context) and map it to the queued TD that
   contains it, if any (a new pure helper over the ring and the queue).
   This is the controller's position, captured before anything moves it.
3. **Attribute.** If the state is Halted or Error and the captured position
   lies inside a queued TD, that TD is the one the fault stopped: it is
   marked cancelled with `USBD_STATUS_INTERNAL_HC_ERROR` (or the halting
   code's own status, for T4). If the state is Stopped, or the position lies
   in no TD, no TD is attributed and none is failed.
4. **Move the software dequeue to the captured position** if they differ,
   so the existing helpers, which relocate to the software dequeue, act on
   the controller's position and not on an assumption.
5. **Recover through the cancel path.** `hcdCfgCancelPipe`'s machinery:
   Halted takes Reset Endpoint TSP 1 and Set TR Dequeue; the attributed TD,
   now the head, is removed and the dequeue moved to the first survivor;
   the survivors are restarted by a doorbell; the attributed TD completes
   through the drain. `Halted` is cleared.
6. **Disabled** (Endpoint Not Enabled): no TRB can be owned by a disabled
   endpoint, so every transfer on the pipe is completed
   `USBD_STATUS_INTERNAL_HC_ERROR` through the drain, the pipe is recycled
   by `hcdCfgRecycle`, and the pipe resumes empty.
7. Resume the pipe: the one pause the sequence took.

**When it runs.** T3: any Transfer Event with a code `XhciXferCodeInfo`
refuses, whose slot and DCI name an open pipe, on a non-isochronous
endpoint. T4: a Stall, or a Transaction, Babble or Split Transaction error
on a non-isochronous endpoint, that the queue could not match - Foreign by
pointer (zero included) or Unmatched - whose slot and DCI name an open pipe.
A slot or DCI with no open pipe is counted and dropped as today. The event
only requests the sequence; the sequence's state check decides. A stale
event that names a pipe now Running in a new configuration costs one Stop
Endpoint and a restart in place, and is charged to the episode.

**Streams.** The Prime Pipe STALL path is kept as it is. Any other T3 or T4
event on a streams endpoint requests the existing endpoint abort
(`hcdCfgAbort`), which drains every stream with the status and empties
their rings, the streams staying open; per-stream attribution is not
attempted.

**EP0.** Client control transfers and the thread's own share `Ep0Pipe`
(`hcd_dev.c`, `hcdEp0Result`, line 103). The same sequence runs on EP0,
with `hcdResetEp0`'s commands (TSP 0, since a control transfer's Setup
stage resets the toggle), and the attributed transfer is completed whichever
owner it belongs to: a client transfer through the drain, the thread's own
by its existing outcome path. Client control transfers have no timeout
today, and this design adds none; it removes the case in which one waits on
a Halted EP0.

The deviation in `implementation-invariants.md`, "Fatal Errors", is
superseded and rewritten (35.6), and the comment at `xhci_xfer.c` line 270
corrected.

### 4.4 Submissions to a Halted endpoint

Unchanged: published and rung, the doorbell ignored until the recovery, and
the sequence's survivor restart rings it again. Revision 0's failing them
was withdrawn at revision 1.

### 4.5 The root port the controller gave up on (35-T.5, unconditional)

**PED cleared.** A USB 2.0 root port with PEC set, PED clear, CCS set and a
device held is fed a disconnect. Once the disconnect's teardown has
completed - the PDO reported missing, the slot disabled - the thread reads
PORTSC afresh, and feeds a connect only if CCS is still set and PP is set.
As `XhciHubPortDecide` does for an external hub. `GET_PORT_STATUS` keeps
the PDO-state answer and its race rule; the old PDO is no longer listed and
answers 0.

**Over-current.** A root port with OCC set, or found with PP clear while
the driver's own state says it powered it, is held and its device fed a
disconnect. The thread then watches OCA on each pass: once OCA reads clear
on two passes in a row, it sets PP, waits the power-on interval, and reads
PORTSC afresh before looking for a connection. A port whose OCA does not
clear within the over-current wait stays held.

**Episodes.** Each port has one episode for both, armed by the first fault.
Its budget, charged when the recovery action starts, is three re-enumerations
and three repowers. Transitions the recovery itself causes - CCS falling
because PP was cleared, the driver's own disconnects - never re-arm it, and
the budget survives a repower. It is re-armed by evidence of a genuine
change only: CCS observed clear with PP set and no fault active for the
stable-disconnect interval, followed by a connection; or the device
enumerated and working for the stable-progress interval. Exhausted, the
port is held in a terminal state the dump shows (`hold` with its reason),
released only by that stable-disconnect evidence or a controller start.

### 4.6 The controller halted or unreadable (35-T.6, unconditional)

**HCH.** HCH read set while the driver's own state says the controller runs
- D0, started, not stopping, not in recovery, not in a power transition,
Run/Stop last written 1 - requests the in-place recovery, as HCE and HSE
do, latched as they are.

**The rate bound.** Beside `RecoveryFailuresConsecutive`, which keeps its
meaning, a window: three recoveries begun within ten minutes. A fourth
inside the window is not begun; the controller stays latched failed until a
stop and start, which is today's terminal after three failures in a row.
The window is reset at start.

**All-ones.** An all-ones USBSTS keeps its reading as a window that stopped
decoding and never requests a reset. What changes is that it is no longer
silent: once it has persisted for the containment interval while PnP says
present and the power state is D0, the driver contains it through paths it
already has.

- It reads PCI configuration space. If the function still answers there,
  it clears Bus Master Enable and reads it back clear: that read is the
  proof DMA has stopped. The controller is latched failed with reason
  `unreadable`, the health poll stops reading it, and every device is
  dropped through the existing departure, so their transfers complete
  `DEVICE_GONE`, are parked and later completed `STATUS_CANCELLED` - never
  `STATUS_DEVICE_NOT_CONNECTED` on a listed device.
- If configuration space reads all-ones too, or Bus Master Enable cannot be
  read back clear, DMA cannot be proved stopped: the common buffer is
  pinned through the existing `HcdSvcDmaNotStopped` and transfers are kept,
  as `HcdIoDeviceDrain` already does for a pinned buffer, until PnP removes
  the device.

Revalidation happens only at a start or a D0 entry, through the start's own
checks.

### 4.7 The interval cap (35-T.7, gated) and the Average TRB Length switch

**The cap.** An interrupt endpoint whose Interval exceeds 8 (32 ms) is
programmed at 8, as Linux's `XHCI_LIMIT_ENDPOINT_INTERVAL_9`, on every AMD
controller (vendor `1022`), wider than Linux's list because the AM5 and
X570 lines are unlisted and the report is on one of them, under
`XhciIntervalCap` (section 2's values). Polling a long-interval endpoint at
32 ms is legal under USB 2.0 and costs a few polls a second, which is why
it is gated. It applies at every speed, Interval being an exponent in each;
isochronous endpoints are not capped, since their interval sets their data
rate.

**Ordering with fast polling.** A caller's rewritten bInterval is applied
first (`hcd_cfg.c`, line 520), then the cap, then fast polling. The pipe
keeps its uncapped Interval, and fast polling's eligibility (Interval 7 or
8, `xhci_pipe.c`, `XhciPipeFastPoll`) is judged on that uncapped value, so
the cap never makes an endpoint eligible that was not. Fast polling's
fallback on a refused Configure Endpoint restores the capped Interval, not
the uncapped one. Host vectors cover each combination of the cap's three
values with fast polling's modes and the fallback.

**The Average TRB Length switch** (owner, 2026-10-06). Revision 0 proposed
taking the Average TRB Length of a periodic endpoint as its Max ESIT
Payload, as Linux programs it; review round 1 showed the field enters the
controller's bandwidth admission (4.14.1.1), so changing it changes what a
healthy controller is told, and that Max ESIT Payload may be zero or
exceed the field. So it is not unconditional and not gated by id: it is a
switch, `XhciAvgTrbEsit`, a REG_DWORD in the controller's driver key read
at each start - 0, the default, keeps 1024, the specification's suggestion;
1 programs an interrupt endpoint's Average TRB Length as its Max ESIT
Payload held to 1 to 65535; isochronous and other endpoints are never
changed; absent, not a DWORD or any other number reads as 0. Both INFs
write it at 0 on every controller install path with `0x00010003` under
34.1's rule, with its `VAL-*` row and the footprints. It exists so a tester
can compare the two values on a machine that misbehaves; nothing ties it to
the report.

### 4.8 The counters a user can send (35-T.8)

The per-queue `Errors`, `BadCodes`, `UnmatchedEvents`, `ForeignEvents`,
halts, soft retries and endpoint recoveries; a histogram of completion
codes 0 to 255 by count; `BackstopDrains`; each root port's episode, its
charges and its hold; the controller's recovery window and its terminal
reason; and the values in effect (`XhciIntervalCap`, `XhciAvgTrbEsit`), in
the snapshot `XHCISNAP` copies, under the schema rule. They live in the
extension, which the snapshot already carries, rather than in
`XHCIHC_COUNTERS`. `XhciLogErrorBudget` is called from the transfer
engine's error path, so the note ring carries the first records of each
code. `xhciqual/quirks.c` grows report-only AMD rows.

### 4.9 The episode budgets

Every behaviour that acts is charged when its action starts, to a budget
with explicit re-arm and lifecycle rules, so no fault loops.

| Behaviour | Episode | Budget | Re-armed by | Terminal | Reset at |
|---|---|---|---|---|---|
| 4.1 backstop | none | at most one drain per poll period | - | - (it only queues a drain) | - |
| 4.2 soft retry | one TD | 3 retries | the TD's completion | the error completes as today; charged to 4.3's episode | the TD's completion |
| 4.3 endpoint recovery | one endpoint | 3 recoveries | the stable-progress interval with transfers completing and no recovery | a port cycle of the device (`HcdEnumCycle`), charged to 4.5's episode | a device's removal, a configuration change, a controller start |
| 4.5 root port | one port | 3 re-enumerations and 3 repowers | stable-disconnect evidence, or the stable-progress interval enumerated | the port held, shown in the dump | a controller start |
| 4.6 controller recovery | the controller | 3 recoveries begun in 10 minutes | the window | latched failed until a stop and start | a controller start |
| 4.6 all-ones | the controller | one containment | - | contained: unreadable, or pinned | a start or D0 entry |

The intervals, proposed: stable-progress 60 s; stable-disconnect 1 s;
over-current wait 5 s; power-on interval 100 ms; containment 1 s (ten
health polls). Each is a named constant in the pure core with a host vector
at its boundary.

An endpoint whose budget is exhausted is not failed back to its class
driver: the device is cycled, which is a replug done by the bus, and every
target's class drivers already handle a departure and an arrival. A device
that keeps failing reaches the port's budget, and the port is held.

### 4.10 What the class drivers see

Revision 1 deferred this to a reading. Revision 2 removes the dependency:
every recovery above is owned by the bus, and what a class driver sees is
either an error completion it already gets today for the same fault (a soft
retry exhausted, a TD the controller attributed), or a device departure and
arrival (an exhausted endpoint, a root port re-enumerated), never a new
status and never `STATUS_DEVICE_NOT_CONNECTED` on a listed device.

35-T.0 still reads, per target, what `hidusb.sys`, `usbstor.sys` and
`usbaudio.sys` do with each error completion this design can return -
which requests they send and whether they resubmit - tagged and given
`legal-provenance.md` rows, to set the endpoint budget against how fast a
client's own reset loop runs. The design does not wait on it.

## 5. Injection

QEMU raises none of these faults, and a fabricated event is only useful if
the hardware state the driver then acts on is coherent with it. The
`qemu`-flavour-only layer, beside 35.4's, therefore makes each fault real
where QEMU allows it, and where it does not, it emulates the command that
would meet a faked state rather than send it:

| Fault | How it is made | What the driver's commands meet |
|---|---|---|
| Lost interrupt | the ISR acknowledges as today but does not queue the DPC, for a set number of interrupts | real hardware; the event waits in the ring |
| Transaction Error | a real Stop Endpoint on the chosen endpoint (QEMU goes Stopped, the TD kept), then an injected Transaction Error event on the TD at the dequeue | context reads answer Halted until the driver's Reset Endpoint, which the layer completes with Success without sending (the real endpoint is already Stopped, the state a Reset Endpoint leaves); the doorbell restarts the TD for real. Persistent: injected again on each restart |
| Refused code, halt with no TD | as the Transaction Error, the event carrying the refused code, or a halting code with a pointer off the ring or zero | as above; Stop Endpoint on a real Running endpoint is sent for real |
| Root port PED | a real write of PED 1 to a USB 2.0 root port's PORTSC, which disables the port | real hardware |
| Over-current | QEMU has no port power control to clear; PORTSC reads for the chosen port answer PP clear and OCA and OCC set, and a write setting PP is recorded and answered | emulated reads until the layer releases OCA |
| HCH | a real write clearing Run/Stop | real hardware; the in-place recovery runs for real |
| All-ones | the health poll's USBSTS read answers all-ones; configuration space is real | real Bus Master Enable clear and read-back |

The expected outcomes are fault-specific, and the legs check each:

| Fault | Transient (cleared within the budget) | Persistent |
|---|---|---|
| Lost interrupt | drained by the backstop within two polls, `BackstopDrains` counted | the same each time; the backstop is the delivery, not a recovery, so it has no terminal |
| Transaction Error | the TD completes after its retries, data intact | the error completes; after the endpoint budget, the device is cycled |
| Refused code, halt with no TD | the endpoint recovered, the attributed TD failed, survivors complete | after the endpoint budget, the device cycled; after the port budget, the port held |
| Root port PED or over-current | the device re-enumerated | the port held, shown |
| HCH | one recovery | the window reached, the controller latched failed |
| All-ones | none (a transition shorter than the containment interval) | contained: unreadable, devices dropped, transfers parked |

And beside them: a cancel, ABORT_PIPE, RESET_PIPE, SYNC_RESET_PIPE and
RESET_PORT each racing a soft retry and an endpoint recovery, at each of
the thread's decision points; a stream endpoint; EP0 with a client control
transfer and with the thread's own; teardown and a power transition during
each; and every budget exhausted.

## 6. Provenance

Linux is interface documentation for the downward hardware behaviour
(`AGENTS.md`, "Reference Implementations"): the behaviours above are
described from `external/linux/xhci-ring.c` (`process_bulk_intr_td`,
`handle_tx_event`, `prepare_ring`), `xhci-mem.c` (`xhci_endpoint_init`) and
`xhci-pci.c` (the AMD, Etron, Promontory-A and Promontory 21 lists), and no
function body is copied. Linux's `hub.c` is not in the mirror and nothing
rests on it. The class-driver readings of section 4.10 are new contract
readings and take their tags and rows when they are made.

## 7. What it changes in the documents

The release notes and README (`XhciIntervalCap`, `XhciAvgTrbEsit`, the AMD
report), record 13 (section 6's error contract, from 4.10), record 15 (the
shared peek), `implementation-invariants.md` ("Fatal Errors": the
refused-code deviation superseded, the all-ones containment added),
`failure-diagnosis.md`, `xhcisnap/README.md`,
`xhciqual/hardware-testing.md`, `source-files.md` and `runs/run-35.md`
(roadmap 35.6).

## 8. Decisions taken (owner, 2026-10-06)

- The work joins Phase 35, and Phase 35's release is `2.2.0.0`.
- All the behaviours land together, not one at a time after a diagnostic
  reading.
- The interval cap is gated on every AMD controller (vendor `1022`), with
  the 0/1/2 override.
- The lost-interrupt backstop is this work's; polled delivery reuses its
  peek.
- The Average TRB Length change is not made by default (after review round
  1, finding 1); it is the switch `XhciAvgTrbEsit`, written by the INFs at
  0, off.

## 9. Open for review

The proposed intervals and budgets of section 4.9; the names
`XhciIntervalCap` and `XhciAvgTrbEsit`; and whether the per-target readings
of section 4.10 call for a different endpoint budget.

## 10. The review findings, and where each is answered

Round 1:

| Finding | Severity | Answered in |
|---|---|---|
| 1 Average TRB Length neither legal everywhere nor free | high | 4.7: a switch, off by default |
| 2 A refused code does not prove its TD safe to complete | high | 4.3, steps 1 to 5; section 2's ownership clause |
| 3 "Fail the oldest" can fail an unrelated transfer | high | 4.3: attribution by the captured hardware position |
| 4 Class-driver recovery assumed | high | 4.10: bus-owned recovery, a device cycle as the terminal |
| 5 The backstop must use the ISR's lifetime protocol | high | 4.1 |
| 6 All-ones and the recovery budget | high | 4.6, 4.9 |
| 7 Root-port recovery not fully bounded | medium | 4.5, 4.9 |
| 8 Current-code and Linux descriptions wrong | medium | section 3, rows T3 and T4 |
| 9 The retry bound is not Linux's | medium | 4.2 |
| 10 Absent quirk flags do not explain Linux | medium | section 1, a hypothesis |
| 11 Injection and acceptance need fault-specific outcomes | medium | section 5 |

Round 2:

| Finding | Severity | Answered in |
|---|---|---|
| 1 The injected Reset Endpoint met the wrong state | high | section 5: the command emulated, the real endpoint already Stopped |
| 2 All-ones containment released I/O with no DMA proof | high | 4.6: Bus Master Enable cleared and read back, or the buffer pinned and transfers kept |
| 3 `STATUS_DEVICE_NOT_CONNECTED` makes Windows 98 HID resubmit | high | 4.6 and 4.10: the existing departure and parking, never that status on a listed device |
| 4 The existing quiesce does not retire the failed TD | high | 4.0 and 4.3: capture, attribute, move the software dequeue, then the cancel path; Disabled by recycle |
| 5 Pause is not retry ownership | high | 4.2: the `Retry` state, one balanced pause, the decision under the pipe lock |
| 6 Dropping off-ring and zero pointers defeats a halt with no TD | high | 4.3: the event only requests, the endpoint's state decides; the Prime Pipe path kept |
| 7 EP0 has two owners | medium | 4.3, EP0 |
| 8 CCS 0 then 1 can come from repowering | high | 4.5: recovery-caused transitions never re-arm; stable-disconnect evidence |
| 9 A refused code on a Running endpoint still hangs | medium | 4.3, step 1: the endpoint is stopped whatever its state |
| 10 The budget table was not executable | medium | 4.9: limits, charging points, re-arm, terminals, lifecycle resets, intervals |
| 11 Backstop activity does not prove a lost interrupt; extra DPCs | medium | 4.1: the two-poll rule, the extra DPC stated, the counter's meaning narrowed |
| 12 The interval cap and fast polling do not compose by themselves | medium | 4.7: the ordering, eligibility on the uncapped value, the fallback |
| 13 Roadmap alignment | low | 35-V (the backstop's exception, the race legs) and 35.6 (record 15) |
