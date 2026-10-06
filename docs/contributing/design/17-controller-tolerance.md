# Controller tolerance

Design record for roadmap-hcd tasks 35-T.0 to 35-T.9 (Phase 35, release
`2.2.0.0`). Revision 7, 2026-10-06, written before code. Revisions 4, 5
and 6 are `9163f77`, `74d14e8` and `cc82d37`; revisions 5 to 7 answer
review rounds 5 to 7 (section 10). Revision 0
(`60f8f64`) drafted the findings and the shape; revision 1 (`4a63a35`)
answered review round 1; revision 2 (`67a3f75`) rested the recoveries on
the machinery as it is and answered round 2; revision 3 (`e8d2c0e`)
replaced the in-place endpoint recovery, by the owner's decision, with a
device cycle - the replug the user does by hand, done by the bus (section
4.3) - and made the soft retry pause nothing (section 4.2). Revision 4
answers review round 4: a clock of the driver's own (section 4.0), the
power gate taken explicitly (4.1, 4.6), the soft retry intercepted before
the engine's terminal mutations and generation-checked (4.2), the cycle
before a PDO exists (4.3), and containment that drains only after its proof
(4.6). Section 10 maps every finding of the seven rounds to where it is
answered. **Converged**: review round 8, on revision 7 (`edc7a2a`), found
every earlier finding resolved and nothing material remaining. Section
4.11, the off-switch `XhciTolerance`, was added after it by the owner's
decision of the same day, and converged in its turn at review round 11
(`d9b39c4`).

Revision 8, 2026-10-07, for roadmap-hcd task 35.6, after the code: section
4.5 takes the three departures task 35-T.5 built and its done-note left to
this record ("As built"), and section 7 says where each document change
landed. Nothing else in the design changes.

Revision 9, 2026-10-07, for roadmap-hcd task 35.6, once 35-T.2, 35-T.3/4,
35-T.8 and the first part of 35-T.9 were built and reviewed to convergence
(branch `2.2.0.0` at `adbc4e3`): each section whose code departs from its
text gains an "As built" paragraph - sections 4.2, 4.3, 4.5, 4.6, 4.8, 4.11
and 5 - and section 7 says where the documents stand. The departures were
taken in the code, accepted in review, and are recorded here rather than
written back into the text above them, which stays the design as converged.
One known limitation is recorded in section 4.6.

Revision 10, 2026-10-07, for roadmap-hcd task 35.6, once 35-T.9's second
part was built and the final review of the whole branch had converged, at
its round 4 (branch `2.2.0.0` at `2000fcc`): "As built" notes in sections
4.3 (the slot-fatal teardown; the configuration's fallback cycles; one
re-enumeration charge per pending cycle), 4.6 (an invalidation no halt or
reset proved; the Bus Master Enable a proof cleared put back; the
unproven-DMA containment's own terminal reason), 4.9 (who charges the
re-enumeration budget, and at which `XhciTolerance` values), 4.11 (what
acts at 0 that is not a tolerance behaviour) and 5 (the second part), and
section 7. Two of these are DMA-safety and specification rules rather than
tolerance behaviours, and apply at every `XhciTolerance` value.

Revision 11, 2026-10-07, after `2.2.0.0`, for two findings of the 35-V
legs: section 4.6 gains an "As built" note (a terminal no recovery acts on
completes the transfers it holds, at every `XhciTolerance` value) and
section 4.11's note on what acts at 0 a line for it; section 5's PED row
says the layer answers PED clear on QEMU, which ignores the PED write.

## 1. What is asked, and what is not

A tester on an AMD AM5 board reports that a USB mouse randomly stops
working: "basically unusable" under the `1.x.x.x` releases, the frozen
`usbport.sys` miniport (owner's correction, 2026-10-06; this record said
`2.0.0.0` until then), and under the `2.1.x` builds it stops less often, and
after being moved to another port it works for longer. Which controller sits behind each port, and what the driver saw,
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
(`XHCI_ASMEDIA_MODIFY_FLOWCONTROL`); Intel's `XHCI_MISSING_CAS`, which was
Phase 35's first lead and was set aside by 35.0's reading (issue 11); and an in-place endpoint recovery that keeps a
faulted device's other transfers alive (withdrawn at this revision,
section 4.3).

## 2. The rule: tolerant by default, gated by exception

A behaviour is unconditional when all of these hold:

- it is legal under xHCI 1.2c, and every command it issues meets that
  command's state preconditions;
- on a controller that never raises the fault it costs nothing beyond a
  bounded register or memory read on a path already taken, and it changes
  no field the controller is told on a healthy device's path (no context
  field the controller uses for admission or scheduling);
- the controller never loses a TRB or a buffer it still owns: a transfer is
  completed only once its TRBs are retired, by the controller's own event,
  by a stop the driver issued and the controller confirmed, or by a Disable
  Slot that takes every TRB of the slot back;
- it is bounded by an episode budget (section 4.9), so a fault that
  persists ends in a stated terminal state and nothing loops;
- it does not change what a class driver sees from a healthy device, and
  after a fault it shows a class driver only what that driver already sees
  today - an error completion the driver already gets for the same fault,
  or a device departure and arrival (section 4.10).

A behaviour that fails any of these is gated - a pure-core decision on the
PCI vendor and device id and a REG_DWORD in the controller's driver key
with three values (1 the default, the list; 0 off; 2
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

### 4.0 The machinery the behaviours are built on

Read on 2026-10-06; the design uses these as they are, or says where it
changes them.

- Every recovery command runs on the controller thread, which serializes
  them; a cancel, ABORT_PIPE, RESET_PIPE, SYNC_RESET_PIPE and RESET_PORT
  each pause the pipe, and so does the thread's own control transfer.
  There is no pipe lock: these paths take the controller lock.
- `Paused` is a nesting counter with no owner (`hcd.h`, line 240; `hcd_io.c`,
  `HcdIoPipePause` and `HcdIoPipeResume`, lines 1323 and 1337); resume
  releases held submissions, which are published and rung. A submission
  while not paused is published and rung, Halted or not, and the
  controller ignores a doorbell for a Halted endpoint.
- Every existing quiesce relocates the controller to the **software**
  dequeue (`hcd_cfg.c`, `hcdCfgQuiesce`, line 2224), and no helper reads the
  Endpoint Context's TR Dequeue Pointer. This design adds no such helper.
- There is no configuration generation; an event is mapped to a pipe by
  slot and DCI alone (`hcd_dev.c`, `hcdEventPipe`, line 231).
- `HcdEnumCycle` re-enumerates a device at its location, a root port or an
  external hub's port: the PDO departs, the slot is disabled - which takes
  back every TRB of the slot - and the device is enumerated again with a new
  PDO. Every target's class drivers handle that as a removal and an
  arrival. It is what a user's replug does.
- A dropped device's transfers complete `DEVICE_GONE`, are parked on the
  PDO and later completed `STATUS_CANCELLED`, unless the common buffer is
  pinned, when they are kept (`hcd_io.c`, `HcdIoDeviceDrain`, line 1893,
  and `HcdIoPark`, line 2028); a refusal is completed by a 1 ms DPC, never
  inline. Windows 98 SE's `hidclass.sys` answers a read failing
  `STATUS_DEVICE_NOT_CONNECTED` by resubmitting at once while its device is
  started (`hcd_io.c`, lines 1947 to 1953), so this design never returns
  that status for a device that is still listed.
- The enumeration's settle clock (`HcdEnumSettleClock`, `XhciEnumElapsedMs`,
  `hcd_enum.c` line 3725) is the low 32 bits of `KeQuerySystemTime`, the one
  time source the Windows 98 import evidence admits: it wraps every 429.5 s
  and follows adjustments of the system time, so it cannot measure a
  ten-minute window and is not monotonic. Polls cannot be counted instead,
  because the controller thread wakes on work as well as on its 100 ms
  timeout and does blocking work between polls.
- **The tolerance clock**, new: a count of 100 ms ticks, advanced by a
  relative kernel timer - `KeInitializeTimer` and `KeSetTimer` with a DPC,
  both on the Windows 98 import evidence (`xhci98-imports.allow`, rows 102
  and 103) - whose DPC increments the count and arms the timer again,
  relative, for the next 100 ms. A relative timer is unaffected by a change
  of the system time, which is why the per-port budget already uses one
  (`hcd_enum.c`, lines 2007 to 2013). It can expire up to one system clock
  period early, and the error accumulates over re-arms, so a tick is not
  credited as 100 ms. The clock period cannot be read on Windows 98's import
  evidence (`KeQueryTimeIncrement` has no row), so each tick is credited at
  its lower bound against the slowest clock interrupt a PC runs, the 8254
  timer at its power-on 18.2 Hz, a period under 55 ms: at least 45 ms per
  tick. An interval of T ms is therefore counted as `ceil(T / 45) + 1`
  ticks - the extra tick because the stamp may be taken just before one -
  and can only run long, never short. How long it runs has no fixed upper
  bound: a long interval with timely DPCs comes to about 2.2 times its
  nominal length, a short one more (100 ms is four ticks, about 300 to 400
  ms), and a delayed DPC stretches any of them further. A
  ULONG count wraps after thirteen years. The timer is armed at start and
  closed at stop by the same rule as the frame timer's (`hcd_svc.c`):
  cancelled, and its DPC counted in flight and waited for. Host vectors
  cover an interval's tick count at its boundary and the accumulated early
  expiry of every tick at the 55 ms bound.
- The controller's power gate (`HcdPowerGateEnter` and `HcdPowerGateLeave`,
  `hcd_ctl.c` line 560) excludes power transitions. The thread's health poll
  (`hcdPoll`) runs before the thread takes it, and `hcdRecover` takes it
  itself (line 601). The health poll's own admission is `HcInfoStatus`,
  `INITIALIZED` and `ControllerFailed`; it checks neither the power state nor
  `DpcClosed`.
- `ScratchTainted` is controller-wide: set by a timed-out EP0 transfer, it
  halts commands and enumeration until a controller reset (`hcd_enum.c`
  line 129). It is not used by this design.

