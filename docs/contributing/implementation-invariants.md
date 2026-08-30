# Implementation Invariants

These rules should stay true across all driver phases. They are kept small and
concrete so that no source pass bakes in an assumption that is hard to unwind
later. Each bullet opens with the rule; what follows it is the reason and the
evidence.

## Context Stride

- Read `HCCPARAMS1.CSZ` during controller initialization.
- Use `ContextSize = 64` when CSZ is 1; otherwise use `ContextSize = 32`.
- Compute every context byte offset as `context_index * ContextSize`.
- Do not confuse 32-bit DMA addressing with 32-byte xHCI contexts. They are unrelated.

## Doorbells

- Command ring doorbell: write `DB[0] = 0`.
- Endpoint doorbell: write `DB[slotId]` with target DCI in bits 7:0.
- Keep stream ID bits zero unless streams are explicitly implemented.
- EP0 target DCI is 1.

## Completion Matching

- **A Command Completion Event's TRB Pointer is the command TRB's address** (spec 6.4.2.2). Commands are single-TRB TDs, so comparing it against the address returned when the command was enqueued is exact.
- **A Transfer Event's TRB Pointer is "the address of the TRB that generated this event"** (spec Table 6-37): normally the TD's last TRB via IOC, but an earlier one when a short packet with ISP or an error occurred there (4.11.3.1). It is therefore not the TD's head, and a transfer must never be matched by comparing it against one. Resolve the pointer to a ring index, then to the TD that owns that index (`XhciRingTdBounds`), and key per-TD bookkeeping on the head.
- **An event retires a TD only when it names that TD's last TRB.** The rule is positional; the completion code does not override it.
  - 4.11.7 p.214: "Software shall not interpret an error Event as indicating that the TD that it is associated with is `complete` (i.e. ownership of all the TRBs of the TD have been relinquished by the xHC), unless the TRB Pointer field of the error Transfer Event references the last TRB of the TD." Short Packet gets an identical sentence of its own at 4.10.1.1.2 p.175.
  - The same holds for a successful intermediate event. The spec offers intermediate IOC events as a way for software to "update its Dequeue Pointer and reuse the TRBs that have been consumed by the xHC", and says those intermediate events report Success. Such an event means the controller has passed that TRB, not that it has finished the TD. Reclaiming the TD there frees TRBs the controller is still executing, and the next enqueue overwrites live work.
  - "The xHC advances" is not "software may reclaim". Short Packet and Missed Service Error both let the controller keep going by itself ("shall advance to the first TRB of the next TD or the Enqueue Pointer, whichever is encountered first", p.172), and neither transfers ownership early. p.188 states the distinction outright: the xHC "may automatically advance to the next TD", and even so, if the event "does not point to last TRB of the Isoch TD ... software will have to wait until the next IOC flag is encountered by the endpoint before it can reclaim" the TD. So a mid-TD event with either code is deferred: not retired, and not recovered either, because the endpoint is not halted, and resetting it and reprogramming a dequeue pointer mid-advance would be wrong.
  - The tail event that ends the deferral is guaranteed because this driver sets IOC on every TD's last TRB, with control transfers as the one exception.

    For a short packet "two events shall be generated ... a second for the last TRB with the IOC flag set" (p.175), and for a skipped isoch TD "the xHC shall not drop Events associated with TRBs as it attempts to resynchronize an Isoch pipe, e.g. ... if IOC = `1` in an Event Data or Normal TRB then it returns Missed Service Error" (p.201).

    On a control endpoint IOC is on the Status Stage TRB only, because the spec says so (p.430); what stands in for the guarantee there is that the xHC "shall advance to the Status Stage TD" (p.433) and that a retire jumps past the matched tail, sweeping the Setup and Data TDs with it. See "Control Transfers" below.
  - A deferred TD is not leaked if that event never arrives. Some controllers drop it: without the Contiguous Frame ID Capability an xHC "may not generate a Missed Service Error Transfer Event for every ESIT missed" (p.187), and an Event Ring full condition suppresses them too. `XhciRingRetireTd` jumps the dequeue pointer to just past the retired TD's tail rather than walking one TD at a time, so the next event that does name a tail sweeps up every deferred TD ahead of it. A caller with its own TD list must therefore complete every entry below the new dequeue index, not just the TD it matched.
  - Codes 24-25 mean software owns the command ring; codes 26-28 mean software owns the associated transfer ring (Stop Endpoint "transfer[s] ownership of all the TDs on the associated Transfer Ring to software"). These never retire, even at a tail, and neither family is valid for the other ring kind.
  - The tail event is not guaranteed in practice, and one departure follows from that. QEMU's xHC sends only the first of the two events p.175 mandates for a short packet (measured, batch 8-V.2; see `docs/usb-xhci-info/xhci-data-structures.md`, "The withheld second Short Packet Event"). Waiting was therefore a hang on every multi-TRB IN TD that ended short, which is a whole dead bulk endpoint.

    So a Short Packet on a TD that is entirely data ends the transfer without its tail, on the strength of p.210 ("the xHC shall advance to the first TRB of the next TD or the Enqueue Pointer"), which means the controller has provably finished the TD, and of p.175's own requirement that the second event repeat the first's length rather than add to it.

    Three limits keep it narrow: it is only for a TD that is entirely data, never a control transfer's Data Stage, whose Status Stage TD still has to run; the shape test lives in the transfer layer, which is the only one that can tell those apart; and the ring layer takes it through a separate entry point, `XhciRingRetireAdvancedTd`, which accepts Short Packet on an endpoint ring and nothing else. Everything else on this list is unchanged: an error, a Missed Service, an intermediate Success and a control short data stage all still defer.
  - The departure ends the transfer, but not at the event. A Transfer Event names a TRB address, not a generation, so retiring on the short event would free TRBs while the promised tail may still be unread, and once they are re-let the tail is indistinguishable from the new TD's own event. The short event therefore arms a deferral: the transfer stays queued and keeps its TRBs, and the retire happens at the end of a drain pass that observed the event ring empty.

    That is the observation, not the exit taken. The drain tests its bound before it dequeues, so a pass whose last consumed event is the `XHCI_DPC_MAX_EVENTS`-th leaves without asking the ring anything, and if that was also the controller's last event the ring is empty.

    Gating on the exit skipped the settle there, and with the ring empty IPE never re-asserts (4.17.5 p.270), so nothing brought the DPC back and the transfer was stranded for the life of the endpoint (batch 8-V.2's dead bulk IN endpoint, reintroduced by its own first fix). A bounded exit therefore takes an explicit `XhciEventRingPending` peek, and only a pass that still sees events pending declines to settle.

    A deferral is also answered by the promised tail arriving in-band (the conforming controller's whole path, on which the departure is never taken), or by a later TD's retire sweeping it, which by FIFO ordering is stronger proof the tail was never sent and must complete the transfer as the successful short read it is rather than failing it as a dropped event.

    Never retire a deferred TD from a pass that did not prove the ring empty, and never refuse to settle merely because the pass hit its bound, since that strands the transfer instead. Both halves are the safety property, and each has its own vector (`test_slot_bulk_short_packet_bounded_pass_does_not_settle` and `test_slot_bulk_short_packet_bound_and_empty_still_settles`).

    A record/ring divergence, discovered at the event or at the settle, retires nothing, completes nothing, and asks for a Stop Endpoint with a drain continuation, because the endpoint is Running and neither its queue nor its dequeue pointer may be touched until a stop has transferred ownership.
  So retirement is a decision, not a step. `XhciRingClassifyEvent` makes it and `XhciRingRetireTd` refuses to act otherwise, so the mistake cannot be made by forgetting to check. The one departure above is a second, named entry point for the same reason: widening `CanRetire` would move a decision into a layer that lacks the knowledge to make it.
  - The departure's residual hazard is narrowed, not removed. Retiring a short TD frees its TRBs while the promised tail event may still be in flight, and a Transfer Event names a TRB address, not a generation, so once those TRBs are re-let to a later TD a delayed tail is indistinguishable from that TD's own, and the owner search matches it to the live transfer, which can measure or complete it wrongly.
    What is closed: an exit with events still in the ring no longer retires anything, so a tail still in the ring is consumed and matched before any settle can happen. (That is the ring's state, not the exit's: a pass that stopped at the bound with the ring empty settles, and must, or the transfer is stranded.) FIFO ordering is what makes that true.
    What is left, stated plainly: the xHC may write the tail just after a pass has read the ring empty; the settle then retires, the completion goes out, usbport reposts from inside `UsbPortCompleteTransfer`, and the tail arrives against re-let TRBs. Nothing available to this driver closes that. Event identity would need Event Data TRBs, and this driver places none, so it is bounded rather than gone. `test_event_tail_after_the_settle_is_still_misattributed` constructs it on a tiny ring and pins the behaviour, so the boundary stays executable rather than prose; its sibling `test_event_delayed_tail_after_reuse_is_misattributed` asserts the closed half.
    Two rules preserve what protection there is: do not size a ring close to one TD, and do not add a path that posts many completions without an intervening drain. `MidTdTailsCensored` and `MidTdVerdictVoided` are diagnostics, not mitigations; they report the ambiguity after the fact. On a conforming controller they should read zero, because such a machine answers every deferral in-band and records no tail at all.
- **Slot ownership and endpoint state are two independent facts, and one event can assert both.** A halting error on a control/bulk/interrupt TD's last TRB relinquishes that TD's TRBs and leaves the endpoint halted: p.214 grants the first, and p.176 the second ("all Transfer Ring error conditions force the state of the associated endpoint to Halted and require system software intervention to recover").

  A single three-valued verdict cannot carry both; reporting only the retire loses the recovery command, reporting only the recovery strands the slots. `XHCI_TD_COMPLETION` therefore exposes `CanRetire` and `NeedsRecovery` as separate flags, computed independently. When both are set, retire first: the dequeue pointer then already sits on the next TD, which is where p.172 says to point the hardware.
- **"Needs recovery" is not "is halted", and the ring layer does not say which state.** p.176's sentence above is the general rule; TRB Error is the stated exception, and 4.8.3 p.149 puts it somewhere else: it "should cause a Running Endpoint to transition to the Error state", from which "a Set TR Dequeue Pointer Command shall be used to transition the endpoint to the Stopped state".

  Disabled, Running, Halted, Stopped and Error are five separately encoded states (Table 6-8), so the two sentences describe different destinations. `NeedsRecovery` therefore means only "software must act"; the slot layer decides with which command, and a TRB Error must not be recorded as a halt. Recording one made a non-default endpoint wait for the device-side `ClearFeature(ENDPOINT_HALT)` that a reset-pipe carries, which nothing is obliged to send for a host-side error, while every submission in the meantime was answered `USBD_STATUS_STALL_PID`.
- **Which errors halt depends on the ring, so the ring carries its kind.** `XhciRingInit` takes an `XHCI_RING_KIND_*` and refuses an unrecognised one; it is fixed at creation rather than restated per event, where two call sites could disagree about one ring.
  - `ENDPOINT` (control/bulk/interrupt): errors halt, per p.176 above, except TRB Error, which puts the endpoint in the Error state instead (4.8.3 p.149). Both need software; only the first needs a Reset Endpoint.
  - `ISOCH`: no error ever halts it. "An isoch end point never halts because there is no handshake to report a halt condition ... an isoch pipe is not halted in an error case. If an error is detected, the xHC shall continue to process the data associated with the next ESIT of the transfer" (p.177), restated at 4.10.2.8 p.184 and illustrated at p.188, where a USB Transaction Error on an Isoch IN leaves a pipe that "does not stall, but advances to the next Isoch TD in preparation for the next Interval". Errors here behave like Missed Service: defer mid-TD, retire at the tail. Resetting and repositioning one of these would disrupt a pipe the controller is still running. Bandwidth Overrun (18) and Isoch Buffer Overrun (31) are isoch-only codes and fall under this rule.
    - TRB Error is the one error code that still asks for recovery on an isoch ring (the stopped family, 26-28, reaches every transfer-ring kind and is not an error). What p.177 exempts an isoch pipe from is the Halted state, and 4.8.3 p.149 never put a TRB Error there: it names no endpoint type and sends a Running endpoint to Error, out of which only a Set TR Dequeue Pointer leads. Nothing an isoch pipe does by itself (advancing to the next ESIT, resynchronizing) leaves that state, so treating it like the other isoch errors leaves the pipe stopped for good.
  - `COMMAND`: no endpoint exists. A failed command is a result, not a fault; the ring runs on to the next command. Only codes 24-25 stop it.
- **A completion code is validated against the ring kind before it can change ownership or request recovery.** Table 6-90 assigns codes 24-25 only to Command Completion Events, 26-28 only to Transfer Events, and 18/23/31 only to isoch transfers. Ring Underrun/Overrun explicitly have no valid Transfer Event TRB pointer, while Event Ring Full is a Host Controller Event; none belongs in the TD classifier. VF Event Ring Full is instead a valid Force Event command result. `XhciRingClassifyEvent` returns `XHCI_RING_BAD_COMPLETION` for mismatches. Unknown vendor information (224-255) is interpreted as Success, and unknown vendor errors (192-223) as Undefined Error, as Table 6-90 requires.
- **An unassigned completion code retires nothing, completes nothing and records no halt.** It increments `BadCodes` and is otherwise ignored. Nothing in this driver knows what the controller did with those TRBs, so retiring them would free memory the xHC may still be reading, and manufacturing a halt would claim a hardware state no event reported.

  Ring ownership is preserved outright by retiring nothing; "completed exactly once" and "submissions stopped" are discharged by usbport's own 10 s URB timeout, armed on every `SubmitTransfer` that returned success and ending in `AbortTransfer`, from which the transfer leaves this driver's queue and the quiesce chain reclaims the TRBs. That bound is driven by a vector rather than asserted, because a test that only checks that nothing happened passes equally against a driver where nothing ever happens.
- **The two recovery routes end differently, and the difference is who is waiting.** A halted endpoint waits for usbport's reset-pipe, which arrives only once usbport's own transfer list is empty (SP4 `0x23E88`), so a transfer still on this driver's queue at that point is one nobody is waiting on and the drain completes it. A TRB Error's Error state asks for no drain at all, and the survivor keeps its place. A vector that assumed the two were symmetric was wrong about the halt route.
- **This driver sets IOC only on a TD's last TRB, and on a control endpoint only on the Status Stage TRB of the whole transfer.** That is what makes an intermediate Success event unexpected rather than routine. Do not start using intermediate IOC for ring reuse without revisiting TD-boundary derivation: advancing the dequeue pointer into the middle of a TD would break the Chain-flag walk that recovers a TD's head.
- **After a halt or a stop, the dequeue pointer software programs into Set TR Dequeue Pointer must be the one the ring reports** (`XhciRingDequeuePA`), or the software and hardware pointers diverge.

  On a halting error "the xHC shall stop on the TRB in error, the endpoint shall be halted, and software shall use a Set TR Dequeue Pointer Command to advance the Transfer Ring to the next TD" (p.172). Recovery order is Reset Endpoint, then Set TR Dequeue Pointer, then ring the doorbell (p.116); "software is responsible for cleaning up any partially completed transfers" (p.118). An endpoint in the Error state skips the first rung: a Reset Endpoint "may only be issued to endpoints in the Halted state" (4.6.8 p.118) and would answer Context State Error, so a TRB Error is recovered with the Set TR Dequeue Pointer alone.
- **That command carries a cycle bit as well as an address, and it is not a constant.** Bit 0 of the New TR Dequeue Pointer field is the Dequeue Cycle State, which "identifies the value of the xHC Consumer Cycle State (CCS) flag for the TRB referenced by the TR Dequeue Pointer" (Table 6-67 p.455; 4.6.10 p.127 in prose). A ring that has wrapped an odd number of times sits on TRBs written with the opposite cycle, so a hard-coded DCS is wrong on half of all laps, and wrong silently: too low, the controller reads a live TD as unproduced and stops at it; too high, it executes stale TRBs from the previous lap. Take it from `XhciRingDequeueCycle` and OR it into `XhciRingDequeuePA`; compute it nowhere else.
- **A dequeue pointer may only be placed at a TD boundary, and only forwards.** "The xHC shall assume that the modified Dequeue Pointer references the first TRB of a TD" (p.172). Two software-side rules follow from the dequeue pointer being what every free-space and ownership calculation is measured from: a position behind the current one resurrects reclaimed slots as outstanding, and on an empty ring any position other than the enqueue pointer invents outstanding work out of stale TRBs. Either way the free count shrinks with nothing left to retire it and the ring eventually reports full for good, with no error anywhere to say why. `XhciRingSetDequeue` accepts only the enqueue position (discarding all pending work) or an outstanding TRB that is its own TD head.
- **One TD can produce several events**: while advancing to the end of a TD the xHC generates an event for every further TRB whose IOC flag is set (4.11.3.1). A pointer to an already-retired TRB is the expected trailing case; ignore it, do not treat it as an error and do not move the dequeue pointer.
- **Three pointer values are not ring addresses at all** and must be rejected before resolution, not resolved into a plausible index:
  - `ED = 1` (Transfer Event DW3 bit 2): the parameter is "64 bits of Event Data" from an Event Data TRB (Tables 6-37, 6-39). This driver places no Event Data TRBs, so such an event is unexpected: log and discard it.
  - Zero: the xHC uses it for errors it cannot attribute to a TRB, e.g. Ring Overrun/Underrun, and "software shall treat it as invalid" (4.11.3.1).
  - An address on another ring, or the Link TRB.

## Command Ring

- One command outstanding at a time. Match every Command Completion Event against the issued TRB's physical address regardless: "The Command TRB Pointer field of the Command Completion Event shall point to the Command TRB that initiated the event" (4.6.1, p.93), so an event naming anything else is either a duplicate or a disagreement about where the ring is, and both are worth counting apart from a completion.
- Never wait for a command completion inside a usbport callback; callbacks run at DISPATCH_LEVEL under usbport's locks (`docs/usb-xhci-info/usbport-miniport-abi.md` section 7). Issue, ring `DB[0]`, return; complete from the DPC.
- Every command carries a timeout. Recovery order: CRCR.CA abort -> adopt the dequeue pointer the Command Ring Stopped event reports -> escalate to `UsbPortInvalidateController(RESET)` if CRR stays set ~5 s after CA, or if that reported pointer is one the software ring cannot hold. The full ladder, and why repositioning CRCR is not an available rung, is in `docs/usb-xhci-info/xhci-programming.md` "Command Ring Discipline".
- **Do not ring `DB[0]` between asserting CA and seeing the Command Ring Stopped event.** "If the Command doorbell is rung before CRR = `0`, (i.e. the ring is not fully stopped), then the behavior is undefined, e.g. the Command Ring may not restart" (Table 5-24 note, p.368). That needs an aborting state distinct from "a command is outstanding", because a Command Aborted event alone does not end it.
- Write the CRCR pointer field only while CRR = 0; keep a software copy of the ring pointer (the register reads back 0).
- **Write CA only while CRR = 1, and compose it from a read.** RCS, CS, CA and the pointer all read back as `0`, but CRCR 5:4 are RsvdP (Table 5-24, p.367), the only bits of the register a read can carry anything in, so a read-modify-write is still required.

  The same read decides whether to write at all, because the two halves of this register are ignored in opposite states: CA "is ignored by the xHC if Command Ring Running (CRR) = `0`" (p.367) while the pointer is latched then: "If the CRCR is written while the Command Ring is stopped (CRR = `0`), the value of this field shall be used to fetch the first Command TRB the next time the Host Controller Doorbell register is written" (p.368).

  So a bare `CRCR = CA` on a ring that has already stopped is not a harmless no-op: it repoints the command ring at physical address 0. Declining the write there loses nothing (CA would have been ignored) and the abort state machine is unchanged, so rung 2 still bounds it.
- **Controller state uses one stable driver-image lock, and it is innermost** (the whole model is in `docs/contributing/design/05-locking-model.md`; the section "Locking and Lock Order" below is its summary). Create it once in `DriverEntry`; never put it in the miniport extension, which usbport zeroes before every start.

  It serializes command state and command MMIO; the complete bounded event-DPC drain (including ERDP publication and IMAN re-arm); interrupt enable/disable; the terminal transition, which masks before publishing `ControllerFailed`; and lifecycle quiesce admission, which retires the command generation, masks, then clears `INITIALIZED` before releasing the lock for the bounded halt. DPC and `EnableInterrupts` validate admission only after acquiring it.

  Every non-ISR read-modify-write of `Flags` uses the same lock, including `RUNNING` changes before/after controller MMIO, the suspend/resume `SUSPENDED` transition, and start/stop bookkeeping; otherwise an unrelated `INTERRUPTS` update can restore stale admission bits or be lost by a quiesce write based on an older word. Resume's test-and-clear of `SUSPENDED` is one locked transition, not a read followed by a later write. A naturally aligned word prevents tearing but does not make a check followed by MMIO atomic.

  The ISR remains the stateless DIRQL exception and never takes this ordinary DISPATCH-level lock; every state that makes it decline must be published only after delivery is provably suppressed (enable confirmed clear by a read back; see "Interrupt Ordering"), or a level-triggered shared INTx can remain asserted forever.

  No usbport service or bounded wait may occur while the lock is held: decide under it, act or wait after dropping it. Closing admission freezes ERDP while R/S remains set for the bounded halt window; the controller may fill the finite event ring and leave EHB asserted, which is an accepted teardown cost because that generation is never drained again and the ring is reclaimed only after halt/BME proof or rebuilt on resume.
- **The uncancellable timeout is made safe by a generation, not by a cancel.** `UsbPortRequestAsyncCallback` exposes no cancellation and copies its context, so pass a monotonically increasing generation in that copy and claim the outstanding command only while it is still current. Allocate a new generation per command, not per start: two commands sharing one let a watchdog belonging to a command that already completed abort the next. Advance it on every stop, suspend and reinitialization, which is what retires every callback already in usbport's timer queue. A stale or post-stop callback must return before touching a register; by then the controller may be in D3.
  - Both halves of this bullet were found by mutation rather than review. Allocating the generation as `CommandGeneration` instead of `CommandGeneration + 1` failed zero checks until a vector kept the first command's copied context alive across the second submit, which is what usbport's timer queue does. The same pass deleted a command invalidation (what `xhciCommandInvalidateLocked` does) at the top of `XhciInitController` that also failed zero checks when removed: both paths into that function have already done the job (`XhciCommandInit` on the start side, `XhciQuiesceController` on every stop, suspend and resume), so those two are the choke points, and defence that cannot be distinguished from its absence is not kept.
- **A generation kept in the miniport extension cannot survive a restart, so the token that separates one start from the next must not live there.** usbport calls `RtlZeroMemory` over the whole miniport extension immediately before every `StartController`, and keeps no list of the timer callbacks it has already handed out.

  Issue a start epoch from a driver-image counter usbport does not touch, record it in the extension, and carry it in every timer context. Check only the two callback pointers before taking the stable driver-image lock; validate the extension bracket, epoch, generation, initialized state and failed state while holding it. Never hand out epoch 0, so a zeroed-but-not-yet-started extension matches nothing either. (`docs/usb-xhci-info/usbport-miniport-abi.md`, "`UsbPortRequestAsyncCallback`: what its return value is worth, and what it costs".)
- **A command that cannot be timed is not issued.** The service returns 0 on success and on allocation failure, so a miniport can check only that it exists. Do that before enqueuing anything, because a refusal before the enqueue is a command that never went out while a failure after the doorbell is a command that sits on the ring for the life of the driver. The undetectable half (a pool failure inside the service) is owed to `CheckController` noticing a command pending across many polls.
- **The command ring has its own TRB-type admission rule.** The ring layer checks capacity and Chain shape but has no opinion about type (it is the same code the transfer rings use), so the submitter must refuse anything outside the architected command range 9-23 (Table 6-91) before publishing. A Link TRB is the ring layer's to place; Vendor Defined types are legal there but mean nothing unqualified by PCI Vendor/Device, so refuse them until a phase needs one.
- **`ResetController` runs at DISPATCH_LEVEL inside one of usbport's spin locks. It cannot reinitialize anything, and it must not wait for anybody else to.** The RESET invalidation queues a DPC, and that DPC brackets the callback with `KfAcquireSpinLock`/`KfReleaseSpinLock` on both shipping builds (`docs/usb-xhci-info/usbport-miniport-abi.md`). `UsbPortWait` is `KeDelayExecutionThread`, so the init sequence sleeps; performing it there is a hang on Win98 and a Verifier bugcheck on Win2000. So the callback does only what is safe there (mask the interrupt enables, mark the controller failed, and raise a recovery request) and returns.
- **The recovery request has an owner, and naming one is the rule.** A failure path whose recovery step has no owner is not a recovery path.

  The census in the ABI doc (the two notes after the `UsbPortInvalidateController(RESET)` box) established that usbport reaches `StopController`/`StartController` only from a PnP or power transition something outside it initiates, that the reset DPC arms nothing, and that `CheckController` is a `VOID` slot from which usbport collects no verdict.

  A callback that only marked the controller failed and named a stop/start in its trace text therefore left the machine down until the next cold boot; that was the whole of Finding 3 (`docs/contributing/runs/run-13e.md`, Finding S). So every such path in this driver names the context that performs the recovery, and that context is one this driver reaches on its own.
  - The owner here is usbport's health poll (nominal 500 ms; 36-80 ms measured on the E460, see "Fatal Errors"), which arms one `UsbPortRequestAsyncCallback`; the callback runs at DISPATCH_LEVEL holding no usbport lock, and `XhciRecoverController` reinitializes there. The arming is not done in `ResetController` itself: that would nest usbport's timer machinery inside usbport's reset-DPC spin lock, two hierarchies with no stated order between them.
  - Everything the recovery runs sets `ext->InitBelowPassive`, and that flag is a contract rather than a convenience: while it is set, every bounded wait stalls instead of sleeping, `UsbPortReadWriteConfigSpace` is not called at all, and `XhciFailClosedDma` counts instead of bugchecking, because nothing on that path reclaims the common buffer, which is the premise the bugcheck rests on. A service documented PASSIVE_LEVEL-only must be skipped there, never "probably fine".
  - The retries are bounded (`XHCI_RECOVERY_MAX_ATTEMPTS`) and a controller that will not come back stays latched. That is still a terminal state; the difference is that it is a measured one, with `RecoveryAttempts`, `RecoveryFailures` and `RecoveryLastStep` saying so from a release build.
  - Count the callback's own invocations, and read them beside the recovery's: `ResetControllerCalls` rising while `RecoveryCompletions` stays at zero is a controller that latched and never came back.
- **Never write a callback body against an assumed IRQL.** Derive the calling context from the shipping binaries first (which lock, which DPC, what the caller does after the return) and record it in the ABI doc. A host suite invokes callbacks synchronously from `main()`; it has no IRQL and cannot see a sleeping operation in the wrong context, so a wrong assumption here is invisible until the target hangs.

## Locking and Lock Order

The complete derivation, the static review of every entry point, and the record
of how the root-hub, transfer and endpoint state joined the lock are in
`docs/contributing/design/05-locking-model.md`. The rules that must hold in
code:

- **One miniport lock, and it is innermost.** `xhciControllerLock`, created in
  `DriverEntry` and nowhere else, reached only through
  `XhciControllerLockAcquire`/`Release`. Order:
  `MiniportSpinLock` or `MiniportInterruptsSpinLock` -> `xhciControllerLock` ->
  nothing.
- **No usbport service and no bounded wait while it is held.** Decide under the
  lock, act after dropping it. That covers `UsbPortWait`,
  `UsbPortRequestAsyncCallback`, `UsbPortInvalidateController`,
  `UsbPortReadWriteConfigSpace`, `UsbPortBugCheck`, the completion and
  invalidate services, and `KeStallExecutionProcessor`. The host suite checks
  this for every service stub and wait hook through one report function,
  asserted once at the end of the run; a new service must be routed into it
  rather than exempted by omission.
- **Every non-ISR read-modify-write of `Flags` happens under this lock, in one
  of two shapes.** A standalone transition uses `XhciControllerUpdateFlags`,
  which brackets itself; a transition that is part of a larger locked
  transaction (quiesce clearing `INITIALIZED` after the mask,
  `Enable`/`DisableInterrupts` moving `INTERRUPTS` beside the enables) is
  written inline while the lock is already held. The second shape is required,
  not tolerated: the helper acquires, so calling it inside a locked region is a
  recursive acquisition of a DISPATCH-level spin lock, which hangs.
- **"Innermost" does not mean "always safe to acquire".** One lock removes
  lock-order deadlock, because nothing is ever taken after it; it does nothing
  about taking it twice on one CPU, which spins forever. Every helper that
  brackets itself may only be called from an unlocked context. The host suite's
  `commandLockErrorsTotal` catches nesting and unbalanced release.
- **The ISR takes no lock and needs none**; see "Interrupt Ordering". It is
  stateless and its one read-modify-write is monotone.
- **New shared state joins this lock** unless all three criteria in the design
  doc's section 8 hold; if a sibling is ever added the order is
  `xhciControllerLock` -> per-endpoint lock, never the reverse.
- **The init and reinit sequence is the one exemption**, and it rests on a
  precondition rather than on scope: `XhciInitController` clears
  `XHCI_EXT_FLAG_INITIALIZED` under the lock as its first act and sets
  `HcInfoStatus` bad on entry, and every other context tests one of those before
  touching controller state. Do not add a path that touches controller state
  without such a gate.

## Ring Full and Backpressure

- Before enqueuing, verify the whole TD fits: advancing Enqueue (past the Link TRB where applicable) must not reach the Dequeue position (spec 4.9.2.2).
- If it does not fit, write nothing and return busy from SubmitTransfer; usbport requeues and retries. Never write a partial TD.
- A producer ring keeps one slot permanently empty so `Enqueue == Dequeue` can only mean empty. With the wrap-back Link TRB occupying the last position, an N-TRB segment therefore holds N - 2 outstanding TRBs, not N - 1.
- **A refused transfer is re-offered by a latch, not by usbport's timer alone.** A `SubmitTransfer` refused with `MP_STATUS_NO_RESOURCES` is left queued by usbport and re-offered on its health-poll timer (nominal 500 ms, 36-80 ms measured).

  That never mattered on an interrupt endpoint (one read outstanding, the ring never full) and is a throughput defect on a bulk one, where a full ring is the ordinary steady state of a device moving data as fast as it can; a mass-storage device would have run at two transfers a second with nothing reporting a fault.

  So `XHCI_TRANSFER_QUEUE` carries `RetryArmed`/`RetryFree`/`RetriesAsked`, on the queue because it is the one structure EP0 and a pooled endpoint both have, and the re-offer condition is strictly more free TRBs than at the refusal, which is what makes it terminate: each re-offer costs a real retirement, and an empty ring always fits one transfer (the `max_transfer_fits_a_pool_ring` assert). One release function, two triggers: the completion that frees the TRBs, and the health poll for space a completion never freed (a Set TR Dequeue placement, a drain).

## TD Publication

- **Enqueueing is a TD-level operation, never a loop over single TRBs.** Write the head TRB's Control word with the inverse of the producer cycle first, write every other TRB of the TD, then store the head TRB's real Control word as the single last store. A controller already consuming the work ahead of the TD would otherwise walk into a TD whose later TRBs do not exist yet.
- The doorbell is rung by the caller after that publishing store, never inside the encode layer.
- Ring memory is cached common-buffer memory, so this ordering rests on `volatile` accesses, not on a barrier and not on an uncached mapping (`docs/contributing/design/04-controller-common-buffer.md` section 6).
- A TD that spans the Link TRB must set the Link TRB's Chain bit, and a crossing between TDs must clear it. The Link TRB is permanent and shared by every lap, so this is rewritten at each crossing (spec 4.11.7; transcribed in `docs/usb-xhci-info/xhci-data-structures.md`, "TD composition and Link placement"). The xHC ignores CH on the command ring, so one code path serves both.
- Publishing the Link TRB's new cycle mid-TD is safe only because the TD's head is still invalid in front of it. That is a consequence of the rule above, not an independent one; do not reorder the publish without re-deriving it.
- **Draining a transfer queue does not empty the ring behind it.** `XhciXferQueueDrain` detaches transfers and touches no ring, so the dequeue pointer still sits on the TRBs those transfers left behind. Anything that needs the ring empty (a Configure Endpoint whose Add Context flag takes its TR Dequeue Pointer from where the ring is now, for instance) must move the pointer itself with `XhciRingSetDequeue`. Assuming otherwise cost a real defect in the reconfigure path: a Drop+Add would have republished a context pointing at cancelled TRBs whose buffers usbport had already reclaimed.

## Fatal Errors

- `CheckController` polls USBSTS.HCE and HSE every invocation. It reads the
  register under the controller lock and escalates after releasing it. A
  check of `ControllerFailed` followed by unsynchronized MMIO is the defect
  closed everywhere else in the driver, and `UsbPortInvalidateController` is a
  usbport service that may not be called under this driver's lock.
- **Escalate on the transition, not on every poll.** HCE is read-only and HSE
  is left unacknowledged: clearing an RW1C bit would destroy the one durable
  record of why the controller failed, on a path that has already decided not
  to retry in place. So both stay set, and an unlatched poll would queue a
  reset on every health poll for the life of the driver.
- **An all-ones USBSTS is not a fatal-bit report.** It is a window that has
  stopped decoding, and HCE and HSE are two of the thirty-two bits it answers
  with. The same operand rule applies to the interrupt masks; here the cost of
  getting it wrong is a healthy controller marked terminally failed on one bad
  read.
- **A Host Controller Event escalates from the DPC, not from the poll.** Event
  Ring Full and Event Lost set neither HCE nor HSE, so no poll of `USBSTS` will
  ever see them, and a driver that recorded the completion code and waited for
  `CheckController` would wait forever. That event is the only controller-level
  report of them. A TD-related Event Lost also arrives as a Transfer Event with
  completion code 32, halting that endpoint ("An Event Lost Error shall be
  generated for the endpoint ... [and] shall halt the endpoint", 4.10.1,
  p.173); both routes escalate.

  Code 32 is not the only completion code `xhciXferCodeInfo` marks `Fatal`.
  Table 6-90 marks Undefined Error (33) fatal outright ("An Undefined Error
  shall be treated as a fatal error by software", p.469) and requires an
  unrecognised vendor error (192-223) to be read as that condition, which is
  every code in the range for a driver with no vendor knowledge. All of them
  escalate the same way code 32 does.

  Every Host Controller Event completion code escalates, vendor ranges
  included: the event is controller-level by construction (Table 6-91) and
  this driver has no vendor knowledge with which to call one benign. That last
  part is a knowing deviation from a "shall" (Table 6-90 says an unrecognised
  code in 224-255 "shall" be read as Success, p.469), taken because an
  unnecessary reset is bounded and recoverable while a fault read as Success
  is silent.

  The drain still finishes and still publishes ERDP with EHB = 1; returning
  early would silence the interrupter for good.
- **A dropped event is not a late one**, which is what makes this fatal rather
  than a statistic. Every completion is matched by identity (a command by its
  TRB pointer, a transfer by its TD), so nothing arrives afterwards to resolve
  it: the engine stays pending and its watchdog aborts a ring whose real state
  nobody knows.
- **Slot-fatal is a third severity, and it is not controller-fatal.**
  Incompatible Device Error (22) is "fatal as far as the Slot is concerned.
  Software shall issue a Disable Slot Command to recover" (Table 6-90, p.468).
  Filed with the ordinary transfer errors, the endpoint would be halted, reset
  and repositioned while the slot stayed enabled, a loop with no exit but an
  unplug. It routes instead to the ordinary device teardown
  (`xhciDevSlotFatalEvent`), which owes the Disable Slot behind the endpoint
  stops 4.6.4 p.97 requires first. It does not escalate to a controller
  invalidation: the spec scopes the fault to one slot, and answering it by
  resetting the controller would take every other device down with it.
- **"Any command or transfer" means both event families.** The sentence that
  scopes code 22 to the Slot opens "This error may be returned by any command
  or transfer" (p.468), and Undefined Error and the vendor error range are not
  scoped to transfers either. A command path that reduces every completion
  code to success/non-success turns a Configure Endpoint answered with 22 into
  one `EndpointConfigureFailures` on a device that stays in service, and
  escalates nothing for a command answered with 33 or a vendor error, while
  the same code on the same ring refuses a restore. So `XhciCommandEvent` asks
  `XhciXferCodeInfo` after the ordinary retirement: a slot-fatal code goes to
  `XhciSlotCommandSlotFatal`, selected by the event's own Slot ID, and a fatal
  one takes the engine's existing `XHCI_CMD_ACTION_RESET` route.
- **Endpoint Not Enabled Error (`XHCI_CC_EP_NOT_ENABLED`, 12) is not a command
  result.** Table 6-90 p.467: "Asserted if a doorbell is rung for an endpoint
  that is in the Disabled state. The Slot ID and error Endpoint ID are
  reported. Also refer to section 4.7." Section 4.7 p.143 gives the event
  family: a doorbell written against a Disabled endpoint means "the xHC should
  generate a Transfer Event TRB with the TRB Pointer, TRB Transfer Length,
  Event Data (ED) fields set to '0', a Completion Code of Endpoint Not Enabled
  Error, and the Slot ID and Endpoint ID fields", posted to the Primary Event
  Ring. So it arrives on the transfer path, pointerless, like Ring Underrun
  and Ring Overrun.
- **This driver refuses code 12 rather than decoding it, and what that costs is
  immediate recovery rather than recovery.** `XhciXferCodeInfo` answers
  `XHCI_XFER_BAD_PARAM` for it and `xhciCompletionCodeValid` leaves it off both
  endpoint lists, so it is counted as a bad code and no completion is
  attributed to it. That is the safe direction and is not the deviation: a
  zero TRB pointer offered to the per-TD matcher would resolve to whatever sits
  at the ring's base. The deviation is that nothing acts on the event.

  The TD stays queued until usbport times it out, and recovery arrives on the
  cancellation path that follows: the Stop Endpoint that cancellation issues
  reads the Endpoint Context back, and a `Disabled` reading is the condition
  code 12 reports, so `xhciEpStopped`'s Disabled branch raises
  `XHCI_EPQ_NO_CONTEXT` and calls `xhciEpOweContextRestore`, which schedules
  the Add-without-Drop Configure Endpoint that puts the context back. The
  implemented behaviour is delayed recovery through a path that already exists
  and has its own vector, not an endpoint stranded for good.

  What justifies leaving the immediate route unbuilt: the event is a `should`
  rather than a `shall`, and the same page says "The xHC may ignore doorbell
  references to Device Slots in the Disabled state or endpoints in the
  Disabled state", so its absence proves nothing either; and every lower rung
  is illegal on a Disabled endpoint (a Reset Endpoint "may only be issued to
  endpoints in the Halted state", 4.6.8 p.118), so an immediate answer would
  have to be the same Configure Endpoint the delayed path already issues,
  bought at the cost of a second route to it.

  The measurement that reopens it is an `xfer.error` log record carrying
  completion code 12: `XhciSlotTransferEvent` logs every non-ordinary code with
  its Slot ID and DCI, in release builds as well as debug. `XHCISNAP` has read
  both the note ring and the counters off Windows 98 bare metal since
  `0.0.0.6`, from a shipping `release` build and a shipping `debug` build
  alike, measured on the E460. What Windows 98 metal still has no channel for
  is a live trace; the snapshot is a read on demand rather than a stop-time
  flush, so the record is taken after the fact rather than caught as it
  happens. No run of this project has produced one.
- **The list of fatal codes lives in `xhciXferCodeInfo` and nowhere else.**
  Every hand-written enumeration of them found downstream was short by at
  least one. Any path that has to know whether a code is fatal (the DPC's
  transfer arm, the restore's stale-event drain) asks the table, so that
  widening Table 6-90's transcription widens every reader in the same edit.
- **`CheckController` also ages the outstanding command.**
  `UsbPortRequestAsyncCallback` returns 0 on success and on its own pool
  allocation failure, so a command can be enqueued, doorbelled and left with
  nothing scheduled to time it, and the one-outstanding-command engine then
  answers BUSY to everything for the life of the driver. Measure the age in
  milliseconds (`XHCI_COMMAND_AGE_MS`) on the poll clock, size the threshold
  clear of the whole watchdog ladder so it can only catch a ladder that was
  never armed, key the stamp to the command generation so two commands issued
  between two polls do not share an age, and escalate once per crossing.
- **No threshold in this driver may be expressed as a count of health polls.**
  An earlier rule said the opposite ("count the age in polls, not
  milliseconds: the miniport has no clock it may read at DISPATCH without a
  new import, and a host that polls more slowly then fires later rather than
  sooner"). Both halves of it failed.
  - The clock existed. `XhciFrameNumber` is MFINDEX-derived, DISPATCH-safe,
    already in the extension and already sampled by that very poll.
    `XhciPollClockAdvance` keeps a second axis off the same register,
    `PollClockMs`, one tick per frame and one frame per millisecond, because
    the published axis carries a stall path that advances per call and a
    congruence claim an isochronous Frame ID depends on, and neither belongs
    in a watchdog.
  - The safety argument named one direction of two. The ThinkPad E460 polls
    this miniport at 36-80 ms (36 ms idle, about 75 ms while the bus is busy),
    measured directly by this clock: 354,364 ms against 6,461 polls
    (`run-13e.md`, Finding V). Every budget in the driver was therefore about
    an order of magnitude short. `XHCI_COMMAND_AGE_POLLS` came out at 2.3-5.1 s
    instead of 32 s, at or under the 5 s watchdog it was sized to sit 12 s
    behind, so the backstop that "cannot pre-empt a ladder that is working"
    was pre-empting it every time. `CommandsTimedOut` read 0 in every dump
    ever taken from that machine, and the driver had been resetting
    controllers over commands that were merely slow.
  - Do not repeat the earlier figure of about 1 ms for the poll period. It
    was 971,359 polls divided by a session nobody timed, an inference wrong by
    one to two orders of magnitude. The rule is unchanged by the correction (a
    threshold whose size the host decides is the defect, whether the factor
    is ten or a thousand), and the poll rate is not even constant on one
    machine.
  - Raising a count is not the repair. That re-parameterises a quantity whose
    units are wrong, and the rate is not a constant: the 2a and 2b guests and
    this same E460 on an earlier boot all show different
    `CheckCallbacks`-to-`HealthPolls` ratios.
  - So: size from the mechanism the wait belongs to, in that mechanism's own
    unit, and compare against `PollClockMs` with an unsigned difference. The
    five in the shipping tree are `XHCI_COMMAND_AGE_MS`, `XHCI_DEV_AGE_MS`,
    `XHCI_DEV_STALL_MS`, `XHCI_PORT_AGE_MS` and `XHCI_EP_RESTART_MS`, and the
    two relationships that have to hold (the command backstop clearing the
    watchdog ladder, the port age clearing the reset deadline) are
    `XHCI_C_ASSERT`s rather than prose. The port age was the worst of the
    five.
  - The two `*_POLLS` names left in `src/` are both inside `#ifdef
    XHCI_FIX_*`: `XHCI_RH_SWEEP_SLOW_POLLS` (W15SLOW) and
    `XHCI_RH_GATE_STUCK_POLLS` (W7), so no shipping flavour carries either.
    They are Finding 3 bench candidates whose cadence is the measurement, and
    they are left alone on purpose. A candidate that is ever promoted to a
    shipping flavour has to convert first, and W7's is the same defect as the
    rest: twenty polls is ten seconds at 500 ms and 0.7-1.6 s at the E460's
    36-80 ms.
  - A poll rate may only change the resolution of an answer, never its size.
    That is the property the poll-count form claimed and did not have.
- Either flag set: stop submitting, fail pending work, request `UsbPortInvalidateController(RESET)`.
- **Escalate through the ladder, and give the last rung an owner.** A miniport
  may not retry a fatal condition by poking the hardware again from wherever it
  noticed it, but an escalation with nowhere to go leaves the machine down
  until the next cold boot. Concretely, in three parts:
  1. A rung may not be skipped. The health poll's age detector, the backstop
     for a command whose watchdog was never armed, writes `CRCR.CA` first and
     asks for a controller reset only if the ring is still not stopped an
     interval later. Asking for the reset outright is what the wedged E460
     read like from the counters: `CommandAgeResets 1` with `CommandsTimedOut`,
     `CommandAbortWaits` and `CommandAbortsNotWritten` all 0, so a single Stop
     Endpoint that would not complete took the whole controller out of service
     without the specification's own remedy being tried once.
  2. The recovery is not "HCRST + full reinit in `ResetController`". That
     callback runs at DISPATCH_LEVEL inside one of usbport's spin locks, where
     the init sequence's waits are illegal (see "Wait Primitives"). What it
     does there is mask the interrupt enables, mark the controller failed, and
     raise a request.
  3. The reinitialization then happens in place, from the async-callback DPC
     the health poll arms, under `ext->InitBelowPassive`. See the
     `ResetController` bullets under "Command Ring" for the whole contract.
- **A failed controller has to be enforced, not just recorded.** Command
  submission, the command watchdog, the command-completion arm, the ISR, the
  DPC and `EnableInterrupts` all check it. A mark that only one of them reads
  lets an idle engine issue a fresh command, an uncancellable timer write
  `CRCR.CA`, or a queued DPC re-arm `IMAN.IE` against the masking the reset
  just performed. The stop, quiesce, suspend and mask paths keep working:
  proving DMA has stopped is what still matters about a controller nobody is
  going to fix.
- Keep `USBCMD.HSEE` = 0.

## DMA Addresses

- All hardware-visible addresses programmed into xHCI registers or TRBs must be physical bus addresses.
- **The driver is a 32-bit DMA consumer on both targets**: upper 32 bits are always written as zero. On Win98 that is a property of the OS. On Win2000 it is a declared constraint, not a guarantee - SP4 supports PAE on Advanced/Datacenter Server, where physical memory above 4 GB exists. Under Option A the miniport never allocates or maps DMA memory itself, so the 32-bit ceiling is `usbport.sys`'s DMA adapter to enforce (the HAL double-buffers a high buffer down); the miniport must program whatever physical address it is handed and must never widen it. Never construct a 64-bit address from a Lo/Hi pair.
- **Confirmed, statically, in both shipping `usbport.sys` builds**: the adapter is created `Dma32BitAddresses = 1` / `DmaWidth = Width32Bits` with `Dma64BitAddresses` never written, and every transfer SG element is produced by that adapter's `MapTransfer`.

  But usbport does not mask the SG high DWORD - it stores whatever the HAL returned. The zero is inherited from the adapter's declared width, not enforced at the element. So the miniport checks the high DWORD of every SG element and fails the transfer if it is nonzero; it never merely assumes it.

  `USBPORT_RESOURCES.StartPA` is only a `ULONG`, so its high DWORD is not exposed and cannot be checked by the miniport. Rely on the same 32-bit adapter contract for that common buffer, and reject any `StartPA + common-buffer size` overflow. See `docs/usb-xhci-info/usbport-miniport-abi.md` section 5.
- Keep helper APIs that write both low and high DWORDs so the TRB/register format remains correct even though Hi is always 0.
- Never put a virtual URB buffer pointer into a TRB.

## Transfer Buffers

- Under Option A, `usbport.sys` hands the miniport already-mapped scatter/gather physical addresses for URB payloads - confirmed statically in both target binaries, through the NT DMA adapter, page-granular. Program those into TRBs directly. What the static pass does not settle is element ordering versus `SgOffset` and everything else about `SubmitTransfer`; order TRBs by `SgOffset` and instrument the list when the callback first becomes reachable in Phase 6 (`docs/usb-xhci-info/usbport-miniport-interface.md`, "What Phase 3 can and cannot prove about transfer mapping").
- A single TRB's data buffer must not span a 64 KB physical boundary (xHCI spec 6.4.1 note). Split every SG fragment at 64 KB boundaries into chained TRBs - length <= 64 KB alone is not sufficient.
- Common-buffer bounce buffers are the fallback policy only where the driver owns the mapping itself (Option B, or if the spike shows usbport passes virtual buffers): OUT transfers copy caller data into the bounce buffer before ringing the doorbell; IN transfers copy from the bounce buffer back to the caller after completion.
- **Actual transferred length is `requested_length - residual_length` only for a single-TRB TD.** Without the qualifier it is wrong for every multi-TRB TD: "For multi-TRB TDs, if ED = `0`, the TRB Transfer Length only reflects the number of bytes transferred for the buffer associated with the Transfer TRB pointed to by the Transfer Event, not the total bytes transferred for the TD" (Table 6-39 note, p.441).

  The general rule is the sum at 4.10.1.1.2 p.175 - the TRB Transfer Length fields from the TD's first TRB up to and including the one the event named, minus that event's residual - which is `XhciRingSumTrbLengths`. An error event uses the same shape (Table 6-38). The difference is invisible on the single-fragment transfers enumeration mostly does and silently wrong the moment a descriptor read is scattered.
- **The Setup Stage's 8 bytes are not part of it**, and neither is the Status Stage: a control transfer's byte count is the Data Stage TD's alone. ReactOS's EHCI miniport excludes the SETUP PID's length from its own accumulator for the same reason.
- **A cancelled multi-TRB TD's byte count is the forced Stopped Transfer Event's sum-to-the-named-TRB minus its residual.** On the single-TRB TDs of Phase 7a that was indistinguishable from `requested - residual`; on bulk it is not, and the mutation swapping the two fails 12 checks (task 8-A.3).
- **Two length readings are not residuals at all** and must never be subtracted: a Transfer Event with `ED = 1`, and one with Completion Code Stopped - Short Packet (28), both of which report the Event Data Transfer Length Accumulator instead (Table 6-38, p.440).

  The EDTLA is a running total of the bytes the TD has moved, so `sum - residual` produces nonsense from it. It is still a byte count: the xHC maintains it per endpoint and clears it before each TD whether or not software ever places an Event Data TRB (4.11.5.2, p.209-210) - those TRBs report the accumulator rather than create it. A Stopped - Short Packet event's length is therefore directly usable as "bytes transferred so far", which is how the cancellation path reads it (`XhciXferQueueStopped`); the ordinary event path still refuses it, because there the field would be fed to the residual arithmetic.
- **Any event that measured a byte count fixes the reported length; only when nothing measured anything may a terminal Success be read as "the whole request completed".** The completion code decides the `USBD_STATUS`; the length field decides the bytes; where they disagree the length field wins. Keying the latch on the completion code instead lets two distinct silent overreports through, and both discard a number the controller actually reported in favour of `requested_length`:
  - A Success carrying a nonzero residual - the spurious-success quirk (`docs/usb-xhci-info/xhci-programming.md`, "Spurious success"): NEC uPD720200, Fresco Logic FL1000 and early VIA VL800 revisions return code 1 for transfers that delivered fewer bytes, with the length field still correct. Linux carried this as `XHCI_TRUST_TX_LENGTH` before making it every controller's default.
  - A Success carrying a residual of zero on a non-final TRB of the TD, which measures the bytes moved so far. If the controller then advanced to the Status Stage rather than to the next data TRB, the transfer ended there, and nothing distinguishes that from "more is coming" at the time the event arrives.
- **The asymmetry that decides the ambiguous case: prefer the underreport.** A short length is a short descriptor, which usbport and usbhub see and retry or fail on; an overreport is a buffer whose tail was never written being read as valid data. Nothing legitimate is lost by fixing the length on a zero-residual data-stage Success, because this driver gives no data TRB any reason to raise a Success event at all - IOC is the Status Stage TRB's alone (p.430) and ISP produces code 13, not code 1. Count such an event, since it is the only reading that would show a controller emitting one and thereby being underreported.
- **Keep the three apart in the counters**: a Short Packet (a controller reporting a short transfer as the spec requires), a Success with a residual (a controller that does not), and a zero-residual Success on a data TRB (a controller talking about TRBs nobody asked it to report on). Same length arithmetic, three different diagnoses.
- **A failed transfer keeps the length it had when it failed.** A controller that has already contradicted itself on a transfer does not get to put a byte count back on it with a later event.
- **An impossible length is refused, not clamped.** A residual larger than the TRBs it applies to, or a computed length beyond the buffer usbport mapped, means the answer cannot be believed: complete the transfer with an error rather than reporting `sum - residual` (an unsigned wrap, near 4 GB) or a zero nothing measured. Count the causes apart - a bad residual is the controller's fault, a length past the buffer is this driver's bookkeeping, and a sum that cannot be taken at all is the ring and the transfer record having diverged.

## Control Transfers

- **A control transfer is two or three TDs, not one**: "Control transfers require two or three TDs to define them: a Setup Stage TD followed by an Status Stage TD, if a data stage is required for the transfer an optional Data Stage TD will reside between" (spec 6.4.1.2, p.430). The Setup Stage TD is a single TRB; the Data Stage TD is a Data Stage TRB chained to zero or more Normal TRBs; the Status Stage TD is a single TRB. Chain flags mark the TRBs within each TD and must be clear at the boundaries between them.
- **They are still published as one store.** Publishing TD by TD lets the xHC begin a control transfer whose Status Stage TRB software has not written yet. `XhciRingEnqueueTdGroup` writes every TRB with the head's cycle bit inverted and stores the head's real Control word last, as the single-TD case does.
- **IOC belongs only to the Status Stage TRB**: "Note: The IOC flag should only be set in the Status Stage TRB of a Control transfer" (p.430). So a successful control transfer produces exactly one Transfer Event, and it names the group's last TRB. This is the one place the "IOC on every TD's last TRB" rule under "Completion Matching" does not apply, and what replaces the guarantee it provided is that the xHC "shall advance to the Status Stage TD" after a short packet (p.433), plus a retire that jumps past the matched tail and so sweeps the Setup and Data TDs with it.
- **ISP goes on every IN data TRB**, the last included. A short packet may land on any of them and only the TRB it lands on reports it. The redundancy p.176 notes - ISP beside IOC - does not arise, because IOC is elsewhere.
- **Retire only at the group's end.** An error on the Data Stage TD's last TRB satisfies `CanRetire` just as a Status Stage event does, and retiring there reclaims the data TD while leaving the Status Stage TRB of a transfer that no longer exists outstanding for the life of the ring - one slot lost per failed transfer, with no error anywhere to say why. Every other way a transfer ends is an error or a stop, where the position is placed explicitly with `XhciRingSetDequeue`, which is the spec's own instruction: "software shall use a Set TR Dequeue Pointer Command to advance the Transfer Ring to the next TD" (p.172).
- **Direction comes from the SETUP bytes, and the caller's flag is checked against it, not used.** Spec p.192 makes it software's job to keep the Data and Status Stage DIR flags consistent with `bmRequestType`'s DTD bit and `wLength`; Table 4-7 (p.193) is the mapping, transcribed in `docs/usb-xhci-info/xhci-data-structures.md`. A transfer whose two statements of its own direction disagree is malformed, and guessing which to believe would program a data stage that moves bytes the wrong way.
- **Drive the TD from `TransferBufferLength`, not from `wLength` and not from the SG list.** They may legitimately differ ("communicating with some non-compliant devices may require violating this rule. The transfer lengths managed by the xHC depend strictly on the TRB Length fields", p.193), and a zero-length transfer arrives with an SG list that is non-NULL but empty - the normal case for SET_ADDRESS, SET_CONFIGURATION and every no-data control request (batch 6-0, both shipping builds).
- **Consume the whole SG list in `SgOffset` order.** Whether usbport hands the elements over in that order is what task 6-V.1 measures; selecting by offset is correct either way, and it turns a list that does not tile `[0, TransferBufferLength)` exactly - a gap, an overlap, a zero-length element, elements left over - into a refusal rather than a transfer that moves the right number of bytes from the wrong places.

## Device Addressing

- Never place a SET_ADDRESS setup packet on a transfer ring. The xHC blocks software-issued SET_ADDRESS and completes the TRB with a TRB Error (spec section 4.5.4.1); addresses are assigned via the Address Device command (spec section 4.6.5) - the xHC issues SET_ADDRESS on the bus itself.
- `usbport.sys` sends SET_ADDRESS as an ordinary EP0 control transfer; the miniport must intercept it, issue Address Device (BSR = 0), and complete the transfer back as success.
- Keep a usbport-address -> Slot ID map. Treat the address usbport assigns and the xHC-assigned one as unrelated: they may coincide (both allocators tend to count up from 1), so never infer the mapping from equality and never assert a mismatch. Every later endpoint open and transfer is keyed by usbport's address.
- **The address-0 pipe's root port is derived and then checked, never taken from usbport.** `USBPORT_ENDPOINT_PROPERTIES.HubAddr`/`PortNumber` do not name a root port: `HubAddr` is the TT hub's address (`0xFFFF` when there is no TT), and `PortNumber` is the port on that TT hub whenever a TT was selected, at any tier depth - it coincides with the root port only for a root-attached device, and only because this driver reports every connected root port as High Speed (see "Root Hub Reporting"), which makes usbport skip the TT lookup and leave the seeded value alone (measured in batch 6-V's VM runs, on both builds).

  Reading `PortNumber` as a root-port index therefore assumes the very thing the field is being consulted to establish. The only signal that says which port is being enumerated is the completion of a port reset - usbhub resets a port and immediately creates a device at address 0 on it.

  Validate that hint against the port's own shadow (still connected, still enabled, no record bound) before using it, and refuse the open rather than guess when it does not hold or when more than one port could be meant. A slot built for the wrong port is a failure that never reports itself.
- **A nonzero `SubmitTransfer` return cannot fail a transfer** - usbport leaves it queued for retry. That is the right answer while a device's command chain is still running, and an infinite loop once the chain has failed. To fail a transfer, accept it and complete it with an error. Ask for the retry with `UsbPortInvalidateEndpoint` when the chain completes, rather than waiting on usbport's health-poll timer.
- **A refusal that can never stop being true must fail the transfer, and it livelocks the URB's thread if it does not.** Measured on 2b (batch 6-V, blocker 2): opening a device's property page left Device Manager half-painted while `transfers refused for retry` climbed 2 per cycle against a frozen `transfers submitted`.

  The permanent set is `dev == NULL || dev->State == XHCI_DEV_STATE_FREE || ext->ControllerFailed || ext->HcInfoStatus != XHCI_HC_OK` - a gone device is answered `USBD_STATUS_CANCELED`, a failed or undecodable controller `USBD_STATUS_INTERNAL_HC_ERROR`, both counted in `TransfersFailedGone`; a suspended or reinitialising controller still refuses for retry, and the REMOVE case stays a retry by design (changing it fails `test_slot_endpoint_remove`).

  The `FREE` clause is there because `xhciDevFromRef` bounds-checks an index and nothing else, so a released record still answers a non-NULL pointer and `dev == NULL` alone never covered the real case. The livelock signature to read is a climbing `refused for retry` beside a frozen `submitted`. (`USBD_STATUS_DEVICE_GONE` does not exist in the Windows 2000 DDK's `usbdi.h`; it is later-WDK vocabulary.)
- **Releasing a device record is a claim about the hardware and needs evidence.** A record's Slot ID and its common-buffer blocks become reusable when it is released, so a release on a controller that may still have that slot enabled hands the next device a device context the xHC is still following - and the failure surfaces as corruption in a later enumeration. A live `USBSTS.HCH`, or a quiesce that returned success, is evidence; "we are about to reset it" is not, because the reinitialisation's preflight may refuse and by design writes nothing at all. Where the evidence is missing, abandon the record in place - Slot ID kept, DCBAA entry untouched - for the reset to release later.
- **Completing a transfer gives its DMA buffer back, so it needs the same evidence.** `UsbPortCompleteTransfer` is not merely an answer: usbport unmaps the transfer's scatter/gather list and finishes the URB, and the pages return to whoever owned them. A transfer completed while the TRBs that name it are still on a ring the controller can execute is a DMA target handed back under a live master - the fault `XhciFailClosedDma` exists to report, created instead. So a teardown that cannot prove the controller stopped leaves its transfers queued; they are answered by whichever path does prove it (after `HCRST` there is no ring left to execute them), and where nothing ever does, the fail-closed bugcheck is the answer rather than a completion.
- **A restore is not a shortcut past the reinitialisation's preconditions.** `Restore State` skips `XhciInitController`, so anything that path establishes must be established again or the resume reports success on a controller that cannot serve a device: PCI Bus Master Enable put back if the suspend's quiesce took it (a controller without it runs, accepts doorbells and delivers nothing at all), `USBSTS.CNR` clear before any operational-register write (5.4.2, and a power transition is when it is set), and the capability registers re-derived to confirm it is the same controller the saved layout was carved for.
- **An EP0 reopen must not reinitialise the transfer ring.** The xHC's Endpoint Context still holds a TR Dequeue Pointer into it, and Evaluate Context updates Max Packet Size and Max Exit Latency only (spec 6.2.3.2) - so a ring restarted at its base leaves the software and hardware pointers on different laps of the same memory, with nothing that can reconcile them short of Stop Endpoint and Set TR Dequeue Pointer. This is the reason EP0's ring lives in the controller common buffer (`docs/contributing/design/04-controller-common-buffer.md` section 3.4).
- **A port disable or power-off is two obligations, not one, and only the first is unconditional** (batch 6-V, blocker 1). `XhciSlotPortDisowned` runs the instant `RH_ClearFeaturePortEnable`/`RH_ClearFeaturePortPower` arrives: usbport has destroyed its device object and freed the USB address whatever the hardware did, so the record gives up its address-map entry and its EP0 binding, keeps its slot and ring, and refuses further transfers as a permanent failure - a surviving map entry is what refused the next device at that address.

  `XhciSlotPortDisabled`, which completes the queued transfers and hands their mapped buffers back, waits for the port to confirm in its own bit - `PP` for a power-off, `PED` for a disable, an all-ones read confirming neither - carried on `DisownPending`/`DisownWantsPp` and collected by the health poll, unbounded by design: early execution is the DMA hazard and there is nothing safe to give up to. It is not derived from an observed `PED = 0`, because `PR = 1` clears `PED` (Table 5-27 p.371) and a shadow-derived trigger would tear down every device mid-reset.

  Measured: the disable write confirms sometimes on 2b (`outcome=1` and `outcome=2` in one log) and never on Win98, where all three disowns read `outcome=2` and `xhciRhAdmitted` is false while idle-suspended, so the poll cannot collect - the disown half carries the entire fix there. A disowned record whose port never confirms keeps its slot until the physical unplug releases it, which is by design rather than a leak. `DevicesDisownedOut` and `DevicesDisabledOut` are printed as a pair for that reason.
- **A retained record re-entering enumeration takes the branch the Output Slot Context's Slot State allows, and on a churn of fresh devices that is `ADDRESS_BSR`.** `xhciDevReenterAtDefault` runs on usbport's `ReopenPipe` of EP0 mid-enumeration, before the Address Device that would put the slot in `Addressed`, so `SlotsResetToDefault` reads 0 with `DevicesReopened` nonzero and `SlotStatesUnreadable` 0 - every read succeeded and none found `Addressed`/`Configured`, so no Reset Device was owed (task 12.5 measured 12/0/0). A zero there is not the defect it was once predicted to be; the reading that exercises the Reset Device branch is a workload re-enumerating an established device in place, and none has been run.

## Endpoint Configuration

- **A reprogram must not overwrite `Params` before its command succeeds.** "The Output Slot/Endpoint Context parameters shall not be changed if any error is detected by this command" (the closing note of 4.6.7, p.115; Configure Endpoint has no sentence of its own, and the driver applies the same rule to it), so a refused Configure Endpoint would otherwise leave this driver describing an endpoint the xHC does not have, with the old description gone. `PendingParams` holds the new description until the completion.
- **An isochronous endpoint's Interval comes from usbport's per-packet stamps, not from `Period`**, which usbport forces to 1 for isoch. The stamps are one packet per microframe on High Speed and one per frame otherwise - an ESIT - so Interval is 0 and 3, both inside Table 6-12's isoch rows (FS Isoch is 3-18 where FS/LS Interrupt stops at 10). CErr is 0 by `shall`, and Low Speed is refused outright because USB 2.0 gives it no isochronous transfers at all. That Interval is correct only when `bInterval = 1`; the configuration-descriptor snoop below is what replaces the assumption where a descriptor was seen.
- **The Frame ID window is a distance, taken in the full 32-bit domain and only then compared against the spec's two bounds.** `(current + IST + 1)` to `(current + 895)` is measured as `frameNumber - CurrentFrame` on the full 32-bit numbers: a magnitude comparison is right for half of every second and wrong for the other half (fails 9 checks), and reducing both sides mod 2048 first makes a stamp from a lap ago indistinguishable from one 48 frames ahead. Only the Frame ID written into the TRB is the low 11 bits.
- **There is no isochronous-specific cancellation path.** An isochronous transfer leaves its queue through the same `XhciXferQueueRemove` and its TRBs are reclaimed by the same quiescence chain as every other kind; a second policy for a state that already has one is not wanted.
- **usbport does not zero the transfer extension between transfers.** A record inheriting `XHCI_XFER_FLAG_ISOCH` from a previous tenant would send an ordinary bulk failure through the isochronous completion service with a block pointer usbport had already freed; `xhciDevStampFailure` clears `Flags`, and every path that fails a request before it reaches the ring goes through one kind-aware helper so the completion service cannot be picked wrong.
- **The configuration-descriptor snoop (`src/xhci_desc.c`) commits only a complete configuration.** usbport reads the nine-byte header first to learn `wTotalLength` and then re-reads the whole descriptor, so a partial reply is the ordinary first half of every enumeration and not a fault - but a table built from one would say "no isochronous endpoint past byte 64" and be believed.

  The walk is a byte-level state machine fed in 32-byte chunks off the SG list's `MappedSystemVa` (no private pool, DISPATCH_LEVEL), so feeding the same bytes at every chunk size must produce the same table, which `test_desc` sweeps. The table records its own `bConfigurationValue`, and a snooped `SET_CONFIGURATION` selecting any other value discards it (`DescConfigsSuperseded`, expected 0) - acted on at the submit, since the selection has no reply. An alternate-setting disagreement makes the reading unusable rather than picking one: `SET_INTERFACE` never reaches the miniport.

  The derived/assumed provenance moves only when the Configure Endpoint commits, and `IsoCadenceMismatches` is split into assumed and derived because a derived-cadence mismatch is by design - usbport stamps High-Speed packets per microframe whatever the descriptor says.

## Hub Paths

- Route String is zero only for devices attached directly to xHCI root ports.
- Downstream hub devices must encode their hub-port path in the Route String.
- FS/LS devices behind HS hubs must include the transaction-translator hub slot and port fields.
- Test both single-TT and multi-TT High-Speed hubs before calling USB 2.0 hub support stable.

## PnP Resources

- Under Option A, `usbport.sys` owns the host FDO and hands the miniport its translated resources (BAR, interrupt) at the start-controller callback - the miniport does not process `IRP_MN_START_DEVICE` itself. Use those translated resources for BAR mapping; never rediscover the BAR from PCI config space. (Under the Option B fallback, the same applies but via `IRP_MN_START_DEVICE`.)
- PCI config space reads are for identification and quirk selection, not normal BAR discovery.
- Resource acquisition and release should be symmetric: map/allocate on start-controller, quiesce/free on stop-controller, following the lifecycle `usbport.sys` drives.

## Interrupt Delivery

- The driver depends on line-based (INTx) interrupt delivery on both targets. Neither Win98/NUSB `usbport.sys` + the 9x PCI driver nor Win2000's native `usbport.sys` + NT5 PCI driver has an MSI/MSI-X path, so Interrupter 0's INTx# assertion is the only way events reach software.
- INTx is optional in xHCI (spec section 4.17.3: "PCI Interrupt Pins are optional"). At init, read the PCI Interrupt Pin register (config 0x3D); if it is 0 the controller is MSI/MSI-X-only and is unsupported on both targets - abort with a diagnostic rather than binding to a controller that will never deliver an interrupt.
- **A pin that cannot be read is not a pin of zero.** The gate refuses pin 0; a failed config read of the Interrupt Pin register is logged and does not refuse. PCI derives a device's interrupt resource from that register, so a controller with no pin never receives the `USBPORT_RESOURCES_INTERRUPT` bit the resource check before this one already requires, and turning a service failure into a hard refusal would decline a working controller on no evidence.
- Do not program MSI/MSI-X capability structures. Even under the Option B fallback, neither kernel/HAL has MSI infrastructure to target.
- What does differ between targets is only the routing above the pin: Win98 and Win2000's PIC HALs (`hal.dll` / `halacpi.dll`) take INTx through the 8259 PIC, while Win2000's APIC HALs - including both ACPI Uniprocessor (`halaacpi.dll`) and ACPI Multiprocessor (`halmacpi.dll`) - route it through the IOAPIC. The miniport is unaffected either way - `usbport.sys` owns the interrupt object - but it is why the Phase 0 qualifier's PIC-mode C4 result is not equally strong evidence for both (`docs/contributing/design/01-hardware-qualification-tool.md`, "What a C4 PASS proves for each target").

Why there is no MSI on either target. MSI is an interrupt delivered as a memory write to the Local APIC's `0xFEE00000` window, where the address encodes the target APIC/CPU and the data encodes the IDT vector. Making that work requires the OS to allocate the vector, compute the APIC address/data, and program the device's capability - kernel/HAL-owned APIC machinery. Win9x is a PIC/IRQ-only platform (8259 IRQs 0-15 via the VMM, no APIC interrupt subsystem), so there is no service to target and a driver cannot bolt one on without breaking the kernel's own PIC-based delivery. This is not 9x-specific: the NT kernel gained MSI only in Windows Vista, so the Win2000-derived `usbport.sys` is line-based too. INTx is therefore the only delivery path on both counts.

## Interrupt Ordering

- Program the event ring and interrupter registers while controller interrupts are still masked.
- Under Option A, `usbport.sys` owns the interrupt object (the miniport does not call `IoConnectInterrupt`); ensure the miniport ISR/DPC callbacks are registered before setting `IR[0].IMAN.IE` or `USBCMD.INTE`. (Under Option B, call `IoConnectInterrupt` before enabling those bits.)
- The ISR callback claims the interrupt only when `USBSTS.EINT` or another owned status bit proves it belongs to this controller.
- **Mask `USBCMD.INTE` before `IR[0].IMAN.IE`; unmask in the reverse order.**
  The mask order is the one that matters: `DisableInterrupts` runs at
  DISPATCH_LEVEL under usbport's `MiniportSpinLock`, which does not exclude a
  DIRQL ISR, so an ISR racing it must cost a spurious interrupt rather than a
  delivered one. Every write to `IMAN` is a read-modify-write - `IP` is RW1C in
  bit 0, `IE` is RW in bit 1 and 31:2 are RsvdP, so there is no way to touch one
  without deciding the other.
  - The masking, unmasking and re-arming paths write `IP` as 0: a 1 in bit 0
    would acknowledge an interrupt nothing has seen. The ISR is the one writer
    that writes `IP` as 1, because acknowledging is what it is for -
    and it writes `IE` as 0 in the same word (see "the ISR therefore writes IE
    as 0" below).
  - **One documented exception to the read-modify-write rule, and only one:**
    when `XhciIsr` cannot read a valid `IMAN` operand at all - every read in a
    small bounded budget answers `0xFFFFFFFF` - it writes a literal
    `XHCI_IMAN_IP` and counts it (`IsrImanLiteralAcks`). There is no value from
    which a specification-compliant RsvdP write can be formed at that point, and
    both alternatives are worse: writing nothing leaves `IP` set behind an `EHB`
    only the DPC clears, so a shared level-triggered INTx this controller is
    asserting has no acknowledger at all, while deriving the write from all ones
    publishes every RsvdP bit as 1 and asserts `IE`.

    The literal is monotone in the direction every masking path moves: `IP`
    acknowledged, `IE` 0, reserved 0, which is the value a reset leaves. It is
    an emergency exception, not an ordinary compliant RMW, and no other site
    may take it.
- **Never derive a reserved-preserving write from an all-ones read, and write
  from the read that was validated.** An undecoding window answers `0xFFFFFFFF`;
  fed through the `USBCMD` RMW that becomes `0xFFFFFFFB` - `R/S`, `HCRST`,
  `LHCRST`, `CSS` and `CRS` asserted together on a controller that may only have
  returned one bad read - and through the `IMAN` RMW it publishes every RsvdP
  bit as 1. Validate each operand before the first write, and pass the
  validated value into the write rather than re-reading inside it; a window that
  dies between the guard's read and a second one supplies RsvdP as all ones.
- **A decline gate is only safe behind a mask that was actually applied.** The
  ISR declines at DIRQL without acknowledging, so every state that makes it
  decline - `INITIALIZED` cleared by quiesce, `ControllerFailed` published by
  `ResetController` - may be acted on only once delivery is provably suppressed.
  Masking before publishing is necessary but not sufficient: a mask whose window
  answered all ones wrote nothing, and a decline behind it strands an asserted
  shared level-triggered INTx that nobody will acknowledge.

  Publish whether the mask suppressed delivery (`InterruptDeliverySuppressed`),
  meaning either `INTE` or `IE` confirmed clear by a read back, since either
  alone stops delivery, and make the ISR's gates conditional on it. When it is
  not set, fall through and prove ownership from `USBSTS`, which declines a
  genuinely dead window by its all-ones read. "Can a register be touched at
  all" is a separate question, answered by `HcInfoStatus`, and nothing relaxes
  it.
- **A refused unmask must be escalated, not absorbed.** `EnableInterrupts`
  returns void and usbport calls it once after `StartController` - not after a
  successful resume - so a refusal is invisible and the start still counts as
  successful. Without it, one transient all-ones read leaves a started
  controller that never interrupts again. Retry the operand reads, and the
  write, a bounded number of times (no stall - the callback holds two spin
  locks), confirming from a read back rather than from the accessor returning,
  then request the usbport-managed controller reset after dropping the
  controller lock, since that is a usbport service.

  This escalation is containment, not repair, and must not be described as
  repair. `UsbPortInvalidateController(RESET)` queues a DPC that calls the
  miniport's own `ResetController` at DISPATCH inside a usbport lock, and
  usbport does nothing afterwards but release it (`docs/usb-xhci-info/usbport-miniport-abi.md`,
  "`UsbPortInvalidateController(RESET)`: real in the binaries"). This driver's
  `ResetController` marks the controller terminally failed.

  So the escalation converts a silently non-interrupting controller into a
  visibly failed one and stops the command engine cleanly. The only path that
  actually restores service is a stop/start, which no miniport can initiate.
- **On a refusal, mask and unmask go opposite ways, and that is intended.**
  Masking applies whichever half it could still derive: refusing a derivable
  `INTE` clear leaves the enable up, and `ResetController` publishes
  `ControllerFailed` immediately after, so the ISR then declines a still-asserted
  shared level-triggered INTx without acknowledging it - the livelock the mask
  order exists to prevent. Unmasking refuses both halves together, because a
  half-enable buys nothing and a refusal leaves the interrupt off rather than
  live. Note what that does not mean: nothing re-tries it on its own (see
  the escalation bullet above), so the refusal is safe but not self-correcting.
  State the direction in any new masking path; symmetry is the wrong instinct
  here.
- **Enabling interrupts must release Event Handler Busy, and must acknowledge
  nothing.** By the time usbport calls `EnableInterrupts` the controller has
  been running and its ports powered, so a device already attached has produced
  its Port Status Change Event and the interrupter has reacted to it with the
  enables still masked: `IMAN.IP` = 1, `USBSTS.EINT` = 1, and `ERDP.EHB` = 1,
  because EHB "shall be set to `1` when the IP bit is set to `1`" (5.5.2.3.3,
  p.394). So the enable path writes `ERDP` with EHB = 1 at the unmoved
  dequeue pointer - consuming no event - before setting either enable, while
  both are still clear and no DPC can be in flight to race the write.
  Acknowledging instead is fatal in either half:
  - Clearing `IP` with EHB still set leaves no path back to `IP` = 1 ("when
    IMODC transitions to `0`: if EHB = `0` and IPE = `1`, then IP shall be set
    to `1`", 4.17.5, p.270). Only the DPC's final `ERDP` write clears EHB, and
    the DPC only runs off an ISR that returned `TRUE` - so the events already on
    the ring are stranded for the life of the driver.
  - Clearing `EINT` while `IP` stays set is worse: `EINT` follows `IP` `0` to
    `1` transitions, so no further one occurs, the ISR reads `EINT` = 0 and
    declines its own controller's interrupt - on a line that "remains asserted
    until the device driver clears the Interrupt Pending (IP) flag" (4.17.3,
    p.268). That live-locks the whole IRQ, not just this device.
- **Every `ERDP` writer holds the controller lock.** The DPC owns the software dequeue pointer
  and relies on its own intermediate publications carrying EHB = 0; a second
  writer can publish a pointer the DPC has already moved past and can clear EHB
  in the middle of a drain. Any callback that wants to acknowledge interrupt
  state therefore inherits that constraint, because the acknowledgement and the
  EHB release are inseparable (above).

  `EnableInterrupts` qualifies only because both enables are still clear when
  it writes, so no DPC can be in flight. `FlushInterrupts` does not: all three
  shipping builds call it from the D0 power completion holding neither miniport
  lock (`docs/usb-xhci-info/usbport-miniport-abi.md`), so it touches no
  register at all until the miniport has an interior lock over event-ring
  state. Masking interrupts around such a callback is not a substitute: it does
  not exclude a DPC already running on another CPU.
- The resume path owes the same release. usbport calls `EnableInterrupts` after
  it restarts a controller whose resume failed, never after a successful one,
  so a resume that reinitializes and runs the controller itself is the only
  thing that can release the interrupter it just left busy.

## Event Ring Draining

- **A queued DPC object is coalesced.** If the controller interrupts again
  before the DPC is dequeued, another request for that same object is ignored
  (Oney p.206). Therefore the callback owns all events accumulated since its
  prior run, not one interrupt's snapshot. Once dequeued, the object can be
  queued again even while its callback is executing. Under Option A,
  usbport's `MiniportInterruptsSpinLock` serializes miniport
  `InterruptDpc` callbacks; confirm the NUSB binary retains that contract in
  the Phase 3 spike.
- The DPC must drain until the Event Ring Cycle Bit indicates empty.
  During a long burst, publish progress by updating `ERDP` periodically with
  EHB left set (the project uses every 32 events); after observing empty,
  write the final dequeue pointer with EHB = 1 to clear Event Handler Busy.
  EHB is RW1C: an intermediate write carries 0 in bit 3, which leaves EHB
  set and keeps the interrupter suppressed; writing 1 mid-drain would clear
  it (`docs/usb-xhci-info/xhci-data-structures.md`, ISR/DPC rules).
  Do not stop after one event or after a fixed interrupt-time snapshot.
- **The drain is nevertheless bounded, and the bound is safe.** A controller
  producing events faster than software consumes them must not own a CPU at
  DISPATCH_LEVEL, so the DPC stops after a fixed number of events (this
  project: four laps of the event ring). Stopping early does not strand the
  rest, because the interrupter re-asserts by itself: its internal IPE flag
  "shall be cleared to `0` ... if the Event Ring transitions to empty" and is
  set by every event inserted, and "when IMODC transitions to `0`: if EHB =
  `0` and IPE = `1`, then IP shall be set to `1`" (4.17.5, p.270).

  What makes that work is that the final ERDP write with EHB = 1 is
  unconditional, taken on the bounded exit and on a pass that found the ring
  empty. A pass that skipped it because it had nothing to do would leave Event
  Handler Busy set and silence the interrupter permanently.
- Keep the miniport ISR stateless: acknowledge USBSTS.EINT then IMAN.IP and
  return `TRUE`; let usbport request the DPC. The Event Ring itself is the
  pending-work queue, so no ISR-to-DPC bit mask or `InterlockedOr` is needed.
  The EINT-then-IP order is the spec's, stated as a rule for this
  software: "Software that uses EINT shall clear it prior to clearing any IP
  flags. A race condition may occur if software clears the IP flags then clears
  the EINT flag, and between the operations another IP `0` to `1` transition
  occurs. In this case the new IP transition shall be lost" (5.4.2, p.364).
- **IMAN can only be acknowledged by a read-modify-write, and the ISR resolves
  that window by direction rather than by a lock.** IP is RW1C in bit 0, IE is
  RW in bit 1, and 31:2 are RsvdP, so clearing IP means deciding IE. The ISR
  runs at DIRQL and can take no DISPATCH-level lock, so it is excluded from
  nothing: every masking path (`DisableInterrupts`, quiesce, suspend, the
  terminal failure transition) runs under the controller lock, and an ISR that
  carried IE through from its read would undo a mask that landed between that
  read and its write.

  The ISR therefore writes IE as 0. It can lower the bit
  and never raise it, which is the same direction every masking path moves it,
  so no interleaving exists in which the ISR resurrects an enable and the two
  contexts need not exclude each other. This costs no delivery: the xHC set EHB
  when it set IP, and IP cannot be set again while EHB is set (5.5.2.3.3 p.394;
  4.17.5 p.270), so nothing can be generated in that window whatever IE holds.
  IE clear is the normal state between an interrupt and its DPC.
- **Because the ISR clears IE, the DPC's re-arm is the sole restorer of
  delivery on a running controller, and it carries every rule the unmask path
  does.** usbport calls `EnableInterrupts` once after `StartController` and not
  again while the controller runs, so a re-arm that is skipped or swallowed is
  permanent silence rather than a missed cycle.

  `XhciRearmInterrupter` therefore refuses an all-ones read as an operand and
  as evidence (all ones has IE set, so reading it as "already armed" is the
  worst available interpretation, and was a real defect). It writes from the
  validated read, confirms IE by read back, retries a bounded number of times
  without stalling, and reports failure so the DPC can escalate through
  `UsbPortInvalidateController(RESET)` after dropping the lock. It touches
  `IMAN` only, so it must not update `InterruptDeliverySuppressed`, which is a
  claim about both enables.

  Generalisation: any path that becomes the only writer of an enable inherits
  the whole operand/read-back/escalation contract, not just the write.
- **Only the DPC raises IMAN.IE again, and it needs two conditions.** usbport's
  `enableInterrupts` BOOLEAN was sampled before the DPC was queued, and the DPC
  holds `MiniportInterruptsSpinLock` while `DisableInterrupts` holds
  `MiniportSpinLock` - different locks - so that argument can be a stale TRUE
  after a mask on another CPU. Re-arm only when it is TRUE and
  `XHCI_EXT_FLAG_INTERRUPTS` is set, the latter maintained under the same
  controller lock the mask holds. Anywhere that sets IE must write IP as 0
  - the bit is RW1C, and a 1 would acknowledge an interrupt nothing has seen.
  Keep masking in the order `USBCMD.INTE` first, then `IMAN.IE`: with the ISR
  monotone that is defence in depth rather than the whole defence, but it is
  what makes a mask that could derive only one of its two operands safe.
  This also avoids depending on `InterlockedOr`, which is absent from the
  installed Win2K DDK headers (Oney describes it as new in the XP DDK).
- The DPC callback and submit/abort callbacks can run concurrently because
  usbport protects them with different locks. Transfer-ring producer and
  consumer metadata therefore takes the controller lock - and because
  `UsbPortCompleteTransfer` is a usbport service, the drain must retire under
  the lock, thread completed transfers onto a list, release, and complete them
  afterwards. The design and the storage that list has to use are in
  `docs/contributing/design/05-locking-model.md` section 7.

## Wait Primitives

- **PASSIVE_LEVEL lifecycle waits** (BIOS handoff, halt/reset, CNR clear)
  use the registration packet's `UsbPortWait(milliseconds)` service under
  Option A. It is implemented with `KeDelayExecutionThread` and blocks the
  start/reset worker thread, which usbport expects. Under Option B, call
  `KeDelayExecutionThread` directly. Always use a bounded,
  specification-derived timeout.
- **Root-hub callbacks do not sleep.** `RH_SetFeaturePortReset` runs at
  DISPATCH_LEVEL. In the ReactOS contract, the Set/Clear feature callbacks
  are not protected by usbport's `MiniportSpinLock`; do not infer
  serialization from the locked status-query callbacks. Set PORTSC.PR,
  schedule a timeout through `UsbPortRequestAsyncCallback`, and return.
  Protect reset state shared with `InterruptDpc` using the miniport's own
  interior lock.
- **An async timeout is uncancellable and unprotected.**
  `UsbPortRequestAsyncCallback` invokes its callback from a timer DPC without
  either usbport miniport lock. Keep an armed flag and monotonically
  increasing current generation in miniport-owned state, and pass that
  generation in the copied callback context. Under the miniport's own lock,
  let exactly one of the Port Status Change Event/PRC path and the matching
  timeout claim completion; stale generations and callbacks after controller
  stop must return without touching MMIO or publishing a change. The timer is
  only the missing-event backstop.
- For port power, write PP and return; advertise the required 20 ms
  power-on-to-good delay in `RH_GetRootHubData` so the hub stack waits before
  touching the port.
- **Short register-settle waits** (< about 50 us) use
  `KeStallExecutionProcessor`, which busy-waits and is callable at any IRQL.
  It may run long (higher-IRQL activity preempts it), so never use it as a
  precise timer (Oney p.101).
- **Never busy-wait milliseconds at DISPATCH_LEVEL or above.** Do not put a
  controller-reset timeout or a USB port reset delay in a callback holding a
  usbport spin lock.
- **The one exception is the recovery path, and it is declared.** With
  `ext->InitBelowPassive` set, `XhciInitController` runs at DISPATCH_LEVEL from
  the `UsbPortRequestAsyncCallback` DPC the health poll armed, holding no
  usbport lock, and every bounded wait in the init sequence stalls
  (`KeStallExecutionProcessor`) instead of sleeping; `XhciDelayMs`
  (`src/xhci_pci.c`) is the only fixed delay and takes the stall form there
  too, 20 ms once per attempt. The flag is a contract: services documented
  PASSIVE_LEVEL-only are skipped under it, not risked (see "Fatal Errors").

## MMIO Sanity

- An all-ones read (0xFFFF from HCIVERSION, 0xFFFFFFFF from any 32-bit register) is never valid data - it means the device is not decoding the access (Memory Space Enable clear, device in D3, BAR not mapped, or device removed).
- Validate HCIVERSION and CAPLENGTH for plausibility immediately after mapping BAR0, before any other register access; abort init with a diagnostic on all-ones or a zero CAPLENGTH.
- Validate `RTSOFF` and `DBOFF` with subtraction-based bounds checks
  (`offset <= mapped_size - required_bytes`) so a high invalid offset cannot
  wrap during `offset + size`. Reject zero and all-ones values before deriving
  runtime or doorbell pointers.
- Verify `PAGESIZE` (operational + 0x08) reports 4 KB support (bit 0) before allocating page-sized structures; the allocator assumes 4 KB pages.

## Port Speed Decoding

- The `1 = FS, 2 = LS, 3 = HS, 4 = SS` PORTSC Port Speed encoding is only the
  default. A Supported Protocol capability with PSIC > 0 defines its own
  Protocol Speed IDs, and the PORTSC value indexes that table (spec 7.2.2.1.2;
  layout in `docs/usb-xhci-info/xhci-data-structures.md` section 6).
- Read the PSI dwords during the Phase 4 port classification and decode speeds
  against them, falling back to the defaults only when PSIC = 0. Do not hardcode
  the defaults: the fleet's Intel controllers ship a table whose entries happen
  to match, so a wrong assumption fails silently until a controller that
  reorders them appears.
- Retain all 15 entries allowed by the four-bit PSIC field. A PORTSC PSIV absent
  from a non-empty advertised table is unknown, not a default ID. Any functional
  decision derived from speed (including EP0's initial Max Packet Size) must use
  the decoded speed class while the Slot Context continues to receive the raw
  controller PSIV.
- **A device behind a hub has no PORTSC to read a PSIV from, and usbport reports
  a speed class, so its Protocol Speed ID is looked up by inverting the same
  PSI table** (`XhciPortPsivForSpeed`) - refusing rather than defaulting when
  the table has no entry for that class. "3 means High Speed" is what a
  controller that reordered its IDs would break.

## DMA Teardown

- Do not free or reuse controller-visible memory until hardware reset has
  completed or PCI Bus Master Enable is confirmed clear. If neither can be
  proven, retain the allocation handles, prevent later cleanup from freeing
  them, mark the controller cleanup as failed, and require a cold boot.
- **Under Option A the miniport cannot retain those handles, so on a path that
  reclaims it fails closed instead.** `usbport.sys` owns the common buffer and
  frees it as soon as `StopController` returns; the callback returns `VOID` and
  there is nothing to withhold. Recording the failure and returning was the
  first answer and it is not sufficient - a counter describes the corruption, it
  does not prevent it, and the corruption lands in whichever driver the pool
  hands those pages to next, silently and misattributed. So a quiesce that can
  prove neither halt nor BME calls `UsbPortBugCheck` (registration packet
  `+0x11C`), which is what "require a cold boot" means on a target. Write the
  counters and the trace before the call: the service passes four hard-zero
  bugcheck parameters and does not return.
- **The rule is per reclaiming path, not per callback.** `StopController` and
  both failure exits of `XhciInitController` reclaim; each escalates, and each
  escalates every time, because every reclaim is its own freed buffer and there
  is nothing for a latch to save. Suspend does not, by design: it leaves
  the buffer allocated to this driver, so an xHC still mastering is writing into
  pages nobody else will be given. Count that; do not take the machine down for
  it.
- Retry the BME fallback (`XHCI_BUS_MASTER_ATTEMPTS`) before concluding it
  failed. Every way a single attempt fails is a transient on some machine, and
  the answer to a final refusal is now a bugcheck, so one flaky config cycle
  must not be what decides it. A bit that genuinely will not clear reads back
  set on every attempt, so the retry cannot manufacture a proof.
- **"usbport is waiting on it" is a reason to answer a transfer eventually,
  never a reason to answer it now.** The transfers of an abandoned record stay
  queued on the same condition as the record, and are answered by whichever
  path proves the controller stopped: `XhciSlotInit` runs after HCRST - where
  there is no ring, no enabled slot and no DCBAA entry left to execute them - and
  drains them for the resume's deferred drain to complete. Run from the
  write-nothing preflight at the top of `XhciInitController` instead, a refusal
  would drop every record while the controller kept its slots.
- A teardown reset failure is distinct from the initial C2 result. Preserve C2
  as observed and report the cleanup failure as its own disqualifying outcome.

## PORTSC Writes

- Treat PORTSC change bits as RW1C.
- Every PORTSC write must mask out change bits unless the intent is to clear those exact bits.
- Manage only USB 2.0 protocol ports. USB 3.x protocol ports are out of scope: leave them unpowered and unmanaged.
- **PORTSC may only be written while the controller is running.** "Software
  shall ensure that the xHC is running (HCHalted (HCH) = `0`) before attempting
  to write to this register" (5.4.8, p.371). So port power is set after
  `USBCMD.RUN`, never beside it - and conversely, anything that wants to clear
  port power at teardown has to do it before the halt.
- **"Left unpowered" is an action, not an omission.** Whether or not an
  implementation has port power switches, "it shall automatically enable VBus on
  all Root Hub ports after a Chip Hardware Reset or HCRST", leaving each port in
  the Disconnected state, "i.e. Port Power (PP) is asserted" (4.19.4, p.295). A
  driver that simply does not touch the USB 3.x ports therefore leaves them
  powered, SuperSpeed devices train onto ports nothing services, and the USB 2.0
  fallback the port strategy depends on never happens. Drive PP to 0 on every
  USB 3.x protocol port explicitly.
- **Power the USB 2.0 ports before unpowering the USB 3.x ones.**
  "Implementations shall OR together the output of the PORTSC register Port
  Power pins for Root Hub Ports that map to the same Physical USB Connector"
  (4.19.7 implementation note, p.303), so on a connector whose two halves are
  both being written, deasserting first drops VBus for the width of the loop.
- **HCCPARAMS1.PPC does not gate whether PP is written.** It says whether the
  controller has power switches. "Software cannot change the state of the port
  unless Port Power (PP) is asserted (`1`), regardless of the Port Power Control
  (PPC) capability" (5.4.8, p.371), and at PPC = 0 a port with PP = 0 "is
  nonfunctional and shall not report attaches, detaches, or Port Link State
  (PLS) changes" (Table 5-27, p.375) even with VBus hard-wired on. Set PP on the
  managed ports and clear it on the USB 3.x ports at either value of PPC.
- **A `0` to `1` transition of PP owes 20 ms.** "The host is required to have
  power stable to the port within 20 milliseconds of the `0` to `1` transition
  of PP. If PPC = `1` software is responsible for waiting 20 ms. after asserting
  PP, before attempting to change the state of the port" (5.4.8, p.371). One
  wait after a whole port pass covers every transition in it, since they were
  all written before it began. A port that never reaches its target is a port,
  not a controller: count it and carry on.
- **PP is allowed to lag the write, in either direction, so poll for it.** "A
  port implementation shall initiate a Port Power change immediately when PP is
  written, however the PP flag may be delayed in reflecting this change" (Table
  5-27 footnote 91, p.375). This is a separate allowance from the 20 ms above
  and applies to deassertions too, which owe no flat delay - so a single
  readback after writing counts every lagging port as a failure. Poll each
  changed port boundedly, and confirm with "after modifying PP, software shall
  read PP and confirm that it is reached its target state before modifying it
  again" (same table) rather than assuming the write took.
- **The assertions must be confirmed, not merely written first, before any
  deassertion.** Ordering the writes is not enough on its own: if the USB 2.0
  half of a connector has not actually come up when the USB 3.x half goes down,
  the OR that feeds VBus is momentarily zero and the connector loses power
  anyway.
- **Take port power back off at teardown, and do it before the halt.** "Before
  the xHC driver is unloaded, the driver should clear the Port Power (PP) flag
  of all Root Hub ports to place them into the Disabled state and reduce port
  power consumption" (4.19.4, p.296). Because PORTSC is unwritable on a halted
  controller, this is the first step of the ordered teardown, not a tidy-up
  after it. Inside that pass there is no ordering obligation - every port of a
  connector is going down, so the VBus OR reaching zero is the intent.
- **Gate the teardown pass on two separate facts, and count a skip apart from a
  failure.** Whether this driver ever wrote R/S decides whether the power is
  this driver's to take (a start refused in the preflight, or one that declined
  because the controller would not stay halted, has claimed nothing). A live
  `USBSTS.HCH` read decides whether the write is legal now. Neither implies the
  other. A pass that could not run legally has left port power up, which is a
  wasted watt and not a fault; a port that took the write and ignored it is
  hardware misbehaviour. They look identical in the port state and must not
  share a counter.
- **A port the driver has no opinion about keeps that answer at teardown too.**
  The start pass leaves an unclassified port (`XHCI_PORT_CLASS_NONE`) alone
  because writing and not writing are both statements. A stop is routinely
  followed by a start, and that start would leave it alone again - so clearing
  PP at teardown would be a permanent one-way change made on a stop/start cycle.
  Read 4.19.4's "all Root Hub ports" as all the ports this driver powered, which
  is the only set it can put back.
- **A suspend is not an unload and must not unpower the ports.** Win98's NUSB
  `usbport.sys` issues suspend/resume pairs repeatedly at idle; dropping VBus on
  every connector there would re-enumerate every attached device on each pair.
- **On the measured shutdown ordering the pass cannot run at all, and that is
  recorded, not worked around.** usbport's clean shutdown is
  `SuspendController` -> `DisableInterrupts` -> `StopController(TRUE)`
  (`docs/contributing/lessons.md`, Phase 3 task 8), so the stop routinely arrives on a
  controller the suspend has already halted, with `PORTSC` unwritable.

  Do not restart the controller to reach the write. It would set R/S on a
  controller whose common buffer `usbport.sys` reclaims the moment the callback
  returns, put the driver's most safety-critical path behind a second halt that
  can fail, and risk a bugcheck on a machine that was shutting down cleanly,
  all to honour a "should" about power consumption on the one path where the
  machine is about to lose power regardless.

  The case 4.19.4 is written about is a driver disabled or unloaded while the
  machine keeps running, and there the controller is still running when the
  stop arrives. Count the skip under its own counter so a release build can
  tell it from a start that never ran.
- **Do not restate a downstream admission gate at the top of a lifecycle
  entry point.** Each step of the teardown already refuses the states it must
  not act in. A pre-emptive copy of one of those checks added nothing and
  swallowed the case it was not written for - a stop on a suspended
  controller has neither `RUNNING` nor `INITIALIZED`, so it returned before the
  port pass could record that it had been skipped, which on the measured
  ordering is every ordinary unload. One gate per concern, applied where the
  concern is.

## Root Hub Reporting

The callback contracts these rest on are binary-confirmed
in `docs/usb-xhci-info/usbport-miniport-abi.md` section 4; what is here is what the miniport
must therefore do.

- **Every latch site that is not itself a report must queue an announcement.**
  There are three: the start/resume seed, the Port Status Change Event path, and
  `RH_GetPortStatus`. The first two latch into a shadow nobody is looking at, so
  the change is announced (`UsbPortInvalidateRootHub`) or it is lost - and lost
  permanently, because both acknowledge the PORTSC bit as they go and the
  controller does not repeat itself. `RH_GetPortStatus` is the exception: it
  latches and reports in the same call and owes nothing. The seed's case is
  the one that looks optional and is not: on a start, usbhub's initial scan
  would probably find an unannounced connect anyway, but after a resume the
  root hub already exists, nothing rescans it, and a device plugged in during
  the suspend is simply missing.
- **A change is acknowledged in hardware immediately and latched in software
  until the hub class asks for it.** The two halves are not alternatives and
  dropping either loses a connect. An unacknowledged PORTSC change bit
  suppresses the controller's next Port Status Change Event for that port -
  measured on QEMU, where 24 hot-plug operations produced 6 events
  (`docs/contributing/lessons.md`, "hot-plug operations are not hot-plug
  events") - so the RW1C bit goes down the moment it is
  read. The hub-class change it implies is then held in the port shadow and is
  cleared only by the matching `RH_ClearFeaturePortXChange` callback,
  because usbport may poll long after the event and the hub class has no other
  way to be told.
- **A status query returns `MP_STATUS_SUCCESS` even when it has nothing to
  report, and even when the controller is not in service.** The status-change
  endpoint's scan treats any nonzero return from `RH_GetPortStatus` or
  `RH_GetHubStatus` as a hard error, abandons the whole scan and leaves the root
  hub's change pipe stalled on every future poll. Report zeros and succeed. The
  only nonzero either may answer is for a NULL output buffer, where there is
  nothing to report into.
- **A refusal is `MP_STATUS_NOT_SUPPORTED`, never `MP_STATUS_FAILURE`.** The
  mapper is seven instructions in both shipping builds and maps 1 - and only 1 -
  to `RH_STATUS_NO_CHANGES`, which leaves an endpoint-0 request queued instead
  of failing it. That is a hang rather than an error.
- **Validate the port index in the miniport.** usbport validates
  `1 <= wIndex <= bNumberOfPorts` on the class-command path only: the
  status-change scan synthesizes 1..N itself, and Win2000's hub-directed feature
  path passes `Port = 0` to `RH_ClearFeaturePortOvercurrentChange`. That one
  must tolerate zero rather than index with it, and must not refuse it - the path
  runs on ordinary hub traffic.
- **`NumberOfPorts` is never 0, in any state.** usbport sizes the root-hub
  descriptor's masks as `((NumberOfPorts - 1) >> 3) + 1` with no guard in either
  shipping build, so zero unsigned-wraps into a request for about 1 GB of
  nonpaged pool. A driver that does not know its port count reports one
  permanently disconnected port. This applies to the refusal paths too: an
  unrecognisable extension must still fill the caller's structure, because
  usbport builds a descriptor out of it either way.
- **`C_PORT_SUSPEND` is a completed resume, so it is only derivable on a port
  that could have completed one.** Derive it only when the new reading still
  shows the port enabled (`PED`) and connected (`CCS`). The case is a stale
  `PLC`: it is RW1CS, so a legitimate one - the U3-to-Resume announcing a
  device's wakeup - stays set until software writes a 1, and that acknowledgement
  is deferred while a Port Power change is in flight.

  Once the port is out of service, `PLS` no longer reads Resume ("this field is
  undefined if PP = `0`", p.374; `PED` and `PR` likewise read `0`, p.371), so
  previous-Resume plus current-not-Resume plus a set `PLC` is the shape of a
  device waking up on a port whose power was taken away under it.

  A disable does not set `PLC`. "This flag is set to `1` due to the following
  PLS transitions" (p.378) enumerates them, and a disable is not among them;
  the row's closing exclusion narrows that list, it does not define it.
- **Never expose a raw xHCI bit as a hub-class bit.** The two vocabularies have
  different positions and different meanings: hub-class suspend is a PLS field
  value, and hub-class `C_PORT_SUSPEND` is a completed resume while xHCI has no
  such bit (only PLC, which is any link-state change - so it takes a comparison
  against the previous state).
- **Every connected managed root port is reported to usbport as High Speed,
  whatever it decoded, and this is the one place the driver knowingly tells
  usbport something untrue.** `XHCI_HUB_PORT_LOW_SPEED` is never set at all.
  usbport applies the EHCI model, in which a root port can only have a High Speed
  device enabled because Full and Low Speed devices are released to a companion
  controller.

  Told otherwise, `USBPORT_CreateDevice` looks for the transaction translator
  that model guarantees, and `USBPORT_GetTt`'s `TtCount <= 1` branch
  `CONTAINING_RECORD`s an empty `TtList` into `0xFFFFFFEC` instead of returning
  NULL, which survives `USBPORT_OpenPipe`'s null check and bugchecks both
  primary targets. The empty-list guard exists only on the multi-TT branch, and
  ReactOS's early returns are in neither shipping build; the instruction-level
  derivation is `docs/usb-xhci-info/usbport-miniport-abi.md` section 8, "The
  transaction-translator lookup".

  Three consequences bind the rest of the driver:
  - **The untruth stops at the reporting layer for everything the miniport can
    still see, and the endpoint interval is the one thing it cannot.** The port
    shadow keeps the decoded class, and that - never usbport's `DeviceSpeed` -
    is what the slot and endpoint contexts and EP0's max packet size must be
    programmed from, because usbport now derives them on High Speed rules.

    The interrupt interval is not recoverable and Phase 7 must not assume it
    is. usbport branches on `DeviceSpeed` when it converts `bInterval` into
    `EndpointProperties->Period`: a device it believes is High Speed goes
    through `USBPORT_NormalizeHsInterval`, which is `1 << min(bInterval-1, 5)`,
    where the truthful Full/Low Speed path would have used `bInterval`
    milliseconds directly. `USBPORT_ENDPOINT_PROPERTIES` carries no raw
    `bInterval`, so the miniport receives only the result - and the result is
    lossy, because every true `bInterval >= 6` collapses onto the same clamped
    32. The error is always in the slower direction, so it costs latency and
    never over-commits bandwidth or violates the protocol, but a true 8 or 10 ms
    HID endpoint arrives as 32 ms:

    | true `bInterval` (ms) | truthful `Period` | `Period` under the override |
    |---|---|---|
    | 1 | 1 | 1 |
    | 2 | 2 | 2 |
    | 3 | 2 | 4 |
    | 4 | 4 | 8 |
    | 8 | 8 | 32 |
    | 10 | 8 | 32 |
    | 16 | 16 | 32 |
    | >= 32 | 32 | 32 |

    Phase 7 therefore inherits a decision, not a fix: either accept the latency,
    or have the miniport choose its own xHCI interval for Full and Low Speed
    interrupt endpoints - which is legal, since `bInterval` bounds the maximum
    service latency and polling sooner is permitted, but is a heuristic because
    the true value is gone. Do not write code that claims to reconstruct it.
  - **It is gated on a connection**, because the speed bits mean nothing without
    one and an empty port claiming High Speed would be a second untruth rather
    than the one that was argued for.
  - It does not cover a hub with no transaction translator plugged directly
    into a root port, and the cost there is an unenumerable device rather than
    a kernel bugcheck. The bugcheck was predicted: a USB 1.1 hub, or a 2.0 hub
    declaring `bDeviceProtocol = 0`, is marked High Speed by this same rule
    while its own `TtCount` stays 0, so the first Full or Low Speed device
    plugged into it looked certain to re-enter the identical fault one level
    down.

    Measurement refuted that, in batch 7b-V0, on both targets. A QEMU
    `usb-hub` (USB 1.1, no TT) on a root port with a Full Speed mouse behind
    it faulted on neither stack: `HubAddr` came back as the hub's own address,
    the documented "the TT lookup succeeded" reading, so a TT record existed
    for a hub that has none. The prediction had been reasoned from the
    binaries before the topology had ever been attached; a disassembly can say
    what a function does with an empty list, and not whether the list is
    empty.

    The reading of why (the override marks the hub High Speed, and a
    believed-HS hub is given `USBPORT_Initialize20Hub`) is not itself
    confirmed, and is open question 6 in
    `docs/contributing/design/02-hub-topology-route-string.md`. It matters: if
    it holds, every TT field this driver fills for a device behind a 1.1 hub
    describes a translator that does not physically exist.

    What batch 7b-V0 did measure was that the device behind such a hub did not
    enumerate: no slot, no endpoint, and an unbounded requeue-and-retry that
    reached 1,803 refusals and a dead guest on Win98 and 17 with the machine
    intact on Windows 2000. Batch 7b-A bounded the refusal, and behind-hub
    devices now enumerate on both targets: the batch verdict run addressed
    three of them two tiers deep with zero refusals.

    The TT pair is derived from the topology graph, which knows each hub's
    decoded speed, with usbport's pair kept only as a cross-check, so the
    physically absent translator is compared against and never programmed.
    There is still no interception point: `USBPORT_CreateDevice` calls `GetTt` before any
    miniport callback for that device, so this driver is never asked; the
    graph is how the driver answers the question usbport cannot be trusted
    with. A genuine USB 2.0 hub is unaffected: it really is High Speed and
    really is given a TT.

- **Because the report no longer distinguishes the speeds, the decode must be
  recorded where it still exists.** This is not optional bookkeeping: the Phase 5
  checkpoint asks for LS, FS and HS to be observed on a target, and after the
  override `RH_GetPortStatus` answers `0x0503` for all three, so nothing usbport
  is told - and nothing in the trace that follows from it - can tell them apart.

  The evidence is `RhSpeedsSeen`, the set of speed classes this start has ever
  decoded (`XHCI_RH_SEEN_LOW` / `_FULL` / `_HIGH`), OR-ed in `xhciRhRefresh` the
  first time each appears and traced there with `StartEpoch` and the port that
  supplied it. `XHCI_RH_SEEN_HIGH` is the negative control that distinguishes an
  all-HS bus from one where a slow device attached.

  The set is cleared only by usbport zeroing the extension before a
  `StartController`, so each start accumulates its own evidence, and
  `StartEpoch` in the trace value names which controller and which start a
  line came from.

  There is no periodic print of it, by design. Every trace macro's state
  is per expansion and therefore per driver image, while these values are per
  controller: two xHCI controllers whose sets differ alternate at a shared
  `XHCI_DBG_VALUE_CHANGED` site, every sample counts as changed, and the budget
  goes in about eight seconds at a nominal 500 ms callback (sooner at the
  measured 36-80 ms). Being monotone per
  controller does not make a site quiet. A site gated on a monotone set with no
  budget at all cannot be exhausted by any number of controllers, so the
  one-shot line is the whole of it.

  It is a monotone set rather than a count or a snapshot, and it has to be.
  Every trace macro is bounded by a per-site driver-image static that no
  start, stop or resume resets, so a witness that moves for reasons unrelated
  to the question has spent its budget by the time an operator plugs the
  device in.

  Two earlier shapes failed there and are recorded so they are not
  reintroduced. A running count moved on every resume, because the root hub is
  rebuilt and every connected port re-decoded. A tally of what is attached now
  moved too, because a resume genuinely takes the bus down (HCRST clears `PP`
  on every port, the ports are re-powered, and a device is not re-detected the
  instant the seed reads it), so it dips and returns.

  Change-gating rescues neither once more than one port is populated, since
  the distinct values cycle round and each differs from the one before it.
  Win98 idle-suspends within about half a second of a start, so this is the
  ordinary case there, not a corner. A set changes at most once per member for
  the life of a start, whatever the bus does.

  `RhFirstDecodes` counts the firings and must equal the number of bits in the
  set. It exists because a set makes its own gate invisible: OR is idempotent,
  so a site firing on every decode leaves the value identical while spending
  the print budget the design is about. It is the only way a host test - which
  compiles trace macros away - can see that.

  SuperSpeed still has no hub-class encoding, and an undecodable speed is still
  `XHCI_SPEED_UNKNOWN` in the shadow rather than a guessed default - what changed
  is only what usbport is told.
- **`RH_DisableIrq`/`RH_EnableIrq` are a software gate on the announcement,
  never `IMAN.IE`.** That bit gates the whole interrupter and would silence
  transfer completions with port changes. A close is not guaranteed a matching
  open - the scan's early return and both of its non-empty exits bypass the
  enable - so no state may be held back waiting for one; a change arriving while
  the gate is closed stays latched in the shadow where the next status query
  finds it.
- **`UsbPortInvalidateRootHub` is never called while the miniport's own lock is
  held, and that is a self-deadlock rather than lock-order hygiene.** The service
  calls `RH_DisableIrq` straight back into the miniport
  (`external/reactos/usbport/roothub.c:916-956`), which takes that same
  non-recursive spin lock. Decide the announcement under the lock and make the
  call after releasing it.
- **One invalidation drains every owed change.** It makes usbport re-poll all
  ports - the status-change scan walks 1..N calling `RH_GetPortStatus` - so a
  second call asks for a pass the first already covers. And a change latched
  while the gate is closed is not lost by being unannounced: usbport closes that
  gate by taking a notification, so a closed gate means a scan is already
  outstanding and it reads the shadow when it runs.
  - So `RootHubInvalidatesOwed` is a "there is news" flag with a count attached
    for diagnosis, and `RootHubInvalidates`/`RootHubInvalidatesGated` are the
    two outcomes. The one enable site is that scan's own no-changes exit - the
    moment usbport becomes interested again - and the health poll re-attempts
    the drain each interval, so a change latched while the gate was closed is
    announced when it re-opens without another port event to carry it.
    `RH_GetPortStatus` announces nothing: it latched and reported in the same
    call, so it owes no poll.
- **A port operation returns as soon as its write is issued, and reports
  completion through the change bits.** PP may lag in either direction, a
  suspend takes effect when the link reaches U3, a resume when it reaches U0.
  These callbacks run at DISPATCH_LEVEL and must not wait, so a read back taken
  immediately would report the old state on a conforming controller.
- **An uncancellable callback proves ownership before it touches a register,
  not after.** Every port timer fires - `UsbPortRequestAsyncCallback` keeps no
  list and offers no cancellation - so arriving with nothing to do is ordinary
  input, and the order of "claim the generation" against "read the port" decides
  whether that ordinary case is inert. A first version of the reset watchdog read
  PORTSC first, on the reasonable argument that a deadline expiring is not
  evidence of failure (the event may simply not have been drained). The reading
  is right; taking it before the claim is not, because the refresh it performs
  claims whatever is armed on that port - so a timer left over from before a
  suspend arrived after the resume and completed a reset a fresh caller had just
  started. Claim, then read.
- **A port with an operation armed refuses every request, not only the ones that
  would have written.** The busy test belongs before any operation-specific
  evaluation. A resume requested on a port already mid-reset otherwise takes the
  "this port is not suspended" exit and reports success - true of the link
  state and a lie about the request, since the port is in the middle of an
  operation that will change it.
- **The armed state excludes the synchronous port operations too, and not all
  in the same way.** Power-off and disable preempt an operation in flight and
  proceed: they are the caller taking the port out of service, and refusing them
  would block usbhub's own recovery for a reset that is not finishing, which is
  a power cycle. Preempting is safe because retiring advances the
  generation, so the in-flight timer is stale before it fires. Suspend
  refuses; its own precondition happens to refuse in both reachable states
  (PED = 0 mid-reset, PLS = 15 mid-resume) and the exclusion must not rest on
  that accident. Power-on does neither - a port cannot be mid-reset without PP,
  and the neutral write carries no PR and no LWS.
- **Every way an operation can end must report it - and a report must follow an
  observation, never precede one.** These pull in opposite directions and both
  matter. An operation that stops with nobody told is worse than one
  that fails: usbhub waits out its own much longer timeout. But `C_PORT_RESET`
  means "the reset sequence is complete", so latching it while `PR` is still set
  is a false statement about the hardware with a concrete failure mode - usbhub
  clears the change, re-powers, starts a new reset, and the old reset's
  unacknowledged `PRC` is then observed and claims the new reset's generation,
  reporting it complete microseconds after it began.
- **Clearing `PP` or writing `PED = 1` ends a reset in progress, and the
  controller says nothing about it.** "Note that this flag shall not be set to
  `1` if the reset processing was forced to terminate due to software clearing PP
  or PED to '0'" (PRC, Table 5-27, p.377) - and the same exclusion is on CSC, PEC,
  PLC and WRC, so a software-initiated power-off produces no change bits at
  all and therefore no Port Status Change Event.

  Two rules follow. The driver that issued that write is the only thing that
  can report `C_PORT_RESET`, so it must; waiting for an observation the
  specification forbids the controller from producing is a reset that never
  completes. And it must report after the write, not before: the same latch
  made first is a claim that the sequence is complete while `PR` is still set.

  A resume interrupted the same way is abandoned rather than reported, because
  `C_PORT_SUSPEND` means a completed resume and no hub-class report is owed
  for one that did not complete.
- **The Port Power confirmation is state on the port, not a step inside one
  callback.** The rule is "before modifying it again" (p.375), so it binds
  every later writer, not the one that issued the change - and the writer that
  makes it reachable is the least conspicuous one: every value this family
  writes is built on a neutral base, and a neutral value carries `PP` as
  it reads, so an ordinary status query acknowledging a change bit on a port
  whose power-off is in flight writes `PP` back as 1 and cancels it.

  Record the outstanding confirmation on the port; hold back internal writes
  until a read shows `PP` at its target (which is also where the confirmation
  is made, once and on evidence); and refuse operations in that window rather
  than skipping their write, since answering success for something not
  performed is worse.

  Bound the wait: a `PP` that never arrives must not disable the port for
  ever, so the health poll gives up after the same interval the armed-operation
  age detector uses. One write without the confirmation beats a port that
  refuses everything for the life of the driver.

  Make the confirmation from the read, not from the write it guards. Putting
  it in the write path couples a rule about `PP` to whether something else
  happened to need writing, and a quiet port then never settles its wait.
- **A write that was held back is not an action performed.** Once writes go
  through a helper that can decline one, every caller that concluded something
  from "I wrote it" has to check that it did. Both resume-completion sites
  discarded that answer and counted the resume complete, then disarmed the port -
  so the signalling was never terminated and the counters said it had been.
  Where waiting is safe (T(DRSMDN) is a floor with no ceiling), keep the
  operation armed and re-time it; where the caller is retiring it anyway, count
  it abandoned rather than completed.
- **An interrupting operation ends nothing until it is observed to have taken
  effect - and that applies to whatever is armed, not only a reset.** A
  power-off or disable still in flight that retires an in-flight resume disarms
  the one thing that was going to write the terminating U0, leaving the port
  signalling with nothing to end it. Confirm each operation in its own target
  bit (`PP` for a power-off, `PED` for a disable - "there may be a delay in
  disabling or enabling a port", p.372) and retire nothing until it shows.
- **There is one way to read a port, and everything uses it.** Reading PORTSC
  obliges the reader to acknowledge the change bits it observed and fold them
  into the shadow; a second reader that looks at one bit and discards the rest
  drops connects on the floor. If a path needs a value from PORTSC, it goes
  through the refresh - the same rule as "one way to write it", and broken the
  same way: by a path added later for an unrelated reason.
- **The one sanctioned exception to that rule is an observation mode.**
  `xhciPassThru` reads the raw PORTSC array through `XhciReadPortsc` and does
  not acknowledge and does not fold. It is not a second reader added for an
  unrelated reason; it is the point of the instrument. Finding Q established
  that nobody had ever read PORTSC in the wedged state, and an instrument that
  acknowledged what it came to measure would destroy the evidence on the one
  boot that mattered.

  The three conditions it is granted on:

  1. The channel is shut by default. It ships in every flavour, but
     `XhciLogVerbosity` is 0 on every machine, and at 0 `xhciPassThru` returns
     `MP_STATUS_NOT_SUPPORTED` without reading a register or touching the
     caller's block. The exception is therefore reachable only on a machine
     whose owner switched it on and restarted.
  2. It is a read channel only: it announces nothing, latches nothing, and no
     other code in the driver consumes what it returns. The payload leaves the
     machine as bytes in a user-mode file.
  3. Its regression (`test_passthru_snapshot`) requires the register to be
     byte-for-byte unchanged afterwards and the port shadow untouched, so an
     accidental fold fails the build rather than quietly becoming a second
     reader. It runs in every host-suite run.

  Any other unacknowledged read is still the defect this rule describes. If
  the instrument is ever removed, remove this exception with it: an exception
  standing with nothing behind it is how a rule erodes.
- **And "after the write" is not "after the port stopped": read it back.** "A
  port implementation shall initiate a Port Power change immediately when PP is
  written, however the PP flag may be delayed in reflecting this change, e.g.
  due to waiting for a port related state machine to complete reset signaling"
  (footnote 91, p.375), and "after modifying PP, software shall read PP and
  confirm that it is reached its target state ... undefined behavior may occur if
  this procedure is not followed" (p.375).

  A reset in progress is the spec's own
  example of what delays a power-off, so the interrupting write is the case
  that may not have taken effect yet. Report the reset ended only when a
  read-back of `PP` confirms the port reached the Powered-off state;
  otherwise report nothing and leave it armed, where its existing deadline covers
  it.
- **A change bit that is already set belongs to whoever set it, so drain it
  before you conclude anything.** `PRC` is RW1CS: a reset that completed just
  before an interrupting call leaves it in PORTSC, and neither that call's write
  nor a retire clears it. Reporting the reset as preempted there leaves a genuine
  completion behind, and the next refresh attributes it to whatever is armed by
  then - usbhub re-powers, starts a second reset, and the first one's leftover
  completes it microseconds later.

  Fold the port in through the ordinary refresh
  first: it acknowledges the bit and, when the completion is real, claims the
  armed operation as the completion it was. Gate that on the same confirmation
  as the report, because the acknowledgement is a neutral write and a neutral
  value carries `PP` as it reads - one made while a `PP = 0` is in flight
  re-asserts the power just removed.
- **Confirm the bit the operation targets, not one the target state masks.**
  `PR` "is `0` if PP is `0`" (Table 5-27, p.371), so reading `PR` back after a
  power-off confirms nothing - it is 0 because power is off, whatever the port
  did. It is also wrong the other way: inside the lag window a reset can complete
  normally (`PR` 1->0, `PRC` set) while `PP` still reads 1, and a `PR` test
  calls that a preemption, reports it, and leaves a real `PRC` pending to be
  mis-attributed to the next armed operation. The same reading removes a special
  case rather than adding one: a disable can never end a reset, because PED
  "shall automatically be cleared to `0` when PR is set to `1`" and a written 1
  clears nothing on a bit that reads 0 - so the port never reaches Powered-off
  and the single `PP` confirmation already covers it.
- **Every mechanism built on `UsbPortRequestAsyncCallback` needs an independent
  age measure, because that service cannot report failing to arm.** It "returns 0
  on success and 0 when its pool allocation fails", and only a callback disarms a
  port or completes a command - so a failed allocation leaves the state armed for
  the life of the driver, refusing everything that follows on that object.

  The health poll is the measure, in milliseconds on `PollClockMs`
  (`XHCI_PORT_AGE_MS`; see "No threshold in this driver may be expressed as a
  count of health polls" under "Fatal Errors"), with a threshold well clear of
  the legitimate worst case.

  Retiring is not enough: finish the operation the way its timer would have,
  or the rescue trades one silent failure for another. A reset reports
  `C_PORT_RESET` so the hub driver stops waiting, and a resume owes its
  terminating U0 write, since a port left signalling resume is a port driving
  the bus. Keep the count inside the object it describes, zeroed by the act of
  arming, so it cannot be inherited by the next operation.
- **A per-port generation must survive a rebuild of the root hub.** usbport
  zeroes the miniport extension before every `StartController` and not before
  `ResumeController`, so a resume reuses the start epoch. If the generations
  restarted at zero when the shadow array was rebuilt, the first operation armed
  after a resume would carry the same epoch, hub port and generation as one armed
  just before the suspend - and Win98 idle-suspends within about half a second of
  a start, so that window is the ordinary case there. Carry the generation across
  a rebuild and advance it.
- **Two port operations are not single writes at all, and both need the armed
  generation plus uncancellable timeout: reset, and resume.**
  Reset is asynchronous and reports through PRC. Resume is worse - it is two
  writes with a mandatory gap: "for a USB2 protocol port, software shall write a
  `15` (Resume) to the PLS field to initiate resume signaling ... software shall
  ensure that resume is signaled for at least 20 ms (TDRSMDN) ... after TDRSMDN
  is complete, software shall write a `0` (U0) to the PLS field" (4.15.2.2,
  p.257), with the device-initiated path timing the same interval from the
  transition into Resume (4.15.2.1, p.256).

  A USB 3.x port writes U0 to initiate, in the same numbered list, so on a
  USB 2.0-only driver U0-first is the one value that cannot be right, and it
  fails silently, reporting success while the port stays in U3. Issuing only
  the first write is worse than refusing: resume signalling that nothing
  terminates leaves the port driving the bus.
- **A timeout and a minimum duration are not the same timer, and the two
  asynchronous port operations need one of each.** For a reset the timer is
  a deadline: PRC is the completion, and the timer exists only to recover a
  completion that never arrived, so the event and the timer may race and either
  may claim the operation.

  For a resume the timer is a floor: "software shall ensure that resume is
  signaled for at least 20 ms" (4.15.2.2, p.257) is a minimum duration of bus
  signalling, so the timer alone completes it and no Port Status Change Event
  may shorten it, not the PLC that announces entry into the Resume state, not
  a CSC from a mid-resume unplug. An event in that interval updates the shadow
  and leaves the operation armed.

  Reusing reset's race for resume inverts a minimum into a maximum: the U0
  write lands early, the device never wakes, and both sides report success.
  Late is safe and early is a protocol violation, so round the interval up.
  Concretely, `XHCI_PORT_RESUME_TIMER_MS` is T(DRSMDN) plus one clock tick:
  usbport's timer is a per-call `KeSetTimer` whose resolution is the system
  tick, and the interval this driver needs starts at the `PLS = 15` write
  rather than at the arm.
- **Suspend has a precondition, and the driver checks it.** "Software should not
  attempt to suspend a port unless the port reports that it is in the enabled
  (PED = `1`, PLS < `3`) state" (4.15.1, p.255). A suspend written to a disabled
  port, or to one already in U3 or Resume, has no defined outcome.
- **Bringing the controller back to D0 does not resume its ports.** "Any Root
  Hub port that is in the Resume or U3 state when the xHC is transitioned to the
  D0 power state shall require software to drive the port to the U0 state. The
  xHC shall not automatically transition a root hub port from the Resume or U3
  state to the U0 state" (4.15, p.254).

  The start's own seed is where that is discharged, synchronously: it runs at
  PASSIVE_LEVEL, where the 20 ms may simply be waited for, so it uses the same
  two builders as the callback path without the timer that exists only because
  a DISPATCH_LEVEL callback may not wait.

  It is expected to find nothing while a controller resume is a full
  reinitialisation (HCRST puts every port register back to its default), and
  starts to matter the moment a CSS/CRS restore replaces that. The seed pays
  T(DRSMDN) once for all such ports, since their intervals overlap and each is
  still at least the minimum, and `RhPortsDriventoU0` plus its vector are how
  the expected zero is checked rather than assumed.
- **The logical-to-physical port map is explicit, built once per start from the
  port classification.** usbport's port numbers are dense and 1-based over the
  managed ports; xHCI's interleave USB 2.0 and USB 3.x groups. Every
  arithmetic shortcut between them ("USB 2.0 ports come first", "hub port n is
  PORTSC n") is false on a controller that orders its groups the other way, and
  the reverse map must answer "not mine" for an unmanaged port rather than
  indexing a shadow that does not describe it.

## Starting and Stopping the Controller

- **R/S may only be set on a halted controller.** "Software shall not write a
  `1` to this flag unless the xHC is in the Halted state (i.e. HCH in the USBSTS
  register is `1`). Doing so may yield undefined results" (5.4.1, p.359). Check
  it rather than assuming the reset left it that way.
- Everything the controller will follow must be programmed first: MaxSlotsEn,
  DCBAAP, the command ring and the interrupter's event ring "shall be completed
  before setting the USBCMD register Run/Stop (R/S) bit to `1`" (4.2, p.69).
- **Record that R/S was written before writing it, not after it is confirmed.**
  The window in which the controller may be executing - and writing into the
  common buffer - opens at the write. A start that sets R/S and then fails its
  confirmation still has a controller to stop, and must not look to any later
  path like one that never started.
- **USBCMD can never be written as a literal.** Bits 6:4, 12 and 31:17 are
  RsvdP, and "software shall preserve the value read for writes to bits"
  (5.1.1, p.338). Every write to it - the halt, HCRST, R/S, the quiesce - reads
  first, carries the reserved fields back unchanged, and names the defined
  controls it wants set. That HCRST is about to reset the register is not an
  exception: RsvdP is a rule about the write, and a controller that implements
  something reserved sets it at reset like any other field.
- **Neither can any of the other five, and a composed write is the shape that
  hides it.** `USBCMD` and `IMAN` got the rule one register at a time in Phase 4;
  `CONFIG` (31:10, p.370), `DNCTRL` (31:16, p.367), `ERSTSZ` (31:16, p.393),
  `ERSTBA` (5:0, p.394) and `CRCR` (5:4, p.367) were still full-register stores
  composed from a layout - `CONFIG = MaxSlotsEn`, `CRCR = base | RCS`,
  `DNCTRL = 0` - and each cleared a reserved field. The field list is in
  `docs/usb-xhci-info/xhci-data-structures.md` sections 3 and 4, along with the
  two registers that have no reserved field at all (`ERDP`, `IMOD`) so that a
  plain write of one of those reads as a decision rather than a miss. The
  read-modify-write helpers are in `src/xhci_init.c`; `CRCR`'s are exported
  because the command engine's abort is a caller outside that file.
  - **An all-ones read is not a legal operand**, here as everywhere else: it is
    what an undecoding window answers, and preserving it publishes every
    reserved bit as `1`. Each helper refuses after a bounded retry and writes
    nothing; the init steps turn that into `XHCI_INIT_NO_RMW_OPERAND` and the
    restore into a failed restore. `IMAN`'s DIRQL literal fallback stays the one
    documented exception - see "Interrupt Ordering".
  - **`DCBAAP`'s low six bits are RsvdZ, not RsvdP**, and the ERST entry's DW3
    likewise: writing zero is what the specification asks for there, so those
    stay plain writes. Getting the two confused in either direction is a defect.
- **A stop must halt.** Once R/S is set, a `StopController` that only drops
  software state leaves the controller writing into memory `usbport.sys`
  reclaims. Mask `USBCMD.INTE` then `IMAN.IE`, clear R/S, and wait for HCHalted.
- **HCRST is not the escalation when a halt times out**: "software shall not set
  this bit to `1` when the HCHalted (HCH) bit in the USBSTS register is a `0`"
  (5.4.1, p.360).
- **HCRST is not part of a teardown that succeeded either.** After a confirmed
  halt it would be legal, it is the stronger of the two DMA proofs, and it
  would return every pointer register to its default in one write. It also
  undoes the step above it: an xHC "shall automatically enable VBus on all Root
  Hub ports after a Chip Hardware Reset or HCRST" (4.19.4, p.295), so it
  re-powers every port the teardown just took down - including the USB 3.x ports
  the port strategy requires unpowered - on a machine that no longer has a
  driver to serve whatever trains onto them. The halt is already a sufficient
  proof, so the reset buys a cleaner register image at the cost of the only
  user-visible thing the teardown achieves.
- **Do not try to tear the pointer registers down, and do not read that as an
  omission.** Zeroing DCBAAP, CRCR, ERSTBA and ERDP after the halt substitutes
  physical address 0 - real memory on x86 - for a common buffer `usbport.sys` is
  about to reclaim, and the one register that would make the result coherent
  cannot be written: "For the Primary Interrupter: Writing a value of `0` to
  this field shall result in undefined behavior of the Event Ring. The Primary
  Event Ring cannot be disabled" (ERSTSZ, 5.5.2.3.1, p.393). There is no way to
  say "this controller points at nothing". The halt is the statement, and the
  next start's HCRST is what actually clears them.
- **The ordered teardown is: port power off, then quiesce.** Everything else a
  stop owes - blocking new work, retiring the command generation and its
  uncancellable watchdog, masking `USBCMD.INTE` then `IMAN.IE`, closing ISR and
  DPC admission, clearing R/S, waiting for HCHalted, and proving DMA stopped -
  belongs to the quiesce, so suspend can share it unchanged: suspend
  wants all of that and none of the port pass. Every path after which usbport
  reclaims the common buffer runs the whole teardown, not the quiesce alone -
  the `StopController` callback and both exits of the init sequence that follow
  a written R/S.
- **A stop arriving on a suspended controller skips the port pass, and
  restarting the controller to reach the PORTSC writes is refused** (Phase 4
  task 8). usbport's measured clean shutdown is `SuspendController` ->
  `DisableInterrupts` -> `StopController(TRUE)` (`lessons.md`), so on the
  ordinary unload the stop always arrives halted with `RUNNING` and
  `INITIALIZED` both clear, and the pass is unreachable there; it is counted
  under `PortTeardownSkippedSuspended`, apart from the never-ran case, because a
  release build has no trace and the two must not read alike.

  Setting R/S again to reach the writes would run a controller whose common
  buffer usbport reclaims the moment the callback returns, put the most
  safety-critical path behind a second halt that can fail, and risk a bugcheck
  on a machine that was shutting down cleanly, all to honour a "should" about
  power consumption on the one path where the machine is about to lose power
  anyway. 4.19.4 is written about a driver disabled or unloaded while the
  machine keeps running, and there the controller is still running when the
  stop arrives, so the pass does run.

  The entry point does not restate the quiesce's admission check either: one
  gate per concern, applied where the concern is. The restatement hid this
  case.
- **There are exactly two proofs that DMA has stopped, and an all-ones register
  read is neither.** "DMA Teardown" above names them: a completed hardware reset,
  or PCI Bus Master Enable confirmed clear. All-ones says the memory window is
  not decoding, which a cleared Memory Space Enable produces on a device that is
  still perfectly able to master the bus - MSE and BME are different bits, and
  config space is reachable either way. So when the halt cannot be had, clear BME
  and read it back; a write that returned success is not the same as a bit
  that stayed clear.
- **A quiesce reports whether it succeeded, and keeps its "may be running" state
  until it did.** Clearing the flag on entry means a failed attempt looks
  identical to a controller that was never started, so the next stop silently
  agrees the buffer is safe. When neither proof is available, say so and count
  it - and then, on a path that reclaims, fail closed: the record alone was
  the first answer to this and it is not sufficient, because under Option A the
  miniport cannot withhold the common buffer from `usbport.sys`, so a counter
  explains a corruption it did nothing to stop. See "DMA Teardown" above for
  which paths reclaim and why suspend is not one of them.
- **Read PCI Command.BME on every start and require it, before running the
  controller.** A controller without bus mastering produces no command
  completions, no events and no transfers - it is indistinguishable from one
  that never answers, which is the hardest failure to diagnose remotely. Checking
  it only when the driver's own records say it cleared the bit trusts bookkeeping
  instead of hardware: BME clear for any other reason (a power transition,
  firmware or a bus driver that never enabled it) then sails straight through to
  R/S. The check needs one config read, so it belongs in the preflight beside the
  INTx gate, where a refusal costs the machine nothing.
- **Restore BME only if the quiesce took it.** PCI sets that bit when the bus
  driver starts the device, and a stop/start pair is not a PnP restart, so a bit
  this driver cleared is a bit this driver must put back - with a read-back to
  confirm. A BME that is clear for somebody else's reason is a refusal, not a
  repair: configuration space belongs to the bus driver and usbport, and the
  one exception claimed here is undoing this driver's own act.
- **An unreadable Command register refuses, and the INTx gate's leniency does not
  transfer to it.** That gate can treat an unreadable Interrupt Pin as a service
  failure rather than a statement about the hardware because a second,
  independent witness exists: PCI derives a device's interrupt resource from
  that same register, so `USBPORT_RESOURCES_INTERRUPT` - already required - says
  a pin is there. Bus mastering has no such witness;
  `USBPORT_RESOURCES_MEMORY` says a window was assigned and nothing about
  mastering.

  So a Command register that cannot be read leaves the driver knowing nothing
  about the one bit the check exists to establish, and continuing is the
  failure being guarded against wearing the costume of tolerance. Copying a
  rule's shape is not the same as inheriting its precondition: check the
  evidence the original rested on before reusing it.

## Suspend and Resume

- **The suspend halts the controller.** Windows 2000 performs real D-state
  transitions, and a running xHC entering D3 is doing DMA into memory nobody
  expects it to touch; spec 4.23.2 (p.313) begins its power-down sequence by
  stopping the controller for that reason.
- **A halt is not a loss of state, which is what makes this affordable on
  Win98.** "The internal state of the xHC shall be valid until it enters the
  D3cold state ... If prior to setting the xHC into the D3cold state, software
  decides to restart the xHC, then a Restore State operation is not required"
  (4.23.2, p.314). Win98's NUSB `usbport.sys` issues
  `SuspendController`/`ResumeController` pairs repeatedly, as idle behaviour
  (measured in the Phase 3 spike; native Win2000 `usbport.sys` never idle-
  suspended at all), and an idle pair that never reaches D3cold therefore costs
  a halt and a restart - not a re-enumeration.
- **The suspend masks the interrupt enables itself, and must not wait to be
  asked.** `DisableInterrupts` was observed around the shutdown sequence, but
  nothing observed says the idle pairs are bracketed the same way. A suspend that
  leaves `IMAN.IE` set while dropping the flag the ISR tests produces an
  interrupt this driver declines and nobody else can claim - the line stays
  asserted with no owner. The same applies whatever state the controller was in:
  do not assume the halt path will do the masking on its way past, because it is
  a no-op on a controller that is not running.
- **Nothing may read a register while suspended.** Drop whatever flag the ISR and
  DPC test before touching MMIO, for the duration.
- **The resume tries `Restore State` first and falls back to a full bounded
  reinitialization.** The suspend saves with `CSS` (`xhciSaveState`), the resume
  attempts `CRS` (`xhciRestoreState`), and `USBSTS.SRE` is the only success
  test. A restore that sets `SRE`, times out, or cannot meet the preconditions
  falls through to the reinitialization, which is unchanged and is still the
  thing that is correct whatever the controller did: re-derive the capability
  registers and reprogram every pointer register from the common buffer, which
  `usbport.sys` does not reclaim across a suspend. Restore the interrupt enables
  afterwards if usbport had them on, since the initialization path
  leaves them masked.
- **The fallback is the measured path, not the exception.** QEMU sets `SRE` on
  every restore (batch 6-0), so both target VMs exercise the error path and the
  reinitialization carries every resume there. Do not let the restore path's
  correctness rest on VM evidence.
- **A restore does not bring back what it never covered.** "The state of a Root
  Hub port is not covered by a Save or Restore operation" (p.315), so the port
  shadow is rebuilt either way - so `XhciRootHubBuild` clears every
  per-tenancy field rather than carrying a reading across. The
  flags themselves carry internal Slot, Endpoint and Stream state across D3cold
  (4.23.2.1, p.315), and the register-image save and restore that make `CRS`
  work at all are 4.23.2 step 4 (p.313 and p.314).
- **`Save State` is declined outright unless the controller declares FSC.**
  4.23.2 p.313 step 1 is two clauses, not one: Stop Endpoint on every Busy
  endpoint in the Running state, "and, if FSC = `0`, ... for all Idle
  endpoints in the Running state as well", because that command is what makes the
  xHC write the TR Dequeue Pointer and DCS back into the endpoint context.

  The driver's quiescence gate covers the first clause and cannot cover the
  second: an empty software queue is what an Idle-but-Running endpoint looks
  like from the miniport, so the gate passes the very set the clause names.
  The note under step 3 confirms the division from the other side by
  conditioning "idle is enough" on `FSC = '1'`.

  Until a suspend-path Stop Endpoint exists (task 7a-B.1's command, but a
  synchronous wait on a controller whose interrupts are already masked), the
  only conforming answer on an `FSC = 0` controller is not to save. The resume
  then reinitialises, which this project already treats as correct-but-slower.
  - The pass is not going to be built; the behaviour is published instead
    (task 12.1; `docs/using/release-notes.md`, the standby entry under "Known limitations"). The reasoning is
    what makes the rule above permanent rather than interim: the pass is a synchronous wait for N command
    completions at PASSIVE on a controller this very path has already masked the
    interrupts of, i.e. new machinery on the one path no vehicle in this
    project can exercise - QEMU implements `CRS` as `usbsts |= SRE`, so a
    successful save/restore has never executed anywhere. Do not read the
    published limitation as a decision that the pass would be wrong; it is a
    decision that untestable code on a silently-failing path is the worse risk.
    Reopening it means reopening the task.
  - **A command completion is matched by physical address and nothing else, so
    rebuilding the command ring makes every undrained event ambiguous.** The
    restore reinitialises the ring from TRB zero (step 6), which hands the next
    command the address an abandoned one had; a Command Completion Event still
    on the event ring would then match it and retire the wrong command,
    carrying another command's completion code and Slot ID into the slot
    layer. `CommandGeneration` and `StartEpoch` do not help: they arm the
    watchdog and are not carried on the event.

    The rule: receive the ring's pending events before the first command of a
    new incarnation is issued. The restore does it with
    `XhciEventDiscardStale`, after the rebuild and before R/S, which is save
    step 2's "all Command Completion Events ... have been received" performed
    late rather than skipped.

    Dropping them is right
    only because nothing has executed since the save, their owners were
    already told by `XhciSlotCommandLost`, and `XhciRootHubInit` re-reads
    every PORTSC immediately afterwards. The residue is not harmless.
  - **Those three reasons name three event types, and a drain that applies them
    to a fourth is discarding something nobody argued was discardable.** A Host
    Controller Event is a controller-level fault whose two commonest codes,
    Event Ring Full and Event Lost, set neither `USBSTS.HCE` nor `HSE`, so no
    register poll reports them; the save gate looks at endpoint queues and the
    command engine, never at the ring's contents, so nothing excludes one from
    being there. The rule: a bulk drain must classify.
  - **Classifying one type is not enough either.** Event Lost is not reported
    only as a Host Controller Event; that is true of Event Ring Full and false
    of Event Lost. A TD-related one is "generated for the endpoint" and "shall
    halt the endpoint" (4.10.1, p.173), arriving as a Transfer Event with
    completion code 32, the one transfer code `xhciXferCodeInfo` marks
    `Fatal`.

    Nor does the save gate establish that no ordinary Transfer Event
    can be outstanding: the gate counts software queue entries, and
    `XhciAbortTransfer` empties that queue while the xHC may still own the
    TRBs (`AbortsBeforeStopped` counts that window). Dropping an ordinary
    Transfer Event is still right, but because usbport withdrew the transfer,
    not because none exists. The rule: when a discard rests on a gate
    elsewhere, read the gate, not its summary.
  - **A classification that enumerates is a classification that goes stale.**
    Every hand-written list of fatal codes in this driver has come up short.
    Table 6-90 marks Undefined Error (33) fatal in as many words (p.469),
    requires an unrecognised vendor error (192-223) to be read as that
    condition, and makes Incompatible Device Error (22) fatal to the slot with
    a Disable Slot as its named recovery (p.468), which a successful restore
    would carry past by preserving the very slot that owes one. The drain
    holds no list: it asks `XhciXferCodeInfo`, where Table 6-90 is
    transcribed, and it asks for Command Completion Events as well, because
    the table's fatal codes are not scoped to transfers.
  - Finding any fatal event fails the restore, which drops the resume into its
    reinitialisation (HCRST, a fresh event ring, every device rebuilt). That
    is the repair the "Fatal Errors" rule asks for, reached without involving
    usbport. Vendor-defined and out-of-range types stay droppable: "advance
    past and ignore" is the specification's own instruction for the first
    (4.11.6, p.212) and this driver has never acted on either. The two
    counters partition rather than overlap: a fatal event is counted in
    `RestoreEventsFatal` and not in `RestoreEventsDiscarded`, because an event
    that refuses the restore is an event acted on. `RestoreFatalKind` and
    `RestoreFatalCode` say which kind ended it and which code it carried;
    `LastHostControllerCode` alone, written by one of the arms, cannot.
  - **The save gate reads endpoint quiescence, not just queue counts.** Step 1
    asks about Busy endpoints ("Stop all USB activity by issuing Stop Endpoint
    Commands for Busy endpoints in the Running state", 4.23.2 p.313) and an
    empty software queue does not answer it: an abort empties the queue while
    the xHC may still own the TRBs. The gate declines while any endpoint
    carries `XHCI_EPQ_INFLIGHT`, `XHCI_EPQ_FAILED` or a position debt
    (`XHCI_EPQ_REPOSITION`, `XHCI_EPQ_FORCE_DEQUEUE`). The cost is a resume
    that reinitialises where it might have restored; the alternative is a
    saved image of a controller still executing a ring this driver has since
    rewritten.

    The synchronous Stop Endpoint pass of task 12.1 is what would have relaxed
    it, and that task chose to publish rather than build, so this gate is
    settled, including its sticky half: an ordinary `XHCI_EPQ_FAILED`
    is cleared only by a client reset-pipe that nothing obliges anyone to
    send, so one failed stop can cost a controller every later `CSS` for the
    life of the driver, on hardware that declares FSC.

    `EndpointQuiesceFailures` is the counter that says a machine has been in
    that state. It counts entries, not endpoints currently stuck, and the
    reset-pipe clears `XHCI_EPQ_FAILED` without decrementing it. So nonzero is
    historical evidence that the state was entered and that a declined `CSS`
    has an explanation; it is not a live reading that saves are still being
    declined, and reading it as one would report a controller as permanently
    degraded after it had recovered.

    Nothing in the driver publishes the live
    answer, by design: the question the limitation needs answered is "did
    this happen here", which is the cumulative one. It is counted inside
    `xhciEpQuiesceFail`, the only site that sets `XHCI_EPQ_FAILED` without
    `XHCI_EPQ_UNAVAILABLE`, i.e. the state this gate declines on.
    `EndpointQuiesceLost` and `EndpointQuiesceUnavailable` each name one route
    and neither answers it.
  - **FSC is read from `HCCPARAMS2` (Base + 1Ch, bit 2), and the gate is reach,
    not version.** Do not also require HCIVERSION >= 1.10 on the grounds
    that the register arrived in xHCI 1.1; Appendix H.1 says why that is wrong: it lists the capabilities "that were optional for xHCI
    1.0 implementations [and] are now required in xHCI 1.1 implementations", and
    H.1.6 is FSC (p.593) - as are U3C, CTC and CIC, three more bits of the same
    register. A 1.0 controller answers this bit honestly and may answer 1, so a
    version gate would force `FSC = 0` on hardware that had just said otherwise,
    and the cost of a wrong `0` is that every resume reinitialises the bus.

    What is checked instead is that the register is reachable at all
    (`CAPLENGTH` and the mapped window must both extend to `0x20`), plus the
    usual all-ones refusal. The read itself is outside
    `XHCI_CAP_REGISTERS_BYTES`, so it is bounded against `IoSpaceLength` at
    the call site before the derivation runs.
  - **Which fleet controllers decline is not known, and must not be inferred from
    HCIVERSION.** `xhciqual/results/` establishes that the E460 and the P14s Gen 1 report
    HCIVERSION 1.00 with `CAPLENGTH = 80h` - the register is reachable on both -
    and says nothing about their FSC bit, because the qualifier printed
    HCCPARAMS1 and not HCCPARAMS2 when those runs were taken. The qualifier now prints
    the register, so the answer costs one `XHCIQUAL xhci --probe-only` per
    machine and no fleet run has to be re-planned around it.

    `qemu-xhci` is the one measured controller and it reads
    `HCCPARAMS2 = 00000000`, i.e. `FSC = 0`, taken with that build the same
    day, so every `CSS` on both target VMs is declined before the halt check
    is even reached. On a running machine `SavesDeclinedNoFsc` is the answer;
    a nonzero value means every resume there reinitialises.

    `scripts\vm-matrix\gen-offsets.ps1`
    derives the readable field set from the print sites, so a counter with no
    print site cannot be read out of a guest at all; "add the counter" and
    "make the counter readable" are two separate pieces of work.
- **The restore performs steps 6 and 7 - reinitialise the command ring, then
  write `CRCR` - between `CRS` and `R/S`.** Omitting them is not "leave the
  pointer alone": the specification's alternative for an unwritten `CRCR` is that
  "the Command Ring shall begin fetching Command TRBs using the current value of
  the internal Command Ring CCS flag" (Table 5-24, p.367), reloaded from the saved
  image, which the driver's software ring has no reason to agree with. The next
  command is then ignored or a stale TRB is consumed, and the watchdog escalates
  to a controller reset.

  Discarding the ring's contents is legal only because `xhciSaveState` refuses
  unless the command engine is IDLE, which the suspend's quiesce has already
  guaranteed by abandoning any outstanding command, so that refusal is a guard
  on this step rather than a branch anything takes. The ordering is the
  specification's: after `CRS`, which would otherwise overwrite it, and before
  `R/S`, after which `CRR = 1` makes the write ignored.
  - **Unexercised by construction on every run this project has taken.** QEMU
    implements `CRS` as `usbsts |= SRE`, so both target VMs take the error path
    and never reach these lines. A result box closing this says so; a green host
    suite is a statement about the model.
- **Retiring the armed port generations lives in the quiesce transition**
  (`XhciRootHubRetireOperations` under `XhciControllerBeginQuiesce`), not in the
  two lifecycle callbacks - because a stop, a suspend and a failed-start
  teardown all reach the quiesce and only the quiesce. It advances every port's
  generation, so every uncancellable timer is stale before it fires, and drops
  `RootHubInvalidatesOwed`, which has nobody left to announce to and whose
  changes the next start's or resume's seed re-reads from hardware anyway
  .
- **`Get32BitFrameNumber` answers a delta and advances while halted.** MFINDEX
  is eleven bits of frame and restarts at zero after HCRST, so an absolute
  reading goes backwards twice a second; and usbport's post-open wait is
  uncapped and compares against a frame stamped before the suspend, so a reader
  that froze on a suspended controller would hang the enumerating thread. A
  controller that cannot be read is therefore answered with an increment, which
  is the safe direction: no traffic can be in flight on a halted xHC.

  That stall path advances one per call, not per frame, so the published axis
  is not by itself congruent to MFINDEX, and an isochronous Frame ID derived
  from a usbport stamp would have named a different, real frame 2,047 times
  out of 2,048. The resync therefore advances the number forward
  to the next value congruent to MFINDEX's Frame Index (at most 2,047 frames,
  never backwards, so the monotonicity usbport depends on is untouched);
  `FrameCongruent` says when the claim holds and `FrameResyncSkew` measures how
  much of the axis the stall path invented.
