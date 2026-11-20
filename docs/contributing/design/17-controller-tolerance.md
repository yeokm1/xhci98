# Controller tolerance

Design record for roadmap-hcd tasks 35-T.0 to 35-T.9 (Phase 35, release
`2.2.0.0`). Revision 7, 2026-10-06, written before code. Revisions 4, 5
and 6 are `9163f77`, `74d14e8` and `cc82d37`; each of revisions 1 to 7
answers the review of the one before it (section 10). Revision 0
(`60f8f64`) drafted the findings and the shape; revision 1 (`4a63a35`)
answered its review; revision 2 (`67a3f75`) rested the recoveries on
the machinery as it is; revision 3 (`e8d2c0e`)
replaced the in-place endpoint recovery, by the owner's decision, with a
device cycle - the replug the user does by hand, done by the bus (section
4.3) - and made the soft retry pause nothing (section 4.2). Revision 4
adds a clock of the driver's own (section 4.0), the
power gate taken explicitly (4.1, 4.6), the soft retry intercepted before
the engine's terminal mutations and generation-checked (4.2), the cycle
before a PDO exists (4.3), and containment that drains only after its proof
(4.6). Section 10 maps every point raised against revisions 0 to 6 to
where it is answered. Revision 7 (`edc7a2a`) was reviewed and revised on
2026-10-06 with nothing material remaining. Section 4.11, the off-switch
`XhciTolerance`, was added after it by the owner's decision of the same
day, and reviewed and revised in its turn (`d9b39c4`).

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
part was built and the whole branch had been reviewed and revised
(branch `2.2.0.0` at `2000fcc`): "As built" notes in sections
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
completes the transfers it holds, at every `XhciTolerance` value), with
its review's rules - no Save or Restore State on a latched-failed
controller, a resume refusing the spent run of failures, and a save gate
that reads the HCD's own transfers from submission to completion - and
section 4.11's note on what acts at 0 a line for each; section 5's PED row
says the layer answers PED clear on QEMU, which ignores the PED write, and
section 5 notes QEMU's dequeue left on the Link TRB.

Revision 12, 2026-10-11, for roadmap-hcd task 36.54, written before code:
section 4.12 brings stream endpoints into the soft retry, restarts a
stream ring at its base when a port reset rebuilds its endpoint, and adds a
quirk for QEMU's xHCI alone, which ignores a Stream Context's DCS bit; it
records the diagnosis of the UAS drive that stayed dead after a
Transaction Error on its data stream. Section 4.2's scope line points to
it.

Revision 13, 2026-10-13, for roadmap-hcd task 38.3: section 4.13, the
release of the transfers a pinned containment keeps, drafted, reviewed
once, and deferred by the owner the same day to after `2.4.0.0`
(PCR.1): no proof reachable on a disable establishes that the function
can no longer write to memory. The section records why, the owner's
answers and what a reopening must answer. Nothing else changes.

Revision 14, 2026-10-13, for roadmap-hcd task 38.11, written before code:
section 4.14 accepts controllers whose HCIVERSION is 0.96 behind
`XhciAccept096`, on by default, with the TD Size field in 0.96's units on
them (owner, 2026-10-13), and audits every other behaviour of xHCI 1.0 the
driver relies on against what 0.96 may lack. The owner's answers of the
same day to its seven questions are folded in (section 4.14.11), and the
section was reviewed and revised (an eighth question, D8, the isochronous
Frame ID below 1.0, answered (b) by the owner the same day). Nothing else changes.

Revision 15, 2026-11-09, for roadmap-usb4 task 48.32 (was PCR.1): section
4.13.7. The release the task asked for was designed, reviewed and
revised, and is not built by the owner's decisions of the same day: the
link above the controller read down proves no drain of writes already
forwarded, and D3hot is not built either. The stop's all-ones
fallback, an existing gap the review found, goes to task 48.33. Nothing
else changes.

Revision 16, 2026-11-09, for roadmap-usb4 task 48.33, a draft written
before code: section 4.13.8. With every transfer staged through buffers
the driver owns (design record 13 section 11.11), an unproven stop
completes its clients' requests and keeps only the driver's own memory;
the stop's all-ones fallback fails closed. The owner's questions are in
record 13. Nothing else changes.

Revision 17, 2026-11-11, for roadmap-usb4 task 49.16, with no code:
section 4.13.9. The staged path of section 4.13.8 audited against the
code, path by path, with what no leg has read; the lists of what must be
shown before `XhciDmaStaging` 1 could be chosen for a dock's controller
or be the default; and the intermediate choices, for the owner. No
default changes. Nothing else changes.

Revision 18, 2026-11-11, for roadmap-usb4 tasks 49.29 and 49.16: section
4.13.9. List 1 points at the instrumentation task 49.29 builds (record
13 section 11.11.12), and the command stranded at `XhciDmaStaging` 0 is
completed with an error, as the owner decided the same day. Nothing
else changes.

Revision 19, 2026-11-17, for roadmap-usb4 tasks 50.30 and 50.31, a
design written before code: section 4.15. A command or a thread control
transfer that times out is aborted, and where the abort ends cleanly only
that device fails and its place is held; every reset request records its
reason, place and slot; a controller latched failed is tried again after
`XhciFailedRetrySec` seconds, backing off, and at once on a root-port
change. Section 4.9's table gains its rows; sections 4.6 and 5 point to
it. Nothing else changes.

Revision 20, 2026-11-17, the same day, before code: section 4.15 revised
for the owner's answers of that day (section 4.15.0) and a review of
revision 19: the new handling is chosen by `XhciDeviceIsolation` (0
today's handling, 1 the default on controllers listed after a reading on
real hardware, 2 every controller), a hub that itself stops answering
keeps the reset, the spread of failures is recorded and not acted on; and
the corrections - leaving the command wait for a stop, a removal or a
power transition, who owns what after an abort, the emulated abort
sequence, the recovery status as a request of its own, a reason for a
failed resume, and the tries' storm limit and resume admission.

Revision 21, 2026-11-17, the same day, before the code of steps 3 to 5
(steps 1 and 2 may be built on revision 20's text): section 4.15
reviewed and revised again. The earlier waits a stop, a removal or a
power transition meets before the stop body, each route named, and what
a left command owns until it is settled, across a suspend and a
root-hub removal included (4.15.3); the ownership audit finished caller
by caller, the failed device frozen, the mark never suppressing a
mandatory reset, every endpoint checked before Disable Slot, and the
quarantine's end (4.15.5a); hold 4 read before link recovery (4.15.4);
the reason table matched to every site the code has, the engine's
staged in the drain (4.15.8); the saved aborted address's lifetime, the
emulated "already fetched" case kept clear of the engine's own guard,
code `2B` (Reset Device), and code `26` armed by the device's own
unanswered request, so that the field report's sequence is emulated end
to end (4.15.9); six mandatory bench observations plus a conditional
seventh (4.15.10); the tries' storm history, the message-interrupt
mode's resume and the stated bound (4.15.13); and the narrower
statement of what the default changes (4.15.14).

Revision 22, 2026-11-17, the same day, after steps 1 and 2 were built
and reviewed and before the code of step 3: the ownership audit searched
again from the code and completed - the bounded Stop Endpoint and its
four callers, the read-back's resend, RESET_PORT's quiet, the late path's
Stop, both commands of the readdress, the Error exit's command corrected
to Set TR Dequeue, and the emulator's own commands' exemption (4.15.5a);
who settles a left command that names no device, a left Enable Slot
above all, when no controller stop or HCRST follows (4.15.3); and the
emulator's scope and lifecycle claims aligned with what step 2 built -
`26` at a root port only, the withholding bound to the command placed,
the layer's addresses dropped at every invalidation and rebuild, and what
step 2 leaves to step 3 (4.15.9).

Revision 23, 2026-11-17, the same day, after a review of revision 22 and
before the code of step 3: a left command's settlement given a state of
its own (`SETTLING`), in which the controller thread's settlement alone
may submit - a Disable Slot of the slot it settles and the Stop Endpoints
an owed record's teardown needs - while every other submission and
every input context stays refused (4.15.3); a left Enable Slot's debt
kept across a successful Restore State, which rebuilds the command ring
but disables no slot, and settled after it, by the returned slot's
Disable Slot where a trusted completion gave one, by an HCRST otherwise;
the returned Slot ID checked for range and ownership before that Disable
Slot (4.15.3); and the recovery entries' audit stated as narrowly as the
code is - one direct assignment gains a reason, the rest keep their own
records (4.15.8).

Revision 24, 2026-11-18, after step 3's code was reviewed and before its
guest legs: a settlement command that a route's bit interrupts is treated
as a settlement command that failed - the debt cleared, the reset
requested, Enable Slot held - where revision 23 left the debt `SETTLING`
for the stop's teardown or a recovery's HCRST (4.15.3; decided, owner,
2026-11-18); and `XhciDeviceIsolation`'s reading of an absent, mistyped
or out-of-range value as `1` confirmed (4.15.11; owner, 2026-11-18).

Revision 25, 2026-11-18, after step 3's guest legs (`runs/run-50.md`,
"50.30 step 3 (device isolation): the guest legs", read under emulation)
and before any code of it: section 4.15.16, three amendments and one
disposition. A root port's hold is observed, and released where the
sample shows the stable disconnect, before that sample's actions and
connect are suppressed (the run sheet's F1); an Enable Slot that a debt
or the Enable Slot hold refuses is a deferred enumeration, kept as an
obligation and resumed after a clean settlement, at root ports and hub
ports alike (its F4); the qemu flavour's one-shot recovery-command
withholding for bit 7 of codes `24` and `25` (its F3); and its F2 read
as an emulator artefact, with no driver change. Sections 4.15.3,
4.15.4 and 4.15.9 point to it. No registry value is added.

Revision 26, 2026-11-18, the same day, after a review of revision 25:
part A of section 4.15.16 cleared and built; parts B, C and D revised
before any of their code. D: F2's captured transition is the
emulator's, but an enable change is sticky on real hubs, so the hub
path checks share one predicate of absence and an enable change alone
on a connected, enabled port no longer reads as a departure. B: the
deferred enumeration is reconsidered whenever admission opens again,
the successful Restore State's clearing included, under one lock, only
while the port still owns its deferred failure, cancelled by an
accepted change, through the ordinary inspection and its gates; hub
ports' refusal handling owed in a guest. C: DCI 1 named, the lock
order stated, an asynchronous cycle mark for the second scratch-taint
occurrence, a rendezvous for the window after a failed settlement, and
the failed settlement's notes as the code writes them. No registry
value is added.

Revision 27, 2026-11-18, the same day, after a review of revision 26,
docs only: section 4.15.16 C's vehicle for the second scratch-taint
occurrence no longer adds a timer and DPC; the controller thread's own
EP0 wait loop observes an armed mark deadline at each look and makes
the mark itself, so no new callback, and no callback lifetime, exists.
Nothing else changes.

Revision 28, 2026-11-18, the same day, docs only, written before code:
section 4.15.16 gains E, the query-stop under a debt driven by a
qemu-flavour hook (code `2D`, a work item with its admission, count,
events and drain, no new import), as the owner decided the same day; F,
a frozen device's cancel left to its teardown as section 4.15.5a
already says; and G, leg 13 read with outcome 1 for step 3's
acceptance, outcome 5 latching by design. Section 4.15.9's leg 13
points to G.

Revision 29, 2026-11-19, with code: section 4.15.16 B's obligation
(the flag, its generations and the count) moved into pure functions of
`src\xhci_enum.c` that the driver calls, behaviour unchanged, with host
vectors for the whole protocol and for both clearings; B's "As built"
says what the vectors hold and what stays owed in a guest. Nothing else
changes.

Revision 30, 2026-11-19, with code: section 4.15.16 C and F built as
written (their "As built" notes); no design changes.

Revision 31, 2026-11-19, docs only, written before code: section
4.15.16 E revised. The hook makes no IRP and queues no work item: the
controller thread calls the driver's own query and cancel
(`HcdMsg4QueryStop`, `HcdMsg4CancelStop`) directly, as decided that day
on the owner's behalf, and every reading is a driver-path exercise. Each
call is committed atomically under the controller and injection locks;
every real PnP minor that could overlap it (START included) and every
power request, for the whole of the driver's own handling of it, enters
a bracket that ends the
exercise or waits for the thread to end it - by the cancel for a request
that is not terminal, by a hand-off with no reconnection for a stop or a
removal. The test's debt is formed by the withheld command itself: with
argument bit 7 the withholding raises a qemu-only route bit,
`HCD_LEAVE_QTEST`, which then keeps the settlement from running while the
thread serves the peers.

Revision 32, 2026-11-19, with code: section 4.15.16 E built as revision
31 designs it (its "As built" note); no design change.

Revision 33, 2026-11-19, docs only, after the guest legs on the final
image: the "As built" notes of section 4.15.16 B, C, E and F say what
those legs read and what stays owed; C's notes that its recorded reason
is the abort's `CMD_KEPT` where its legs name `DISABLE_SLOT`. No design
change.

Revision 34, 2026-11-19, written before code: section 4.15.16 C's leg 6
redesigned, as the owner decided that day (given through the
coordinator), in place of listing it as owed. What kept it out of reach
was the arrangement, not the point: while a recovery is owed the
controller reads failed and the thread's cancel service does not run,
so a class driver that cancels its transfers before its device power
IRP (HID's) holds a sleep's children until the recovery has run, and the
controller's own power IRPs only follow it. With no such transfer the
controller's power IRPs came inside the hold, and the point still let
go only at its bound: a power transition's route bit stands for a few
milliseconds and the point sampled it every 100 ms. So, in the qemu
flavour alone, `HcdEnumLeaveRaise` also latches each bit it raises into
an injection field that point 3 reads; the leg is run with hubs and the
failed target alone. Legs 2 and 3 name `CMD_KEPT`, the reason the
driver records, by the owner's decision of the same day (given through
the coordinator). No registry value is added.

Revision 35, 2026-11-19, with code: section 4.15.16 C's leg 6 redesign
built as revision 34 designs it (its "As built" note); no design change.

Revision 36, 2026-11-19, docs only, after the guest legs on the image of
`ea5eadc5a`: the leg 6 redesign's "As built" note says what they read.
No design change.

Revision 37, 2026-11-19, written before code, for a finding of task
50.30 step 3's code reading: section 4.6 gains a note that a run of
failed recoveries below its bound ends at a resume whose
reinitialization succeeded, as it ends at a recovery that succeeds
(`RecoveryFailuresConsecutive` 0). Until now the count outlived such a
resume, and a later, unrelated fault got one or two recoveries fewer
than the run allows. No registry value is added.

Revision 38, 2026-11-20, with code: the owner's decision of 2026-11-20
lists the Intel 7 Series xHCI, `8086:1E31`, the field report's
controller of section 4.15.1, in section 4.15.11's table without the
reading on real hardware that section 4.15.10 makes the entry
criterion. Section 4.15.10 records the rule as amended for that one
controller; sections 4.15.0, 4.15.1, 4.15.9, 4.15.11 and 4.15.14 say
what follows from it. No registry value is added.

Revision 39, 2026-11-20, with code: by the owner's decision of the same
day Setup's and the USB Tree's `XhciDeviceIsolation` row names the
listed controller (section 4.15.11, after the tuning draft). No
behaviour changes.

Revision 40, 2026-11-20, with code, for roadmap-usb4 task 50.38: section
5 gains "The trigger reader". On 98 SE the controller thread was read
twice, live, inside the qemu flavour's once-a-second read of
`XhciQemuInject` and never returned from it, with a cancel waiting on
the thread; the read is moved to a thread of its own in the qemu flavour,
and the controller thread takes its readings. The release flavour is not
changed. No registry value is added beyond the qemu flavour's forcing
value `XhciQemuThreadRefReader`.

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

**Amended for `2.3.0.0` (roadmap-hcd 36.55): the step runs in the
tolerance clock's DPC, not on the controller thread.** Windows 98 SE
stopped under it. The run was the `qemu` flavour, `XhciInterruptMode` 1,
the dead line of record 15 section 11, and a Bulk-Only copy from an
MS-DOS prompt (`runs/run-36.md`, "36.55"). The controller thread stopped
being scheduled while the copy's request waited, and it never ran again.
This happened twice with the thread in two different places: once in its
loop's 100 ms timed wait, and once between two steps of a pass, runnable
code that waits on nothing. Through both stops the tolerance clock's
timer DPC kept running every 100 ms. The power gate was free, and an
event stood at the dequeue. On a delivery where the backstop is the only
drain, the request waited for a completion that only the unscheduled
thread would queue. That is a deadlock. What in the VMM holds the thread
off was not identified. Every instruction sampled was in the VMM's ring
0, none in this driver. What was read is that a DPC still runs where the
thread does not.

It is not confined to mode 1 or to the qemu flavour's dead line. In the
default mode 0, on a working line, the qemu flavour's lost-interrupt
injection (section 5, code 01) was made during the same copy. The thread
stopped at the same place mid-pass after it had delivered 117 of the lost
interrupts, and the copy stopped with an event pending. One lost
interrupt during a copy to such a drive is enough on Windows 98 SE. Every
release with this backstop, `2.2.0.0` onwards, ran it on the thread
(`v2.2.0.0`'s `hcd_ctl.c`, `hcdPoll`), so every one has the exposure.
Releases before `2.2.0.0` had no backstop, and there a lost interrupt
waited for the next interrupt. The trigger, as read, needs a drive in
MS-DOS compatibility mode: the BIOS-mapped view of a stick attached at
power-on (the run record's E:). The same stick's protected-mode letter
(G:) copied and compared clean under mode 1 on the dead line, 3661
transfers, with the thread running throughout. The step therefore moves
to the place that was seen to run:

- **Where.** `hcdTolDpc`, first, before it takes `TimerLock` to count the
  tick and arm the timer again. The DPC never holds both locks. The order
  follows the frame sampler's DPC: controller work first, re-arm last.
  `HcdTimersDrain` waits for the re-arm or its refusal, so once it returns
  nothing of this DPC still touches the controller. The step runs on every
  tick, so the observation is looked at every 100 ms. Before, it was
  looked at on each thread pass, at least 100 ms apart and later whenever
  the thread was busy.
- **What.** It is the same step. Under the controller lock it checks
  `hcdDrainAdmitted`, which reads `HcInfoStatus`, `INITIALIZED`, the
  failure, D0, `SuspendedInD0`, `DpcClosed` and a ring that exists. It
  peeks at the dequeue and updates the one observation. When the interval
  has passed, it queues `IsrDpc` through the ISR's admission and counts
  `BackstopDrains` under the lock. `XhciTolerance` 0 still turns it off,
  and the qemu flavour's start hold (task 36.4) still holds it back.
- **No power gate.** The gate is a wait, and a DPC cannot take it. The
  step does not need it. What the step queues is the ISR's own DPC, and
  that drain admits itself under the controller lock (`ControllerFailed`,
  `INITIALIZED`, a poll delivery's service). An interrupt can already
  queue it at any point of a power transition or a recovery, and the
  drain has never relied on the gate for that (record 15 section 2). The
  admission the step reads is a filter, not the safety. A stale read
  costs one DPC that drains nothing. Record 15 section 7.1's peek in the
  frame timer's DPC already queues the drain from a timer DPC, with no
  gate, under a poll delivery.
- **Bounded.** One peek and at most one insertion per tick. The drain it
  queues is bounded by `XHCI_DPC_MAX_EVENTS`, as every drain is. The step
  never drains inline, for section 7.1 of record 15's reason: a drain
  completes IRPs, and a completion routine resubmits.
- **The thread no longer takes it, and no thread fallback is kept.** The
  interval is measured on the tolerance clock, and only this DPC advances
  that clock. A thread step therefore could never queue a drain at a time
  the DPC was not running. While the DPC runs, it takes the step on every
  tick, so a thread step would only repeat the same observation under the
  same lock. The thread's own waits keep their faster drains:
  `hcdThreadWaitDrain` (task 36.13) and `HcdCommandDrain` (task 36.4)
  queue a drain between the slices of a command or EP0 wait as soon as an
  event stands, without waiting out the interval.
- **The release.** The ISR is disconnected and the thread stopped before
  `hcdRelease` closes the DPC. The tolerance clock's DPC, however, runs
  until `HcdTimersDrain`, which comes after the close. It is a producer
  racing the close on another processor. The pairing is made explicit:
  the producer counts `DpcsInFlight` with an interlocked increment and
  then reads `DpcClosed`. The release raises `DpcClosed` with an
  interlocked exchange and then waits `DpcsInFlight` out. Both sides
  order their write before their read, so either the producer sees the
  close and rolls its count back, or the release sees the count and waits
  for the DPC. The DPC then runs closed and only retires its count.
- **Timing.** A lost interrupt is drained at the first tick after the
  fourth since it was seen, about 300 to 400 ms at the usual clock rates,
  plus the DPC's own latency. The thread's pacing no longer adds to it.
  Under mode 1 on a dead line, every completion comes at that pace.

Moving the step off the thread revises this section's first paragraph,
the owner's design of 2026-10-06 ("a step of its own on each
controller-thread pass"). Orchestrator decision, owner confirmed 2026-10-11.

**Amended for 37.4 (roadmap-hcd 37.4): the event ring check after the
re-arm.** Phase 36's matrix counted `BackstopDrains` in every NT 6
snapshot under `msi=off,msix=on` (Windows 7 3 to 20 a session, Vista 115
to 288) and 0 under MSI, the line and polling (`runs/run-36.md`, "MSI-X
BackstopDrains (NT6)"). The cause read from QEMU's source and the trace is
QEMU's: qemu-xhci un-uses and re-uses the MSI-X vector on every IMAN.IE
toggle and raises the interrupt before it marks the vector used again, and
`msix_notify` returns on an unused vector before its mask and pending-bit
check. An event written while the ISR's IE=0 stood is therefore never
signalled, and it waits for this backstop. On a compliant controller IP
set while IE was clear is delivered once IE is back (xHCI 1.2c 5.5.2.1),
so `2.3.0.0` shipped unchanged.

The check closes the wait without touching the interrupter:

- **Where.** `hcdIsrDpc`, after `XhciEventDpc` returns, on an interrupt
  delivery (not `XHCI_POLL_DELIVERY_POLL`, whose drain is followed by
  record 15 section 7.1's peek instead). `hcdRearmPeek`.
- **What.** Under the controller lock: `hcdDrainAdmitted`, the re-arm's
  own condition (`XHCI_EXT_FLAG_INTERRUPTS`, so a drain usbport-style
  disabled interrupts behind queues nothing), and `XhciEventRingPending`
  at the dequeue. An event there is counted in `RearmPeekPending`, and
  `IsrDpc` is queued through the ISR's admission (`hcdQueueDrain`'s), counted
  in `RearmPeekDrains` when the insertion was taken. The qemu flavour's
  start hold (task 36.4) holds it back as it does the backstop.
- **No register.** It writes nothing: ERDP, EHB and IE stay the drain's,
  and the ISR's IE=0 (record 05, "The DIRQL exception") is untouched. This
  is the option a review preferred (2026-10-11) over re-arming before the final
  ERDP write, which would let the interrupter fire into a pass in progress,
  or a blind second ERDP write.
- **Bounded, never a chain.** One peek and at most one insertion per
  interrupt-queued drain. The insertion passes `HCD_DRAIN_FROM_PEEK` as
  `SystemArgument1` (every other insertion passes NULL), and a drain that
  carries it does not peek, so a further drain waits for an interrupt,
  which moderation paces. Review (2026-10-12) showed that the first
  draft's re-peeking drain could chain DPCs back to back under sustained
  completions, holding a processor at DISPATCH_LEVEL and starving the
  controller thread. An event stranded in the second drain's own window
  waits for the backstop, as before 37.4. `KeInsertQueueDpc` keeps the
  arguments of whichever insertion queued the DPC and refuses the other.
  When an interrupt queued it first, the peek's insertion is refused and
  costs nothing, and that drain runs with NULL and peeks. When the peek's
  insertion won, an interrupt's is refused and the drain runs with the
  marker and does not peek; that loses only a peek, since the drain itself
  takes what the interrupt signalled.
- **Cost on a compliant controller.** One cached memory read per drain.
  An event that arrives between the re-arm and the peek is drained twice
  over: by the DPC this queues and by the interrupt it also raises, the
  second finding the ring empty, the cost the backstop already accepts
  (above).
- **Release.** The insertion repeats `hcdQueueDrain`'s pairing with
  `DpcClosed`, the one the backstop uses; the caller is itself counted in
  `DpcsInFlight`.

Expected: `BackstopDrains` 0 under MSI-X on Vista and 7 with
`RearmPeekDrains` carrying the events the backstop used to, and the line
and MSI rows unchanged in function and timing (their peek counters count
only events that arrived in the window, each also interrupted). Neither counter is
gated by `XhciTolerance`: the check is part of the drain, not a tolerance
behaviour. Orchestrator design, owner to confirm.

### 4.2 The soft retry (35-T.2, unconditional but for an exclusion list)

**Scope.** A USB Transaction Error on a bulk or interrupt endpoint that has
no streams, whose device is not behind a TT, on a controller not on Linux's
`XHCI_NO_SOFT_RETRY` list (`1022:43B9`, `1022:43BB`, `1B6F:7023`,
`1B6F:7052`). EP0, isochronous and stream endpoints keep today's handling.
(Since revision 12 a stream's own queue is in scope too: section 4.12.)

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

**As built (35-T.2, `4ab1663`, revised in `6c0ecc1`, `35f77b6` and
`8fd1f49`; revision 9).** The scope is set once per
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
place. Every in-place form needs to attribute the
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

**As built (35-T.3/4, `0e1cb42` and `c0f429c`, revised in `18f383f`,
`bdc93c0` and `adbc4e3`, with `d69814f` meeting 35-T.2; revision 9).** The producer is `hcd_dev.c`'s `hcdTolCycleEvent`
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

**As built (35-T.5, `778b730`, revised in `4a9010d`, `82d87fe` and
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

Revision 9 adds one rule taken after 35-T.5 was built (`cd8f744`):
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
The window is reset at start. (Task 50.30, section 4.15: where
`XhciDeviceIsolation` applies, a device that stops answering no longer
requests a recovery when its abort ends cleanly, so a cleanly isolated
failure no longer spends this window; task 50.31, section 4.15.13:
a latched controller is tried again, each try starting a fresh window.)

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

**As built (revision 9).** Three rules changed after 35-T.6 was built:

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

HCH proves the controller stopped executing, not that it will write
nothing more: Save State (CSS) writes the cached Slot, Endpoint and Stream
Contexts to memory while halted (xHCI 5.4.1), and after a release the
slots stay enabled over buffers given back. So no CSS or CRS is issued on a
latched-failed controller (`XhciTolSaveRefused`, in `xhciSaveState` and
`xhciRestoreState`), and the release discards any saved image; only the
HCRST of a recovery or a start, which clears the latch, retires the slots.

The release is once per lifetime, so the terminal it answers must last the
lifetime too. A resume refused the containments and the window already;
it now refuses the spent run of failures as well (`XhciTolResumeRefused`
in `hcdResumeContained`), at either `XhciTolerance` value. Before, a
resume through reinitialization cleared `ControllerFailed` while
`RecoveryFailuresConsecutive` stayed spent and the release stayed taken,
so after re-enumeration a later fault got neither a recovery nor a
release, and the hang came back. This is a change from `2.1.1.0` at 0,
where such a resume brought the controller back: now only a stop and a
start do, as section 4.6 says of the window.

**A run of failures below its bound ends at a resume that
reinitializes** (revision 37, from task 50.30 step 3's code reading).
The paragraph above refuses a resume only at a terminal. With one or two
failures in a row and the next recovery still owed - paced
(`XhciTolRecoverPaced`), deferred, or not yet reached by the thread - the
resume is admitted and, since a latched-failed controller is never saved
(`XhciTolSaveRefused`), it reinitializes. When that succeeds,
`XhciInitController` has cleared `ControllerFailed` and the controller
runs again, reinitialized, as after a recovery that succeeds; but
`RecoveryFailuresConsecutive` was cleared only by a recovery that
succeeds, so it outlived the resume. A later, unrelated fault then began
its run with those failures already counted, and got two recoveries or
one, not three, before the controller latched stopped (the FAILURES
terminal). The same in `3.5.0.0`. Record 07's rule decides it (section
7: a refusal increments the count, a success clears it; a budget spent
on a controller another path has already brought back is Finding T's
expiry-date defect): the run ends there too. So `hcdResume` sets the
count to 0 once `XhciResumeController` has succeeded through a
reinitialization (`ResumeReinits` advanced; the pure
`XhciTolResumeFailures`), at every `XhciTolerance` value, and notes
`ctrl.run.resumed` with the count it ended when that was not 0.
Unchanged: a resume that restored (never one of a latched controller) or
found nothing programmed (`HcInfoStatus` not good, `ControllerFailed`
left set) keeps the count; a resume that fails is charged nothing and
requests the recovery (`HcdControllerFail`), so its run goes on; a
resume refused at a terminal changes nothing; under `XhciInterruptMode`
4, a resume whose re-arm failed still reinitializes and then latches the
lost message (`hcdResumeMsg4`, after `hcdResume`), a terminal no count
reaches, as `hcdRecover` latches it after a recovery that succeeded and
cleared the count; the window keeps its
stamps, so at `XhciTolerance` 1 a fault inside the same ten minutes
still meets it, as one after a recovery that succeeded does; and a
request the resume served stays set, harmless (the comment at
`hcdRecover`'s gate).

The save gate this rests on did not hold. `xhciSaveState` declines while a
device has work outstanding, but it asked the frozen miniport's device
table (`ext->Devices`), which the HCD never fills, so it passed with
transfers queued; on a Force Save Context controller a suspend could then
save over busy endpoints, and a completion written while halted was
dropped at the restore by `XhciEventDiscardStale`, its URB left pending.
Pre-existing. The gate now asks the HCD's own state under the controller
lock (`XhciSlotSaveBusy`, `hcd_dev.c`): a pipe is busy - every endpoint,
EP0 and each open stream - while any of its transfer records is in a
state but free (mapping, on the ring, held, or retired and not yet
completed), an IRP waits for a record, a record is held, or its queue
holds a TD, so a transfer counts from its submission to its completion.
A declined save sends the resume to the reinitialization, whose
invalidation completes the transfers. What passes stays nonblocking: an
idle gate arms a guard (`SavePublished`) in the same hold of the lock; a
TD `HcdIoMapped` publishes on a controller not initialized sets it and
clears `SavedStateValid` (the pure `XhciTolSavePublish`); the commit after
CSS sets `SavedStateValid` only if nothing was published
(`XhciSlotSaveCommit`, `XhciTolSaveCommit`); and after a restore has run
the controller, `XhciSlotSaveSpoiled` sends the resume to the
reinitialization when anything was published since the save, including
between CRS and Run/Stop. A bus with nothing outstanding saves as before;
a controller without Force Save Context, every virtual machine's, declines
earlier anyway. At every `XhciTolerance` value.

### 4.6.1 A reset that outlasts the recovery's stall (task 49.35)

**The reading.** A field report on `3.4.3.0` (an ASMedia ASM1042,
`1B21:1042` revision 00, HCIVERSION 0.96, Windows XP x64): right after the
start an escalation a release build did not record raised the first reset
request (the note ring reads `iso.none`, `ctrl.failed.here=1`,
`door.alias`). The in-place recovery ran `XhciInitController` with
`InitBelowPassive` set, wrote HCRST, and the wait for `USBCMD.HCRST` to
clear ran out after its 1 ms pause and 10 ms stall: refused at
`XHCI_INIT_STEP_RESET` with USBSTS `00000001`. The next two attempts began
on the next controller-thread passes - `XhciSlotInvalidateAll` wakes the
thread, and `hcdRecover` asked again at once - found HCRST still set in
`XhciRecoverController`'s pre-check and refused, each charged. All three
began inside one tolerance tick, and the controller stayed latched. A
second report shows the third attempt finding the reset finished, writing a
fresh HCRST and failing the same way: this part's reset takes more than
about 11 ms and well under a second. The recovery's own text assumed the
extra time would come from "the next health poll"; nothing spaced the
attempts, and each wrote a new HCRST.

**The split.** `XhciRecoverController` keeps its DISPATCH_LEVEL contract
and every stall its length. What it cannot finish it hands back uncharged
(`XHCI_RECOVER_RESET_BUSY`: its pre-check found a reset running, or it
refused at the reset with one running; `RecoverResetOwed` says the HCRST was
this driver's), and `hcdRecover`, on the controller thread at
PASSIVE_LEVEL with the power gate held, waits for it - USBCMD.HCRST and
USBSTS.CNR read every 10 ms for up to `XHCI_TOL_RESET_SETTLE_MS` (1000, the
start's own bound), through the pure `XhciTolResetSettle`, an all-ones read
ending it. Settled with a reset owed, the reset is proven
(`RecoverResetProven`) for the one call of the recovery that follows, whose
`xhciReset` adopts it instead of writing another (`reset.adopted`): between
the proof and that point the rerun writes only the quiesce's interrupt
masks (USBCMD.INTE and IMAN, legal once CNR is 0, 5.4.2) and the xECP
handoff, and finds HCH set, none of which needs a reset after it. A
refusal at `XHCI_INIT_STEP_BUS_MASTER_RESTORE` with the bit this driver
cleared still to put back is handed back the same way
(`XHCI_RECOVER_BME_RESTORE`); until 49.35 it was charged and then run
again, so one recovery could cost two. One loop in `hcdRecover` finishes
both, at most three times, the bit put back before the rerun (a settled
reset leaves the controller halted and reset, `hcdRestoreBusMaster`'s
precondition), the window charged once as before. An attempt still not
finished - a reset not settled in the bound, or the restore refused - is
charged once (`XhciRecoverCharge`).

**The pacing.** After any attempt that did not recover, the next is not
begun before `XHCI_TOL_RECOVER_GAP_TICKS` (`XHCI_TOL_TICKS(500)`, 13 ticks:
at least 540 ms, about 1.3 s nominal) have passed since its stamp
(`RecoveryFailTick`, the pure `XhciTolRecoverPaced`); the request stays
owed and the thread's 100 ms timeout brings it back. The latch is
unchanged - three consecutive failures, the window - and a controller that
never comes back now reaches it after three bounded waits and two gaps,
about six seconds nominally, instead of inside one tick. At every
`XhciTolerance` value: the clock runs at 0 too.

**With mode 4 (task 49.24).** A continuation re-reads mode 4's admission
(`Msg4Barred`, `Msg4Lost`) under the controller lock after each settle and
stops before any restore or rerun (`ctrl.recover.msg4` 1); and a success
is not taken while a loss is latched - the brake's DPC can set it while a
call runs, on another processor or during a configuration IRP - the
enables masked and `ControllerFailed` set again, uncharged
(`ctrl.recover.msg4` 2). That closes the same window for the first call.

**What is recorded.** Notes `ctrl.recover.resetbusy` (USBSTS),
`ctrl.recover.settled` (ms waited), `ctrl.recover.unsettled` (USBCMD),
`ctrl.recover.bme`, `reset.adopted`; and, at the escalations a release
build did not record until 49.35, each bounded to the first
`XHCI_NOTE_ESCALATIONS` (4) of a start: `evt.hc` (a Host Controller Event's
completion code), `evt.rearm.failed` (IMAN), `intr.unmask.failed`,
`intr.mask.unproven`, `cmd.diverged` (the reason in bits 31:28),
`cmd.fatal` (the code), `cmd.abort.stuck` (CRCR), `cmd.age.reset`,
`ctrl.fatal` (USBSTS); and `dma.proof` (the `XHCI_TOL_PROOF_*` verdict of
every Bus Master Enable proof, the successful one that clears the bit
included). The counters of section 4.8 carry the same at words 34 to 45.
The Host Controller Event's escalation itself is unchanged.

**Elsewhere below PASSIVE_LEVEL.** `InitBelowPassive` is set by the
in-place recovery alone. The resume's reinitialization
(`XhciResumeController` from `hcd_power.c`) and the start run at
PASSIVE_LEVEL with the 1000 ms bound; the all-ones containment writes no
HCRST; Windows 98 SE and ME run the same thread and paths.

**Tests.** `test_tol`: `XhciTolResetSettle` against a fake controller that
holds HCRST, and separately CNR, for 0, 5, 50, 500 and 1500 ms and for
ever; the pacing; and a model of `hcdRecover`'s loop on the same fake - 0
and 5 ms recover with no wait, 50 and 500 ms after one settle with one
HCRST written, 1500 ms and for ever reach the latch after three charged
failures at least the gap apart. The qemu flavour's faults 1B (every HCRST
held `arg` x 10 ms, HCRST or CNR) and 1C (one Host Controller Event) are in
section 5.

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
the uncapped one. Mode 4 (task 36.19, record 13 section 13.7) is the
exception and comes before the cap: keyed on the caller's `bInterval` (128
to 130 under task 36.19, programming Interval 2 to 0; 128 to 143 since task
42-A.18, programming Interval code - 128), it programs an Interval the cap
leaves when it is 8 or less, so a capped controller does not silence a fast
code, and lowers to 8 when it is above, as any endpoint's; its fallback is
Interval 3, which the cap leaves too. An endpoint mode 4 does not change
goes through the cap as above. Host vectors cover each combination of the cap's three values with
fast polling's modes, mode 4 included, and the fallback.

**Task 38.10 (design record 20 sections 4.9, 4.10 and 13).** The cap's
value 1 also covers 1002:7316, the ATI Navi10's USB-C controller, the one
id of Linux's `XHCI_LIMIT_ENDPOINT_INTERVAL_9` outside vendor 1022
(`XhciTolCapApplies`); its threshold, its endpoint types and its 0 and 2
are unchanged. Beside it, `XhciTiIntervalCap` takes the same shape for TI's
TUSB73x0 (104C:8241, Linux's `XHCI_LIMIT_ENDPOINT_INTERVAL_7`): an
interrupt endpoint of a High-Speed or faster device whose Interval is 7 or
more is programmed at 6, isochronous endpoints never (owner, 2026-10-13).
It is applied after this cap (`XhciTolTiCapInterval` in `hcdCfgTolerance`),
as Linux orders the two, so the ordering with fast polling above carries
over: eligibility is still judged on the uncapped Interval.

**The Average TRB Length switch** (owner, 2026-10-06). Revision 0 proposed
taking the Average TRB Length of a periodic endpoint as its Max ESIT
Payload, as Linux programs it; but the field enters the
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

**As built (35-T.8, `dd6bd3c` and `df4045c`, revised in `39066c0`;
revision 9).** The queues' counts are published as
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
Since task 49.35 the counter block has 46 named words before the
histogram, not 34: `ResetSettles`, `ResetSettleMsMax`, `ResetUnsettled`,
`ResetBusy`, `RecoverPaced`, `RecoverReruns`, `HcEvents`, `HcEventCode`, `RearmFails`,
`CmdDiverged`, `CmdFatal` and `MaskFails` (section 4.6.1), shown in a
plain Save Report's per-controller section; XHCITR still decodes a driver
that serves 34, and an older XHCITR prints the new words by number.
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
| 4.15 a device failed alone (task 50.30, where `XhciDeviceIsolation` applies) | the place | the 3 re-enumerations above (each cycle); an enumeration failed unanswered is held at once | as 4.3 | the place held, reason 4 (powered), shown | a controller start |
| 4.15 the spread records (task 50.30) | the controller | none: recorded, never acted on | - | - | a controller start |
| 4.15.13 the tries (task 50.31) | the controller | from `XhciFailedRetrySec`, doubling to 600 s | 10 minutes with no reset request | none: unlimited, backed off | a controller start |

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
whenever one reached it, and which a resume no longer clears. With it go
two rules on Save and Restore State, neither tolerance (section 4.6's
revision-11 notes): no CSS or CRS on a latched-failed controller, and a
save gate that counts every transfer from submission to completion and
spoils a save a late mapping publishes into. At 0 the refused resume of
a spent run of failures is a change from `2.1.1.0`, where that resume
brought the controller back.

### 4.12 Stream endpoints (36.54, revision 12)

Section 4.2 left stream endpoints out of the soft retry. Phase 36's matrix
then found a UAS drive that never moved data again after an injected
Transaction Error on a data stream (XP x64, the `qemu` flavour, fault 16;
2 runs of 3 stuck until the device was removed and added again, and the
36.22 stage-1 build the same), while a Bulk-Only stick under the same fault
was recovered by the soft retry. The same matrix recorded a stuck device
after fault 17 (a short packet) and after fault 15 (a stop) at 2 MiB
commands, each with `xhciuas` showing no timeout, and on Windows 98 SE a
stuck device whose `XHCISNAP` run hung. This section is the diagnosis
(4.12.1, 4.12.6, 4.12.7) and its three answers: the soft retry on a stream
(4.12.2), stream rings that restart at their base when a port reset
rebuilds their endpoint (4.12.3), and a quirk for QEMU's xHCI alone
(4.12.4).

#### 4.12.1 What the traces show

The record of 4.2 implied that a stream's failure waits for `xhciuas`'s
class timeout. It does not: a data transfer that fails starts `xhciuas`'s
recovery at once (`uas_xport.c`, `uasXferDone` marks the slot Failed, and
the recovery worker resets the device), so the RESET_PORT follows the fault
within a second. That reset is what then fails to converge, and only on
QEMU.

QEMU's xHCI (`hw/usb/hcd-xhci.c`, read at QEMU 11.1.0) ignores a Stream
Context's DCS bit. `xhci_find_stream`, the first time a stream is used
after its endpoint was configured, stopped or reset (a fresh endpoint
context, or `xhci_reset_streams`, sets every stream's `sct` to -1), reads
the Stream Context and initialises the stream's ring with `xhci_ring_init`,
which sets the consumer cycle state to 1 and never reads bit 0 of the
context. The Endpoint Context's own path reads its DCS, and a Set TR
Dequeue Pointer for a stream takes the DCS it is given, for that stream,
until the next stop or reset. A stream whose ring the driver has wrapped an
odd number of times, so that its producer cycle is 0, is then invisible to
QEMU: a doorbell for it gives `usb_xhci_ep_kick` and no transfer, and no
event. QEMU's kick then writes its own CCS back into the Stream Context.

Traced on XP x64 (QEMU's own trace events, and the guest's memory read
through the monitor; development host A): after the RESET_PORT the driver
had written every Stream Context with the DCS of its ring (0 where the ring
was on a cycle-0 lap), every data-in and status stream on such a lap stayed
dead, and the contexts read DCS 1 over TRBs whose cycle bit was 0. Each
later timeout, ABORT TASK, LOGICAL UNIT RESET and port reset stopped or
reconfigured the endpoints and forced CCS 1 again, so the loop never
converged; the run that recovered was the one whose next tag landed on
rings on an even lap. The driver's Stream Contexts, its Set TR Dequeue
pointers and DCS, and its per-stream doorbells were correct throughout. A
real controller reads DCS (xHCI 1.2c 6.2.4.1 and 4.12; 4.6.9 for a Stop
Endpoint), so the port reset as it stands converges there. On QEMU the
same defect reaches every stop or reset of a streams endpoint - a cancel,
an abort, selective suspend's stop, the injection's own stop - not only
this fault: the same signature (after an abort's Stop Endpoint, a data-in
stream kicked with no transfer started) was read on Vista x64 after fault
14, a lost completion.

A defect of the driver's own sits beside it, found in review and fixed
here because it touches the same recovery on every controller: a stream
whose own event failed keeps its `StreamFault` until
`hcdCfgRecoverDequeue` moves it, but a client's reset that programs every
stream's dequeue (SYNC_RESET_PIPE's host half), or recycles the endpoint
(RESET_PIPE), left it set; a later halt of another stream then moved this
stream again with Set TR Dequeue and discarded whatever partial progress
it had saved since (4.6.10). From this revision a successful Set TR
Dequeue of a stream, and a successful Configure Endpoint that gives its
endpoint a new context, discharge that stream's `StreamFault` and
`Halted`.

The injection adds nothing of its own to that: it delivers the error for a
TD QEMU never started, while QEMU's `usb-uas` still holds that tag's data
phase, which a real error would not leave; the dead streams come from the
stops and the reconfiguration, which a real error's recovery runs too.

#### 4.12.2 The soft retry on a stream

Linux soft-retries a Transaction Error on a stream ring exactly as on any
bulk ring (`xhci-ring.c`, `process_bulk_intr_td`, whose ring is the
stream's; after the Reset Endpoint with TSP set, `xhci_handle_cmd_reset_ep`
rings every stream with TDs, `ring_doorbell_for_active_rings`), and xHCI
1.2c 4.6.8.1 excludes isochronous endpoints and devices behind a TT, not
streams. This design follows, at the endpoint's scope, since a Reset
Endpoint acts on every stream of it:

- **Scope.** A stream's own queue is in scope on the terms of 4.2:
  tolerance on, a bulk endpoint, not behind a TT, the controller not on the
  `XHCI_NO_SOFT_RETRY` list; set when the stream's pipe is built
  (`hcdCfgStreamsBuild`). The endpoint's own queue never holds a TD while
  its streams are open and stays out of scope.
- **Interception** is 4.2's, unchanged: the engine diverts a Transaction
  Error on the head of the stream's queue, keeps the event on the TD - its
  record, its mapping and its place on the ring untouched - and sets that
  queue's `RetryWanted` with its own generation.
- **The thread** visits every stream of every streams endpoint as it visits
  a pipe (`HcdCfgRetryService`). It replays (4.2's today's path) when an
  operation is pending anywhere on the endpoint: on the endpoint pipe
  (paused, closed, a queued ABORT_PIPE, RESET_PIPE or SYNC_RESET_PIPE on any
  of its handles, a queued RESET_PORT, the PDO removing), on the stream (a
  cancel, paused, closed), a cancel pending on any other stream of the
  endpoint, whose Stop Endpoint would stop this stream too, or a drain
  pending on the endpoint or any of its streams (`DrainPending`: a refused
  retire, or a Prime Pipe STALL's, which the thread's cancel service
  answers after the retry service and which needs the whole endpoint at
  rest).
- **The command.** The endpoint is paused (`HcdIoPipePause` on the
  endpoint, which holds every stream), and Reset Endpoint with TSP 1 is
  issued on the endpoint: the streams share its sequence, and every one of
  them halted with it (4.6.8 disables the endpoint's doorbell). No Set TR
  Dequeue: the failed stream's context keeps its dequeue and any partial
  progress (4.12: the Stream Contexts reflect the forward progress before
  the endpoint enters Halted; 4.6.8 keeps the state across the Reset
  Endpoint), and so does every other stream's. On QEMU alone 4.12.4's
  quirk runs here, while the endpoint is still paused.
- **After it**, under 4.2's generation rule, the pause is released and the
  doorbell of every stream of the endpoint with a TD on its ring and not
  paused or closed is rung, the failed stream's included, each with its
  Stream ID (`hcdCfgStreamKick`, which keeps its exclusion of a stream
  whose pipelined successor waits to be taken off): a doorbell rung for
  another stream while the endpoint was Halted was ignored (xHCI 4.7,
  p.144), and work on each stream's ring takes a doorbell write of its own
  (4.7, p.143). Past G only the
  retry's own pause is given back; a failed command leaves the endpoint
  paused for `hcdCfgFault`'s recovery, as 4.2's failed reset does. The
  pause, which 4.2 does without for an endpoint with one ring, holds
  submissions for one command's time; it is taken on every controller so
  the path is one.
- **The failed TD** keeps its record, its mapping and its place: a
  diverted event sets no `StreamFault` and no `Halted`; a replay sets them
  through the ordinary result path, as today.
- **A quiesce** that meets the endpoint Halted (a cancel, an abort, a
  RESET_PIPE) settles every stream's deferred outcome after its Reset
  Endpoint, not only the pipe it was asked for, before
  `hcdCfgRecoverDequeue` moves the failed streams past their TDs; the cycle
  service's halt check (`hcdCycleResolve`) reads a live retry on any stream
  of the endpoint as the halt's owner.
- **The bound** is 4.2's: three per TD, the fourth taking today's path, so
  the class driver then sees the failure it sees today and `xhciuas` resets
  the device. Counted as 4.2 counts (`RetryDiverts`, `RetryResets`,
  `RetryRecovered`, `RetryExhausted`, `RetryReplayed`), and the Reset
  Endpoints a stream's retry issued in `StreamRetryResets` (counter word
  167).

What `xhciuas` sees of a recovered error is nothing: its data URB stays
pending and completes when the retried TD does.

#### 4.12.3 A port reset restarts the stream rings at their base

RESET_PORT drains every pipe and leaves each ring empty at its enqueue
position for the Configure Endpoint that re-adds it (`hcdCfgResetQuiet`).
From this revision, once the whole reset has let go of the old rings -
every request cancelled and completed, the drain waited for, and the
Reset Device done, which leaves every endpoint but EP0 Disabled (4.6.11) -
each stream ring of an endpoint the reset restores is reinitialised in
place before the Configure Endpoint builds its Stream Contexts: its TRBs
zeroed, the Link TRB rewritten with Toggle Cycle and the cycle no TRB of
the new lap carries, enqueue and dequeue 0, producer cycle 1
(`XhciRingRestart`, refused for a ring that is not empty, which then keeps
its position as today). The allocation, its physical address, the ring's
size (1024 TRBs for a stage-3 ring, `BigRing`) and the stream's queue -
its counters, token sequence and retry scope - are kept; the queue
forgets only the ring positions it still remembers (the mid-TD tails it
waits for, `XhciXferQueueRestart`). Nothing that could refer to the old
layout survives: every request was completed and every promoted record or
refill drained before the Reset Device. A ring already at its base with
cycle 1 is left as it is and not counted. The Configure gives the endpoint a
new context (4.6.6), so no position on the ring is owed to anything, and
every Stream Context it builds carries DCS 1. The Drop-and-Add that recycles
one streams endpoint (`hcdCfgRecycleCode`) does the same before its
Configure. Legal on every controller, and it makes a port reset converge on
QEMU, whose forced CCS 1 is then the truth. Rings without streams keep
today's handling: QEMU reads an Endpoint Context's DCS. Counted in
`StreamRingRestarts` (word 168).

#### 4.12.4 The QEMU stream-context quirk

The remaining reach of the defect is every Stop Endpoint and Reset Endpoint
on a streams endpoint whose streams keep their TDs. For QEMU's xHCI alone -
PCI vendor and device ID `1B36:000D` exactly, latched at the start (pure
`XhciTolQemuStreamQuirk`); no other ID, no registry value - after a Stop
Endpoint or a Reset Endpoint that completes successfully on an endpoint
with streams open, while the whole endpoint is paused by the caller (or,
in the `qemu` flavour, its doorbells withheld by the injection) and the
endpoint reads Stopped, the driver gives each stream back the one thing
QEMU discarded, its consumer cycle, with Set TR Dequeue Pointer (SCT
Primary Transfer Ring), and every such command completes before the
caller's resume can ring a stream:

- **The position is QEMU's own where QEMU's own is sound.** QEMU's stop and
  reset do not rewind a stream: they leave the address it last wrote in
  the Stream Context, and its reload starts there (`xhci_set_ep_state`
  with no stream context, then `xhci_find_stream`). Where that address
  names a TRB of the TDs the driver still holds (from its software dequeue
  to its enqueue), the quirk keeps it. An address on the Link TRB, which a
  correct QEMU would follow, is first taken as the ring's base - the same
  position, named as the first TRB of a TD, as a Set TR Dequeue is to name
  one (4.6.10) - and judged as any other. Anywhere else the address is stale
  and is replaced by the software dequeue: behind it every TRB's completion
  was reported and retired - QEMU can leave such an address when a stream
  completes a TD while a sibling has halted the endpoint (its kick then
  returns before rewriting the context, on the Link TRB too when that TD was
  at the base), and a failed TD's context names
  its first TRB until the recovery moves it - and past the enqueue nothing
  was written.
- **The DCS is the cycle the driver wrote at that position**: the TRB's own
  cycle bit inside the TDs it holds, the producer cycle at the enqueue, the
  software dequeue's DCS when the address was replaced.
- **The command is owed** when that DCS is 0, which QEMU's forced 1 gets
  wrong, or when the address is not the one the context holds; a stream at
  its own address wanting DCS 1 is left alone.
- **Containment.** Before each command the endpoint must still read
  Stopped. QEMU's device wakeup can restart a Stopped endpoint on its own
  (`xhci_wakeup_endpoint` calls `xhci_kick_ep`; a real controller restarts
  a Stopped endpoint only for a doorbell), which no pause or withheld
  doorbell prevents: an endpoint found Running again, before a command or
  as the reason one was refused with Context State Error, gets one more
  Stop Endpoint and the whole resync from the start, at most twice
  (`QemuStreamRestops`). A resync that still cannot finish - any other
  refusal, a command that never completed, a third restart - is treated as
  a command that never completed: the controller reset is requested
  (`HcdSvcRequestReset`, as the command path requests it for a command
  with no completion), `QemuStreamResyncFailed` is counted, and the Stop
  or Reset Endpoint it followed reports no completion (0). Every caller
  already contains that answer, as it contains a timed-out command: the
  quiesce and RESET_PORT's quiet fault and leave the endpoint paused, the
  soft retry faults and leaves it paused, and selective suspend finds the
  controller failed and ends its step. The injection, whose own stop runs
  the resync itself, gives the attempt up with nothing rung (abandon note
  `44`) - not its ordinary abandon, which rings every stream - and leaves
  the endpoint to the recovery. The
  same re-stop applies to the driver's own stream Set TR Dequeue on QEMU
  (`hcdCfgSetDequeueOne`), which meets the same wakeup.
The hook is in the thread's command path (`HcdThreadCommand`), run once by
the outermost call (the injection's command hook sends a command it lets
through by a nested call), never for the commands it issues itself, and it
returns the original command's completion and control unchanged unless the
resync could not finish. It covers
the quiesce (each of whose callers pauses the endpoint), the soft retry's
reset, RESET_PORT's quiet, selective suspend's stop and the client's
resets alike; the caller's own ring edits and Set TR Dequeue follow as
before. A Stop or Reset on an endpoint nobody holds (the teardown of a
device that is leaving) is left alone. The injection's own stop is the one
exception: its verdict reads the Stream Context as QEMU left it, so the
hook does not run for the injection's own commands, and the injection runs
the resync itself once it has read that evidence and before it rings or
delivers anything - and abandons the injection, as it does any it cannot
place, when that resync had to stop the endpoint again, since QEMU may
then have moved the TD its verdict found untouched (note `43`), and gives it
up with nothing rung when the resync could not finish.

On a real controller a Set TR Dequeue here would be wrong: it discards the
state a stopped stream saved in its context even when it names the same
TRB (4.6.9, 4.6.10), so a stream the device left in mid-TD would restart
that TRB from its beginning. QEMU saves no such state - its Stop Endpoint
and Reset Endpoint drop every transfer in flight, and its `xhci_reset_ep`
ignores TSP - and the quirk keeps every address QEMU could soundly restart
from, so it adds no replay and no skip of its own; whatever QEMU's discarding of a TD in
mid-transfer costs, it costs with or without the quirk. That is why the
gate is the exact ID and not a heuristic, and also why the `qemu`
flavour's injection cannot show that a real controller's partial progress
survives the soft retry: that half rests on the specification (4.6.8.1)
and on Linux's practice, and is a bench item. Counted: `QemuStreamQuirk`
(word 169, 1 when the quirk is armed on this controller),
`QemuStreamResyncs` (word 170, the Set TR Dequeue commands it completed),
`QemuStreamResyncFailed` (word 171, resyncs that could not finish and
asked for the recovery) and `QemuStreamRestops` (word 172, the extra Stop
Endpoints QEMU's own restart of an endpoint cost).

This works around a defect of QEMU, the emulator, in `hcd-xhci.c`
(`xhci_find_stream` calling `xhci_ring_init`, which forces CCS 1), read at
QEMU 11.1.0; real controllers never take this path.

#### 4.12.5 What does not change

`xhciuas`'s recovery is unchanged: it already acts at once on a failed
transfer, and with 4.12.3 and 4.12.4 its port reset and its aborts
converge on QEMU as they do on a real controller. EP0, isochronous
endpoints and endpoints behind a TT keep 4.2's exclusions. The soft
retry's injection (fault 16 on a stream) is answered as section 5 answers
it on a bulk endpoint: the Reset Endpoint emulated, the real endpoint
already Stopped by the injection's own stop.

#### 4.12.6 "No timeout" after faults 17 and 15

Each of those dumps was taken before the stuck command's own timeout. The
class driver above `xhciuas` gives a read or write a timeout in proportion
to its length - read in the traces as 10 s for an 8-byte command and 160 s
for a 1 MiB read, the ABORT TASK following each on time (10 s per 64 KiB,
so 320 s for a 2 MiB command) - and `xhciuas` arms each command's timer
from the SRB (`uasSlotPrepare`) and counts it down each second
(`uasTick`). The fault-17 dump was taken 145.7 s after the last command,
before its observed 160 s; the 2 MiB fault-15 dumps at 281 s and 291 s,
before the 320 s that the same proportion gives - an inference, since no
2 MiB command was watched to its timeout. These dumps do not show a timer
that failed to arm or fire, and the traced 1 MiB commands show it firing
on time. What kept the device stuck was 4.12.1's defect: fault 15's
stop, fault 17's stop and Set TR Dequeue, and every later abort's stop
re-forced CCS 1. Fault 17's model has one cost of its own: QEMU's
`usb-uas` still waits for the rest of the data the injection cut short, so
that command can end only by its timeout and ABORT TASK; with 4.12.4 the
device then recovers.

#### 4.12.7 Windows 98 SE

The matrix's Windows 98 SE run (fault 16 during a copy from a UAS disk)
showed the same dead disk, and two more things: `XHCISNAP` hung at
"extension:", and the desktop froze after about seven minutes. Traced on
development host A with the stamped `2.2.0.3` `qemu` build:

- **The dead disk is 4.12.1's defect.** A fault on rings still on their
  first lap recovered (the copy finished, `fc` clean); one after the rings
  had wrapped once left every status stream on a cycle-0 lap dead (kicked,
  never started, contexts reading DCS 1 over cycle-0 TRBs), while the
  data-in streams, all on cycle-1 laps there, still moved data. Each
  ABORT TASK and LOGICAL UNIT RESET was answered by QEMU on a dead status
  stream and so never seen; each port reset reconfigured the endpoints
  and forced CCS 1 again. That is the "transfers cancelled" every 15 s.
- **The door was not involved.** `XHCISNAP -probe`, which sends the same
  door request and writes no file, returned at once while the disk was
  stuck, and so did `dir C:\`; the driver's `PowerGate` read free in 119
  of 120 samples of guest memory (the other held, with no waiter), and
  the controller thread kept completing its passes (its debug log grew
  every 30 s). What hung was the first file `XHCISNAP` creates
  (`<name>.BIN.TMP`, created before the first door request): its
  directory entry was never written. The same hang followed a plain
  `echo x > C:\T1.TXT`, with neither the driver nor `XHCISNAP` involved.
  Windows 98's file system queues behind the read the copy is stuck in;
  that is not a lock or queue of the driver's.
- **The freeze** is Windows 98's own: every sampled instruction pointer
  in ring-0 VMM (`C000A881`, `C0003621`, `C005340E` and their
  neighbours), none in a driver, the signature the matrix had already
  read for 98 SE's own wedges.

With 4.12.2 to 4.12.4 the disk does not stay stuck, so nothing waits on
it.

#### 4.12.8 A stream's dequeue read back after a cancel (issue 26 section 8, task 49.39, 2026-11-12)

Issue 26 section 8: on an Intel 7 Series controller, after a data-in
cancel on a UAS drive's stream endpoint, the drive's later reads were
answered GOOD while their data-in TDs never ran, and one stream's Set TR
Dequeue reached the controller with the endpoint Running although the read
before it had said Stopped. Neither what restarts that endpoint nor where
the stream's dequeue then stood is known. This section makes the driver
check, after every cancel on a stream endpoint, that what the controller
saved for each stream is what the driver's rings say, and act only where
acting cannot lose or replay data. The owner's decision of 2026-11-12: on
by default, with `XhciStreamDequeueRepair` 0 leaving the checks and their
notes and taking no action.

**When a Stream Context is authoritative.** xHCI 4.12 (p.219) and 4.6.9
(p.119): before an endpoint enters Stopped the controller makes every
stream's TR Dequeue Pointer and DCS reflect its forward progress
(`docs/usb-xhci-info/xhci-data-structures.md`, the Reset Endpoint and Stop
Endpoint row). The driver reads a Stream Context only when all of these
hold, which is the cancel's own state:

1. the Stop Endpoint's completion (or the Set TR Dequeue's) has been
   received - every event the controller wrote before it has been consumed
   (record 13 section 14's 43.69 gate);
2. the endpoint's Output Context reads Stopped (`hcdCfgEpState`);
3. no doorbell has been rung for the endpoint since that stop: the
   endpoint is paused (`HcdIoPipePause`) from before the stop until the
   cancel's own doorbells at its end;
4. for the stream read, the held short-deferred TDs its saved dequeue shows
   it has passed are settled (`HcdDevStopSettle`) - and a settle that did
   not finish (the ring refused a retire) forbids any action on that
   stream.

A stream read while any of them fails is not compared.

**c1 - the stream the cancel sets.** `hcdCfgCancelPipe` sends a Set TR
Dequeue to the oldest surviving TD's first TRB, or to the enqueue when none
survives: positions no execution of the controller has reached, since it
stopped inside the cancelled head TD or before it. After a Success with the
endpoint still Stopped (`hcdCfgSetDequeueOne`), the Stream Context is read
and its TR Dequeue Pointer (63:4, both words) and DCS are compared with what
was sent; SCT is masked, and checked to be 1 (Primary TR) separately. Equal:
done. Different: the command is sent again, at most twice more, each
followed by the same read; still different, or a command that fails, gives
the device up: `hcdCfgStreamMark` and a failed return, so `hcdCfgFault`
cycles the device alone (issue 24's route). The endpoint read Running after
the Success: nothing is compared and nothing rewound, because it may have
moved legitimately from the new position; it is stopped again (bounded,
`hcdCfgStopBounded`) and that stream is then checked as c2 checks the
others.

**A restart between the difference and the resend** (the merge review of
2026-11-13): issue 26 section 8.1 is exactly an endpoint read Stopped and
then Running at the next command, so c1's own resend must not assume the
endpoint is still at rest. Before each resend the endpoint is read again;
read Running, nothing is sent and the pass goes back to the Running branch
above: stopped again (bounded), then that stream's saved position placed
as c2 places it - its saved progress assessed, never a rewind of what the
controller may have done since. That branch places only an endpoint the
bounded stop left exactly Stopped (`XhciStreamAfterRestop`): Halted or
Error after it - the restarted endpoint faulted while the stop was pending
- is no state whose Stream Context is authoritative, and neither a
placement nor clearing the stream's fault restores it, so the device is
given up, as for a stop that would not hold. For the same reason, once a
difference has been found and not yet repaired, a read that finds the
endpoint Halted or Error - before the resend, or at the pass after one -
gives the device up: the first read's leave-it-to-the-quiesce answer for
those states is for a stream compared equal or not yet compared, not for
one whose saved position is known wrong, whose stream fault must not be
cleared unplaced. A resend answered Context State Error is
read the same way (`XhciStreamResendAfter`): the endpoint now Running is
that branch too, not a give-up; read Stopped, Halted or Error, or another
completion code, gives the device up as before. For a stream compared
equal or not yet compared, the Running branch ends the pass (a placed
stream is kept, an unplaceable one given up). This is `hcdCfgSetDequeueOne`'s own
handling of a refused command - a restarted endpoint stopped again - with
the one difference that after the stop the position is placed rather than
the software dequeue sent again: c1 resends only because the saved
position differed, and once the endpoint has run that difference may be
the controller's real progress. The exception is a stream whose
difference was found before the restart: it is resent only when it holds
no TD of the driver's (below), so no run can have moved a transfer the
driver owns, and after the stop it is read back again rather than placed -
still different, its stale position is sent right on the endpoint now at
rest (placement would only find a cancelled TD's position unplaceable and
cycle the drive for nothing). At most two such stops and two resends
(`HCD_DEQ_RESTOPS`, `HCD_DEQ_RESENDS`); past either, the device is given
up.

What no read can close is a restart after the last read and before the
command lands, if the controller then takes the command because the stream
is not the one it is executing (xHCI 4.6.10 takes Set TR Dequeue for a
stream that is not active; 4.12: the controller does not know whether a
device will select a stream again). A run in that window starts from the
controller's saved position; if it reaches a TD the driver still has
queued on that stream and moves it, a resend to the driver's position
would replay that TD's transferred part. So c1 resends only for a stream
that holds no TD of the driver's (`XhciStreamMayResend`: the queue empty,
the endpoint Stopped at the read just before): no run in the window can
move a transfer the driver owns, and replacing the stale position loses
nothing. That is the issue's case: a UAS stream carries one command, and
its cancel leaves the stream with none. A stream with a TD queued is given
up at the difference, as before this revision, since nothing short of a
device cycle proves that TD untouched. A run that starts from a resent
position comes after the command; the next read finds the endpoint Running
and the Running branch keeps that progress by placing it.

**c2 - every other stream of the endpoint**, before the cancel's doorbells,
in two steps. First the position alone (`XhciXferStopPosition`, the half
of `XhciXferStopPlace` that reads no queue): a saved pointer on that
stream's ring, inside its outstanding window (from the software dequeue to
the enqueue, a Link TRB taken as the base it leads to) and carrying the DCS
the ring gives that TRB, is a position the controller can hold. Any other -
off the ring, behind the software dequeue, past the enqueue, the wrong DCS
- is one it could not have reached by executing the ring, and rewinding it
cannot be proven safe: a context the controller corrupted may sit anywhere
relative to what it actually did. It is never repaired by a Set TR
Dequeue: with the repair on the device is given up before any doorbell
(`hcdCfgStreamMark`, then `hcdCfgFault`: the device alone cycled); with it
off, it is noted and counted only. Then a holdable position is settled
against the queue as 43.69 does for the cancelled pipe
(`HcdDevStopSettle`): inside a queued TD or between them is legal, partial
progress is kept and nothing is sent. Past a TD whose completion has not
come, which the settle calls `UNREAD`, is noted and counted (`unsettled`)
and nothing is done: QEMU publishes its fetch pointer past a TD it holds in
flight (`xhci_kick_epctx`; `test_xfer`'s `test_stop_place_fetch_pointer`),
and on a real controller that TD is left to its own timeout and to record
13 section 16.15, which resets the device without losing the volume.

**On QEMU's xHCI** 4.12.4's resync runs at the stop, before these checks,
and gives every stream QEMU left on an address outside its window the
software dequeue, so c2's give-up is not reached there by QEMU's own
behaviour: only by the host vectors. A fetch pointer past a TD in flight is
inside the window, kept by the resync, and is c2's `unsettled` case. c1 is
reached on QEMU by fault 1D below, which alters the cancel's own command
after the resync.

**What a cycle costs** is issue 24's: the device leaves and returns, its
volume with it, and a removable drive's volume may be verified again. It is
taken only on a reading the specification says cannot occur.

**`XhciStreamDequeueRepair`**, a `REG_DWORD` in the controller's driver key
and in the machine-wide `Services\USB` key, which wins when there, read at
each start (`hcd_ctl.c`, beside the quirk values): absent or 1, the checks
act as above; 0, c1's read-back and c2's placement run and are noted and
counted, and no extra Set TR Dequeue and no give-up follows - `3.4.3.0`'s
behaviour; any other value is refused and 1 stands. Both INFs write it 1
with no-clobber; an INF-gate row and a `tuning.md` row.

**Notes and counters.** `cfg.deq.readback` (slot `<< 16` | DCI `<< 8` |
stream `<< 4` | the verdict: 0 equal, 1 different and sent again, 2 given
up, 3 Running, not compared), `cfg.deq.verify` (the same fields, the
placement verdict and whether it was acted on), `cfg.deq.giveup`; counters
`StreamDeqChecked`, `StreamDeqMismatch`, `StreamDeqResent`,
`StreamDeqGiveUps`, `StreamDeqUnplaced`, `StreamDeqUnsettled`.

**The injection** (qemu flavour; `XhciQemuInject`, `build-and-test.md`):

- `1D` (29), a stale dequeue: the next `arg` stream Set TR Dequeues sent by
  a cancel whose head TD was cancelled are sent for real with the TR Dequeue
  Pointer and DCS of that head TD's first TRB in place of the driver's.
  QEMU's `xhci_set_ep_dequeue` takes it and writes the Stream Context
  (`xhci_set_ep_state`), so the read-back sees the stale position and the
  stream would re-run the cancelled TD. A deliberately stale hardware
  dequeue: a pass shows the driver recovers from that fault, not that it
  is the Intel controller's mechanism. With `arg` 0 to 2, c1 sends the
  command again and the cancel completes; from 3 every send is altered and
  the device is given up.
- `1E` (30), a restart between the read and the command: after
  `hcdCfgSetDequeueOne` reads the endpoint Stopped and before the stream's
  Set TR Dequeue is sent, the endpoint's doorbell is rung for a stream of
  it holding a queued TD, at most `arg` times. QEMU then runs the endpoint:
  the command meets Context State Error and the existing retry stops and
  sends again, or lands and c1's Running branch follows.
- `1F` (31), a restart inside the repair: after c1 has found a difference
  and before its resend, the endpoint's doorbell is rung for another stream
  of it - one holding a queued TD, else any other, whose empty ring runs
  nothing but takes the endpoint Running - at most `arg` times. Used with `1D` (a difference to
  repair), it shows the resend meeting a restarted endpoint: the pre-send
  read or the command's Context State Error takes c1's Running branch, and
  the stream is placed without a cycle. Noted `qemu.inj.repairrace`.
- `20` (32), a bad device descriptor at a root port (roadmap-usb4 task
  50.7): the next `arg` 8-byte device-descriptor reads of an enumeration
  at the root port in bits 15:8 (0 any; `arg` 0 one, 255 until `CLEAR`)
  are answered with bMaxPacketSize0 0, the transfer itself real and
  successful, so the machine's DESC8 check fails with
  `XHCI_ENUM_FAIL_DESCRIPTOR` as a device with a bad descriptor does; with
  `arg` 2 its one retry meets it too and the port rests in Failed, with no
  cycle, recovery or hold - the "device failed enumeration" connection
  design record 25 section 24 item 4's note is for, which the emulator's
  devices never give. Noted `qemu.inj.enumdesc`.

Both are independent of the emulator's `usb-uas x-ata-passthrough-refuse`
(`usb4-emu`'s `docs/XHCI98-HANDOFF.md`), which gives the drive's refusal
with the data-in TD left to cancel. Each can be used without the other.

**Vectors.** `test_stream`: c1's comparison (equal, a pointer off by one
TRB, a DCS flipped, SCT not 1, the high word nonzero), and what follows a
resend (`XhciStreamResendAfter`: Success reads back again; Context State
Error with the endpoint Running is the Running branch; Context State Error
with it Stopped, Halted or Error, another code, or no command is a
give-up; `XhciStreamAfterRestop`: only Stopped after the bounded stop is
placed - Running, Halted and Error give up; `XhciStreamMayResend`: only an
empty stream read Stopped is resent - a queued TD, or any other state, is
not). `test_xfer`:
`XhciXferStopPosition` (empty, wrapped, inside the head TD, on the Link
TRB, behind the software dequeue, past the enqueue, wrong DCS, off the
ring) and `XhciXferStopPlace` unchanged by the split, including QEMU's
fetch pointer past an unfinished TD after `XhciRingResyncOwed`, which is
holdable and settles `UNREAD`. Legs (task 49.39): one mismatch
repaired; a persistent one ending in the device cycle; command failures
during the repair; neighbouring streams with transfers in flight
unaffected; a TD partly done when cancelled; a copy-compare across a
repair.

#### 4.12.9 What the next report says (issue 26 section 8, task 49.39, 2026-11-12)

Issue 26 section 8 could not tell where the data-in stream's dequeue stood
while the reads starved: no report of `3.4.3.0` carries the endpoint's or
the streams' contexts, the failure ring has no times, and an event inside
no queued TD is a count with nothing else. Four additions, so that the
reporter's next report settles the mechanism - a stale dequeue seen after a
successful completion while stopped supports section 4.12.8; matching
dequeues with continued starvation points elsewhere:

- **The stream contexts** (`XHCI_SNAPSHOT_REGION_CONTEXTS`, region 4, a
  new region, so the schema stays 5: an older driver answers
  `XHCI_SNAPSHOT_S_BAD_REGION` and the tool says "not served by this
  driver"; an older tool never asks). One record per endpoint with
  streams open, in Slot ID and DCI order: the slot, the DCI, the Output
  Endpoint Context's five dwords, the stream count, then for each stream
  the Stream Context's first two dwords beside the driver's own view of
  that stream's ring - its software dequeue (address and cycle), its
  enqueue and the TDs queued. Pointer-free ULONGs; each record carries
  its own size, and a reader walks by sizes. Read under the controller
  lock while the controller is STARTED, as the slots region is
  (`hcd_door.c`, `hcdDoorContexts`): one window can catch the controller
  mid-move, and the text says so. XHCITR prints each stream as a line -
  the saved position, its placement against the ring (the same
  `XhciXferStopPosition` as section 4.12.8), the driver's dequeue and
  enqueue - in `xhcitr\tr_ctx.c`, a file of its own.
- **The stream trace** (`XHCI_SNAPSHOT_REGION_TRACE`, region 5): every
  flavour, per controller, a ring of 256 entries of four ULONGs - the time
  (the system time in the `at` notes' 102.4 us units), a kind byte with
  slot, DCI and stream, and two values - of what happens to stream
  endpoints only: a command sent and its completion code, an endpoint
  state read, a stream's Set TR Dequeue's pointer with its DCS and SCT and
  the read-back of section 4.12.8, a cancel's TD bounds, a stream's
  doorbell, a stream's transfer event (pointer, code, length). Written
  under the controller lock (the event path's own), so the cost is a
  store per event. **A capture is taken**, not a stop: when a UAS
  device's `RESET_PORT` arrives (before its Reset Device: record 13
  section 16.15's reset) and when section 4.12.8 gives a device up, the
  whole ring is copied, in the same hold, to a second buffer with a
  capture number, the reason and the time, and the live ring goes on.
  Reading the region changes neither: every read returns the last
  capture and the live ring, so a save that fails, or a second report
  taken later, still carries the same capture until a newer incident
  replaces it (its number tells them apart).
- **The failure ring's times** (`xhci98_xfer.h`): `XHCI98_FAILURES` version 2
  appends eight time words (the class driver's own seconds since its start,
  entry `i`'s at word `i`) after the unchanged header and ring. The bus
  accepts version 1 at its size and version 2 at its size
  (`hcd_urb.c`), and copies, in one hold, the existing 33 words to the
  existing block (older XHCITR keeps its four-word stride) and the count
  with the eight times, and the LUN's ATA refusal bits (record 13 section
  16.16), to a new block appended after header word 929 with its own
  records, words and first-record trio (`XHCI_SNAPSHOT_HCD_UTIME_*`). A
  header that ends at 929 is an earlier driver's. XHCITR prints a time
  beside each failure only when the new block's count equals the old
  block's, and "ATA PASS-THROUGH (12)/(16) refused by the drive: answered
  here until it is replugged" when a bit is set. `xhciuas` sends version 2
  and falls back to version 1 when a bus refuses it.
- **An event inside no queued TD** (`hcd_dev.c`, the transfer event's
  path): the first eight of each start note `xfer.unmatched` (slot `<< 16`
  | DCI `<< 8` | stream `<< 0`, the stream the ring names, 0 for none) and
  `xfer.unmatched.trb` (the TRB pointer's low dword), and a trace entry.

The quick save that carries these (record 25 section 22 item 14) now
flushes what it wrote: see that record.

#### 4.12.10 A controller whose stream Set TR Dequeue moves another stream (task 50.20, 2026-11-15)

**Standing.** This is the design as approved on 2026-11-15, built in
task 50.20's commits, with the controller list made a setting the same
day (`XhciStreamDequeueAvoid`, the owner's decision; "Which
controllers" below). Nothing here has yet been read on the bench. The
validation plan at the end of this section says what is to be shown, and
results are written only when they have been read (`runs/run-50.md`).

Reports from one computer - an Intel 7 Series xHCI (`8086:1E31`), a UAS
drive with four streams on its data-in, data-out and status endpoints,
`3.5.0.0` on XP x64 - carry sixteen-plus stream-trace captures
(4.12.9). Each one shows the same thing around a stream cancel on an
endpoint:

- the Stop Endpoint's Stopped event names a TRB on the ring of a stream
  other than the one cancelled;
- after the cancel's Set TR Dequeue for stream X, the position just sent
  for X later behaves as another stream's.

That behaviour takes three forms:

- **The replay.** A TRB of X's completed TD completes a second time
  after another stream's doorbell, and that stream's command then times
  out with its status in (`010559`, `035038`).
- **Babble** at exactly the position just set (`012801`, `023320`).
- **The give-up.** That exact value is read in a third stream's context
  by 4.12.8's c2, which gives the device up and cycles it (`034046` to
  `043311`). Three cycles hold the port, and the drive is gone until it
  is replugged.

The triggers are the cancels of a monitoring program's refused commands
(HWMonitor's, Speccy's and HWiNFO's ATA pass-through and vendor CDBs:
CHECK CONDITION with the data-in TD posted) and the recovery's own
cancels. The drive-side check found no cause (context indexing, the
command's Stream ID and SCT, the tag-to-stream mapping), and every capture
fits a controller that keeps a cached state for the stream it last ran and
loads a Set TR Dequeue for another stream into it. That is a reading, not
a documented erratum, and nothing here depends on the mechanism. It rests
on one observed fact: **on this controller, after a stream's Set TR
Dequeue, the other streams' positions on that endpoint cannot be
trusted**, whether in their contexts or hidden. Nothing in the captures
implicates a Stop Endpoint or a Reset Endpoint alone, and every foreign
value lies on the same endpoint as the command that sent it.

The fix splits the work by who holds the knowledge. The bus never sends
that command and holds an endpoint whose position it withheld. The UAS
client, which alone knows whether a device task has ended, chooses and
runs the recovery. Only a client that declares this contract at the
streams open gets it; every other client keeps `3.5.0.0`'s paths,
unchanged (the owner's choice of 2026-11-15).

**Which controllers: `XhciStreamDequeueAvoid`** (owner, 2026-11-15). A
registry value in the form of the Linux quirk values (design record 20
section 6.1), read at each start from the controller's driver key and
written by every INF at 1:

- `0`: off on every controller: `3.5.0.0`'s check and cycle, on the
  Intel 7 Series too;
- `1`, the default: on the listed controllers. The list is
  `XHCI_QK_DEQ_AVOID`'s in `xhci_quirk.c` - `8086:1E31` alone -
  matched against the PCI vendor and device dword
  (`XhciStreamSetDeqUntrustedId` answers from it). Lynx Point `8C31` and
  `9C31` are investigation targets, not listed on their ancestry;
- `2`: on every controller, at the user's own risk on one not known to
  need it. What it costs there: no stream Set TR Dequeue on a declaring
  client's endpoints, so every cancel that takes a TD off a stream ring
  holds the endpoint until the client's recovery - an endpoint recycle,
  or a device reset where other commands were in flight - in place of
  one command; nothing is gained on a controller without the fault.

Absent, another type or another number reads as 1. The value is the
fifteenth of the quirk table (`XHCI_QK_DEQ_AVOID`, the label
`quirk.deqavoid`, Linux flag "none") and no Linux quirk; its packed word
is counter word 325 (`QuirkStreamDequeueAvoid`), apart from the fourteen
at 199 to 212, and XHCITR prints it as the quirk block's last row. The
start decides once HcdQuirkStart has matched the lists
(`hcdStreamDeqApplies`, `hcd_ctl.c`): `hc->StreamSetDeqUntrusted` is
`XhciQuirkOn` for it, latched for the started lifetime. Whatever the
value, the contract still needs a client that declares the recovery at the
streams open (below): a non-declaring client keeps `3.5.0.0`'s paths at
every value (option B stands). In the `qemu` flavour fault 33 counts the
controller as listed (below), so the value still decides.

**The handshake** (`xhci98_streams.h`). `XHCI98_STREAMS_REQUEST.Reserved`
becomes `Flags`, with the same layout, in two halves:

- bits 15:0 are the client's: `XHCI98_STREAMS_F_CLIENT_RECOVERS`, "this
  client runs this section's recovery";
- bits 31:16 are the bus's: `XHCI98_STREAMS_F_ACK_CONTRACT`.

The bus clears its half on every request before pending it
(`hcdStreamsIoctl`), and the thread sets the acknowledgment only on a
successful open, on a flagged controller, of a request that carried the
declaration (`XhciStreamAckFlags`).

The client sets `Flags` to exactly the declaration before every
OPEN_STREAMS. It takes the acknowledgment only from an open that
succeeded with bit 16 set, and enters the contract only when all three
endpoint opens acknowledged; a start whose opens disagree fails
(`UasStreamsAcked`, `UasStreamsContract`). A bus before this one never
writes the field, so it echoes the declaration and never acknowledges:
the new client then behaves as before. A client before this one sends 0
and gets no acknowledgment.

**Per endpoint, not per controller.** The thread sets `StreamContract` on
the endpoint pipe object in the same success that writes the
acknowledgment.

- **On a contract endpoint** everything below applies.
- **Every other streams endpoint, on a flagged controller too, takes
  today's paths unchanged**: Set TR Dequeue, c1, c2, 4.12.8's give-up and
  cycle, the QEMU resync. A client that does not declare - Microsoft's UAS
  driver on 8 and later with this bus on the controller, an older
  xhciuas, any other - keeps the field's failure modes on that controller.

`StreamContract` lasts as long as its streams are open.

- **CLOSE_STREAMS.** A successful close keeps it through its quiesce and
  its context replacement, discharges the debt on the surviving endpoint
  object, then clears it before the endpoint resumes; a failed close keeps
  it.
- **Opens.** A failed or `ALREADY_OPEN` open leaves the owner's contract
  as it is. A reopen takes its own answer: zero for a client that does
  not declare.
- **A select, a configuration change and retirement.** New pipe objects
  begin without a contract. A provisional detachment keeps the old
  object's contract and debt, and a refused Configure's reattachment
  restores that same state. Terminal retirement ends them.
- **RESET_PORT and the resume replay** keep it with the pipe objects they
  keep.

**The invariant.** On a contract endpoint no Set TR Dequeue Pointer
command naming a stream is sent, with one exception, the Error exit. A
caller that drops the endpoint (a select, a switch, a deconfigure) and
finds it in Error sends that one command: only it or a Reset Device
leaves Error (xHCI 4.8.3), and a Drop needs Stopped (4.6.6). The exit is
one attempt - no Context State Error retry, no read-back or resend (c1),
no c2, no stream kick - followed by the Drop before any doorbell. Its
failure is the caller's (`hcdCfgFault`), and the endpoint is never
resumed after it. Each one is noted `cfg.deq.errorexit`.

The backstop is `hcdCfgSetDequeueCode`, the one production builder of a
stream's command besides the QEMU resync. It resolves the endpoint
(`hcdCfgEndpointOf`) and, when that object has the contract, refuses
both a command naming a stream and any Set TR Dequeue built for an
endpoint pipe with streams attached, unless the Error exit's helper
calls it. A refusal counts `StreamDeqWithheld` and notes
`cfg.deq.withheld`; that counter stays 0 unless this design has a
defect.

On a contract endpoint, and only there:

- c2 (`hcdCfgStreamsPlaced`) is skipped, even for a cancel that moved
  nothing;
- the QEMU resync (4.12.4) is skipped for that endpoint;
- the `qemu` flavour's own stream commands are refused there
  (`qemu.inj.contract`).

**Every site, on a contract endpoint.** A caller is KEEP when the
endpoint keeps its context, REPLACE when the caller gives it a new one
before any doorbell, DROP when it removes it (`XhciStreamFlagDecide`).
Each first proves the endpoint at rest, as today: Stopped after the
bounded stop, Halted, Error or Disabled, read after a completed command.
Running after the bounded stop is today's give-up, `hcdCfgFault`.
"Withheld" means: the stream's command (for a Halted endpoint in a KEEP
caller, also the Reset Endpoint and the failed streams' dequeue) is not
sent, and the endpoint pipe's `DeqDebt` is set.

- **The IRP cancel** (`hcdCfgCancelPipe`; a PDO's cancel marks the same
  way) is KEEP. The quiesce leaves Halted and Error as they are. The
  take-off is today's. A TD taken off a ring, or the endpoint Halted or
  Error, means withheld.
- **ABORT_PIPE, a drain and a removing PDO's abort** (`hcdCfgAbort`) are
  KEEP: the same, with success answered.
- **SYNC_RESET_PIPE** (`hcdCfgResetHost`) is KEEP. Busy is unchanged.
  Halted or Error means withheld, with success answered; the endpoint
  stays held until a RESET_PIPE or RESET_PORT.
- **RESET_PIPE** (`hcdCfgReset`) is REPLACE, and it is the client's
  endpoint recycle:
  - its abort of a busy endpoint takes Halted to Stopped (Reset Endpoint,
    TSP 1) with the dequeue withheld;
  - **Error fails the request** (`HCD_USBD_INTERNAL_HC_ERROR`), the
    endpoint held, for the client's device reset;
  - for Halted, the steps are the contract's own, in order: Reset
    Endpoint (TSP 0), the device's CLEAR_FEATURE(ENDPOINT_HALT), then the
    recycle (Drop and Add, `hcdCfgRecycleCode`), in place of
    `XHCI_PIPE_STEP_SET_DEQUEUE`;
  - for 36.12's owed record, whose endpoint the earlier Reset Endpoint
    already left Stopped, no second Reset Endpoint (as
    `XhciPipeResetSteps` omits it today): the device's clear, then the
    recycle;
  - `XhciPipeResetSteps` stays as it is, and every endpoint without the
    contract takes its steps unchanged;
  - not Halted: today's (the clear, the quiesce, the recycle);
  - a clear not taken leaves today's `ClearOwed` record, and the debt;
  - **a recycle Configure the controller refuses** with a completion code
    (Resource Error, or any other) fails the request with the endpoint
    held: the debt raised if no cancel raised it (`cfg.debt.raise` why 8),
    the operation's own pause given back, `cfg.recycle.refused` noted, and
    neither a controller reset nor the device's cycle asked
    (`hcdCfgRecycleFailed`, both of `hcdCfgReset`'s recycle sites; the
    decision `XhciStreamRecycleFailed`). The client then takes its device
    reset (RESET_PORT: Reset Device and a Configure that gives every
    endpoint a new context), and TEST UNIT READY per LUN; the drive stays.
    The recycle has already emptied the endpoint's rings in software before
    its Configure - every stream's dequeue moved to its enqueue, and each
    stream with nothing queued restarted at its base, its ring zeroed
    (`XhciRingRestart`) - so the software rings no longer match the
    controller's old context. On a real controller that is safe because
    the endpoint stays held: the debt keeps every doorbell off it, a
    controller following xHCI 4.6 runs a Stopped endpoint only for a
    doorbell, and the only things that release the debt give that object a
    new context built from the same software rings first (the device
    reset's Configure, or a later recycle that succeeds). Leaving the
    rings as they were instead would need the recycle to undo its
    preparation, a larger change for no gain while the endpoint is held.
    **On QEMU this is not shown.** Its device wakeup restarts a Stopped
    endpoint whatever the pause (`xhci_wakeup_endpoint`, 4.12.4 above),
    and fault 34 answers before the Configure, so the Stream Contexts in
    memory still name the old positions on rings the restart has zeroed.
    A stream whose saved DCS is 1 then finds a TRB it does not own and
    runs nothing; one whose saved DCS is 0 could take a zeroed TRB as its
    own. The data endpoints wake only for a command's data phase, and the
    barrier starts no command until the recovery ends, but a status
    endpoint can wake for a task management answer. What the fault-34
    legs read instead: the emulator's model loads (none expected), and any
    transfer event on the held endpoint between `cfg.recycle.refused` and
    the device reset's `cfg.debt.release` (none expected; `xfer.unmatched`
    and the trace);
  - **a recycle Configure with no completion** is a command that never
    completed, and is handled as every such command is: the command layer
    takes the controller as failed and requests its reset (`hcdCommand`),
    every device on it removed and found again. No special case.

  USB 3.2 9.4.5: ClearFeature(ENDPOINT_HALT) "always results in the data
  sequence being reinitialized to zero, and if Streams are enabled, the
  Stream State Machine shall be reinitialized to the Disabled state". The
  Drop and Add then gives every stream of the endpoint a fresh context,
  its ring restarted at its base (4.12.3).
- **CLOSE_STREAMS** is REPLACE: withheld, then the close's own recycle
  discharges it.
- **SELECT_INTERFACE's switch, a select's deconfigure, an unconfigure or a
  PnP stop** are DROP. The quiesce takes Halted to Stopped by Reset
  Endpoint (TSP 1) with the dequeue withheld; Error is the Error exit.
- **Unchanged:**
  - the soft retry (4.12.2, a Reset Endpoint with no Set TR Dequeue). An
    owed endpoint is paused, which its contention test reads as
    contended: the retry is replayed into today's error path;
  - RESET_PORT and the link keep (`hcdCfgRestore`: Stop only, the drain,
    Reset Device, Configure), which is not a cancel;
  - the resume keep;
  - selective suspend's rewind (streams excluded);
  - EP0, a Disabled endpoint, a stop that will not hold.

**The cancel's epilogue.** `hcdCfgCancelPipe` keeps one epilogue, the
ordinary one.

1. The entry takes the operation's own pause.
2. In the lock hold that ends the take-off, a withheld command sets
   `DeqDebt` if it is clear (`HcdIoPipePauseLocked` takes its one
   reference).
3. The direct survivors' doorbell, which tests no pause, is not rung
   while `DeqDebt` is set (`XhciStreamSurvivorDoorbell`). That holds for a
   later cancel with nothing moved as well.
4. `HcdIoDeferred` delivers what the take-off completed, in today's order
   after the doorbell's hold. An owing endpoint already holds its debt
   then.
5. `HcdIoPipeResume` releases exactly the operation's reference, and
   `hcdCfgStreamKick`, which tests the pause, rings nothing on an owed
   endpoint.

`hcdCfgAbort` has the same shape. The failure exits keep today's "left
paused" for the teardown or recovery that follows.

**Host-context debt, by pipe object.** `DeqDebt` on an endpoint pipe owns
exactly one pause reference of that object, which `HcdIoPipePause`'s rule
extends to its streams. It is taken only on the clear-to-set transition
(`XhciStreamDebtSet`), on the controller thread, under the controller
lock.

- **Released** (`XhciStreamDebtReplaced`) only when a successful command
  gives that same object a new context: its recycle, the replay of
  RESET_PORT, the link keep or the resume keep, or a rollback's Add. The
  caller's own pause still holds until the caller resumes.
- **Cleared with nothing released** (`XhciStreamDebtClosed`) when the
  object is terminally retired or freed. A provisional switch detachment
  is not that.
- **Never inherited** by a replacement object at the same DCI.

Every other holder of a pause keeps its own: an operation in progress, a
failed command's containment, 36.12's `ClearOwed`, selective suspend, a
switch. The debt is the bus's half only. It says nothing about device
tasks, and no transition of it clears the client's obligation.

**xhciuas's recovery** (`uas_xport.c`, only when `fdo->StreamContract`
is set; every path is otherwise today's).

- **The reason and the barrier.** `uasActCancel`, under the FDO lock,
  marks a busy transfer for cancelling. On a stream transfer it also sets
  `fdo->StreamOwed` (the device-task obligation) and adds the transfer's
  endpoint to `fdo->StreamGated`, and asks for the worker unless one is
  running (`UasStreamOnCancel`): a cancel made while `Recovering` is set
  belongs to that recovery.

  `uasPump` starts no command while `Recovering` or `StreamOwed` is set.
  The mark is made in the same hold that decides the cancel - in
  `uasStatusIu` before the slot's finish, in `uasEndSlotLocked`, in
  `uasTmf` - so the barrier stands before the trigger's IRP is cancelled,
  its slot finished or its tag freed.

  Status reposts, READY handling and the worker's task management bypass
  the pump and may go on serving tasks already started. Their transfers
  are owned by xhciuas from `uasXferBuild` on, before `uasActRun` submits
  them with the lock dropped, so the proof below sees them.
- **The proof** (`UasStreamProof`) is the worker's step whenever
  `StreamOwed` is set, even when the refused command is long gone. It
  runs one bounded loop - 10 ms polls, one five-second budget for the
  whole step, which no intermediate result restarts - re-evaluating under
  the FDO lock each time:
  - **The device reset at once** when any of these holds: a command slot
    ACTIVE; a task management slot; a tag in quarantine (the bitmap, or a
    still-owned slot with `Quarantine` set); a task management answer that
    was not an applicable success (`uasTmfOk`).
  - **Wait** while either of these holds: a transfer xhciuas cancelled is
    not back (`CancelRefs` nonzero, or `Cancelling` with `Busy`); or a
    FINAL slot still has a busy transfer xhciuas did not cancel (its
    command IU's completion, or data it did not cancel), whose endpoint
    the recycle would otherwise abort.
  - **The endpoint recycle** when every transfer is back, no slot has a
    busy transfer, and none of the reset conditions holds. `StreamGated`
    is taken and cleared under the lock, and each endpoint in it gets
    URB_FUNCTION_RESET_PIPE on its endpoint handle, serially. Before the
    barrier drops, the lock is taken again and any endpoint added
    meanwhile is recycled by the same proof. One RESET_PIPE failing is the
    device reset, and the pump never reopens between endpoints.
  - **The budget running out** is the device reset.

  The device reset is `uasResetDevice`, unchanged: every SRB in flight
  ended BUS_RESET, every transfer back, RESET_PORT. Either success clears
  `StreamOwed`.
- **The recovery-owned command phase.** It runs after `uasResetDevice`'s
  successful RESET_PORT on a contract device. A port reset raises UNIT
  ATTENTION, ASC 29h, on real drives (record 20). Without this phase the
  first READ after the reset would meet CHECK CONDITION with its data-in
  posted, and its cancel, with the other retried READs alive, would reset
  the device again.

  A marked internal command (`UAS_ICMD_RECOVERY`) is started by the pump
  even while `Recovering` and `StreamOwed` are set; every other request,
  the wake check's included, stays queued. Each open LUN gets TEST UNIT
  READY, which has no data phase and so no cancel, each with a 5 s
  deadline. The outcome comes from the slot's end (`UasRecoveryPhaseNext`):

  - a device answer: a parsed SENSE IU (GOOD or CHECK CONDITION) or a
    RESPONSE IU;
    - UNIT ATTENTION (sense key 6): again, up to four answers in all;
    - any other answer, or the fourth UNIT ATTENTION, ends the LUN's
      phase (counted `UasStreamUnitAttentions`);
  - no device answer: an allocation or admission failure, a transport
    failure, or the deadline passing before or after submission;
    - an allocation or admission failure is tried once more after 100 ms;
    - anything else is a device that did not come back from its reset.
      It is Dead, and `CycleOwed` is set explicitly - RESET_PORT
      succeeded, so `uasResetDevice`'s own condition would not set it -
      and the cycle is asked of the bus (45.41 L5). A phase command still
      in the device is abandoned as the wake check abandons one.

  Removal during the phase ends the pass as today.
- **Unchanged:** the tag quarantine, `uasGiveUp` and its 16.17 counters
  (a stream-owed reset is a reset for another cause, unless a live
  `FinalReset` command makes it also a 16.17 reset), the failure ring.
  The seal (45.23) defers the worker as it defers every recovery;
  `StreamOwed` survives it.

The cases the captures show:

- **A refusal alone** (every Speccy and HWMonitor capture: the data-in TD
  was the endpoint's only one, and no other command was in flight) costs
  one RESET_PIPE: no device reset and nothing else ends.
- **A refusal while other commands run** resets the device, and the class
  driver retries the SRBs after the TEST UNIT READY.
- **A recovery that cancels on the status endpoint with other status TDs
  alive** (`034959`, `040540`, `043120`) resets the device.

**Request lengths** keep today's rules. With the command never sent on a
contract endpoint, no byte the controller reports is less trustworthy
than on any controller, and xhciuas takes a transfer's length only when
it succeeded (`uasXferDone`).

**What bounds repeated recoveries.** There is no new give-up, and nothing
counted here gates an action.

- A recycle ends nothing.
- A device reset is xhciuas's existing reset, bounded as its resets are.
  The worker's sixteen-round escalation applies to timeouts, and
  `uasGiveUp` only to 16.17's `FinalReset` commands; neither bounds every
  stream-owed reset.
- A drive that keeps UNIT ATTENTION past four answers, or keeps refusing
  a retried command with its data-in posted while other commands run, can
  cost a reset each time. The class driver's retry budget ends each
  request, not the stream of later ones.

The following still remove the drive:

- a stop that will not hold;
- a recovery command in the bus with no completion (the controller
  reset, as for every command that never completes); a recycle Configure
  the controller refuses with a code does not remove it - the request
  fails, the endpoint held, and the client's device reset follows;
- RESET_PORT failing, the client's device reset itself (the device alone
  cycled through xhciuas's Dead path, the last resort);
- xhciuas's Dead paths;
- the device gone or the controller failed.

Successful recycles and resets charge nothing to the location's budget
and never lead to the hold.

**Mixed clients.** Endpoints of one device can have different clients
only when two functions open streams; each endpoint follows its own
state. On one controller, a declaring xhciuas on one drive and a
non-declaring client on another: the second's stream Set TR Dequeues are
still sent. Every foreign value in the captures lies within one endpoint,
and the captures had one streams device, so whether this controller's
cached state is per endpoint, per slot or wider is not known. If the
second drive's command could disturb the first's endpoint, the contract
would not protect it. That residual is accepted with the owner's choice.
RESET_PORT resets every function of the physical device: that is its
existing scope, not a new reset.

**Counters and notes.** Counter words: `StreamDeqUntrusted` (1 when
armed), `StreamDeqWithheld` (the backstop), `StreamDebts`,
`StreamContractOpens`, `StreamErrorExits`. Notes: `cfg.deq.withheld`,
`cfg.debt.raise` (slot, DCI, why, the pause count after it),
`cfg.debt.release` (slot, DCI, replay, the count),
`cfg.deq.errorexit`, `usbdx.contract`. xhciuas counts its endpoint
recycles, the device resets the stream recovery chose and the UNIT
ATTENTIONs its command phase read past (`StreamCounters`, in its FDO; its
telemetry record is unchanged). The bus's notes show each recycle as a
`cfg.debt.release`.

**The injection** (`qemu` flavour). Fault 33 (`21`,
`XHCI_INJ_STREAM_UNTRUSTED`). Unlike every other fault, its value is
also read at the controller's start: the flag has to stand before the
streams open, and a value present at a start otherwise never fires. The
argument is bits 7:0:

- 1: the controller counted as listed, as `1E31` is: flagged at
  `XhciStreamDequeueAvoid` 1 or 2, not at 0;
- 2: the QEMU resync off for every streams endpoint, the controller
  not counted as listed, so the flag stays as `XhciStreamDequeueAvoid`
  decides it: unflagged at 0, or at 1 on QEMU's unlisted controller (the
  emulator's give-up form of 4.12.8 then shows); flagged at 2, where a
  declaring client still takes the contract recovery;
- 3: both.

**The validation plan.** All test hooks are confined to the `qemu`
flavour of the bus and to a test build of xhciuas; no release build
carries them.

- **Rendezvous, not delays alone.** Fault 35 (`23`) holds the bus's
  controller thread at a named point: between a contract endpoint's
  cancel take-off and its completion delivery (1), or between RESET_PIPE's
  clear and its recycle (2). It notes `qemu.inj.rv.reached` with a count,
  and holds until the release (`23` again, `arg` 0) or 60 s. A guest
  program submits while the point holds, and the note names the window
  it landed in.

  The windows inside xhciuas follow from these. Between the barrier's
  lock hold and the cancel's return, the bus holds at point 1. Between the
  cancel's return and the recycle, the worker's RESET_PIPE waits at
  point 2.

  One interval is not controlled deterministically: between xhciuas
  building a transfer under its lock and `uasActRun` submitting it, which
  can run at DISPATCH_LEVEL, where nothing may wait. The proof's rule that
  covers it - a built transfer is owned and so seen - is shown by the
  vectors and by stress (several programs with the copy loop), not by a
  rendezvous.
- **Control builds.** `xhciuas.sys`'s `qemu` flavour reads
  `XhciUasQemuTest` from the drive's device key (a qemu-only test value).
  Bit 0 opens the streams without the declaration, the non-declaring
  control; bit 1 leaves out the command phase, the UNIT ATTENTION loop's
  control. No release build reads it.
- **The recycle's failures.** Fault 34 (`22`; fault 19 reaches only a
  switch's Configure) fails RESET_PIPE's recycle Configure with Resource
  Error, or loses its completion (`arg` bit 4). Its port byte selects the
  endpoint (its DCI) and `arg` bits 3:0 the attempt (the Nth recycle), and
  `qemu.inj.recycle` reports both. It covers the partial
  two-endpoint recovery: the first endpoint recycled and the second
  failing, with the pump still closed and the device then reset.
- **Pause references and owners.** Every debt transition is noted with
  the object's pause count after it:
  - `cfg.debt.raise`: slot, DCI, why, the count;
  - `cfg.debt.release`: slot, DCI, whether it was a replay, the count.

  After every recovery, failed select, rollback, reattachment,
  CLOSE_STREAMS and OPEN_STREAMS, controller restart and standby, the notes
  are read in order. The count left must be the operation's own and the
  unrelated holders' alone, the debt on the expected object only, and no
  object freed with a reference counted on it. The snapshot's region
  layouts are unchanged.
- **A second DMA into a completed request, and what shows it.** None of
  the following measures the integrity of a completed buffer directly:
  - **The emulator's trigger log.** The model names every foreign Set TR
    Dequeue it loads into the current stream's cached state on QEMU's log
    (`xhci98-model: setdeq-current: loaded #N slot S ep E stream X into
    current stream Y at P`). Without that load the model cannot produce
    its corruption, at the next stop, recycle or reset included. BEFORE
    legs show loads. An ordinary AFTER leg on a contract endpoint must show
    none. The one permitted exception is the Error exit's command, which
    may log a load; it is safe only because the Drop follows before
    anything runs on the endpoint, and each one is noted
    `cfg.deq.errorexit`. This shows avoidance of the modelled mechanism
    only.
  - **`xfer.unmatched`**: an event whose TRB belongs to no TD queued at the
    time. A replayed TD usually posts one. The note is not proof either way:
    a reused TRB address can match a new TD; an event after an endpoint's
    retirement takes another route; and the named note keeps only the
    first eight of a start (the counter counts all).
  - **The capture vectors** test decisions and observe no DMA.

  Not detected by any of these:
  - an unmodelled controller fault;
  - a DMA that posts no useful event;
  - on real hardware, a DMA into a client buffer already returned
    (`XhciDmaStaging` 0, the field's setting).

  The last is the bench's question: the reporter's reports and the
  reference-file comparison there. A driver-side guard on a retained
  staging buffer is not built. It would cover only sampled staging
  buffers, never the field's staging-off client buffers, and against the
  modelled mechanism it adds nothing the trigger log does not show.
- **xhciuas's evidence and phase:**
  - FINAL with data outstanding;
  - a cancelled transfer that completes Success anyway;
  - an answered but unsuccessful task management function;
  - quarantine pending in a finishing slot;
  - the command phase's deadline before and after submission, an
    allocation failure, a transport failure, four UNIT ATTENTIONs and
    removal during the phase (the emulator's `usb-uas x-reset-ua`).
- **The host vectors** (`test_stream`, `test_uas`, and `test_sdeq`, which
  holds the thirteen captures as data) are to show the decisions:
  - on a contract endpoint, none of the captured sequences reaches a
    stream Set TR Dequeue, c2, a give-up, a cycle or a budget charge;
  - xhciuas's proof takes the recycle for every refusal and the reset for
    every status-endpoint recovery;
  - without the contract, the same sequences give 4.12.8's give-up and
    the hold, as `3.5.0.0` did.
- **The emulator legs** are to show the driver against a model of the
  fault (usb4-emu's `qemu-xhci x-stream-setdeq-current`), inside QEMU's
  own behaviour. Each leg's emulator build and its hash are recorded in
  the run sheet. The legs:
  - **On XP x64**, the field's system, BEFORE (fault 33 arg 2, the model
    on) and AFTER (fault 33 arg 1). Each runs HWMonitor 1.62, Speccy 1.32
    and HWiNFO64 6.43 with its SMART reading on, each started several
    times, alongside a copy-compare loop, a file held open on the drive and
    a reader of reference files;
  - the same AFTER set on 7 x64;
  - one leg each on 98 SE and on 2000, where xhciuas runs streams there;
  - **the Error exit**: a DROP of an endpoint brought to Error;
  - **two LUNs** on one drive (the command phase per LUN);
  - **a clear the device refuses** (`usb-uas` without `x-clear-halt`, or
    fault 13): RESET_PIPE fails into the device reset;
  - **the command phase disabled** (an xhciuas test build without it) with
    `x-reset-ua`: the UNIT ATTENTION loop shows, as the control;
  - **PnP removal of the drive while an endpoint owes**: the debt ends with
    the object, and nothing is left paused or published;
  - **the control**: a flagged controller with a non-declaring client (an
    xhciuas test build without the declaration), which must behave as
    `3.5.0.0`, the give-up form included;
  - **4.12.8's fault legs 1D, 1E and 1F** on endpoints without the
    contract, unchanged;
  - **two drives on one controller**, one declaring and one not;
  - **an unflagged controller**: behaviour as `3.5.0.0`.
- **Only the field controller can show** that the Intel controller, given
  no stream Set TR Dequeue and fresh contexts from the Drop and Add, no
  longer misplaces a stream, and whether its hidden state is discarded
  with the old context.

#### 4.12.11 The recovery's requests on memory made at the start (task 50.38, 2026-11-20)

**Standing.** Design before code, for roadmap task 50.38, by the owner's
choice of 2026-11-20: built apart from the release branch, its entry into
a release decided by the owner once the legs at the end of this section
had been read. Built and read on guests the same day (`17066cbe6`; "As
read" below, `runs/run-50.md`, "50.38 (continued)"). The owner's decision
on those readings was a recount with the `qemu` flavour's registry read
moved off the controller thread first, then the decision; that recount
was clean (below).

**What was read** (`runs/run-50.md`, "50.38"; runtime: the guest's memory
read over QMP as data, return addresses placed by this build's own
compiler listings, no closed code read). On 98 SE, in 50.20's legs (the
model on, `x-refuse-rw-every=64`), 4 of 52 counted runs stalled in mid-run
with a stream recovery unfinished. Two were read live (`s4v15`, `s4v23`)
and read the same, word for word:

- the refused READ's data-in transfer had been cancelled and the bus had
  done its part (the Stop Endpoint, the request completed cancelled, the
  endpoint held with its debt for the client's recycle, as section
  4.12.10 designs); xHCI98's controller thread was alive and idle;
- xhciuas's recovery worker was inside the allocation of the RESET_PIPE's
  IRP: `uasRecoveryWorker` -> `uasStreamRecover` -> `uasResetPipe` ->
  `UasSyncUrb` -> `uasInternalIoctl`, the return from `IoAllocateIrp`,
  with words below it that fit `ExAllocatePoolWithTag` (the pool tag
  `Irp `, the IRP's size `94h`) and addresses in the VMM's range beside a
  page's size; the RESET_PIPE never reached the bus;
- the drive's read Windows 98 waited for was held by that same recovery
  (the pump closed while `Recovering`), and the whole guest stopped.

What the kernel waited on there is not read. A third stall (`r98v1-2`)
fits the same picture from files alone; the fourth (`s2v03r`) is another
kind (the bus's thread did not act on the cancel) and is not this
section's.

**The change, in one line.** Every request the recovery worker sends the
bus, and the storage of its command phase, is made of memory xhciuas
allocates once, at the engine's first start, as it already allocates its
transfer IRPs; the worker reuses it and allocates nothing of its own.

**Every allocation on the worker's path**, from `uasRecoveryWorker` down,
as the source reads (`src/uas/uas_xport.c`, `uas_fdo.c`):

| Where | What, per use | After this change |
|---|---|---|
| `uasResetPipe` (the endpoint recycle) | the `_URB_PIPE_REQUEST` (`UasAlloc`) and its IRP (`IoAllocateIrp`, `uasInternalIoctl`) | the FDO's own URB and the recovery IRP |
| `uasResetDevice` (the device reset) | the IRP of `IOCTL_INTERNAL_USB_RESET_PORT` | the recovery IRP |
| `uasRecoveryPhase` (the command phase, each TEST UNIT READY) | the `UAS_ICMD`, its SRB (`UasAlloc` twice) and its carrier IRP (`IoAllocateIrp(1)`); no data buffer, no MDL (no data phase) | the recovery command |
| `uasCycleOwed` (45.41 L5's cycle) | the IRP of `IOCTL_XHCI98_RESUME_STATE` (op CYCLE; its argument on the worker's stack), then, when the bus answers the op invalid, the IRP of `IOCTL_INTERNAL_USB_CYCLE_PORT` | the recovery IRP, the one after the other |

Nothing else on that path allocates. `uasTmf` (ABORT TASK, LOGICAL UNIT
RESET) waits on an event on its stack and sends on the slot's transfer
IRPs, made at the start; `uasAbortAll`, `uasEndLun` and `uasEndPdoSlots`
cancel with `IoCancelIrp`, whose target in the bus marks and wakes the
controller thread (`hcd_io.c`, `hcdCancel`); `uasWaitAllFree`,
`uasGiveUp`, `uasCloseLun` and `uasRecoveryRequest` allocate nothing; the
worker's work item is the FDO's own.

Inside the calls, as far as their source shows: the bus allocates no pool
at the four requests' arrival - RESET_PIPE and RESET_PORT are pended to
its thread (`HcdCfgQueue`, `hcd_cfg.c`), CYCLE_PORT and RESUME_STATE
answered and completed before the dispatch returns (`hcdCyclePort`,
`hcdResumeStateIoctl`, `hcd_urb.c`) - and the thread's recycle and the
RESET_PORT restore reuse the device's pipe records (`hcdCfgReset`,
`hcdCfgRestore`, `hcdCfgReplayConfig`, `hcdCfgStreamsRestart`); the
bus's pool sites for pipes and streams are the selects' and the streams
open's alone (`hcdCfgPipeNew`, `hcdCfgStreamsBuild`). The kernel's own
calls on the path - `IoInitializeIrp`, `IoCallDriver`, `IoCancelIrp`,
`IoCompleteRequest` (the bus's, and the worker's for a held class SRB),
`KeInitializeEvent`, `KeSetEvent`, `KeWaitForSingleObject`,
`KeDelayExecutionThread`, `KeQuerySystemTime` - name no allocation in
the DDK's contract; their code is closed and not read.

Left as they are, and why:

- **The worker's closing `uasPump`**, which starts the class requests the
  recovery held: each data command takes its MDL (`UasDataMdl`,
  `IoAllocateMdl`), as every command does on every path that pumps. It is
  every transfer's allocation, not the recovery's; whether a stall can
  move there is what the count below reads.
- **The completion of a held class request** (`uasRecoveryRequest`,
  `uasFinish`): what the class driver above does in its completion
  routine is its own.
- **The sleep phase and the wake check** (45.23, design record 20 section
  4.3.3): their resume requests (`uasResumeRequest`) and the wake check's
  CYCLE_PORT keep an IRP allocated per request, and its identity reads
  are not recovery commands, so they allocate as today. They are not
  excluded from a recovery throughout - the sleep phase lifts the seal
  before its WITHDRAW, the wake check before its medium check, and
  lifting it can queue the worker at once (`uasUnseal`) - and they need
  not be: they share none of the set, which only the worker's call tree
  touches (below).
- **The start's and the stop's own requests** (descriptors, the
  selection, the streams open and close, REPORT LUNS and INQUIRY): a
  failure there is the start's.

**The set**, made in `UasEngineInit` beside the transfer IRPs - at an
FDO's first start, kept across stops, freed by `UasEngineFree` at its
removal or when the init itself fails:

- `RecoveryIrp`: an IRP in the driver's own pool (`UasIrpAlloc`), sized
  for `fdo->Lower->StackSize` as read then (`RecoveryIrpStack`,
  `RecoveryIrpSize` kept beside it);
- `RecoveryUrb`: a `struct _URB_PIPE_REQUEST` inside the FDO's extension,
  which is nonpaged and lives as long as the FDO: no allocation at all;
- the recovery command: one `UAS_ICMD`, its SRB and its carrier IRP (an
  own-pool IRP of one stack location: the carrier is queued, never
  sent), with `RecoveryIcmdBusy` under the FDO lock.

A start that cannot make them fails as a start that cannot make its
slots does today: `STATUS_INSUFFICIENT_RESOURCES`, through `UasEngineInit`'s
own cleanup (`UasFree` of what was made; a NULL is passed over), then
`uasFdoQuiesce`. The next start makes the engine again (`Slots` NULL).

**Its use.**

- `UasRecoveryIoctl(fdo, code, arg1)` (`uas_fdo.c`, PASSIVE_LEVEL, the
  worker only): `uasInternalIoctl`'s steps on `RecoveryIrp` - the IRP
  initialised (`UasIrpReset`, `IoInitializeIrp`, with the stack count it
  was made for), its status preset, the next stack location filled, the
  completion `uasSignal` (it sets an event on the worker's stack and
  returns `STATUS_MORE_PROCESSING_REQUIRED`), `IoCallDriver`,
  `UasWaitEvent`, the status read - and nothing freed.
- `uasResetPipe` fills `RecoveryUrb` (zeroed, then the fields of today)
  and sends it with `UasRecoveryIoctl` (`IOCTL_INTERNAL_USB_SUBMIT_URB`;
  the URB's own status checked as `UasSyncUrb` checks it).
- `uasResetDevice` sends RESET_PORT, and `uasCycleOwed` RESUME_STATE and
  its CYCLE_PORT fallback, with `UasRecoveryIoctl`. Their outcomes are
  read as today; `STATUS_INSUFFICIENT_RESOURCES` no longer comes from
  them, so their "not sent" and "stays owed" branches remain for a bus
  that answers that status itself.
- `uasInternalCommandAs` with `recovery` TRUE (the command phase's
  commands, and nothing else; every other internal command allocates as
  today) takes the recovery command (`uasIcmdTake`):
  - **taken** under the FDO lock when it is not busy, busy then set; when
    it is busy, `STATUS_INSUFFICIENT_RESOURCES`, the outcome an
    allocation failure gives the phase today (`UAS_RP_A_RESOURCE`, tried
    once more after 100 ms);
  - **made ready** as a fresh allocation is: the record cleared
    (`UasZero`, as today's allocation is), its SRB, its carrier and the
    carrier's size put back with the `Own` mark, and the carrier
    initialised (`UasIrpReset`, one location: no MDL, no cancel routine,
    every DriverContext slot 0). The steps that follow are today's: the
    event initialised, `State` WAITING, `Recovery` set, the SRB cleared
    and filled; `uasAdmit` writes the carrier's four DriverContext slots
    and the queue its list entry. The phase sends TEST UNIT READY alone,
    with no data: a recovery command with a data length is refused
    `STATUS_INVALID_PARAMETER` before anything is taken, so the record
    needs no buffer;
  - **given back** - busy cleared under the FDO lock, nothing freed - at
    every place where today's command is freed: admission refused (the
    engine stopped, or the FDO Dead or gone, between the taking and the
    queueing), the waiter once the command is DONE, and the finish of a
    command its waiter abandoned (`uasIcmdDone`).

**The stack size.** `fdo->Lower` is the device object this FDO attached
to at `AddDevice`, and its `StackSize` is fixed from then on: a later
attachment lands on top of the stack, above this FDO. The transfer IRPs
already rest on that. Should `Lower->StackSize` ever read larger than
`RecoveryIrpStack`, `UasRecoveryIoctl` sends the request through
`uasInternalIoctl` instead, with an IRP allocated as today, so an IRP
short of stack locations is never sent.

**What serialises it.** One worker using the set at a time, and every
user of the set on its call tree:

- The FDO has one work item, `fdo->Recovery`, queued only by a caller to
  which `uasWantRecovery` answered TRUE. That sets `RecoveryQueued` under
  the FDO lock, and only the worker clears it, in the same hold that ends
  its pass (`UAS_RECOVER_NONE`). `UasEngineStart` resets it only after a
  stop whose rundown has waited for the worker's reference (`IoCount`),
  or at a first start. A second worker can be queued once the first has
  cleared it, while the first still fails its drained requests, pumps and
  drops its reference; that tail uses nothing of the set, so the set has
  one user at a time.
- `uasResetPipe` is called only by `uasStreamRecover`; `uasResetDevice`
  only by `uasStreamRecover`, `uasResetLun`, `uasRecoveryRequest` and the
  worker's own switch; `uasCycleOwed` only by `uasResetDevice`,
  `uasGiveUp` and the worker's entry; `uasRecoveryPhase` only by
  `uasResetDevice` - every one of them reached from `uasRecoveryWorker`
  alone.
- Each use is synchronous. The IRP is made ready again only after
  `UasWaitEvent` has returned, that is after its completion routine has
  set the event; the routine touches nothing after `KeSetEvent` and
  returns `STATUS_MORE_PROCESSING_REQUIRED`, after which the I/O manager
  does not touch the IRP again - the contract the transfer and telemetry
  IRPs and today's synchronous requests already rest on.
- The recovery command is busy from its taking to its return, both under
  the FDO lock. A phase command still owned when its waiter returns is
  one whose deadline passed: that is `UAS_RP_FAILED`, the phase ends,
  `uasResetDevice` marks the FDO Dead, and a Dead FDO runs no phase again
  until a start - which follows a stop whose rundown has finished every
  command, and so returned the storage. The busy answer is not reached in
  that order; if it were, it is a resource failure, never a second
  command on the same storage.

**Cancel and timeout.** None, as today. The worker's requests are waited
for with no time-out and never cancelled; the bus completes RESET_PIPE and
RESET_PORT from its thread, CYCLE_PORT and RESUME_STATE inline, and
everything it holds by the time it tears the device down (the engine's
rundown rule, `uas_xport.c`, "RUNDOWN"). This change adds no wait and
takes none away.

**Stop, surprise removal and removal during a recovery.**
`UasEngineStop` returns only once the worker has dropped its reference
(`IoCount`), at its last line, so every stop, surprise removal or removal
waits for the request in flight to complete, as it does today (50.20's
leg 2: the stop waited for the recovery and then completed, `S98a`,
`L2a-2kf`). The set is freed only by `UasEngineFree` in `uasFdoRemove`,
after `uasFdoQuiesce`. A worker that finds the engine stopped between two
requests ends its pass as today.

**Imports.** None new. The IRP and the command's storage come from
`UasIrpAlloc` and `UasAlloc` and are reinitialised by `UasIrpReset`
(`ExAllocatePoolWithTag`, `IoInitializeIrp`: `SITES=uas_mem.obj`,
`xhciuas-imports.allow` and its amd64 twin); `IoCallDriver`,
`KeInitializeEvent`, `KeSetEvent`, `KeWaitForSingleObject` and
`ExFreePool` are imported already. `IoReuseIrp` is not used (design
record 13 Appendix A row 27). The import gate passes with no new row.

**Each target.**

- **Every target** (98 SE, ME, 2000 to 10, x86 and x64), and every UAS
  device, streamed or not, on a contract or not - the engine is the
  same:
  - **a start needs four more allocations** (the IRP, the command, its
    SRB and its carrier, about 400 bytes on x86), so a start in
    nonpaged pool exhaustion can now fail where it would have gone on,
    even for a device that never needs a recovery; it fails as a start
    whose slots cannot be made already does;
  - **a recovery no longer meets an allocation failure on these
    requests**: where today a recycle not sent became the device reset, a
    RESET_PORT not sent left the device Dead with its cycle owed, a cycle
    not sent stayed owed and was asked again each second, and a phase
    command not made was a resource answer, the recovery now takes the
    path it takes with memory. That differs from today where one of
    these allocations would have failed: nonpaged pool exhausted, or
    Driver Verifier's low resources simulation (2000 onward), which then
    finds these allocations at the start alone and none at a recovery;
  - **and, with no allocation failing, in its timing**: each phase
    command's five-second deadline starts before the command is made
    (`uasRecoveryPhase`), so today's three allocations spend part of it.
    An allocation that succeeds but slowly can leave today's command
    abandoned at its deadline, with the device Dead and the cycle asked,
    where the command made at the start is answered in time. That is
    read from the source, not observed; the same holds for the requests'
    IRPs, whose waits have no deadline, only in that the recovery's steps
    no longer wait on an allocation at all.
- **98 SE and ME** (NTKERN's allocator, which both share): the
  allocations both captures implicate are no longer made on the
  recovery's path. ME was not in the count; what is said here of 98 SE
  is said of ME by that shared allocator, not by a reading.
- **2000 to 10.** The DDK gives nonpaged pool allocation and
  `IoAllocateIrp` at up to DISPATCH_LEVEL, with failure as a possible
  outcome; whether they can wait inside is neither documented there nor
  read. What is measured is that none of 50.20's 2000, XP x64 or 7 x64
  runs stalled. Further on these systems:
  - with two processors the completion routine can run on the other
    one: the IRP is touched again only after the event wait has
    returned, as above;
  - on x64 the IRP's size comes from its stack count, as for the
    transfer IRPs, and the URB in the extension has the architecture's
    own layout.

**Test hooks** (`qemu` flavour only; `XhciUasQemuTest`, the drive's device
key, read at each start, never by a release build; bits 0 and 1 are
50.20's):

- bit 2 (`UAS_QT_PHASE_FAILS`): the command phase's commands run, then
  its outcome is taken as no device answer, so the device is Dead and the
  cycle asked (RESET_PORT, the phase's commands and RESUME_STATE, one
  after the other on the set);
- bit 3 (`UAS_QT_CYCLE_UNKNOWN`): the cycle asked with an op the bus does
  not know, which it answers invalid as an older bus answers the cycle,
  so the CYCLE_PORT fallback follows on the same IRP;
- bit 4 (`UAS_QT_NO_RECOVERY_SET`): the start's last allocation of the
  set, the command's carrier, taken as failed after the others were made,
  so the init's cleanup frees a set made in part, and the start fails;
- bit 5 (`UAS_QT_PHASE_ABANDON`): each LUN's first phase command after a
  port reset is marked abandoned in the same hold that queues it, before
  the pump can start it, so its finish - whenever the device answers it -
  is the abandoned command's, which gives the storage back
  (`uasIcmdDone`); the waiter returns at once with the resource answer,
  and the phase tries again after 100 ms (`UAS_RP_RETRY`). That retry
  finds the command only if the abandoned command's finish has given it
  back by then; a second resource answer is the device that did not come
  back (Dead, the cycle).

**Legs, as planned** (what was read is "As read" below). What shows that
a request went on the set:
the bus's per-URB log (`XhciQemuStageFault` 8) prints each URB's IRP, and
the FDO's `RecoveryIrp` field, read once over QMP as data (the extension
found from the bus's PDO's `AttachedDevice`, the field's offset from this
build's compiler listing), names the IRP the log shows for each of that
FDO's RESET_PIPEs. A repeated address in the log alone shows nothing:
today's allocation and free give one address most of the time (`s4v23`:
one IRP for 32 of its 35 RESET_PIPEs).

- **The count.** The 98 SE harness of 50.38 unchanged (value 1, fault 33
  arg 1, the model on, `x-refuse-rw-every=64`, the live reading at any
  stall), with xhciuas swapped for this change's `qemu` build and the bus
  as counted (`779c6a5d2`; xhciuas has no other change since): the
  plan, 50 counted runs with no stall of the kind read above. At the rate of the
  count before (that kind in 3 of 52), 50 runs with none happen by
  chance about once in 20; at the rate of any stall (4 in 52), about once
  in 55. A stall of the other kind in the count is read, not taken for
  this change failing.
- **The paths beyond the recycle**, on 98 SE, on 2000 (x86) and on XP
  x64 (amd64). Fault 34 (a recycle Configure refused): RESET_PIPE fails
  into the device reset - RESET_PORT pended in the bus, then the phase's
  TEST UNIT READY on the recovery command, again at the next fault; bit
  2, then bits 2 and 3: RESUME_STATE's CYCLE, then its CYCLE_PORT
  fallback, each completed inline, on the IRP the RESET_PIPE and
  RESET_PORT used; bit 5: the drive kept after each device reset, and the
  copies and compares going on, show that every abandoned command's
  finish gave the storage back and the retry ran on it (the bus's log
  shows the two commands after the RESET_PORT); a device reset ending
  Dead with the cycle under bit 5 means the storage had not come back
  within 100 ms, and that run is repeated before anything is read from
  it.
- **A failed start** (bit 4): the start fails, the device shows its
  failure, and with the bit cleared the next start runs.
- **A stop and a removal during a recovery**: 50.20's leg 2 shape (fault
  35 point 2 holding the bus's RESET_PIPE, the worker's request pending
  on the set) on 98 SE and on 2000. The bus has no rendezvous inside the
  phase, so a stop while the phase's command is in the device is not
  driven; the rundown argument above covers it, and bit 5's leg reads
  the late finish against the removal that follows.
- **Regression**: 50.20's recycle legs on 2000 (`r2kv1`) and XP x64
  (the `XA1`/`XS1` configuration), x86 and amd64 builds: every refusal
  recycled, every compare equal.

**As read** (2026-11-20, `runs/run-50.md`, "50.38 (continued)"; runtime:
the bus's logs, QEMU's trace, and guest memory read over QMP as data):

- **The count ended by its rule at 41 counted runs.** The rule, written
  before the first run, ends it at a stall read as the recovery worker
  inside an allocation or waiting on a request to the bus it never got
  back. In the 41st, `s5v47`, the worker waited on its RESET_PIPE, which
  the bus had received on the recovery IRP and did not serve, its
  controller thread being stuck in the `qemu` flavour's once-a-second
  registry read. Nine runs that ran after it are reported apart.
- **No allocation stall** in the 41 nor in the nine after them; every
  RESET_PIPE of an FDO named one IRP, the FDO's `RecoveryIrp` where the
  field was read (`s5v01`, LA, LD). At a fixed rate of 3 in 52 a run (the
  count before: 2 read live, 1 compatible), 41 runs with none happen by
  chance about once in 11, 50 about once in 20.
- **Five stalls**, 4 in the 41 counted runs and 1 after them: four read
  live and placed, all with xHCI98's controller thread inside the `qemu`
  flavour's registry read (`HcdInjPoll` -> `HcdCtlQemuReadDword`) and not
  returning; one not placed (its QEMU ended before the reading). The guest stalled about as often as before
  this change, so the count could not say whether a release build, which
  makes no such read, still stalls.
- **Read on the set:** on 98 SE the device reset (fault 34), the
  abandoned command and its retry (bit 5), the cycle by RESUME_STATE (bit
  2) and the CYCLE_PORT fallback (bits 2 and 3; after each cycle the 98
  SE guest stopped, as 50.20's control runs did at their cycles), a
  failed start (bit 4), the stop and a surprise removal during a held
  RESET_PIPE; on XP x64 (amd64) the recycle, the device reset, bit 5 with
  two resets of one FDO, and the fallback (with one copy round failed and
  a Delayed Write Failed warning in that leg, no cause assigned); on 2000
  (x86) the recycle
  (with one device reset the recovery chose itself) and the stop during
  a held RESET_PIPE. No leg used the monitoring programs: the XP x64 legs
  took `x-refuse-rw-every=64` as their trigger.
- **Owed:** on 98 SE a second device reset of one FDO (the phase again on
  the same storage); on 2000 the device reset, the cycle, the fallback
  and bit 5; on XP x64 the cycle by RESUME_STATE alone and a stop during
  a recovery; a stop while the phase's command is in the device, which no
  rendezvous holds (the rundown argument above covers it).
- **The recount with both changes** (2026-11-20, roadmap task 50.38 on
  branch `w50-qpoll`, `a9c4ec6a6`: the `qemu` flavour's registry read
  moved off the controller thread; `runs/run-50.md`, its "50.38 (kind
  A)"): the same 98 SE harness and stop rule, this change's `qemu`
  xhciuas (`ECB4C24B...`) with that branch's `qemu` xhci98
  (`4A235EAC...`): 50 counted runs, 50 ended, no stall of either kind.
  With both changes in one build it does not separate their
  contributions, and it reads the `qemu` test build in the emulated rig,
  not a release build.

**What it leaves.** The other kind of stall (`s2v03r`); the class
requests' MDLs the worker's closing pump allocates; the sleep and wake
paths' requests; and whatever the kernel does inside the calls that
remain. It removes the allocation both captures stopped in, and the
recovery's other per-use allocations; it cannot promise more.

### 4.13 Releasing the transfers kept by a pinned containment (deferred, PCR.1)

**Deferred by the owner on 2026-10-13 to after `2.4.0.0`** ("Planned
work", PCR.1 again; it was roadmap-hcd task 38.3). Nothing here is built,
and the known limitation stands as the release notes state it: disabling
a controller the driver closed off without proof that DMA stopped waits
for ever on the transfers it keeps. Why it was deferred:

- **Proof (b), D3hot asked for through the PCI stack and read back, was
  dropped.** It is safe only if a later return to D0 resets the
  controller, which PMCSR's `No_Soft_Reset` set prevents (xHCI 1.2c
  5.2.7.1's note, p. 344; 4.23.1, p. 312; A.1.1, p. 564). `XHCIQUAL`'s probe
  reads it set on every Intel PCH xHCI read so far - `8086:1E31` (B490),
  `8086:9CB1` (E7250), `8086:9D2F` (E460, HP EliteBook 850 G5), `8086:02ED`
  (P14s Gen 1) - and on AMD's chipset controller `1022:43F7` (the B650M);
  clear only on AMD's processor controllers `1022:15B6`, `15B7`, `15B8`
  and Intel's Thunderbolt `8086:15DB` (runtime, `xhciqual/results/`, held
  on the maintainer's disk). And QEMU's `qemu-xhci` has no PCI Power
  Management capability (runtime, `lessons.md`, "PME# cannot be reached in
  this vehicle"), so no leg could read it.
- **Proof (a), configuration space reading all ones, was shown by the
  review not to prove isolation.** All ones shows that configuration
  access failed, not why. Two counterexamples: a faulty firmware or bridge
  restore that closes the downstream configuration and memory window while
  upstream bus mastering stays enabled; and an endpoint whose inbound path
  is broken while its DMA engine still writes. A good ID read at the start
  excludes neither. `hcdContainProve` (`hcd_ctl.c`) already refuses all
  ones as proof for the same reason, and the task's own condition says an
  unreachable configuration window alone proves nothing.
- **The only safe evidence is a real PnP removal** - the bus driver
  reporting the function gone - **and a disable never gives one**, so no
  proof reachable on a disable would release anything. (Qualified on
  2026-10-25, section 4.13.5: a removal notice alone is not enough;
  `pci.sys` reports one on a vendor id of all ones, so the isolation
  or reset behind a removal must itself be established.)

#### 4.13.1 The problem, as read

Since `2.2.0.0` a containment without proof (section 4.6: all ones
without a Bus Master Enable read back clear, or an invalidation no halt or
HCRST proved) pins the common buffer and keeps every transfer and mapping
(`HcdIoDeviceDrain` returns 0 while `CommonBufferPinned` is set,
`hcd_io.c`). The wait is in the class driver above our PDO, at its own
REMOVE: our PDO does not wait on a pinned buffer (`hcdPdoQuiesce`,
`hcd_pdo.c`).

- Windows 2000: a controller disable hung 4 of 4 from fresh boots; the
  HID mouse's REMOVE waits on its kept interrupt-IN read. Windows 98 SE:
  the disable hung after the controller's QUERY_STOP with a suspended
  client, and completed with no client (runtime, `runs/run-36.md`, "A-98
  and A-2000 fault legs").
- A disable reaches the controller as `QUERY_STOP` then `STOP` on Windows
  98 SE and as `QUERY_REMOVE` then `REMOVE` on Windows 2000, whose remove
  unloads the image (runtime, `lessons.md`, 26-A.2). ME, XP, Vista and 7
  are not read.

#### 4.13.2 The owner's answers (2026-10-13), kept for a reopening

1. **Trigger**: the controller FDO's own `QUERY_STOP`, `QUERY_REMOVE`,
   `STOP`, `SURPRISE_REMOVAL` and `REMOVE` only, not a child PDO's; a leg
   reads the PnP order per target, and a target where a child waits first
   goes back to the owner.
2. **Stalled reads**: the release's own configuration reads bounded at
   1 s, the context leaked safely, ending with no proof; other callers of
   `HcdSvcConfigSpace` unchanged.
3. **Proof (a)**: the whole 64-byte header all ones in two complete
   paired reads at least 20 ms apart, USBSTS all ones, and the start's ID
   read through the same route. Overtaken by the review (above).
4. **Proof (b)**: dropped, with the D3 request, the power IRP and every
   PMCSR and PM capability reading.
5. **The record** (decision 7 of the draft): in memory and the log only;
   no registry value. Deleting the FDO loses it.
6. **A start-time check** (open): leaning to variant B - the start's own
   halt and HCRST, then carry on, counted and logged - as a diagnostic
   only, within one FDO's life (9x disable and enable, an NT rebalance),
   never as the safety foundation; safety must hold before anything is
   released. The start does not restore Bus Master Enable before its halt
   and HCRST (`XhciInitController`, `xhci_init.c`: preflight read, halt,
   HCRST, restore last), but PCI START has already run in `hcdStart`.
7. **A buffer pinned by the stop's own quiesce** (`XhciFailClosedDma`,
   not a containment): out of scope.

#### 4.13.3 The design as far as it went

What a future implementer can keep, whatever proof is found:

- **Reading configuration space so a failed read never looks like all
  ones.** The proven route (`IRP_MN_READ_CONFIG` to the `AddDevice` PDO,
  `HcdSvcConfigSpace`) zero-fills and judges the status alone;
  `IoStatus.Information` is unproven per target (it passed as the length
  on Windows 2000 and was refused on 98 SE when judged, 26-A.2). The paired
  read - each dword twice, into buffers prefilled `0x5A` and `0xA5`, a byte
  counted only if both halves agree - establishes which bytes the bus
  driver wrote without relying on the byte count.
- **The trigger**: in the controller's PnP dispatch before the IRP goes
  down and before `HcdStopController`; at a REMOVE, at the head of
  `hcdRemove`, before admission closes.
- **The release, once proven**: clear `CommonBufferPinned` for the current
  allocation, then drop every device through the existing departure
  (`hcdDropAll`, as `HcdEnumContain`'s proven branch) under the power
  gate, as `HcdIoShutFlush` drains from the power path; selective
  suspend's quarantine (`hcd_susp.c`) then runs down, which run-36's 98 SE
  hang needs; the stop frees the buffer, stream blocks and wide pool, and
  must not reach MMIO and `XhciFailClosedDma` again. The map-register wait
  of section 4.6's known limitation ends once the kept transfers give
  their registers back.
- **Injection and legs** the draft proposed: an injected proof on top of
  fault 07, a stalled read, the start check; the roadmap's leg (a disable
  on 98 SE, ME, 2000, XP and 7 x64, with and without a client read
  pending), a suspended client on 98 SE, the PnP order per target, and the
  unproven case still holding. Commits `9e70590b` and `66df2a2d` hold both
  drafts in full.

#### 4.13.4 What a reopening must answer

Beyond a proof that establishes isolation (above), the other points
raised on the draft:

- **An image unload under a late completion.** Dropping the last FDO
  reference inside the completion routine leaves driver code running
  after the protection goes; on 9x the protection is not established
  (`hcdRemove`; `lessons.md`, the "signalled but still executing" unload).
  An interlock must cover the routine's return, or an abandoned request
  keeps the image for good.
- **A later trigger on torn-down resources.** A `SURPRISE_REMOVAL` with no
  proof leaves `CommonBufferPinned` and `Contained` set while `HcdDmaClose`
  drops the pointers and `hcdRelease` unmaps `BarVa`; a following `REMOVE`
  would read USBSTS through stale state. Eligibility must be bound to a
  live allocation and start generation and end before teardown.
- **Injection that frees memory the emulated controller still names.**
  Fault 07 does not halt QEMU's controller (`hcdContainProve` clears the
  real Bus Master Enable before `HcdInjPciCommand` falsifies the readback),
  and PCI START may restore it before the driver's halt; an injected proof
  must first bring about a real halt and reset, or real removal.
- **The original requests must complete.** The reused drain parks them
  (`HcdIoDeferred` to `HcdIoPark`, `hcd_io.c`), completed only on cancel,
  abort or PDO closure; a release must choose and specify their
  completion, and count completions apart from DMA retirement.
- **Timing as a contract.** Two relative 10 ms waits do not promise
  20 ms on these targets, a 1 ms poll does not define a 1 s deadline, and
  polling cannot bound a lower driver that blocks inside `IoCallDriver`.
- **The start's ID is not a validated reading.** `ext->PciVendorDevice` is
  zeroed and read without a completeness check, and a reinitialization can
  overwrite it; a proof leaning on it needs a validated result tied to the
  start generation.
- **The legs.** A route to `REMOVE` for the unload leg (a 9x disable gives
  a `STOP`), ME in it, every target's PnP order rather than five, and the
  sequences `SURPRISE_REMOVAL` then `REMOVE` with no proof and a release, a
  cancelled query and a later stop.

#### 4.13.5 The proof routes read for roadmap-usb4 task 44.33 (2026-10-25)

Task 44.33 is research only, with no code. It looked for a proof route
this section had not found, on 98 SE and 2000. The verdict above still
stands: nothing here releases a kept transfer. One route, the parent
bridge, is the only one left that might; it is set out last, with what a
reopening would need.

**What a route must give.** A containment pins only when the driver's
own proof has failed (section 4.6). That proof is Bus Master Enable
cleared and read back clear. It fails when configuration space reads all
ones, when the read itself fails, or when the bit will not clear. So a
route counts only if it meets three conditions:

- It rests on evidence other than the reading that failed: another
  device's, or the function's own once a validated read shows that the
  function answers again. That a failure at containment leaves the
  function silent for good is not established either way.
- It gives that evidence before a class driver's teardown waits on a
  kept transfer.
- It keeps the controller from mastering through any later return to
  service until HCRST has completed. This is the condition section 4.13
  sets for D3hot.

**Route 1: what Windows 2000 SP4's `pci.sys` does at the function's stop
or remove.** Read statically from version 5.00.2195.6655
(`legal-provenance.md` section 4):

- `STOP` clears IO, Memory and Bus Master Enable (`Command & 0xFFF8`),
  written only if the value changed and never read back. It does not
  enter D3.
- `REMOVE` and `SURPRISE_REMOVAL` clear the same three bits and then
  enter D3 through the PM capability. They wait 10 ms, read PMCSR's
  PowerState back and retry up to 100 times at 1 ms, then ignore the
  result. Both steps are skipped when the PDO is marked not present.
- `QUERY_REMOVE` and `QUERY_STOP` touch no hardware.
- `START` sets Bus Master Enable before the function driver's start
  runs.
- The return to D0 rewrites the configuration header and puts back the
  Command bits saved before D3. No code tests `No_Soft_Reset`.
- At a bus rescan, a child is reported missing when its vendor id reads
  `0xFFFF` or 0, or its identity has changed.
- No child path touches the parent bridge.

It fails on every count:

- **Too late.** On 2000 a disable sends the controller `QUERY_REMOVE`,
  which touches nothing. The PDO's `REMOVE` comes only after the
  children's REMOVEs, and those are the requests that wait on the kept
  transfers (section 4.13.1).
- **No evidence.** The only read-back, PMCSR, goes through the same
  configuration space whose failure made the pin.
- **The restart is unsafe.** `START` sets Bus Master Enable before this
  driver's halt and HCRST can run.
- **"A real PnP removal" on 2000 is weaker than it sounds.** Such a
  removal rests on a vendor id of `0xFFFF`, the reading this section
  already found proves no isolation. After it, `pci.sys` writes nothing
  to the device. So the removal adds nothing beyond its own trigger.
  Whether a removal reported by another bus driver (ACPI eject, a
  Thunderbolt connection manager) carries stronger evidence was not
  read.

**Route 2: what Windows 98 SE's `PCI.VXD` does at the devnode's stop or
remove.** Read statically from version 4.10.2222:

- `CONFIG_STOP`, `CONFIG_REMOVE` and an APM set-D3 write `Command &
  0xFFF8` as a dword with Status 0, but only when an allocated log
  configuration exists.
- They read back the decode bits alone; Bus Master Enable is never read
  back.
- Where a PM capability exists, they then enter D3, wait 10 ms and poll
  PowerState, returning `CR_FAILURE` on a timeout.
- Configuration space that reads all ones gets the write sent into the
  void and a success returned.
- `START` and the D0 path set Bus Master Enable.
- No child path touches the parent bridge.

The order is no better than 2000's. The Windows 98 DDK's `configmg.h`
says it in the comment above the `CONFIG_*` codes (lines 1400 to 1440 of
`tools\win98ddk-extracted\all\INCS_DDK\INC_WIN98_CONFIGMG.H`):

- `CONFIG_STOP` goes to "each devnode, branch going up".
- `CONFIG_REMOVE` goes to "each sub tree node, doing branch going up".

So the controller's own handler (NTKERN, then this driver's `STOP`) and
its children come before the parent's enumerator. `PCI.VXD` acts last,
after the client's wait has begun (section 4.13.1: run-36's 98 SE hang
followed the controller's `QUERY_STOP`). This is the header's word,
not a reading of `CONFIGMG.VXD`.

**Route 3: the xHCI specification's own guarantees.**

- HCRST resets the controller's "internal pipelines, timers, counters,
  state machines". It leaves "PCI Configuration registers ... not
  affected". And "software shall not set this bit to '1' when the
  HCHalted (HCH) bit ... is a '0'" (xHCI 1.2c 5.4.1, USBCMD bit 1).
- Section 4.21.2 asks that the controller be halted, HCH read 1, before
  Bus Master Enable is cleared. It warns that clearing it on a running
  controller may raise HCE.

Both guarantees need a register window that decodes, and both need the
halt that failed on the unproven-invalidation branch. Section 4.6
already uses what they give: HCH on a decoding window (revision 11) and
HCRST at a recovery or start. They add nothing on the branches that
pin.

**Route 4: an IOMMU.** These readings establish no usable route. A
byte search for the DMA remapping table's signature `DMAR` found none
in:

- Windows 2000 SP4's eight HALs, `ntoskrnl.exe`, `ntkrnlmp.exe` or
  `acpi.sys`;
- Windows 98 SE's `acpi.sys`, `ntkern.vxd`, `PCI.VXD` or
  `CONFIGMG.VXD`.

`DSDT` is found in both `acpi.sys` builds, so the search sees a table
signature compiled into these files. 98 SE's `VMM32.VXD` on the CD is
compressed and was not searched. That is a narrow static result: no
file read names the table. It does not exclude a signature built at run
time, another component, another kind of remapping unit, or firmware
that leaves one enabled, none of which was read.

**Route 5: Function Level Reset.** It is a write to the function's own
configuration space, so it can serve only once that space answers again
(the first condition). The 8 Series PCH datasheet lists that PCH's xHCI
function's capabilities as PCI Power Management and MSI (section 16.1,
`PWR_CAPID` to `MSI_MCTL`), with no FLR capability. Whether AMD's,
Thunderbolt's or ASMedia's controllers advertise FLR was not read, nor
what a completed FLR would prove there.

**Route 6: the parent bridge.** This is the only route read that meets
the first condition without waiting for the function to answer again. It applies to a controller behind a bridge. Two
readings of the bridge would serve; both are reads of a device whose
configuration space did not fail:

- **(b1) The bridge's own Bus Master Enable, cleared and read back
  clear.** The 8 Series PCH datasheet says of its root ports' `PCICMD`
  bit 2: "0 = Disable. Memory and I/O requests received at a Root Port
  must be handled as Unsupported Requests" (19.1.3, p. 681). Once that
  holds, a new request from below is refused, whatever state the
  controller is in. It says nothing of writes the bridge accepted
  before the bit cleared: that they have reached memory by the time the
  read-back completes is not established, and a reopening must find an
  ordering or drain guarantee before anything is released. Only a bridge whose own reading is validated
  counts: its vendor and device id read back as expected, through the
  paired read of section 4.13.3.
- **(b2) The link to the controller down.** The datasheet says of the
  root port's `LSTS` bit 13, "0 = Data Link Control and Management
  State Machine is not in the DL_Active state" (19.1.30, p. 695). It is
  meaningful only where Link Active Reporting Capable is set, which
  those root ports hardwire to 1. Its reading needs no write. **It does
  not meet the third condition alone**: it fences nothing and resets
  nothing, and the link can come back before this driver has run HCRST.
  It can serve only beside (b1), as further evidence.

That a link not in DL_Active carries no requests from below is the PCI
Express Base Specification's rule; it is not in `docs/references`.
Bridges other than Intel's root ports (AMD's internal bridges,
Thunderbolt downstream ports, PCIe switches) are not read here.

Where it reaches:

- `XHCIQUAL`'s readings place every Intel PCH controller read so far on
  bus 0 at `00:14.0`: `8086:1E31`, `9CB1`, `9D2F`, `02ED`. These are
  root-complex integrated, with no bridge above them, and this route
  does nothing for them.
- The controllers on other buses are behind a bridge:
  - AMD's `1022:15B6`, `15B7` and `15B8` (`0D:00.3`, `0D:00.4`,
    `0E:00.0`; `7B:00.3`, `7B:00.4`, `7C:00.0`);
  - AMD's chipset `1022:43F7` (`0A:00.0`);
  - Intel's Thunderbolt `8086:15DB` (`3B:00.0`);
  - an ASM4242's `1B21:2426` (`79:00.0`).

  (Runtime, `xhciqual/results/`, held on the maintainer's disk.)
- A tunnelled controller lost with its tunnel is the likeliest pin of
  all. The bridge above it stays on the host, whose link to the gone
  device would read down.

Both targets can reach the parent bridge:

- **98 SE:** `PCI.VXD`'s `_PCI_Read_Config` and `_PCI_Write_Config`, the
  services record 21 section 6.1.1 already calls.
- **2000:** `HalGetBusData`, already imported (`hcd_ctl.c`). Writing
  the bit would need `HalSetBusData`, which has no row.

What it would cost, and what a reopening would have to settle:

- **It writes a register another driver owns, with no owner for how
  long it stays clear.** `pci.sys` and `PCI.VXD` own the bridge's
  Command register. Routes 1 and 2 show they rewrite it at the bridge's
  own start and D0 return, not at a child's, but nothing coordinates
  with those writers. A reopening must cover every path that can set
  the bit again or run the controller before HCRST:
  - the bridge's own power transitions;
  - a rebalance;
  - this controller's removal and the image's unload;
  - a replacement FDO.

  Or it must prove that each such path resets the controller first.
  Whether a driver may clear a parent bridge's Bus Master Enable at all
  is the owner's question.
- **It cuts off the bridge's other functions.** It stops their DMA and
  their MSI too, which a root port with one device below it does not
  feel. A bridge with more than one function below it would have to be
  refused.
- **The bit must stay clear until HCRST.** Clearing it blocks only
  requests going up, so a later start can still reach the controller's
  registers through the bridge's memory window. The start would have to
  halt and reset the controller before setting the bridge's bit again,
  while the controller's own bit is already set by `pci.sys` or
  `PCI.VXD`. A start that cannot do that keeps the transfers, and the
  bridge stays closed.
- **Where it would run.** It belongs at the containment itself, in
  `hcdContainProve` beside the controller's own bit, rather than at the
  stop. A containment proven that way takes the proven branch and never
  pins, so the disable never waits and section 4.13.4's teardown-ordering
  points do not arise. A pin left from before would still need the
  release of section 4.13.3.
- **Its readings and legs.** For a reopening:
  - a reading per target of how to find the parent: its bus, device and
    function from this function's own location;
  - whether QEMU's `pcie-root-port` honours its Bus Master Enable for
    requests from below, and whether it models Data Link Layer Link
    Active, so that a q35 leg can show both;
  - a leg that closes the tunnel under a running transfer.

**Recommendation.** None of the routes that need no new owner decision is
strong enough to reopen PCR.1's code. Routes 1 to 5 prove nothing on the
branches that pin, and the known limitation stays as the release notes
state it.

Route 6 is worth an owner's question, not code. It would be a
narrower PCR.1: a proof through the parent bridge, taken at the
containment, for a controller behind a bridge with nothing else below
it. Before any design, the owner would have to answer whether this
driver may clear a parent bridge's Bus Master Enable, (b1). Reading the
link down, (b2), is no alternative on its own. Even with the owner's
yes, three things are open and must be settled first:

- the guarantee that writes accepted before the bit cleared have
  drained;
- every path that could set the bit again before HCRST;
- the readings and legs above.

#### 4.13.6 Reopened for one case: a tunnel the USB4 connection manager closed (task 45.35)

**Why.** Task 45.12 placed a hot-plugged dock whole on XP and 2000, so
the dock's own xHCI starts there for the first time. A surprise unplug of
that dock with a USB tablet on its xHCI then stalls Windows' removal of
the whole dock (`runs/run-45.md`, "45.35"). The controller's window reads
all ones, a command times out, the recovery is refused, and the unproven
invalidation pins the buffer and keeps every transfer. The tablet's
interrupt-IN read is never completed, and no `SURPRISE_REMOVAL` reaches
this driver's PDOs. With a USB disk in the tablet's place nothing is
pending, and the removal runs to the controller's stop. This is the case
section 4.13.5 calls the likeliest pin of all.

**The owner's decision (2026-10-29).** For this case alone, a tunnel path
that the USB4 software connection manager has closed **and read back
closed** proves that no function behind that tunnel port can reach host
memory. The read-back is record 22 section 7.3 step 1:
- the host PCIe Down adapter's Path Enable 0;
- both host-side path entries' Valid 0;
- Pending Packets 0 on both;
- the adapter's LTSSM in Detect.

That is the core's `TBT_U4_LINKDOWN.PathClosed`. An ingress adapter drops
every packet of a path once its Valid is 0 and dequeues what it held, so
nothing from below crosses the host router after it. Writes it forwarded
before that left it before the read-back completed. The proof is
**asked** only by a containment or an unproven invalidation, which come
seconds later (a command's timeout, then the containment's interval).

**What it is not.** It is not a removal notice, not configuration space
reading all ones, and not a reading of this driver's own. It does not
apply to:
- the firmware connection manager's client (the ICM profile, record 21),
  which reads nothing back;
- 98 SE and ME;
- a controller that is not behind a tunnel port of a domain that this
  release's connection manager runs.

There the limitation of section 4.13 stands as the release notes state
it.

**Where it acts.** In `hcdContainProve` (`hcd_ctl.c`), the proof that
both the all-ones containment (section 4.6) and an invalidation no halt
or HCRST proved (`HcdCtlProveDmaStopped`, `HcdEnumService`) take:
- When configuration space answers, the proof is today's: Bus Master
  Enable read back clear.
- When it does not, or the bit will not clear, the tunnel's statement is
  asked. A yes is the proof, logged `tol.proof.tunnel`. The dump's
  tolerance block counts the starts that took the interface, the asks and
  the proofs (`tol.iso.held`, `tol.iso.asked`, `tol.iso.proofs`).

So a proven containment takes the proven branch (`HcdEnumContain(hc, 1)`:
every device drained and dropped, every transfer completed). Section
4.13.4's points do not arise:
- nothing is released after a pin;
- the original requests complete as the proven branch already completes
  them;
- the proof is taken at the containment, on a live start generation,
  before any teardown.

**After the proof.** The proven branch drops every device as a port
disconnect drops it: `hcdDropAll` drains each device's rings with
`HCD_USBD_DEVICE_GONE`, and each request is then held on its PDO
(`HcdIoPark`). The request is completed `CANCELED` at the client's cancel
or abort, or at the PDO's own stop or removal (`HcdIoParkedRelease`). A
request the client has already cancelled is not held: `HcdIoPark`
declines it and `HcdIoDeferred` completes it, so it does not wait either. That was the path every ordinary USB unplug took, the
dock's HID tablet on a port included, and a contained controller keeps it
on every system; from task 50.28 an ordinary unplug on 2000 and later
completes those requests `DEVICE_GONE` instead (record 13 section 10.5.3).

What the stall lacked was only this step. The tablet's read stayed on a
ring whose stop could not be commanded, so the cancel its class driver
issued at the removal never completed. Once dropped, that cancel completes
it. A new request after the proof reaches no ring:
- on the containment's branch it is held (`HcdIoHoldUnreadable`, the
  controller latched `Unreadable`);
- on the invalidation's branch (`hcdInvalidate`) the controller stays
  failed with its devices dropped, so a request names no device and is
  refused at the next tick.

**The residual, stated as such.** The connection manager closes the path
only when its own poll finds the router gone, in order: the teardown's
requests, the read-back, then `cmU4LinkDown`. Nothing ties that to this
driver's thread. In every leg read, the closure came before this driver's
first command timeout, but that is the common case, not a guarantee; the
unproven-invalidation route does not wait for the containment's interval.

A proof asked before the statement exists is refused, and the pin it
then decides is permanent, as today. No pin is released later. The
release notes' limitation covers this order. The emulator cannot delay
the closure, so no leg reads it.

**The channel** (record 22 section 10.5): an interface that `xhcicm`'s
child filter answers on this controller's own PDO. The filter is attached
there by the kernel chain (record 31 section 4.3), below this driver's
FDO.
- **Request.** `IRP_MN_QUERY_INTERFACE` with `GUID_XHCI_CM_ISOLATION`,
  version 1, size `XHCI_CM_ISO_INTERFACE`. It is sent at the controller's
  start, after the lower stack has completed `START_DEVICE`, to the object
  `IoAttachDeviceToDeviceStack` returned, never to the PDO directly. It is
  sent on NT only (`IoIsWdmVersionAvailable(1, 0x10)`). A refusal leaves
  the proof as before.
- **Order.** The interface is taken in `hcdStart` before the controller
  thread starts, and it is released and its pointer cleared in
  `hcdStopBody`, after `hcdThreadStop` has joined the thread.
  `hcdContainProve` runs only on that thread, so no call can overlap the
  release. `STOP_DEVICE` and `SURPRISE_REMOVAL` reach `hcdStopBody`
  through `HcdStopController` before the IRP is passed down, and a start
  that fails after starting the thread reaches it directly. A start that
  fails before that point releases it on its own failure path before
  completing the IRP. `hcdRemove` releases it as well, whatever the start
  reached. The release is idempotent (the pointer cleared under the
  thread's absence), so a retried start never overwrites a held one. The
  interface is asked again at the next start.
- **Use.** `Isolated(Context)` is called at `PASSIVE_LEVEL` from
  `hcdContainProve` alone.
- **No new import.** `IoAllocateIrp`, `IoCallDriver` and `IoFreeIrp` are
  already imported. The callback object Stor98 uses is not taken, because
  Windows 98's NTKERN has no `ExCreateCallback` and this binary loads
  there.

**Verification.** Host vectors for both halves:
- `XhciTolProof` in `xhci_tol.c`, in `test_tol.c`: the proof's
  inputs to its verdict;
- `CmIsoDecide` in `cm_iso.c`, in `test_cm_iso.c`: the statement's
  conditions.

Legs on XP and 2000 (the dock, hot-plugged):
- the tablet on its xHCI: unplug, then replug, then shutdown;
- the same with a USB disk;
- the controller's own report reading `RELEASED` with the tunnel proof;
- the ASM4242's own xHCI, which is not behind a tunnel port, never
  answered.

#### 4.13.7 Reopened for task 48.32: no new release proof

**Why.** The owner took PCR.1 into Phase 48 on 2026-11-09 as task 48.32:
the transfers a pinned containment keeps released once the controller is
proven unable to write to memory, its configuration space all ones
through a validated read, or D3hot read back. Its design was written and
reviewed and revised over several rounds; the owner's decisions of the
same day (Q-PCR.1 to Q-PCR.4) leave it at this: **no new proof releases
anything**, and the known limitation stays as the release notes state
it. An existing gap the review found, the stop's all-ones fallback, goes
to task 48.33 (below).

**The specifications cited.** The PCI Express Base Specification
revision 2.0 and the PCI Bus Power Management Interface Specification
1.2, the copies the owner added to `docs/references` (2026-11-09,
Q-PCR.2; `docs/references/README.md` has their rows), cited by section
and the page the copy prints.

**The link above the controller read down: considered, not built.** The
route the design proposed was the function's own vendor id read all ones
through a validated read (each byte read twice through `HalGetBusData`,
into buffers prefilled `5Ah` and `A5h`, counted only where both agree),
and the nearest bridge on its recorded path that still answers, with the
identity it had at the start, reading its Link Status's Data Link Layer
Link Active (bit 13) at 0, at two walks 100 ms apart; the function
associated with its place in the HAL's configuration space through its
Interrupt Line byte. It shows that nothing more crosses the link and,
since an Upstream Port's DL_Down is handled as a reset of its component,
that the function will name none of its old memory when it returns
(PCIe 2.0 section 2.9.1, pp. 130-131). It does **not** show that the writes
the port had already forwarded upstream, into the root complex, have
reached memory: 2.9.1 resets the component below and discards the
affected transactions, and gives no bound for those. No cheap step that
works everywhere closes that: a read of the surviving port, or of
another device through it, or a processor read of memory, does not
establish that they have drained. A release on it could hand back a page
a write in flight then lands in. So it is not built (the owner,
2026-11-09, Q-PCR.3 (b), after a review): where only this route would
prove the controller stopped, the transfers stay kept and the disable or
removal waits, as today. Nor is it kept as a diagnostic: its start-time
scan and association are not cheap, and a reading that releases nothing
is not worth that cost here.

**Why Bus Master Enable read back clear differs.** The shipping proof
(section 4.6) reads the Command register back from the function itself.
A configuration read's completion may not pass the function's earlier
posted writes on its way up (PCIe 2.0 section 2.4.1, the ordering rules
D2a and D2b, p. 102; section 6.4, p. 374, on a host read flushing a
device's earlier writes in traffic class 0), so that read-back can
establish that earlier writes drained as well as that mastering stopped,
subject to the writes' traffic class and attributes. A link that is down
carries no such read from the function. Whether the USB4 tunnel's
read-back (section 4.13.6) covers the queues on the root complex's side
of the host router is not shown here. Both shipping proofs go to an
audit of their own (the owner, 2026-11-09), not to this task.

**D3hot read back: not built** (the owner, 2026-11-09, Q-PCR.1, after a
review). The reasons, as reviewed:

- PCI Express makes the termination of a function's requests, with their
  completions outstanding, the software's prerequisite to D3hot (PCIe
  2.0 section 5.3.1.4.1, pp. 301-302); a write of PMCSR does not replace
  that quiescence. A controller that would not halt is one whose
  quiescence the driver could not establish (whether it still has
  completions outstanding is not known either way).
- The PMCSR write's own completion may pass the function's posted writes
  (PCIe 2.0 section 2.4.1, rule E2, p. 102), so the write proves nothing
  of them. A successful read of PMCSR afterwards is a read completion,
  which under D2a may not pass the function's earlier posted writes in
  traffic class 0 without Relaxed Ordering (p. 102; section 6.4, p.
  374), as the Bus Master Enable read-back above; so it is not the
  drainage that D3hot lacks, but the quiescence before it and what
  PowerState itself is worth from a function that would not stop.
- A wedged xHCI may keep its configuration logic answering while its DMA
  engine is stuck; that is an engineering inference, not a reading.
- The function's own report is weaker than an observation made
  independently above it.
- A wrong release corrupts memory; keeping the buffers is the failure to
  prefer.

Neither the configuration space answering in D3hot (normal there) nor
`No_Soft_Reset` (which does not weaken D3hot's prohibition on bus
mastering, PCI PM 1.2 sections 5.4.1 and 5.6, and matters only
at the return to D0) is a reason. To be reconsidered only for hardware
whose vendor documents DMA gated and drained before D3hot is observable,
with fault-injected evidence, and then only as corroboration of another
proof.

**The stop's all-ones fallback: to task 48.33.** The review found an
existing gap beside this task. When the controller will not halt, the
stop's quiesce falls back to clearing Bus Master Enable
(`xhciTryClearBusMaster`, `xhci_init.c`), and that fallback takes a
Command register reading `FFFFh` - at its first read or at its read-back
after the write - as a device off the bus that masters nothing; its
caller then frees every buffer with no pin. That is the very evidence
section 4.13 rejects, reached by any stop that comes before a containment
has pinned (a removal, a surprise removal, a disable within the
containment's interval, a refused start). Failing closed there instead
(the owner's first answer, Q-PCR.4 (a)) would keep the clients'
transfers, and with them hang the removal wherever a class driver waits
on a request (on 98 SE and ME always, on NT unless a tunnel statement
proves isolation), because a client's buffer cannot be kept once its
request completes. So the owner moved the fix to a task of its own,
48.33 (2026-11-09): every transfer's data staged through buffers this
driver owns, so that an unproven stop completes its clients' requests
and keeps only the driver's own allocations out of use until the next
start; the all-ones fallback folds into that design (design record 13
section 11 and this record, written for 48.33). Until it lands the
fallback stays as it is. A bridge's Bus Master Enable cleared as a proof
(section 4.13.5, route 6) is not built either (the owner, 2026-11-09).

**Verification.** No code changes for this task, so no host vectors or
legs; its readings are the review's of this section and the
specifications cited.

#### 4.13.8 The unproven stop with every transfer staged (task 48.33, design before code)

**Why.** Section 4.13.7 leaves no proof that releases the clients' buffers
of a controller that may still write to memory, and the stop's all-ones
fallback frees everything on evidence section 4.13 rejects. Task 48.33
(the owner, 2026-11-09) removes the need for such a proof instead: every
transfer's data is staged through buffers the driver owns (design record
13 section 11.11), so the controller never names a client page, and a
stop it cannot prove has only the driver's own memory to keep. Written
before code, reviewed and revised with record 13 section 11.11; the
owner's questions Q-STG.1 to Q-STG.4 are there.

**What "unproven" covers.** Every stop, removal, surprise removal,
refused start or containment whose quiesce does not prove the controller
stopped mastering: section 4.6's routes as they are, with one change -
`xhciTryClearBusMaster` (`xhci_init.c`) no longer takes a Command
register reading `FFFFh`, at its first read or at its read-back after
the write, as a device off the bus; either now reads "not proven". This
is the gap section 4.13.7 handed to this task, closed by failing closed,
which with staging costs no hang. `hcdContainProve` (`hcd_ctl.c`)
already refuses all ones. Bus Master Enable read back clear and the USB4
tunnel's statement (section 4.13.6) stay the proofs, unchanged (their
audit is the coordinator's task, not this one).

**What an unproven stop does** (record 13 section 11.11.6, in order):
staging admission closed; every device drained exactly as a proven stop
drains it (a containment without proof takes the proven containment's
`hcdDropAll`, so its devices leave through the departure and their PDOs
are removed with no stop of the controller needed), waiting requests unlinked from the staging queue; the
processors' staging work joined (the staging DPC counted from its
queueing, every admission pass, copy and publication in progress - a
wait on processor work only); every client request ended with
`DEVICE_GONE` and taken along today's departure route (held on its PDO,
cancellable, where a departure holds it), and every request so held
released `CANCELED` before the controller FDO's own stop or removal
completes, whether or not its PDO was stopped first; every allocation the
controller could name - the common buffer, the enumeration scratch,
stream blocks and isochronous rings, and the staging pool, reserves and
small buffers - kept with the adapter for the rest of the boot, its
charge in the driver's quarantine ledger turned from live to kept (the
image stays loaded from the first kept allocation, so the ledger and its
cap last the boot); processor-only memory freed.

**What it replaces.** `CommonBufferPinned` stops meaning "the clients'
requests stay pending": `HcdIoDeviceDrain`'s early return, the device
record `HcdIoDeviceGone` keeps, `hcdPdoQuiesce`'s skipped wait
(`hcd_pdo.c`), `HcdDmaWideClose`'s kept pool and `HcdEnumContain`'s
pinned route (`hcdDrainAll` keeping every device, `hcd_enum.c`) go; the map pump's
quarantine (`MapQuarantined`, and the refusal of a later start on the
same FDO in `hcd_pnp.c`) goes with the map pump, since no channel request
is ever outstanding, while its rundown of processor work stays, as the
staging drain. `CommonBufferPinned` stays as the mark that the memory is
kept. The selective suspend rundown keeps its abandonment of a client's
idle callback still running (`hcd_susp.c`), triggered by the stop as
today. The release-notes limitation section 4.13 states - a disable of a
controller closed off without proof waits for ever - is lifted for every
client request but one: a client's idle callback still running when the
stop abandons it, whose PDO stays kept as today (`IdleQuarantined`).

**The owner's decisions (2026-11-09)**, recorded in full in record 13
section 11.11.10: Q-STG.1 (a), the client's buffer copied through a 64 KiB
window the driver owns; Q-STG.2 (b), the pool a setting,
`XhciStagePoolKiB`, 2048 KiB by default; Q-STG.3 (a), every kept
allocation kept until Windows restarts, one 64 MiB ledger for the driver,
a start or a pipe open refused past it; Q-STG.4 (a), nothing stays
direct. Built on branch `w48-pci` (record 13 section 11.11.11): the
all-ones fallback fails closed at both reads, `HcdIoDeviceDrain`'s early
return, `hcdPdoQuiesce`'s skipped wait, `HcdEnumContain`'s pinned route
and the map pump's quarantine are gone, and an unproven stop closes
staging's admission (`HcdSvcDmaNotStopped`), drains as a proven stop does
and keeps its DMA memory through the ledger.

**The switch.** All of the above holds with `XhciDmaStaging` 1 alone
(record 13 section 11.11, "The switch"). Since the owner's change of
2026-11-10 the default is 0: every transfer goes direct as in 3.4.2.0,
and an unproven stop keeps its devices' records and the clients'
requests pending, section 4.13.1's wait, until a user sets 1.

**Owed.** The legs record 13 section 11.11.9 lists, on 98 SE, ME, 2000,
XP and 7 x64, with the qemu flavour's fault switch at each point a
request can be.

#### 4.13.9 The staged path audited, and what must be shown before staging is the default (task 49.16)

**Why.** Task 49.16 (the owner, 2026-11-11, from the README's driver
issues): a dock unplugged with a keyboard, mouse, tablet or similar
device on its own USB controller is never fully removed unless it was
stopped first. That is section 4.13.1's wait, and section 4.13.8 already
designs what ends it. This section is an audit of that design against
the code as it stands on branch `3.5.0.0` at `a4436a9b2`, with no code
changed, and the list of what must be read before `XhciDmaStaging` 1
could be the default. No default is changed here.

**What task 49.25 added, and what it did not** (2026-11-11; the legs'
record is `runs/run-49.md`, "49.25", after "49.6 (fault 1)", whose
order they repeat). On XP, under the firmware connection manager, an
HP Thunderbolt Dock G2 model removed by surprise in the emulator, with
its network function bound and used:

- with `XhciDmaStaging` 0 on the dock's controller the removal never
  finished and XP never shut down: the network driver's halt waited on a
  cancelled control request, then on an ABORT_PIPE, and behind them on
  five transfers left on the rings (legs X4b, X5 and X6; the report
  read "CONTAINED" and "PINNED");
- with 1, the same binary and the same order, every request completed,
  `IRP_MN_SURPRISE_REMOVAL` and the removal reached every object of the
  dock, and XP restarted (legs X7b and X8).

So the mechanism of section 4.13.8 is supported: on one system, with
one dock model and one client driver, in the emulator, with requests
that happened to be on the rings, and with no replug afterwards. It
does **not** establish that staging should be the default. That
decision changes every transfer of every controller on every system,
and rests on the lists below.

**What 1 changes on a controller closed off without proof.** Read in
the code; five places test the switch or act on it:

- `HcdEnumContain` (`hcd_enum.c`) takes the proven containment's
  `hcdDropAll` when there is a proof **or** staging is on, so every
  device is drained and leaves through the departure; with 0 it only
  drains, which on a pinned buffer keeps everything.
- `HcdIoDeviceDrain` (`hcd_io.c`) returns before `HcdCfgFlushDevice`
  and `HcdIoDrainPipe` only when the buffer is pinned **and** staging
  is off.
- `hcdPdoQuiesce` (`hcd_pdo.c`) waits for the PDO's pending count on a
  pinned buffer with staging on, and skips that wait with it off.
- `hcdStopBody` (`hcd_ctl.c`) releases every child PDO's held requests
  (`HcdIoParkCloseAll`) after an unproven stop with staging on.
- `xhciTryClearBusMaster` (`xhci_init.c`, through
  `XhciStageCommandProof`), the fallback of a stop whose halt failed,
  takes a Command register of `FFFFh` as a function off the bus with 0
  and as no proof with 1.

**Why a request may then complete.** The argument, each step read in
the code:

- *The controller names only the driver's memory.* Every data TRB of a
  client's transfer names staging: a small buffer, a slab, a reserve,
  the control slab or an isochronous share (`hcd_stage.c`). The hubs'
  status transfers and enumeration's use the scratch, which is kept
  with the rest (`hcd_hub.c`, `HcdHubRearm`, whose one element is in
  `ScratchPa`). A Setup stage is in its TRB. The qemu flavour counts
  any published element outside its TD's staging (`stage.foreign`).
- *All of that memory is kept.* Each allocation comes from `hcdDmaAlloc`
  and goes back through `hcdDmaFree` (`hcd_dma.c`), which keeps it and
  moves its charge from live to kept while `CommonBufferPinned` is set;
  `HcdDmaClose` then keeps the adapter too.
- *The pin comes before the drain.* `HcdSvcDmaNotStopped` (`hcd_svc.c`)
  sets the pin and closes staging's admission, and both routes that
  contain call it before `HcdEnumContain` (`hcdContain`, `hcd_ctl.c`;
  the unproven invalidation, `HcdEnumService`, `hcd_enum.c`). So a pipe
  the drop closes keeps its staging, and a slab a drained record gives
  back to the pool is handed to no other transfer.
- *Nothing is copied from memory the controller may still write.* An
  IN taken off without its Transfer Event copies nothing out while the
  buffer is pinned (`hcdStageCopyOut`), and the event ring is not
  drained on a failed controller (`xhci_evt.c`, the drain's
  `ControllerFailed` test).
- *Processor work is joined.* `HcdStageDrain` waits out the staging
  DPC, the retry timer and every admission pass, copy and publication
  before `HcdStageClose` and `HcdDmaClose` run (`hcdRelease`,
  `hcd_ctl.c`).
- *The accounting holds.* The ledger is the image's, not a
  controller's; the first kept allocation clears `DriverUnload` under
  its lock (`hcdLedgerKeep`).

**The verdict of the reading.** In the paths read, nothing at 1 frees
or reuses memory the controller may still name, and nothing completes
a request twice. That is a reading of the code, not a proof: record 13
section 11.11.9's checks of it are not built (below). One liveness risk
was found and is **not** resolved: the held request of the table's
fourth row.

**Each path that completes or cancels a request on a contained
controller.** "Contained" is after the latch (`Tol.Unreadable`);
"failed" is the interval before it, from the first command timeout.

| Path | Staging 0 | Staging 1 | Evidence | Open |
| --- | --- | --- | --- | --- |
| A client cancels a transfer on a ring | `hcdCancel` marks the record and wakes the thread; the thread serves no cancel on a failed controller (`HcdEnumService`, the `hcdHalted` tests), so it never completes | the same until the containment; its drain then ends the record, and `HcdIoPark` declines a cancelled request, so it completes at once | `hcd_io.c` (`hcdCancel`, `HcdIoPark`); X4b against X7b and X8 ("URB 13 completed after cancel") | the time to the containment is a command's timeout and the refused recovery, about 5 s in X8; no reading bounds it |
| A client cancels a request not on a ring | a request waiting for a transfer record is unlinked by its own cancel routine and refused at the next tick, with no thread; a record held behind its pipe's owner or on a paused pipe is marked for the thread, as one on a ring is | the same; and a record waiting for staging, or a copy-out waiting for a window, is unlinked and completed with neither the thread nor the controller, one whose copy runs being finished by the copy's owner | `hcd_io.c` (`hcdWaitCancel`, `hcdCancel`, `HcdIoWideStop`), `hcd_stage.c` (`HcdStageCancelLocked`) | no leg has met a record in staging's own states at an unproven stop |
| ABORT_PIPE sent after the containment | answered at dispatch with success; releases only requests already held on the PDO, not those on the rings | the same; every request was drained and held, so the abort releases them | `hcd_urb.c` (`hcdUnreadable`), `hcd_cfg.c` (`HcdCfgQueue`); X8 ("held URBs released", 4 and 1) | none read |
| Any other request sent after the containment: a transfer, RESET_PIPE, a select, a static-streams request | held on its PDO, cancellable; released by a cancel, an abort that covers it, or the PDO's stop or removal | the same, and at the controller's own stop | `hcd_urb.c` (`hcdSubmitUrb`: the `Unreadable` test comes before the departed branch; `hcdUnreadable`), `hcd_io.c` (`HcdIoHoldUnreadable`) | **the liveness risk.** A client that sends such a request from its own removal handling and waits for it without a bound, before passing its REMOVE down, stalls the removal at either value; staging does not end that wait. On NT the client hears `IRP_MN_SURPRISE_REMOVAL` first (X8; section 4.13.6's legs). 98 SE and ME have no surprise removal, and ME's `usbaudio.sys` sends a select before its REMOVE (record 13 section 18.2): on a contained controller that select is held, and the release of record 13 section 18.5 (`HcdIoIsoRelease`), which sits in the departed branch, is never reached. Not read on any system |
| ABORT_PIPE, RESET_PIPE or a select queued while failed, before the containment | stays queued for good: the thread serves nothing on a failed controller, the pinned drain returns before the flush, and the request has no cancel routine | flushed by the drain: an abort succeeds, any other is held on its PDO as in the row above | `hcd_cfg.c` (`HcdCfgService`, `HcdCfgFlushDevice`, `hcdCfgHoldUnreadable`); X5 and X6's second abort (its place in the queue is code reading, not a log line) | with 0, a request that names no client page waits for nothing it needs; a narrower fix exists and would not end the removal's wait (`runs/run-49.md`, "49.25") |
| A port reset, or the private OPEN_STREAMS and CLOSE_STREAMS requests | one queued stays queued (above); a new one finds the device gone and is refused | one queued is completed by the drain; a new one is refused | `hcd_cfg.c` (`HcdCfgQueue`: the hold takes URBs only; `HcdCfgFlushDevice`) | none read |
| A descriptor read queued through the door | completed even when the record is kept | completed | `hcd_io.c` (`HcdIoDeviceDrain`: `HcdDoorFlushDevice` before the early return) | none |
| The idle request, `XhciSelectiveSuspend` 0 (the INFs' value) | held with a cancel routine, its callback never called; completed at the client's cancel or the PDO's stop or removal | the same | `hcd_urb.c` (`hcdIdleSubmit`, `HcdUrbIdleDrain`) | none |
| The idle request, `XhciSelectiveSuspend` 1, its callback running at the stop | the callback's thread is abandoned and the pin recorded on its PDO; the PDO's rundown does not wait for it and keeps the object | **the same**: neither test reads the switch. The PDO object and the callback's context stay for the boot, and the idle request completes only when the client's callback returns | `hcd_susp.c` (`HcdSuspCbStop`, `hcdSuspCbAbandon`, `HcdSuspIdleRundown`, `hcdSuspCbThread`) | section 4.13.8's one exception, as designed. A client that itself waits for its callback is not moved on by the abandonment. No leg has a callback running at an unproven stop |
| A child PDO's power requests | selective suspend's admission and every pended child request are settled by the stop | the same | `hcd_ctl.c` (`hcdStopBody`: `HcdSuspControllerDownLocked`, `HcdTmrJoin`) | not read on a contained controller at either value |
| The controller's own power requests | a contained controller is not resumed. The shutdown's flush completes nothing on one: both routes that contain set `Tol.Unreadable`, and any value of it suppresses the flush of commands and of transfers alike (without it, a pinned or unhalted controller has its commands flushed and no transfer) | the same rule. The software queues were drained at the containment; requests held on a PDO stay held until their client or PDO lets go | `hcd_power.c` (`hcdResumeContained`, `hcdShutFlush`), `xhci_tol.c` (`XhciTolShutFlush`) | that XP's shutdown waits on the stalled removal, not on a power request, is an inference (no power request was sent in X6) |
| STOP, SURPRISE_REMOVAL or REMOVE of a child PDO | held requests released; the wait for the count skipped on a pinned buffer, so the PDO does not wait, its client above does | held requests released; the count waited for, which the drain brings to 0 | `hcd_pdo.c` (`hcdPdoQuiesce`); X8 (minor 17h, then 02h, to every object) | a wait inside the PDO's own request is new with 1: it ends only once the containment or an invalidation has come. Read on XP alone |
| A refusal completed at the next tick | a refused request is on its PDO's own list and timer, cancellable; the PDO's REMOVE waits for them, its STOP and SURPRISE_REMOVAL do not | the same | `hcd_io.c` (`HcdIoRefuseLater`), `hcd_pdo.c` (`hcdPdoRefusalsWait`) | none read |
| The controller's stop or removal after the containment | every DMA block kept; the devices' records and requests kept | every DMA block kept; devices dropped, every held request released `CANCELED` (`HcdIoParkCloseAll`) | `hcd_ctl.c` (`hcdStopBody`), `hcd_dma.c`; X8 ("failing closed", then the release); `runs/run-48.md`, "48.33", the unproven stop on 7 x86 and 7 x64 | 98 SE, ME, 2000 and Vista have no such leg |
| A stop before any containment whose halt fails and whose fallback reads `FFFFh` (a removal with nothing pending) | read as off the bus: everything freed, no pin, the evidence section 4.13.7 rejects | not proven: completed as above and the memory kept | `xhci_init.c` (`xhciTryClearBusMaster`), `xhci_stage.c` (`XhciStageCommandProof`); `runs/run-45.md`, "45.35", the USB disk case | with 1 each such removal keeps the controller's DMA memory for the boot (below) |
| External hubs and the root hub | the hub's own status transfer carries no request; a hub's and the root hub's removal wait on nothing of the controller but the thread's detach, served halted or not | the same; the hubs leave with their subtrees in the drop | `hcd_io.c` (`hcdDrainOne`), `hcd_hubfdo.c`, `hcd_rh.c`, `hcd_enum.c` (`HcdEnumDetach`, `hcdDropAll`); X8 ("hub leaving") | on 98 SE and ME the root hub's REMOVE comes with the thread running; not read contained |
| A composite device | each function PDO as a child PDO above, its requests held and released per PDO; the composite parent's PDO answers its descriptor reads and selects itself, at once, and holds nothing | the same routes | `hcd_pdo.c` (`hcdPdoQuiesce`, `HcdIoCancelPdo`), `hcd_urb.c` (`HcdCompositePdoInternalIoctl`) | not read contained |
| UAS streams | kept with the rest | every stream's pipe drained; the stream block and the streams' staging kept through the same release | `hcd_io.c` (`HcdIoDrainPipe`), `hcd_stage.c` (`HcdStageStreamsClose`) | no unproven stop with a stream request in flight has been read |
| Isochronous requests | kept with the rest | drained and held as a departure's; on a contained controller the select that would hand them back on 98 SE and ME is itself held (the fourth row) | `hcd_io.c` (`HcdIoPark`, `HcdIoIsoRelease`), `hcd_urb.c` | no unproven stop with audio playing has been read; issue 25's ground |

**What differs between sections 4.13.8 and 11.11 and the code, and what
no leg has read.**

- Three texts describe the 0 route alone: the USB Tree's report prints
  "PINNED: no proof DMA stopped; buffer, transfers kept" for a
  containment without proof at either value (`xhcitr/tr_snap.c`); the
  comment above `hcdContain` (`hcd_ctl.c`, its step 3); and the comment
  above `HcdIoIsoRelease` (`hcd_io.c`), which says an unproven
  controller parks nothing. With 1 the transfers are not kept and the
  drain does park. The TRBs left on the kept rings name staging alone;
  a held request still carries its client's buffer, which nothing the
  controller can reach names any more. So the texts are wrong and the
  behaviour is not. Left for the next change there.
- Record 13 section 11.11.9's fault points - a record held `WAITING`,
  `FILLING`, `READY`, `DRAINING` or `COPY_PENDING` when the stop meets
  it - and its log of each request's one completion are not built
  (section 11.11.11). `XhciQemuStageFault` bit 0 acts at the stop,
  after the quiesce, so it does not make a containment on a started
  controller either; section 5's injection of all ones without the
  proof does. Every unproven stop read so far met requests published
  on a ring, and none placed them there on purpose: bulk writes on
  7 x86 and 7 x64 (`runs/run-48.md`, "48.33"), a network function's
  reads on XP (X8).
- No unproven stop has been read with a UAS stream request, with an
  isochronous pipe running, with a selectively suspended PDO, or on
  98 SE, ME, 2000 or Vista at all.
- With 1, each stop of the table's row "a stop before any containment"
  keeps the controller's common buffer, its event ring's block, its
  scratch and all its staging. By the constants that is some 2.5 MiB
  with the default pool (`XhciStagePoolKiB` 2048) before any pipe's,
  stream's or isochronous allocation, against the ledger's 64 MiB for
  the image, which the live charges of every started controller share.
  So some tens of surprise removals in one boot, not hundreds, before
  a start is refused (Code 10) or a pipe fails to open, as Q-STG.3 (a)
  decided. The figure is an estimate; no leg has read the bytes kept,
  the refusal, or the ledger carried over to a new controller (the
  7 x86 leg saw the image stay loaded and did not read the notes).
- An update keeps what an earlier install wrote: both INFs write
  `XhciDmaStaging` under the don't-overwrite rule, and `3.4.3.0` wrote
  0. A new default in the INFs would reach a fresh install alone; how
  an existing value moves is the installer's to decide (record 23) and
  is not designed.
- The tunnel statement's own audit (section 4.13.7) is still owed, and
  is not part of this one.

**What must be shown first.** Three lists, because three decisions
rest on them: staging for a dock's controller on 2000 to 7; the same
on 98 SE and ME; and the default for every controller. Each item says
what passes. Record 13 section 11.11.9's list stands whole; nothing
here replaces it.

*List 1: the unproven stop itself, on every system that would run it.*

1. **Each state a request can be in at the stop** (record 13 section
   11.11.9): waiting for staging, filling, ready, published, draining,
   waiting for a window; the staging DPC queued and running; the retry
   timer set; a mapping that fails some times, and one that never
   maps, ended at its give-up; a cancel or an abort racing each. Passes
   when each request completes exactly once, nothing is copied after a
   completion, `stage.foreign` is 0, each PDO's pending count returns
   to 0, and the allocations made equal those freed plus those the
   ledger keeps. The fault points and the per-request log this needs
   are not built; building them, or accepting another reading in their
   place, comes before any change of default and is not this task's
   (built since for task 49.29, record 13 section 11.11.12).
2. **A containment on a started controller**, by section 5's injection
   of all ones without the proof, with a HID read pending, a bulk copy
   in flight, a UAS stream request in flight and audio playing, and
   with an ABORT_PIPE sent while the controller is failed and not yet
   contained: every PDO removed with the controller left started, the
   abort answered, nothing faulting.
3. **The request a client sends as it is removed** (the table's fourth
   row), on 98 SE and ME first: a HID device, a mass-storage device
   and an audio device playing through Windows' own `usbaudio.sys`,
   each on a controller contained without proof at 1. Passes when the
   removal completes. If a client is found waiting on a held request,
   the hold on a contained controller (section 4.6) goes back to the
   owner before anything else here.
4. **The bytes kept.** After one unproven stop, the bytes kept; a run
   to the cap with the pool raised, the start's refusal and a pipe's
   read; the last controller removed and one added, the total carried
   over. Today's notes cannot give this: `dma.quarantine.first` holds
   the first kept allocation's size alone, `dma.quarantine.cap` the
   total only at a refusal, and both leave with the removed
   controller's log. A reading of the ledger's two totals that outlives
   the controller comes first, and is not built (built since for task
   49.29, record 13 section 11.11.12).
5. **The idle request**, two cases apart. Ordinary: a held request, or
   a callback that has returned, at an unproven stop:
   `idle.quarantined` stays 0. Deliberate: a callback held from
   returning at the stop: the PDO is kept and counted, the removal
   below it completes, and the idle request completes once, when the
   callback is let go.

The command stranded at 0 (the table's fifth row; task 49.25's side
finding) is folded into this task as a question, not as a condition
of 1: the containment at 0 could complete an abort, and hold any other
command, as its drain at 1 does, since none names a client's page.
That ends no removal's wait by itself (the client still waits for its
transfers), so it matters only for a controller left at 0, and it is a
change of what 0 does: the owner's. If taken, its leg is that abort at
0, and passes when the abort completes once while the transfers on
the rings stay pending and their memory kept; no PDO's removal is its
criterion.

**The command stranded at 0, completed with an error (the owner,
2026-11-11; task 49.16, design before code).** Where: `HcdIoDeviceDrain`
(`hcd_io.c`), the early return taken when the buffer is pinned and
staging is off. Before it returns, every command queued for the device's
thread - its entries on `SlowIrps` and selective suspend's deferrals,
but any a cancel routine owns - is taken off under the controller lock
and completed `DEVICE_NOT_RESPONDING`, one without a URB (RESET_PORT,
the streams requests) `STATUS_UNSUCCESSFUL`, as the shutdown latch's
command flush completes them (`HcdCfgShutFlush`), counted
(`cfg.stranded`). It reaches every route that pins at 0: the
containment and the unproven invalidation (`HcdEnumContain`'s
`hcdDrainAll`), and a later stop of the same controller.

- *Why it is safe at 0.* None of these requests names a client page,
  none was started (the thread serves nothing once the controller has
  failed), and each completes on the thread's own path
  (`hcdCfgComplete`). A command the thread was serving when the
  controller failed is not on the list and ends as today. A request
  sent after the containment is untouched: held, or an abort answered,
  as the table's fourth row says (`HcdCfgQueue`).
- *Why an error and not the drain's answers at 1.* At 1 the drain
  answers an abort with success because the pipe's transfers are then
  drained and held; at 0 they stay on the rings, so an abort that
  succeeded would say what is not so. Holding the others would keep the
  wait this change removes.
- *What it does not do.* It ends no removal's wait: a client that waits
  for its transfers (`cdcnet`'s quiesce, record 13 section 11.11's
  reason) still waits at 0. It releases no held request.
- *Whom it changes.* A client whose command was queued between the
  failure and the containment now has it fail instead of never
  returning: `cdcnet`'s second ABORT_PIPE in legs X5 and X6
  (`runs/run-49.md`, "49.25"), a select or RESET_PIPE in the same
  window (ME's `usbaudio.sys` sends a select before its REMOVE, record
  13 section 18.2). Searched in `runs/`, `issues/` and the code's
  comments for earlier incidents on this path: none beyond X5 and X6;
  issue 30's select was refused at dispatch on a running controller,
  not on this path. At 1 nothing changes.
- *Its leg* is the paragraph above's: that abort at 0, passing when it
  completes once while the transfers on the rings stay pending and
  their memory kept.

*List 2: the dock, unplugged without a stop (this task's own legs).*

6. **On each system and architecture staging is to be chosen for**,
   of 2000, XP, XP x64, and Vista and 7 in both architectures, under a
   firmware connection manager, 1 on the dock's controller, with in
   turn a HID device on that controller, its network function bound
   and used, a disk in a copy, and a disk idle. One not read stays at
   0. Passes when:
   - every device of the dock is gone within a minute;
   - `IRP_MN_SURPRISE_REMOVAL` and then the removal reach every object
     of the dock;
   - the report afterwards shows no controller contained;
   - the dock plugged back is found, every device starts and works (a
     key, a ping, a copy compared), three times in a row;
   - after a last unplug Windows shuts down and restarts;
   - on Vista and 7 the replug does not stop the computer responding
     (`runs/run-45.md`, "45.49");
   - the same order at 0 stalls with the HID device and with the
     network function, so the leg can tell the two apart; with an idle
     disk the dock is removed at 0 too (`runs/run-45.md`, "45.35" and
     "45.49"), so the two disk cases are judged on their own outcome
     at each value.
7. **The same on 98 SE and ME**, where the removal arrives with no
   surprise removal before it, after items 3 and 8.
8. **98 SE and ME: where a copy reaches the client's bytes** (task
   49.15). A copy runs in whatever process context its DPC finds. On
   98 SE every buffer mass storage handed the bus in the workloads
   read (COMMAND.COM copies and XHCIPERF on a Bulk-Only stick) carried
   a system address, so no window was mapped (`runs/run-48.md`, "48.33
   Staging off by default"); a Win32 copy and a UAS disk are unread.
   Only the qemu flavour counts the sources (`stage.direct.buffer`,
   `stage.direct.mdl`, `stage.direct.low`): it is put on the machine
   by hand for the reading, in the emulator and on real hardware
   alike, and is never published (design record 08). Copies by a Win32
   program and by COMMAND.COM, to a Bulk-Only stick and a UAS disk,
   pass with `stage.direct.low` 0 and every copy equal. Then the
   window route, with `XhciQemuStageFault` bit 1: passes when
   `stage.window.maps` shows windows were used and the copies compare
   equal; and `XhciQemuStageMapProbe`, whose outcome must be a mapping
   that returned NULL, with the copy going on after the unmapping -
   the probe running out of MDLs reads nothing. Where the qemu flavour
   will not load on a real machine, what is left there is the release
   flavour's Win32 copy and compare at 1 with another program busy,
   which reads the result and not the addresses.
9. **One real dock**, on 98 SE and on one NT system: item 6's steps,
   the owner's bench.

*List 3: more, before 1 is the default for every controller.*

10. **98 SE: playback beside copies** (task 49.5's left item). Over
    its 18 s bound in 4 of 8 plays at 0, 1 of 8 at 1, and 4 of 8 with
    the copies on C: alone at 0. Owed: that control at 1, and the same
    on a real processor; passes when 1 is no worse than 0 on both.
11. **A stop that is not a removal, with transfers in flight.** On
    2000 no disable landed while the stick was in use (25 tries), and
    the surprise removal read in its place passed at 0 and 1 (task
    49.5). That reads the removal, not the stop's rundown: a disable
    during copies is read at 1 on 98 SE (Device Manager; read at 0
    only) and on XP (read at 0 only), and on 2000 if a route to it is
    found.
12. **The speed on real hardware, on an NT system.** Read on 98 SE
    alone (the P14s's UAS drive: no difference); the emulator's cost
    on 7 x64 reached 73 %. One NT reading at 0 and 1, the owner's
    bound.
13. **What 1 refuses that 0 serves.** An isochronous request larger
    than its pipe's share, a start without the control slab or a
    window MDL, a pipe without its reserve: `stage.iso.refused`,
    `stage.noreserve` and `stage.pool.short` read 0 over the audio and
    storage legs of each system, and a 98 SE start with little memory
    still starts every controller.
14. **The systems and memory layouts no leg at 1 has run**: Vista, XP
    x64, 2000 under PAE and 7 x64 with 6 GB (record 13 section
    11.11.9's last item), and Windows 8 to 10 wherever this driver is
    installed on them, each with the copy and compare and a hot unplug;
    or the default changed only on the systems read.

**An intermediate choice with no change of default.** Three were
considered; none is built.

- **(A) As today.** 0 stays; a user sets `XhciDmaStaging` 1 in the
  dock's controller's own key, which is what legs X7b and X8 did. It
  changes nobody who does not ask. It needs that controller's key
  found, which no tool does, and a restart of the controller; and a
  machine-wide value of that name, in any type, is read in its place
  (`hcdReadDword`, `hcd_ctl.c`).
- **(B) Staging forced on for a controller behind a tunnel port.** A
  tunnelled controller can leave at any time, and is the case this
  task is about; an on-board one keeps 0. What it takes:
  - the decision before staging's memory is made, at the first start
    too: the switch is read early in the start (`hcd_ctl.c`), and the
    one channel to the connection manager is asked at its end, on NT
    alone, is answered on 2000 and XP alone, and binds only under the
    USB4 software connection manager (section 4.13.6; record 22
    section 10.5). So a new statement is needed, "this function is
    behind a tunnel port", that the firmware connection manager's
    client can make too, and a route for it on 98 SE and ME, where no
    filter sits below this driver;
  - a rule for the value: whether an explicit 0 still means off for
    such a controller, and how a 0 the install wrote is told from one
    a user chose; or a third value meaning "behind a tunnel only",
    which changes what the INFs write and so is a change of default
    in all but name;
  - lists 1 and 2 for the systems it reaches.

  Whom it changes: every client of a dock's own controller, on the
  systems it is taken for, whose data is then copied - a dock's audio,
  storage, network and HID - and nobody on an on-board controller. Its
  risks are those of 1 itself, on fewer machines: items 3 and 8 still
  decide 98 SE and ME, item 4 applies to exactly these controllers,
  and item 13's refusals reach a dock's devices. Earlier incidents on
  this path, searched in `runs/` and `issues/`: the playback bound of
  item 10; one fatal exception 0E during a 98 SE start in the emulator
  after the switch was set back to 0, one start in five, not
  reproduced and not explained (`runs/run-48.md`, "48.33 The owner's
  bench kit"); and issue 25, a USB audio device unplugged while
  Windows' own driver plays it on 98 SE and ME, the ground of item 3.
- **(C) By a list of controller ids.** Refused: a dock carries other
  makers' controllers as well as the Thunderbolt controller's own, and
  the same parts are soldered on boards.

**Recommendation, for the owner.** Keep 0 in `3.5.0.0` and stay with
(A) until list 1 is read. Then (B) on each system of 2000 to 7 that
item 6 has passed on, and on no other; 98 SE and ME after items 3, 7
and 8; and the default for every controller as a later decision, on
list 3 and on (B)'s record in use. Under every
choice the limitation of section 4.13 stands for a controller left at
0.

**Verification.** No code changes, so no host vectors and no legs of
this section's own. Its readings are the code at `a4436a9b2` and the
legs cited; where a row rests on code alone its last column says what
is unread.

**The shutdown at 0, read 2026-11-13** (guests of 2000 SP4, XP SP3,
Vista SP2 x86 and 7 SP1 x86, the controller's failure injected, left
contained at the shipped `XhciDmaStaging` 0): neither a normal shutdown
nor a forced one finished (8 of 8 timed runs, held 10 and 20 minutes);
the devices stay listed and reading the drives does not hang; only a
power-off ended it. With audio playing the shutdown waits on the audio
application; without it, it stops after the shutdown's power query with
one request to the USB disk held. Owner, 2026-11-13 ("Say power
button"): the user documents no longer say to restart the machine; they
say not to disable the controller, to save the work, and to switch the
computer off at the power button if Windows does not finish shutting
down or restarting. A wording change only; no planned item.

#### 4.13.10 A child stopped when a containment drops it, on 98 SE and ME (MEBOOTDROP.1)

**What was read** (task 49.16's legs, `runs/run-49.md`, "49.16 (the
remaining legs)"; the qemu flavour's trace of those runs, read again for
this section). On ME, with `XhciDmaStaging` 1, a USB tablet at power-on
and the controller contained without proof at its first transfer (the
qemu flavour's hold with the containment armed, `XhciQemuStageFault`
0x0001000C), ME never finished starting, 2 of 2. The order the device
PDO saw:

1. START (minor 00h), completed by this driver;
2. its class driver's first request, a GET_DESCRIPTOR (URB function
   0Bh), held, then completed once with DEV_NOT_RESPONDING at the
   containment, whose drop (`hcdDropAll`) took the PDO off the bus
   (`HcdDevicePdoGone`) and asked for the root hub's relations;
3. STOP (minor 04h), with no QUERY_STOP before it;
4. the controller's and the root hub's relations, the answer without the
   PDO;
5. nothing more: no REMOVE reached the PDO.

The control (the hold alone, no containment; the request ended
CANCELLED by its client) saw 1 to 3 the same and then the child still
listed in the relations, its capabilities and ids asked again, no second
START and no REMOVE, and ME reached the desktop. On 98 SE the same fault
and device gave no STOP before the relations, and the REMOVE came.

**What it is.** Run-49's placement read the STOP as ME's start-up
rebalance, from the request order. The same order is consistent with,
and strongly supported by, task 43.49's reading (`runs/run-43.md`,
"43.49"; record 13 section 10.5.2): the GET_DESCRIPTOR is the HID
minidriver's own start request, so the stack's START failed; ME sends a
STOP after a failed START; and ME's `hidusb.sys`, read there in memory
and statically, then holds the REMOVE that follows on an event nothing
sets, before passing it down - 43.49's IRP order ("START, then STOP,
then the relations without it, then nothing"). Neither trace records
the stack's own START completion or the held REMOVE; nothing of ME was
read for this section. It is record 13 section 10.5.2's point 6 (run
d2: the device leaving while the class driver's start requests are
under way), reached here by a containment instead of an unplug. The
legs below test the reading: a mass-storage device in the tablet's
place (no `hidusb.sys`) is expected to be removed and ME to finish
starting before the change.

So the omission in step 4 turns a failed start into a hang: with the
child still listed (the control) ME asks for no removal and goes on.

**The change** (98 SE and ME only; a containment's drop only):

1. **The mark.** While `HcdEnumContain` drops every device
   (`hcdDropAll`, with the proof or with staging on; from `hcdContain`,
   and from the unproven invalidation that becomes a containment in
   `HcdEnumService`), the controller records that the departure is a
   containment's (`ContainDropping`, thread only, cleared before
   `HcdEnumContain` returns). `HcdDevicePdoGone` then marks
   `StopCarry` each PDO it takes off the bus that PnP has seen
   (`Reported`, and not missing at once by the ancestor or let-go rule),
   on a system without `IoIsWdmVersionAvailable(1, 0x10)` alone, asked
   at PASSIVE_LEVEL before the lock; the first mark sets the
   controller's `CarryArmed`, never cleared. No other departure marks:
   an unplug, a port's own reset, an ordinary invalidation's drop and
   the controller's stop are unchanged.
2. **Carried while stopped.** A relations answer
   (`HcdDevicePdoRelations`, of the root hub, a hub FDO or a composite
   FDO) carries, besides the listed PDOs, every gone PDO under that
   parent that is *carried* (`hcdCarriedLocked`): on its own - marked,
   its PnP state STOPPED (a STOP and no START since, not a surprise
   removal), no REMOVE received, not reported missing (the pure rule
   `XhciEnumStopCarry`), and no ancestor gone from PnP's view (the
   ancestor rule, `hcdAncestorGoneLocked`); or as a hub or composite
   parent PDO, itself gone and not missing, removed, deleted or let go,
   above a PDO carried on its own, since PnP removes a devnode's
   children with it. The answer's count, its copy and its sweep (the
   marking of missing PDOs and the move of removed ones) use that one
   predicate in the one hold of `PdoListLock`, so a PDO copied into an
   answer is never marked missing by it. A carried PDO is referenced in
   the answer as a listed one is, is not marked `MissingReported`, stays
   on `GonePdos` with no device record, and its port keeps waiting on it
   (`HcdDevicePdoExists`), so its place is not enumerated again while it
   is carried. A marked PDO still started at the answer is omitted as
   today: ME removes a started HID stack without the wait (its STOP runs
   from the started state). The serial-id check (`hcdSerialTakenLocked`)
   counts a carried PDO under the same parent too: its rule that one
   answer omits the old PDO while presenting the newcomer does not hold
   for one carried, so a device with a serial id moved to another port
   while the controller was stopped is given its port-based id instead
   of presenting the carried one's id twice. The state the predicate
   reads besides the lifecycle flags, the PnP state, is written by the
   PDO's own PnP dispatch outside the lock; on 98 SE and ME the
   configuration manager sends one PnP IRP at a time, so a child's STOP
   or START and its parent's relations query never run together, and on
   NT no PDO is marked.
3. **Its next START** clears the mark under `PdoListLock` and is then
   answered as a departed PDO's START is today on 98 SE and ME (record
   13 section 10.5.2): a device or function PDO becomes a ghost and the
   START succeeds; a hub or composite parent, a removed PDO or a
   quarantined one is refused ("device gone"). Once it is answered, the
   relations are asked for again: of each gone hub or composite
   ancestor whose FDO PnP has started (`hcdCarryAsk`), since
   `hcdInvalidateFor` names listed parents alone, and as
   `hcdInvalidateFor` asks them for its parent (the root hub's, for a
   gone parent). Its parent's answer omits it, or the root hub's answer
   omits the gone ancestor no longer carried, and the removal follows.
   **Why not refuse every such START**, as the roadmap entry words it: a
   refused START leaves ME's `hidusb.sys` in the failed state again, and
   its later REMOVE waits as in step 5; the ghost START lets its own
   start reach the started state (the descriptors and the select
   answered from the copies; a START answered success alone is not
   enough, 43.49's run c2), so the STOP that follows the class driver's
   failure runs its stop routine and the REMOVE passes, as 47.10's legs
   read on ME.
4. **Its REMOVE** (the carried devnode removed by PnP before any START:
   the user's disable, or its parent's removal) clears the mark; the
   PDO is then a gone PDO whose REMOVE came before the answer that omits
   it (`RemoveReceived` set, on `GonePdos`): the next answer of its
   parent moves it to `RemovedPdos`, and the one after deletes it; or the
   parent's release (`HcdDevicePdoReleaseAll`) deletes it. The REMOVE
   asks for no relations: it may be the first of a parent's removal,
   and a relations request naming a parent PnP is removing is not one to
   make. On a contained controller nothing enumerates, so for a child of
   the root hub its port's wait lasts until the controller's stop and
   start, whose first answer runs the sweep, or its removal, whose
   release deletes it; below a gone parent still carried by another
   child, until that parent leaves (the limits below).

**What the user sees.** ME finishes starting. The device is listed in
Device Manager under the xHCI98 root hub with the problem ME gives a
device whose start failed, as in the uncontained control, and does not
work; XHCITR reports the controller contained as before. The next start
of Windows finds it again.

**Limits.**

- The carry lasts until the PDO's next START or REMOVE. With neither
  (ME sends no second START to a device whose start failed, in the
  control) it is the boot, and its port waits for as long. That
  matters only after a stop and a start of the controller in the same
  session (a disable and enable): the device on that port comes back
  only once its PDO's START has ended the carry. The object, its copies
  and any request held on it may outlive the carry: a PDO the
  controller's release orphans before its REMOVE is retired at that
  REMOVE on 98 SE and ME (`HcdPdoRetire`), deleted at a later relations
  answer of any of this driver's buses or kept for the boot.
- A REMOVE of the carried devnode before any START - the controller
  disabled, or removed - still meets the `hidusb.sys` wait of step 5,
  exactly as the uncontained control's failed tablet would (43.49's
  limitation, unchanged).
- A START refused for a hub or composite parent ends that parent's own
  mark only; while a child below it is carried it stays carried. After
  a stop and a start of the controller in the same session (a disable
  and enable), such a gone parent's START is refused - it is this
  driver's own hub or composite FDO, which no ghost serves - so its FDO
  stays stopped, PnP starts nothing below it, and its carried children
  keep it carried: that subtree stays listed, stopped and not
  enumerated again until it is removed or Windows restarts. A child of
  the root hub has no such parent and is started again (the ghost), and
  its carry ends.
- The order in which the class driver's failure and the relations query
  reach the configuration manager is ME's; if an answer came before the
  STOP (the PDO still started), the PDO is omitted as today and the hang
  is not prevented. ME's configuration manager runs one device at a
  time, so in the read order the START's failure and its STOP come
  first.
- 98 SE sends no STOP before the relations in the read order, so it is
  expected to see no carry; where it does (a STOP after a start that
  failed above the PDO, record 13 section 10.5.2's 98 SE reading), the
  device stays listed and stopped instead of being removed.
- Record 13 section 10.5.2's ghost limits stand: a class driver that
  waited inside its own START for a held data transfer would stall.

**NT untouched.** The mark is set only where `IoIsWdmVersionAvailable(1,
0x10)` is false, and every new branch reads it: the answer's carry
returns 0 at its first test on a controller that never marked a PDO
(`CarryArmed`), the serial check's new clause reads the same predicate,
the START's invalidation runs only when a mark was cleared, and the
REMOVE's clear writes 0 over 0. On 2000 and later no PDO is carried and
every answer, START and REMOVE behaves as before.

**Log.** `pdo.carry` (the PDO's serial) at the first answer that
carries each PDO (up to eight an answer; the rest at a later answer), `pdo.carry.end` (serial) at the START that ends a carry
an answer has used; the qemu flavour traces both.

**Host vectors** (`test\test_enum.c`): `XhciEnumStopCarry` over its four
inputs: 1 only for marked, stopped, no REMOVE, not reported missing.
The list walks (ancestor closure, the answer's membership, the serial
reservation, the invalidations) are not pure and are read in the legs.

**Legs** (the qemu flavour, 98 SE and ME q35 bases, `XhciQemuStageFault`
0x0001000C at the start unless said): before the change, ME with the
tablet at power-on over several starts (the rate), and ME with a
Bulk-Only stick in its place (the reading above); after it, ME with the
tablet at a root port (finishes starting; `pdo.carry`; Device Manager),
the same behind a hub (the ancestor carry), and behind a hub with the
controller then disabled and enabled (the stopped-parent limit), the
stick, and 98 SE with
the tablet; the carried PDO's next START where ME can be brought to send
one (the ghost, the STOP and REMOVE, the deletion); controls on both
systems: an uncontained start (`XhciQemuStageFault` 0), the hold armed
after the desktop (the stick's STOP and REMOVE as before), an ordinary
stop and removal (Device Manager's removal of the stick, the controller
disabled and enabled), a standby and resume, and a surprise removal
(`device_del` of the stick at the desktop).

**What the legs read (2026-11-15, `runs/run-50.md`, "MEBOOTDROP.1").**
The qemu flavour, the q35 ME and 98 SE bases, `XhciDmaStaging` 1 and
`XhciQemuStageFault` 0x0001000C (the hold with the containment armed)
or 8 (the log alone), each start a fresh overlay of one prepared image.

- *Before* (the `3.5.1.0` branch's build): ME with a tablet at
  power-on stopped at the start 4 of 4 (START, the class driver's
  GET_DESCRIPTOR failed DEV_NOT_RESPONDING at the containment, STOP,
  the relations without the PDO, no REMOVE; the screen at SeaBIOS's
  "Booting from Hard Disk..." or the ME logo at 7 minutes). ME with a
  Bulk-Only stick in its place finished starting (1 of 1): the same
  START, failed request and STOP, then the REMOVE reached the PDO -
  the reading above, that the wait is `hidusb.sys`'s and not the
  omission itself.
- *After*: ME with the tablet finished starting 5 of 5, `pdo.carry` at
  the first answer after the STOP, no REMOVE; Device Manager lists "USB
  Human Interface Device" with Code 10 ("This device is either not
  present, not working properly, or does not have all the drivers
  installed"). Behind a hub (`usb-hub`, the tablet at its port 1) the
  hub PDO and the tablet were both carried and ME finished starting.
  The stick at power-on is now carried too, listed with its failed
  start instead of removed. 98 SE with the tablet: the relations and
  the REMOVE with no STOP before, no carry, as before.
- *The carried PDO's next START* was not reached: Device Manager's
  Refresh, and the controller disabled and enabled, each brought the
  relations (the PDO carried again), its capabilities and its ids, and
  no START; after the enable the tablet was not enumerated again (its
  port waits on the carried PDO), the limit stated above, and Shut
  Down finished. The ghost START and the removal after it are unread.
- *Controls* (the log alone, no hold): an uncontained start with the
  tablet on ME and 98 SE (desktop, no carry); a device hot-plugged at
  the desktop and pulled (`device_del`; ME a stick, 98 SE a tablet):
  the relations and the REMOVE, Shut Down finished; Device Manager's
  Remove of the tablet on both (QUERY_REMOVE, the transfers cancelled,
  an ABORT_PIPE, REMOVE) and the device found again by Refresh (98
  SE through its wizard) and started; standby and wake with the tablet,
  on the images task 50.6's legs prepared (ME on an i440FX machine, 98
  SE on q35 with the disk on PIIX; that branch's package, this build's
  `xhci98.sys` put over it), the tablet's reads completing after the
  wake - the images made for these legs (ME installed on q35, 98 SE
  with the disk on AHCI) refuse the standby before any power request
  reaches this driver, ME's under the `3.5.1.0` build too. With the
  hold armed after the desktop and a device plugged then: on ME the
  stick is now carried (before: STOP, relations, REMOVE); on 98 SE the
  tablet is removed (relations, REMOVE, no STOP), no carry.
- Not read: 98 SE with the tablet behind a hub (the new hub's wizard
  held its start past the leg); NT, where no PDO is marked (above).

### 4.14 Controllers below xHCI 1.0: version 0.96 accepted (38.11, revision 14)

Roadmap-hcd task 38.11 (owner, 2026-10-13). Written before code, and
built on 2026-10-14 (branch `p38-096`); section 4.14.12 is the code as
built and where it departs from the text above. The owner's decisions,
those given with the task and the answers to the draft's questions D1 to
D7, are section 4.14.11 and are written into the sections below.

**Why here.** This record is the one about running on controllers that
depart from what the driver otherwise assumes, under section 2's rule:
tolerant where that costs a compliant controller nothing, gated where it
does not. Accepting a controller older than the specification revision the
driver was written to is that question asked of a whole revision rather
than of one fault, and its reporting lands beside `XhciIntervalCap` and
`XhciAvgTrbEsit`. Record 13 is the HCD's architecture and has no section
on which controllers are admitted; record 04 is the common buffer, which
nothing here changes; record 20 (38.10) is Linux's quirks keyed on vendor
and device id, and this gate is keyed on HCIVERSION instead.

#### 4.14.1 What the driver does today

Read from source on `phase-38` at `3da55ff8`; no 0.96 controller has run
under this driver.

- `XhciCheckCapDword0` (`src/xhci_caps.c`) returns `XHCI_HC_BAD_VERSION`
  (4) for an HCIVERSION below `XHCI_HCIVERSION_1_0` (`0x0100`) or of
  `0xFFFF`. It runs at step 0c of `XhciInitController` (`src/xhci_init.c`,
  `XHCI_INIT_STEP_CAP_SANITY`, 4), before any register with a side effect,
  and again inside `XhciDeriveHcInfo` (`src/xhci_caps.c`), which
  `XhciDeriveControllerInfo` (`src/xhci_init.c`) calls at the preflight
  decode, at the recheck after HCRST, at a restore's same-controller test
  (`xhciRestoreState`) and on each in-place reinitialization, which
  re-enters `XhciInitController`.
- The refusal goes through `xhciInitFailed` (notes `init.refused.step` 4
  and `init.refused.status` 4) to `hcdStartBody` (`src/hcd_ctl.c`), which
  notes `start.refused.step` and `start.refused.status`, stops, releases and
  returns `STATUS_DEVICE_CONFIGURATION_ERROR` from the start IRP: Device
  Manager's Code 10, "This device cannot start". Nothing has been written
  to the controller by then. The HCIVERSION itself is noted only after a
  successful decode (`hc.version`), so a refused start's log does not carry
  it, and this refusal path, unlike a delivery's (`hcdLogRefused`, task
  36.53), does not flush the note ring to DebugView.
- Nothing else in `src` tests the version. `XHCI_HCIVERSION_1_1` is
  defined and unused. HCCPARAMS2 is read on reach alone (CAPLENGTH and the
  window both to 20h), not on version (`XhciDeriveHcInfo`; record 13's and
  `xhci-data-structures.md`'s FSC notes).
- `XHCIQUAL` (`xhciqual/xhcicap.c`) refuses a register block whose
  HCIVERSION is below 0.90 as "not an xHCI register block" and otherwise
  prints the version (`report.c`); nothing in it states the driver's
  floor. `docs/usb-xhci-info/xhci-programming.md`'s init step 4 says
  "must be >= 0x0100". The host suite has two vectors for the refusal
  (`test/test_caps.c`).

The first-generation NEC/Renesas uPD720200 and uPD720200A (`1033:0194`)
are 0.96 parts, so today they stop at Code 10. So does ASMedia's ASM1042
(`1B21:1042`), which Linux says "reports 0.96" (`xhci-pci.c:453-461`).

#### 4.14.2 The sources

**No official public copy of the 0.96 specification was found in the
search of 2026-10-13, and none was fetched.** Revision 0.96 is dated
2009-05-08 in the revision history (1.2c p.21). Intel's xHCI page as the
Internet Archive holds it on 2010-03-25 and 2010-05-08 offered the draft
only to companies that signed the xHCI contributor agreement ("After the
above requirements have been met, your company will be sent a PDF
electronic copy of the (draft) specification") and links no download; the
capture of 2010-06-18, after revision 1.0 (2010-05-21), is the first seen
to link a public PDF, `xHCI_Specification_for_USB.pdf`, whose revision is
not verified, since it was not fetched. Copies on document-sharing sites
are not official sources and were not taken. `docs/references/README.md`,
"xHCI 0.96", records the search with the archive address of each capture. Every "0.96
says" below is therefore one of:

- **Appendix G of 1.2c, "0.96 Exceptions"** (p.591-592), Intel's own list
  of "the significant differences between 0.96 and 1.0 implementations":
  G.1, some 0.96 controllers may not generate an event for a Link TRB with
  IOC set while advancing to the next TD after a Short Packet; G.2, Force
  Stopped Event optional in 0.96, with HCCPARAMS bit 8 then being FSE; G.3,
  Secondary Bandwidth Domain Reporting optional, bit 9 then being SBD; G.4,
  USB2 L1 Capability optional, bit 16 of the Supported Protocol
  Capability's dword 08h then being L1C. It does not mention TD Size, so
  it is not a complete list.
- **Linux at the pinned commit** (`external/README.md`), which has driven
  0.96 controllers, the uPD720200 among them, since they shipped; its
  branches below 1.0 are in 4.14.3. Linux outranks this repository's own
  reading of hardware behaviour (`failure-diagnosis.md`, trust order), and
  it is evidence of what 0.96 parts accept, not of what the 0.96 text says.
- **Unread**, where neither says.

What a reading of the 0.96 text would settle, if a copy ever becomes
available to the project: the TD Size definition (the counterpart of 1.2c
4.11.2.4, p.198) and whether the last TRB of a TD carries 0; the Setup
Stage TRB's dword 3 bits 17:16 (1.2c Table 6-26, p.432); the Isoch TRB's
TBC, TLBPC, SIA and Frame ID (1.2c Table 6-34, p.436-437); HCCPARAMS bits
10 and 11 and the register at 1Ch; USBCMD bits 10 to 16 and CSS/CRS; the
Stopped completion codes 26 to 28; PORTSC bits 24 (CAS), 30 (DR) and 31
(WPR) and the PLS values; the Supported Protocol Capability's PSI fields
and Protocol Slot Type; HCSPARAMS2 bits 25:21; the Endpoint Context's
Average TRB Length, Max ESIT Payload and LSA; and what Evaluate Context
evaluates.

#### 4.14.3 The audit

Every behaviour of xHCI 1.0 and later the driver depends on, against what
0.96 may lack or define differently. "As 1.0 holds" means the owner's rule
- a 0.96 controller driven as a 1.0 one - is safe on the evidence; a row
naming a decision departs from it, as section 4.14.11 records. Linux citations are `file:line` at
the pinned commit.

| # | Behaviour | xHCI 1.2c | 0.96 | Linux below 1.0 | The driver | Verdict |
|---|---|---|---|---|---|---|
| A1 | TD Size | 4.11.2.4 p.198: packets remaining after this TRB, capped at 31, 0 on the last TRB | unread; not in Appendix G | `xhci-ring.c:3472-3516` (`xhci_td_remainder`): bytes remaining **including** this TRB, shifted right by 10, capped at 31 by `TRB_TD_SIZE`, and returned before the last-TRB test, so the last TRB carries its own length in KiB. MediaTek's 0.96 platform glue takes a third form (`3498-3509`): packets, the current TRB included (`trb_buff_len` set to 0 before the subtraction); platform glue, not PCI, so outside this design | `xhci_xfer.c`, `xhciXferTdSize`: the 1.0 form for every TRB, then 0 written over the last (control, normal and isoch builders) | the owner's rule (4.14.5); D1: Linux's form, the last TRB included |
| A2 | Setup Stage TRB Transfer Type (TRT) | Table 6-26 p.432, dword 3 bits 17:16 | unread | written only at 1.0 or later (`xhci-ring.c:3820-3828`, "xHCI 1.0/1.1 6.4.1.2.1"); 0 below | `XhciXferBuildControl` writes 0, 2 or 3 always | D2: 0 below 1.0 |
| A3 | Isoch TRB TBC and TLBPC | Table 6-34 p.436-437 | unread | both written as 0 below 1.0 (`xhci-ring.c:3895-3913`, "Only xHCI 1.0 host controllers support this field"; `3923-3945`) | `xhciXferIsoBurstFields` computes both; TLBPC is nonzero for a High-Speed high-bandwidth or SuperSpeed burst TD | D3: both 0 below 1.0 |
| A4 | Isoch Frame ID and SIA, CFC | 4.11.2.5 p.199-200; CFC HCCPARAMS1 bit 11, p.353 | bit 11 unread (Appendix G names bits 8 and 9 only) | Frame ID only when CFC reads 1, else SIA (`xhci-ring.c:4119-4126`), at every version | with CFC 0 a new flow's first TD may still carry a Frame ID, SIA after it (`XhciXferIsoFramePolicy`, `xhci_xfer.c`, `XHCI_ISO_START_FIRST_FRAME_ID`; called from `hcd_io.c`, `hcdIsoPublish`), where Linux writes SIA on every TD | D8, open: whether a 0.96 part honours a Frame ID at all is unread (4.14.6) |
| A5 | Block Event Interrupt (BEI) | p.437 | unread | never set below 1.0 (`xhci-ring.c:4035-4040`) | `XHCI_TRB_BEI` is defined and never written | not applicable |
| A6 | Event Data TRBs, ED, EDTLA | 4.11.5.2 p.209-210 | unread | - | no Event Data TRB is placed; an ED event is counted and discarded (`EventDataEvents`); the EDTLA is read only from a Stopped - Short Packet event | as 1.0 holds |
| A7 | Stopped, Stopped - Length Invalid, Stopped - Short Packet; SPC | 4.6.9 p.120-123; SPC HCCPARAMS1 bit 9 | G.2: Force Stopped Event optional; bit 9 is SBD, not SPC | - | the three codes are measurements latched for an abort (`XhciXferQueueStopped`) and selective suspend's evidence of an untouched head TD (`hcd_dev.c`, `hcdDevStopUntouched`); nothing waits for one, and without the event the suspend does not rewind (`hcd_susp.c`, `hcdSuspRewind`); SPC is never read | as 1.0 holds: a missing event takes the path that acts on no evidence |
| A8 | HCCPARAMS1 bits 8 and 9 | PAE and SPC, p.353 | FSE and SBD (G.2, G.3, p.591) | - | neither bit is read (`xhci.h` decodes AC64, BNC, CSZ, PPC, CFC, MaxPSASize, xECP) | not applicable |
| A9 | HCCPARAMS2 at 1Ch: FSC, LEC, CIC | 5.3.9 p.355-356; H.1 p.593 | unread; presumably reserved | read only above 1.0 (`xhci.c:5464-5467`), so never on a 0.96 part | read on reach at every version; FSC decides whether a Save State is taken (`xhciSaveState`), LEC whether Max ESIT Payload Hi is written and Mult reserved (`hcd_cfg.c`, `XhciPipeEndpointParamsAt`) | D4: taken as 0 below 1.0 |
| A10 | Contexts: Max ESIT Payload Hi, Mult | Table 6-8 p.414, 6.2.3.8 p.420 | unread | Mult with LEC only above 1.0 (`xhci-mem.c:1342-1355`) | follows LEC (A9) | with D4: LEC 0, Mult as today |
| A11 | Average TRB Length | 4.14.1.1, Table 6-11 | unread | EP0 8 at every version (`xhci-mem.c:1200`); a control endpoint's 8 at configure only at 1.0 or later (`1485-1487`); bulk 0 everywhere | EP0 8; others 1024, or `XhciAvgTrbEsit` | as 1.0 holds: a bandwidth hint, and Linux's EP0 8 is version-free |
| A12 | Streams: MaxPSASize, LSA, Secondary Stream Arrays | 4.12, p.227, Table 6-8 p.414 | unread | linear arrays at every version | linear arrays only (`xhci_stream.c`, `XhciStreamEndpointContext`), no secondary arrays | as 1.0 holds; ASM1042 is Linux's `XHCI_BROKEN_STREAMS` part, which record 20's `XhciStreamsOff` lists |
| A13 | Supported Protocol Capability, PSI, Protocol Slot Type | 7.2 | G.4: dword 08h bit 16 is L1C; PSI unread | PSI and the default speed IDs at every version | PSIC 0 takes the default speed IDs (`xhci_caps.c`); L1 and LPM never used; Enable Slot with slot type 0 (`hcd_enum.c`) | as 1.0 holds |
| A14 | PORTSC: PLS values, CAS, DR, WPR | 5.4.8 p.376-379 | unread (Appendix G names only L1's PLS use, G.4) | - | U0, U3, Resume and the SuperSpeed states read; CAS in Disconnected answered with a warm reset (`xhci_link.c`, 4.19.8); DR never read; WPR on SuperSpeed ports | as 1.0 holds where a bit a 0.96 part lacks reads 0: no CAS, so no warm reset on it; unread on silicon |
| A15 | Link TRB IOC, chain bit | 4.11.5.1; G.1 | G.1: an IOC Link TRB may give no event after a Short Packet | `XHCI_LINK_TRB_QUIRK` at 0.95 only (`xhci.c:5489-5492`); AMD and NEC 0.96 keep isochronous Link TRBs chained (`xhci.h:1775-1786`) | no Link TRB carries IOC (`xhci_ring.c`, `XhciTrbLink`); the chain bit follows the TD (`XhciRingEnqueueTd`) | G.1 not applicable; the AMD and NEC chaining **not taken** (owner): untested ground (4.14.10) |
| A16 | A Success event after a Short Packet | 4.10.1.1 p.175 | - | `XHCI_SPURIOUS_SUCCESS` only above 0.96 (`xhci.c:5482-5487`) | both kinds of controller handled: a second event on the TD's last TRB, of either code, or none (`xhci_xfer.c`, the mid-TD deferral and `XhciXferDrainSettled`) | as 1.0 holds |
| A17 | Hub slot: TT Think Time, Number of Ports; Configure Endpoint | Table 6-6 p.409-410 | unread | Configure Endpoint above 0.95, Evaluate Context at 0.95 (`xhci.c:5323-5362`); below 1.0 the think time on any hub, at 1.0 on a High-Speed hub only | Configure Endpoint; TTT only on a High-Speed hub | as 1.0 holds: only a High-Speed hub has a TT, so the two agree on real hubs |
| A18 | Reset Device completion | 4.6.11 | Linux: Context State Error for a slot in the wrong state (`xhci.c:4020-4040`) | treated as no-op | a failed Reset Device fails the RESET_PORT and the device is enumerated afresh (`hcd_enum.c`) | as 1.0 holds: 1.0 answers the same state the same way |
| A19 | Halt, HCRST, CNR, the BIOS handoff, the event and command rings, IMOD, MFINDEX | 4.2, 4.22.1, 5.4, 5.5 | unread | no version branch | no version branch | as 1.0 holds on Linux's evidence |
| A20 | USBCMD writes | 5.4.1 p.359-361 | bits 10 to 16 unread | - | every bit named at 1.2 written as each caller names it, the rest preserved (`xhciWriteUsbCmdFrom`, `XHCI_USBCMD_DEFINED_MASK`) | as 1.0 holds: 1.0 controllers already take the same writes to 1.1 and 1.2 bits |
| A21 | HCSPARAMS2 Max Scratchpad Buffers Hi (25:21) | 5.3.4 | unread | read at every version | read at every version | as 1.0 holds where the bits read 0 |
| A22 | LPM, HLC, BESL, U1/U2 timeouts | 4.23.5 | G.4 | HLC only at 1.0 or later (`xhci-mem.c:2137-2142`) | never enabled | not applicable |
| A23 | Version-keyed Linux rules elsewhere | - | - | babble on a 0.95 control endpoint (`xhci-ring.c:2196-2215`); runtime PM at 1.2 (`xhci-pci.c:510-512`); AMD 0.96 (`xhci-pci.c:308-309`) | - | 0.95 stays refused; PM not applicable; AMD 0.96 not taken (owner) |

Records 04, 13 and 17 cite no text that is 1.0-only beyond what the table
covers: record 13 section 11.7's TD Size note (point 5) and record 03's
TD Size vectors assume the packet form, and both gain the version form with
the code. `xhci-data-structures.md`'s TD Size formula and
`xhci-programming.md`'s init step 4 are the two documents that state the
1.0 floor or form as a rule.

#### 4.14.4 The gate: `XhciAccept096`

A REG_DWORD under the controller's driver key (owner, 2026-10-13: a value
there, on by default, 0 restoring the 1.0 floor). The name follows record
20's effect style (section 5.1 there); confirmed by the owner.

| Value | Meaning |
|---|---|
| 0 | the 1.0 floor, today's behaviour: below 1.0 is refused, Code 10 |
| 1, the default | the 0.96 floor: HCIVERSION from `0x0096` up is accepted |
| absent, another type or any other number | 1 |

- **Read** once per start by `hcdReadValues` (`hcd_ctl.c`), after
  `hcdStartBody` zeroes the extension, with `hcdReadDword`, and latched in
  the extension as the floor (`XHCI_HCIVERSION_0_96`, `0x0096`, or
  `XHCI_HCIVERSION_1_0`), so every check of a started lifetime - the
  preflight, the recheck after HCRST, the resume's recheck and each
  in-place reinitialization - uses the same floor; a change takes effect at
  the next start. The decision is a pure function, `XhciVersionFloor(found,
  value)`, with host vectors.
- **Applied** where the version is checked today: `XhciCheckCapDword0`
  takes the floor as a parameter, and `XhciDeriveHcInfo` passes it through.
  `0xFFFF` stays refused at every value. Everything below `0x0096` stays
  refused at every value: 0.95 and earlier are Linux's link-TRB quirk
  territory (A15), and the task opens 0.96, nothing older. A value from
  `0x0097` to `0x00FF` (no such revision was published; `0x009A` up is not
  BCD) is below 1.0 and is driven by the 0.96 rules.
- **One effective version.** Capability dword 0 is read in two places, the
  step 0c check (`XhciInitController`) and `XhciDeriveControllerInfo`, which
  runs again after HCRST, at a restore and at each reinitialization. Both
  pass the raw read through one function, `xhciEffectiveCapDword0`, before
  anything decodes it; in the shipping flavours it returns the read
  unchanged, and in the `qemu` flavour it applies D7's override (4.14.10).
  So the floor, the TD Size form, D2 to D4 and `XhciHcInfoEqual`'s
  same-controller test all see one version for the whole started lifetime.
- **Independent of `XhciTolerance`** (section 4.11): neither turns the
  other off.
- **F6 Setup** writes no values (`txtsetup.oem`), so the default, accepted,
  applies there.
- **What "accepted" changes is decided by version, not by the value.** The
  value decides only whether a controller below 1.0 starts; once one has,
  the forms of 4.14.5 and 4.14.6 follow from its HCIVERSION. On a 1.0 or
  later controller, at every value, what the driver programs into the
  controller and every policy that follows from it are unchanged; what
  changes there is diagnostic only - the version, floor and value in the
  log and the counter block, and the flush of a refused start (4.14.7).

#### 4.14.5 TD Size by version (the owner's rule)

Owner, 2026-10-13: a 0.96 controller is driven as a 1.0 one but for one
rule - the TD Size field is written in the units its version defines,
remaining packets from 1.0, remaining bytes divided by 1024 below it, each
capped at 31. NEC's isochronous Link TRB chaining and the AMD 0.96 rules
are not taken. The owner's answers to the draft's questions add three more
version rules (4.14.6, D2 to D4), so below 1.0 the driver departs from the
1.0 programming in four places: TD Size, Transfer Type, TBC and TLBPC, and
HCCPARAMS2, and by D8 a fifth, the Frame ID.

- `xhciXferTdSize` (`src/xhci_xfer.c`) gains the form as an input. The
  packet form is today's, unchanged. The 0.96 form is Linux's
  (`xhci_td_remainder`): the bytes of the TD not yet described before this
  TRB, this TRB's own included, shifted right by 10, held to 31. With the
  builder's running `LengthSum`, which is inclusive of this TRB, that is
  `(TotalLength - (LengthSum - length)) >> 10`.
- **The last TRB** (D1, owner: Linux's form) carries the same expression,
  as Linux writes it, so a last TRB of 1 KiB or more is not 0; the
  builders' "0 on the last TRB" clear (`XhciXferBuildControl`,
  `XhciXferBuildNormal`, `xhciXferBuildIsoPacket`) applies to the packet
  form only.
- **The form reaches the builders through their requests.** The three
  request structures (`XHCI_CONTROL_REQUEST`, `XHCI_NORMAL_REQUEST`,
  `XHCI_ISO_REQUEST`, `src/xhci_xfer.h`) gain a `TdSizeForm` field: 1 the
  packet form, 2 the 0.96 form, and **0 refused** as `XHCI_XFER_BAD_PARAM`,
  so a site that builds a request and forgets the field fails its first
  transfer in any leg and in its host vector instead of silently writing
  the 1.0 form on a 0.96 controller. Every site that fills one takes it
  from one helper on the controller's effective HCIVERSION; there are seven
  today: `hcdThreadControlQuiet` (`hcd_enum.c`, the enumeration's control
  transfers), `HcdHubRearm` (`hcd_hub.c`, a hub's status-change interrupt
  transfer), and in `hcd_io.c` `HcdIoMapped` (a client's control and normal
  transfers, two requests), `hcdIsoPublish` (isochronous), `hcdWidePump`
  (stage 2's chunks) and `hcdWideBigOne` (stage 3's one TD a URB on a
  stream ring).
- **Isochronous TDs** take the same form: TD Size is per TD
  (`PacketLength`), as today.
- Stage 2's and stage 3's long TDs (record 13 sections 11.6 and 11.7) are
  TDs like any other, each sized by its own length: stage 2 cuts a URB
  into several TDs, each with its own TD Size count down, and stage 3's
  single TD of up to 2 MiB saturates at 31 in both forms until its last
  31 KiB, or its last 31 packets.

#### 4.14.6 The other departures below 1.0

Taken by the owner (4.14.11), each as Linux has it. All are decided by
HCIVERSION below `0x0100`, with host vectors on both sides of it.

- **D2, Transfer Type 0.** `XhciXferBuildControl` writes TRT 0 in the
  Setup Stage TRB below 1.0, as Linux does (A2). The Data Stage TRB's DIR
  bit, which is the direction the controller acts on, is unchanged.
- **D3, TBC and TLBPC 0.** `xhciXferBuildIsoPacket` writes both as 0 below
  1.0, as Linux does (A3). The Max ESIT Payload refusal and the burst
  fields' range check stay.
- **D4, HCCPARAMS2 taken as 0 below 1.0.** `XhciDeriveHcInfo` does not
  believe the register below 1.0, as Linux does not read it there (A9):
  FSC and LEC 0. FSC 0 means a Save State is declined and a resume
  reinitializes the bus (section 4.6's notes; what an FSC-0 1.0 controller
  such as the E460's does today). LEC 0 changes nothing a 0.96 part can
  have, since SuperSpeedPlus came after it. Exactly at 1.0 the register is
  still read on reach, as today; Linux's stricter test (above 1.0) is not
  copied.
- **D8, taken (b) (owner, 2026-10-13): the Frame ID below 1.0.** With CFC 0 the driver may give a
  new isochronous flow's first TD a Frame ID (`XhciXferIsoFramePolicy`,
  `XHCI_ISO_START_FIRST_FRAME_ID`), where Linux writes SIA on every TD of a
  controller whose CFC reads 0 (A4). Linux's test is on CFC alone, not on
  the version, and what HCCPARAMS bit 11 means below 1.0 is unread, so
  Linux does not settle what a 0.96 part does with a Frame ID; Appendix G
  does not say either. Proposed, as this project's conservative policy
  rather than a rule taken from Linux: below 1.0, SIA on every TD
  whatever bit 11 reads. A CFC input of 0 alone would not do it - with
  `fresh` set `XhciXferIsoFramePolicy` still allows a Frame ID through
  `cfc || fresh` (`xhci_xfer.c`), and `XhciXferIsoStartPlan` answers
  `XHCI_ISO_START_SIA_NO_CFC` only when the flow is not fresh - so both
  helpers take a version input and, below 1.0, prohibit the Frame ID
  outright, independent of CFC and freshness: the policy leaves
  `Frames.Allowed` 0, and the plan answers `XHCI_ISO_START_SIA_NO_CFC` for
  every explicit-start case, a new flow's included, the reason then
  meaning no usable CFC capability rather than a zero bit. A client's ASAP
  stays `XHCI_ISO_START_ASAP`. At 1.0 and later both helpers are as
  today. The owner took it (4.14.11, D8 (b)).

#### 4.14.7 What a user and a report see

- **The counters, which reach DebugView at every verbosity.** The INFs
  write `XhciLogVerbosity` 0, at which `XhciLogAppend` (`xhci_log.c`)
  records no ordinary note, so the notes alone would not carry the
  version. The counter block (`src/xhci_counters.h`) gains three words
  after the last one in use at implementation, each `FFFFFFFF` from
  `HcdCountersStart` until written ("not read"):
  - **the versions**: bits 15:0 the register's own HCIVERSION and bits
    31:16 the effective one, both written at step 0c before the check, so a
    refused start has them; the two halves differ only under D7's override;
  - **the value read**: the DWORD, or `FFFFFFFF` when absent or of another
    type;
  - **the policy**: bits 15:0 the floor (`0x0096` or `0x0100`), bits 19:16
    the TD Size form (1 packets, 2 KiB; 0 before the check), bit 20 set
    when D7's override applied (`qemu` flavour only), bit 21 set once the
    versions word was written - at the step 0c read, whether or not the
    check then refused - the rest 0.

  Bit 21 is what says the versions were read: a register that answers all
  ones gives a versions word of `FFFFFFFF` too, so the word alone cannot
  tell "read `FFFF`" from "not read". `XHCISNAP` and the counter dump take
  bit 21 as authoritative, and print "not read" only when the policy word
  is `FFFFFFFF` or bit 21 is clear.

  `hcdLogCountersLocked` (`hcd_log.c`), which publishes a chosen list
  rather than the whole block, names the three, so the counter dump of a
  flush carries them with `XhciLogVerbosity` 0 and `XhciLogDebugView` 1.
- **The notes**, at verbosity 2 and above: `hc.version` before the step 0c
  check rather than after the decode, `hc.floor` (`0x0096` or `0x0100`)
  and `hc.accept096`.
- **The flush of a refused start (D5).** Every refusal of the initial
  start's `XhciInitController`, at any step, flushes the ring as a refused
  delivery does (`hcdLogRefused`'s `HcdLogFlush(hc,
  XHCI_LOG_REASON_FAILURE, 1)`): the one call is made where `hcdStartBody`
  handles that refusal, after its `start.refused.*` notes, at
  PASSIVE_LEVEL. So with `XhciLogDebugView` 1 DebugView shows why there was
  a Code 10 at any step. The other callers of `XhciInitController` - a
  resume's and a recovery's in-place reinitialization (`xhci_init.c`) -
  may run below PASSIVE_LEVEL, where `HcdLogFlush` emits nothing by design,
  so they keep today's reporting, unchanged; the flush is not moved into
  `xhciInitFailed`, which those paths share.
- **`XHCISNAP`.** It decodes the three counter words above, with
  `check-counter-names.ps1` and `xhcisnap/README.md`, and prints one line
  under the controller's identity, for example `xHCI 0.96 (HCIVERSION
  0096h): accepted below 1.0 (XhciAccept096 1); TD Size in 1 KiB units`,
  or `xHCI 1.00: XhciAccept096 1, not applicable`. Whether `XHCISNAP`
  can open a controller whose start failed is not read on any target, so
  a refused start's counters are carried by the flush above, which does
  not depend on it.
- **`XHCIQUAL`.** `report.c` follows the HCIVERSION line, for a version
  below 1.0, with what the driver does with it: from 0.96, "xhci98 drives it
  with XhciAccept096 at its default 1 (TD Size in 1 KiB units; untested
  ground); at 0 it refuses to start it (Code 10)"; below 0.96, "xhci98
  refuses to start it (Code 10)". Below 1.0 its HCCPARAMS2 line keeps the
  raw value but drops the FSC, U3C and CMC decode and the "FSC=0:
  suspend/resume re-enumerates" remark: what offset 1Ch means there is not
  established, and the line says instead that the driver takes FSC and LEC
  as 0 (D4). The raw value read on a 0.96 part shows what the register
  holds, not what it means. The verdict is unchanged
  (D6, owner): it judges the handoff, DMA, interrupts and ports, which the
  version does not change, and the line says what the driver does.
- **The release notes and README.** The value under "Registry settings";
  under "Untested ground" a row: "Controllers below xHCI 1.0 (0.96: NEC/
  Renesas uPD720200 and uPD720200A, ASMedia ASM1042 and others) - accepted
  by default (`XhciAccept096`), driven as xHCI 1.0 controllers but for
  the fields Linux writes differently on them (TD Size in 0.96's units,
  the control transfer's Transfer Type, the isochronous burst counts) and
  with the capability register at 1Ch ignored, so a resume from standby
  re-enumerates their devices; built against host tests and Linux's handling
  of the same parts; read on no 0.96 controller. Linux's chained
  isochronous Link TRBs for NEC and AMD 0.96 parts are not applied, so
  isochronous transfers (audio, video) on those parts are the least
  certain. Set `XhciAccept096` to 0 to refuse such a controller as
  `2.3.1.0` did." The readme template in `make-release.ps1` the same.

#### 4.14.8 Rows and gates

- `src/xhci98.inf`, `[Xhci.AddReg]` and `[Xhci.AddReg.NT]`, and
  `src/xhci98-amd64.inf`'s `[Xhci.AddReg.NT]`:
  `HKR,,XhciAccept096,0x00010003,1`, under task 34.1's rule, beside
  `XhciUsb2HubSpeedFallback`.
- `scripts/inf-gate/check-inf.ps1`, `$requiredValues`: a row `Name =
  "XhciAccept096"`, `Type = "0x00010003"`, `Default = "1"`, its `Why` (this
  section) and `DefaultWhy` ("1 is the driver's own default - absent,
  another number or another type is on; 0 restores the 1.0 floor of
  `2.3.1.0`"), with the self-tests' cases; the footprints
  `expected-footprint.txt` (Windows 98, 2000, Vista and 7 x86) and
  `expected-footprint-amd64.txt` (XP x64, Vista and 7 x64).
- `XhciAccept096` is not a `qemu`-only value and is in every flavour.

#### 4.14.9 Host tests

- `test_caps`: the floor at both values - `0x0095` refused at both,
  `0x0096` accepted at 1 and refused at 0, `0x0099` and `0x00A0` accepted
  at 1, `0x0100`, `0x0110` and `0x0120` accepted at both, `0xFFFF` refused
  at both; `XhciVersionFloor` for absent, 0, 1, 2, another number and
  another type; and HCCPARAMS2 ignored below 1.0 (D4) and believed at
  1.0, with the existing reach gates unchanged.
- `test_xfer`: TD Size in both forms - a three-fragment control data stage
  and a bulk TD worked by hand, 1023, 1024 and 1025-byte remainders,
  saturation at 31 for a 2 MiB stage 3 TD, successive stage 2 TDs each
  sized by its own length with a short final one, a zero-length TD, and
  the last TRB in Linux's form (D1); `TdSizeForm` 0 refused; TRT 0 below
  1.0 (D2) and today's values at 1.0. Each vector asserts every word of
  every TRB built, not the field alone, in both forms.
- `test_iso`: TD Size per TD in both forms; TBC and TLBPC 0 below 1.0
  (D3) and today's values at 1.0; and, with D8 as taken, a new flow at
  an explicit start frame on a CFC-0 controller - a Frame ID on its first
  TD at 1.0, SIA below it - every TRB word asserted.
- `test_iso` again, with D8 as taken: below 1.0 SIA on every TD with
  bit 11 read both clear and set, for a fresh flow at an explicit start
  frame and for one under way, each asserting the emitted SIA bit and
  Frame ID field and `XhciXferIsoStartPlan`'s answer
  (`XHCI_ISO_START_SIA_NO_CFC`; ASAP still `XHCI_ISO_START_ASAP`), and at
  1.0 the existing CFC-0 and CFC-1 vectors, the fresh-flow Frame ID
  included, unchanged.
- The counter words' packing (a pure helper): register `0x0100` and
  effective `0x0096` under the override, an unread pair, a read of
  `FFFF`/`FFFF` told apart from it by bit 21, a refusal's words, and the
  policy word for both floors and forms; `XHCISNAP`'s decoder on the same
  words (`-selftest`).
- `test_caps` again: a nonzero HCCPARAMS2 (FSC and LEC set) ignored below
  1.0 and believed at 1.0; QEMU's register reads 0, so only the host
  vector can show D4 holds. `xhciEffectiveCapDword0`: the override
  replaces HCIVERSION alone, keeps CAPLENGTH and the other bits, and is
  the identity in a shipping build; `XhciHcInfoEqual` holds across a
  re-derivation under the override.
- The seven request-filling sites of 4.14.5 are each reached by a vector
  or a leg below, and a site that forgets `TdSizeForm` fails.
- Every existing vector keeps the packet form and passes unchanged.

#### 4.14.10 Legs

No virtual machine presents a controller below 1.0: QEMU's answers
`0x01000000 | LEN_CAP` (`external/qemu/hcd-xhci.c:2753-2754`). So:

- **QEMU, the value absent, 1 and 0**, on Windows 98 SE and Windows 2000
  SP4, the two primary targets (XP and later in addition where the matrix
  reaches them): the controller starts, a mouse, a hub, a stick and USB
  audio work, and `XHCISNAP` shows HCIVERSION 1.00, the value and "not
  applicable" - the 1.0 programming unchanged at every value.
- **The 0.96 branches in a guest** (D7, owner): a `qemu`-flavour-only value,
  `XhciQemuHciVersion`, written by no INF, makes the driver decode a given
  HCIVERSION in place of the register's, as `XhciQemuPsiE460` replaces a PSI
  table (section 5). Its contract: read once per start by `hcdReadValues`
  under `XHCI_FLAVOUR_QEMU` and latched for the started lifetime; a DWORD
  from `0x0001` to `0xFFFE` replaces bits 31:16 of every capability dword 0
  read, through `xhciEffectiveCapDword0` (4.14.4), CAPLENGTH and the rest
  kept; absent, 0, `0xFFFF` and up, or another type applies nothing; when
  it applies, the note `qemu.hciversion` and the counter word of 4.14.7
  say so, beside the register's own value, so a dump never passes an
  injected version off as the controller's. The shipping flavours neither
  read nor apply it. QEMU reads neither TD Size, TRT, TBC nor TLBPC
  (`hcd-xhci.c` defines the isochronous fields and uses none of them), so
  a guest at `0x0096` runs the branches below 1.0 through the driver - the
  floor at 1 and 0 (the refusal's counter dump read in DebugView at
  `XhciLogVerbosity` 0, the register's `0100` beside the effective
  `0096`), the TD Size form, D2 and D3 on a mouse, a hub, a stick and USB
  audio for the isochronous request - on Windows 98 SE and Windows 2000
  SP4. Stage 2's and stage 3's paths need more than a stick: they run only
  on Windows 2000 and later with `XhciWideMap` 1 and a register-free
  adapter (`hcd_dma.c`, `HcdDmaWideOpen`, which keeps them off on Windows
  98 SE and ME), and stage 3 only on a SuperSpeed UAS stream ring. So on
  the Windows 2000 SP4 guest at `0x0096`, with `XhciWideMap` 1, a
  SuperSpeed UAS drive copies large files twice - once with the large
  stream rings refused by the `qemu` flavour's `XhciQemuBigRefuse` (record
  13 section 11.7, point 6's fallback) for stage 2, once with them for
  stage 3 - and the transfer-pipe counters (`XHCI_XPIPE_STATS`, record 13
  sections 11.6 and 11.7, point 9) show that `hcdWidePump` and
  `hcdWideBigOne` published TDs. It shows
  they break nothing in the driver, not that a 0.96 controller accepts the
  fields: QEMU ignores them, so their bit patterns rest on the host
  vectors, and QEMU's HCCPARAMS2 reads 0, so D4 rests on its host vector.
- **A 0.96 controller**: a uPD720200 or uPD720200A, or another 0.96 part,
  read by a user with one or on the owner's bench, after the cut if not
  before (the roadmap's task text), with `XHCIQUAL` first (its HCCPARAMS2
  line shows what offset 1Ch holds) and an `XHCISNAP` dump: a stick copied and compared, a
  mouse, a hub, and if anything isochronous is at hand, audio. Until then
  the release notes' row of 4.14.7 stands.

#### 4.14.11 Decisions (owner, 2026-10-13)

With the task (the roadmap's text): the floor becomes 0.96 behind a value
under the controller's driver key, on by default, 0 restoring the 1.0
floor; a 0.96 controller is driven as a 1.0 one but for the TD Size field,
written in its version's units; NEC's isochronous Link TRB chaining and the
AMD 0.96 rules are not taken.

On the draft's questions, each recommendation taken:

- **D1, TD Size on the last TRB below 1.0**: Linux's form on every TRB,
  the last included (4.14.5). Not taken: 0 on the last TRB with the KiB
  form on the others.
- **D2, Transfer Type in the Setup Stage TRB below 1.0**: 0, as Linux
  (4.14.6).
- **D3, TBC and TLBPC in an Isoch TRB below 1.0**: 0, as Linux (4.14.6).
- **D4, HCCPARAMS2 below 1.0**: taken as 0, as Linux, so FSC and LEC are
  0 (4.14.6).
- **D5, the refused start's flush**: every refusal of
  `XhciInitController` flushes the note ring, not the version's alone
  (4.14.7). Where it can act is narrowed: every refusal of the
  initial start, at any step; a resume's or a recovery's reinitialization
  may run below PASSIVE_LEVEL, where no flush is emitted, and keeps
  today's reporting.
- **D6, `XHCIQUAL`'s verdict**: unchanged; a report line only (4.14.7).
- **D7, the `qemu`-flavour version override**: taken,
  `XhciQemuHciVersion`, `qemu` flavour only, written by no INF (4.14.10).
- **The value's name**: `XhciAccept096`.

Raised in the review of the draft, answered by the owner the same day:

- **D8, the isochronous Frame ID below 1.0: taken (b).** (a) As 1.0: with CFC 0 a new
  flow's first TD may carry a Frame ID, as today; or (b) SIA on every TD
  below 1.0, whatever HCCPARAMS bit 11 reads - Linux's behaviour on a
  controller whose CFC reads 0, taken here as the project's own
  conservative policy. Recommended (b): whether 0.96 defines or honours a
  Frame ID, and what bit 11 means there, are unread (4.14.6).

Confirmed by the owner on 2026-10-14:

- **TSP in a Reset Endpoint command and SP in a Stop Endpoint command
  below 1.0: kept as on 1.0.** A 0.96 controller gets the same TSP
  (`XHCI_TRB_TSP`, the HCD's soft retry) and SP (`XHCI_TRB_SP`) as a 1.0
  one, as Linux sets both with no version test (`xhci-ring.c:4459-4471`,
  TSP on a soft reset; `4447-4457`, SP as its caller asks). What 0.96
  defines for either bit is unread.

#### 4.14.12 As built (2026-10-14)

The code follows sections 4.14.4 to 4.14.10; what it adds or settles:

- **The floor.** `XhciVersionFloor(found, value)` and the floor parameter of
  `XhciCheckCapDword0` and `XhciDeriveHcInfo` (`xhci_caps.c`). A floor
  other than `XHCI_HCIVERSION_0_96` - 0, a path that never latched one,
  included - is the 1.0 floor, so nothing can open below 1.0 by accident.
  The value and the floor are latched by `hcdReadValues` into the
  extension (`HciFloor`, `HciAccept096Value`), beside the qemu flavour's
  `HciQemuVersion`.
- **The effective version.** The pure `XhciEffectiveCapDword0(raw,
  override)` leaves an all-ones read all ones, so a device that is not
  decoding is still refused as one under the override; `xhci_init.c`'s
  `xhciEffectiveCapDword0` applies it in the qemu flavour only and is the
  identity elsewhere. Step 0c records the register's and the effective
  version, whether the override applied and, once the check accepted, the
  TD Size form (`HciVersionRegister`, `HciVersionEffective`,
  `HciOverrideApplied`, `HciTdSizeForm`, `HciVersionRead`; eight words
  appended to `XHCI_EXTENSION`, the parity `TrailingPad` keeps unchanged).
- **The counter words** are 173 to 175 (`HciVersions`, `HciAccept096Value`,
  `HciVersionPolicy`), set to FFFFFFFFh by `HcdCountersStart`. The HCD
  packs them (`hcd_ctl.c`, `hcdVersionCounters`) from the extension at
  `hcdReadValues` (the value and the policy) and again as soon as
  `XhciInitController` returns, on either outcome, before the refusal's
  flush: the same words section 4.14.7 asks to be written at step 0c,
  written by the layer that owns the counter block. `hcdLogCountersLocked`
  names them `hc.versions`, `hc.accept096` and `hc.version.policy`.
- **The notes.** `hc.version` is the effective version, before the check;
  under the override `qemu.hciversion` (the effective) and
  `hc.version.register` (the register's) follow it. `hc.accept096` and
  `hc.floor` at `hcdReadValues`, and in the qemu flavour
  `qemu.hciversion.value`.
- **D5.** One `HcdLogFlush(hc, XHCI_LOG_REASON_FAILURE, 1)` in
  `hcdStartBody`'s refusal branch, after `HcdCountersPoll`; nothing moved
  into `xhciInitFailed`.
- **The form in the builders.** `TdSizeForm` is the one version input the
  three builders take: `XHCI_TD_SIZE_FORM_KIB` selects the KiB TD Size, the
  last TRB kept (D1), TRT 0 (D2) and TBC and TLBPC 0 (D3); 0 or any other
  value is `XHCI_XFER_BAD_PARAM` before anything is written. The seven
  sites take it from `XhciTdSizeFormOf(HcInfo.HciVersion)`, the effective
  version.
- **D8.** `XhciXferIsoFramePolicy` and `XhciXferIsoStartPlan` take the
  effective HCIVERSION as a sixth argument; below 1.0 the plan answers
  `XHCI_ISO_START_SIA_NO_CFC` before the frame-sample test, so an explicit
  start with no sample below 1.0 reads `SIA_NO_CFC`, not `SIA_NO_SAMPLE`.
  The vectors are in `test_td` beside the 36.57 plan vectors they extend,
  not in `test_iso`.
- **`XHCISNAP`.** The section is "controller version", printed after
  "stream endpoints" from the counter block: the tool prints no
  HCIVERSION anywhere else, so there is no identity line to sit under.
  `-selftest-version` drives it over eight canned cases and a short block;
  `selftest.cmd` checks each line.
- **`XHCIQUAL`.** The line follows HCIVERSION for 0.90 to 0.FF (below 0.90
  the tool refuses the register block, as before); below 1.0 the
  HCCPARAMS2 line prints the raw value and says the driver takes FSC and
  LEC as 0, with no field decoded.
- **Host tests.** `test_caps` (`test_version_floor`), `test_xfer`
  (`test_td_size_control_forms`, `test_td_size_normal_forms`,
  `test_td_size_big_td_forms`), `test_iso` (`test_build_td_size_forms`),
  `test_td` (the D8 plan vectors) and `test_snap` (the three words'
  offsets); every existing request in the suites names the packet form.
- **The audit at code time** found nothing to change in the code beyond
  section 4.14.3; what it found to put to the owner is in the task's
  report (roadmap-hcd 38.11), not here.

### 4.15 A device that stops answering, and a controller stopped after repeated failures (tasks 50.30 and 50.31)

Design before code, 2026-11-17, for roadmap-usb4 tasks 50.30 and 50.31
(owner, 2026-11-17: both in this phase, the cut waiting for them).
Section 4.15.1 is what was read; 4.15.2 to 4.15.12 are task 50.30, 4.15.13
is task 50.31, and 4.15.14 and 4.15.15 the order, the risk and the
questions. The USB Tree's half (the display of a stopped controller and
task 50.32's restart) is design record 25 section 26.

#### 4.15.0 The owner's decisions (2026-11-17)

Taken on the design's first revision; they rule where any text below
seems to say otherwise.

1. **Enablement.** One value in the 0, 1, 2 form of the driver's other
   per-controller values, `XhciDeviceIsolation` (section 4.15.11): `0`
   today's handling on every controller; `1`, the default, the new
   handling (a device that stops answering fails alone) on the controllers
   whose abort behaviour has been read on real hardware, from a table in
   the driver; `2` the new handling on every controller, at the user's own
   risk. Written by both INFs at `1` without overwriting, in the INF
   gate's table, `tuning.md`, and the installer's and the USB Tree's
   settings rows; the snapshot shows the value read, whether the
   controller is listed and whether the handling applies. **If no reading
   exists at the cut, the table is empty and the default behaves as `0`.**
   (Amended by the owner for one controller, `8086:1E31`, on 2026-11-20:
   section 4.15.10.)
   The reasons and notes (section 4.15.8) are recorded at every value.
   `XhciTolerance` keeps its present meaning. (This replaces the answer
   first given the same day, off by default behind an on switch.)
2. **A hub** that itself stops answering keeps today's controller reset in
   this release, with its own reason; cycling a hub at its upstream place
   is later work. A device behind a healthy hub is covered.
3. **The spread of failures** across places is recorded only (notes and
   the snapshot's text): no automatic controller recovery from it in this
   release, and it is not described as telling a controller's failure
   from a device's.
4. **The rest.** Task 50.31's tries at a default of 5 s and a normal
   ceiling of 600 s, a value of `1` read as invalid (invalid values use
   the default), on by default, after the storm and resume corrections of
   section 4.15.13; the bus's own Bulk-Only medium check keeps its reset
   in this release with its reason recorded; on 98 SE and ME the Tree's
   restart is a remove and rescan, offered only if a guest reading shows
   that sequence works on both, and otherwise not offered there; and no
   user-visible wording promises more than the driver does.

#### 4.15.1 What was read

**The first example.** A field report on Windows 7 x64 with an Intel
7-series controller (`8086:1E31`), `3.5.0.0`, three saves at the default
settings. A
High Speed USB audio device at a root port, often starting and stopping
its stream, and two other devices on other ports. On a
SELECT_INTERFACE to alternate 0 of the audio device, Configure Endpoint
succeeded and the thread's SET_INTERFACE on EP0 got no completion of any
kind in 5 s. `hcdEp0Late` (record 13 section 10.5.1) stopped EP0 cleanly,
read the device present, and `XhciTolLateVerdict` answered RESET: the
controller recovery was requested (`ctrl.failed.here` 1; the site writes no
note of its own, so it is named by elimination). After each recovery the
other two devices enumerated again; the audio device's port reset at
High Speed, Enable Slot succeeded and Address Device timed out
(`cmd.timeout.trb` and `cmd.givenup` `0B0s0000`, type 11 Address Device),
which `hcdCommand` gave up and answered with a recovery. Three recoveries
about 5 s apart, the fourth refused by the window of section 4.6
(`ctrl.window.refused`): latched failed, every device on the controller
lost. The later saves follow restarts of the controller and fail the same
way within 5 to 16 s. The device evidently kept the fault through HCRST and
driver restarts, so only removing its power cleared it.

**The second example.** Later saves of the same field report, with a
Fresco Logic FL1100 controller on an add-in card. The same audio device, moved there straight from the Intel port, was attached
behind a SuperSpeed hub on the card, enumerated and worked briefly (very
short isochronous sessions), then the same sequence: a reset request with
no note naming it 5 s before the select ended failed (`cfg.select.end`
`03010407`, slot 3), so again the thread's control transfer deadline; then
Address Device time-outs on slot 3 after each recovery (`0B030000`) and the
latch, which took down the hub and every other device on the card for the
one device behind the hub. The other controllers of the same saves show
no recovery.

**What that shows.** The fault follows the device across two makers'
controllers and behind a hub, and unplugging the device cleared it (it
enumerated at once on the other card); a power-off was needed only because
the controller had latched. The driver turned one device's fault into the
controller's: (a) a present device's control transfer unanswered for 5 s is
a controller recovery even after EP0 stopped cleanly; (b) every command
`hcdCommand` gives up is a recovery, and the thread gives up at the same
5 s at which the engine's watchdog begins its abort (`HCD_COMMAND_WAIT_MS`
and `XHCI_COMMAND_TIMEOUT_MS`, both 5000), so it never sees how the abort
ends (`cmd.abandoned` at each recovery's start); (c) each of those
recoveries is charged to the controller's window; (d) a report cannot say
why a reset was requested, nor which device, and the USB Tree shows a
latched controller as started with empty ports.

**What Linux does** [linux] (facts, `external/linux`, no code taken): a
command past `XHCI_CMD_DEFAULT_TIMEOUT` (5000 ms) is aborted by
`xhci_handle_command_timeout` through `xhci_abort_cmd_ring`, which treats
the host as dead (`xhci_hc_died`) only when CRR does not negate within 5 s;
a ring that stops is restarted by `xhci_handle_stopped_cmd_ring` with the
aborted command's waiter told it was aborted; `xhci_abort_cmd_ring` writes
both halves of CRCR, because some controllers need it, and has a path of
its own for a stop event that never comes. Address Device
(`xhci_setup_device`), Configure Endpoint, Evaluate Context and Reset
Device have time-out handling in their synchronous callers; Reset Endpoint
and Set TR Dequeue completions are handled asynchronously by the event
handlers. The one command whose time-out tears the host down is Stop
Endpoint (the first branch of `xhci_handle_command_timeout`). Linux's
handling does not establish that a caller removes the device: removal
after an aborted state-changing command is this driver's policy, not a
reading of Linux. A control transfer the core
times out is cancelled through `xhci_urb_dequeue` (a Stop Endpoint), and
`xhci_handle_cmd_stop_ep` with `xhci_invalidate_cancelled_tds` moves the
dequeue past it and gives it back; no controller reset.

#### 4.15.2 The change, and what stays

Where `XhciDeviceIsolation` makes the new handling apply (section
4.15.11), a command or a thread control transfer that times out is
aborted; where the abort ends cleanly, only that device fails, its place
(a root port or a hub's port) is held until the device is unplugged or the
controller starts, and every other device keeps running; the controller is
reset where the abort itself fails, for the commands of section 4.15.3's
table that keep it, where the device's own teardown fails, and for a hub
whose own transfer gets no answer. Every reset request records its reason
and the place and slot involved (section 4.15.8), at every value. Section
4.15.13 then tries a latched controller again.

What stays: the engine's watchdog and abort ladder (`xhciCommandTimeout`,
the rungs and their 5 s intervals, `cmd.abort.stuck`, the divergence
resets of `xhciCommandRingStopped`), apart from section 4.15.9's
correction of the abandoned TRB's rewrite; every command failure answered
with a completion code (`hcdCfgFault` and issues 24 and 26 keep their
rules); client transfers keep having no deadline in the bus; the window of
section 4.6, the containment, the pinned buffer and the terminal release.
Where the handling does not apply, none of 4.15.3 to 4.15.6 acts.

#### 4.15.3 The thread's wait against the engine's abort (decision 1)

**Today.** `hcdCommand` arms the waiter, submits, and waits
`HCD_COMMAND_WAIT_MS` for its own completion (`hcdCommandWaitDone`); the
engine armed its watchdog at the submit with `XHCI_COMMAND_TIMEOUT_MS`.
Both are 5 s, so the thread usually gives up first, requests the reset
(`cmd.givenup`, `HcdSvcRequestReset`), and the reset's invalidation
abandons the command before the abort's events arrive; or the watchdog
writes CA first, the abort's Command Aborted reaches the waiter as a loss
(`XhciSlotCommandLost`, `CmdDoneLost`), and the thread takes that loss for
the controller's failure too ("only the reset settles that", the comment
at the `CmdDoneLost` test).

**The wait.** Where the new handling applies (section 4.15.11), the
thread's wait gains a second phase. Phase 1 is
unchanged: `HCD_COMMAND_WAIT_MS` for the command's own completion. When
it ends with no completion, or a loss arrives, the thread does not give up
but waits for the engine's verdict, in 100 ms slices with the drain asked
for between them as now, up to a new bound `HCD_COMMAND_VERDICT_MS`:

- `XHCI_COMMAND_ABORT_MS * (XHCI_COMMAND_ABORT_WAITS + 1) + 1000`, that is
  16 s after phase 1, 21 s after the submit at most: the abort ladder's
  own worst case (the abort written at 5 s, at most three abort
  intervals), plus a second's margin, so the thread's bound is a detector
  of a ladder that never ran and never a competing timeout (rule 1 of the
  thresholds in `xhci_hw.h`). It stays below `XHCI_COMMAND_AGE_MS` (32 s),
  the health poll's backstop. Both relations are compile-time asserts
  beside the existing one.

The verdicts, read under the controller lock in the hold that clears the
event, in this order:

1. **The command's own completion** (`CmdDonePA` equal to this submit's
   TRB): its code, as now. This includes the abort's ordinary race, where
   the command completed before CA took effect (4.6.1.2's implementation
   note: "the Command Ring will normally stop after the completion of a
   command"); the engine stays ABORTING until the Command Ring Stopped
   event, and the next submit's busy path waits that out as it does now.
2. **The abort ended cleanly.** New: when `xhciCommandRingStopped` ends an
   abort and puts the engine back to IDLE (`XHCI_CMD_ACTION_NONE`, the
   reported position adopted, with or without its No Op rewrite), it
   records the TRB the abort abandoned - the one the Command Aborted
   event named (`xhciCommandAborted` has already ended it as outstanding,
   so it is kept there), or the one still outstanding when the stop event
   came alone - and the command generation (`CommandAbortEndedPA`,
   `CommandAbortEndedGen`, under the lock) and
   calls a new HCD hook, `XhciSlotCommandAbortEnded`, which sets the
   waiter's mark (`CmdAbortPA`) and signals `CmdDoneEvent`. The waiter's
   mark equal to its own TRB, its generation unchanged and the controller
   not failed is the verdict **aborted**: the command was aborted, and in
   the checkpoint's terms did not happen
   (4.6.1.2: "An xHC implementation should 'checkpoint' the state
   associated with a command before a command is initiated ... the
   command's previous state should be restored", p.94), or, where the
   checkpoint is a "should" the controller did not honour, the device's
   teardown below takes back whatever it left (a Disable Slot is legal in
   every enabled state, 4.6.4, p.97 to 98). The checkpoint is guidance,
   not proof that nothing happened: the verdict means "the command was
   aborted; its device is torn down", and section 4.15.5a keeps every
   resource the command named until that teardown has proved it
   released.
3. **The abort failed or the ring diverged.** The engine has requested
   the reset itself (`cmd.abort.stuck`, `cmd.diverged`); the thread sees
   `ControllerFailed` and returns 0 with no request of its own (the
   reason is the engine's, section 4.15.8).
4. **Lost to an invalidation** (a stop, a quiesce, a recovery: the
   generation moved, `xhciCommandInvalidateLocked`): 0, nothing requested,
   as now. A stop, a removal or a power transition does not arrive this
   way while the thread waits (the stop joins the thread before its
   quiesce, and enumeration holds the power gate): those leave the wait by
   the signal below.
5. **The bound passes with none of these** (unreachable while the engine's
   ladder runs): 0 and the reset, reason `CMD_NO_VERDICT`.

*Implementation note on the order above* (task 50.30 step 3, reviewed
and revised; not a change of rule). The implementation clears
`CmdDoneEvent` immediately before the hold that reads the verdicts, not
inside it (`hcdCommandWaitIsolated`). No verdict can be lost by it:
every input the hold reads is a persistent mark written under the
controller lock (`CmdDonePA`, the abort mark `CmdAbort`,
`ControllerFailed`, the command generation, `CmdWaitLeave`), never the
event's state, so a mark published between
the clear and the hold is read by that hold, and one published after it
leaves the event signalled for the next slice's look.

**The distinct result.** An aborted command returns
`XHCI_CC_COMMAND_ABORTED` (25), never 0, so every caller can tell it from
"never completed" and from a completion code the controller gave. Counted
(`EnumCommandsAborted`, beside `EnumCommandsTimedOut`) and noted once per
command, `cmd.aborted` (TRB type in 31:24, slot in 23:16, endpoint in
15:8, 7:0 1 when a Command Aborted event came, 2 when only the stop event
came - 4.6.1.2's note: "a Command Completion Event with the Completion
Code set to Command Aborted may not be found on the Event Ring after an
abort operation").

**Which commands keep the reset.** Decided in `hcdCommand` by the TRB
type, from a pure table (`XhciTolAbortKeepsReset`), so no caller has to:

| Command | Aborted cleanly | Why |
|---|---|---|
| Address Device (BSR 0) | the known slot disabled, the port reset before the one retry, the place held after it (4.15.4) | the one command that waits on the device: the xHC issues SET_ADDRESS (4.6.5, p.98), and 4.6.1.2's implementation note names it: "An example of a command that may hang is the Address Device Command, because waiting for a SET_ADDRESS request to be acknowledged by a USB device is outside of the xHC's ability to control" (p.94). Linux: `xhci_setup_device`, `-ETIME`, no reset |
| Evaluate Context, Configure Endpoint, Reset Device, Reset Endpoint, Set TR Dequeue Pointer | the device is removed - never continued as if the command had or had not happened - and every resource the command named kept until the teardown proves it released (4.15.5a) | each names one slot that is known; none of them carries USB traffic, so a hang is unusual, but its effect is confined to that slot by the checkpoint, and where it is not, the device's teardown ends in a Disable Slot, which "terminate[s] any slot related USB activity" and frees the slot's resources (4.6.4, p.97). A controller that hangs these at several places is recorded by the spread records (4.15.6) and not acted on in this release (section 4.15.0 item 3). |
| Stop Endpoint | the reset | the owner's decision, and Linux's (`xhci_handle_command_timeout` halts and kills the host on it): an aborted Stop Endpoint leaves the endpoint as it was, Running with its TDs live in client buffers, and a Disable Slot may be issued only with "any active endpoints ... in the Stopped state or Idle in the Running state" (4.6.4, the note before the steps, p.97), so no teardown can follow it soundly |
| Enable Slot | the reset | the slot is not known: an aborted Enable Slot's completion carries no Slot ID, the checkpoint is a "should", and a slot the controller enabled and no record owns is a disagreement only HCRST settles (the rule `hcdPerform` already applies to a slot id no record can take) |
| Disable Slot | the reset, its slot's resources quarantined until it | it is the step that takes a slot's TRBs back; nothing after it can prove them out of the controller's hands (`hcdDisableSlotId`'s rule, kept), and "A Command Completion Event is always returned for a Disable Slot Command" (4.6.4, p.97), so none at all is the controller's fault |
| No Op, and every command naming no slot | the reset | no device to fail; only the controller can be at fault |

**Leaving the wait for a stop, a removal or a power transition.** Read
against the code: `hcdStopBody` calls `hcdThreadStop`, which joins the
controller thread, before `XhciStopController`, whose quiesce is what
invalidates an outstanding command; `HcdThreadWake` signals the thread's
`WorkEvent`, not `CmdDoneEvent`; and the enumeration runs holding the
power gate, which a power transition waits for. So nothing today ends a
command wait early, and with phase 2 a stop could wait up to 21 s. Worse,
several routes wait for the thread before any stop body runs. The new
handling therefore adds an admission signal of its own, `CmdWaitLeave`,
a word of one bit per route (set under the controller lock, then
`CmdDoneEvent` signalled; each route clears only its own bit), raised at
these points, each named in the code it lands in:

| Route | Raised | Cleared |
|---|---|---|
| The controller's stop body (`hcdStopBody`: a disable, a stop, a surprise removal on NT, an unload after a stop) | before `hcdThreadStop` joins the thread | at the next start (the lifetime ends) |
| The controller's removal (`hcdRemove`) | right after `PnpState` is set removed and **before** `HcdMsg4Gone`, `HcdIoLeave` and the wait on `RemoveEvent` for the outstanding I/O - so a removal with no surprise-removal request first (98 SE and ME report a departed controller with this IRP alone) closes admission before that wait | never (the object goes) |
| Mode 4's query-stop (`HcdMsg4QueryStop`, NT 5 with the message-interrupt mode) | before it takes the power gate | on a refusal (the bar lifted and the interrupter re-enabled, or the loss latched), after it leaves the gate; on success it stays until the stop body raises its own bit, or until `HcdMsg4CancelStop` (a cancel, or a lower driver failing the query) has reconnected, which clears it after leaving the gate |
| The root hub's removal (`HcdEnumDetach`, on every Windows, 98 SE and ME included) | before it waits for `EnumDetachDone` | when the detach has completed (`EnumDetachDone` set) |
| A device power IRP to D1 to D3 (`HcdControllerPower`'s `IRP_MN_SET_POWER` arm) | before its `HcdPowerGateEnter` | when that IRP leaves the gate |
| The direct transition (`hcdDirectTransition`, from `hcdPowerWork` for `HCD_POWER_WORK_DIRECT`: a system IRP whose device IRP could not be requested) | before its `HcdPowerGateEnter`, toward sleep or shutdown only | when it leaves the gate |
| The shutdown worker (`hcdPowerWork` for `HCD_POWER_WORK_SHUT`, `hcdShutFlush`) | before its `HcdPowerGateEnter` | when it leaves the gate |

A D0 (`hcdD0Finish`, `hcdPowerWorkD0`) raises nothing: it makes no
transition of a running controller, and waits for the gate as today. The
same IRPs reach the same dispatch on 98 SE and ME through NTKERN; mode 4
exists only on NT 5, so its row is NT's alone. Each slice of both phases
of the wait, and the busy path's (`hcdCommandBusy`), reads the word.

**What "left" is, and how long it lasts.** Set, the thread leaves the
wait with `HCD_CMD_LEFT` (0 to callers that only test for success). That
result is not a code a caller interprets: from that moment the
controller's `CmdLeftOwed` is set (under the lock, with the left
command's TRB type, slot and generation), and until it is settled:

- every later `hcdCommand` returns `HCD_CMD_LEFT` at once, nothing
  submitted - the one exception is the settlement's own submission, in
  the settlement state below, never an ordinary caller's;
- no input context is built (`hcdCfgBuildInput` and the enumeration's
  `hcdBuildEp0Input` for Address Device and Evaluate Context refuse), because the left
  command may still be reading the shared input context;
- the thread's own reset sites (the reasons of section 4.15.8 marked
  "thread") request nothing while it is set, counted
  (`Tol.Stats.ResetLeftSuppressed`); the engine's, the health poll's and
  the interrupt paths' requests are unaffected, since they report the
  controller;
- every caller's failure path reads `CmdLeftOwed` (or the device's
  `CmdLeft` mark, below) before it rolls back, frees or requests: a left
  command is owned as an aborted one is (section 4.15.5a's rule), so no
  refusal rollback runs, nothing it named is freed, and its pipes stay
  paused. The streams-open path, which today requests the reset on a
  result of 0, and every caller listed in 4.15.5a's table read it there;
- the device whose slot the left command named carries `dev->CmdLeft`
  (the TRB type), persistent until its teardown, as `AbortVerdict` is.

The enumeration and the configuration service then unwind with their
steps undone, and the power gate is dropped.

**The settlement state.** `CmdLeftOwed` has two states besides clear,
held under the controller lock beside the left command's TRB type, slot
and generation:

- `OWED`: the left command may still execute. Every rule of the list
  above holds, and nothing at all is submitted, the settlement's
  submission included.
- `SETTLING`: the engine has ended the left command - its completion, a
  clean abort, a loss, or an invalidation that moved the generation,
  recorded under the lock - and the controller thread's pass has taken
  its settlement, with no route's bit raised. The left command can no
  longer read anything, but what it did is not yet settled, so every
  rule of the list above still holds for every ordinary caller: each
  `hcdCommand` returns `HCD_CMD_LEFT` with nothing submitted, no input
  context is built, and the thread's own reset sites stay suppressed.

The one exception is a separate entry, `hcdCmdSettle`, which the
controller thread's settlement pass alone calls, and only in `SETTLING`
(in `OWED` it refuses, as `hcdCommand` does). It submits through the
same engine path as any command - the ring, the doorbell, the wait, the
watchdog and the abort - and admits, one at a time, only a command on
the settlement's list, which the pass writes under the lock as it moves
`OWED` to `SETTLING` and which `hcdCmdSettle` checks under the lock
before it places anything:

- the Disable Slot of a left Enable Slot's returned slot, once that Slot
  ID has passed the checks of "A left command that names no device"
  below;
- for each record the root hub's removal quarantined as owed (below),
  the Stop Endpoints its endpoints need and then its Disable Slot, in
  section 4.15.5a's order, each naming that record's own slot.

No other type is admitted, none of them builds an input context (Stop
Endpoint and Disable Slot take none), and the list is emptied as each
entry completes. A settlement command that a route's bit interrupts
(a stop, a removal or a power transition raised during its wait) is not
a second left command, and it is not left `SETTLING` either: once
every route's bit is clear it is treated as a settlement command that
failed, as the next sentences describe, its reset `CMD_KEPT` naming the
interrupted command's slot with its TRB type as the extra; while a bit
stays raised, the stop's teardown or a recovery's HCRST may still
settle the debt first (decided, owner, 2026-11-18; revision 23 kept the
debt `SETTLING` there and left it to the stop's teardown or a
recovery's HCRST alone). Otherwise the pass clears the debt only when
its list is empty. A
settlement command that fails keeps the reset, requested by the thread
as it clears the debt (so after the suppression has ended), and from
that moment Enable Slot stays refused (`CmdEnableHeld`, set with the
clearing, ended only by an HCRST or a controller start); a refused or
deferred reset request leaves it set, since a slot the driver could not
release may still be enabled.

An enumeration whose Enable Slot admission refuses - under the debt or
the hold - is deferred, not failed, and is reconsidered whenever
admission opens again (section 4.15.16 B, revisions 25 and 26).

**Who settles a left command.** The command stays outstanding in the
engine, and one of these settles it, whichever comes first:

1. **The engine's own end of it** (a completion, a clean abort, a loss),
   seen by the thread at its next pass once the route's bit has cleared:
   the device with `CmdLeft` is then failed alone as an aborted command's
   would be (4.15.5a: removed, never continued, its resources retained
   until the teardown's Disable Slot), whatever the completion said,
   since nothing waited for it; an engine verdict of reset is a recovery
   (the third).
2. **A controller stop's teardown.** The stop's quiesce invalidates the
   command **in software** (the generation moves); that is not evidence
   that the controller stopped reading what the command named. The
   release of what it owned waits for the stop's own evidence - the halt
   and reset, or the containment's proof (section 4.6) - as the teardown
   already waits for every ring.
3. **A controller recovery's HCRST**, or the terminal release's proven
   route (section 4.6), which releases every quarantined record.

**A root hub removed while a command is left.** `HcdEnumDetach` does not
enter the controller's stop body, and no controller stop need follow
(the controller stays started; a later root-hub start re-attaches). The
detach's drop meets `HCD_CMD_LEFT` at each Disable Slot: each record is
quarantined (off its port, kept in `SlotDevice[]`, as a Disable Slot
that failed leaves it today) with the quarantine marked "owed", and no
reset is requested. The owner of the settlement is the controller thread,
outside any root-hub lifetime: once the detach bit has cleared and the
engine reports the left command ended (the first case above), its pass
moves the debt to `SETTLING` with each owed record on its list, stops or
checks each owed record's endpoints and issues the Disable Slots then
(section 4.15.5a's order), through `hcdCmdSettle`, since ordinary
admission stays closed until the list is empty; a Disable Slot that fails
there keeps the reset with the record quarantined; a stop or a recovery
first settles them as the second and third cases do.

**A left command that names no device.** The first case fails a device;
the second and third need a controller stop or an HCRST. A left Enable
Slot has no device record, and while it is outstanding no slot anyone
owns; a root hub's removal brings no stop. So the thread settles a left
command that names no device itself, at the first case's pass, from the
engine's own end of it, matched by the address and generation
`CmdLeftOwed` keeps (the completion handler records a left command's
completion beside it, its code and Slot ID, under the controller lock):

- an Enable Slot that completed with success returned a slot nothing
  placed: the thread takes that Slot ID from the recorded completion,
  never enters it in `SlotDevice[]` or the DCBAA, and, in `SETTLING`,
  issues its Disable Slot through `hcdCmdSettle` before `CmdLeftOwed` is
  cleared; a success releases the slot, a failure keeps the reset
  (`DISABLE_SLOT`) and sets `CmdEnableHeld`. An Enable Slot that
  completed with an error code allocated nothing, and the debt is
  cleared;
- **before that Disable Slot, the returned Slot ID is checked**, under
  the lock, and the Disable Slot is placed only if every check holds: the
  completion was matched to the left command by address and generation;
  its code is Success; the Slot ID is in 1 to the number of slots this
  start enabled (`CONFIG.MaxSlotsEn`); no record owns it (`SlotDevice[]`
  empty there, and no device, quarantined record or owed record names
  it); its DCBAA entry is zero; and it is not on the settlement's list
  already. A Slot ID that fails any of them is not disabled - a Disable
  Slot of a slot another record owns would take a live device's slot
  from under it, and one outside the enabled range is the controller and
  the driver disagreeing - and the debt is settled as an Enable Slot with
  no Slot ID to trust (next item), the check that failed noted
  (`cmd.left.slot`, the Slot ID in 15:8 and the check in 7:0);
- an Enable Slot whose end gives no Slot ID to trust - the engine's clean
  abort (25: whether a slot was allocated is unknown), its loss, an
  invalidation that moved the generation before any completion, or a
  completion the engine could not match - requires the reset (`CMD_KEPT`,
  TRB type 9), requested by the thread as it clears the debt, so after
  the suppression of the thread's own reset sites that the debt imposes
  has ended; no Enable Slot is issued until that HCRST (`CmdEnableHeld`);
- a No Op owns nothing and is cleared at its end, whatever it was;
- any other command with no device (none is issued through `hcdCommand`
  today; a new one is a change to section 4.15.5a's table first) requires
  the reset, as an Enable Slot with no trusted end does.

A controller stop or a recovery's HCRST that comes first settles each of
these, as the second and third cases do: every slot goes with the reset.

**Across a suspend and a resume.** A power-down's own path halts the
controller; the suspend abandons the outstanding command rather than
completing it (`xhciSaveState`), and a successful Restore State rebuilds
the command ring and re-synchronises CRCR (`XhciResumeController`'s restore), so the
old command cannot execute again after the restore. Its outcome is still
unknown and may have changed the device's state (a Configure Endpoint,
an Evaluate Context, a Reset Device). So, before any pipe of a device
with `CmdLeft` resumes: where the restore succeeded and the slot was
kept, the device is removed through the unplug path (section 4.15.5's
cycle: its endpoints stopped or checked, Disable Slot, its place
enumerated again) and its pipes stay paused until then; where the resume
reinitialized the controller (no saved state, or the restore failed),
every slot is gone with the HCRST, and the resume keep (task 45.23,
`hcdRkeepFlags`) refuses to keep a device with `CmdLeft`, so it is
enumerated afresh. An Address Device left before it had a PDO is a
failed step, its slot disabled by the machine's own path after the
resume.

**A left command's debt across the resume.** A reinitialization (no
saved state, or a failed restore) is an HCRST: every slot goes with it,
and it clears `CmdLeftOwed` and `CmdEnableHeld` whatever was left, with
every `CmdLeft` mark kept for the reconciliation above. A successful
Restore State is not: it rebuilds the command ring, so the left command
can no longer execute, but it disables no slot: the restore brings back
the internal state the save stored (xHCI 4.23.2, p.313 to 315), and
nothing in it disables one, so a slot a left Enable Slot allocated before
the save is to be taken as enabled again after it - and that slot has no
device record and so no `CmdLeft` mark to reconcile it. (On QEMU every
restore reports SRE and the resume reinitializes, so this case is read
there only as the reinitialization; `docs/usb-xhci-info/xhci-data-structures.md`,
"Controller Save/Restore State".) So the restore clears the debt
only for a left command that names a device, whose mark carries the rest,
and only when no record is quarantined as owed, no settlement command
was interrupted and no reset is already owed in the debt - a
settlement's failure reason, an overflowed list, or a slotless end
owing it (otherwise it keeps the debt `SETTLING`, so the pass settles
or fails it);
for one that names none it moves `OWED` to `SETTLING` and keeps the debt,
the suspend's abandonment recorded as the command's end:

- a left Enable Slot whose success completion, with its Slot ID, was
  recorded under the lock before the suspend (the engine had ended it,
  and the thread's settlement had not yet run): its slot was enabled
  when the state was saved and is enabled again after the restore, so
  after the resume, at the thread's first pass with no
  route's bit raised and the power gate left, the returned Slot ID passes
  the checks above (`MaxSlotsEn` read again after the restore) and its
  Disable Slot goes through `hcdCmdSettle`; any check failing, or the
  Disable Slot failing, requires the reset as above;
- a left Enable Slot that completed with an error code before the suspend
  allocated nothing, and the debt is cleared;
- a left Enable Slot with no recorded end - the suspend abandoned it
  while it was outstanding, so whether the controller allocated a slot
  before its state was saved is unknown - is an Enable Slot with no Slot
  ID to trust: the thread requests the reset (`CMD_KEPT`, TRB type 9) at
  that first pass, as it clears the debt, and `CmdEnableHeld` refuses
  Enable Slot until the HCRST, so no device is enumerated onto a
  controller whose slots the driver does not know;
- a No Op is cleared at the restore.

Until the debt clears, the resume's own reconciliation of devices with
`CmdLeft` waits behind it: it needs Stop Endpoints and Disable Slots
through ordinary admission, which `SETTLING` keeps closed.

**Implementation note: what "request nothing" means for a mandatory
reset** (task 50.30 step 3, reviewed and revised). The suppression of the
thread's own reset sites is a deferral, not a discard: a reset a thread
site requests while a debt stands is not lost. That covers the requests
the sites make; where a site requests none for a command the debt
affects, the obligation is recorded at the points below, and nothing
else. A thread request made while the debt stands is counted
(`ResetLeftSuppressed`) and the first one kept with its reason, extra and
place, read when it was made (`XhciLeftDebtDefer`); the settlement pass
requests it as the debt clears, before the settlement's own reset, and a
successful Restore State that clears the debt keeps it for the next pass
with no route's bit raised; an HCRST, which is what it asked for, drops
it. A left Stop Endpoint that names a device also owes the reset whatever
its end (the table above): deferred as the leave takes the debt, with the
device's place (`XhciLeftDebtLeave`, `XhciTolLeftNamedReset`), so a
successful Restore State that clears the debt cannot erase it before the
list is written. A Stop Endpoint that admission refuses under a debt,
outside the teardown, owes it too, deferred at the refusal, whichever
caller issued it (`XhciTolAdmitOwesReset`; selective suspend's stop
requests none of its own). A successful Restore State keeps the debt
`SETTLING`, even for a left command that names a device, while a record
is quarantined as owed, since only the settlement takes such a record
back, and while a reset is already owed in it (an overflowed list, a
slotless end that owes it), so that the pass clears it and requests that
reset (`XhciLeftDebtRestored`). A tainted scratch does not keep the
pass out: the settlement fails there with no command issued, and the
debt is cleared and the reset requested (`XhciLeftDebtStep`); only a
failed controller's recovery settles the debt instead. An interrupted
settlement command fails the settlement at the first pass with every
route's bit clear (while a bit stays raised, a stop's teardown or a
recovery's HCRST may settle the debt first), in the same locked hold
that clears the debt and sets `CmdEnableHeld`: every record still owed
becomes an ordinary quarantined record, nothing is freed, and the
interrupted command is not issued again (`XhciLeftDebtPass`,
`XhciLeftDebtFail`). The reset it owes is `CMD_KEPT`, naming the
interrupted command's slot with its TRB type as the extra (the first
one's, if a second was interrupted), requested after the deferred ones
once the lock is let go. Every failure of the settlement takes that
same single hold - an entry's Stop Endpoint or Disable Slot answered with
an error, an overflowed list, a tainted scratch - so no failed
settlement's reset is left in a standing debt for a successful Restore
State to clear (`XhciLeftDebtFailClear`). These are the points that
record an obligation under a debt; a reset that none of them records is
not covered by them. A successful Restore State keeps an
interrupted settlement's debt too, so the pass fails it. The settlement
lists its owed records child before parent, by each record's own tier
(`XhciLeftDebtAddRecords`), as the ordinary teardown disables them.

Without the new handling the wait is today's, which a stop already
waits out, and none of this applies.

**Alternatives rejected.** (a) The thread writing CA itself: two owners of
CRCR, and the engine's ladder and its "only the stop event ends an abort"
rule (the comment over `xhciCommandRingStopped`) would have a second
writer it cannot see. (b) Lengthening phase 1 alone: the thread would then
outlast the abort but could still not tell an aborted command from one
lost to an invalidation or to the engine's own reset. (c) A shorter
watchdog for Address Device (a SET_ADDRESS should be answered within
50 ms): 5 s is the specification's own figure for the abort (4.6.1.2,
p.94) and Linux's default; a shorter one would make slow but working
devices fail alone, which no reading asks for. (d) Treating every aborted
command alike (Linux's choice for all but Stop Endpoint): rejected for
Enable Slot, Disable Slot and the slotless commands for the reasons in the
table.

#### 4.15.4 After an aborted Address Device (decision 2)

`hcdAddress` sees 25 and fails the enumeration step with a new cause,
`XHCI_ENUM_FAIL_UNANSWERED`, beside `XHCI_ENUM_FAIL_ADDRESS` (which stays
for an Address Device answered with an error code). Then, in order:

1. **A device behind a transaction translator first.** A Full or Low
   Speed device behind a High Speed hub (`TtSlot` not 0) has its hub's TT
   buffer cleared for EP0 at address 0 (`HcdHubClearTt`, as for a
   Transaction Error), **before** step 2 can free the record that holds
   its routing - or the routing (the TT hub's slot, its port, the
   multi-TT flag) is copied into the clean-up first. A High Speed device
   behind a hub uses no TT and gets no clear. A clear that fails leaves
   the child's place held all the same (safe); a clear that times out is
   the hub's own unanswered transfer, which keeps the controller reset
   (section 4.15.0 item 2).
2. **The slot.** No context is read: after a clean abort the slot is
   Enabled again if the controller honoured the checkpoint, and Default or
   Addressed if it did not, and a Disable Slot is legal in all three
   (4.6.4, p.98). Its preconditions hold: the enumeration has queued no
   EP0 TD, so EP0 is at most Idle in Running, and the only command that
   named the slot has completed (the abort's). The machine's existing
   `DISABLE_SLOT` action issues it (`hcdDisableRecord`); a Disable Slot
   that fails or times out keeps the reset, its record quarantined as
   now, with step 4's hold set before the request.
3. **The port reset, then one retry.** After an abort the USB device's
   address state is unknown (4.6.5's SET_ADDRESS may or may not have
   reached it), so the retry begins with a port reset, which puts the
   device back at the default address: the machine's retry re-enters
   `Reset`, at a root port and at a hub's port alike. One retry only: a
   hub port's three attempts (`XHCI_HUB_PORT_ATTEMPTS`) are capped at two
   for the cause `UNANSWERED`. Two aborts that end promptly cost about
   10 s of the controller thread; two commands that reach the full
   verdict bound (section 4.15.3) about 42 s, before the enumeration's and
   the teardown's other work.
4. **Failed, with its cause, and held.** On the last failure the machine
   is `Failed` as now, the fail note (`enum.port.fail`) carrying the new
   cause, and the place is held with a new hold reason,
   `XHCI_TOL_HOLD_UNANSWERED` (4), powered, released as
   `XHCI_TOL_HOLD_REENUMS` is (section 4.5: the stable-disconnect
   evidence, or a controller start; `XhciTolLocObserveChange`, which
   releases hold 1 alone today, releases both). Hold 4 suppresses **every
   automatic enumeration and reset attempt** at the place: no connect is
   fed, no PED or hub-port-disable reconnect, no warm reset of a
   SuperSpeed link (`hcdPortChanged`'s and the link recovery's), no
   RESET_PORT on the bus's own behalf; change bits are still acknowledged,
   and the disconnect that releases it is still observed. A rescan after
   an unrelated recovery leaves the place alone. **Where it is read.**
   Not only in the connect feed: today `hcdPortChanged` takes
   `XhciLinkPortFeed`'s action (`XHCI_LINK_ACT_WARM_RESET`, the stuck-link
   quirk's count included) independently of what the feed passes to the
   machine, and its in-flight warm-reset branch can request the reset
   (`WARM_RESET`). So the hold is read in `hcdPortChanged` after the
   change bits are acknowledged and the companion hold handled, and
   **before** the link feed's action is taken: a place held with reason 4
   takes no link action (counted, `Tol.Stats.HeldLinkSkips`), feeds no
   connect, and starts no warm reset; its CCS and the edge still go to
   the stable-disconnect observation. The hub's own port path reads it the
   same way before any port reset or warm reset of a hub's port. At a
   root port the sample's own stable-disconnect observation comes before
   the hold is read, so a sample that releases the hold is not
   suppressed (section 4.15.16 A, revision 25).
5. **The port disabled where that keeps the unplug visible.** A USB 2.0
   root port is disabled by writing PED 1: its Disabled state keeps CCS 1,
   and "A disconnect detect (CCS = '0') in the Disabled or Enabled state"
   takes it to Disconnected with CSC set (4.19.1.1.2, p.275), so the
   unplug that releases the hold is still seen, and the device, with no
   SOF, suspends. A USB 3 root port is **not** disabled: its Disabled
   state is (PP, CCS, PED, PR) = 1, 0, 0, 0 (Figure 4-27, 4.19.1.2.1,
   p.279), which would hide the unplug; its slot is disabled and nothing
   addresses the device, and link changes there are acknowledged under
   hold 4's rule. A hub's port is disabled as the machine's give-up
   already does (`HcdHubPortDisable`), and the hub's status pipe reports
   its unplug as now; a leg reads that the disabled child's disconnect is
   still seen.
6. **Holds across a recovery.** A root place's hold survives an in-place
   recovery; a hub's places are initialized when the hub starts again
   (section 4.5, "A hub's port budgets start afresh"), so a device behind
   a hub may be tried once more after a recovery, costing that hub port
   its two attempts per recovery, bounded by the window and section
   4.15.13's backing off. A table of held places surviving a recovery was
   considered and rejected: state for a bounded cost.

The other devices: the command ring is blocked behind the hanging Address
Device for its 5 s, so a command another device needs waits behind it, as
it waits behind the thread today; their transfers keep running.

At a hub's port the hub stays up: its slot, its status pipe and the
devices on its other ports are untouched, and the hold is the hub port's
(`Tol.HubLoc[]`), released on that port's `C_PORT_CONNECTION` with the
connection gone for the stable-disconnect interval, through
`HcdTolLocObserve` from the hub's status as for any hub port today.

#### 4.15.5 A present device whose control transfer got no answer (decision 3)

This is `hcdEp0Late` (record 13 section 10.5.1), which already stops EP0,
reads the departure and retires the record. One verdict changes, where the
new handling applies: a path that reads present no longer answers RESET
for a device that is not a hub.

- `XhciTolLateVerdict(left, present, hub, published, isolate)` gains the
  last input: gone, LEFT as now; a hub, RESET as now (section 4.15.0 item
  2), its reason `HUB_UNANSWERED`; otherwise, with `isolate` set, a
  published device is **CYCLE** and one with no PDO yet **LEFT** (its
  enumeration's failed step tears it down), present or not; with
  `isolate` clear, today's rows. Host vectors at both values.
- The stop and the retire keep their rule: a Stop Endpoint that does not
  succeed (an error code, or no completion: Stop Endpoint keeps the reset
  in 4.15.3's table), EP0 left Running or Disabled, Halted with no
  completion, or a retire that fails (another record queued, the Set TR
  Dequeue failing or aborted) keeps the reset with the scratch tainted,
  exactly as now (`XhciTolLateStopped`, `XhciTolLateRetired`, unchanged),
  since this path needs proof that the shared scratch can be reused.
- **CYCLE** for a present device: the device is first marked failed
  alone (`dev->FailedAlone`, a persistent verdict, under the controller
  lock), then `HcdCfgCycle` charges its place (one re-enumeration of
  section 4.5's budget, once per pending cycle, `XhciTolCycleCharge`;
  section 4.5's stable-progress re-arming unchanged) and queues the cycle;
  refused, the device still goes and the place is held
  (`XHCI_TOL_HOLD_REENUMS`). The cycle is the unplug path (`HcdEnumCycle`,
  `hcdSubtreeGo` with `HCD_GO_UNPLUG`): for a composite device the parent
  and **every function PDO** are reported missing, not only the function
  whose request timed out; its endpoints stopped, its slot disabled, the
  place enumerated again. If the device then hangs its Address Device,
  4.15.4 holds the place.
- **A device with no PDO yet** fails its step as now; where the step's
  failure was this late path with the device present, the attempt's cause
  is `UNANSWERED`, so its last failure holds the place as 4.15.4 says.
- **A hub's own control transfer** unanswered keeps the reset (section
  4.15.0 item 2), with `HUB_UNANSWERED` recorded; so does `hcdCreateLate`
  for a hub. Cycling a hub at its upstream place (its upstream-port
  action, charge, child teardown order and PnP wait) is later work.
- **The door's transfer** keeps its withdrawal first (`hcdEp0Withdraw`);
  where that does not finish it falls to this path, as now.

**What the client sees.** The thread's transfer carried a client request
only for a select or a configuration request, or a control request the bus
makes on a client's behalf. That request completes once, from its active
slow request's owner, with `USBD_STATUS_DEVICE_GONE` and the IRP
`STATUS_DEVICE_NOT_CONNECTED` through `XhciPipeNtStatus`
(`STATUS_DELETE_PENDING` on a closing PDO), on every Windows. Today's
`hcdCfgSelectAnswer` does not give that for a present device:
`hcdCfgDeviceLeft` treats neither `Ep0Stuck` nor an owed cycle as a
departure, so the device's `FailedAlone` verdict is made an explicit input
of the answer, and a second select arriving before the teardown is
answered the same at the gate. That is what the same client was shown
after today's recovery, whose invalidation completed the request as gone
and removed the device. The select completes while the PDO is still
listed, as record 13 section 10.5.1 already completes a departing device's;
section 4.0's rule against `STATUS_DEVICE_NOT_CONNECTED` on a listed
device guards against Windows 98 SE's `hidclass.sys` resubmitting a read at
once, and this path completes a select or a configuration request, never a
read. The device's queued and parked requests depart through their
existing owners: held on 98 SE and ME (record 13 section 10.5, step 3),
completed as gone once the PDO is reported on 2000 and later with task
50.28; cancelled ones through task 50.26's encoder. No second completion
mechanism is added. Open handles stay handles to the departed instance:
applications, audio players and storage users may need to open the device
again after it re-enumerates, and a storage write in flight may have
failed. On 98 SE and ME retained requests and a delayed PDO removal can
delay the re-enumeration.

**Other endpoint types.** No other path is shared. Client transfers have
no deadline in the bus. The bus's own Bulk-Only medium check
(`hcdBotXfer`) keeps its taint and reset in this release (section 4.15.0
item 4), with its reason `BOT_XFER` recorded.

#### 4.15.5a Who owns what after an abort (the ownership audit)

A command answered 25 is not a refusal. Today several callers read a
nonzero result as the controller's refusal and free or roll back what the
command named; with 25 that could free memory the controller still
references. The audit below is complete for the branch at revision 22,
searched again from the code: every call of `hcdCommand`,
`HcdThreadCommand` and `HcdThreadCommandRaw` and of the configuration
wrappers that issue through them (`hcdCfgCommand`, `hcdCfgSetDequeueCode`,
`hcdCfgRecycleCode`, `hcdCfgConfigureAdd`, and `hcdCfgStopCmd` under
`hcdCfgStopBounded`); every `XhciCommandSubmit` outside the engine
(`hcdCommand` and its busy retry, `hcdCommandBusy`, alone); the engine's
own submission (`XhciCommandNoOpSelfTest`); and every helper that
classifies a result (`XhciOwnCommandEvent`, `XhciTolStopRest`,
`XhciStreamResendAfter`, `XhciStreamRecycleFailed`, `XhciTolLateStopped`,
and the Boolean reductions of the callers below). The first column names
the function that issues the command; where a row groups the callers of
one helper, the helper is named and the callers after it, and a
placement or USB-control helper grouped beside an issuer is named as
such. The implementation keeps the table as its checklist and adds
nothing to it silently (a new caller is a change to this table first). A left command (section 4.15.3,
`HCD_CMD_LEFT`) is owned as an aborted one: every row reads `CmdLeft` as
it reads 25.

**The rule.** A command answered 25 owns, until the device's teardown has
completed its Disable Slot, every resource it named or could have
installed (both the old and the new endpoint rings and contexts, stream
arrays and stream rings, the input context's references); the pipes it
touched stay paused; no ordinary refusal rollback runs; the device is
**removed**, never continued as if the command had or had not happened.

| Caller, or the path that classifies the result | Command | Disposition after 25 |
|---|---|---|
| `hcdAddress` (enumeration) | Address Device | section 4.15.4 |
| `hcdEvaluate` and the enumeration's bMaxPacketSize0 fix | Evaluate Context | the step fails, cause `UNANSWERED` (4.15.4's hold on the last failure) |
| `hcdCfgStreamsOpen` | Configure Endpoint | treated as today's 0: the possibly installed stream-context block retired (`StreamsRetired`), kept until the teardown; no reset request (the mark, through `hcdCfgFault`'s step) |
| `hcdCfgRecycleFailed` through `XhciStreamRecycleFailed` (RESET_PIPE's recycle) | Configure Endpoint | today a contract endpoint classifies any nonzero code, 25 included, as an ordinary refusal (`XHCI_STREAM_RF_HOLD`: the debt raised, the pause given back, `hcdCfgFault` bypassed). `XhciStreamRecycleFailed` gains the verdict as an input and answers a new `XHCI_STREAM_RF_REMOVE` for 25 on either kind of endpoint: no debt, no resume, the device failed alone through `hcdCfgFault`'s step, the recycled rings kept. Host vectors in `test_stream` |
| `hcdCfgStreamsClose` | Configure Endpoint (`hcdCfgRecycleCode`, no streams) | the detached stream block (`st`: its context array and every stream ring) is neither freed nor re-attached: it goes on the device's `StreamsRetired` list, owned by the teardown (`HcdCfgDeviceGone` frees it after the Disable Slot's success, or keeps it under the quarantine rule below); the close fails, the endpoint paused |
| `hcdCfgStreamsSequence`, `hcdCfgStreamPlace`, `hcdCfgStreamsPlaced` (stream placement) | Configure Endpoint, Set TR Dequeue | removed; the placed and the previous stream rings retained; no debt raised as for a refusal |
| `hcdCfgErrorExit` (a contract stream endpoint's Error exit, through `hcdCfgRecoverOne`) | Set TR Dequeue (`hcdCfgSetDequeueCode`, one attempt) | removed; the stream's ring and the position sent retained; the stream's fault is not cleared and the endpoint never resumed; the debt is not raised |
| `HcdCfgHubOpen`, the hub's bring-up | Configure Endpoint | a hub keeps the reset (section 4.15.0 item 2), reason `CMD_KEPT` with the hub's slot; nothing freed before the HCRST |
| The ordinary configuration: a select, a configuration, an interface switch, and their rollback; `XhciOwnCommandEvent` | Configure Endpoint | 25 maps to an event of its own, "removed", never to the refusal the machine rolls back; the old and the new rings, stream arrays and contexts all kept; the pipes paused |
| `hcdCfgDeconfigure` | Configure Endpoint (Deconfigure) | removed; the dropped rings retained until the Disable Slot |
| `hcdCfgUnconfigureFunction` (one function of a composite) | Configure Endpoint (drop) | removed - the whole device, every function (section 4.15.5's composite rule); retained |
| `hcdCfgReplayConfig` from RESET_PORT (`hcdCfgResetPort`, after its Reset Device) | Configure Endpoint | the RESET_PORT fails; its existing fallback cycles the device as a removal; no SET_CONFIGURATION or SET_INTERFACE is sent; the replay's fresh rings are retained, not freed by its failure cleanup |
| `hcdCfgReplayConfig` from the resume keep (`HcdCfgResumeReplay`, `hcdCfgReplayResume`, task 45.23) | Configure Endpoint | the kept device is dropped (`hcdRkeepDrop`) as a removal; its new slot's rings retained until that slot's Disable Slot; nothing else replayed |
| `hcdCfgResetPort` | Reset Device | the RESET_PORT fails, the fallback cycles the device; nothing replayed; every ring retained (the device's endpoints may or may not have been disabled by it) |
| `HcdThreadReaddress` (RESET_PORT's hardware half, from `hcdCfgRestore`), its first command | Reset Device | as the row above: the RESET_PORT fails before EP0's ring is rebuilt (the rebuild follows only a Reset Device that succeeded), the fallback cycles the device, every ring retained |
| `HcdThreadReaddress`, its second command | Address Device (BSR 0) | EP0's ring was zeroed and rebuilt, and the shared input context built, before it: the ring is the command's until the device's Disable Slot, the input context until the command is settled (`CmdLeftOwed`'s rule for a left one; for 25 no input context is built until the device's teardown has issued its Disable Slot); the RESET_PORT fails, the fallback cycles the device, and no `hcdCfgReplayConfig` follows |
| `hcdResetEp0` | Stop Endpoint | keeps the reset (table, Stop Endpoint), reason `EP0_RECOVERY` 1 |
| `hcdResetEp0` | Reset Endpoint, Set TR Dequeue | removed: a published device cycled, one with no PDO fails its step (`UNANSWERED`); EP0 stays paused, no doorbell |
| `hcdCfgResetEndpoint`, `hcdCfgRetryResetEndpoint`, `hcdCfgResetHost`, `hcdCfgReset` (RESET_PIPE, the soft retry) | Reset Endpoint, Set TR Dequeue | removed; nothing proceeds as though the endpoint had been reset or repositioned |
| `hcdCfgCancelPipe`, `hcdCfgSetDequeueOne` (the cancel and abort paths) | Set TR Dequeue | removed; the endpoint is not resumed (see the cancel's wording below) |
| `hcdCfgStopBounded` (`hcdCfgStopCmd` under `XhciTolStopRest`), from `hcdCfgQuiesce`, `hcdCfgSetDequeueOne`, `hcdCfgDeqReadBack`'s restop and `hcdCfgStreamsPlaced` | Stop Endpoint | today `XhciTolStopRest` gives up at once on a Stop answered with any code but success or Context State Error (its restops are only for an endpoint that still reads Running, or Stopped after a Context State Error), and `hcdCfgStopBounded` returns the give-up as Running after `hcdCfgStreamMark` has set the device-cycle mark, so `hcdCfgFault` cycles the device alone; 25 would be such a code. An aborted or left Stop is not a give-up: `XhciTolStopRest` gains the verdict as an input and answers it at once, with no restop, as a result of its own, which `hcdCfgStopBounded` turns into the mandatory reset (`CMD_KEPT`, TRB type 15) requested directly, never through `hcdCfgFault`; the stream mark is not set for it, and none of the four callers proceeds as though the endpoint were at rest. Host vectors in `test_tol` |
| `hcdCfgDeqReadBack`'s resend (`hcdCfgSetDequeueCode`, classified by `XhciStreamResendAfter`) | Set TR Dequeue | today a resend answered with any code but success is `XhciStreamResendAfter`'s give-up, except Context State Error on an endpoint read Running (retried after a restop): `hcdCfgDeqGiveUp` returns 0, which `hcdCfgSetDequeueOne` reads as a failed dequeue. For 25 or a left command it answers a new result of its own: the read-back returns at once, with no further resend, restop or placement; the stream's ring and the position sent stay owned; the device is removed (this section's rule), never handled as today's failed dequeue. Its restops are the row above's. Host vectors in `test_sdeq` |
| `hcdCfgResetQuietAs` (RESET_PORT's quiet, from `hcdCfgRestore`) | Stop Endpoint | today a failed Stop goes to `hcdCfgFault`, which an earlier mark on the device (the stream mark, `AbortVerdict`) turns into the device's cycle. An aborted or left Stop here keeps the reset (`CMD_KEPT`, TRB type 15), requested directly, before any drain or ring emptying, whatever marks the device carries ("a mark never suppresses a mandatory reset", below): the pipe is not drained and its ring not emptied |
| `HcdCfgRewind` from selective suspend's `hcdSuspRewind` | Set TR Dequeue | `hcdSuspRewind` returns a third outcome, aborted, distinct from today's refused 0; `hcdSuspStop` carries it through the suspend result machine as its own result, so the endpoint is never restarted (no doorbell, no selective-suspend restart of that device) and the device is removed by the device-alone path, not by today's uncertain-outcome cycle that a later resume could race |
| `hcdEp0Late` (`XhciTolLateStopped`), the thread's own transfer past its deadline | Stop Endpoint | today `XhciTolLateStopped` reads `done` first, so a transfer that completed while its Stop failed reads `DONE`. An aborted or left Stop is classified before `done` and before the state read: the reset (`XHCI_TOL_LATE_RESET`, reason `EP0_LATE`), since a transfer's completion says nothing about whether the Stop ran; the late path's own `LEFT` (a device found leaving) is read only after a Stop that answered. Host vectors in `test_tol` |
| `hcdEp0Withdraw` (the door's transfer) | Stop Endpoint | not withdrawn; the late path keeps the reset (Stop Endpoint row) |
| `hcdEp0Withdraw` | Set TR Dequeue | not withdrawn, and the shared scratch is tainted: the late path keeps the reset (the stricter shared-scratch rule: only proof that the scratch is out of the controller's hands lets it be reused) |
| `hcdEp0Retire` (the late path's scratch retirement) | Set TR Dequeue | keeps the reset with the scratch tainted (`XhciTolLateRetired`, unchanged) |
| `HcdCfgQemuStreamResync` (qemu flavour only) | Set TR Dequeue | keeps the reset, reason `STREAMS` 2 |
| `hcdDeviceStop`, `hcdHubQuiesceBelow` | Stop Endpoint | keeps the reset (`TEARDOWN_STOP`) |
| `hcdSuspStop` (selective suspend's stop) | Stop Endpoint | keeps the reset, as today |
| `hcdDisableSlotId`, `hcdDisableRecord` | Disable Slot | keeps the reset (`DISABLE_SLOT`), the record quarantined (below) |
| `hcdPerform`, `hcdRkeepRestore` | Enable Slot | keeps the reset (`CMD_KEPT`) |
| The self-test and every slotless command | No Op and others | keeps the reset (`CMD_KEPT`) |
| The emulator's own commands (`hcdInjSend`; `HcdThreadCommandRaw`), qemu flavour only, under `InjOwnCmd` | as the fault sends them | exempt from the hang faults' withholding (the layer never withholds a command it sends itself); their results are the injection's, read by it alone and deciding nothing outside it, except `HcdCfgQemuStreamResync`'s, listed above |

**The verdict survives the wrappers.** `hcdCommand` sets a persistent mark
on the device whose slot the aborted command named (`dev->AbortVerdict`,
the TRB type, under the controller lock), beside returning 25, so a
wrapper that reduces the result to success or failure, and `hcdCfgFault`,
which takes no code, read the mark. `hcdCfgFault` gains one step before
its present-device reset: a device whose mark is set fails alone - marked
`FailedAlone`, cycled through `HcdCfgCycle` if published, or left to its
enumeration step if not - and a second call for the same failure finds the
cycle already owed and requests nothing, the issue-24 pattern
(`StreamDeqCycled`). A hub with the mark keeps the reset. A command
answered with an error code keeps every rule it has now.

**The failed device is frozen.** From the moment `FailedAlone` (or
`AbortVerdict`, or `CmdLeft`) is set, under the controller lock, the
device takes no further work of any kind until its teardown: every
`hcdCommand` naming its slot is refused before it is built, except the
teardown's own Stop Endpoints and Disable Slot; the configuration service
answers every request for it as gone (select, interface, configuration,
RESET_PIPE, ABORT_PIPE, the stream requests, RESET_PORT), the door
refuses it, selective suspend skips it, and no doorbell is rung for it.
The mark is kept until `HcdCfgDeviceGone` frees the record. It is charged
once: `XhciTolCycleCharge` at the first mark, never again for the same
device however many callers meet it.

**A mark never suppresses a mandatory reset.** The marks decide only
`hcdCfgFault`'s present-device step and the rows above. Every reset the
table keeps - a Stop Endpoint that failed, a Disable Slot that failed, a
tainted scratch, the engine's stuck abort or divergence, the health
poll - is requested whatever marks are set, on that device or another.

**Before the Disable Slot, every endpoint that may be active.** Disable
Slot may be issued only with "any active endpoints ... in the Stopped
state or Idle in the Running state" (4.6.4, p.97). The teardown therefore
reads, from the output context, the state of every DCI that any of the
device's objects names - its open pipes, the rings retained after an
aborted Configure Endpoint or replay (the old and the new configurations'
endpoints both), the retired stream blocks' endpoints - and stops each
one that reads Running with work on it (a Stop Endpoint; one that fails
keeps the reset, as `hcdDeviceStop` does today). Only then is the Disable
Slot issued.

**If the Disable Slot fails.** The record and everything retained with
it stay quarantined until a reset that proves them out of the
controller's hands: the recovery's HCRST, when that recovery completes,
or the existing proven terminal-release route (section 4.6's terminal
release, its invalidation drained or its proof taken). Where neither
proof can be had - the recovery does not finish and the proof fails -
they stay with the pinned common buffer under the containment for the
lifetime, never freed.

**What the cancel paths have completed, and what they still own.**
`hcdCfgAbortPaused` drains the pipe's requests (`HcdIoDrainPipe`)
**before** it issues the Set TR Dequeue: those requests were taken off
the ring by the stop and are completed already, safely, and nothing
here changes them. What an aborted Set TR Dequeue leaves owned is the
rest: requests queued behind the paused pipe, requests submitted after
the drain (held by the pause), and any TD still on a ring the controller
may still reference. Those complete as gone with the device's removal,
through their existing owners (held on 98 SE and ME, record 13 section
10.5), and the rings they occupy are freed only after the Disable Slot.
**Implementation notes** (task 50.30 step 3, reviewed and revised; notes
of the implementation, not changes of rule):

- *One rule for "every endpoint that may be active".* The ordinary
  teardown (`hcdDeviceStop`) and the settlement's teardown of an owed
  record (`hcdLeftSettleRecord`) take each endpoint by one function,
  `hcdTeardownStops` (`XhciTolTeardownStops`): an open pipe with work on
  its ring or its streams' rings, or an endpoint a retained ring or a
  retired stream block names (`hcdDeviceRetainedDcis`), stopped when it
  reads Running.
- *A confirmed Disable Slot's bookkeeping is one function*
  (`hcdSlotDisabled`), whoever issued it - the ordinary teardown or either
  settlement entry - so the input hold of an aborted re-address
  (`InputHeldSlot`) is released by its slot's Disable Slot however that
  came about (`XhciTolInputHeldAfter`); the giving back of a disabled
  record (its DCBAA entry cleared, the record freed) is one function too
  (`hcdRecordGiveBack`).
- *A command refused under another device's debt.* Section 4.15.3's
  admission refuses every ordinary command while a debt stands, with
  nothing submitted; a command naming another device's slot now marks that
  device (`AdmitRefused`, `XhciTolAdmitMarks`: not an Enable Slot, a No Op
  or a Disable Slot, whose record the teardown quarantines as owed). The
  mark is one of the frozen marks below, so the caller's `hcdCfgFault`
  fails the device alone, the pass does at the debt's clearing if no
  caller did (`XhciTolMarkedFails`), a reset a caller requests for it is
  deferred to the clearing, and a refused Stop Endpoint outside the
  teardown owes the reset itself (section 4.15.3's implementation note). A
  command that builds an input context is refused by the input builders
  before admission, so the command is never submitted; its caller may
  already have paused the device's pipes for it, and those stay paused
  until the device's failure or the clearing reconciles it.
- *"Except the teardown's own" is a scope, not a type.* A frozen device's
  slot is admitted only for a Stop Endpoint or Disable Slot the teardown
  issues (`hcdDeviceStop`, `hcdDisableSlotId`, the controller's
  `CmdTeardown` around them) or the settlement's (`XhciTolFrozenAdmits`);
  any other Stop Endpoint is refused before it is built. The callers that
  could meet a frozen device outside the teardown check first, so that the
  frozen refusal itself requests no reset: EP0's automatic recovery
  (the pass's loop skips a frozen device and `hcdResetEp0` refuses one),
  `hcdCfgStopBounded` (reads not at rest, no command; its caller's
  `hcdCfgFault` meets the mark), `hcdCfgResetQuietAs`, selective
  suspend's stop (aborted: never restarted, removed alone) and a hub
  port's quiesce (its pipes paused, no Stop).
- *No doorbell and no publication for a frozen device*, at every site that
  rings or publishes for a device: besides the ordinary I/O, the stream
  kick, the cancel's survivors, the retry, the door and selective suspend,
  a hub port's release after a resume (a frozen device is skipped, its
  pipes left paused), a hub's status-change transfer (neither published
  nor rung), the bus's own Bulk-Only medium check, and the qemu flavour's
  injection.

#### 4.15.6 The budget, the hold, how it shows, and the spread records (decision 4)

**Charged to a place.** A place is a root port or a hub's port
(`XHCI_TOL_LOC`, section 4.5). What 50.30 charges there:

- a present published device failed alone through 4.15.5: one
  re-enumeration (the cycle), of the place's three;
- an enumeration that ends failed with the cause `UNANSWERED` (4.15.4, or
  a pre-PDO transfer under 4.15.5): no further charge; the place is held
  at once with reason 4, since the enumeration has already spent its
  bounded attempts on the device;
- an aborted command of the second row of the table on a published device:
  as a cycle, through `hcdCfgFault` (section 4.15.5a).

So the first example costs one cycle and two aborted addresses (about
15 s of thread time), then the place is held. A device that addresses but
keeps leaving control transfers unanswered costs three cycles, then the
fourth fault removes it and holds the place (section 4.5's refused
charge).

**What releases a hold.** Section 4.5's: the stable-disconnect evidence
(CCS clear with PP set for the stable-disconnect interval, at least 1 s,
then a connection; a hub's port by its status), or a controller start (a
restart, a disable and enable, task 50.32's restart). An in-place recovery
does not release a root place's hold; a hub's places start afresh with
their hub (4.15.4 item 6).

**How it shows to Windows.** A device that fails alone is removed: its PDO
is reported missing (`IoInvalidateDeviceRelations` on its parent), on 98 SE
and ME as on 2000 and later, and it leaves Device Manager as on an unplug;
on 2000 and XP a storage device's removal may bring the system's own
"unsafe removal" notice, as an unplug does. No problem code is shown and no
PDO is kept. A failed device kept listed with a problem code (the PDO
answering `PNP_DEVICE_FAILED`, Code 43-like on 2000 and later) was
rejected: a listed PDO with no slot would have every request refused,
which is the status record 17 forbids on a listed device, and what 98 SE
and ME make of that state is unread. The place's hold is shown by the USB
Tree (record 25 section 24, reason 4 in section 26), in the snapshot's
tolerance block (`NOW HELD, the device stopped answering`) and in the note
`tol.loc.hold` (place, reason 4).

**A cleanly isolated device failure does not charge the controller's
window.** It requests no recovery. That device's failed abort, failed Stop
Endpoint or failed Disable Slot still does, as every recovery is charged,
since there the controller itself did not answer.

**The spread of failures, recorded only** (section 4.15.0 item 3). A
controller whose command engine is sick but whose aborts end cleanly would
lose its devices one place at a time, each held, with no recovery; each
place costs at most one cycle and two aborted addresses before it is held.
Each device failed alone is recorded (place, the hub it hangs from, the
device's VID:PID where read, `unknown` where not, the tolerance clock) in a
small ring of the controller's, printed in the snapshot's tolerance block
and noted (`tol.spread`, place and count). Nothing acts on it: no
recovery is requested from it, and it does not tell a controller's failure
from a device's - a VID:PID names a model, not a device, and several
places behind one failing hub are not independent evidence. It is evidence
for a reader of the report, and for a later decision.

#### 4.15.7 At the window's latch: what names the cause

Each recovery begun keeps the record of the request that started it
(4.15.8), captured under the controller lock while the slot's record is
still valid; the first triggering reason of a recovery is kept and later
requests are counted without overwriting it. When the window refuses a
fourth, or the failures in a row are spent, the latch's record is the last
request's, and the dump, the recovery-status request (section 4.15.8) and
the Tree name the place and the device **involved** - not a cause: "3 of
the last 4 recovery requests involved the device at port 3" is
supportable, "the device caused the failure" often is not. A request with
no device names none ("not recorded"). Containment (terminals 4 and 5) and
the lost message (terminal 6) are not reset requests and get their own
presentation (record 25 section 26 item 2).

#### 4.15.8 Every reset request gives its reason (decision 5)

`HcdSvcRequestReset(ext)` becomes `HcdSvcRequestResetWhy(ext, reason,
slot, port, extra)` - a slot's device names its own place, read from the
slot's record under the lock - and the engine's
`XhciRequestControllerReset(ext)` becomes
`XhciRequestControllerResetWhy(ext, reason, slot, extra)`; the drain's
request carries the place its staging site copied
(`HcdSvcRequestResetPlaced`, below). Every request, whether or not it is the
one that latches `ControllerFailed`, is counted by reason
(`Tol.Stats.ResetWhy[]`, one counter per reason code); the one that latches it is
also kept in a ring of the last four (`XHCI_RESET_REC`: reason, extra,
slot, root port, route string, VID:PID when read, the tolerance clock,
the recovery count), carried in the extension so the snapshot has it, and
noted:

`ctrl.failed.why` = reason in 31:24, root port in 23:16, slot in 15:8,
extra in 7:0, written beside `ctrl.failed.here` (kept, the call count), at
every logging level the note ring records at; and once more after a
latch, `ctrl.latched.cause` (the same form, the cause of 4.15.7).

| Reason | Code | Sites in the code (all keep their decision; 50.30 changes only those marked; "thread" marks the thread's own sites, which a left command suppresses, section 4.15.3) | extra |
|---|---|---|---|
| `CMD_KEPT` | 1 | thread: `hcdCommand`, a command of the table's reset rows aborted cleanly (new) | TRB type |
| `CMD_NO_VERDICT` | 2 | thread: `hcdCommand`'s give-up (today's `cmd.givenup` site, `hcd_enum.c`; with step 3 the verdict bound) | TRB type |
| `ENGINE_ABORT_STUCK` | 3 | `xhciCommandTimeout` rung 2 or 3 (`cmd.abort.stuck`) | attempt |
| `ENGINE_DIVERGED` | 4 | `xhciCommandRingStopped`'s four divergences (`cmd.diverged`), staged for the drain | its reason 1 to 4 |
| `HEALTH` | 5 | `hcdPoll` on `XhciControllerHealthPoll`'s escalation (HCE, HSE, HCH, command age) | which |
| `EVENT_RING` | 6 | `xhciEventDrain`'s request when nothing staged a reason: the interrupter's rearm failed (`evt.rearm.failed`), or an event the drain itself could not take | 1 rearm, 0 other |
| `INTERRUPTS` | 7 | `XhciEnableInterrupts`, `XhciDisableInterrupts` | 1 enable, 2 disable |
| `POLL_RETIRE` | 8 | `HcdPollThreadPass` (`intr.retire.recover`) | - |
| `DISABLE_SLOT` | 9 | thread: `hcdDisableSlotId` | - |
| `EP0_RECOVERY` | 10 | thread: `hcdResetEp0`'s three (with step 3 a Reset Endpoint or Set TR Dequeue answered 25 fails the device alone instead) | 1 stop, 2 reset, 3 dequeue |
| `EP0_LATE` | 11 | thread: `hcdThreadControlQuiet`'s deadline (step 3 narrows it to a failed stop or retire) | `XHCI_TOL_LATE_*` reason, or 0 |
| `CREATE_LATE` | 12 | thread: `hcdCreateLate` | - |
| `SLOT_ID` | 13 | thread: `hcdPerform`'s and `hcdRkeepRestore`'s slot id no record can take | 1, 2 |
| `PORTSC_DEAD` | 14 | thread: `hcdPortUnreadable`, `hcdHoldService`'s two | 1 to 3 |
| `WARM_RESET` | 15 | thread: `hcdPortChanged`'s warm reset that never ended | - |
| `TEARDOWN_STOP` | 16 | thread: `hcdDeviceStop`, `hcdHubQuiesceBelow` (a Stop Endpoint that failed) | DCI |
| `CFG_FAULT` | 17 | thread: `hcdCfgFault` (a present device's command failed) | - |
| `STREAMS` | 18 | thread: `hcdCfgStreamsOpen`'s Configure Endpoint, `HcdCfgQemuStreamResync` | 1, 2 |
| `BOT_XFER` | 19 | thread: `hcdBotXfer`'s deadline | - |
| `RESUME_FAILED` | 20 | `HcdControllerFail` (a direct assignment today, given a reason) | - |
| `INJECTED` | 21 | the qemu flavour's `HcdInjRecycleConfigure`, `HcdInjSelectConfigure` | fault |
| `HUB_UNANSWERED` | 22 | thread: `hcdThreadControlQuiet`'s deadline and `hcdCreateLate` for a hub (step 3; until then those sites keep 11 and 12) | - |
| `CMD_FATAL` | 23 | `XhciCommandEvent`: a completion code Table 6-90 calls fatal (`cmd.fatal`), staged for the drain; the device named only when the event matched the outstanding command | the code |
| `XFER_FATAL` | 24 | `XhciSlotTransferEvent`: a fatal code on a Transfer Event, staged for the drain; the device named only when the event matched a TD queued on its pipe | the code |

**The engine's reasons reach the drain staged.** The engine's escalations
are not requests at their own sites: `xhciCommandRingStopped`, the
command completion's fatal arm and the transfer event's fatal code return
"reset" to `xhciEventDrain`, which requests once after it drops the lock.
Each stages its reason and extra under the controller lock in the
extension (`XHCI_TOL_WHY`'s `Staged` words, the first staged in that
drain kept, later ones counted only), and the drain's request takes and
clears it; with nothing staged it is `EVENT_RING`. **A device is named
only where the staging site proved it**, and its place is copied when it
stages, under the same lock: a transfer event's fatal code whose event
the engine matched to a TD queued on a device's pipe stages that device's
slot, root port, route and VID:PID (`hcdWhyXferFatal`, through
`XhciTolWhyStageXfer`). Matched is the engine's own fact
(`XHCI_XFER_EVENT_RESULT.Matched`, set by `XhciXferEvent` and
`XhciXferIsoEvent` only once the event's pointer resolves onto a queued
TD), not the code's: both engines set `Fatal` before they resolve the
pointer, so an Event Lost or Undefined Error inside no TD of an open
pipe - EP0's, an ordinary pipe's or an isochronous pipe's - keeps the
controller reset and `XFER_FATAL` and names no device. A command's
fatal code stages the slot of the event only when the event matched the
outstanding command, the rule the slot-fatal arm already keeps
(`HcdSvcWhyStageLocked`). An event no queue or command owns stages no
device, since its Slot ID may name a slot recycled since, and a
divergence or a failed rearm names none. The drain's request records the
copied place (`HcdSvcRequestResetPlaced`) and reads nothing from a slot
after it has let the lock go. The reason list is one header table
(`xhci_tol.h`), with the words for each in the USB Tree's and the
snapshot reader's own tables held equal by `test_xhq`.

The tolerance block grows by the counters, the ring and the retry state;
the snapshot's header words give its size, and `tr_snap.c` reads the new
fields only from a block long enough to hold them, so a reader of an older
driver's snapshot is unaffected. A device failed alone is noted too,
`dev.fail.alone` (root port in 31:24, slot in 23:16, 15:8 1 an Address
Device, 2 a control transfer, 3 another command, 7:0 the TRB type or the
late verdict). The table above is the
implementation's checklist: a reset site without a reason does not build
(the old name is removed, so a missed site is a compile error). The
snapshot prints the counters and the ring (`tr_snap.c`, `print_tol`, after
the terminal line).

**The recovery status, a request of its own.** The query request keeps
version 3 unchanged: its readers (`tr_xhq_parse`, `tr_xhq_driver`) accept
versions 1 to 3 and read a header-sized block first, so a version 4 would
break every installed tool - record 13 section 8.12 gives the same reason
for the holds request's own IOCTL. A new request beside the holds request
(a function code next to `XHH_IOCTL`'s, `METHOD_BUFFERED`, no input, no
access rights, answered at every logging level, of a started controller
only as the holds request is) answers a bounded block: magic, version,
size; the terminal value (so a tool can tell latched from contained or
the lost message); the ring of reset records; the involvement count of
4.15.7; `XhciDeviceIsolation` read, listed, applies (4.15.11); and
section 4.15.13's retry state (the value, the try number, the seconds to
the next try, about). A driver before it answers
`STATUS_INVALID_DEVICE_REQUEST`, and the tool falls back (record 25
section 26 item 1). Host vectors hold its layout; new tool with an old
driver, and old tool with a new driver, are both legs.

**Recovery entries that are not reset requests.** Every direct assignment
of `ControllerFailed` or `RecoveryRequested` in the code, audited:

- `HcdControllerFail` (the failed resume, `hcd_ctl.c`): sets both
  directly; given the reason `RESUME_FAILED`, recorded as a request.
- `hcdRecover`'s lost-message relatch (a recovery that succeeded while
  mode 4's loss was latched) and `HcdMsg4LostLatch`: terminal 6, the lost
  message, not a request; its own note (`ctrl.recover.msg4`,
  `msg4.lost`) stays its record.
- `hcdRecover`'s re-request after a failed attempt (`RecoveryRequested`
  1 while failures in a row remain): the same recovery continuing, not a
  new request; the record of the request that began it is kept.
- `XhciRecoverCharge` and `xhciRecoverOnce` (`xhci_init.c`): a recovery's
  own failure, which keeps the controller failed; not a request
  (`ctrl.recover.refused` stays its note).
- `hcdContain` and the enumeration service's pinned-buffer latch
  (`HcdEnumService`, `slots.unproven.pinned`): terminals 4 and 5, the
  containment, not requests; record 25 section 26 item 2 presents them.
- `HcdSvcRequestReset` itself: the one request path, which every site in
  the table above reaches with its reason.

So not every entry into the failed state gains a new reason: of the
direct assignments only the failed resume's is given one; the others
keep the record they already had - the request that began the recovery,
the containment's terminal, the lost message's note - and none of them
is counted as a request.

No decision changes at any of them: step 1 adds the recording only, and
the reset admission (`HcdSvcRequestReset`'s test of `ControllerFailed`
and the latch's checks in `hcdRecover`) is untouched; a host vector
holds that each site's admission outcome is the same as before.

#### 4.15.9 The emulator (decision 6)

**Why it must be emulated.** QEMU's xHCI model cannot show a clean abort:
this driver writes CRCR's low dword alone for the abort
(`XhciWriteCrcrAbort`), and QEMU acts on CA only at a write of the high
dword (`hcd-xhci.c`, `xhci_oper_write`, the CRCR high case); and QEMU sets
CRR when it processes commands and clears it only at that abort
(`xhci_process_commands`), so after the first command CRR reads set for
good. On QEMU a real abort therefore always ends at rung 2 with
`cmd.abort.stuck` and the reset (static, the mirror `external/qemu`). And
no QEMU device withholds an answer to SET_ADDRESS or to a control request.

**The engine's correction it shows.** Today `xhciCommandAborted` ends the
command as outstanding (`CommandTrbPA` cleared), so a Command Ring Stopped
that then names the same TRB finds `abandoned` 0 and adopts the position
without the No Op rewrite: the aborted command stays executable at the
next doorbell, ahead of the Disable Slot that follows. A real controller
that had fetched the command advances past it (4.6.1.2, p.93), but the
engine must not depend on that: `xhciCommandAborted` keeps the TRB it
ended (`CommandAbortedPA`), and `xhciCommandRingStopped` rewrites a TRB at
the reported position that is either the outstanding one or that one,
before it adopts the position. This is a change at every value of
`XhciDeviceIsolation`, since the engine's abort runs at every value; it is
built as a commit of its own, separately reviewed, with host vectors in
the engine's suite and the regression legs below.

**It is defensive handling of an anomalous sequence, not a normal
outcome.** The specification has a controller that fetched and was
executing the aborted command advance past it (4.6.1.1, 4.6.1.2), so the
ordinary aborted case is a Command Aborted naming the command and a
Command Ring Stopped naming the next TRB, which the engine already
adopts; it also permits an abort that reports no Command Aborted at all.
A Command Ring Stopped naming the very TRB a Command Aborted has just
ended is not a sequence the specification describes, and no controller
has been read producing it. It is handled because, once the ring is
stopped, its TRBs are software's to edit until the next doorbell, and a
valid command left there would run ahead of the next one. It is not free
of consequence: the TRB newly treated as abandoned meets the
second-abandonment guard when a No Op rewritten for an earlier
abandonment is still unanswered (divergence 2), and a rewrite the ring
refuses (divergence 3), so a case that today adopts the position without
a reset can now end in the controller reset. No claim is made that it can
only improve recovery. It is therefore kept out of the merge of steps 1
and 2 and admitted on its own emulator legs: outcome 3 through the real
engine, the next command's execution after the restart, outcomes 1, 2
and 4, a wrap, the outstanding-No-Op divergence, and an invalidation and
a rebuild between the abort and the stop. Before step 3 a guest reaches
only the first and the outcomes' ends below, since every command the
forcing values abandon is given up by the thread and the reset rebuilds
the ring before another doorbell ("What step 2 alone shows of the
outcomes"); the rest are driven through the engine itself on the host,
over a modelled controller (`test_cmd`, 2026-11-18), until step 3's legs.

- **The saved address's lifetime.** `CommandAbortedPA` is set only by a
  Command Aborted event that names the outstanding command of the current
  abort (state `ABORTING`), with the command generation beside it
  (`CommandAbortedGen`); it is consulted only by the Command Ring Stopped
  that ends that same abort, in that generation; and it is cleared there
  once used, by every invalidation (`xhciCommandInvalidateLocked`), and by
  every rebuild of the command ring (the start, a recovery's
  reinitialization, the restore). It is never left to match a later
  command placed at the same physical address.
- **What it does not change.** An ordinary completion that arrives first
  keeps its precedence: it is matched and ends the command, a Command
  Aborted after it matches nothing and keeps nothing, and the stop does
  not take that TRB for the rewrite - the specified race between a
  completion and CA, resolved as today. That is not a tolerance of
  duplicate events. A completion arriving after the Command Aborted that
  ended the same command is counted unmatched and does not reverse the
  loss; one arriving at that address after the rewrite is taken as the
  rewritten No Op's, since the engine matches a completion by its address
  alone. The four divergence checks stay
  where they are and are taken before the rewrite (a stop with no abort
  outstanding, a second abandonment while a rewrite is unanswered, a TRB
  that cannot be rewritten, a position the ring cannot hold); and a
  rewritten No Op is retired by `CommandNoOpRewrittenPA` as now.

**The forcing values.** New codes of `XhciQemuInject` (section 5's
trigger: 31:24 a sequence, 23:16 the fault, 15:8 the root port - 0 the
first that fits, bit 15 set for a device behind a hub at that root port -
7:0 the argument), after the last taken (35, `23` hex):

| Code | Name | What it does |
|---|---|---|
| `24` hex | `XHCI_INJ_ADDRESS_HANG` | the next Address Device to a device at the place is placed on the command ring and its doorbell withheld (QEMU never fetches it), so the engine's watchdog fires for real at 5 s; at rung 1, after the real CA write (which QEMU ignores), the layer acts out the outcome the argument chooses |
| `25` hex | `XHCI_INJ_EP0_HANG` | the thread's next control transfer to the device at the place has its doorbell withheld (`HcdInjEp0Doorbell`), so the deadline passes and `hcdEp0Late` sends a real Stop Endpoint and a real Set TR Dequeue; the target may be a hub itself (the hub's own transfer: the reset path) |
| `26` hex | `XHCI_INJ_DEVICE_HANG` | the device stops answering until its place's disconnect is seen: every Address Device and every thread control transfer at the place withheld as `24` and `25` do, until the place's disconnect is observed (CCS 0 read at that root port) or `FF`. **A root port only**: its end is the root port's disconnect, and the layer has no hook on a hub's downstream port, so it could neither latch the hub port's route nor see that port's disconnect - armed behind a hub it would hang the hub's other devices and outlive the device. `26` with the port byte's bit 7 is therefore refused (`qemu.inj.hang.refused`) and nothing is armed; a hang behind a hub is made with `24` or `25` and the port byte's bit 7, which apply to any device behind a hub at that root port and end by their count. **Armed by count or by the device's own behaviour** (argument bit 7 below): armed by behaviour, it does nothing until a thread control transfer to a device at the place reaches its deadline of its own accord (the device itself left it unanswered: `hcdThreadControlQuiet`'s deadline, `hcdEp0Late` entered with no withheld doorbell of the layer's); from that moment the place is hung as above. The armed state belongs to the controller's injection state, not to the device record, so it survives the device's teardown and the controller's recoveries, and only the disconnect, `FF` or a stop and start of the controller ends it (every start begins with nothing armed, `HcdInjStart`); on a latched controller the thread inspects no port, so a disconnect there ends nothing until that next start (read on 2000, 2026-11-17) |
| `27` to `2B` hex | `XHCI_INJ_CFGEP_HANG`, `_RESETEP_HANG`, `_SETDEQ_HANG`, `_EVAL_HANG`, `_RESETDEV_HANG` | the next Configure Endpoint, Reset Endpoint, Set TR Dequeue, Evaluate Context or Reset Device for the device at the place withheld as `24` withholds Address Device, so each caller of section 4.15.5a meets 25 |

The argument: bits 3:0 the count (1 to 14; 15 persistent, as above);
bits 6:4 the outcome acted out for a withheld command; bit 7 for `26`
"armed by behaviour" (`26`'s count is never spent: it hangs one place,
latched at its first withholding or its arming, until that root port's
disconnect or `FF`), and for `24` and `25` "withhold once more the
recovery command that follows" (below; step 3's, reserved until then):

| Outcome | What the layer does | What it exercises |
|---|---|---|
| 0 | a Command Ring Stopped naming the withheld TRB, no Command Aborted | stopped at the unfetched command: the engine's rewrite to a No Op Command before the next doorbell |
| 1 | the "already fetched" case: the layer turns the withheld TRB into a No Op Command of its own, then delivers a Command Aborted naming it and a Command Ring Stopped naming the next TRB | aborted, then stopped at the next TRB: the real controller's ordinary case, the position adopted |
| 2 | as 1 without the Command Aborted | stopped without aborted (4.6.1.2's note): a clean stop |
| 3 | a Command Aborted naming the withheld TRB and a Command Ring Stopped naming the same TRB | the engine's correction above: the rewrite still happens |
| 4 | the doorbell rung at the CA write, so QEMU completes the command; then a Command Ring Stopped at the next TRB | an ordinary completion racing CA: its own code, the engine idle only after the stop |
| 5 | nothing | the stop event missing: rung 2 reads QEMU's CRR set, `cmd.abort.stuck`, the reset |
| 6 | a Command Ring Stopped naming a position the ring cannot hold | divergence 4, the reset |

**What step 2 alone shows of the outcomes.** Until step 3, the thread's
command wait (`hcdCommand`) ends at the first of three things: the
command's own completion, which it returns; the command's loss
(`XhciSlotCommandLost`, which the engine calls for a Command Aborted
naming the command, for a Command Ring Stopped that ends it while it is
still outstanding, and at an invalidation); or its deadline, 5 s, the
same nominal interval as the engine's watchdog. Each of the last two is
today's `cmd.givenup` (section 4.15.3) and the reset with reason
`CMD_NO_VERDICT`; the request marks the controller failed before the
thread goes on, the submit refuses a failed controller, the recovery runs
on the same thread and rebuilds the command ring before it admits a
command again, and its invalidation drops the layer's events and marker
(`HcdInjCmdRingReset`). What each outcome can end in with step 2's build
(corrected 2026-11-18; this paragraph said every outcome ends in
`cmd.givenup` and one recovery):

- Outcomes 0, 1, 2, 3 and 6 abandon the command, so each ends in
  `cmd.givenup` and one recovery (outcome 6 asks for the reset itself as
  well, divergence 4, and the second request finds the controller already
  failed). Outcome 1's and 2's No Op is never retired
  (`qemu.inj.noop.retired` absent), and the No Op the engine writes over
  the withheld TRB at outcomes 0 and 3 is never rung: no doorbell reaches
  that ring before the recovery rebuilds it.
- Outcome 5 delivers no event: the wait's deadline gives up, and the
  recovery's invalidation comes before rung 2, which therefore never
  reaches `cmd.abort.stuck` (read on 2000 with two processors,
  2026-11-17).
- Outcome 4 rings the withheld command at the CA write, so QEMU completes
  it, and the Command Ring Stopped follows at the next TRB. When that
  completion reaches the engine and the waiter before the wait's final
  deadline check (which looks at the event once more after the deadline),
  the command passes with its own completion code: no `cmd.givenup`, no
  recovery, the position adopted at the stop (read once on Windows 7 x86,
  2026-11-18). The watchdog's 5 s and the wait's 5 s are equal nominal
  deadlines started at different moments of one submit, so neither is
  bound to come first; when the deadline is found passed with nothing
  signalled, outcome 4 too ends in `cmd.givenup` and one recovery.

The outcomes become distinguishable once step 3 makes the wait outlast
the abort; their legs are step 3's.

**The "already fetched" case without impersonating the engine.** A
controller that fetched the command before CA moves its own dequeue past
it, and the engine adopts the next TRB. QEMU never fetched it, so its
dequeue is still on the withheld TRB, which would execute at the next
doorbell. The layer must make it harmless, but it may not register it as
the engine's rewritten No Op (`CommandNoOpRewrittenPA`): the engine's
second-abandonment guard rejects any abandonment while that marker is
set (divergence 2), and outcome 2's stop - whose outstanding command is
still live - would always meet it, as would outcome 1's where the aborted
address became the abandoned one. The guard stays exactly as it is.
Instead the layer keeps its own marker (`InjHang.NoOpPA`, in the
injection state), rewrites the withheld TRB in place as a No Op Command
with its cycle bit kept, and swallows the one completion QEMU later gives
for that address through the injection layer's event filter
(`HcdInjEventSwallow`, qemu flavour only), so the engine never sees a
completion it did not cause; the marker is cleared with that completion,
at every invalidation (`xhciCommandInvalidateLocked`), at every rebuild of
the command ring (`xhciProgramCommandRing` at start and recovery,
`xhciRestoreState`) and at the HCRST, by one hook
(`HcdInjCmdRingReset`), which drops with it the withheld TRB's address,
the Stopped owed after outcome 4 and the outcome's events not yet
delivered, so none reaches a new ring generation; a `26` armed by
behaviour stays armed. The engine's own marker is left to the engine.

**The withholding is bound to the command placed.** The hook before the
submit only marks the command (its TRB type and slot) when it fits the
fault; the count is spent, and the doorbell withheld, at the command
doorbell of that very command once the engine has made it outstanding
(the engine's outstanding type and slot are the mark's), and the mark
ends with the submit's answer, whatever it was. A submit refused or busy
spends nothing and leaves nothing a later command's doorbell could take.

Bit 7 for `24` and `25` withholds once more the recovery command that
follows, with the same outcome: for `24` the Disable Slot after the
aborted Address Device; for `25` the late path's Stop Endpoint (which
keeps the reset). It is step 3's: step 2 reserves the bit and does not
act on it; section 4.15.16 C (revisions 25 and 26) is its design. Two
more cases are made by the legs rather than an outcome: a stale
generation (a disable of the controller while a withheld command is
outstanding: since `HcdInjCmdRingReset` the invalidation drops the
layer's undelivered events, so none is delivered stale) and ring wrap
(the withheld command placed at the ring's last usable slot). Step 2
builds no padding to reach the wrap: a leg reaches it only by the
commands it issues before the fault fires. The pure decisions (which
command at which place is withheld, the behaviour arming, the outcome's
event sequence, the disconnect's disarming) are in `xhci_inj.c` with host
vectors in `test_inj`; the hooks (the command doorbell in the submit,
rung 1 of the watchdog, the thread's EP0 doorbell, the late path's entry
for the behaviour arming, the port change for the disarming) are under
`XHCI_FLAVOUR_QEMU` and compiled out of `release`, which the release
image's import and string checks show as for the other qemu-only code.
Notes `qemu.inj.hang.armed` (code, place, by behaviour, argument),
`qemu.inj.hang.refused` (a `26` behind a hub, the trigger's value),
`qemu.inj.hang` (code, place, slot, outcome; written where the doorbell is
withheld), `qemu.inj.outcome` (outcome, events), `qemu.inj.noop.retired`,
`qemu.inj.armed` (place, the slot whose transfer armed it) and
`qemu.inj.disarm` (place). The EP0 withholding of `25` and `26` is the
thread's own control transfer's (`HcdInjEp0Doorbell` from
`hcdThreadControlQuiet`): the configuration service's SET_INTERFACE and
SET_CONFIGURATION and the enumeration's requests, never a class driver's
control transfer through a URB, which no fault of this table withholds.

**The list, forced.** Step 3's (not in steps 1 and 2). This paragraph
first described a qemu-only registry value; the implementation is a table
of the flavour instead, and this text follows it: `XhciQemuIsolationListed`
is a pure function the qemu flavour alone calls, which lists the emulated
controller (`1B36:000D`, packed `0x000D1B36` as the other per-controller
lists pack device and vendor), so `XhciDeviceIsolation` at its default `1`
applies in a qemu-flavour guest with no registry value at all; there is no
value that removes the emulator from that list, so removing a listing value
does not make the qemu build unlisted. The release flavour never calls it,
and its own table lists one controller, `8086:1E31` (section 4.15.11,
since revision 38), so there the default applies on that controller alone
and never on the emulator's own id. Evidence that the handling does
**not** apply is therefore taken with `XhciDeviceIsolation` `0` (either
flavour), or with the release flavour at its default (absent or `1`) on a
controller the table does not list (QEMU's own id among them), never by an
unlisting value. An emulated controller given another PCI identity is
matched against the release table in either flavour, and the qemu
flavour's own table does not list it: revision 38's legs gave usb4-emu's
`usb4-dock-xhci` (a `qemu-xhci` with a settable identity) the ids
`8086:1E31` and `8086:1E30` (`runs/run-50.md`, "50.30 (device isolation): 8086:1E31 listed by the owner's decision"). The
start notes what it decided (`tol.isolate`).

**The field report, emulated end to end.** The first example of section
4.15.1 is reproduced in a guest with both drivers it involves: this
driver, and the USB Audio 2.0 function driver of the sibling project
(`usb-wdm-free`), read-only from its own checkout, in a throwaway guest.
The device is the sibling project's USB Audio 2.0 device simulator over
QEMU's `usb-redir`, in its variant that stops answering every control
request (while staying connected) after a set number of alternate-setting
changes within a set time; a stream churn program of the same project
opens and closes streams rapidly to reach it. In QEMU an unanswered
control request leaves the thread's transfer without a completion, which
reaches this driver's 5 s path (`hcdEp0Late`) exactly as the field did.
QEMU, however, answers Address Device itself without asking the device,
so the field's second half (an Address Device timing out after each
recovery) needs code `26` armed by behaviour at the simulator's root
port: the simulator supplies the first unanswered SET_INTERFACE, that
arms the place, and from then on every Address Device and control
transfer there hangs until the simulated device is unplugged. The signature
expected, as in the field: one unanswered SET_INTERFACE, then three
recoveries each followed by an Address Device time-out, then the window's
refusal and the latch, with a second device (a stick under a checked copy,
or the keyboard) on the controller throughout.

What this does and does not reproduce, recorded with the leg: it is not
the real device's firmware fault (the simulator models only "stops
answering control requests"); the controller's own abort behaviour is
injected (outcome chosen by the argument), never QEMU's; the timing is the
emulator's (TCG), not the field machine's; and QEMU's handling of
SET_ADDRESS is bypassed by the injection, not exercised. What it shows is
this driver's decisions, the notes and the Tree's display for the same
sequence of events the field report recorded.

**The baseline, then each later step in the same leg.** Leg 13 below is
run first at today's handling (step 2's build: isolation not built), to
show the trace reproduces the field's sequence and the latch, and that
unplugging the simulated device alone does not bring the controller back
today. The same leg is then the acceptance of steps 3 and 4: with
isolation applying (`XhciDeviceIsolation` 1 and `XhciQemuIsolationListed`
1) the simulated device fails alone, its place is held and the second
device never stops; with the tries (`XhciFailedRetrySec` at its default)
and isolation at `0`, the latch comes as today and the unplug of the
simulated device brings a try at once that recovers the controller.

What the emulation does not show: a controller aborting a USB transaction
it is executing (a SET_ADDRESS the device NAKs); section 4.15.10.

**The legs** (qemu flavour, `XhciDeviceIsolation` 1 with
`XhciQemuIsolationListed` 1 unless said; 98 SE, ME, 2000, XP and 7; 8.1
and 10 where the guests are free):

1. **A device stops answering mid-use, others carrying traffic.** QEMU's
   `usb-audio` at a root port playing, a `usb-storage` stick under a file
   copy and a tablet moving, the copy checked by its checksum. Code `26`,
   outcome 1, persistent, at the audio device's port, then a stream stop
   or start in the player. Read: `cmd.aborted` twice, `dev.fail.alone`,
   `tol.loc.hold` reason 4, no `ctrl.failed.here`; the copy and the tablet
   unaffected; the audio device's select answered gone once; the device
   leaves Device Manager; the Tree's port note. Then `device_del` and,
   after a few seconds, `device_add`: the hold released, the device
   enumerates and plays after the player reopens it.
2. **Behind a hub.** The same behind a QEMU `usb-hub` with a mouse behind
   it, made with `25` (bit 7 of the port byte, persistent count) since
   `26` takes a root port only: the hub and the mouse stay, the hub's port
   held and disabled, its unplug seen and the hold released, then `FF`.
3. **The device moved** to another root port, and to the other controller
   of this driver on a guest with two: it enumerates there at once.
4. **Address Device hangs at plug-in** (code `24`, each outcome 0 to 3):
   the port reset before the retry, two aborts, the place held; `FF` and
   a replug restore it.
5. **A SuperSpeed device**: a SuperSpeed stick with code `24` at a USB 3
   root port: held, the port left enabled, link changes acknowledged and
   nothing reset there, the replug releases it.
6. **Each state-changing command**: codes `27` to `2B` against a select
   (Configure Endpoint), a RESET_PIPE (Reset Endpoint), an ABORT_PIPE's
   cancel (Set TR Dequeue), the enumeration (Evaluate Context, with a
   full-speed device whose bMaxPacketSize0 differs) and a RESET_PORT
   (Reset Device, then its replay); and, by the same codes, the callers
   section 4.15.5a's table adds: a stream recycle (`27` on RESET_PIPE of a
   UAS stream endpoint: removed, not held by a debt), a streams close
   (`27`: the detached block retired), a selective-suspend rewind (`29`
   during a suspend of an interrupt endpoint with a read pending: no
   restart, removed), the resume keep's replay (`27` across a standby
   with a kept device) and a function unconfigure (`27`): each time the
   device removed, nothing freed early (the qemu flavour's checks of
   retired memory), no rollback, no reset.
7. **The abort fails** (outcomes 5 and 6), **the stop hangs** (`25` bit
   7) and **Disable Slot hangs** (`24` bit 7): the reset, each with its
   reason, the root place already held where the Disable Slot hung.
8. **A hub's own transfer**: code `25` at the hub: today's reset, reason
   22.
9. **Composite**: a QEMU composite device (audio with HID): every function
   removed, a second select before the teardown answered gone, a cancel
   racing the cycle.
10. **Lifecycle during a forced hang** (code `24` persistent, outcome 5,
    so the command never ends): a Device Manager disable, a removal of the
    controller's devnode with and without a surprise removal first, a
    surprise removal where the emulator can hot-unplug the controller, a
    query-stop under the message-interrupt mode (2000 and XP, refused and
    then granted), a removal of the root hub's devnode alone, a standby or
    hibernation where the guest resumes (the device with the left command
    removed before its pipes resume), and a shutdown: each proceeds within
    a slice of the signal, with no clean abort noted and no command-owned
    memory freed early; on 98 SE and ME as on 2000 and XP.
11. **The values.** `XhciDeviceIsolation` `0` on the qemu flavour, and
    the release flavour at its default (absent, and `1`: the emulator
    unlisted there): leg 1 meets today's handling, a recovery per
    time-out, the window, the latch, the reasons recorded; `2` on the
    release flavour: leg 1's outcome with no controller listed.
12. **No regression**: the engine's outcome 3 at every value; the 43.49
    plug and unplug series on 98 SE; the 35-V injection rows `08` to `12`;
    the device matrix on 98 SE, 2000, XP and 7 with the release flavour.
13. **The field report, end to end** (the paragraph above): Windows 7
    (XP where no guest with both drivers is at hand), the sibling
    project's audio driver bound to the simulated USB Audio 2.0 device at
    a root port, the simulator set to stop answering after a burst of
    alternate-setting changes, its churn program opening and closing
    streams, a stick copying (or the keyboard) on another port, code `26`
    armed by behaviour at the simulator's port with outcome 5 (no stop
    event, as the field's `cmd.givenup` then `cmd.abandoned` shows).
    Read: the unanswered SET_INTERFACE (`ctrl.failed.why` reason 11),
    three recoveries each followed by an Address Device time-out
    (reason 2, TRB type 11, the simulator's slot), `ctrl.window.refused`,
    the latch with `ctrl.latched.cause`, the second device lost with the
    rest, the Tree's stopped line naming the port involved; then
    `device_del` of the simulator: the hang disarmed
    (`qemu.inj.disarm`), and at today's handling the controller stays
    stopped. Steps 3 and 4 repeat it and read what changes.
    (Revision 28, section 4.15.16 G: with step 3 in, outcome 5 still
    latches by design - the stuck abort keeps the mandatory reset - so
    step 3's acceptance reads the leg with outcome 1, and the outcome-5
    reading is kept as the field's sequence.)

#### 4.15.10 What only a real controller shows, and the list (decision 7)

A reading on real hardware is the entry criterion of section 4.15.11's
table. For a controller family it shows, with a device that NAKs while
still electrically connected (a pulled cable tests something else), on a
build that records the reasons and the abort notes:

1. that the controller acts on this driver's abort write, which sets CA in
   CRCR's low dword alone (`XhciWriteCrcrAbort`; Linux writes both halves
   for controllers that need it);
2. the events and their order: Command Aborted then Command Ring Stopped
   at the next TRB, or the stop event alone, and where the stop points;
3. CRR negating, and when, against the engine's intervals;
4. the slot's output state after the abort (the Slot Context's state as
   the controller wrote it: the note `cmd.abort.slot`, slot in 15:8 and
   Slot State in 7:0, written after each clean abort of a command that
   names a slot, in every build);
5. a Disable Slot that then succeeds, and the retry after the port reset
   behaving as the design says;
6. behind a High Speed hub, a Full or Low Speed device: the TT clear and
   what follows;
7. traffic on the controller's other ports continuing throughout (a copy
   checked by its checksum, an input device), and the Stop Endpoint of a
   NAKed control transfer completing.

The criterion is **six mandatory observations plus conditional
transaction-translator coverage**, not an unconditional seven-item pass:
items 1 to 5 and 7, for Address Device and for a control transfer, are
the minimum for an entry; item 6 is read where the family has a High
Speed hub and a Full or Low Speed device to test behind it. An entry
listed without item 6 says so beside it in the table and in its run
section ("TT path not read"), and the snapshot's isolation line and the
listing's note carry that limitation, so a report from a device behind a
TT on that controller is read knowing it.
The readings go to `runs/run-50.md`, one section per family, with the
controller's PCI ids, the build, the device used and each item's result.
An entry is added by a change to the table with that run section cited
beside it, by the owner's decision on the reading, as the other
per-controller lists are kept; a reading that fails an item adds nothing
and is recorded as such. The owner's first reading is planned on his
Intel 7-series board (the field case); a run sheet for it is handed to him
with this design (not tracked).

**The owner's amendment (2026-11-20).** The owner listed the Intel 7
Series xHCI, `8086:1E31` (the Panther Point PCH's, the field report's
controller of section 4.15.1), in section 4.15.11's table for this
release, knowing that the criterion above asks for a reading of the
controller's abort on real hardware first and that only the emulator's
model of the abort (section 4.15.9) had been read. None of items 1 to 7
has been read on that controller, item 6 included: the entry is not a
reading. It cites this decision and the emulator legs that showed the
table at work (`runs/run-50.md`, "50.30 (device isolation): 8086:1E31 listed by the owner's decision"), not a bench section.
The reading of this section stays owed for it; should it fail an item,
whether the entry stays is the owner's decision. The criterion is
unchanged for every other controller. No "TT path not read" note is
carried in the snapshot or the listing for this entry: no item was read,
and a note there would be a new user-visible text, the owner's to word.

#### 4.15.11 `XhciDeviceIsolation`: the value and the table

**The value** (section 4.15.0 item 1). `XhciDeviceIsolation`, a
`REG_DWORD` in the controller's driver key and machine-wide in
`Services\USB`, read at each start and latched for the lifetime, in the
form of the per-controller values (`XhciStreamDequeueAvoid`,
`XhciIntervalCap`): `0` today's handling on every controller; `1`, the
default, the new handling on a controller the table lists; `2` the new
handling on every controller, at the user's own risk. Absent, another
type or another number reads as `1` (confirmed, owner, 2026-11-18). It
is independent of `XhciTolerance`,
which keeps its meaning: where the new handling applies it charges and
holds places at every `XhciTolerance` value, as the slot-fatal teardown
does (section 4.3's revision-10 note).

**What is active where it does not apply** (`0`, or `1` on an unlisted
controller - every controller but `8086:1E31`, the one entry since
revision 38):
nothing of the new command wait (no phase 2, no leave signal: today's 5 s
wait and give-up), no abort verdict (25 is never returned; a lost command
is today's reset), no device-alone failure (`XhciTolLateVerdict`'s
`isolate` input clear, `hcdCfgFault` unchanged), no hold reason 4 and no
spread records. What is active at every value: the reasons, the ring, the
counters and the notes of section 4.15.8; the recovery-status request;
the engine's rewrite correction of section 4.15.9; task 50.31's tries
(their own value).

**The table.** `XhciIsolationListed(vendorDevice)`, a pure function in
`xhci_tol.c` in the manner of the other per-controller lists (the
quirks of record 20, `XhciStreamDequeueAvoid`'s list): PCI vendor and
device id pairs, each entry citing its run section (section 4.15.10).
It starts with what has been read by the cut; **if no reading exists at
the cut the table is empty and the default `1` behaves as `0`.** It holds
one entry, `8086:1E31` (`0x1E318086`, every revision), listed by the
owner's decision of 2026-11-20 without a reading (section 4.15.10's
amendment); on every other controller the default `1` behaves as `0`. The
qemu flavour's `XhciQemuIsolationListed` lists the emulated controller
(section 4.15.9).

**What a report shows.** The snapshot's tolerance block prints the value
read, whether the controller is listed and whether the handling applies,
as the per-controller values' lines do (`isolation value 1, listed no,
applies no`), and the start notes it (`tol.isolate`: value in 15:8,
listed in bit 1, applies in bit 0).

**Rows and gates.** Both INFs write `HKR,,XhciDeviceIsolation,
0x00010003,1` on every controller install path without overwriting (34.1's
rule); the INF gate's table gains its row (since the release, the type,
the default, why, and why that default) and its self-test rows; the
installer's `instValues[]` and the Tree's `trsRows[]` gain it on the
controller-faults tab (record 25 section 22; `test_trset` holds them in
step); `tuning.md` gains its row and a paragraph, draft for the owner:
"`XhciDeviceIsolation`: what the driver does when a device stops
answering while plugged in. `1` (default): on controllers whose behaviour
has been checked, the device is removed and its port put on hold, and the
other devices on the controller keep working; on other controllers, as
`0`. `0`: the whole controller is reset, which removes every device on it
for a moment, as earlier releases did. `2`: as `1` on every controller;
use it only if your controller is not yet listed and you accept that its
behaviour has not been checked. Read when the controller starts." Row:
minimum `0`, maximum `2`, default when absent `1`, written by the install
`1`. Since revision 38 the one listed controller has not been checked on
real hardware (section 4.15.10), so "whose behaviour has been checked",
the owner's wording in Setup, the USB Tree and `tuning.md`, no longer
describes the list. Since revision 39 (owner, 2026-11-20) Setup's and the
USB Tree's row names the controller: the meaning ends "on the listed
controller" and choice 1 reads "on the Intel 7 Series, 8086:1E31";
`tuning.md`'s text takes the same wording with the release's user
documents (task 50.final), from the owner's drafts.

#### 4.15.12 The tasks already in this phase (decision 8)

- **50.26 (the status of a cancelled transfer).** 50.30 completes no
  request as cancelled itself; requests that are cancelled while a device
  fails alone, and those held on 98 SE and ME and released as cancelled,
  go through 50.26's encoder as every cancelled completion does. The
  failed select's `DEVICE_GONE` is not a cancel and is not encoded. No
  change to 50.26.
- **50.27 (the end call and the thread joins).** No thread, DPC or timer
  is added by 50.30 or 50.31: the wait is the controller thread's and the
  tries run on its 100 ms pass, on the tolerance clock. A stop's join of
  the controller thread is not lengthened by the longer command wait,
  because the stop raises the leave signal before it joins (section
  4.15.3) and the wait ends at its next slice; leg 10 of section 4.15.9
  reads it on 98 SE, ME, 2000 and XP.
- **50.28 (a departed device's pending transfers, branch `w50-gone`, not
  yet merged).** Read on that branch: it changes the hold points of a
  departure's drain (`HcdIoDeferred`, `hcdWaitingRefuse`, `HcdIoPark`), the
  report's sweep in `HcdDevicePdoGone` (`hcdGoneSweep`, `GoneNow`) and adds
  `XhciUnplugComplete`; it does not touch `hcdEp0Late`, `hcdEp0Retire`,
  `XhciTolLateVerdict`, `hcdCfgFault`, the holds or the recovery. Where
  the two meet: a device 50.30 fails alone leaves through `hcdSubtreeGo`
  and `HcdDevicePdoGone`, so on 2000 and later with `XhciUnplugComplete` 1
  its clients' requests complete `DEVICE_GONE` at once (a storage driver
  then does not wait out its own time-out), on 98 SE and ME and with the
  controller contained they stay held - 50.28's rules, unchanged.
  Merging: both add fields to `hcd.h`, rows to the two INFs, the INF gate,
  `tuning.md`, the installer's `instValues[]` and the Tree's `trsRows[]`
  (50.31's value beside 50.28's), and qemu-only code beside
  `XhciQemuStageFault`; textual conflicts, resolved at the merge, 50.28
  first. Record 17 section 4.6's rule on a listed device is kept by both.

#### 4.15.13 A controller stopped after repeated failures is tried again (task 50.31)

*Superseded in its detail on 2026-11-18 by design record 38*
(`38-failed-controller-retry.md`), the design of task 50.31: it
takes this section as its start, settles each item the task's line leaves
to its design, and lists in its section 21 every place it differs from
the items below; where the two differ, record 38 rules. The owner's
decisions of section 4.15.0 are unchanged by it, and the text below is
left as it was.

Today a latched controller (terminal 2, three failed recoveries in a row,
or 3, the window refused a fourth) stays stopped until a stop and start.
Owner, 2026-11-17: a registry value in seconds, a few seconds by default,
backing off, and an immediate try on a root-port change; 0 is today's
behaviour; never in containment or with memory that may still be written;
none while suspended.

1. **The value.** `XhciFailedRetrySec`, a `REG_DWORD` in the controller's
   driver key (and machine-wide in `Services\USB`, as every value), read
   at each start and latched for the lifetime. It is a setting of its own,
   not gated by `XhciTolerance`; at `0` there terminal 3 never occurs (the
   window is a tolerance behaviour) and only terminal 2 is tried again.
   `0`: never tried again
   (today's behaviour). `2` to `3600`: the first wait in seconds. Invalid
   values (`1`, above `3600`, another type) and absence use `5`, the
   default, which both INFs write (item 12); nothing reports a refusal.
   `1` is invalid because a try sooner than the recovery's own pacing gap
   (task 49.35, `XhciTolRecoverPaced`) gains nothing.
2. **Which terminals.** Terminals 2 and 3 alone. Never 4 or 5 (contained:
   the window stopped decoding, or DMA not proven stopped and the common
   buffer pinned), never with `CommonBufferPinned` or `Tol.Unreadable`
   set, and never 6 (`XhciInterruptMode` 4's lost message: the interrupt
   route is what failed, and a rerun would clear the latch it holds, the
   comment in `hcdRecover`).
3. **The waits.** The first try comes the value's seconds after the latch;
   each later wait doubles, to a ceiling of 600 s (ten minutes, the
   window's length; `XHCI_TOL_RETRY_CEILING_S`). The one exception, on
   purpose: a value above 600 keeps its larger interval for every try, not
   doubled and not cut to 600. The waits are measured on the tolerance
   clock (section 4.0), so each runs long, never short (about 2.2 times its
   nominal length at worst; 5 s is about 11 s by the clock): the Tree says
   "about".
4. **What a try is.** In the thread's pass after `hcdRecover` and
   `hcdTerminalRelease`, under the power gate, admitted only in D0,
   outside a power transition, started, with PnP saying present, no stop
   or removal begun (the leave signal of section 4.15.3 and `ThreadStop`
   clear; one raised after admission ends the try before the recovery is
   requested), and only once the terminal release's DMA question is
   settled (`TerminalReleased` set and its invalidation drained, or its
   proof taken and the devices dropped; a release that pinned the buffer
   is containment, item 2): USBSTS
   read, and not all ones (an all-ones read skips the try, counted, and
   the next wait doubles); then, under the controller lock, the window is
   reset as a start resets it (`Window.Refused` 0, its stamps cleared),
   `RecoveryFailuresConsecutive` 0, `TerminalReleased` 0 (so a later
   terminal is released again, the 35-V hang's rule), and the recovery
   requested (`RecoveryRequested` 1; `ControllerFailed` is still 1). The
   ordinary `hcdRecover` then runs it, Bus Master Enable put back by its
   continuation where the terminal release's proof cleared it
   (`hcdRestoreBusMaster`). The try is the window's first recovery.
5. **What ends the backing off.** A try that recovers, followed by ten
   minutes on the tolerance clock with no reset request, resets the next
   wait to the value. A try whose recovery fails (three in a row, terminal
   2) or that is followed by three recoveries within ten minutes (terminal
   3) latches again, and the next wait doubles. The tries are unlimited in
   number. **The bound, stated as two parts:** timed tries come no more
   often than the current wait, which doubles to the ceiling (600 s, or the
   value itself above 600; by the clock up to about 2.2 times as long);
   accelerated tries (item 6) come only in place of a timed one and are
   separately limited to one per back-off step and three in any ten
   minutes. So once the waits have reached the ceiling, at most one timed
   try per ceiling interval plus at most three accelerated tries in any
   ten minutes. Each try costs a reinitialization and a re-enumeration of
   what is plugged in.
6. **The port-change try, and its storm limit.** While latched with the
   value not 0, each thread pass (100 ms) in D0, under the power gate,
   samples every root port's PORTSC (reads only, no write). **Fresh
   evidence only:** at the latch each port's CCS is taken as its baseline
   and any CSC already set is consumed logically there (never counted);
   an episode is a CCS state different from the baseline that then reads
   the same on every sample for the stable-disconnect interval (at least
   1 s on the tolerance clock), and each episode is consumed once - the
   new state becomes the baseline. **The storm limit:** a port-change try
   takes the place of the next timed try, never adds to it: the back-off
   level still doubles after it, and at most one port-change try is taken
   per back-off step, so a flapping connection cannot bring tries every
   base interval once the waits have grown; in no case more than three
   port-change tries in ten minutes. **The storm history outlives a
   latch:** the record of port-change tries (their tolerance-clock stamps,
   the last three kept, and the step each was taken in) belongs to the
   controller's lifetime, not to a latched episode: it is not reset at a
   relatch, at a try that recovers, or by the fresh window a try starts
   (item 7), and only a controller start clears it, as it clears the
   back-off level. Resetting it at each latch would let a device that
   latches the controller again after each try bring three tries per
   latch. A PORTSC reading all ones stops the
   sampling for the rest of the latched episode (the existing containment
   governs from there). **Safe with bus mastering off:** the reads are the
   CPU's memory reads of the controller's registers, which Memory Space
   Enable governs and the terminal release leaves set; Bus Master Enable,
   which its proof clears, governs only the controller's own requests,
   and a register read makes the controller write nothing; reading does
   not clear a change bit. A broken function may still fail to complete a
   read, and failed hardware may show no change at all: the reads are an
   opportunity, not a guarantee. The sampling costs a few register reads
   each 100 ms while latched, and keeps the thread waking. **Stable
   disconnect from sampled edges:** the place's stable-disconnect
   observation (section 4.5) is fed the sampled CCS and an edge, never CSC
   on every pass (the observer restarts its timer while `changed` is
   asserted), so a held place whose device was unplugged while the
   controller was stopped is released at the try. A device behind a hub
   cannot be seen while latched (its hub needs transfers): only the waits
   bring that try.
7. **The window and failures in a row.** Covered by item 4: a try starts a
   fresh window and a fresh run of failures; the backing off and the storm
   history (item 6) remember the earlier latches. With 50.30 where it
   applies, a device that stops answering no longer charges the window, so
   the tries meet controller faults.
8. **Containment, a pinned buffer, the lost-message mode.** Excluded (item
   2), and re-read at each try: a try whose recovery ends contained (its
   invalidation unproven and the proof failing, section 4.6's revision-10
   note) is never tried again in that lifetime.
9. **Power.** No try while suspended or in a transition: the deadline is
   only compared in D0 under the power gate. Today a resume of a latched
   controller is refused by `hcdResumeContained` before
   `XhciResumeController` clears `XHCI_EXT_FLAG_SUSPENDED`, and
   `hcdRecover` refuses while that flag is set, so a latched controller
   resumed into D0 could never be tried. The safe transition, under the
   power gate in the resume path: a latched controller whose terminal is
   2 or 3 (never a containment or the lost message, which stay refused as
   now) is taken to D0 without reinitializing - the suspended flag
   cleared, `ControllerFailed`, the terminal and the saved-state refusal
   kept, no Restore State, interrupts left masked - and the thread's try
   then admits it as any other; a deadline that passed in D3 brings the
   try at the first admitted pass.
   **The message-interrupt mode** (`XhciInterruptMode` 4) needs more:
   its suspend (`HcdMsg4Suspend`) sets `Msg4Barred` and clears its
   readiness, and `hcdRecover` refuses while barred, so clearing the
   suspended flag alone would still leave the try refused. In the same
   transition, under the power gate, for terminals 2 and 3 only: the
   connection is re-armed as the ordinary resume re-arms it
   (`HcdMsg4Resume`, `HcdMsgNtReArm` with its one repeat), its readiness
   set and the bar lifted only once the re-arm succeeded, with the
   interrupter still masked (`ControllerFailed` keeps it masked until the
   try's recovery unmasks it). A re-arm that fails latches the loss
   (`HcdMsg4LostLatch`, terminal 6) as the ordinary resume does, and
   terminal 6 stays excluded from every try. A loss already latched
   (terminal 6) is never re-armed here. **The resume's phase is settled,
   so its waiters get an accurate outcome:** the resume keep's phase
   (`ResumePhase`, and the pending keep state `hcdResumeKeepWait` waits
   on) is ended in this transition as "not kept" - a latched controller
   has no devices to keep - so the D0's completion and the children's
   resume proceed at once with the devices reported gone, never waiting
   for a keep that no reinitialization will make.
10. **98 SE and ME.** The same, at the same default. The known gap: after
    a controller recovery, devices re-enumerated on other ports got no
    Plug and Play from Windows ME (`hcdCfgFault`'s comment, 2026-10-04).
    A try comes after every device was already removed by the latch;
    where ME does not show one again, unplugging and plugging it in again
    does. Unlimited tries on 98 SE and ME also repeat the retained
    requests' handling, the waits for removed PDOs and that gap at each
    try; the backing off bounds how often. The release notes' line (record
    25 section 26) says so. Turning the tries off on 9x was rejected: it
    would leave every device lost where some would come back.
11. **A machine started from a USB drive.** The system disk left with the
    latch; a try cannot restore a system disk Windows has lost, and what
    Windows does when such a disk reappears as a new device is not read.
    Task 50.32's restart is refused by Windows for a disk holding the
    paging file.
12. **Rows and gates.** Both INFs write `HKR,,XhciFailedRetrySec,
    0x00010003,5` on every controller install path, without overwriting
    (34.1's rule); the INF gate's table gains its row (`Since` the
    release, the type, the default, why, and why that default) with its
    self-test rows (present, wrong default, clobbering flags); `tuning.md`
    gains its row in "The driver's own settings" and a paragraph in
    "Controller faults"; the installer's `instValues[]` and the Tree's
    `trsRows[]` gain it on the Tree's controller-faults tab beside
    `XhciTolerance` (record 25 section 22; `test_trset` holds the three in
    step with the INFs and `tuning.md`).
13. **What the user sees.** The tray's red icon (record 25 section 7.2.1)
    while latched, cleared when a try recovers; the Tree's controller line
    and details (record 25 section 26: the place involved, and the next
    attempt in about N s); the snapshot's terminal line says when the next try is due
    instead of "it stays failed until a stop and start", which stays the
    text at `0`. No new balloon. Draft wording for `tuning.md`, for the
    owner: "`XhciFailedRetrySec`: when a controller has stopped after
    repeated failures, the driver may try it again by itself: first after
    this many seconds, then after waits that double up to ten minutes (a
    value above 600 keeps its own longer wait); a device unplugged from or
    plugged into one of the controller's own ports (not a hub's) can bring
    the next try sooner, but not more often than the waits allow. `0`
    leaves a stopped controller stopped until Windows restarts or it is
    disabled and enabled. Default `5`. The waits are approximate and can
    run up to about twice as long. A controller closed off because it
    stopped answering is never tried again." Row: minimum `0`, maximum `3600`,
    default when absent `5`, written by the install `5`.
14. **Notes and counters.** `ctrl.retry` (try number in 31:16, the wait
    just ended in seconds in 15:0) at each try; `ctrl.retry.why` (1 the
    wait, 2 a root-port change); `ctrl.retry.skip` (1 all ones, 2
    contained, 3 not in D0); `Tol.Stats.Retries`, `RetriesRecovered`;
    the retry state (level, seconds to the next, the last cause) in the
    recovery-status request (section 4.15.8) and the snapshot.
15. **Host vectors** (`test_tol`): the value's reading at every boundary
    (0, 1, 2, 3600, 3601, absent, wrong type); the doubling and the
    ceiling, a value above it; the clean-run reset at the tick boundary;
    the admission (each terminal 2 to 6, pinned, unreadable, D0 or not);
    the rate limit of the port-change try; a try that latches again.
16. **Legs** (qemu flavour; 98 SE, 2000, XP, 7; ME as a reading). (a)
    Latch by code `05` (HCH) four times inside the window, a device
    connected: at about the value's seconds (by the clock) the controller
    is tried, recovers, and every device enumerates again; the Tree's
    line clears. (b) With an HCH injected again after each try: the waits
    double to the ceiling (read from `ctrl.retry`), the controller latched
    meanwhile. (h) A flapping connection (a `device_add` and `device_del` loop at a
    root port) while latched, and a CSC already set at the latch: the
    tries stay within the storm limit, across a relatch after a try. (i) A
    resume of a latched controller (2000 and XP standby where the guest
    resumes, hibernation; and on 2000 and XP under `XhciInterruptMode` 4,
    the re-arm and the bar):
    the try after D0. (j) A disable of the controller while a try is due:
    no try begins. (c) The primary case, by the owner's reading of the
    field:
    latch the controller with a device whose aborts fail (code `26`,
    outcome 5, the stop event missing, or outcome 6, a divergence -
    outcome 2 is a clean stop and would isolate the device instead -
    persistent until its disconnect: a recovery at each time-out, the
    window refused, the latch; and once more as leg 13 of section 4.15.9,
    the simulated audio device arming code `26` by its own unanswered
    request), then
    unplug the failing device (`device_del`) while latched: the try comes
    at once (`ctrl.retry.why` 2), the other devices come back, the device
    plugged in again enumerates; again with the device behind a hub, where
    the try comes at the wait instead, and with the device plugged at once
    into another root port or the other controller of this driver. (d) Containment by code `06` and `07`: no
    try, ever. (e) `XhciFailedRetrySec` 0: no try. (f) A suspend while a
    try is due (2000 and XP hibernation, 98 SE standby where the guest
    resumes): no try in D3, the try after D0. (g) ME: a try and which
    devices Windows shows again.

#### 4.15.14 Order, commits and risk

**Order.** Each step a commit or a short series on its own branch, each
able to merge alone in this order:

1. **Reasons** (4.15.8): the request's new signature at every site, the
   ring, the counters, the notes, the recovery-status request and the
   snapshot's print. No behaviour changes. Lowest risk; it also gives the
   Tree (record 25 section 26) its data.
2. **The forcing values** (4.15.9): the qemu flavour's three codes and
   their host vectors; release unchanged.
3. **The device fails alone** (4.15.3 to 4.15.7, 4.15.11): the wait and
   the verdicts, the table, the callers, the Address Device path and hold
   reason 4, `XhciTolLateVerdict`, `hcdCfgFault`'s step, the spread records, `XhciDeviceIsolation` and its
   empty table (one entry since revision 38), the ownership audit;
   legs 1 to 10.
4. **The tries** (4.15.13): the value, the INFs, the gate, `tuning.md`,
   the installer's and the Tree's rows, the tries and the port-change
   read; its legs. Independent of step 3 in code (it can merge before it),
   but its primary case is read with step 3 in.
5. Task 50.32 (record 25 section 26), the tool's own branch: it reads
   step 1's part and falls back where a driver does not give it.

**Risk, honestly, under the owner's default.** The release is otherwise
close to its cut.

- Step 3's code ships, but acts only where `XhciDeviceIsolation` makes it
  apply: at `1` on a listed controller, or at `2`. With the table empty at
  the cut, **the isolation policy is off for every user at the default**
  (since revision 38 one controller is listed, `8086:1E31`, unread on real
  hardware: the policy is on at the default on it and off on every other).
  That is narrower than "the default changes nothing": still active at
  the default, on every controller, are the engine's rewrite correction
  (section 4.15.9, always on, its own reviewed commit), the reasons, notes
  and counters with the recovery-status request (step 1), and task
  50.31's tries, which are on by default (step 4). On every controller
  but `8086:1E31` the risk of step 3 at the default is then the gating
  itself (a path reachable at `0` that should not be: host vectors at each
  value, leg 11); on `8086:1E31` the risks of where it applies, which
  follow, are default risks. Where it applies, it
  touches `hcdCommand`, which every
  command passes through, the ownership of what an aborted command named
  (section 4.15.5a: a caller missed in that audit could free memory the
  controller still references), the enumeration's failure path and the
  late-transfer verdict; the thread can be held about 42 s where it gave
  up at 5 s; and on 98 SE and ME the longer waits, the stop and power
  ordering, the listed-PDO completions, the composite teardown and the
  PnP-held re-enumeration are the main risks. It needs its legs and the
  device matrix before any controller is listed, and the bench reading
  before each entry (the owner listed `8086:1E31` before its bench
  reading, section 4.15.10).
- Step 4 is on by default. It runs only on a controller already latched;
  its risks are repeated bursts of recovery and re-enumeration (bounded by
  the backing off and the storm limit), the new resume transition of a
  latched controller, and on 98 SE and ME the reappearance gap at each
  try. `0` turns it off.
- Step 1 changes no decision; its risk is a missed site (a compile error
  by construction, plus the audit of direct assignments) and the new
  request (the query's version 3 is untouched; both directions of
  compatibility are legs).
- The Tree's restart (step 5) is a tool change; on 98 SE and ME it is not
  offered unless its guest reading succeeds.

#### 4.15.15 Settled, and what is left

Settled by the owner on 2026-11-17 (section 4.15.0): the enablement and
its table, the hub, the spread records, the tries' numbers, the medium
check, the 9x restart route and the wording rule. Later work, not in this
release: a hub that itself stops answering cycled at its upstream place;
the medium check's stop and retire; escalation from the spread records, if
readings ever justify one; default-on beyond the listed controllers.
Left to the implementation, against this text: the request's function code
next to the holds request's and the exact field layout of the
recovery-status block (step 1). The ownership audit is the design's, not
the implementation's (section 4.15.5a's table, revision 22): a caller it
does not list is a change to that table, reviewed, before code.

**What steps 3 to 5 still wait for.** Revision 22 answers the review of
revision 21 and of steps 1 and 2 in the text, and revision 23 the review
of revision 22 (the settlement state and its one admission exception,
the slotless debt across a restore, the returned slot's checks); steps 3
to 5 begin their behavioural code once revision 23 has been reviewed in
turn. Steps 1 and 2 (the reasons and the status
request; the forcing values, with code `26` armed by behaviour) change no
decision the driver makes in `release` and were cleared to start on
revision 20; the engine's rewrite correction is a behavioural change at
every value and goes as a commit of its own with its own review.
That commit goes into this release and is not deferred to a later one
(owner, 2026-11-17).

#### 4.15.16 After step 3's guest legs (revisions 25 to 34)

Step 3's guest legs (`runs/run-50.md`, "50.30 step 3 (device isolation):
the guest legs", emulation only) found two driver defects, F1 and F4, a
gap in the forcing values, F3, and a classification, F2, whose hub port
words were captured afterwards. This section is the design for each,
written before code and reviewed before any is built. For each: the
rule, the code it touches, what stays, and the legs that will show it.
None adds a registry value: no release driver reads anything new, so
neither INF nor `tuning.md` changes, and the one value the third reads
is the qemu flavour's existing `XhciQemuInject`, whose rows are in
`build-and-test.md`.

**A. A root port's hold is observed before it suppresses (F1).**
What was read: after a root port held with reason 4, the first
`device_del` and `device_add` released the hold (`qemu.inj.disarm`,
`tol.loc.rearm=<port>04`) but the device was not enumerated, and the
port stayed `Failed` until a second replug; read six times on 2000, 98
SE, ME, XP and 7, while at a hub's port one replug was enough. The cause,
from the code: in `hcdPortChanged` (`src\hcd_enum.c`) the hold-4 block
removes `XHCI_LINK_FEED_CONNECT` and the link action from the sample
first, and only later in the same sample does `hcdTolPortFeed` reach
`HcdTolLocObserve`, whose stable-disconnect evidence releases the hold on
that very sample - the reconnect's CSC. The connect it carried is already
gone, and a `Failed` machine fed no connect stays `Failed`:
`XhciLinkPortFeed` (`src\xhci_link.c`) feeds a connect from `Failed`
only with a CSC, and later samples have none. A hub's port is observed
from its status before its decision is taken (`HcdHubPortLook`), which
is why one replug serves there.

- **The rule.** Each root-port sample is observed first: the place's
  stable-disconnect observation for the sample (`HcdTolLocObserve`, with
  the sample's CCS, PP and CSC, at both of `hcdTolPortFeed`'s tolerance
  branches as today: at `XhciTolerance` 0 only where `XhciTolLocActive`)
  runs before hold 4 is read, so a hold the sample releases no longer
  suppresses that sample, and its connect reaches the machine as on any
  port. While the place is still held after the observation, the
  suppression is exactly as today: no link action (`HeldLinkSkips`
  counted), no warm reset, no connect fed. One observation per sample:
  `hcdTolPortFeed` does not observe the sample a second time. USB 2 and
  USB 3 root ports alike - the observation and the hold are the same on
  both; only item 5 of section 4.15.4 (a USB 3 root port not disabled)
  differs, and it is untouched.
- **Code.** `hcdPortChanged` and `hcdTolPortFeed` in `src\hcd_enum.c`:
  the observation moved ahead of the hold-4 block (or `hcdTolPortFeed`
  split into its observation and the rest, the observation called first);
  nothing in `xhci_tol.c`'s release rule (`XhciTolLocObserveChange`)
  changes.
- **What stays.** The release evidence itself (section 4.5's stable
  disconnect: CCS clear with PP set for the interval, then a connection;
  or a controller start); hold 4's suppression while held; the order of
  everything before the hold-4 block (the change bits acknowledged,
  selective suspend's sample, 29-A.5's hold and its companion); the link
  keep, the issue 29 flap count and the over-current and PED faults,
  which run after the observation as they do now; the hub port path.
- **The legs.** Section 4.15.9's leg 1 (`26` at a USB 2 root port, a
  mouse and the composite) and leg 5 (`24` with a SuperSpeed stick at a
  USB 3 root port) on 98 SE, ME, 2000, XP and 7: one `device_del`, a few
  seconds, `device_add`, and the device enumerates (`tol.loc.rearm` then
  the enumeration's lines, no second replug). And the suppression still
  holding: a replug faster than the stable-disconnect interval
  (`device_del` and `device_add` with no wait) leaves the place held and
  nothing enumerated, and the next proper replug enumerates. A hub's port
  (leg 2) unchanged: one replug.
- **As built** (`5935d9ec7`, revision 26): `hcdPortChanged` calls
  `HcdTolLocObserve` once, after the link feed is decided and before the
  hold-4 block; `hcdTolPortFeed`'s two observations are gone, its hold
  reads unchanged. The call stays on the controller thread at
  PASSIVE_LEVEL, outside the controller lock, as it was. `test_tol`
  gains the vector that a replug inside the stable-disconnect interval
  leaves hold 4 standing; `hcdPortChanged` itself has no host suite.

**B. A refused Enable Slot is a deferred enumeration, reconsidered when
admission opens again (F4; revised in revision 26).** What was read: a
root hub started while a debt stood (E7G on 2000, X7G on XP, one reading
each) had every root port's Enable Slot refused, each port ending
`Failed` with the cause "no slot" after its retry; the settlement then
cleared the debt with no reset, and nothing looked at those ports again
- no device listed until a further disable and enable of the root hub.
Where the debt ended in an HCRST instead (E7E), the recovery's
re-enumeration brought every port back. The cause, from the code:
admission refuses the Enable Slot correctly (`hcdLeftAdmit`,
`src\hcd_enum.c`; `hcdCommandEx` then answers `HCD_CMD_LEFT` under a
debt, and 0 under the Enable Slot hold, `Left.EnableHeld`), but
`hcdPerform`'s Enable Slot action passes either refusal to the machine
as an ordinary failed command, so `XhciEnumStep` fails the step with
`XHCI_ENUM_FAIL_NO_SLOT`, `hcdRun` spends the retry on another refusal
and leaves the port `Failed`, and a hub's port is given up and disabled
(`HcdHubPortDisable`). Nothing schedules a second look when the debt
clears, and a plain inspection is not enough: the port reads connected
with no CSC, and a `Failed` machine fed no connect stays `Failed` (A
above). A debt clears by two routes that leave admission open: the
settlement's clean clearing (`hcdLeftClearRelease`), and a successful
Restore State (`HcdEnumLeftRestored`, `src\hcd_enum.c`, through
`XhciLeftDebtRestored`, `src\xhci_tol.c`, called from the power path,
`src\hcd_power.c`), which clears a debt directly without the settlement.
Revision 25 served only the first; this revision serves both.

- **The refusal.** An Enable Slot refused at admission - by a standing
  debt or by the Enable Slot hold, before anything is submitted - is not
  the controller saying it has no slot, and is not a failure of the
  device. `hcdCommandEx` answers it with a value of its own,
  `HCD_CMD_NOT_ADMITTED` (not a completion code, as `HCD_CMD_LEFT` is
  not), in place of `HCD_CMD_LEFT` or 0: today the debt's refusal
  answers `HCD_CMD_LEFT`, which an Enable Slot whose own wait a route
  left also answers, and the hold's answers 0, which a halted controller
  also answers. Every other command refused at admission keeps
  `HCD_CMD_LEFT` and its marking (an Enable Slot marks nothing,
  `XhciTolAdmitMarks`). The one other Enable Slot, the resume keep's
  restore (`hcdRkeepRestore`), takes every answer but success as a
  failure, so it reads the new value as it reads the refusal today. On
  `HCD_CMD_NOT_ADMITTED`, `hcdPerform`'s `XHCI_ENUM_ACT_ENABLE_SLOT`
  feeds the machine a deferred failure (a flag on the event), and
  `XhciEnumStep` (`src\xhci_enum.c`, `src\xhci_enum.h`) fails the step
  with a new cause, `XHCI_ENUM_FAIL_DEFERRED` (9), which
  `XhciEnumRetriesFor` gives no retry: a retry under the same refusal is
  refused again, and no retry is spent. `hcdRun` writes the fail note
  (`enum.port.fail`, cause 9) and does not give a hub's port up: no
  `HcdHubPortDisable` for this cause. An Enable Slot whose own wait was
  left stays as today (its debt is the left command's, and the route that
  left it removes the port or ends in the HCRST). An Enable Slot the
  controller answers with No Slots Available, or any other completion
  code, stays `XHCI_ENUM_FAIL_NO_SLOT` with its retry: real slot
  exhaustion is unchanged.
- **The obligation and its one lock.** The deferral leaves an obligation
  on the port: a flag (`EnumDeferred`), the generation it was taken in
  (`EnumDeferredGen`) and the port's connect generation at the deferral
  (35-T.3/4's, moved by every accepted connect, disconnect and cycle
  abandonment, `hcdFeed`), with a count of ports owed on the controller
  (`EnumDeferredCount`) and the controller's obligation generation
  (`EnumDeferredGenNow`). All five are read and written only under the
  controller lock, at any IRQL up to DISPATCH_LEVEL. The thread takes the
  obligation in `hcdRun` as it records the failure. An HCRST clears every
  obligation from `HcdEnumLeftHcrstLocked` (the invalidation of
  `XhciSlotInit`, `src\hcd_dev.c`, at DISPATCH_LEVEL, the lock held) by
  advancing `EnumDeferredGenNow` and zeroing the count, walking no port:
  a flag of an older generation reads as clear, and the thread clears it
  when it next touches the port. Every clearing of one port's flag
  decrements the count only if the flag was set and current, so a
  clearing that loses a race to another (an HCRST, a removal, an accepted
  change, the resumption) does nothing a second time.
- **When it is served: whenever admission is open.** Reconsideration is
  not tied to one clearing. The controller thread, in its powered service
  pass (`HcdEnumService`, PASSIVE_LEVEL, the power gate held as for every
  pass), serves the obligations whenever, under the controller lock, the
  count is not zero and admission is open again: no debt
  (`Left.Owed` clear), the Enable Slot not held, the controller not
  failed or halted, its thread not stopping, and the root hub started.
  The clean clearing (`hcdLeftClearRelease`) and the successful Restore
  State's clearing (`HcdEnumLeftRestored`) each also mark the thread's
  next pass as owed (the existing pass request), so the service is not
  left to an unrelated wake; the power path only marks, and the thread
  does the work, after its normal service admission has resumed. A
  settlement that ends owing a reset leaves admission closed (the hold),
  and the HCRST that follows clears the obligations; its re-enumeration
  looks at every port afresh (E7E's reading).
- **What the service does, and only while it still applies.** For each
  port whose flag is set and current, under the controller lock: the
  flag is cleared (the count decremented), and the port is resumed only
  if it still owns the slotless deferred failure it was taken for - its
  machine `Failed` with the cause `DEFERRED`, no slot and no device, and
  its connect generation the one recorded at the deferral - and its
  place is still the bus's: a root port with the root hub started, or a
  hub's port whose hub is not `Draining` and still has its device.
  Resuming is two things and no more, after the lock is let go: the
  machine put back to `Empty` (`XhciEnumReset`), and the port's ordinary
  inspection owed - a root port's (`hcdPortInspectAgain`) and a hub's
  port by its hub's `Changed` bit. The inspection then decides as it
  does for any port: from `Empty` and connected it feeds a connect with
  no CSC needed (`XhciLinkPortFeed`'s `Empty` row, `XhciHubPortDecide`'s
  at a hub's port), which is the case of a port left `Failed` with no
  connect change that a plain look would miss; and every existing gate
  stays the inspection's - a location hold (any reason), 29-A.5's hold
  and its companion, the serial-deferral latch of task 45.40, selective
  suspend's ownership of a port, the power gate and the stop's and
  removal's ownership of a port. Nothing is fed past them: the service
  feeds no connect itself. A port whose conditions no longer hold only
  loses its flag. A note at the service (`cmd.left.enum`: ports resumed,
  ports dropped).
- **Cancellation and replacement.** The obligation ends without a
  resumption when the port's machine accepts a departure or a new
  enumeration: `hcdFeed` clears the flag where it moves the connect
  generation (an accepted disconnect, an accepted connect that starts an
  enumeration, a cycle's abandonment). A raw connection change that the
  machine does not accept leaves the flag: a `Failed` port's disconnect
  sample feeds no disconnect (`XhciLinkPortFeed`), so the flag stays,
  and the service's resumption then inspects a port that reads empty and
  feeds nothing - harmless. A new deferral of the same port replaces the
  old flag (same port, current generation, the new connect generation).
  A hub's departure and the root hub's stop clear their ports' flags
  where they reset the ports, idempotently.
- **Snapshot and tools.** The snapshot exports a port's `FailCause`; the
  new value 9 is carried in the existing field, so no offset, size or
  existing cause number changes, and readers that bound their tables
  print "?" for it, as both XHCITR tables do for 8 today. The count, the
  generations and the note's counter are not exported in this revision.
  `XHCITR` (`xhcitr\tr_snap.c`, `enum_cause_name` and `print_hcd`'s
  `cause_short`): names for causes 8 and 9, their wording a draft for the
  owner.
- **What stays.** Admission itself (`hcdLeftAdmit`, `XhciTolLeftAdmit`):
  nothing is submitted under a debt but the settlement's own commands,
  and the Enable Slot hold still holds until an HCRST or a start; the
  debt, its list, its settlement and `XhciLeftDebtRestored`'s rules; the
  HCRST route's re-enumeration; the cause, retry and give-up of a slot
  the controller refuses; the machine's every other failure; every gate
  named above.
- **The legs (emulation) and vectors.**
  1. E7G and X7G again (the root hub's removal, then its start at once,
     `24` 0x88 outcome 1, on 2000 and XP): every root port's Enable Slot
     refused with cause 9 and no retry spent, then after
     `cmd.left.settled=00000000` the `cmd.left.enum` note and every port
     enumerating with no further action - every hub and device listed,
     the stick's copy compared.
  2. The same twice in one controller start (a repeated debt): deferred
     again, resumed again after the second clean settlement.
  3. E7E again (outcome 5): the HCRST's re-enumeration as before, the
     obligations dropped by the generation (`cmd.left.enum` absent).
  4. A device plugged at a free root port inside the debt's window:
     deferred, enumerated after the clean settlement.
  5. Around the clearing: the device of a deferred port unplugged before
     the settlement ends (the resumption inspects an empty port, nothing
     enumerated, no error); a device plugged anew at a deferred port so
     that its accepted connect cancels the obligation before the service
     (one enumeration, not two); the root hub disabled while ports owe
     (the flags dropped with the ports).
  6. A hold kept: a root port held with reason 4 (`26`) when the debt
     legs run: after the clean settlement that port stays held and
     unenumerated while the others enumerate.
  7. Real slot exhaustion: the emulated controller limited to fewer slots
     than devices (the emulator's slot count, if its controller model
     takes one; to be read before the leg), the extra device's Enable
     Slot answered No Slots Available: cause 2 with its retry, no
     deferral, no `cmd.left.enum`.
  8. Host vectors: the machine's new cause and its zero retries, the
     return to `Empty`, and a hub port's decision from `Empty` and
     connected (`test_enum`, `test_hub`); the service's decision -
     resume, drop, or leave - as a pure function of the flag, the
     generations and the machine (`test_enum`); and the restore-clear
     route: a debt that `XhciLeftDebtRestored` clears leaves admission
     open, and the same decision then resumes (`test_tol`, `test_enum`).
     The restore-clear route in a guest needs a controller that declares
     the save capability, which QEMU's does not (section 4.15.10's
     list); it is owed there.
- **Hub ports.** The refusal handling at a hub's port - cause 9, no
  retry, the port not given up or disabled - is reachable in a guest by
  routes the step-3 legs already used: XP4 (a hibernation while a hub
  child's Address Device was withheld: its retry's Enable Slot refused
  under the debt, then "hub port given up, disabled" before the power
  teardown) and E7S (the 2000 shutdown, the same class of sequence). With
  this change both must read cause 9, no retry and no give-up before the
  teardown; that is owed in a guest, on XP (blind, hibernation) and 2000
  (shutdown). The reconsideration after a clean settlement on a hub that
  survives the debt is a separate integration obligation: every debt
  route the legs have removes or powers down the hubs, so it needs a
  route that keeps them - a query-stop harness (section 4.15.16's
  question to the owner) or a qemu-only arrangement that leaves a debt
  standing with the hubs live - and until one exists it is an open
  question for the owner, not discharged by vectors.
- **As built** (`23ceaad05`, its obligation's protocol made pure
  afterwards, revision 29). The flag and its two generations are a
  port's `XHCI_ENUM_DEFER` and the count and the current generation the
  controller's `XHCI_ENUM_DEFERS` (`src\xhci_enum.h`), and the decisions
  are pure functions that `hcd_enum.c` calls under the controller lock:
  `XhciEnumDeferTake` (taken, or a standing flag replaced),
  `XhciEnumDeferDrop` (idempotent; the count moves only for a current
  flag), `XhciEnumDefersRenew` (an HCRST or a start), `XhciEnumDefersOpen`
  (admission open and something owed) and `XhciEnumDeferServe` (stale,
  resume or drop, through `XhciEnumDeferResumable`). What the host
  vectors hold: the cause and its zero retries, every condition of the
  resumption (slot, PDO, device record, state, cause, connect
  generation, place), and the protocol - replacement, the drop counted
  once, every flag and the count left standing while admission is
  closed for each of its six reasons, the stale, resume and drop
  decisions, and an HCRST's generation leaving a later deferral's count
  untouched (`test_enum`'s `test_deferred` and `test_defer_obligation`);
  and the restore's clearing and the settlement's clean clearing each
  opening admission to the same decision, while a settlement owing a
  reset leaves it closed until the HCRST ends the obligation (`test_tol`'s
  `test_left_defer_open`, which links `xhci_enum.c`). What they do not
  hold: `hcdDeferService`'s walk of the ports, the inspection that a
  resumption owes, and the thread, the lock and the power gate around
  them. Those, and legs 1 to 7 and the hub ports above, are owed in a
  guest; the step-3 legs at `23ceaad05` read the refusal (cause 9, no
  retry, a hub's port not given up) and the HCRST route, not the
  clean-settlement resumption (`runs/run-50.md`). On the final image of
  `b80630837` the resumption after a clean settlement was read on XP and
  2000, in a debt that section E's bit 7 forms and holds with every hub
  and device present (`runs/run-50.md`, "50.30 step 3 (device isolation): E, and B, C and F on the final image"):
  deferrals with cause 9 and no retry at a root port and at a surviving
  hub's port, then `cmd.left.enum` and both enumerating with no action;
  a second debt in the same start; a device plugged during it; one
  unplugged before the clearing, nothing enumerated there. Still owed:
  leg 5's other two cases (a fresh accepted connect cancelling a
  deferral before the service, the root hub disabled while ports owe),
  leg 6 (a hold standing before the debt kept across the settlement -
  the hold read in these legs was made after it), the restore-clear
  route, and the XP4 and E7S routes' hub-port refusal, which the legs
  did not reach on XP or 2000 (the withheld command's wait had ended
  before the power route came).

**C. The recovery command withheld once more: bit 7 of `24` and `25`
(F3; revised in revision 26).** What was read: with `0x1F258891` the
late path's Stop Endpoint ran (`xhci_inj.h` still reserves the bit, and
`xhci_inj.c` reads bit 7 for `26` alone), so section 4.15.9's leg 7 and
the acceptance list's scratch taint, failed Disable Slot quarantine,
interrupted settlement and failed settlement could not be forced. The
withholding matches by the fault's own command type
(`XhciInjHangCmdType`), so it can never select the Stop or the Disable
Slot that follows.

- **The rule** (qemu flavour only). When `24` or `25` with argument bit
  7 makes its primary withholding (`HcdInjCmdDoorbell` for `24`,
  `HcdInjEp0Doorbell` for `25`), the layer latches a one-shot
  **recovery-command obligation**: the slot it withheld for, and the
  commands that may meet it - for `24` that slot's Disable Slot; for `25`
  the first of that slot's Stop Endpoint whose Endpoint ID is 1 (DCI 1,
  EP0: the late path's) and its Disable Slot (a cycle's teardown after
  the thread's EP0 work was abandoned, which sends no Stop of EP0). A
  Stop Endpoint of any other DCI never matches. The obligation is the
  latched slot's, not the primary's count: it outlives the primary count
  being spent, and it survives an admission refusal - under a debt the
  ordinary Disable Slot is refused and the settlement's Disable Slot of
  the same slot (`hcdCmdSettle`) is the one admitted, so it is that one
  that meets the obligation. It is consumed only at the doorbell of a
  matching command the engine has actually made outstanding, as the
  primary withholding is bound to the command placed (section 4.15.9);
  a refused or busy submission consumes nothing. The withheld recovery
  command goes through the same real watchdog and the same outcome
  (bits 6:4, but for outcome 7 below) as the primary, settlement
  submissions included, so its verdict is the engine's. The latch is
  dropped by `FF`, by re-arming, at every invalidation of the command
  ring (`HcdInjCmdRingReset`), and at every controller start
  (`HcdInjStart`). A later device given the same slot number cannot
  inherit it: the latched slot is given back either by a Disable Slot
  placed for it, which is a match and consumes the obligation, or by an
  HCRST or a stop, whose invalidation or next start drops it. The
  withholding writes `qemu.inj.hang.recovery` (code, slot, TRB type,
  outcome) and a debugcon line, so a leg can time a route against it.
- **Lock order.** As the layer's today: the controller lock first, the
  injection lock (`InjLock`) inside it (`HcdInjHangCommand`), and the
  doorbell's check under the submit's controller lock
  (`HcdInjCmdDoorbell`); no path takes them the other way.
- **The second scratch-taint occurrence: a cycle mark made inside the
  thread's own wait (revision 27).** A Disable Slot failing for a device
  whose EP0 work the thread abandoned for a cycle (`Ep0Abandoned`, then
  `hcdDisableRecord` tainting the scratch) needs a cycle mark to be set
  while the thread waits on a withheld EP0 transfer. A new trigger value
  written during that wait cannot do it: the injection poll
  (`HcdInjPoll`) runs on the same controller thread, which is the one
  waiting, and the wait only requests drains. Revision 26 proposed a
  one-shot timer and DPC of the layer's own; that is withdrawn, because a
  new callback brings a lifetime of its own (admission, draining queued
  and running callbacks before the controller's storage and locks go,
  cancellation at DISPATCH_LEVEL, a stale callback meeting a later arm)
  that the vehicle does not need. Instead no new callback exists: `25`
  with bit 7 and outcome 7 (unused today) arms, at the primary
  withholding, a **mark deadline** in the injection state - the latched
  slot and a tick about 1 s after the withholding, by the driver's own
  clock (`HcdTolNow`), well inside the thread's 5 s deadline. The
  controller thread's own EP0 wait loop (`hcdThreadControlQuiet`'s look
  loop, `src\hcd_enum.c`), at each look and before it takes the
  controller lock for that look, calls one qemu-only hook
  (`HcdInjWaitLook`, PASSIVE_LEVEL, no lock held). The hook reads the
  deadline under the injection lock; when it is armed for the slot of the
  device this wait is for and its tick has passed, it disarms it in the
  same hold (one shot), lets the injection lock go, takes the controller
  lock, and, only if the device's record is still the one in
  `SlotDevice[]` for that slot, marks the cycle through the one entry
  point every mark goes through, `HcdTolCycleMark` (DISPATCH_LEVEL under
  the controller lock), with reason `XHCI_TOL_CYCLE_REFUSED_CODE` and DCI
  1 - a reason `hcdCycleResolve` takes as it stands. The same look then
  sees the mark pending, resolves it (`XhciTolWaitResolved`'s abandon),
  the EP0 work is abandoned with its TD still on the ring
  (`Ep0Abandoned`, the scratch held), the cycle's teardown issues the
  device's Disable Slot, and that is the latched recovery command,
  withheld and acted out as outcome 1 (the clean abort: completed with
  25). The Disable Slot fails, the scratch is tainted, and the reset is
  requested with its reason. Because the hook runs only on the
  controller thread, inside a wait the thread is in, there is nothing to
  admit, cancel or drain: the controller and its locks outlive every
  call by construction, and a deadline cannot act on a later arm,
  since each arm replaces the deadline in the injection state and the
  hook disarms it when it acts. `FF`, a re-arm, every invalidation and
  every start drop the deadline with the latch (`XhciInjHangEnd`,
  `XhciInjHangRingGone`). A wait for another device, or one that ends
  before the tick, leaves it to expire unused at the next drop.
- **A controlled window after a failed settlement.** An ordinary
  settlement failure requests the reset at once, and the recovery runs
  on the controller thread at its next pass, so a power transition
  started from the guest after the failure is seen cannot be placed
  before the recovery by timing alone. A qemu-only rendezvous point is
  added to code `23` (its point 3): when armed, the controller thread
  holds at the recovery's entry, after the request and before the
  recovery takes the power gate - holding no lock and not the power
  gate - writes `qemu.inj.rv.reached` and a debugcon line, and polls
  every 100 ms; it goes on at `23` with argument 0 (the release), after
  at most 60 s, and at once when any route's bit is raised (a power
  transition, a removal or a stop). The leg arms the point, makes the
  settlement fail, waits for the line, starts the hibernation; the power
  IRP raises its bit, the point lets the thread go, and the recovery
  then meets the power transition and defers as section 4.15.3 and the
  power path already provide for a recovery requested in that state. The
  resume (a reinitialization in QEMU, an HCRST) then shows the failure's
  reset carried through the power transition. It does **not** show the
  successful-Restore-State guard: QEMU declares no save capability, so
  no resume here reaches that guard (section 4.15.10's list keeps it).
- **Selection beyond the first match.** The first matching command after
  the primary is the one taken. A leg that needs a later one - a resend,
  or the settlement's command when an ordinary command of the same type
  and slot would come first - uses a rendezvous of `23` or is built so
  the first match is the one wanted; no ordinal is added in this
  revision, and a leg that cannot be built that way stays owed.
- **Code.** `src\xhci_inj.h` (the bit's text; outcome 7's meaning for
  `25`; the latch's fields in `XHCI_INJ_HANG`: the recovery slot, the
  allowed types, armed; the rendezvous point 3); `src\xhci_inj.c`
  (`XhciInjHangArm` keeping bit 7 for `24` and `25`; the latch at the
  primary's spend; a pure `XhciInjHangRecoveryFits` taking the TRB type,
  slot and Endpoint ID, and its consumption; `XhciInjHangRingGone` and
  `XhciInjHangEnd` dropping it); `src\hcd_inj.c` (`HcdInjHangCommand`
  marking a recovery command that fits, `HcdInjCmdDoorbell` withholding
  it, `HcdInjHangSubmitted` unchanged; `HcdInjWaitLook` and its call
  in the EP0 wait loop of `src\hcd_enum.c`; the
  rendezvous point at the recovery's entry, `src\hcd_ctl.c`'s recovery
  calling it under `XHCI_FLAVOUR_QEMU`). Everything under
  `XHCI_FLAVOUR_QEMU`, compiled out of `release` as the rest of the
  layer; the release image's import and string checks show it as they do
  for the layer today. `build-and-test.md`'s rows for `23`, `24` and `25`
  gain the new meanings. Host vectors in `test_inj`: the latch at the
  primary, the match by slot, type and DCI 1, survival across the
  primary count and a refusal, consumption at the doorbell only, the
  drops, outcome 7's deadline (armed, acted on once for its slot after
  its tick, never for another slot, dropped with the latch).
- **What stays.** Every other code and point; the primary withholding
  and its outcomes 0 to 6; `26`'s bit 7 ("armed by behaviour"); the
  release build.
- **The legs (emulation).**
  1. `25` bit 7 (outcome 1) at a published device's thread transfer: the
     late path's Stop of DCI 1 withheld - the reset with its reason,
     `ScratchTainted`, the device's EP0 scratch held (scratch taint, the
     first occurrence).
  2. `25` bit 7 outcome 7 at a published device's thread transfer: the
     mark made in the wait's look, the abandonment, the cycle's Disable Slot withheld and
     aborted - the scratch tainted at `hcdDisableRecord`, the reset with
     reason `CMD_KEPT`, the aborted Disable Slot's TRB type in its extra
     (scratch taint, the second occurrence). The engine's abort requests
     that reset first; the `DISABLE_SLOT` request of `hcdDisableSlotId`
     follows in the same pass and the one recovery serves both, so the
     recovery's record names `CMD_KEPT` (owner, 2026-11-19, given through
     the coordinator: the legs name the reason the driver records, and the
     code is unchanged).
  3. `24` bit 7 at a fresh plug at a root port: the Disable Slot after
     the aborted Address Device withheld - reason `CMD_KEPT`, as in leg 2,
     the record quarantined, the root place already held (failed Disable
     Slot quarantine).
  4. `24` 0x88 bit 7 outcome 1 with the root hub's removal leaving the
     Address Device: the settlement's Disable Slot of the left slot
     withheld and aborted - the ordinary settlement failure: in one hold
     the owed records converted to quarantine, the debt cleared and
     Enable Slot held, read as `cmd.left.settled` with a reason in bits
     31:24 (`DISABLE_SLOT`, the slot in 23:16), then `ctrl.failed.why`
     and one recovery. (`cmd.settle.failed` belongs to the interrupted
     and overflowed branch, leg 5, not to this one.)
  5. As 4, with a second route raised while the settlement's Disable
     Slot is withheld (the controller's disable, or a hibernation on the
     blind XP guest), timed on the recovery line: `cmd.settle.left`,
     then once every bit is clear `cmd.settle.failed` and the reset
     (revision 24).
  6. As 4 with the rendezvous point 3 armed, on the blind XP guest: the
     failure, the point reached, the hibernation, the recovery deferred
     across it, the resume's reinitialization - the ordinary settlement
     failure followed by a power interruption and the resume. Redesigned
     in revision 34 (below, "Leg 6 redesigned"): its arrangement and the
     point's release.
- **As built** (revision 30). `XHCI_INJ_HANG` gains the latch
  (`Recovery`, bit 7 armed; `RecSlot`; `HoldRec`, the marked submit is the
  recovery command) and outcome 7's deadline (`MarkSlot`, `MarkTick`,
  `XHCI_INJ_HG_MARK_TICKS` 10 ticks of 100 ms), with pure
  `XhciInjHangLatch`, `XhciInjHangRecoveryFits`, `XhciInjHangRecoveryTake`,
  `XhciInjHangMarkDue` and `XhciInjHangActOutcome` (`src\xhci_inj.c`);
  `XhciInjHangRingGone` and `XhciInjHangEnd` drop the latch and the
  deadline, `XhciInjHangArm` keeps bit 7 for `24` and `25` alone.
  `HcdInjCmdDoorbell` latches at the primary of `24` and
  `HcdInjEp0Doorbell` at the primary of `25`, both under the submit's or
  the doorbell's controller lock and the injection lock inside it;
  `HcdInjHangCommand` marks a recovery command by its slot (the device's
  record may be going) when no primary fits, and `HcdInjCmdDoorbell`
  consumes the latch only at that command's own doorbell
  (`qemu.inj.hang.recovery`, a debugcon line). `HcdInjCmdAbortWritten`
  acts outcome 7 out as outcome 1. One departure from the rule above:
  under outcome 7 the Stop Endpoint of DCI 1 does not match. The rule's
  parenthesis says a cycle's teardown after an abandoned EP0 transfer
  sends no Stop of EP0, but the teardown stops a retained endpoint whose
  context reads Running (`hcdDeviceStop`), and the abandoned transfer
  leaves EP0 so; that Stop goes out, and the cycle's Disable Slot is the
  command withheld, as leg 2 needs. `HcdInjRendezvous` writes its
  debugcon line at once when a point is reached, since the log ring is
  not flushed while the thread holds there. `HcdInjWaitLook` (`src\hcd_inj.c`) is
  called at the head of each look of `hcdThreadControlQuiet`'s
  post-submission loop, before the look's `KeWaitForSingleObject` on the
  deadline and its controller lock (`qemu.inj.hang.mark`, slot and
  whether marked). `hcdRecover` (`src\hcd_ctl.c`) calls
  `HcdInjRendezvous` with point 3 at its entry while a recovery is
  requested, before `HcdPowerGateEnter`, and `HcdInjRendezvous` lets
  point 3 go when `CmdWaitLeave` is not 0; since a route raises its bit
  only where the new handling applies (`HcdEnumLeaveRaise`), leg 6 runs
  with isolation applying. All of it is under `XHCI_FLAVOUR_QEMU`.
  Host vectors (`test_inj`'s `test_hang_recovery`): no latch without bit
  7 or for `26` and `27`; the latch at the primary; the match by slot, by
  type and by DCI 1 for `25` alone; a check that consumes nothing and a
  take only for the latched slot, once; the latch outliving the
  primary's count; the drops by a lost ring, by `FF` and by a re-arm;
  outcome 7's deadline for its slot only, once, at its tick across the
  clock's wrap, dropped with the latch, and none without bit 7; and
  outcome 7 acted as 1. On the final image of `b80630837` legs 1 to 4
  were read on XP (`runs/run-50.md`, "50.30 step 3 (device isolation): E, and B, C and F on the final image"), leg 4 with
  section E's bit 7 forming the debt in place of the root hub's removal.
  In legs 2 and 3 the recovery's recorded reason is `CMD_KEPT`, which the
  engine's abort of the withheld Disable Slot requests first; the
  `DISABLE_SLOT` request follows in the same pass and the one recovery
  serves both, so the reading named `CMD_KEPT` where the legs then named
  `DISABLE_SLOT`; revision 34 names `CMD_KEPT` in legs 2 and 3. Leg 5 was
  not reached: no second route came inside the
  settlement's withheld Disable Slot (a hibernation reached the
  controller's power IRPs 11 to 74 s after its request). Leg 6 was not
  shown: rendezvous point 3 held the thread, and the hibernation's power
  IRPs reached the controller only once the point had let it go at its
  60 s bound, so the recovery ran before the power transition. Revision
  34 below reads why and redesigns the leg.
- **Leg 6 redesigned (revision 34, written before code; the owner's
  decision of 2026-11-19, given through the coordinator, in place of
  listing it as owed).**
  - *What was read.* In XC6 the controller's own power IRPs came 74 s
    after the hibernation was requested, after the point's 60 s bound.
    The point was not what stood in the way. While a recovery is owed,
    `ControllerFailed` stands, and `HcdEnumService` runs neither
    `HcdCfgCancelService` nor any other service that completes a
    cancelled transfer (`hcdHalted`): the recovery's invalidation
    completes them (section 4.6). A class driver whose power-down waits
    for its pending transfers to end therefore waits for the recovery -
    read here for the HID devices, the tablet and the mouse `m1` on XC6's
    controller, by the probe below; which request each waits for is
    inferred, not traced - the power manager sends the controller its
    system IRP only after its children's, and the controller's device IRP
    only after that. A
    thread let go earlier would have run the recovery, which alone ends
    those waits, before any power IRP of the controller came. A probe on
    the same final image with those devices taken off (hub A alone at
    QEMU port 4 and the target behind it; no tablet, no `m1`, no stick;
    one reading, `E:\xhci98-scratch\w50\devfault14\ev\P6`, not taken as
    the leg's evidence) reached the controller's system S4 and device D3
    inside the hold, 11 s after the request, and the controller
    suspended (`power.suspend.first`); but `qemu.inj.rv.released=00030258`
    came only after the resume. The point let go at its bound, the
    resume's D0 waited its 15 s for the held thread
    (`resume.keep.d0wait=80003A98`), and the owed reset was served by the
    resume's reinitialization (XHRQ "recov 0 calls 2"). The bit a device
    D3 raises (`HCD_LEAVE_POWER`) stands from before its
    `HcdPowerGateEnter` to after its `HcdPowerGateLeave`, the length of
    the suspend, and point 3 sampled `CmdWaitLeave` once every 100 ms, so
    the bit came and went unseen.
  - *The candidates, and why two fail.* (a) Letting point 3 go at the
    controller's `IRP_MN_QUERY_POWER`, which does come inside the hold
    (XC6's debugcon, "power minor, type, state=00030005"): the thread let
    go there runs the recovery at once, before any SET_POWER, so the
    recovery never meets the transition; and with HID devices present it
    has to, since only the recovery ends the children's waits. (b) A
    qemu-only hook in the power path that lets the point go: the same,
    wherever it sits before the controller's device D3, and at that D3
    it is the route bit already there. Anything that made the
    controller's D3 come first with HID devices present - completing
    cancelled transfers on a failed controller without the recovery, or
    the driver requesting a power IRP of its own - would stand in for the
    driver's path or have the driver send itself a request, and is not
    taken. (c), taken: the release stays the route's bit, made
    unmissable, and the leg leaves out every device that holds a class
    driver's transfer.
  - *The rule* (qemu flavour only). `HcdEnumLeaveRaise`, in the
    controller-lock hold in which it raises a route's bit in
    `CmdWaitLeave`, also ORs that bit into `InjRvLeave`, a qemu-only field
    of the controller. `HcdInjRendezvous`, when point 3 is reached,
    clears `InjRvLeave` under the controller lock, and at each 100 ms
    poll reads `CmdWaitLeave` and `InjRvLeave` under that lock; a bit in
    either lets the thread go. A new note, `qemu.inj.rv.route` (the two
    words OR'ed), is then written before the existing
    `qemu.inj.rv.released`. A bit raised and cleared between two polls is
    seen at the next one; one raised before the point was reached and
    cleared before it counts no more than it does today. `HCD_LEAVE_QTEST`
    (section E), set without `HcdEnumLeaveRaise`, still counts while it
    stands. Points 1 and 2, the `23`/0 release, the 60 s bound and the
    existing notes, `qemu.inj.rv.reached` and `qemu.inj.rv.released`,
    with their formats and the debugcon line, are unchanged. The
    decision is pure: `XhciInjRvRouteLets(point, standing, latched)`, 1
    only for point 3 with a bit in either word.
  - *What the release then meets.* For a power transition the route is
    the device D3's, which raises its bit just before its
    `HcdPowerGateEnter` and takes the gate at once, the held thread
    holding none. Polling orders nothing: the point lets go at the first
    poll that sees the bit, and when that poll falls after the D3 has
    taken the gate - the expected case, the interval before it being a
    few instructions - `hcdRecover` waits for the gate, re-reads the
    powered state under it and the controller lock, finds the controller
    suspended, and leaves the recovery requested and uncharged
    (`hcdRecover`'s admission, task 26-A.2's powered-state check). When
    the poll falls inside that interval the recovery can take the gate
    first and runs before the suspend, as it would with no point; the leg
    reads which came first. Because the controller is latched failed, its
    suspend saves no state on any controller (`xhciSaveState` declines a
    failed controller; P6's debugcon, "save: declined - the controller is
    latched failed"), so the resume's D0 reinitializes - an HCRST, which
    clears `ControllerFailed` - and the request then finds nothing to
    recover. This arrangement therefore always takes the
    reinitialization; a recovery owed across a successful Restore State
    would need a different arrangement, and the restore stays on section
    4.15.10's list. A shutdown's D3 is the same route, and the machine
    powers off with the recovery owed.
  - *Lock order.* The controller lock alone, as `HcdEnumLeaveRaise`
    takes it today; the injection lock is not taken.
  - *Code.* `src\hcd.h` (`InjRvLeave`); `src\hcd_enum.c`
    (`HcdEnumLeaveRaise`, the OR under `XHCI_FLAVOUR_QEMU`);
    `src\hcd_inj.c` (`HcdInjRendezvous`); `src\xhci_inj.h` and
    `src\xhci_inj.c` (`XhciInjRvRouteLets`, `23`'s text); `test\test_inj.c`
    (vectors: point 3 lets go for a standing bit and for a latched one,
    not for neither, and points 1 and 2 never); `build-and-test.md`'s `23`
    row. Nothing in the release flavour, no new import, no registry
    value.
  - *The legs (emulation).*
    1. Leg 6 on XP SP3 (blind, hibernation): as 4 - `2D` `8F` forming
       the debt, `24` port byte `0x88` `91` - with `23` point 3 armed, on
       a guest whose controller carries hub A and the target alone: the
       settlement failure (`cmd.left.settled=09...`, `ctrl.failed.why`),
       the point reached, the hibernation requested at once; the
       controller's S4 and D3 inside the hold; `qemu.inj.rv.route` with
       `10` and `qemu.inj.rv.released` well under 600 polls; no
       `ctrl.recover.begin` before the D3's suspend
       (`power.suspend.first`), so the recovery's admission came after the
       suspension; after the resume its reinitialization, no recovery for
       that request (XHRQ "recov" unchanged) and the devices enumerated.
       Should the recovery win the gate instead, the leg reads
       `ctrl.recover.begin` before `power.suspend.first` and records it.
    2. Leg 6 on 2000 SP4 (two processors), with the power button in
       place of the hibernation: the point let go at the shutdown's D3 by
       its bit, no recovery begun before the machine is off.
    3. Leg 5 attempted once with the same arrangement on XP, without
       point 3, the hibernation requested at section E's trigger: the S4
       IRP's bracket waits for the exercise's end, the settlement then
       runs, and the D3 that follows the S4 may come while the
       settlement's Disable Slot is withheld. Recorded whichever way it
       falls.
  - *What the leg shows, and what it does not.* It shows the driver's
    own path: a recovery the ordinary settlement failure left owed meets
    a power transition that came after the request and before the
    recovery began, finds the controller suspended under the power gate
    and defers, and (XP) the resume's reinitialization carries the
    failure's reset; (2000) the shutdown's D3 met the same way. It does
    not show a sleep with a device whose class driver holds transfers:
    there the children wait for the recovery, which runs first (XC6's
    reading, the driver's behaviour, unchanged). Nor the recovery winning
    the gate against a D3 that has raised its bit but not yet entered
    the gate, an interval of a few instructions, unless a reading falls
    there; nor a recovery owed across a successful Restore State, which a
    failed controller's suspend never saves; nor timing a real controller would give, since the point
    widens the interval by holding the thread, with no lock and no gate,
    as before; nor real hardware.
  - *As built* (revision 35). As designed, in the qemu flavour alone:
    `InjRvLeave` in the controller's injection fields (`src\hcd.h`);
    `HcdEnumLeaveRaise` ORs the bit into it inside its controller-lock
    hold, under `XHCI_FLAVOUR_QEMU`, after its `IsolationApplies` return
    (`src\hcd_enum.c`); `HcdInjRendezvous` (`src\hcd_inj.c`), for point
    3 alone, clears it under the controller lock before
    `qemu.inj.rv.reached` and, at each poll after the key's read, reads
    `CmdWaitLeave` and `InjRvLeave` in one hold of that lock and lets go
    when `XhciInjRvRouteLets` (`src\xhci_inj.c`) says so, noting
    `qemu.inj.rv.route` first; points 1 and 2 take no lock and read no
    route, as before. The latch is written in every qemu-flavour guest a
    route is raised in, and read only by a held point 3. Host vectors
    (`test_inj`'s `test_rv_route`): point 3 held with no bit, let go for a
    power bit standing and for one latched alone, for a removal's and for
    `HCD_LEAVE_QTEST` while it stands; points 1 and 2, and no point,
    never. On the image of `ea5eadc5a` (`runs/run-50.md`, "50.30 step 3
    (device isolation): C's leg 6 redesigned"): leg 6 was read on XP as
    designed - the point let go at the D3 by `HCD_LEAVE_POWER`
    (`qemu.inj.rv.route=00000010`, 35 polls), no recovery began, and the
    resume's reinitialization served the reset (XHRQ "recov 0"). On 2000
    the shutdown's power IRPs came inside the hold and no recovery began
    before the machine was off, but the release itself is not visible
    there, since the note ring is not flushed at the power-off. Leg 5 was
    not reached: the S4 IRP's bracket waited for the exercise's end, and
    the D3 then took the power gate before the thread began the
    settlement; the left command's debt was carried across the suspend
    and cleared by the resume's reinitialization (`cmd.left.hcrst=00000100`),
    section 4.15.3's reinitialization branch read in a guest.

**D. F2: the emulator's transition, and the two path checks reconciled
(revised in revision 26).** The hub port words were captured on 2000
(`runs/run-50.md`, "F2's hub port status and change words", a single
reading of each case): for a fresh device the reset that enables the
port reads "status 0x103, changed 0x12" - C_PORT_ENABLE raised with
C_PORT_RESET - while the reset of a port already enabled reads "changed
0x10". USB 2.0 section 11.24.2.7.2.2 does not set C_PORT_ENABLE for a
reset that enables a port: it is set when an error disables the port
(and, optionally, when a reset leaves the port disabled for an invalid
idle state). So this transition is the emulator's, not one the
specification permits. That does not make the driver's reading of the
bit safe on real hubs: C_PORT_ENABLE stays set until it is cleared or
the port loses power, so a real hub can keep an earlier error's change
across a later reset that enables the port, and a port that reads
connected and enabled with C_PORT_ENABLE set is not, on any hub, proof
that its device has gone. The two path checks disagree on exactly that:
`HcdHubPathPresent` (`src\hcd_hub.c`) takes a connection or an enable
change as a confirmed departure, and the serial deferral's
`hcdSerialDeferGone` (`src\hcd_enum.c`) takes the connection change
alone.

- **The rule.** One predicate of a hub port's absence, shared by both
  checks: a hub port reading not connected, not enabled, or with
  C_PORT_CONNECTION set, or a hub on the path that is departing
  (`Draining`, or no device), establishes that the device has gone;
  C_PORT_ENABLE alone, on a port reading connected and enabled, does
  not. The predicate is a pure function of the port's status and change
  (`XhciHubPortGone`, `src\xhci_hub.c`) that both call for each hub port
  on the path. Each caller keeps what it does when a hub does not answer
  - `HcdHubPathPresent` takes an unanswered hub as confirming nothing,
  the deferral takes it as a reason to give up - and everything else of
  its own.
- **What stays.** The root port part of both checks (CCS, PED, CSC, an
  unreadable PORTSC); `HcdHubPathPresent`'s stopping at a failed
  controller or a tainted scratch; no change bit cleared by either; a
  port disabled by an error still reads not enabled and is still a
  departure; a genuine connection change is still a departure; the hub
  service's own handling of C_PORT_ENABLE (`XhciHubPortDecide`: a port
  disabled with its change raised is a disconnect and a reconnect); the
  restore check of `hcdCfgResetPort` (`src\hcd_cfg.c`), which takes any
  connection or enable change as "changed meanwhile" and cycles the
  device - a conservative outcome, not a departure verdict, left as it
  is; and `HcdHubPortReset` clearing C_PORT_RESET alone (the change is
  left for the port's own look, and is no longer read as a departure).
- **The legs (emulation) and vectors.** The F2 arrangement again
  (`25` port byte `0x88` persistent, a fresh `usb-mouse` at hub port 4.2,
  the `usb_hub_*` trace on) on 2000 and 98 SE: the same words as the
  capture, and now two attempts, "enumeration failed, port/cause=
  01010008" (`UNANSWERED`), `tol.loc.hold=00010104`, the hub and its
  other device up, one replug releasing it. The published device's
  cycle (F2B) unchanged: held. A device that does leave during the
  withheld transfer (`device_del` of the 4.2 mouse while `25` holds its
  transfer): still read as departed (cause 4, no hold). The serial
  deferral's legs of task 45.40 (record 13 section 10.7) unchanged. Host
  vectors in `test_hub` for `XhciHubPortGone`: each of not connected,
  not enabled, connection change, and enable change alone on a connected
  and enabled port.
- **Owner question kept.** The step-3 evidence for section 4.15.5's
  hold of a fresh device behind a hub becomes reachable with this rule
  (the leg above). If the owner prefers to leave the two checks as they
  are, the fresh-child hold stays unshown in this emulator and the
  difference stays unresolved on real hubs; that choice is the owner's.

**E. The query-stop under a debt, a driver-path exercise of the qemu
flavour (owner, 2026-11-18; revision 28, revised in revision 31 before
code).** Mode 4's query-stop is the one route that ends a command wait
while every other device of the controller stays present
(`HcdMsg4QueryStop`, `src\hcd_msg4.c`, raises `HCD_LEAVE_QSTOP` before it
releases the message; a released query keeps the bit until its cancel),
so it is the one route that can show a refused and a released query-stop
under a debt, and another device's RESET_PIPE or selective-suspend Stop
refused under it with its peers kept. Nothing reachable from user mode in
these guests sends `IRP_MN_QUERY_STOP_DEVICE`. The owner's decision: drive
it with a hook of the qemu flavour in the driver, not an external test
client; and, by a decision of 2026-11-19 taken on the owner's behalf, the
hook sends the driver no PnP request of its own but exercises the
driver's own query and cancel code directly.

- **No IRP, no work item.** Revision 28's work item sent
  `IRP_MN_QUERY_STOP_DEVICE` and `IRP_MN_CANCEL_STOP_DEVICE` to the
  controller's own FDO. Those minors are the PnP manager's alone: its own
  state of the devnode would not follow the driver's `PnpState`,
  `PnpStateBeforeQuery` and message ownership; a real query, removal or
  power transition could overlap the made one; an upper filter would be
  passed by while a lower one and `pci.sys` saw an unsolicited
  transaction; and a query this FDO refused would not go down while its
  cancel would. That is withdrawn, and so is the work item, whose
  callback no event can prove has left the image. The exercise is the
  controller thread's own: it calls the driver's query and cancel
  operations directly, `HcdMsg4QueryStop` and `HcdMsg4CancelStop` (the
  two functions `hcd_pnp.c`'s `IRP_MN_QUERY_STOP_DEVICE` and
  `IRP_MN_CANCEL_STOP_DEVICE` branches call, written for PASSIVE_LEVEL with
  no lock held and reading nothing of the IRP; the message library they
  call already serves power work threads), at a point of the thread's
  loop that holds no lock and no gate, beside `HcdMsg4Look`, which takes
  the power gate there in the same way. It never touches `PnpState` or
  `PnpStateBeforeQuery`, forwards nothing and completes nothing, so
  nothing below the FDO and nothing of the PnP manager sees it. Every
  reading is a driver-path exercise (the query's and the cancel's own
  code, the route's bit, the debt and its settlement), never a PnP-manager
  rebalance; what a real rebalance adds - the PnP manager's state, the
  lower stack's answers, a lower driver failing the query - is not shown
  and stays owed to a real rebalance. The thread is joined by every stop
  (`hcdStopBody`, a failed start's included), so nothing of the exercise
  outlives the controller's lifetime.
- **The code.** `XhciQemuInject` code `2D` (`XHCI_INJ_QUERY_STOP`), clear
  of every branch's codes (`2C` is the port switchover's; `30` to `35`
  are reserved for the retry work). The argument: bits 3:0 the hold in
  seconds between the query and its cancel (0: the cancel at the next
  turn of the thread), bits 6:4 the delay in 100 ms units between the
  trigger and the query, bit 7 the test window below.
- **The exercise's state and its one lock order.** In the injection
  state: `State` (IDLE, ARMED, TRIGGERED, CALLING, QUERIED), `Abort`,
  `Terminal`, `Released` (the query returned success with the message
  released), `Due`, `Busy` (the count of real requests inside their
  bracket, below), `Closed`, the withheld command's slot and TRB address,
  and `Idle`, a notification event set exactly while `State` is IDLE.
  Every read and change of them that decides anything is made holding
  the controller lock and the injection lock inside it, the layer's
  established order; nothing takes them the other way.
- **Arming and trigger.** Firing `2D` (the thread's injection poll)
  arms it only when the controller is started, in interrupt mode 4 on NT
  5 (`Msg4System` NT 5, `Msg4Info` set), with isolation applying (a route
  bit is raised only there, `HcdEnumLeaveRaise`), `State` IDLE, `Busy` 0
  and not `Closed`; otherwise `qemu.inj.qstop.refused` (the reason) and
  nothing is armed. The next withholding of a command by the hang faults
  (`24` to `2B`, `HcdInjCmdDoorbell`, DISPATCH_LEVEL under the submit's
  controller lock) takes the arming in the same hold, one shot: `State`
  TRIGGERED, `Due` the delay from now on the tolerance clock, the
  withheld command's slot and address kept, `Idle` cleared. With bit 7 the
  same hold raises the qemu-only route bit `HCD_LEAVE_QTEST` (`0x80`,
  `src\hcd.h`) in `CmdWaitLeave` and signals `CmdDoneEvent`, as
  `HcdEnumLeaveRaise` does but without retaking the lock it holds: the
  command was withheld at that doorbell, so its completion cannot come,
  and its abort comes only from the engine's watchdog, armed for 5 s from
  the submission; under any ordinary scheduling the thread's next look
  comes first and takes the route's verdict, so the debt is formed by the
  withheld command itself. That is not atomic - a submitting thread
  delayed past the watchdog loses the race, and the waiter gives a
  matching abort precedence over a route's bit - so the query's commit
  checks the debt (below), and a debt that did not form is a run-time
  outcome, reported as an unsuccessful arrangement.
- **Each driver call is committed atomically, on the thread.** The
  thread's step (`HcdInjExercise`, PASSIVE_LEVEL, no lock held, once per
  turn of its loop) makes at most one driver call per turn. The loop's
  wait has a 100 ms timeout, but each turn also runs a whole synchronous
  service pass, which can hold the thread for its own waits (a command's
  5 s and its verdict's bound, a transfer's 5 s deadline, more than one
  of them in one pass); so the delay and the hold are minimums, the step
  runs at the first turn after them, and the legs record when it ran. A call is committed in one hold of both locks:
  for the query, `State` TRIGGERED, `Due` passed, not `Abort`, `Busy` 0,
  not `Closed`, the controller started, in D0, not suspended in D0, not
  suspended, and none of the stop, removal, power, direct or shutdown
  routes' bits standing; and with bit 7 the debt standing and naming the
  withheld command (`Left.Owed` not clear, its slot and address the kept
  ones) - a debt that did not form, or names another command, ends the
  exercise with `qemu.inj.qstop.nodebt` and no call: the arrangement is
  reported unsuccessful, never read as a query under a debt. If it holds,
  `State` becomes CALLING in that hold; then, both locks let go, the call
  is made (`qemu.inj.qstop`, its status), and in one hold again `Released`
  is recorded and `State` becomes QUERIED with the hold's end in `Due`.
  For the cancel, `State` QUERIED and either `Due` passed or `Abort` set
  by a request that is not terminal: CALLING, then `HcdMsg4CancelStop` if
  `Released` (`qemu.inj.qcancel`), then, in one hold, `HCD_LEAVE_QTEST`
  cleared, `State` IDLE and `Idle` set. A refused query needs no cancel by
  the driver's own rule (`HcdMsg4QueryStop` has reconnected and cleared
  its bit), and none is made. A call already committed is always finished
  and recorded; `Abort` only decides the next commitment, so "nothing new
  after an abort" has an exact boundary: the hold that commits.
- **Real requests, bracketed: they always meet a state the exercise
  leaves consistent.** Each real request that could overlap a driver call
  of the exercise or end what it uses enters a bracket at its dispatch's
  entry, at PASSIVE_LEVEL, holding no lock, not the door gate and not the
  power gate, and after its own outstanding-I/O reference
  (`HcdIoEnter`) where it takes one, so that a remove waiting for that
  count also waits for it: the PnP minors `IRP_MN_START_DEVICE`,
  `IRP_MN_QUERY_STOP_DEVICE`, `IRP_MN_CANCEL_STOP_DEVICE`,
  `IRP_MN_QUERY_REMOVE_DEVICE`, `IRP_MN_CANCEL_REMOVE_DEVICE`,
  `IRP_MN_STOP_DEVICE` and `IRP_MN_SURPRISE_REMOVAL` after `HcdIoEnter` in
  `HcdControllerPnp`, `IRP_MN_REMOVE_DEVICE` at the top of `hcdRemove`
  (before `HcdMsg4Gone` and everything after it); every
  `IRP_MN_SET_POWER` after
  `HcdIoEnter` in `HcdControllerPower`; and the power work item's
  direct transition toward sleep and its shutdown branch
  (`hcdPowerWork`), before each raises its route's bit. In one hold of
  both locks the bracket raises `Busy`; for `IRP_MN_STOP_DEVICE`,
  `IRP_MN_SURPRISE_REMOVAL`, `IRP_MN_REMOVE_DEVICE` and a START of a
  controller already started (which stops it, `hcd_pnp.c`) it also sets
  `Closed` and `Terminal`; and it sets `Abort`. An exercise ARMED or
  TRIGGERED is ended there by the bracket itself (`HCD_LEAVE_QTEST`
  cleared, IDLE, `Idle` set): no call was made. Otherwise, if `State` is
  CALLING or QUERIED, the bracket wakes the thread and waits for `Idle`;
  the thread finishes its committed call, and at its next turn ends a
  QUERIED exercise - by the cancel for a request that is not terminal,
  which hands the request the state of no exercise; and for a terminal
  one by a hand-off with no reconnection: `HCD_LEAVE_QTEST` cleared and
  IDLE, the released query left exactly as a real query that succeeded
  leaves it for the stop or the removal that follows it, which
  `hcdStopBody` (clearing `HCD_LEAVE_QSTOP`), `HcdMsg4Gone` and the stop's
  release of the message already take over on a real controller; no
  `HcdMsg4CancelStop` reaches hardware a surprise removal may have taken
  away. The bracket's wait holds nothing the thread needs: the thread
  takes the power gate only inside the two calls and its own passes, and
  no bracketed path holds it at entry. The wait is finite but not short:
  it lasts until the thread has finished the call it committed, if any,
  and the rest of the service pass it is in, whose waits are each bounded
  (above) but do not look at `Abort`; waking the thread shortens only an
  idle wait. That is accepted for a qemu-only exercise; each bracket that
  waited notes `qemu.inj.qstop.waited` with the time it waited, and every
  bracket notes `qemu.inj.qstop.real` (the request's kind, and the state
  it found) once its wait is over, before the request goes on. `Busy` is
  lowered where the driver's own handling of the request ends, on every
  path: for a PnP minor at the end of its handling, before its
  `HcdIoLeave`; for a device `IRP_MN_SET_POWER` to D0 where its finish
  completes it; for a system `IRP_MN_SET_POWER` at each of its ends - its
  completion, the power work item's completion of it, and the completion
  routine's return that passes it on (`hcdSystemDone`); for a device IRP
  to D1 to D3 at its dispatch's return, after it has forwarded the IRP,
  since nothing of the driver's runs for it afterwards and the controller
  then reads suspended, which no commit admits; and for the power work
  item's branches when each returns. A nested request (a device IRP the system
  IRP asked for, the work item it queued) enters its own bracket and
  waits only for `Idle`, never for `Busy` to fall, so it cannot wait for
  the request that encloses it. `IRP_MN_REMOVE_DEVICE` never lowers it,
  since the extension is gone after it. A start (`HcdInjStart`) sets the
  exercise IDLE and clears `Closed` before the thread starts, and leaves
  `Busy` as it is: the START's own bracket is inside it, so no arming is
  admitted until the START has returned, and a failed start has stopped
  the thread again, with nothing armed.
- **The refused query's debt window: a route bit of the test's own.**
  `HcdMsg4QueryStop` raises `HCD_LEAVE_QSTOP` and, refusing, clears it
  before it returns, so a debt it caused would be settled at the next
  pass, before ordinary configuration work, and a peer's request made
  after the refusal would find none; holding the thread would not help,
  since the thread is what serves the peer. With bit 7 the debt is held
  by `HCD_LEAVE_QTEST` from the trigger to the end of the exercise. A
  route's bit keeps the settlement from running (`XhciLeftDebtPass`,
  `XhciLeftDebtStep`: no pass while `routes` is not 0) and ends a command
  wait, and blocks nothing else: transfers, the hub service and ordinary
  configuration and suspend service go on, so a peer's request in the
  hold reaches admission, is refused before submission and marked
  (`cmd.left.admit`), and is served once the bit is cleared and the
  settlement has run - failed alone (`XhciTolMarkedFails`) or its owed
  reset delivered once. Under the debt no command but the settlement's is
  admitted, so no other command wait meets the bit. The one other reader
  of a nonzero `CmdWaitLeave` is rendezvous point 3 of code `23`, which it
  releases; the legs here do not arm that point. Without bit 7 no debt is
  formed by the exercise at all: the query is made by the controller
  thread, which can reach its step only after the withheld command's
  wait has ended (the watchdog's abort, in the arrangement below), so
  `HCD_LEAVE_QSTOP` interrupts no wait. The exercise therefore never shows
  the query-stop ending a command wait; that verdict is the route bit's
  in the wait, the same for every route, and is what bit 7's
  `HCD_LEAVE_QTEST` takes. Bit 7 clear is kept for the query's and the
  cancel's own paths with no debt.
- **Imports and builds.** Everything under `XHCI_FLAVOUR_QEMU`, compiled
  into no release build, the brackets included (the release image's
  import and string checks show it as for the rest of the layer). The
  kernel's event and wait routines are already imported; no row is
  added. `build-and-test.md`'s `XhciQemuInject` table gains the `2D`
  row; no new registry value is read.
- **The peers.** The suspend-Stop peer is the existing selective-suspend
  test client (`test\pmclient`, `PMCTL D2` on its tablet); the RESET_PIPE
  peer is the stick's own class driver after code `14` (a lost bulk
  completion), whose timeout sends ABORT_PIPE and RESET_PIPE inside the
  hold. No new client is built.
- **Code.** `src\xhci_inj.h` and `src\xhci_inj.c` (the code, its argument,
  and as pure functions the arming's refusals, the commit decisions for
  the query and the cancel, the bracket's decision - end it here, wait, or
  nothing - and the terminal or cancelling end); `src\hcd_inj.c` (the
  arming, the trigger in `HcdInjCmdDoorbell`, `HcdInjExercise`,
  `HcdInjRealEnter`, `HcdInjRealLeave`); `src\hcd.h` (`HCD_LEAVE_QTEST`,
  qemu flavour only); `src\hcd_ctl.c` (`HcdInjExercise` in the thread's
  loop beside `HcdMsg4Look`); `src\hcd_pnp.c` and `src\hcd_power.c` (the
  brackets, qemu flavour only). Host vectors in `test_inj`: the decoding;
  each refusal of the arming; the trigger's one shot; each condition of
  each commit; the debt check with bit 7; the bracket's three answers for
  each state; a terminal request's hand-off with no cancel and a
  non-terminal one's cancel; `Busy` blocking a commit and not a nested
  bracket.
- **What stays.** `HcdMsg4QueryStop`, `HcdMsg4CancelStop` and the query's
  release and refusal rules; `hcd_pnp.c`'s and `hcd_power.c`'s handling of
  every real request; the debt, its admission and its settlement; the
  other codes; the release build.
- **The legs (emulation; XP SP3 x86, `XhciInterruptMode` 4).** The
  arrangement prepared for this case: every class driver enabled (the
  exercise removes nothing); a target mouse plugged at a hub port with
  `24` 0x88 persistent outcome 1; `2D` armed before the plug. Legs 1 and
  2 read, beside their own lines, which peer operation actually ran (the
  test client's `PMCTL` line, or the stick's ABORT_PIPE and RESET_PIPE
  after `14`), that the peer's command was refused before submission
  (`cmd.left.admit` for the peer's slot, and no TRB of it in QEMU's
  `usb_xhci` command trace), one reconciliation (`XhciTolMarkedFails`
  once, or the owed reset once), and every other device present
  throughout; all of them are reported as a driver-path exercise.
  1. Refused, bit 7 (`XhciQemuMsg4Fail` 4): `cmd.left` naming the
     withheld command with route bits `80`, the debt's records, then
     `qemu.inj.qstop` the refusal's status and `msg4.querystop` likewise;
     inside the hold the peer's request refused and marked; no
     `qemu.inj.qcancel`; after the hold the settlement and the peer's one
     reconciliation.
  2. Released, bit 7, a hold of some seconds: `cmd.left` naming the
     withheld command with route bits `80`, the query released under the
     debt (`msg4.querystop=00000000`), the peer's refusal inside the hold,
     then the cancel's reconnect (`qemu.inj.qcancel`, `msg4.cancelstop`,
     `msg4.noop=FFFFFFFE`), the bit cleared and the settlement. Bit 7
     clear once, for the query's and the cancel's paths alone: no debt.
  3. The brackets, each during a released query's hold: a Device Manager
     disable (on XP a query-remove, which is not terminal: the cancel
     made before the query-remove goes on - `qemu.inj.qcancel`, then the
     bracket's `qemu.inj.qstop.real`; the controller's `pnp.hc` note of
     the minor is written at the dispatch's top, before the bracket, and
     so comes before both), and then the remove with nothing
     active; a hibernation (the system set-power IRP's bracket ends the
     hold by the cancel before the system IRP goes on; its device IRP
     finds the exercise IDLE); and a stop with an exercise active, if a
     stop can be had on the guest (on XP a resource rebalance is not
     reachable, so the stop's and the surprise removal's hand-off rest
     on their host vectors, said so). In each the bracket's
     `qemu.inj.qstop.real` and every note of the request's handling after
     it come after the exercise's last, and the exercise makes no call
     after its `Idle` is set.
  4. The delay-to-query closure: a Device Manager disable inside the
     delay - the exercise ended by the bracket with no call
     (`qemu.inj.qstop` absent).
  5. The refusals: interrupt mode 0 and an exercise already active
     (`qemu.inj.qstop.refused`). Bit 7 with no debt formed
     (`qemu.inj.qstop.nodebt`) is a run-time outcome of a lost race, not
     arranged on purpose: if a leg meets it, it is recorded as an
     unsuccessful arrangement and repeated; its decision has its host
     vector.
- **As built** (revision 32). As designed, in the qemu flavour alone.
  `src\xhci_inj.c` holds the decisions as pure functions over
  `XHCI_INJ_QSTOP` (`XhciInjQsArmRefusal`, `XhciInjQsArm`,
  `XhciInjQsTrigger`, `XhciInjQsDebtFits`, `XhciInjQsStep`,
  `XhciInjQsQueried`, `XhciInjQsEnded`, `XhciInjQsBracket`,
  `XhciInjQsLeave`, `XhciInjQsStart`), with the host vectors listed above
  in `test_inj`. `src\hcd_inj.c` arms (`qemu.inj.qstop.armed`, or
  `qemu.inj.qstop.refused` with the reason, 1 to 6 in the order the
  arming's conditions are listed), triggers in `HcdInjCmdDoorbell` on any
  command the hang faults withhold there, the recovery command of section
  C included (`qemu.inj.qstop.trigger`: slot, TRB type, argument), and
  runs `HcdInjExercise`, `HcdInjRealEnter` and `HcdInjRealLeave`.
  `XhciInjTake` and `XhciInjNeedsLive` name `2D` on its own, so
  `XHCI_INJ_LAST_BUILT` stays `2B` and `2C` stays unknown on this branch.
  Five details the design left open: `Idle` is cleared at the arming as
  well as at the trigger, so it is set exactly while the exercise is
  IDLE; every end writes its `qemu.inj.qstop.end` note in the hold of the
  controller lock that makes the exercise IDLE, so a request entering its
  bracket after, or waiting in it, notes after the exercise's last; a
  start also clears `HCD_LEAVE_QTEST`, so no earlier lifetime's
  bit survives it; `FF` does not end an exercise (its hold is at most
  15 s, and every real request ends it); and a triggered exercise found
  aborted by the thread - which the bracket that sets `Abort` never
  leaves - ends with no call. `Released` is the query's success with
  `Msg4QueryReleased` set. The commit's readiness is `ControllerStarted`
  with the extension's STARTED flag, `PowerDeviceD0`, not
  `SuspendedInD0`, the SUSPENDED flag clear, and none of the stop,
  removal, power, direct and shutdown bits in `CmdWaitLeave`. The notes:
  `qemu.inj.qstop` (the query's status), `qemu.inj.qstop.nodebt` (the
  debt's state, its slot and the withheld slot), `qemu.inj.qcancel` (the
  lost reason after the reconnect, 0 none), `qemu.inj.qstop.end` (how: 1
  the cancel or none owed, 2 the hand-off, 3 no debt, 4 aborted, 5 by the
  bracket; and whether the query released), `qemu.inj.qstop.waited` (ms)
  and `qemu.inj.qstop.real` (the kind - the PnP minor, `40` plus the
  device state, `50` plus the system state, `60` the direct transition,
  `61` the shutdown branch - the state found and the answer: 0 nothing, 1
  ended there, 2 waited); the trigger, the query, the cancel, every end
  and each bracket that ends or waits for an exercise also write a
  debugcon line at once. Its legs were read on XP on the final image of
  `b80630837` (`runs/run-50.md`, "50.30 step 3 (device isolation): E, and B, C and F on the final image"): legs 1, 2 (with
  the stick's class driver as the peer, its Stop Endpoint refused before
  submission and its owed reset delivered once after the clean
  settlement), 2 with bit 7 clear, 4 (with bit 7 clear the thread is held
  in the withheld command's wait, so the exercise stays triggered there),
  5, and 3's query-remove. Not reached: a system SET_POWER waiting for a
  queried exercise and ending it by the cancel - XP's hibernation reached
  the controller's IRP 11 to 27 s after its request, outside the 15 s
  hold; its S4, D3, S0 and D0 IRPs were each read finding nothing active
  or ending an armed exercise. The stop's and the surprise removal's
  hand-off rest on their host vectors, as said above.

**F. A frozen device's cancel (revision 28, written before code).** In
the arrangement legs a resend answered 25 froze the device, and the
cancel service then still served the next cancel on that endpoint before
the teardown (`cfg.deq.verify`, `cfg.deq.giveup`, and reads that placed
the endpoint's other streams; no command issued, no reset; two readings
of two, emulation). `HcdCfgCancelService` (`src\hcd_cfg.c`) skips a
device that is `Gone`, but not a frozen one. Section 4.15.5a's rule - a
frozen device takes no further work of any kind until its teardown - is
the right one: its rings and contexts may still be the controller's
until the teardown's own Stop Endpoints and Disable Slot, and the
teardown completes every request the device holds.

- **The rule.** `HcdCfgCancelService` skips a frozen device
  (`HCD_DEV_FROZEN`) as it skips a departed one: neither its
  `AbortAll` nor any pipe's or stream's cancel or drain is served; its
  marks stay on its pipes, and its teardown completes the cancelled
  requests with the rest (the device's removal status), as it does for a
  departed device.
- **What stays.** Every cancel of a device that is not frozen; the
  teardown and its completion of every request; the frozen device's
  admission rule.
- **The legs (emulation).** The read-back resend arrangement again (the resend
  answered 25, then a cancel on the same endpoint): no `cfg.deq.verify`
  or `cfg.deq.giveup` for the frozen device, the cancelled request
  completed by the teardown, the device removed alone, no reset.
- **As built** (revision 30). `HcdCfgCancelService` (`src\hcd_cfg.c`)
  skips a device that `HCD_DEV_FROZEN` names, read under the controller
  lock (`hcdCfgFrozen`), at the device's turn, and reads it again after
  each abort of the PDO's every pipe, each pipe's cancel or drain and
  each stream's: once it reads frozen, nothing more of that device is
  served in this pass - its later pipes and streams are not visited, and
  an abort of every pipe that it froze inside leaves `AbortAll` set again
  - and the device's teardown completes what it holds. No pure decision
  was factored out of the walk; the leg above, with a device frozen in
  the middle of the service, is its evidence. On the final image of
  `b80630837` the read-back resend arrangement was read once on XP
  (`runs/run-50.md`, "50.30 step 3 (device isolation): E, and B, C and F
  on the final image"): the resend answered 25 froze the device
  (`dev.fail.alone`), and no `cfg.deq.verify` or `cfg.deq.giveup` followed
  for it - the two readings before this change showed both - before its
  cycle re-enumerated it, with no reset. Which of its later pipes or
  streams a service skipped is not visible in the notes, and the
  teardown's completion of the cancelled request was not observed; both
  stay owed.

**G. Leg 13's outcome (revision 28).** Leg 13 of section 4.15.9 arms `26`
by behaviour with outcome 5, as the field report's sequence showed (no
stop event). With step 3 in, outcome 5 cannot meet the step's
expectation that the second device never stops, and does not by design:
a stuck abort keeps the mandatory reset (section 4.15.5a, a mark never
suppresses a mandatory reset), and the persistent `26` meets every
recovery's re-enumeration, so the controller latches, as at today's
handling. So the leg is read twice: with outcome 5, the field's
sequence and the latch at every value (expected, by design); and, as
step 3's acceptance, with outcome 1 (the clean abort): the device fails
alone, its place is held, the second device never stops. Section
4.15.9's leg 13 says so.

**Order.** A is built (revision 25's text, cleared for code). B, C and
D were written before code and built only after their review: D first
(one predicate and its callers, `1904a3c0a`), then B (`23ceaad05`, its
obligation made pure in revision 29), then C and F together
(`0667926bf`, revision 30). E, revised in revision 31, is built only
after that revision's review; it is the qemu flavour's alone, and was
built so in revision 32.

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
| Slow reset (task 49.35, `1B` hex) | every HCRST the driver writes, while armed: the reads its waits make (`XhciWaitForBits`, the recovery's pre-check, the thread's settle, the adopted reset's check) answer HCRST set in USBCMD (port byte 0) or CNR set in USBSTS (1) for `arg` x 10 ms, 255 for ever; QEMU's own reset has completed at once | the recovery's stall, then the settle at PASSIVE_LEVEL and the adopted reset (section 4.6.1) |
| Host Controller Event (task 49.35, `1C` hex) | one injected Host Controller Event, completion code `arg`, handed to the drain | the real escalation: `evt.hc`, the request, the recovery |

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
| Slow reset, armed with a Host Controller Event or HCH to start a recovery | 50 ms: each recovery waits the reset out once and recovers | 1.5 s or for ever: three charged failures at least the gap apart, the controller latched failed |

And beside them: a cancel, ABORT_PIPE, RESET_PIPE, SYNC_RESET_PIPE and
RESET_PORT each pending at each of the soft retry's two decisions; a cycle
requested while a cycle, a RESET_PORT or the location's PED reconnect is
already running; a stream endpoint; teardown and a power transition during
each; and every budget exhausted.

**As built, first part (35-T.9, `8d27a13`, revised in `cade36c` and
`b0dd28c`; revision 9).** The faults whose driver
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

**As built, second part (35-T.9, `5044966`, revised in `6808ff9`;
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
  it, as the text says. For a TD in the ring's last usable slot "the next
  TRB" is the Link TRB, which QEMU follows only when it fetches beyond it
  (`XhciInjAfterPA`); read as the ring's next TD slot, index 0, that case
  was abandoned, the stop having already dropped the fetched TD, and the
  TD was stranded behind QEMU's dequeue for good (35-V, defect candidate
  1).
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

**The trigger reader (task 50.38, revision 40).** Until then the
controller thread read `XhciQemuInject` itself, at the first pass at
least 10 tolerance ticks after its previous read (`HcdInjPoll`), and a
held rendezvous (fault 23) read the machine-wide key every 100 ms on the
same thread. On 98 SE that read was caught twice, live, never returning
(`hcdThread` -> `hcdPoll` -> `HcdInjPoll` -> `HcdCtlQemuReadDword` ->
`hcdReadDword` -> the machine-wide key's `RtlQueryRegistryValues`), the
pass counters flat and a cancelled transfer waiting for the thread's Stop
Endpoint (roadmap-usb4 task 50.38, its "kind A"); what the read waits for
inside the kernel was not read. In the qemu flavour alone a thread of its
own, the trigger reader (`hcd_inj.c`, `hcdInjReader`), now makes that
read, with the same keys (2000 to 10 the machine-wide key alone, task
49.24; 98 SE and ME both, the machine-wide one first), on a deadline 1 s
from the previous read's start (100 ms while a rendezvous holds). A value
found that differs from the one it queued last is queued, eight deep
under `InjLock` (on overflow the oldest is dropped and counted,
`qemu.inj.reader.lost`), and the controller thread woken. The controller
thread takes every queued reading, oldest first, at each pass, through
`XhciInjTake` and `hcdInjFire` as before, so everything a command does
still happens on the controller thread; nothing is taken before the first
admission, 10 tolerance ticks after the start's latch, the gate the
thread's own first read had. A held rendezvous searches the queue for its
release at each 100 ms turn; the release and every reading before it are
removed (passed over, as the loop's own reads passed over them), and the
readings that are not the release stay queued for the pass after a hold
that ends without one. Its bound is 600 turns, whatever the reads take.

The sampling contract changed with it: a value is taken once the reader
has read it, in the order read, at the controller thread's first pass
after the reading; a value replaced before a read is not seen, as before;
a value deleted after it was read is still taken (the kits withdraw a
fault with `FF`, never by deleting the value). A leg that must know a
command was taken waits for its `qemu inject: fired` line.

The reader is made by `hcdThreadStart` after the controller thread and
joined by `hcdThreadStop` after the controller thread's join, so every
stop path that ends the controller thread ends it: on 2000 to 10 on its
thread object, the reference retried until had (record 15 section
4.5.21; the qemu flavour's `XhciQemuThreadRefReader`, machine-wide, makes
that many attempts fail, as `XhciQemuThreadRefCtl` does for the
controller thread); on 98 SE and ME on its exit event, which it sets by a
call chained into `PsTerminateSystemThread` (`HcdThreadEnd9x`, record 15
section 4.5.22, `XhciQemuThreadExitSleep` included). A read in progress
at the stop is waited for, as the stop waited for the controller thread
inside the same read before. The reader runs across power transitions and
the in-place recovery, as the controller thread's read did, and touches
no hardware. A reader that cannot be made is noted
(`qemu.inj.reader.none`) and the lifetime goes on with no trigger read.

Task 35.4's optional override is beside it in the same flavour:
Codes `24` to `26` hex (task 50.30), a command or the thread's control
transfer withheld until the abort the layer emulates, are section
4.15.9's. Codes `30` to `35` hex (task 50.31), the retry of a controller
stopped after repeated failures, are design record 38 section 17's.

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
records 35-T.9's second part and what the final review of the branch
changed.

At revision 12 (roadmap-hcd task 36.54, section 4.12): the release notes'
"Controller faults" bullet takes UAS streams into the soft retry, and
"What `2.3.0.0` changes" gains the UAS retry and the QEMU workaround, which
names the QEMU defect; `build-and-test.md`'s injection section says what
fault `16` on a stream now reads and what the abandon notes `43` and `44`
mean; `xhcisnap/README.md` decodes counter words 167 to 172. The
`README.md` already says only that a transaction error is retried. The
roadmap's 36.54 carries the legs.

At revision 13 (roadmap-hcd task 38.3, deferred as PCR.1, section
4.13): no document changes. The known limitation in the release notes,
the README and the package readme template stands as written.

At revision 14 (roadmap-hcd task 38.11, section 4.14), at implementation:
the release notes, the README and the package readme template (the value,
its "Untested ground" row); `xhcisnap/README.md` (the counter words);
`xhciqual/hardware-testing.md` (the report line);
`docs/usb-xhci-info/xhci-data-structures.md` (TD Size in 0.96's units) and
`xhci-programming.md` (init step 4's floor); record 13 section 11.7's TD
Size note and record 03's vectors; `source-files.md`; and, once record 20
(38.10) is merged, its rows that call 0.96 refused by the floor
(`XHCI_AMD_0x96_HOST`, `XHCI_NEC_HOST`'s chaining, 1B21:1042) and its
section 5.2's "HCIVERSION enters nowhere".

## 8. Decisions taken (owner, 2026-10-06)

- The work joins Phase 35, and Phase 35's release is `2.2.0.0`.
- All the behaviours land together, not one at a time after a diagnostic
  reading.
- The interval cap is gated on every AMD controller (vendor `1022`), with
  the 0/1/2 override.
- The lost-interrupt backstop is this work's; polled delivery reuses its
  peek.
- The Average TRB Length change is not made by default; it is the switch `XhciAvgTrbEsit`, written by the INFs at
  0, off.
- A code nothing claims and a halt with no TD cycle the device, rather than
  recover the endpoint in place.
- The budgets and intervals are constants, not registry values; one
  switch, `XhciTolerance`, INF-written at 1, turns the unconditional
  behaviours off together (section 4.11). The value names `XhciIntervalCap`
  and `XhciAvgTrbEsit` are confirmed.

## 9. Open for review

The values of section 4.9's constants, which stand as proposed and are
retuned in a build against the bench's and any tester's readings.

## 10. The points raised against revisions 0 to 7, and where each is answered

Against revision 0 (`60f8f64`), answered in revision 1:

| Point | Severity | Answered in |
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

Against revision 1 (`4a63a35`), answered in revision 2:

| Point | Severity | Answered in |
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

Against revision 2 (`67a3f75`), answered in revision 3:

| Point | Severity | Answered in |
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

Against revision 3 (`e8d2c0e`), answered in revision 4:

| Point | Severity | Answered in |
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

Against revision 4 (`9163f77`), answered in revision 5:

| Point | Severity | Answered in |
|---|---|---|
| 1 A filtered wall clock does not measure elapsed time | medium | 4.0: ticks of a relative kernel timer, late but never early |
| 2 Containment acted on the ungated poll's samples, across lifetimes | high | 4.6: USBSTS read by the gated step itself; the stamp cleared by any lifetime or power change |
| 3 The deferred outcome lacked the reported TRB pointer | high | 4.2: the whole event kept, replayed with interception bypassed |
| 4 The pre-PDO cycle did not meet the enumeration executor | high | 4.3: `ABANDONED_FOR_CYCLE` through the executor, continuations and retries suppressed, one complete teardown; a failed Disable Slot sets `ScratchTainted` |
| 5 `Unreadable` refusals completed as `STATUS_DEVICE_NOT_CONNECTED` | high | 4.6: new requests parked, cancellable, not refused |
| 6 The second-error injection lost coherence | medium | section 5: another real Stop and the emulated Halted state reinstated first |
| 7 Retry exhaustion had two routes | low | 4.2: never intercepted; the thread never sees a spent TD |

Against revision 5 (`74d14e8`), answered in revision 6:

| Point | Severity | Answered in |
|---|---|---|
| 1 A relative timer can expire up to one clock period early, cumulatively | medium | 4.0: each tick credited at 45 ms against the slowest PC clock, intervals counted as `ceil(T / 45) + 1` ticks |
| 2 The pre-PDO teardown left the location's machine where a CONNECT is ignored | high | 4.3: one handler sets it `EMPTY` with its slot and device cleared, once per attempt, before the CONNECT |
| 3 Parked requests' release triggers misstated; `HcdIoPark` can decline | low | 4.6: today's triggers named; the declined cases completed `STATUS_CANCELLED` or `STATUS_DELETE_PENDING` |

Against revision 6 (`cc82d37`), answered in revision 7:

| Point | Severity | Answered in |
|---|---|---|
| 1 The pre-PDO reconnect read CCS, which an external hub's port does not have | medium | 4.3: connected read as `hcdPortConnected` reads it, for either kind of location |
| 2 "About 2.2 times" stated as an upper bound | low | 4.0: no fixed upper bound; 2.2 the long-interval factor under timely DPCs |

Revision 7 (`edc7a2a`) left nothing material. Against section 4.11 as first
added the same day:

| Point | Severity | Answered in |
|---|---|---|
| 1 The off-switch had no lifecycle contract | medium | 4.11: latched per start, gated at every producer, deferred work settled by the old teardown, all tolerance state initialized explicitly, a pin kept; the 1-0-1 restart legs |

Against section 4.11 as revised, answered before `d9b39c4`:

| Point | Severity | Answered in |
|---|---|---|
| 1 A pinned stop keeps its transfers, and a start clears the flag for its new buffer | medium | 4.11: retired work told from retained work, kept state unreachable by the new lifetime, the old pinned allocation told from the new buffer |

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
| A.1 | **Windows 2000 SP4's `pci.sys` 5.00.2195.6655 at a function PDO's stop, remove and power-down** (59,312 bytes, SHA-256 `66CCD7B1...EA26`, `I386\PCI.SY_`, the same as `SP4.CAB`'s; no symbols): PnP PDO table at RVA `0x3C78`. `STOP` (`0x7A50`) calls the decode disable (`0x2FF6`: `and ...,0FFF8h` at `0x306B`, offset 4 written only if changed, `0x3093`, no read-back). `REMOVE` (`0x799E`) and `SURPRISE_REMOVAL` (`0x7C70`) do the same unless the PDO is marked not present (`+0x78` bit 0), then D3 through the PM capability (`0x24A2`, PMCSR at cap+4, then `0x23F4`: a 10 ms stall from a table at `0x3D74`, a PowerState read-back, 100 retries at 1 ms, `0xC0000186`); their status is ignored. `QUERY_REMOVE` (`0x7958`) and `QUERY_STOP` (`0x7A6E`) touch no hardware. `START` (`0x789E`) ORs 7 into the saved enables (`0x78D9`) and writes Command through `0x152C`. The D0 return (`0x1468`) rewrites the 64-byte header and restores the saved Command bits. No code tests PMCSR bit 3. The bus rescan (`0x6672`, `0x60AE`) skips a vendor id of `0xFFFF` or 0 (`0x6131` to `0x6147`). No child handler touches the parent bridge; Secondary Bus Reset is only in a bridge's own reset (`0x27BA`) | static (MSVC 6 `dumpbin` 6.00.8447 `/headers /disasm` and capstone 5.0.7 over the file expanded with 7-Zip from the owner's Windows 2000 SP4 media; nothing executed; handler names inferred, not symbols; a subagent's read, 2026-10-25, not re-read line by line by the coordinator) | `docs/contributing/design/17-controller-tolerance.md` section 4.13.5 |
| A.2 | **Windows 98 SE's `PCI.VXD` 4.10.2222 at a devnode's stop, remove and set-D3** (65,895 bytes, SHA-256 `A9268F51...303D`, `win98\BASE6.CAB`; device id `0x43`): the enumerator handler (obj4:`0x2763`) sends `CONFIG_STOP` (obj4:`0x2B2F`) and `CONFIG_REMOVE` (obj4:`0x2B08`) to `0x72DF9` with D3. It first runs `0x726E8`, which, when a log configuration is allocated, writes `Command & 0xFFF8` as a dword with Status 0 (obj4:`0x2563`). It reads back the decode bits only (`0x70443`, `and al,3`), never Bus Master Enable. Then, where a PM capability exists, it writes PMCSR (obj4:`0x2BE0`), yields 10 ms (a table at `0x73B14`) and polls PowerState up to 1000 times, returning `CR_FAILURE` on a timeout. The configuration-write primitive (obj4:`0x0C4A`) reads nothing back. D0 sets Bus Master Enable (obj4:`0x11EA`). Services: 0 `_PCI_Get_Version`, 1 `_PCI_Read_Config`, 2 `_PCI_Write_Config`, 3 `_PCI_Lock_Unlock`. No child path touches the parent bridge; Secondary Bus Reset is only in a bridge's own D0 path (obj4:`0x20C7`) | static (a Python LE loader and capstone 5.0.7 over the file extracted with 7-Zip from `Win98SE.iso`, SHA-256 `C048C31E...F584`; nothing executed; a subagent's read, 2026-10-25, not re-read line by line by the coordinator; the order of CONFIGMG's calls not read) | `docs/contributing/design/17-controller-tolerance.md` section 4.13.5 |
| A.3 | **The signature `DMAR` is in none of the files below** (a narrow static result: no other remapping mechanism, run-time-built signature, component or firmware was read): a byte search for the ACPI table signature `DMAR` (which also matches it as a 32-bit immediate) finds none in Windows 2000 SP4's eight HALs, `ntoskrnl.exe`, `ntkrnlmp.exe` (`tools\win2ksp4-extracted\`) and `acpi.sys` 5.00.2195.6xxx from `SP4.CAB` (163,120 bytes, SHA-256 `5F3683B9...3DF2`), nor in Windows 98 SE's `acpi.sys` (`win98\BASE4.CAB`, SHA-256 `3DDCACEC...A338`), `ntkern.vxd`, `PCI.VXD` and `CONFIGMG.VXD` (`WIN98_53.CAB`, SHA-256 `568F800C...4162`); `DSDT` is found in both `acpi.sys` | static (a byte search with `grep -c -a` over files extracted with 7-Zip from the owner's media; nothing executed; 2026-10-25; 98 SE's compressed `VMM32.VXD` not searched, firmware not read) | `docs/contributing/design/17-controller-tolerance.md` section 4.13.5 |