### 4.1 The lost-interrupt backstop (35-T.1, unconditional)

A step of its own on each controller-thread pass, after `hcdRecover`, that
takes the power gate itself as `hcdRecover` does and, inside it, admits
itself only with `HcInfoStatus` good, `INITIALIZED` set, the controller not
failed, the controller in D0 and not in a transition, and `DpcClosed`
clear - the conditions the health poll's own admission lacks. Admitted, it
reads, under the controller lock, the event TRB at the software dequeue
pointer. A cycle bit equal to the consumer's means an event is pending. The
poll keeps one observation: the dequeue index, the cycle, the drain
generation (a counter every drain pass increments, so a ring that wrapped
and came back to the same index is not mistaken for one that never moved),
the start generation (bumped by every start and in-place recovery), and the
time first seen on the tolerance clock. The drain is queued when the same
observation - all four equal - has stood for the backstop interval, 100 ms
counted as section 4.0 says (four ticks): the controller wrote
an event and nothing has drained it since. An event pending for less is
left to the interrupt, so moderation and an interrupt in flight are not
raced. A lost interrupt is drained at the first pass after the fourth tick
since the pass that first saw it, so at least 100 ms and, at the usual
clock rates, about 300 to 400 ms after it; the thread's own pacing adds to
that.

The queue goes through the ISR's admission: `DpcsInFlight` counted before
`KeInsertQueueDpc` and rolled back on a refusal, nothing once `DpcClosed`
is set. The insertion can succeed while a drain executes on another
processor, since a running DPC is no longer queued; the second drain
serializes on the controller lock and finds the ring empty, which costs one
DPC, and only after an event has waited 100 ms. It writes no register: ERDP
and EHB stay the drain's, and IE, clear between the ISR's acknowledgement
and the DPC's re-arm, stays the DPC's to restore. `XHCI_FIX_EVT_REARM` is
retired. One helper serves the peek; record 15's opportunistic checks reuse
it with their own staleness rule.

Its count is `BackstopDrains`: drains the backstop queued, evidence of a
lost or late interrupt, not proof. Cost on a compliant controller: one
cached memory read per poll.

### 4.2 The soft retry (35-T.2, unconditional but for an exclusion list)

**Scope.** A USB Transaction Error on a bulk or interrupt endpoint that has
no streams, whose device is not behind a TT, on a controller not on Linux's
`XHCI_NO_SOFT_RETRY` list (`1022:43B9`, `1022:43BB`, `1B6F:7023`,
`1B6F:7052`). EP0, isochronous and stream endpoints keep today's handling.

**The engine intercepts first.** In `XhciXferEvent`, once the event is
matched to its TD and before any of the TD's terminal mutations - the
length fixed, the failure latched, the retirement (`xhci_xfer.c`, lines
2052 to 2082 and 2147) - a Transaction Error is diverted when the matched
TD is the queue's head, the TD's retries are not spent, and the pipe is in
scope. The whole event - its four dwords, the reported TRB pointer
included, from which today's path computes the bytes transferred and
classifies the completion of a multi-TRB TD (`xhci_xfer.c`, lines 1949 and
1987) - is kept on the TD as its deferred outcome, the TD is left untouched
and unretired at the head with the software dequeue on it, the pipe's retry
generation is incremented, and `RetryWanted` is set with the TD's identity
and that generation, the thread woken. A Transaction Error on a TD that is
not the head - one whose predecessors the engine would sweep - or on a TD
whose three retries are spent is not diverted and takes today's path
whole; that is the one route an exhausted retry takes.

**Applying a deferred outcome** replays the kept event through
`XhciXferEvent` with interception bypassed, so the TD meets exactly today's
error processing and is not diverted a second time; the deferred outcome
and the TD's retry marker are cleared with it. The endpoint is Halted; submissions arriving now are published
and rung as today, and the controller ignores the doorbell.

**A later event for a TD with a deferred outcome** is handled as on any TD:
a Success or Short Packet completes it normally, the deferred outcome
discarded, since nothing of it was ever applied; another Transaction Error
is intercepted again under the same rule, replacing the deferred outcome
and incrementing the generation.

**The thread** takes no pause: every operation that could contend runs on
the same thread, one at a time, and a doorbell rung on the Halted endpoint
is ignored. When it reaches a pipe with `RetryWanted` it records the
generation G and the TD's identity, and decides under the controller lock:

- If a cancel, ABORT_PIPE, RESET_PIPE, SYNC_RESET_PIPE or RESET_PORT is
  pending on the pipe or its device, `DrainPending` is set, or the head is
  no longer that TD, it applies the deferred outcome by the replay above -
  exactly today's mutations, completion with
  `USBD_STATUS_DEV_NOT_RESPONDING` and retirement - if the TD still has one,
  and clears `RetryWanted` if the generation is still G. The endpoint is
  then Halted with today's state, and the pending operation, or the class
  driver's own reset, proceeds from it as it does today.
- Otherwise it issues Reset Endpoint with TSP 1, which xHCI 1.2c 4.6.8.1
  provides so the host's sequence state is kept and the device's data
  toggle stays aligned without a CLEAR_FEATURE; no Set TR Dequeue Pointer,
  no CLEAR_FEATURE; the TD's mapping is kept. The endpoint becomes Stopped
  with the controller's dequeue on the TRB that failed. While the command is
  outstanding, a submission may ring the now-Stopped endpoint, and the
  controller resumes the TD before the thread does; its next event is
  handled as the paragraph above says. When the command completes the
  thread decides again under the controller lock: if the generation is
  still G it rings the doorbell (a second ring is harmless) and clears
  `RetryWanted`; if it is not, a newer event has already re-armed the retry
  and the thread leaves `RetryWanted` set for its next visit, never clearing
  a newer request. The controller resumes at the TRB that failed, so a
  multi-TRB TD's completed TRBs are not transferred twice. An operation that
  became pending meanwhile runs next against a Running or a Halted endpoint,
  which every one of them already handles. A failed Reset Endpoint goes to
  `hcdCfgFault`, as any recovery command does.

**The bound** is this project's: three retries per TD, counted on the TD and
gone with it (Linux allows four, endpoint-wide). The fourth Transaction Error
is not intercepted and takes today's path whole, as the engine paragraph
says; the thread's decision never sees a spent TD. An exhausted retry is not
charged to any further budget: the class driver sees what it sees today.

Cost on a compliant controller: nothing, since no Transaction Error arrives.

**As built (35-T.2, `4ab1663`, review round 1 `6c0ecc1`, `35f77b6` and
`8fd1f49`, converged at round 2; revision 9).** The scope is set once per
pipe when it is opened (`hcdCfgPipeNew`, the pure `XhciTolRetryScope`),
"behind a TT" read as the device's `TtSlot` being nonzero; the interception
is `XhciTolRetryDivert` in `XhciXferEvent`, the event kept on the TD under
`XHCI_XFER_FLAG_RETRY_DEFERRED`, the generation and the request on the
queue rather than the pipe; the replay is `XhciXferRetryReplay`, reached
through `HcdDevRetryReplay`; the thread's service is `HcdCfgRetryService`
and `hcdCfgRetryOne`, the command `hcdCfgRetryResetEndpoint`, the decision
after it `XhciTolRetryAfterReset`; the bound `XHCI_TOL_SOFT_RETRIES`, 3.
Five departures from the text above:

- **A quiesce settles an outstanding retry, after its own Reset
  Endpoint.** `hcdCfgQuiesce`, finding the endpoint Halted, replays a
  deferred outcome once its Reset Endpoint has completed and before it
  reads or programs the dequeue. The error's Transfer Event precedes that
  command's completion on the event ring, so by then the error has been
  diverted; settled earlier, the drain would divert it afterwards and leave
  the software dequeue on the failed TD, and the Set TR Dequeue that
  follows would clear the controller's saved progress in it (xHCI 4.6.8),
  so a later cancel that kept the TD could send OUT data twice. An abort,
  cancel, deconfiguration or port reset therefore meets today's state.
- **A failed Reset Endpoint does not replay.** The TD is left to
  `hcdCfgFault`'s outcome, which completes it with the rest of the device's
  transfers. A device already gone or replaced at its location causes
  nothing new, its departure already under way; a published non-hub device
  whose hub path is no longer present (`HcdHubPathPresent`) has its
  location cycled (`hcdCfgCycle`); any other - a device still present, a
  hub, a device not yet published - requests the controller recovery,
  charged to the window (section 5, `09`).
- **Three event words are kept, not four.** The pointer's high dword is
  always 0 on a kept event: a Transfer Event whose pointer high dword is
  nonzero is refused before any processing (`XhciXferEventHighRefused`,
  `8fd1f49`), counted Foreign, since no ring of this driver lies above
  4 GB and its low dword alone could alias a queued TD. Since 35-T.3/4 that
  event also reaches the device cycle's producer as any Foreign event does
  (section 4.3).
- **"A pending operation" is wider.** Beside a cancel, ABORT_PIPE,
  RESET_PIPE, SYNC_RESET_PIPE, RESET_PORT and `DrainPending`, the thread
  replays rather than resets when the device's PDO is removing (`AbortAll`),
  when the pipe is Paused or Closed, and on a stream pipe.
- **`RetryRecovered` counts the whole completed group.** Every retried TD
  of a completed group that ends without failing is counted, the owner and
  each swept TD that had been diverted and settled as a short success
  included (`35f77b6`).

Since 35-T.3/4 a pass that finds both a retry and a confirmed cycle reason
on the same device takes the cycle (section 4.3, "As built").

### 4.3 The device cycle (35-T.3 and 35-T.4, unconditional)

A code nothing claims (T3) and a halt with no TD (T4) are answered by
cycling the device (owner, 2026-10-06), not by recovering the endpoint in
place. Review round 3 showed every in-place form needs to attribute the
fault to a TD, and that cannot be done soundly here: the Endpoint Context's
TR Dequeue Pointer is defined only in some states (xHCI 1.2c Table 6-10),
the queue's head and the ring's dequeue are kept apart by design, a
recycled endpoint resets only the host's toggle, a Running endpoint can have
passed the TD whose completion was refused, and EP0 needs a Setup boundary
and a thread that is not waiting on itself. A Disable Slot takes back every
TRB of the slot whatever its state, which is the ownership guarantee section
2 asks for, and the re-enumeration resynchronizes both sides of every
endpoint. These faults are not raised by a compliant controller, so the
cost - a re-enumeration, the device's other transfers ending as on an
unplug - is paid only where the alternative is a device dead until the user
replugs it.

