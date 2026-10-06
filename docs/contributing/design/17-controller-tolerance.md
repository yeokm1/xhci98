# Controller tolerance

Design record for roadmap-hcd tasks 35-T.0 to 35-T.9 (Phase 35, release
`2.2.0.0`). Revision 1, 2026-10-06, written before code. Revision 0
(`60f8f64`) drafted the findings and the shape; Codex's review of it,
round 1, returned eleven findings (six high, five medium), all taken, and
this revision answers them: section 10 maps each finding to where it is
answered. Nothing below is converged.

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
(`XHCI_ASMEDIA_MODIFY_FLOWCONTROL`); Intel's `XHCI_MISSING_CAS`, which is
Phase 35's own task 35.3; and, withdrawn in this revision, the Average TRB
Length change (section 4.7).

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
  what it shows a class driver after a fault is a status that driver is
  known to handle (section 4.10).

A behaviour that fails any of these is gated - a pure-core decision on the
PCI vendor and device id and a REG_DWORD in the controller's driver key
with `XhciMissingCas`'s three values (1 the default, the list; 0 off; 2
every controller), written by both INFs under 34.1's rule - or it is not
done.

## 3. The findings

Read in this tree at `08df41f` on 2026-10-06, corrected at revision 1. Each
is a path where a fault leaves a device dead and nothing notices.

| # | Path | Today | Linux (mirror) |
|---|---|---|---|
| T1 | Event delivery | The event-ring drain is queued only by the ISR (`hcd_ctl.c`, `hcdIsr`, which counts `DpcsInFlight` before `KeInsertQueueDpc` and rolls it back on a refused insertion); the 100 ms health poll checks HCE, HSE and command age. A lost line interrupt, or EHB or IE left stuck outside an ISR-to-DPC handoff, silences the bus. The health poll's re-arm is the miniport's `XHCI_FIX_EVT_REARM`, built in no flavour (`xhci_cmd.c`) | No backstop; nothing to copy |
| T2 | USB Transaction Error | Completes the TD at once with `USBD_STATUS_DEV_NOT_RESPONDING` (`xhci_xfer.c`, `XhciXferCodeInfo`) | Soft retry: Reset Endpoint with TSP 1 and the TD kept, while the endpoint-wide `err_count++ > MAX_SOFT_RETRY` (3) is false - four retries from zero - not behind a TT, not on `XHCI_NO_SOFT_RETRY` controllers (`xhci-ring.c`, `process_bulk_intr_td`, line 2531) |
| T3 | Codes nothing claims | Vendor information codes 224 to 255 already complete as success and vendor error codes 192 to 223 as a fatal Undefined Error (`xhci_xfer.c`, `XhciXferCodeInfo`, line 103): both kept as they are. What is refused is the rest - Endpoint Not Enabled, Ring Underrun and Overrun, Missed Service, Bandwidth Overrun, Event Ring Full in a Transfer Event, and unassigned codes - most of them carrying no TRB pointer: `BadCodes` is counted and nothing else. The refusal is a recorded deviation whose recovery was usbport's URB timeout and the Stop Endpoint its cancellation issued (the comment at `xhci_xfer.c` line 270; `implementation-invariants.md`, "Fatal Errors"); the HCD has no usbport, so only a cancel the class driver happens to send reaches that Stop Endpoint | An unrecognised code is logged and the handler returns before its halted-endpoint check (`xhci-ring.c`, `handle_tx_event`, line 2769) |
| T4 | A halt with no TD | A halting event the queue cannot match (`UnmatchedEvents`; `ForeignEvents`, which also counts slot and DCI mismatches and pointers off the ring) leaves the endpoint Halted with no URB failed; later submissions ring a doorbell a Halted endpoint ignores (`hcd_io.c`, `HcdIoSubmit`) | `check_endpoint_halted`, for an event that names a live endpoint, with Transaction, Babble and Split errors qualified as non-isochronous (`xhci-ring.c`, line 2191); `prepare_ring` admits submissions to a Halted endpoint (line 3260) |
| T5 | Root port disabled or over-current | A USB 2.0 root port with PED cleared and CCS set is acknowledged and nothing more (`hcd_enum.c`, `hcdPortChanged`; `xhci_link.c`, `XhciLinkPortFeed`); `GET_PORT_STATUS` answers enabled from the PDO's state (`hcd_urb.c`, `hcdPortStatus`); an over-current that cleared PP is never restored. The external hub path re-enumerates on both (`xhci_hub.c`, `XhciHubPortDecide`) | `hub.c`, which is not in the mirror |
| T6 | Controller halted | An unexpected HCH is not looked at (`xhci_cmd.c`, `XhciControllerHealthPoll`). An all-ones USBSTS is counted (`HealthPollsDead`) and read as a window that stopped decoding, not a fatal report - a recorded invariant (`implementation-invariants.md`, "Fatal Errors"), kept. The recovery budget `RecoveryFailuresConsecutive` is cleared by every completed recovery (`xhci_init.c`, line 4190), so it bounds a run of failed recoveries, not a recover-and-fail-again cycle | - |
| T7 | Long interrupt intervals | Interval from bInterval, as Linux's base rule (`xhci_pipe.c`) | `XHCI_LIMIT_ENDPOINT_INTERVAL_9` caps it at 8 (32 ms) on older AMD ids (`xhci-mem.c`, `xhci_endpoint_init`) |
| D | Diagnosability | `XHCIHC_COUNTERS` lies outside the snapshot `XHCISNAP` copies (`hcd_door.c`); the per-queue `Errors`, `BadCodes`, `UnmatchedEvents` and `ForeignEvents` are published nowhere; `XhciLogErrorBudget` has no HCD caller | - |