**When.** T3: a Transfer Event on a non-isochronous endpoint with a code
`XhciXferCodeInfo` refuses, whose slot names a device - any state of the
endpoint, since a refused code is itself the evidence. T4: a Stall, or a
Transaction, Babble or Split Transaction error on a non-isochronous
endpoint, that the queue could not match - Foreign by pointer, zero
included, or Unmatched - whose slot and DCI name an open pipe, and only if
the thread then reads that endpoint's context as Halted or Error, so a
stale event against an endpoint now running costs one context read and
nothing more. A slot with no device is counted and dropped as today. The
streams endpoint's Prime Pipe STALL path is kept as it is.

**How.** The event marks the device `CycleWanted` with its reason and the
location's connect generation (bumped by every connect and disconnect at
the location), and wakes the thread. The thread acts only if the device is
still the one at that location and the generation is unchanged - otherwise
the device already left or was replaced, and the mark is dropped and
counted. Charged to the location's budget (section 4.9), by one of two
paths:

- **A published device** - it has its PDO and the PDO group's serial, as
  `HcdEnumCycle` and `hcdCycleOwns` require (`hcd_enum.c`, lines 1266 and
  1281) - is cycled by `HcdEnumCycle`, at a root port and an external hub's
  port alike. Client control transfers on EP0 end with the device, as on an
  unplug.
- **A device not yet published** - its control transfers are the
  enumeration's own, before the PDO exists, which `HcdEnumCycle` refuses -
  is handled by the enumeration executor, through an outcome of its own,
  `ABANDONED_FOR_CYCLE`, which every step that issues a control transfer
  returns when its wait was abandoned and which every caller propagates
  unchanged. It is not a failure: it suppresses the executor's ordinary
  continuations - the next control transfer after a step whose failure is
  otherwise tolerated, such as the BOS read (`xhci_enum.c`, lines 282 to
  295) - and its automatic retries (`hcd_enum.c`, lines 2044 to 2065).
  One handler, at the top of the executor where an attempt's outcome is
  taken, then runs once per attempt, guarded by the attempt's own
  done-once mark: the subtree teardown an ordinary failure runs
  (`hcd_enum.c`, line 1899), which disables the slot and frees the device
  record and its DCBAA entry; then the initiating location's own machine,
  which `hcdSubtreeGo` leaves to its caller (`hcd_enum.c`, lines 2975 to
  2977), is set to `EMPTY` with its slot id and device cleared - not left in
  the descriptor or BOS state the attempt reached, from which a CONNECT is
  ignored, since CONNECT starts an enumeration only from `EMPTY` or
  `FAILED` (`xhci_enum.c`, lines 167 to 175), and not `FAILED`, which waits
  for the next connect change; then the location's budget is charged once;
  and, if the location still reads connected and the budget allows, a
  CONNECT is fed to the now-`EMPTY` machine. Connected is read the way the
  existing `hcdPortConnected` reads it for either kind of location
  (`hcd_enum.c`, lines 1086 to 1091): PORTSC's CCS at a root port, the
  hub's port status (`HcdHubPortStatus`) at an external hub's port. This
  is the pre-PDO cycle.

**The thread's own wait.** If the thread is waiting on its own control
transfer to the device when the mark arrives, the mark ends that wait: it
returns "abandoned for a cycle" instead of timing out into the controller
reset that `Ep0Stuck` and `ScratchTainted` would bring (`hcd_enum.c`, the
5000 ms wait). The controller may still own that transfer's TRBs and be
writing into the thread's scratch buffer, so after an abandoned wait the
thread issues nothing on that device and reuses neither the transfer record
nor the scratch buffer until the cycle's Disable Slot has completed, which
takes the slot's TRBs back; the cycle's own path needs neither before
then. If that Disable Slot fails or times out, nothing has taken the TRBs
back, so the exclusion cannot end: the thread sets `ScratchTainted`, as a
timed-out EP0 transfer does today, and the controller recovery that follows
is what ends it.

The deviation in `implementation-invariants.md`, "Fatal Errors", is
superseded and rewritten (35.6, done: the refused-code bullets there now
describe the cycle), and the comment at `xhci_xfer.c` line 270 corrected
(35-T.3/4, `0e1cb42`: the refused-code comments at both of its sites no
longer lean on usbport's URB timeout; the caller cycles the device).

**As built (35-T.3/4, `0e1cb42` and `c0f429c`, review rounds `18f383f`,
`bdc93c0` and `adbc4e3`, converged at round 4, with `d69814f` meeting
35-T.2; revision 9).** The producer is `hcd_dev.c`'s `hcdTolCycleEvent`
over the pure `XhciTolCycleReason`; the mark, `CycleMark` on the device
record, is set only through `HcdTolCycleMark` (DISPATCH_LEVEL, the
controller lock held, a no-op at `XhciTolerance` 0); the connect generation
is `HCD_PORT.ConnectGen`; the thread's service is `hcdCycleService`, its
context read `hcdCycleResolve` with the pure `XhciTolHaltOwner`;
`ABANDONED_FOR_CYCLE` is `XHCI_ENUM_EV_ABANDONED` in `xhci_enum.c` - no
Failed state, no retry, no continuation - and the pre-PDO handler is
`hcdCycleAfter`, once per run; a published device goes through
`HcdEnumCycle`. The thread's own EP0 wait is `hcdThreadControlQuiet`: the
waiter is registered and a pending mark looked for under the controller
lock, so no mark is lost between a look and the wait. The transfer keeps
one deadline, 5000 ms, across every wake, a stale T4 event costing its
context read and no extension. Windows 98's import evidence has no
`KeWaitForMultipleObjects`, so the deadline is a relative timer polled
between **100 ms slices** of the event wait, and a timeout lands up to about
100 ms after it. A mark resolved after the deadline makes the terminal
decision at once (`XhciTolWaitStep`, `XhciTolWaitResolved`), so a flood of
stale marks on a multiprocessor cannot hold the thread past it. After an
abandoned wait `Ep0Stuck` keeps the record and `ScratchHeld` keeps every
thread control transfer off the scratch until the cycle's Disable Slot
completes or a controller reset takes the slot; a failed Disable Slot sets
`ScratchTainted`; `HcdCfgService` defers its remaining requests while
`ScratchHeld` stands. Five departures from the text above:

- **On a streams endpoint, a halt with no TD is a T4.** A Stall,
  Transaction, Babble or Split Transaction error whose pointer no stream
  ring holds marks the device as any unmatched halt does; only the Prime
  Pipe STALL, with a zero pointer, keeps its path.
- **A refused code on a DCI with no open pipe still marks the device.**
  The slot naming a device is enough, which covers an Endpoint Not Enabled
  on an endpoint the configuration never opened.
- **A refused budget still drops the device.** When the location's
  re-enumerations are spent the device is removed all the same and the
  location held, `CyclesRefused` counted - as 35-T.5's refused PED
  reconnect (section 4.5) - rather than left listed and dead behind a hold.
- **A connect the machine ignores does not move the generation.** The
  connect generation moves at every disconnect, every cycle and every
  connect that starts an enumeration; a CONNECT the location's machine
  ignores is not a change of device.
- **The retry and the cycle are decided once a pass.** A device can hold a
  soft retry (section 4.2) and a cycle mark together: the Transaction Error
  that the retry holds leaves its endpoint Halted, and a later unmatched
  event on it marks a T4. `hcdCfgRetryOne` reads the mark through the same
  rule as the cycle service (`HcdTolCycleResolve`) and joins the two
  (`XhciTolJoin`): a confirmed reason commits the pass to the cycle, whose
  teardown drains the deferred TD; otherwise the retry runs now. Halted is
  excused as the retry's own only while the queue's head is the deferred TD
  the live retry names; Error always confirms, since a Reset Endpoint, the
  retry's command, is illegal there. Each pass therefore consumes the retry
  or charges a cycle, and a mark arriving between passes cannot postpone
  both.

The notes `tol.cycle.mark`, `tol.cycle`, `tol.cycle.prepdo`,
`tol.cycle.dropped` and `tol.cycle.wait` record each step; no field was
added to `XHCI_TOL_STATS`, whose cycle counters the foundation already
carried.

**As built: the final review of the branch (revision 10).** Three rules
beside the cycle, found by the final review of the whole branch and fixed
on 2026-10-07:

- **An Incompatible Device Error takes its device down** (`f5c458c`,
  `3cc80ec`). Code 22 "may be returned by any command or transfer, and is
  fatal as far as the Slot is concerned. Software shall issue a Disable
  Slot Command to recover" (xHCI 1.2 Table 6-90). The transfer engine
  already read it so (`XhciXferCodeInfo`'s slot-fatal flag) and the command
  engine already called `XhciSlotCommandSlotFatal` for a Command Completion
  carrying it, but the HCD counted only the command's and ignored the
  transfer's: the endpoint was halted and recovered while the slot stayed
  enabled. That gap predates this release. Both routes now mark the device
  on the event's slot (`hcdSlotFatalMark`, from the event DPC under the
  controller lock; a Transfer Event through the pure `XhciXferSlotFatal`),
  and the thread (`hcdSlotFatalService`, before the cycle service) takes
  the mark: a published device leaves through `HcdEnumCycle` - teardown,
  Disable Slot, PDO reported missing - and its location enumerates afresh,
  counted in `IncompatibleDeviceTeardowns`, noted `slot.fatal.cycle`. A
  device not yet published is left to the enumeration, whose failed step
  already disables the slot before its bounded retries; a departing one to
  its departure. It is the specification's recovery, not a tolerance
  behaviour, so it runs at every `XhciTolerance` value; and so that a
  device whose every Configure Endpoint answers 22 is not re-enumerated for
  ever, the re-enumeration is charged to the location's budget at every
  value, 0 included. Spent, the Disable Slot is still issued and the device
  still removed, and the location held until a stable disconnect or a
  controller start. At 0 the location machinery therefore runs for a
  location something charged, which at 0 is only this path
  (`XhciTolLocActive`: tolerance on, or the location charged or held;
  `XhciTolLocHeld` for the root port's and the hub port's connect); a
  location never charged behaves at 0 exactly as before.
- **The configuration's fallback cycles are charged** (`3459f81`). A
  RESET_PORT whose recovery failed (`hcdCfgResetPort`) and a departing
  device's failed command (`hcdCfgFault`) called `HcdEnumCycle` uncharged,
  so a device that failed the post-reset descriptor read every time but
  enumerated afresh could loop. Both go through `hcdCfgCycle`, which with
  tolerance on charges the re-enumeration to the device's location; a
  refusal still cycles the device and holds the location (`CyclesRefused`).
  At 0 they charge nothing, as before.
- **One charge per pending cycle, whoever asked** (`f364849`). One
  RESET_PORT failure answered with code 22 was charged twice for the one
  cycle it produced, by the fallback and by the slot-fatal teardown, and
  with two earlier cycles the second charge refused the third
  re-enumeration the budget permits. Each port now keeps the charge of the
  cycle pending at its connect generation (the pure
  `XHCI_TOL_CYCLE_CHARGE`, `XhciTolCycleCharge`): the first producer's
  charge is made and its answer recorded, and every later producer at the
  same generation - the fallbacks, the slot-fatal teardown, the cycle
  service, the PED and hub-port-disable reconnects - reads that answer and
  charges nothing. The cycle's own disconnect or connect moves the
  generation, so the next cycle charges afresh; a controller start and a
  hub object's start clear it. Repowers are unchanged.

### 4.4 Submissions to a Halted endpoint

Unchanged: published and rung, the doorbell ignored until a recovery.

### 4.5 The location the controller gave up on (35-T.5, unconditional)

**PED cleared.** A USB 2.0 root port with PEC set, PED clear, CCS set and a
device held is fed a disconnect. Once the disconnect's teardown has
completed - the PDO reported missing, the slot disabled - the thread reads
PORTSC afresh, and feeds a connect only if CCS is still set and PP is set.
As `XhciHubPortDecide` does for an external hub. `GET_PORT_STATUS` keeps
the PDO-state answer and its race rule; the old PDO is no longer listed and
answers 0.

**Over-current.** A root port with OCC set, or found with PP clear while
the driver's own state says it powered it, is held and its device fed a
disconnect. The thread then reads OCA on each pass: once OCA has read clear
for the over-current settle interval, it sets PP, waits the power-on
interval, and reads PORTSC afresh before looking for a connection. A port
whose OCA does not clear within the over-current wait, or whose repowers
are spent, is held unpowered.

**Every location has one budget**, a root port and an external hub's port
alike, for its re-enumerations (4.3's cycles and 4.5's PED reconnects) and,
on a root port, its repowers: three of each. It is kept on the location, so
a cycle, a reconfiguration or a new PDO at the location does not reset it.
Transitions the recovery itself causes - CCS falling because PP was cleared,
the driver's own disconnects and cycles - never re-arm it, and it survives a
repower. It is re-armed only by evidence of a genuine change: CCS observed
clear with PP set and no fault active for the stable-disconnect interval,
followed by a connection; or a device enumerated at the location and
completing transfers, with no charge, for the stable-progress interval.
Exhausted, the location is held - the bus enumerates nothing there - in a
state the dump shows (`hold` with its reason). A hold with the port powered
is released by that stable-disconnect evidence or a controller start. A
hold with the port unpowered cannot show a disconnect, since CCS means
nothing without PP, and the design does not repower a port whose
over-current budget is spent: it is released only by a controller start (a
restart, or a disable and enable), and the dump and the release notes say
so.

**As built (35-T.5, `778b730` and review rounds `4a9010d`, `82d87fe`,
`64461df`; revision 8).** Three departures from the text above, taken in
the code and recorded here:

- **An exhausted budget leaves the device disconnected.** When a PED
  fault's charge finds the budget spent, the disconnect is fed all the same
  and only the reconnect is refused (`CyclesRefused` counted): the device
  leaves Windows and the location is held with nothing enumerated, rather
  than the fault's device staying listed and dead behind a hold.
- **A hub's disabling of a port is charged.** The external hub path
  already re-enumerated a port its hub disabled (`XhciHubPortDecide`); that
  re-enumeration is now charged to the hub port's budget like a root port's
  PED reconnect, and a held hub port is never fed a connect. Section 4.5
  named only root ports for PED.
- **A hub's port budgets start afresh when the hub object is reused.** A
  hub port's `XHCI_TOL_LOC` is kept in the extension (`Tol.HubLoc[]`, so the
  snapshot carries it) and initialized in `HcdHubStart`, so a hub that
  starts again - re-enumerated, or its record reused for another hub at the
  same place - begins with full budgets and no hold, where this section
  keeps a budget "across cycles, reconfiguration and new PDOs". A root
  port's budget is kept as written.

Also built, within the text above: the over-current episode is a pure-core
machine (`XHCI_TOL_OC`); a recovery's own disconnect (an over-current, a
SuperSpeed warm reset, a hub's warm reset) is latched (`RecoveryDisc`) and
never re-arms a budget; stable progress counts only validated Success and
Short Packet retirements; and an unpowered hold is kept off through a
reinitialization's power pass.

Revision 9 adds one rule found in review after 35-T.5 converged (`cd8f744`):
**a hub port's repower is the recovery's own disconnect.** A USB 2.0 hub
(`HcdHubPortLook`) and a SuperSpeed hub (`HcdSsHubPortLook`) mark the
location (`HcdTolLocRecovery`) before setting PORT_POWER on a port an
over-current switched off, as a root port's over-current and a warm reset
already did; before it, a device that stayed away for the stable-disconnect
interval after the repower and came back re-armed a spent budget and
released a powered hold by an absence the recovery made.

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
silent. The evidence is gathered where it is acted on: a step of its own
after `hcdRecover`, which takes the power gate itself and, inside it, admits
itself only in D0, outside a transition, with PnP saying present - so no
power transition or recovery runs beside it. That step reads USBSTS itself;
the health poll's ungated samples are not used. The first all-ones read
stamps the tolerance clock with the controller's start generation; a good
read, a failed admission, a power transition, a start or an in-place
recovery (a new start generation) clears the stamp, so an episode never
outlives the lifetime that began it. Once all-ones has been read on every
admitted pass while the clock advanced by the containment interval, the
step contains the controller, in this order:

1. **Close admission, draining nothing.** `ControllerFailed` is latched
   with the reason `unreadable`: the event drain drains nothing, `hcdHalted`
   stops commands, enumeration and configuration service, the health poll
   stops reading the controller, and `hcdRecover` never acts on this
   reason. A controller-wide `Unreadable` flag, new, is set under the
   controller lock, and every submission path parks a new request on it
   instead of refusing it: held, cancellable, on the device's PDO as
   `HcdIoPark` holds a gone device's, so a class driver's read simply pends
   - a refusal would complete `DEVICE_GONE`, which `XhciPipeNtStatus` maps
   to `STATUS_DEVICE_NOT_CONNECTED` on a PDO that is not closing
   (`hcd_io.c`, lines 2304 to 2324), the status Windows 98 SE's
   `hidclass.sys` resubmits on at once. A parked request is released
   exactly as one is today: completed `STATUS_CANCELLED` when it is
   cancelled, when an abort that covers it runs (`hcd_urb.c`, line 650),
   or when its PDO is stopped or removed (`hcd_pdo.c`, lines 509 and 2200);
   dropping the device does not itself release it. Where `HcdIoPark`
   declines (`hcd_io.c`, line 2037), the request is completed as the
   deferred refusal already chooses for those two cases: `STATUS_CANCELLED`
   for a request already cancelled, `STATUS_DELETE_PENDING` for a PDO that
   is closing - neither is `STATUS_DEVICE_NOT_CONNECTED`. No transfer
   already submitted is completed and no mapping is released in this step.
   `HcdIoDeviceDrain` is not called yet, because it drains as well as
   marking Gone (`hcd_io.c`, lines 1898 to 1912).
2. **Prove DMA stopped.** The thread reads PCI configuration space. If the
   function answers, it clears Bus Master Enable and reads it back clear.
3. **Then release or keep.** With that proof, `HcdIoDeviceDrain` runs for
   each device - its transfers drained, parked and later completed
   `STATUS_CANCELLED` - and the devices are dropped through the existing
   departure, never `STATUS_DEVICE_NOT_CONNECTED` on a listed device.
   Without it - configuration space reads all-ones too, or Bus Master
   Enable will not read back clear - the common buffer is pinned first,
   through the existing `HcdSvcDmaNotStopped`, and only then is
   `HcdIoDeviceDrain` run, which on a pinned buffer marks the device Gone
   and keeps every transfer and mapping under the existing pinned-buffer
   rule, which this design does not relax: not released at removal, nor at
   a D0 entry.

Revalidation happens only at a start, through the start's own checks; a
pinned buffer stays pinned as the existing rule says.

**As built (revision 9).** Three rules changed by review after 35-T.6
converged:

- **An owed recovery waits while USBSTS reads all-ones** (`a76637d`). A
  command that timed out on a window that stopped decoding requests the
  in-place recovery; begun, its reinitialization failed on the dead
  capability registers and each attempt's new start generation restamped
  the all-ones episode, so the containment never ran. `hcdRecover` now
  leaves the request owed and uncharged while tolerance is on, the
  containment would be admitted and USBSTS reads all-ones
  (`XhciTolRecoverDefer`); once the window answers, the recovery runs, and
  once contained, `Unreadable` refuses it as before.