## 4. The behaviours

### 4.1 The lost-interrupt backstop (35-T.1, unconditional)

Each health poll that its existing admission lets read - `HcInfoStatus`
good, `INITIALIZED` set, the controller not failed, in D0, `DpcClosed`
clear - reads, under the controller lock, the event TRB at the software
dequeue pointer. A cycle bit equal to the consumer's says the controller
has written an event the drain has not consumed. Then, and only then, it
queues `IsrDpc` through the same admission the ISR uses: `DpcsInFlight`
incremented before `KeInsertQueueDpc` and decremented if the insertion is
refused, so the teardown's wait counts it, and nothing is queued once
`DpcClosed` is set. One helper serves both producers; record 15's
opportunistic peeks reuse it.

It writes no register. ERDP and EHB stay the drain's to publish; IE is not
touched, since IE clear is the normal state between the ISR's acknowledgement
and the DPC's re-arm, and the DPC's own re-arm and its escalation stay the
only path that restores it. `XHCI_FIX_EVT_REARM` is retired.

Interleavings, each to be shown harmless: the ISR and the poll both find
work and both try to queue (the second insertion is refused and rolled
back; one drain runs); the poll queues while a drain is in flight on
another processor (the DPC runs again and finds the ring empty, already a
counted case, `DrainBoundEmptyHits`); the poll reads while the controller
writes the TRB (the cycle bit is written last, so a torn read sees the old
cycle and waits a poll); teardown between the peek and the insertion
(`DpcClosed` and the in-flight count, as for the ISR).

Its count is `BackstopDrains`; a nonzero value on a line-interrupt machine
is the evidence that an interrupt was lost. Cost on a compliant controller:
one cached memory read per 100 ms, and no DPC.

### 4.2 The soft retry (35-T.2, unconditional but for an exclusion list)

On a USB Transaction Error for a bulk or interrupt endpoint whose device is
not behind a TT, the TD is kept and the endpoint is reset with Reset
Endpoint, TSP 1, and rung again. xHCI 1.2c 4.6.8.1 provides TSP for this
purpose: the host's sequence state is kept, so the device's and the host's
data toggles stay aligned without a CLEAR_FEATURE. The retry path issues no
Set TR Dequeue Pointer and no CLEAR_FEATURE, and keeps the TD's buffer
mapping until the TD completes.

The bound is this project's policy, not Linux's: three retries per TD (Linux
allows four, endpoint-wide). The count is the TD's own, cleared when it
completes; a Success or Short Packet on the endpoint clears nothing else.
Past the bound the error completes as today.

Arbitration: the retry is owned by the pipe's existing pause state, so a
cancel, ABORT_PIPE, RESET_PIPE or RESET_PORT that arrives while the Reset
Endpoint is outstanding wins: the doorbell is rung again only if no such
request is pending once the command completes, and otherwise the retry is
abandoned and the request proceeds from the endpoint's Stopped state.
Submissions arriving during a retry are queued, not failed. A failed Reset
Endpoint goes to `hcdCfgFault` as any recovery command does today.

Not on Linux's `XHCI_NO_SOFT_RETRY` controllers (`1022:43B9`, `1022:43BB`,
`1B6F:7023`, `1B6F:7052`). Cost on a compliant controller: nothing, since
no Transaction Error arrives.

### 4.3 The codes nothing claims (35-T.3, unconditional)

Vendor codes keep today's handling. For the rest of the refused set, a code
never completes a TD by itself, because the controller may still own the
TD's TRBs and buffers; it marks the endpoint suspect and wakes the thread,
which reads the Endpoint Context and acts on its state through the
existing machinery:

- Halted or Error: the existing quiesce (Reset Endpoint or Set TR Dequeue),
  which retires the TRBs, then the drain completes the TD at the dequeue
  pointer with `USBD_STATUS_INTERNAL_HC_ERROR` and the rest are resubmitted
  as an ABORT_PIPE's survivors would be - or failed, to be settled against
  the class-driver reading (section 4.10).
- Disabled (Endpoint Not Enabled): the Configure Endpoint that
  `XHCI_EPQ_NO_CONTEXT` already owes, then the same.
- Running: nothing; the code is counted and the endpoint keeps running.
  Ring Underrun and Overrun and Missed Service are isochronous conditions;
  on an isochronous endpoint the existing isochronous path keeps them.

A pointerless event names an endpoint, not a TD, and is handled at the
endpoint alone. An event whose slot or DCI names no live pipe is counted
and dropped as today. The deviation in `implementation-invariants.md`,
"Fatal Errors", is superseded and rewritten (35.6), and the comment at
`xhci_xfer.c` line 270 corrected.

### 4.4 A halt with no TD (35-T.4, unconditional)

A halting code on an event the queue cannot match acts only when the event
names a live endpoint: its slot and DCI map to an open pipe of the current
configuration, the code is Stall, or Transaction, Babble or Split
Transaction on a non-isochronous endpoint. Stale events - a TD already
retired, a pointer off the ring, a slot or DCI with no pipe - are counted
and dropped as today. For a live endpoint the thread reads the Endpoint
Context; if it is Halted, the endpoint is recovered as section 4.3's Halted
case, and the transfer completed is the one the controller's dequeue
pointer names, never "the oldest". Stream endpoints recover per stream
context; EP0 stays the thread's own transfer path, which already has its
timeout.

Submissions to a Halted pipe are kept queued, as Linux admits them; revision
0's failing them is withdrawn. The doorbell is rung once the endpoint is
out of Halted.

### 4.5 The root port the controller gave up on (35-T.5, unconditional)

A USB 2.0 root port with PEC set, PED clear and CCS set, holding a device,
is fed a disconnect and, once the disconnect's teardown has completed, a
connect - as `XhciHubPortDecide` does for an external hub. The disconnect
is serialized: the device's PDO is reported missing and its slot disabled
before the connect starts. `GET_PORT_STATUS` keeps the PDO-state answer
and its race rule; between the disconnect and the new PDO the old PDO is
no longer listed and answers 0.

An over-current that cleared PP: the port is held, the driver waits to
observe OCA clear, then sets PP and waits the power-on delay before
looking for a connection.

Both are per-port episodes under section 4.9's budget. An episode is armed
by the first fault and re-armed only by a genuine reconnect - CCS observed
0 then 1 - or by a device that has stayed enumerated for the re-arm
interval; synthetic disconnects of the driver's own do not re-arm it.
Exhausted, the port is held in a terminal state the dump shows (`hold`
with a reason), as 29-A.5's holds are, until a genuine reconnect.