- **The containment's admission does not ask `HcInfoStatus`**
  (`hcdContainAdmitted`, `a76637d`). A recovery whose reinitialization met
  the dead window leaves `HcInfoStatus` bad, but the layout the start
  validated is still in `HcInfo`, which only a whole validated decode
  replaces; `STARTED` says the start validated one. The admission is
  tolerance on, not yet contained, started, D0, outside a power transition,
  and PnP saying present.
- **A held slow request keeps its dispatch stamp** (`02ee24d`). A
  RESET_PIPE or SYNC request queued for the thread moves its dispatch stamp
  to `DriverContext[1]`, and step 1's hold puts it back rather than
  stamping the request again, so an ABORT_PIPE submitted after it and
  already run still covers it; before, the request stayed held for good
  behind an abort that covered it.

**Known limitation, not fixed in `2.2.0.0`.** On the pinned branch, if an
adapter-channel allocation was waiting for map registers that the kept
transfers hold, a later stop, disable or restart waits for ever in
`HcdDmaMapDrain` (`hcdRelease`), which waits for the channel request to
end before the adapter goes. It is rare: a 32-bit scatter-gather bus master
normally needs no real map registers. The fix proposed is a "channel
abandoned" teardown - the adapter, the record and the FDO kept,
`hcdMapExecute` made safe to run after the teardown, and a later start
refused while the request is still queued. The release notes, the README
and the package readme list it among the known limitations.

**As built: an invalidation no halt or reset proved (revision 10).** The
final review of the branch found the containment's proof was needed in a
second place, a readable controller rather than an unreadable one. Fixed
on 2026-10-07:

- **An unproven invalidation releases nothing before the proof**
  (`a113df2`). The in-place recovery and the reinitializing resume raise
  `XhciSlotInvalidateAll` before their own halt, and the HCD ignored
  whether the controller had stopped. When that halt timed out (Run/Stop
  written 0, HCH never read set) or an earlier step refused, no HCRST
  followed, yet the thread drained every device: transfers completed,
  client buffers and map registers handed back while the controller could
  still be executing the TDs that name them. This predates the tolerance
  work (it was in `2.0.0.0`). The HCD now remembers an invalidation raised
  without that evidence (`SlotsUnproven`); `XhciSlotInit`, which runs only
  once HCRST has completed, is the proof that clears it. Meeting an
  unproven invalidation, the thread first takes this section's proof (Bus
  Master Enable cleared and read back clear, `HcdCtlProveDmaStopped`); with
  it the devices are dropped as before; without it the common buffer is
  pinned (`HcdSvcDmaNotStopped`) and the containment's no-proof branch runs
  (`HcdEnumContain(hc, 0)`), each device marked Gone and every transfer and
  mapping kept under the pinned-buffer rule. A DMA safety rule, not a
  tolerance behaviour: it applies at every `XhciTolerance` value, 0
  included.
- **The recovery puts back the Bus Master Enable its proof cleared**
  (`372dcd4`). The recovery's retry runs at DISPATCH_LEVEL with
  `InitBelowPassive` set, where `xhciRestoreBusMaster` refuses, so every
  retry after a successful proof halted and reset the controller and then
  refused at `XHCI_INIT_STEP_BUS_MASTER_RESTORE`. `hcdRecover`, on the
  thread at PASSIVE_LEVEL, now sets the bit there after the proven halt and
  reset (`hcdRestoreBusMaster`: read, set, read back set, under the same
  power gate) and runs the sequence once more, as the same recovery: the
  window is not charged again, and a failed restore leaves the attempt
  failed.