### 4.6 The controller halted (35-T.6, unconditional)

HCH read set while the driver's own state says the controller runs - D0,
started, not stopping, not in recovery, not in a power transition, Run/Stop
last written 1 - requests the in-place recovery, as HCE and HSE do,
latched as they are.

An all-ones USBSTS keeps today's reading: the window stopped decoding, which
on a surprise removal or a power transition is expected and is not
recoverable by a reset. What changes is containment: once all-ones has
persisted for the containment interval while PnP and power say the device
is present and in D0, the controller is marked unreadable, the health poll
stops touching it, and outstanding and new I/O is failed with
`STATUS_DEVICE_NOT_CONNECTED`, so class drivers stop waiting. Revalidation
happens only on a start or a D0 entry.

### 4.7 The interval cap (35-T.7, gated); Average TRB Length withdrawn

An interrupt endpoint whose Interval exceeds 8 (32 ms) is programmed at 8,
as Linux's `XHCI_LIMIT_ENDPOINT_INTERVAL_9`, on every AMD controller
(vendor `1022`), wider than Linux's list because the AM5 and X570 lines are
unlisted and the report is on one of them, under `XhciIntervalCap` (section
2's values). Polling a long-interval endpoint at 32 ms is legal under USB
2.0 and costs a few polls a second, which is why it is gated. It applies at
every speed, Interval being an exponent in each; isochronous endpoints are
not capped, since their interval sets their data rate. The fast-poll mode
lowers intervals and never raises them, so the two compose.

The Average TRB Length change of revision 0 is withdrawn (Codex round 1,
finding 1): the field enters the controller's bandwidth admission (4.14.1.1),
so changing it changes what a healthy controller is told; Max ESIT Payload
may be zero on a zero-bandwidth isochronous endpoint or exceed the 16-bit
field; and nothing ties it to the report. It stays 1024 for interrupt
endpoints, the specification's suggestion.

### 4.8 The counters a user can send (35-T.8)

The per-queue `Errors`, `BadCodes`, `UnmatchedEvents`, `ForeignEvents`,
halts and soft retries, a per-completion-code histogram, `BackstopDrains`,
the root-port and controller episodes and their terminal states, and the
gate in effect, in the snapshot `XHCISNAP` copies, under the schema rule.
To settle: whether they move into the extension or into a snapshot region
of their own, the histogram's size, `XhciLogErrorBudget`'s caller, and the
report-only AMD rows in `xhciqual/quirks.c`.

### 4.9 The episode budgets

Every behaviour above that acts is charged to a budget with an explicit
re-arm, so no fault loops:

| Behaviour | Episode | Budget | Re-armed by | Terminal |
|---|---|---|---|---|
| 4.1 backstop | none needed | - | - | it only queues a drain |
| 4.2 soft retry | one TD | 3 retries | the TD's completion | the error completes as today |
| 4.3, 4.4 endpoint recovery | one endpoint | to settle | the endpoint's next Success | the pipe failed until the class driver resets it |
| 4.5 root port | one port | to settle | a genuine reconnect, or the re-arm interval enumerated | the port held, shown in the dump |
| 4.6 HCH and the in-place recovery | the controller | recoveries per window, to settle | the window elapsing without a fault | the controller latched failed until a stop and start |

The last row adds a rate bound beside `RecoveryFailuresConsecutive`, which
keeps its meaning; a recover-and-fail-again cycle then ends.

### 4.10 What the class drivers do with an error

Revision 0 assumed that failing a transfer makes a class driver reset its
pipe. That is not established, and each target's drivers are their own
(design record 13 section 6). 35-T.0 reads, per target, what `hidusb.sys`,
`usbstor.sys` and `usbaudio.sys` do when a transfer completes with each
status this design can return - which requests they send (GET_PORT_STATUS,
ABORT_PIPE, RESET_PIPE, RESET_PORT, CYCLE_PORT), and whether they stop
resubmitting - tagged and given `legal-provenance.md` rows as every
contract reading is. Until that reading exists, the design prefers
recoveries the bus owns (4.2, the host half of 4.3 and 4.4) over failures
that depend on a client, and the fallback when a client never resets is
4.3's terminal state, which the dump shows.

## 5. Injection

QEMU raises none of these faults, and a fabricated event is only useful if
the hardware state matches it: a Transaction Error injected while QEMU's
endpoint is Running makes Reset Endpoint fail its precondition. So each
injection in the `qemu`-flavour-only layer, beside 35.4's, names the
hardware state it fakes and how every command whose precondition depends on
it is answered - for a halt, the endpoint is first stopped for real, the
injected event delivered, and the context read answered Halted until the
driver's Reset Endpoint, which is then issued to the real endpoint from its
Stopped state. Withheld interrupts, a refused code, a halting event off the
queue, PED and PP cleared on a root port, HCH set and USBSTS all-ones each
get the same statement.

The expected outcomes are fault-specific and the legs check each:

| Fault | Transient (cleared before the budget) | Persistent |
|---|---|---|
| Withheld interrupt | events drained by the backstop, `BackstopDrains` counted | the same each poll; nothing else changes |
| Transaction Error | the TD completes after its retries | the error completes; the class driver's reset (4.10) |
| Refused code, halt with no TD | the endpoint recovered, the TD at the dequeue pointer failed | the endpoint terminal, shown |
| Root port PED or PP | the device re-enumerated | the port held, shown |
| HCH | one recovery | the rate bound reached, the controller latched failed |
| USBSTS all-ones | nothing (a transition) | contained: unreadable, I/O failed, no recovery |

And beside them: a cancel and an ABORT_PIPE racing a retry and a recovery;
a stream endpoint; teardown and a power transition during each; every
budget exhausted.

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

The release notes and README (`XhciIntervalCap`, the AMD report), record 13
(section 6's error contract, from 4.10), record 15 (the shared peek),
`implementation-invariants.md` ("Fatal Errors": the refused-code deviation
superseded, the all-ones containment added), `failure-diagnosis.md`,
`xhcisnap/README.md`, `xhciqual/hardware-testing.md`, `source-files.md` and
`runs/run-35.md` (roadmap 35.6).

## 8. Decisions taken (owner, 2026-10-06)

- The work joins Phase 35, and Phase 35's release is `2.2.0.0`.
- All the behaviours land together, not one at a time after a diagnostic
  reading.
- The interval cap is gated on every AMD controller (vendor `1022`), with
  the 0/1/2 override.
- The lost-interrupt backstop is this work's; polled delivery reuses its
  peek.

## 9. Open for review

The "to settle" entries of sections 4.8 and 4.9; the name `XhciIntervalCap`;
whether 4.3's survivors are resubmitted or failed, after 4.10's reading; the
containment interval of 4.6; and, for the owner, the withdrawal of the
Average TRB Length change (4.7), which was part of the seven behaviours
decided on 2026-10-06.

## 10. Codex round 1, and where each finding is answered

| Finding | Severity | Answered in |
|---|---|---|
| 1 Average TRB Length neither legal everywhere nor free | high | 4.7, withdrawn; section 2's rule names admission fields |
| 2 A refused code does not prove its TD safe to complete | high | 4.3; section 2's ownership clause |
| 3 "Fail the oldest" can fail an unrelated transfer | high | 4.4: live endpoint identity, stale events dropped, the dequeue pointer's TD, streams and EP0 |
| 4 Class-driver recovery assumed | high | 4.10; submissions to a Halted pipe kept queued (4.4) |
| 5 The backstop must use the ISR's lifetime protocol | high | 4.1 |
| 6 All-ones and the recovery budget | high | 4.6, 4.9 |
| 7 Root-port recovery not fully bounded | medium | 4.5, 4.9 |
| 8 Current-code and Linux descriptions wrong | medium | section 3, rows T3 and T4 |
| 9 The retry bound is not Linux's | medium | 4.2 |
| 10 Absent quirk flags do not explain Linux | medium | section 1, a hypothesis; the PROM21 route |
| 11 Injection and acceptance need fault-specific outcomes | medium | section 5 |