- **Without the proof, the controller is contained, under a reason of its
  own** (`b21e59d`, `2000fcc`). The no-proof branch latches the
  containment's terminal before it pins: `ControllerFailed`, interrupts
  masked, `Tol.Stats.Contained` the pinned branch, and `Tol.Unreadable`
  set, so no recovery or resume reinitializes into the pinned allocation,
  new requests are held on their PDOs as contained, and only a stop and a
  start, which allocate a fresh buffer, end it. Before, a later recovery
  could reinitialize into the same allocation with `CommonBufferPinned`
  still set, and every later drain kept the new transfers too. The latch is
  shared and its value says which containment set it: 1
  (`XHCI_TOL_CONTAINED_UNREADABLE`, this section's all-ones step) or 2
  (`XHCI_TOL_CONTAINED_DMA_UNPROVEN`, the unproven invalidation). The pure
  `XhciTolTerminal` maps the second to its own reason,
  `XHCI_TOL_TERMINAL_DMA_UNPROVEN` (5), which outranks the window and the
  failures in a row as the all-ones containment does, and `XHCISNAP`
  prints it as "CONTAINED: halt and reset did not complete and Bus Master
  Enable would not clear; DMA not proven stopped, common buffer pinned",
  then that it stays failed until a stop and start. No field was added, so
  `XHCI_TOL_STATE` and the snapshot's layout are unchanged. At
  `XhciTolerance` 0 this is the only way `Unreadable` is set. The known
  limitation above applies to this pinned branch as to the all-ones one.

**As built: a terminal no recovery acts on completes what it holds
(revision 11).** 35-V's Windows 2000 SMP leg fired HCH four times inside
the window: the fourth was refused (`ctrl.window.refused`) and a later
Device Manager disable of the controller hung. Every transfer the halted
controller had been given stayed on its rings: the event drain and the
commands refuse a failed controller, and only a recovery's own
`XhciSlotInvalidateAll` ever completed them, so with no recovery begun no
class driver's request ended, the volume's and the disk's query-remove
waited on them, and the stop that would have dropped them never came. The
terminal after three failures in a row has the same hole when no attempt
reached its invalidation (each refused with CNR up, before touching a
register); `2.1.1.0` had it there too, and otherwise completed the
transfers through the first failed attempt's invalidation. The controller
thread now raises that invalidation itself (`hcdTerminalRelease`, after
`hcdRecover`), once per lifetime, at either terminal, when no invalidation
is pending and a device record is left (the pure
`XhciTolTerminalRelease`). Its evidence is the recovery's own: USBSTS read
then, HCH set on a window that decodes (`XhciTolHaltProven`) - the
controller has stopped executing - and the devices are dropped through the
existing departure (`hcdInvalidate`), their transfers completed, the
devices reported gone so PnP can remove them and a stop can follow; new
requests are refused as on a departed device. Without HCH the
invalidation is unproven and the revision-10 rule above runs: Bus Master
Enable cleared and read back clear, or the buffer pinned and the
controller contained, which then names its own terminal reason; with HCH
the reason a dump names is unchanged. Not a
tolerance behaviour: at `XhciTolerance` 0 the window never refuses, and the
run of failures is released the same way.

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
halts and soft retries; the device cycles with their reasons; a histogram
of completion codes 0 to 255 by count; `BackstopDrains`; each location's
budget, its charges and its hold; the controller's recovery window, the
containment and its branch, and its terminal reason; and the values in
effect (`XhciTolerance`, `XhciIntervalCap`, `XhciAvgTrbEsit`), in the snapshot `XHCISNAP`
copies, under the schema rule. They live in the extension, which the
snapshot already carries, rather than in `XHCIHC_COUNTERS`.
`XhciLogErrorBudget` is called from the transfer engine's error path, so
the note ring carries the first records of each code. `xhciqual/quirks.c`
grows report-only AMD rows.

**As built (35-T.8, `dd6bd3c` and `df4045c`, review round 1 `39066c0`,
converged at round 4; revision 9).** The queues' counts are published as
controller-wide sums (`QueueErrors`, `QueueBadCodes`, `QueueUnmatched`,
`QueueForeign`, `QueueHalts`, and the soft retry's six counters), added as
each event moves them; no per-queue figure reaches the dump, and the slot
and endpoint of an error are in the note ring's `xfer.error` records
instead. `XhciLogErrorBudget` is called from the device layer, not the
transfer engine: `hcdTolCountEvent` in `hcd_dev.c` counts every Transfer
Event's code into the histogram and writes the first
`XHCI_LOG_ERROR_BUDGET` (4) records of each error code through
`XhciLogNoteLocked`, since the event drain already holds the controller
lock. (Until `469b4d8` it called `XhciLogNote`, which takes that lock
again: on a multiprocessor kernel the first error completion spun for ever
at DISPATCH_LEVEL; a uniprocessor kernel, Windows 98 SE's, only raises
IRQL for a spin lock, which is why no reading on the E460 showed it. It was
in no published release.) The snapshot schema stays 5: the HCD region's
header grows from 8 to 20 words with its version left at 1, so an older
`XHCISNAP` walks past the new words by the header's size. Words 8 to 18
place the tolerance state inside the extension image - its offset and
size, the counter words, the recovery window's and the clock's offsets,
each kind of location's offset, count and record size, and the hub ports
per hub object - and word 19 is the terminal reason from the pure
`XhciTolTerminal` (running, a recovery owed, three failures in a row, the
window, unreadable), cut by the door under the controller lock.
`XHCISNAP`'s `print_tol` reads it at every verbosity the door answers: the
values in effect, the terminal reason, the recovery window - only stamps
inside the ten minutes counted, older ones reported as kept past the window,
since the driver prunes them only at its next admission - the containment
and its branch, every nonzero counter by name, the code histogram with
xHCI 1.2 Table 6-90's names, and each location charged, held or re-armed;
`-selftest-tol` checks the decoder.
The extension that carries all of it must fit `XHCISNAP`'s extension
image, which `hcd_door.c` checks at compile time: 256 KB since `7e95afa`,
after 35-T.2's fields took the amd64 extension past the 128 KB it had been. The `tol.*` counter block also goes into
the note ring at each stop, as the other counter blocks do.

### 4.9 The budgets

Every behaviour that acts is charged when its action starts, to a budget
with explicit re-arm and lifecycle rules, so no fault loops.

| Behaviour | Kept on | Budget | Re-armed by | Terminal | Reset at |
|---|---|---|---|---|---|
| 4.1 backstop | - | at most one drain per 100 ms an event stands | - | - (it only queues a drain) | - |
| 4.2 soft retry | the TD | 3 retries | - | today's error completion | the TD's completion |
| 4.3 device cycle, 4.5 PED reconnect | the location | 3 re-enumerations | stable-disconnect evidence, or the stable-progress interval with no charge | the location held, shown in the dump | a controller start |
| 4.5 repower | the root port | 3 repowers | the same, while the port is powered | the port held unpowered, shown; released only by a controller start | a controller start |
| 4.6 controller recovery | the controller | 3 recoveries begun in 10 minutes | the window | latched failed until a stop and start | a controller start |
| 4.6 containment | the controller | once | - | contained: released or pinned | a start (a pin as its rule says) |

The intervals, measured on the tolerance clock (section 4.0): stable-progress
60 s; stable-disconnect 1 s; over-current settle 100 ms and over-current
wait 5 s; power-on 100 ms; containment 1 s; and the recovery window 10
minutes - each a tick count, a named constant in the pure core with a host
vector at its boundary.

**As built: who charges a location's re-enumerations (revision 10).** The
budget of three re-enumerations is charged, with tolerance on, by 4.3's
cycles (before and after the PDO), 4.5's PED and hub-port-disable
reconnects, and the configuration's two fallback cycles (a failed
RESET_PORT recovery, a departing device's failed command; `3459f81`); and
at every `XhciTolerance` value, 0 included, by the slot-fatal teardown of
section 4.3's revision-10 note (`3cc80ec`), the only charge made at 0. A
pending cycle is charged once, however many of these asked for it: the
first producer's answer is kept on the port for its connect generation
(`XHCI_TOL_CYCLE_CHARGE`, `XhciTolCycleCharge`, `f364849`) and read by the
rest. Spent, every producer still removes the device and the location is
held; the re-arm and release are the table's, at 0 as at 1. The unproven
invalidation of section 4.6's revision-10 note falls under the
containment row: once, contained pinned, ended by a start.

### 4.10 What the class drivers see

Every behaviour above is owned by the bus, and what a class driver sees is
either an error completion it already gets today for the same fault (a
soft retry exhausted, which completes exactly as today), or a device
departure and arrival (a cycle, a re-enumerated location, a contained
controller's dropped devices), never a new status and never
`STATUS_DEVICE_NOT_CONNECTED` on a listed device. So the design does not
depend on how a class driver reacts to an error.

The per-target reading - what `hidusb.sys`, `usbstor.sys` and `usbaudio.sys`
send, and whether they resubmit, when a transfer completes with each error
this design can return - is therefore not a precondition of this design's
convergence or of its code. It is taken before 35-V, tagged and given
`legal-provenance.md` rows, and used to set 35-V's expectations of what each
class driver does after an exhausted soft retry and after a cycle.

Taken 2026-10-06, static, in design record 13 section 6.7. In short: no
class driver of any target sends `CYCLE_PORT`, so none asks for a
re-enumeration and none spends the location budget of section 4.5 by
itself. After an exhausted soft retry `hidusb` sends `GET_PORT_STATUS`,
`ABORT_PIPE`, `RESET_PORT` and `SYNC_RESET_PIPE_AND_CLEAR_STALL` once per
failed read, with no limit, paced by `hidclass`'s 1-5 s backoff (98 SE on
any failure; from 2000 on not for `STATUS_CANCELLED` or
`STATUS_DEVICE_NOT_CONNECTED`); `usbstor` resets the pipe on a stall and
otherwise sends up to three rounds of `GET_PORT_STATUS` + `RESET_PORT` per
episode (one `RESET_PORT` on NUSB's 98 SE build), then fails every request
until removed; `usbaudio` sends only `ABORT_PIPE` and
`SYNC_RESET_PIPE_AND_CLEAR_STALL` and keeps streaming. After a cycle each
starts afresh: no counter survives the stack's removal.

### 4.11 The off-switch, and what is not a setting

The budgets and intervals of section 4.9 are named constants of the pure
core, not registry values (owner, 2026-10-06): each value costs INF rows on
every install path, a `VAL-*` gate row, footprints and legs, and a budget
set out of range is how a loop or a premature containment would come back.
They are retuned in a build, against a reading.

One switch turns the unconditional behaviours off together, so a user hit
by a bad interaction on some controller has `2.1.1.0`'s handling back
without a new build: `XhciTolerance`, a REG_DWORD in the controller's
driver key read at each start. 1, the default, applies sections 4.1 to 4.6;
0 applies none of them - no backstop, no soft retry, no device cycle for a
refused code or a halt with no TD, no location recovery, no HCH recovery or
recovery window, no all-ones containment - and every one of those paths is
today's; absent, not a DWORD or any other number reads as 1. It does not
touch `XhciIntervalCap` or `XhciAvgTrbEsit`, which keep their own values,
nor the counters of section 4.8, which are observation and stay on so a
dump taken at 0 still shows what the behaviours would have answered. Both
INFs write it at 1 on every controller install path with `0x00010003`
under 34.1's rule, with its `VAL-*` row and the footprints; `XHCISNAP`
names the value in effect. Each behaviour's decision in the pure core takes
it as an input, with host vectors at 0 and 1.

**Its lifecycle.** The value is latched once per start, before admission
reopens, and never changes within a started lifetime. At 0 the gate is at
every producer, not only at the executors: the transfer engine diverts no
Transaction Error and creates no deferred outcome, no event marks a device
`CycleWanted`, the backstop takes no peek, no location fault is charged or
held, no HCH requests a recovery, and no all-ones read is stamped or sets
`Unreadable` - so nothing is produced that a later step could act on. A
change takes effect at the next start, and the start leaves nothing of the
previous lifetime behind:

- **Deferred work ends with the old lifetime, retired or retained.** A
  stop's existing teardown completes the outstanding transfers of a
  device whose common buffer is not pinned; a TD with a deferred outcome is
  one of them, completed as the teardown completes any, and its deferred
  outcome and `RetryWanted` are retired with it, as a `CycleWanted` mark is
  with its device record. Where the buffer is pinned, the teardown keeps the
  device records and their transfers allocated for good (`hcd_enum.c`,
  lines 264 to 268, `DevicesKept`); a deferred outcome, `RetryWanted` or
  `CycleWanted` on a kept record is retained with it, not destroyed, and is
  unreachable: the new lifetime's executors visit only the records its own
  enumeration creates, and never a kept one.
- **Every piece of tolerance state is initialized explicitly by the start**,
  before admission reopens, wherever it lives: the start clears
  `XHCI_EXTENSION` but keeps the surrounding controller and initializes
  enumeration fields one by one (`hcd_ctl.c`, lines 871 to 880;
  `hcd_enum.c`, line 3753), so zeroing is not relied on. That is the retry
  generations, the backstop's observation, the location budgets and holds,
  the recovery window, the all-ones stamp, `Unreadable`, and the tolerance
  clock, which is rearmed.
- **A pinned allocation stays retained**, under the existing rule, whatever
  the value: the start does not release the old common buffer or anything
  kept with it. It allocates a new common buffer for the new lifetime and
  clears `CommonBufferPinned` for that new allocation (`hcd_dma.c`, lines 67
  to 73), as it does today; the flag describes the current lifetime's
  buffer, and the old one stays retained whatever the new one's flag says.

35-V restarts the controller 1 to 0 to 1 with each of these outstanding - a
deferred retry, a pending cycle, an exhausted budget and a held location,
and a containment with and without its proof - and reads that each start
begins clean, that 0 shows `2.1.1.0`'s handling, and that an update keeps a
user's 0.

**As built: what counts at 0 (`469b4d8`, `8b54faa`, `511b3a3`; revision
9).** "The counters stay on" holds for the counters that observe a fault,
not for those that count an action. At 0 the event's own counts still move
- the completion-code histogram, `QueueErrors`, `QueueBadCodes`,
`QueueUnmatched`, `QueueForeign`, `QueueHalts` - and so do the root ports'
faults: `hcdTolPortFeed` counts `PedFaults` and `OcFaults` and acts on
nothing, OCC and PEC counted once each as change bits, and a power loss
with OCC or without it counted once until PP reads set again (the pure
`XhciTolOffOcCount`, `HCD_PORT.TolOffPpLost`). Until `469b4d8` the port
path returned before counting at 0, so a dump taken for the comparison hid
both. The counters of what a behaviour did - `BackstopDrains`, the soft
retry's, the cycles', `HaltReads`, `Repowers`, `Holds`, `HchRecoveries`,
`WindowRefused`, `DeadEpisodes`, `Contained` - stay at 0, since their
producers are gated. So a dump at 0 shows the faults the behaviours would
have met, not what they would have done about them.

**As built: what acts at 0 that is not a tolerance behaviour (revision
10).** Two rules found in the final review apply at every value, because
neither is tolerance: an Incompatible Device Error's Disable Slot and
charged re-enumeration (section 4.3's revision-10 note), whose location may
be charged and held at 0 - so `Holds` and a held location can show in a
dump taken at 0 - and the unproven invalidation's DMA-stop
proof and containment (section 4.6's revision-10 note), which is the only
way `Unreadable` and `Contained` are set at 0. Neither changes what
`2.1.1.0` did for a fault this design added a behaviour for. Revision 11
adds a third, the release of a terminal no recovery acts on (section 4.6's
revision-11 note): at 0 it can meet only the run of failures, whose
transfers `2.1.1.0` completed through a failed attempt's invalidation
whenever one reached it.

## 5. Injection

QEMU raises none of these faults, and a fabricated event is only useful if
the hardware state the driver then acts on is coherent with it. The
`qemu`-flavour-only layer, independent of 35.4's optional speed-table
override, makes each fault real where QEMU
allows it, and where it does not, it emulates the command that would meet
the faked state rather than send it.

**The soft retry's target** is chosen so its state is known: an interrupt-IN
TD of QEMU's HID tablet or mouse while it has no input, which QEMU NAKs, so
the TD sits at the controller's dequeue with nothing transferred. The layer
issues a real Stop Endpoint, consumes the resulting Stopped event itself so
the driver never sees it, confirms from the context that the dequeue is the
TD's first TRB (and if it is not, abandons the attempt and counts it), then
delivers an injected Transaction Error for that TD. Context reads answer
Halted until the driver's Reset Endpoint, which the layer completes with
Success without sending - the real endpoint is already Stopped, the state a
Reset Endpoint leaves - and the driver's doorbell restarts the TD for real.
Persistent: injected again on each restart.

| Fault | How it is made | What the driver's commands meet |
|---|---|---|
| Lost interrupt | the ISR acknowledges as today but does not queue the DPC, for a set number of interrupts | real hardware; the event waits in the ring |
| Transaction Error | as above | the emulated Halted and Reset Endpoint, then real hardware |
| Reset Endpoint failing | as the Transaction Error, the emulated Reset Endpoint answering Context State Error | `hcdCfgFault`'s real path |
| Refused code | an injected Transfer Event carrying a refused code, for a real TD of a real device | the cycle's real Disable Slot and re-enumeration |
| Halt with no TD, Halted | a real Stop Endpoint, its Stopped event consumed, an injected Stall with a pointer off the ring or zero; context reads answer Halted | the cycle's real Disable Slot |
| Halt with no TD, Error | as above, context reads answering Error | the same |
| Halt with no TD, stale | as above with no context fake: the endpoint really reads Stopped, then the layer rings it | the context read only; no cycle |
| Endpoint Not Enabled | an injected code 12 event | the cycle |
| EP0 during the thread's own transfer | an injected refused code on EP0 while the thread waits on a control transfer to a published device | the wait ended, no reuse of the record or the scratch before the Disable Slot, the cycle |
| EP0 before the PDO | the same during the enumeration's own control transfers, before the device is published | the pre-PDO cycle: the attempt failed, the slot disabled, the connect run again |
| Soft retry, a submission rings during the Reset Endpoint | the layer holds the emulated Reset Endpoint's completion while a submission to the pipe rings it, the real endpoint Stopped and so resumed by that ring | the TD resumed early; the thread's generation check |
| Soft retry, a second error during the Reset Endpoint | as above; once the submission's ring has resumed the real endpoint, the layer issues another real Stop Endpoint, consumes its Stopped event, confirms the dequeue is still the TD's first TRB (abandoning and counting the attempt if not), reinstates the emulated Halted state and only then delivers a second Transaction Error, all before the held completion is delivered | a newer generation; the thread leaves `RetryWanted` set |
| Root port PED | a real write of PED 1 to a USB 2.0 root port's PORTSC, which disables the port without setting PEC, since PEC reports the controller's own disable (`xhci-data-structures.md`, line 232); so the layer also answers PEC set in that port's PORTSC reads, until the driver's change-bit acknowledgement writes PEC 1, and injects a Port Status Change Event for the port. QEMU ignores the PED write (revision 11), so the layer answers PED clear as well, until the driver's port reset (PR written) or the device's departure (CCS read clear) | the real disabled port (on QEMU, the emulated PED clear and its end at the reset), the emulated PEC and its acknowledgement |
| Over-current | QEMU has no port power control; PORTSC reads for the port answer PP clear and OCA and OCC set until the layer releases OCA; a write setting PP is recorded and answered | emulated reads |
| HCH | a real write clearing Run/Stop | real hardware; the in-place recovery runs for real |
| All-ones, proof | the health poll's USBSTS read answers all-ones; configuration space is real | real Bus Master Enable clear and read-back |
| All-ones, no proof | as above, with the Bus Master Enable read-back answering set | the pinned branch |

The expected outcomes are fault-specific, and the legs check each:

| Fault | Transient (cleared within the budget) | Persistent |
|---|---|---|
| Lost interrupt | drained by the backstop at the first poll 100 ms after it was seen, `BackstopDrains` counted | the same each time; the backstop is the delivery, not a recovery, so it has no terminal |
| Transaction Error | the TD completes after its retries, data intact | today's error completion after the third retry |
| Reset Endpoint failing | `hcdCfgFault`'s outcome, as today | the controller window's terminal |
| Refused code, halt with no TD (Halted or Error), Endpoint Not Enabled, EP0 | the device cycled and working again | after three, the location held |
| Halt with no TD, stale | nothing but the context read | nothing |
| Root port PED | the device re-enumerated | the location held, powered |
| Over-current | the port repowered and the device re-enumerated | the port held unpowered until a controller start |
| HCH | one recovery | the window reached, the controller latched failed |
| All-ones | none (shorter than the containment interval) | contained: with proof, devices dropped and transfers parked; without, the buffer pinned and transfers kept |

And beside them: a cancel, ABORT_PIPE, RESET_PIPE, SYNC_RESET_PIPE and
RESET_PORT each pending at each of the soft retry's two decisions; a cycle
requested while a cycle, a RESET_PORT or the location's PED reconnect is
already running; a stream endpoint; teardown and a power transition during
each; and every budget exhausted.

**As built, first part (35-T.9, `8d27a13`, review rounds `cade36c` and
`b0dd28c`, converged at round 3; revision 9).** The faults whose driver
behaviour was in the tree when it was built: the pure `xhci_inj.c` with
host suite `test_inj`, the driver half `hcd_inj.c`, compiled into the
`qemu` flavour alone. The trigger is `XhciQemuInject`, a REG_DWORD in the
controller's driver key that the thread re-reads about once a second: bits
31:24 a sequence (a change fires; the value present at a start is latched
unfired), 23:16 the fault, 15:8 a root port (0 the first that fits), 7:0
the argument. Built: 01 lost interrupt (the ISR acknowledges and does not
queue the drain, for a counted window the thread arms and disarms, decided
by the unsigned `XhciInjIrqDrop`), 02 root port PED, 03 over-current and 04
its release, 05 HCH, 06 all-ones with the Bus Master Enable proof real, 07
all-ones with the read-back answering set, and FF clear; 08 to 18 are
reserved for the rows of 35-T.2 to 35-T.4 and refused with a note, which is
the second part, still to be built. Two departures from the table above:

- **All-ones is a per-pass snapshot met by both readers.** The fault is
  answered at the health poll's USBSTS read as the table says - the read
  `2.1.1.0`'s handling rests on, taken at every `XhciTolerance` value, so
  the fault reaches it at 0 - and at the containment step's own read: the
  thread's first act each pass (`HcdInjPoll`, the pure `XhciInjDeadPass`)
  sets the fault for the pass and spends one pass of its argument whether
  or not any read follows, and every read in that pass answers from it, so
  a finite injection ends even on a failed controller, whose health poll
  no longer reads.
- **A command that needs a live controller is refused on a failed one.**
  01, 02, 03, 05, 06 and 07 (`XhciInjNeedsLive`) are refused with
  `qemu.inj.refused` while `ControllerFailed` is set; CLEAR and the
  over-current release are always taken.

**As built, second part (35-T.9, `5044966`, review round 1 `6808ff9`;
revision 10).** Codes `08` to `12` hex, reserved by the first part, are
built: the rows of 35-T.2 to 35-T.4 - `08` Transaction Error, `09` the
soft retry's Reset Endpoint failing, `0A` a refused code, `0B` and `0C` a
halt with no TD under Halted and Error, `0D` the stale one, `0E` Endpoint
Not Enabled, `0F` EP0 during the thread's own transfer, `10` EP0 before the
PDO, `11` and `12` the soft retry's two races - on the target the table
names, an interrupt-IN TD QEMU is NAKing. The pure decisions are in
`xhci_inj.c` with host vectors in `test_inj`; the hooks (the drain, the
context state reads, `hcdCommand`, the doorbell, the thread's EP0 doorbell
and the service step before the soft retry's) are under
`XHCI_FLAVOUR_QEMU`. `build-and-test.md`, "The qemu flavour's test aids",
gives each code's making, argument and transient and persistent outcome,
and is not repeated here. Where it departs from the text above, or fills
in what the text left open:

- **QEMU fetches the TD ahead.** On the NAK it keeps the interrupt TD
  fetched, so its Stop Endpoint writes a dequeue past the TD. The layer
  recognises exactly that case (a one-TRB TD, Stopped with nothing moved,
  the dequeue on the next TRB) and puts the dequeue back with a real Set TR
  Dequeue before it injects; anything else abandons the attempt and counts
  it, as the text says.
- **The races' ring is the layer's own.** The HID class driver's reads are
  already posted, so no submission arrives on cue; the layer rings the
  endpoint itself, the same doorbell write. The race runs only for the soft
  retry's own Reset Endpoint - the pipe not paused or closed and its
  `RetryWanted` naming the deferred TD still at the queue's head; any other
  reset, and every reset at `XhciTolerance` 0, is answered Success with
  nothing rung (`6808ff9`).
- **The answers end only on a command that succeeds.** A Disable Slot,
  Address Device, Reset Device, a Set TR Dequeue leaving Error, or a
  Configure Endpoint that deconfigures the slot or whose Drop or Add flags
  name the endpoint, is sent and ends the emulation only on Success; a
  Configure Endpoint for another function of a composite device leaves it
  standing (`6808ff9`).
- **`09`'s outcome is the controller recovery window's.** `hcdCfgFault`
  cycles a location only when `HcdHubPathPresent` finds the device gone;
  the injected device is present, so the failed Reset Endpoint requests
  the controller recovery, charged to the window: transient, one recovery
  and the device re-enumerated; persistent, the window's terminal, the
  controller latched failed. The second table's "`hcdCfgFault`'s outcome,
  as today" is that.
- **The refused code is Bandwidth Overrun** (18) for `0A`, and `0C`'s
  off-ring pointer is the event ring's base, a TRB no transfer ring holds.

Task 35.4's optional override is beside it in the same flavour:
`XhciQemuPsiE460`, exactly 1, makes `XhciPortMapOverridePsi` replace every
USB 3 protocol group's PSI table with the E460's (PSIC 3, `04E00121`
`09C00122` `13800123`) after both parses, noted `qemu.psi.e460`.
`build-and-test.md`, "The qemu flavour's test aids", is the operator's
reference for both.

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

The release notes and README (`XhciTolerance`, `XhciIntervalCap`, `XhciAvgTrbEsit`, the AMD
report, a device that is cycled after a controller fault), record 13
(section 6's error contract, from 4.10), record 15 (the shared peek),
`implementation-invariants.md` ("Fatal Errors": the refused-code deviation
superseded, the all-ones containment added), `failure-diagnosis.md`,
`xhcisnap/README.md`, `xhciqual/hardware-testing.md`, `source-files.md` and
`runs/run-35.md` (roadmap 35.6).

Done at 35.6 (2026-10-07): the release notes' section "Controller faults:
what the driver does about them", the three values under "Registry
settings", a known limitation and untested-ground rows; the README's
"Controller faults" and its Tuning rows; the package readme template;
record 13 section 6.8; record 15 section 2; `implementation-invariants.md`'s
refused-code bullets, rewritten; `failure-diagnosis.md`'s "a device that
stops until it is replugged"; `xhciqual/hardware-testing.md`'s AMD rows;
`source-files.md`; and `runs/run-35.md`, opened. `xhcisnap/README.md`'s
counters are 35-T.8's.

Reconciled with the code at revision 9 (35.6, 2026-10-07, at `adbc4e3`):
the release notes, the README and the package readme template say what
`XhciTolerance` 0 still counts, which error the soft retry takes and where
it does not, that a spent budget removes the device at the next fault, and
the known limitation of section 4.6; record 13 section 6.8,
`implementation-invariants.md`'s refused-code bullets and
`failure-diagnosis.md`'s reading order take the departures of sections
4.2, 4.3 and 4.8; `source-files.md` names the files 35-T.2, 35-T.3/4, 35-T.8
and 35-T.9 changed; `runs/run-35.md` records the four tasks. The
operator's description of the injection is `build-and-test.md`'s.

Reconciled again at revision 10 (35.6, 2026-10-07, at `2000fcc`): the
release notes, the README and the package readme template say that a
device the controller reports incompatible is removed and re-enumerated a
bounded number of times, and name `XHCISNAP`'s new terminal reason;
`xhcisnap/README.md` decodes terminal reason 5;
`implementation-invariants.md`'s "Fatal Errors" says how the HCD answers
code 22 and an invalidation no halt or reset proved; `runs/run-35.md`
records 35-T.9's second part and the final review's findings.

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
- A code nothing claims and a halt with no TD cycle the device, rather than
  recover the endpoint in place (after review round 3).
- The budgets and intervals are constants, not registry values; one
  switch, `XhciTolerance`, INF-written at 1, turns the unconditional
  behaviours off together (section 4.11). The value names `XhciIntervalCap`
  and `XhciAvgTrbEsit` are confirmed.

## 9. Open for review

The values of section 4.9's constants, which stand as proposed and are
retuned in a build against the bench's and any tester's readings.

## 10. The review findings, and where each is answered

Round 1:

| Finding | Severity | Answered in |
|---|---|---|
| 1 Average TRB Length neither legal everywhere nor free | high | 4.7: a switch, off by default |
| 2 A refused code does not prove its TD safe to complete | high | 4.3: no TD is completed on the event; the Disable Slot takes the TRBs back |
| 3 "Fail the oldest" can fail an unrelated transfer | high | 4.3: no TD is attributed at all |
| 4 Class-driver recovery assumed | high | 4.10: bus-owned behaviour only |
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
| 1 The injected Reset Endpoint met the wrong state | high | section 5: emulated, the real endpoint already Stopped |
| 2 All-ones containment released I/O with no DMA proof | high | 4.6: admission closed first, Bus Master Enable read back, or pinned |
| 3 `STATUS_DEVICE_NOT_CONNECTED` makes Windows 98 HID resubmit | high | 4.6 and 4.10 |
| 4 The existing quiesce does not retire the failed TD | high | 4.3: no quiesce is used; the device is cycled |
| 5 Pause is not retry ownership | high | 4.2: no pause is taken |
| 6 Dropping off-ring and zero pointers defeats a halt with no TD | high | 4.3: they request the context read |
| 7 EP0 has two owners | medium | 4.3: the thread's wait ended, client transfers end with the device |
| 8 CCS 0 then 1 can come from repowering | high | 4.5 |
| 9 A refused code on a Running endpoint still hangs | medium | 4.3: a refused code cycles in any state |
| 10 The budget table was not executable | medium | 4.9 |
| 11 Backstop activity does not prove a lost interrupt; extra DPCs | medium | 4.1 |
| 12 The interval cap and fast polling do not compose by themselves | medium | 4.7 |
| 13 Roadmap alignment | low | 35-V and 35.6 |

Round 3:

| Finding | Severity | Answered in |
|---|---|---|
| 1 Retry abandonment releases held work before the winning operation; no pipe lock | high | 4.2: no pause, so nothing is released; the decision under the controller lock; abandonment is today's retirement |
| 2 The context's dequeue is undefined in Error or Disabled | high | 4.3: the dequeue is never read |
| 3 Moving the ring's dequeue does not make the TD the queue's head | high | 4.3: no in-place recovery |
| 4 A recycled endpoint resets only the host's toggle | high | 4.3: the cycle resynchronizes both sides |
| 5 Stopping a Running endpoint does not resolve a missing completion | high | 4.3: a refused code cycles in any state |
| 6 EP0 needs a Setup boundary and a thread not waiting on itself | high | 4.3: the wait ended for the cycle |
| 7 Two polls are not 100 ms apart; equal pointers recur | medium | 4.0 and 4.1: elapsed time, the drain and start generations; 4.6 likewise |
| 8 Pinning alone does not contain an unreadable controller | high | 4.6: admission closed first, the pin kept under its own rule |
| 9 Episode resets bypass the bound; external hub locations | medium | 4.5 and 4.9: one budget per location, kept across cycles and reconfiguration |
| 10 Injection ownership and branches | medium | section 5: the NAKing target, the consumed Stopped event, the Error, stale, Endpoint Not Enabled, EP0, command-failure and no-proof legs |
| 11 The class-driver reading's place is inconsistent | medium | 4.10 and 35-T.0: before 35-V, not a precondition of the design |
| 12 The roadmap named the reviewer | low | the roadmap, reworded throughout |

Round 4:

| Finding | Severity | Answered in |
|---|---|---|
| 1 Containment drained before its proof | high | 4.6: admission closed by a new `Unreadable` flag, `HcdIoDeviceDrain` only after the proof or after the pin |
| 2 The power gate was not taken; the health poll lacks D0 and `DpcClosed` | high | 4.0, 4.1 and 4.6: steps of their own after `hcdRecover`, taking the gate and admitting themselves |
| 3 A submission can restart the TD during the Reset Endpoint | high | 4.2: the retry generation and the TD's identity; a newer request never cleared |
| 4 Deferred retirement left the engine's terminal mutations; the matched TD may not be the head | high | 4.2: interception before the mutations, the deferred outcome kept apart; only the head diverted |
| 5 `HcdEnumCycle` needs a published device; the scratch buffer | high | 4.3: the pre-PDO cycle; no reuse of the record or the scratch before the Disable Slot |
| 6 The settle clock wraps at 429.5 s and follows system-time adjustments | high | 4.0: the tolerance clock |
| 7 An unpowered hold cannot show its release evidence | medium | 4.5 and 4.9: released only by a controller start |
| 8 Writing PED does not set PEC | medium | section 5: PEC and its event emulated beside the real disable |
| 9 35-T.5 lost its USB 2.0 and PEC conditions; a stale roadmap line | medium | the roadmap |

Round 5:

| Finding | Severity | Answered in |
|---|---|---|
| 1 A filtered wall clock does not measure elapsed time | medium | 4.0: ticks of a relative kernel timer, late but never early |
| 2 Containment acted on the ungated poll's samples, across lifetimes | high | 4.6: USBSTS read by the gated step itself; the stamp cleared by any lifetime or power change |
| 3 The deferred outcome lacked the reported TRB pointer | high | 4.2: the whole event kept, replayed with interception bypassed |
| 4 The pre-PDO cycle did not meet the enumeration executor | high | 4.3: `ABANDONED_FOR_CYCLE` through the executor, continuations and retries suppressed, one complete teardown; a failed Disable Slot sets `ScratchTainted` |
| 5 `Unreadable` refusals completed as `STATUS_DEVICE_NOT_CONNECTED` | high | 4.6: new requests parked, cancellable, not refused |
| 6 The second-error injection lost coherence | medium | section 5: another real Stop and the emulated Halted state reinstated first |
| 7 Retry exhaustion had two routes | low | 4.2: never intercepted; the thread never sees a spent TD |

Round 6:

| Finding | Severity | Answered in |
|---|---|---|
| 1 A relative timer can expire up to one clock period early, cumulatively | medium | 4.0: each tick credited at 45 ms against the slowest PC clock, intervals counted as `ceil(T / 45) + 1` ticks |
| 2 The pre-PDO teardown left the location's machine where a CONNECT is ignored | high | 4.3: one handler sets it `EMPTY` with its slot and device cleared, once per attempt, before the CONNECT |
| 3 Parked requests' release triggers misstated; `HcdIoPark` can decline | low | 4.6: today's triggers named; the declined cases completed `STATUS_CANCELLED` or `STATUS_DELETE_PENDING` |

Round 7:

| Finding | Severity | Answered in |
|---|---|---|
| 1 The pre-PDO reconnect read CCS, which an external hub's port does not have | medium | 4.3: connected read as `hcdPortConnected` reads it, for either kind of location |
| 2 "About 2.2 times" stated as an upper bound | low | 4.0: no fixed upper bound; 2.2 the long-interval factor under timely DPCs |

Round 8 found nothing material. Round 9, on section 4.11 and the reviewer-name
scrub of the other records:

| Finding | Severity | Answered in |
|---|---|---|
| 1 The off-switch had no lifecycle contract | medium | 4.11: latched per start, gated at every producer, deferred work settled by the old teardown, all tolerance state initialized explicitly, a pin kept; the 1-0-1 restart legs |

Round 10:

| Finding | Severity | Answered in |
|---|---|---|
| 1 A pinned stop keeps its transfers, and a start clears the flag for its new buffer | medium | 4.11: retired work told from retained work, kept state unreachable by the new lifetime, the old pinned allocation told from the new buffer |
